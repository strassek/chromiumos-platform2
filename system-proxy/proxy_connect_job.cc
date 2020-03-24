// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "system-proxy/proxy_connect_job.h"

#include <algorithm>
#include <utility>
#include <vector>

#include <curl/curl.h>
#include <curl/easy.h>

#include <base/base64.h>
#include <base/bind.h>
#include <base/bind_helpers.h>
#include <base/callback_helpers.h>
#include <base/files/file_util.h>
#include <base/strings/stringprintf.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <base/time/time.h>
#include <brillo/http/http_transport.h>
#include <chromeos/patchpanel/net_util.h>
#include <chromeos/patchpanel/socket.h>
#include <chromeos/patchpanel/socket_forwarder.h>

#include "system-proxy/curl_socket.h"

// The libarcnetwork-util library overloads << for socket data structures.
// By C++'s argument-dependent lookup rules, operators defined in a
// different namespace are not visible. We need the using directive to make
// the overload available this namespace.
using patchpanel::operator<<;

namespace {
// There's no RFC recomandation for the max size of http request headers but
// popular http server implementations (Apache, IIS, Tomcat) set the lower limit
// to 8000.
constexpr int kMaxHttpRequestHeadersSize = 8000;
constexpr char kConnectMethod[] = "CONNECT";
constexpr char kHttpScheme[] = "http://";
constexpr base::TimeDelta kCurlConnectTimeout = base::TimeDelta::FromMinutes(2);
constexpr size_t kMaxBadRequestPrintSize = 120;

// HTTP error codes and messages with origin information for debugging (RFC723,
// section 6.1).
const std::string_view kHttpBadRequest =
    "HTTP/1.1 400 Bad Request - Origin: local proxy\r\n\r\n";
const std::string_view kHttpInternalServerError =
    "HTTP/1.1 500 Internal Server Error - Origin: local proxy\r\n\r\n";
const std::string_view kHttpBadGateway =
    "HTTP/1.1 502 Bad Gateway - Origin: local proxy\r\n\r\n";

static size_t WriteCallback(char* contents,
                            size_t size,
                            size_t nmemb,
                            void* userp) {
  for (int i = 0; i < nmemb * size; ++i) {
    ((std::vector<char>*)userp)->push_back(contents[i]);
  }
  return size * nmemb;
}

// Parses the first line of the http CONNECT request and extracts the target
// url. The destination URI is specified in the request line as the host name
// and destination port number separated by a colon (RFC2817, section 5.2):
//      CONNECT server.example.com:80 HTTP/1.1
// If the first line in |raw_request| (the Request-Line) is a correctly formed
// CONNECT request, it will return the destination URI as scheme://host:port,
// otherwise it will return an empty string.
std::string GetUrlFromHttpHeader(const std::vector<char>& raw_request) {
  base::StringPiece request(raw_request.data(), raw_request.size());
  // Request-Line ends with CRLF (RFC2616, section 5.1).
  size_t i = request.find_first_of("\r\n");
  if (i == base::StringPiece::npos)
    return std::string();
  // Elements are delimited by non-breaking space (SP).
  auto pieces =
      base::SplitString(request.substr(0, i), " ", base::TRIM_WHITESPACE,
                        base::SPLIT_WANT_NONEMPTY);
  // Request-Line has the format: Method SP Request-URI SP HTTP-Version CRLF.
  if (pieces.size() < 3)
    return std::string();
  if (pieces[0] != kConnectMethod)
    return std::string();

  return base::JoinString({kHttpScheme, pieces[1]}, "");
}
}  // namespace

