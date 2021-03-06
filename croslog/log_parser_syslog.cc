// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "croslog/log_parser_syslog.h"

#include <memory>
#include <string>
#include <utility>

#include "base/strings/string_number_conversions.h"

namespace {
// The length of time string like "2020-05-25T00:00:00.000000+00:00".
constexpr size_t kTimeStringLength = 32;
}  // namespace

namespace croslog {

LogParserSyslog::LogParserSyslog() = default;

MaybeLogEntry LogParserSyslog::Parse(std::string&& entire_line) {
  if (entire_line.empty()) {
    // Returns an invalid value if the line is invalid or empty.
    return base::nullopt;
  }

  if (entire_line.size() < kTimeStringLength) {
    LOG(WARNING) << "The line is too short: looks non-RFC5424 format?";
    return base::nullopt;
  }

  // Extract 32 chars from the beginning.
  std::string log_time = entire_line.substr(0, kTimeStringLength);

  base::Time time;
  bool result = base::Time::FromString(log_time.c_str(), &time);
  if (!result) {
    LOG(WARNING) << "The line has incorrect time format.";
    return base::nullopt;
  }

  int pos = kTimeStringLength;

  std::string severity_str;
  DCHECK_EQ(' ', entire_line[pos]);
  if (entire_line[pos] == ' ') {
    for (int i = pos + 1; i < entire_line.size(); i++) {
      if (entire_line[i] == ' ') {
        severity_str = entire_line.substr(pos + 1, i - pos - 1);
        pos = i;
        break;
      }
    }
  }

  Severity severity = Severity::UNSPECIFIED;
  if (!severity_str.empty()) {
    severity = SeverityFromString(severity_str);
  }

  std::string tag;
  if (entire_line[pos] == ' ') {
    for (int i = pos + 1; i < entire_line.size(); i++) {
      if (entire_line[i] == '[' || entire_line[i] == ':' ||
          entire_line[i] == ' ') {
        tag = entire_line.substr(pos + 1, i - pos - 1);
        pos = i;
        break;
      }
    }
  }

  int pid = -1;
  if (entire_line[pos] == '[') {
    for (int i = pos + 1; i < entire_line.size(); i++) {
      if (entire_line[i] == ']') {
        std::string pid_str = entire_line.substr(pos + 1, i - pos - 1);
        if (!base::StringToInt(pid_str, &pid))
          pid = -1;
        pos = i;
        break;
      }
    }
    DCHECK_EQ(']', entire_line[pos]);
    pos++;
  }

  if (entire_line[pos] == ':')
    pos++;
  DCHECK_EQ(' ', entire_line[pos]);
  pos++;

  std::string message = entire_line.substr(pos, entire_line.size() - pos);

  return LogEntry{time, severity,           std::move(tag),
                  pid,  std::move(message), std::move(entire_line)};
}

}  // namespace croslog