namespace system_proxy {

ProxyConnectJob::ProxyConnectJob(
    std::unique_ptr<patchpanel::Socket> socket,
    const std::string& credentials,
    ResolveProxyCallback resolve_proxy_callback,
    OnConnectionSetupFinishedCallback setup_finished_callback)
    : credentials_(credentials),
      resolve_proxy_callback_(std::move(resolve_proxy_callback)),
      setup_finished_callback_(std::move(setup_finished_callback)) {
  client_socket_ = std::move(socket);
}

ProxyConnectJob::~ProxyConnectJob() = default;

bool ProxyConnectJob::Start() {
  // Make the socket non-blocking.
  if (!base::SetNonBlocking(client_socket_->fd())) {
    PLOG(ERROR) << *this << " Failed to mark the socket as non-blocking.";
    client_socket_->SendTo(kHttpInternalServerError.data(),
                           kHttpInternalServerError.size());
    return false;
  }
  read_watcher_ = base::FileDescriptorWatcher::WatchReadable(
      client_socket_->fd(),
      base::Bind(&ProxyConnectJob::OnClientReadReady, base::Unretained(this)));
  return true;
}

void ProxyConnectJob::OnClientReadReady() {
  // Stop watching.
  read_watcher_.reset();
  // The first message should be a HTTP CONNECT request.
  std::vector<char> connect_request;
  if (!TryReadHttpHeader(&connect_request)) {
    std::string encoded;
    base::Base64Encode(
        base::StringPiece(connect_request.data(), connect_request.size()),
        &encoded);
    LOG(ERROR) << *this
               << " Failure to read proxy CONNECT request. Base 64 encoded "
                  "request message from client: "
               << encoded;
    OnError(kHttpBadRequest);
    return;
  }

  target_url_ = GetUrlFromHttpHeader(connect_request);
  if (target_url_.empty()) {
    LOG(ERROR)
        << *this
        << " Failed to extract target url from the HTTP CONNECT request.";
    OnError(kHttpBadRequest);
    return;
  }

  std::move(resolve_proxy_callback_)
      .Run(target_url_, base::Bind(&ProxyConnectJob::OnProxyResolution,
                                   base::Unretained(this)));
}

bool ProxyConnectJob::TryReadHttpHeader(std::vector<char>* raw_request) {
  // Used to identify the end of a HTTP header which should be an empty line.
  // Note: all HTTP header lines end with CRLF. HTTP connect requests don't have
  // a body so end of header is end of request.
  std::string crlf_crlf = "\r\n\r\n";
  size_t read_byte_count = 0;
  raw_request->resize(kMaxHttpRequestHeadersSize);

  // Read byte-by-byte and stop when reading an empty line (only CRLF) or when
  // exceeding the max buffer size.
  // TODO(acostinas, chromium:1064536) This may have some measurable performance
  // impact. We should read larger blocks of data, consume the HTTP headers,
  // cache the tunneled payload that may have already been included (e.g. TLS
  // ClientHello) and send it to server after the connection is established.
  while (read_byte_count < kMaxHttpRequestHeadersSize) {
    if (client_socket_->RecvFrom(raw_request->data() + read_byte_count, 1) <=
        0) {
      raw_request->resize(std::min(read_byte_count, kMaxBadRequestPrintSize));
      return false;
    }
    ++read_byte_count;

    // Check if we have an empty line.
    if (read_byte_count > crlf_crlf.size() &&
        std::memcmp(crlf_crlf.data(),
                    raw_request->data() + read_byte_count - crlf_crlf.size(),
                    crlf_crlf.size()) == 0) {
      raw_request->resize(read_byte_count);
      return true;
    }
  }
  return false;
}

void ProxyConnectJob::OnProxyResolution(
    const std::list<std::string>& proxy_servers) {
  proxy_servers_ = proxy_servers;
  DoCurlServerConnection(proxy_servers.front());
}

void ProxyConnectJob::DoCurlServerConnection(const std::string& proxy_url) {
  CURL* easyhandle = curl_easy_init();
  CURLcode res;
  int newSocket = -1;
  std::vector<char> server_connect_reply;

  if (!easyhandle) {
    // Unfortunately it's not possible to get the failure reason.
    LOG(ERROR) << *this << " Failure to create curl handle.";
    curl_easy_cleanup(easyhandle);
    OnError(kHttpInternalServerError);
    return;
  }
  curl_easy_setopt(easyhandle, CURLOPT_URL, target_url_.c_str());

  if (proxy_url != brillo::http::kDirectProxy) {
    curl_easy_setopt(easyhandle, CURLOPT_PROXY, proxy_url.c_str());
    curl_easy_setopt(easyhandle, CURLOPT_HTTPPROXYTUNNEL, 1L);
    curl_easy_setopt(easyhandle, CURLOPT_CONNECT_ONLY, 1);
    // Allow libcurl to pick authentication method. Curl will use the most
    // secure one the remote site claims to support.
    curl_easy_setopt(easyhandle, CURLOPT_PROXYAUTH, CURLAUTH_ANY);
    curl_easy_setopt(easyhandle, CURLOPT_PROXYUSERPWD, credentials_.c_str());
  }
  curl_easy_setopt(easyhandle, CURLOPT_CONNECTTIMEOUT_MS,
                   kCurlConnectTimeout.InMilliseconds());
  curl_easy_setopt(easyhandle, CURLOPT_HEADERFUNCTION, WriteCallback);
  curl_easy_setopt(easyhandle, CURLOPT_HEADERDATA, server_connect_reply.data());

  res = curl_easy_perform(easyhandle);

  if (res != CURLE_OK) {
    LOG(ERROR) << *this << " curl_easy_perform() failed with error: ",
        curl_easy_strerror(res);
    curl_easy_cleanup(easyhandle);
    OnError(kHttpInternalServerError);
    return;
  }
  // Extract the socket from the curl handle.
  res = curl_easy_getinfo(easyhandle, CURLINFO_ACTIVESOCKET, &newSocket);
  if (res != CURLE_OK) {
    LOG(ERROR) << *this << " Failed to get socket from curl with error: "
               << curl_easy_strerror(res);
    curl_easy_cleanup(easyhandle);
    OnError(kHttpBadGateway);
    return;
  }

  ScopedCurlEasyhandle scoped_handle(easyhandle, FreeCurlEasyhandle());
  auto server_conn = std::make_unique<CurlSocket>(base::ScopedFD(newSocket),
                                                  std::move(scoped_handle));

  // Send the server reply to the client. If the connection is successful, the
  // reply should be "HTTP/1.1 200 Connection Established".
  client_socket_->SendTo(server_connect_reply.data(),
                         server_connect_reply.size());

  auto fwd = std::make_unique<patchpanel::SocketForwarder>(
      base::StringPrintf("%d-%d", client_socket_->fd(), server_conn->fd()),
      std::move(client_socket_), std::move(server_conn));
  // Start forwarding data between sockets.
  fwd->Start();
  std::move(setup_finished_callback_).Run(std::move(fwd), this);
}

void ProxyConnectJob::OnError(const std::string_view& http_error_message) {
  client_socket_->SendTo(http_error_message.data(), http_error_message.size());
  std::move(setup_finished_callback_).Run(nullptr, this);
}

std::ostream& operator<<(std::ostream& stream, const ProxyConnectJob& job) {
  stream << "{fd: " << job.client_socket_->fd();
  if (!job.target_url_.empty()) {
    stream << ", url: " << job.target_url_;
  }
  stream << "}";
  return stream;
}

}  // namespace system_proxy
