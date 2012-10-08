// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "login_manager/session_manager_service.h"

#include <dbus/dbus-glib-lowlevel.h>
#include <errno.h>
#include <fcntl.h>
#include <glib.h>
#include <grp.h>
#include <secder.h>
#include <signal.h>
#include <stdio.h>
#include <sys/errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <utility>
#include <vector>

#include <base/basictypes.h>
#include <base/bind.h>
#include <base/callback.h>
#include <base/command_line.h>
#include <base/file_path.h>
#include <base/file_util.h>
#include <base/logging.h>
#include <base/memory/scoped_ptr.h>
#include <base/message_loop_proxy.h>
#include <base/stl_util.h>
#include <base/string_util.h>
#include <chromeos/dbus/dbus.h>
#include <chromeos/dbus/error_constants.h>
#include <chromeos/dbus/service_constants.h>
#include <chromeos/utility.h>

#include "login_manager/child_job.h"
#include "login_manager/device_management_backend.pb.h"
#include "login_manager/interface.h"
#include "login_manager/key_generator.h"
#include "login_manager/login_metrics.h"
#include "login_manager/nss_util.h"
#include "login_manager/policy_store.h"

// Forcibly namespace the dbus-bindings generated server bindings instead of
// modifying the files afterward.
namespace login_manager {  // NOLINT
namespace gobject {  // NOLINT
#include "login_manager/server.h"
}  // namespace gobject
}  // namespace login_manager

namespace em = enterprise_management;
namespace login_manager {

using std::make_pair;
using std::pair;
using std::string;
using std::vector;

// Jacked from chrome base/eintr_wrapper.h
#define HANDLE_EINTR(x) ({ \
  typeof(x) __eintr_result__; \
  do { \
    __eintr_result__ = x; \
  } while (__eintr_result__ == -1 && errno == EINTR); \
  __eintr_result__;\
})

int g_shutdown_pipe_write_fd = -1;
int g_shutdown_pipe_read_fd = -1;

// PolicyService::Completion implementation that forwards the result to a DBus
// invocation context.
class DBusGMethodCompletion : public PolicyService::Completion {
 public:
  // Takes ownership of |context|.
  DBusGMethodCompletion(DBusGMethodInvocation* context);
  virtual ~DBusGMethodCompletion();

  virtual void Success();
  virtual void Failure(const PolicyService::Error& error);

 private:
  DBusGMethodInvocation* context_;

  DISALLOW_COPY_AND_ASSIGN(DBusGMethodCompletion);
};

DBusGMethodCompletion::DBusGMethodCompletion(DBusGMethodInvocation* context)
    : context_(context) {
}

DBusGMethodCompletion::~DBusGMethodCompletion() {
  if (context_) {
    NOTREACHED() << "Unfinished DBUS call!";
    dbus_g_method_return(context_, false);
  }
}

void DBusGMethodCompletion::Success() {
  dbus_g_method_return(context_, true);
  context_ = NULL;
  delete this;
}

void DBusGMethodCompletion::Failure(const PolicyService::Error& error) {
  SystemUtils system;
  system.SetAndSendGError(error.code(), context_, error.message().c_str());
  context_ = NULL;
  delete this;
}

size_t SessionManagerService::kCookieEntropyBytes = 16;

// static
// Common code between SIG{HUP, INT, TERM}Handler.
void SessionManagerService::GracefulShutdownHandler(int signal) {
  // Reinstall the default handler.  We had one shot at graceful shutdown.
  struct sigaction action;
  memset(&action, 0, sizeof(action));
  action.sa_handler = SIG_DFL;
  RAW_CHECK(sigaction(signal, &action, NULL) == 0);

  RAW_CHECK(g_shutdown_pipe_write_fd != -1);
  RAW_CHECK(g_shutdown_pipe_read_fd != -1);

  size_t bytes_written = 0;
  do {
    int rv = HANDLE_EINTR(
        write(g_shutdown_pipe_write_fd,
              reinterpret_cast<const char*>(&signal) + bytes_written,
              sizeof(signal) - bytes_written));
    RAW_CHECK(rv >= 0);
    bytes_written += rv;
  } while (bytes_written < sizeof(signal));

  RAW_LOG(INFO,
          "Successfully wrote to shutdown pipe, resetting signal handler.");
}

// static
void SessionManagerService::SIGHUPHandler(int signal) {
  RAW_CHECK(signal == SIGHUP);
  RAW_LOG(INFO, "Handling SIGHUP.");
  GracefulShutdownHandler(signal);
}
// static
void SessionManagerService::SIGINTHandler(int signal) {
  RAW_CHECK(signal == SIGINT);
  RAW_LOG(INFO, "Handling SIGINT.");
  GracefulShutdownHandler(signal);
}

// static
void SessionManagerService::SIGTERMHandler(int signal) {
  RAW_CHECK(signal == SIGTERM);
  RAW_LOG(INFO, "Handling SIGTERM.");
  GracefulShutdownHandler(signal);
}

const uint32 SessionManagerService::kMaxEmailSize = 200;
const char SessionManagerService::kEmailSeparator = '@';
const char SessionManagerService::kLegalCharacters[] =
    "abcdefghijklmnopqrstuvwxyz"
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    ".@1234567890-+_";
const char SessionManagerService::kIncognitoUser[] = "";
const char SessionManagerService::kDemoUser[] = "demouser";
const char SessionManagerService::kDeviceOwnerPref[] = "cros.device.owner";
const char SessionManagerService::kFirstBootFlag[] = "--first-boot";
const char SessionManagerService::kTestingChannelFlag[] =
    "--testing-channel=NamedTestingInterface:";
const char SessionManagerService::kStarted[] = "started";
const char SessionManagerService::kStopping[] = "stopping";
const char SessionManagerService::kStopped[] = "stopped";
const char SessionManagerService::kFlagFileDir[] = "/var/run/session_manager";
const char *SessionManagerService::kValidSessionServices[] = {
  "tor",
  NULL
};

const char SessionManagerService::kLoggedInFlag[] =
    "/var/run/session_manager/logged_in";
const char SessionManagerService::kResetFile[] =
    "/mnt/stateful_partition/factory_install_reset";

// TODO(mkrebs): Remove CollectChrome timeout and file when
// crosbug.com/5872 is fixed.
// When crash-reporter based crash reporting of Chrome is enabled
// (which should only be during test runs) we use
// kKillTimeoutCollectChrome instead of the kill timeout specified at
// the command line.
const int SessionManagerService::kKillTimeoutCollectChrome = 60;
const char SessionManagerService::kCollectChromeFile[] =
    "/mnt/stateful_partition/etc/collect_chrome_crashes";

namespace {

// A buffer of this size is used to parse the command line to restart a
// process like restarting Chrome for the guest mode.
const int kMaxArgumentsSize = 1024 * 8;

// File that contains world-readable machine statistics. Should be removed once
// the user session starts.
const char kMachineInfoFile[] = "/tmp/machine-info";

}  // namespace

void SessionManagerService::TestApi::ScheduleChildExit(pid_t pid, int status) {
  session_manager_service_->message_loop_->PostTask(
      FROM_HERE,
      base::Bind(&HandleBrowserExit,
                 pid,
                 status,
                 reinterpret_cast<void*>(session_manager_service_)));
}

SessionManagerService::SessionManagerService(
    scoped_ptr<ChildJobInterface> child_job,
    int kill_timeout,
    SystemUtils* utils)
    : browser_(child_job.Pass()),
      exit_on_child_done_(false),
      kill_timeout_(kill_timeout),
      session_manager_(NULL),
      main_loop_(g_main_loop_new(NULL, FALSE)),
      dont_use_directly_(new MessageLoopForUI),
      message_loop_(base::MessageLoopProxy::current()),
      system_(utils),
      key_gen_(new KeyGenerator(utils)),
      upstart_signal_emitter_(new UpstartSignalEmitter),
      session_started_(false),
      session_stopping_(false),
      current_user_is_incognito_(false),
      machine_info_file_(kMachineInfoFile),
      screen_locked_(false),
      set_uid_(false),
      shutting_down_(false),
      exit_code_(SUCCESS) {
  int pipefd[2];
  PLOG_IF(DFATAL, pipe2(pipefd, O_CLOEXEC) < 0) << "Failed to create pipe";
  g_shutdown_pipe_read_fd = pipefd[0];
  g_shutdown_pipe_write_fd = pipefd[1];

  memset(signals_, 0, sizeof(signals_));
  if (!chromeos::SecureRandomString(kCookieEntropyBytes, &cookie_))
    LOG(FATAL) << "Can't generate auth cookie.";

  SetupHandlers();
}

SessionManagerService::~SessionManagerService() {
  if (main_loop_)
    g_main_loop_unref(main_loop_);
  if (session_manager_)
    g_object_unref(session_manager_);

  // Remove this in case it was added by StopSession().
  g_idle_remove_by_data(this);
  RevertHandlers();
}

bool SessionManagerService::Initialize() {
  // Install the type-info for the service with dbus.
  dbus_g_object_type_install_info(
      gobject::session_manager_get_type(),
      &gobject::dbus_glib_session_manager_object_info);

  // Creates D-Bus GLib signal ids.
  signals_[kSignalSessionStateChanged] =
      g_signal_new("session_state_changed",
                   gobject::session_manager_get_type(),
                   G_SIGNAL_RUN_LAST,
                   0,               // class offset
                   NULL, NULL,      // accumulator and data
                   // TODO: This is wrong.  If you need to use it at some point
                   // (probably only if you need to listen to this GLib signal
                   // instead of to the D-Bus signal), you should generate an
                   // appropriate marshaller that takes two string arguments.
                   // See e.g. http://goo.gl/vEGT4.
                   g_cclosure_marshal_VOID__STRING,
                   G_TYPE_NONE,     // return type
                   2,               // num params
                   G_TYPE_STRING,   // "started", "stopping", or "stopped"
                   G_TYPE_STRING);  // current user
  signals_[kSignalLoginPromptVisible] =
      g_signal_new("login_prompt_visible",
                   gobject::session_manager_get_type(),
                   G_SIGNAL_RUN_LAST,
                   0,               // class offset
                   NULL, NULL,      // accumulator and data
                   NULL,            // Use default marshaller.
                   G_TYPE_NONE,     // return type
                   0);              // num params
  signals_[kSignalScreenIsLocked] =
      g_signal_new("screen_is_locked",
                   gobject::session_manager_get_type(),
                   G_SIGNAL_RUN_LAST,
                   0,               // class offset
                   NULL, NULL,      // accumulator and data
                   NULL,            // Use default marshaller.
                   G_TYPE_NONE,     // return type
                   0);              // num params
  signals_[kSignalScreenIsUnlocked] =
      g_signal_new("screen_is_unlocked",
                   gobject::session_manager_get_type(),
                   G_SIGNAL_RUN_LAST,
                   0,               // class offset
                   NULL, NULL,      // accumulator and data
                   NULL,            // Use default marshaller.
                   G_TYPE_NONE,     // return type
                   0);              // num params

  LOG(INFO) << "SessionManagerService starting";
  if (!Reset())
    return false;

  FilePath flag_file_dir(kFlagFileDir);
  if (!file_util::CreateDirectory(flag_file_dir)) {
    PLOG(ERROR) << "Cannot create flag file directory at " << kFlagFileDir;
    return false;
  }
  login_metrics_.reset(new LoginMetrics(flag_file_dir));
  device_policy_ = DevicePolicyService::Create(login_metrics_.get(),
                                               mitigator_.get(),
                                               message_loop_);
  device_policy_->set_delegate(this);
  user_policy_factory_.reset(
      new UserPolicyServiceFactory(
          getuid(),
          message_loop_));
  return true;
}

bool SessionManagerService::Register(
    const chromeos::dbus::BusConnection &connection) {
  if (!chromeos::dbus::AbstractDbusService::Register(connection))
    return false;
  const string filter =
      StringPrintf("type='method_call', interface='%s'", service_interface());
  DBusConnection* conn =
      ::dbus_g_connection_get_connection(connection.g_connection());
  CHECK(conn);
  DBusError error;
  ::dbus_error_init(&error);
  ::dbus_bus_add_match(conn, filter.c_str(), &error);
  if (::dbus_error_is_set(&error)) {
    LOG(WARNING) << "Failed to add match to bus: " << error.name << ", message="
                 << (error.message ? error.message : "unknown error");
    return false;
  }
  if (!::dbus_connection_add_filter(conn,
                                    &SessionManagerService::FilterMessage,
                                    this,
                                    NULL)) {
    LOG(WARNING) << "Failed to add filter to connection";
    return false;
  }
  return true;
}

bool SessionManagerService::Reset() {
  if (session_manager_)
    g_object_unref(session_manager_);
  session_manager_ =
      reinterpret_cast<gobject::SessionManager*>(
          g_object_new(gobject::session_manager_get_type(), NULL));

  // Allow references to this instance.
  session_manager_->service = this;

  if (main_loop_)
    g_main_loop_unref(main_loop_);
  main_loop_ = g_main_loop_new(NULL, false);
  if (!main_loop_) {
    LOG(ERROR) << "Failed to create main loop";
    return false;
  }
  dont_use_directly_.reset(NULL);
  dont_use_directly_.reset(new MessageLoopForUI);
  message_loop_ = base::MessageLoopProxy::current();
  return true;
}

void SessionManagerService::OnPolicyPersisted(bool success) {
  system_->SendStatusSignalToChromium(chromium::kPropertyChangeCompleteSignal,
                                      success);
}

void SessionManagerService::OnKeyPersisted(bool success) {
  system_->SendStatusSignalToChromium(chromium::kOwnerKeySetSignal, success);
}

int SessionManagerService::GetKillTimeout() {
  if (file_util::PathExists(FilePath(kCollectChromeFile)))
    return kKillTimeoutCollectChrome;
  else
    return kill_timeout_;
}

bool SessionManagerService::Run() {
  if (!main_loop_) {
    LOG(ERROR) << "You must have a main loop to call Run.";
    return false;
  }
  g_io_add_watch_full(g_io_channel_unix_new(g_shutdown_pipe_read_fd),
                      G_PRIORITY_HIGH_IDLE,
                      GIOCondition(G_IO_IN | G_IO_PRI | G_IO_HUP),
                      HandleKill,
                      this,
                      NULL);
  if (ShouldRunBrowser())
    RunBrowser();
  else
    AllowGracefulExit();  // Schedules a Shutdown() on current MessageLoop.

  // TODO(cmasone): A corrupted owner key means that the user needs to go
  //                to recovery mode.  How to tell them that from here?
  CHECK(device_policy_->Initialize());
  MessageLoop::current()->Run();
  CleanupChildren(GetKillTimeout());
  DLOG(INFO) << "emitting D-Bus signal SessionStateChanged:" << kStopped;
  system_->BroadcastSignal(session_manager_,
                           signals_[kSignalSessionStateChanged],
                           kStopped, current_user_.c_str());
  return true;
}

bool SessionManagerService::ShouldRunBrowser() {
  return !file_checker_.get() || !file_checker_->exists();
}

bool SessionManagerService::ShouldStopChild(ChildJobInterface* child_job) {
  return child_job->ShouldStop();
}

bool SessionManagerService::Shutdown() {
  DeregisterChildWatchers();
  if (session_started_) {
    session_stopping_ = true;
    DLOG(INFO) << "emitting D-Bus signal SessionStateChanged:" << kStopping;
    system_->BroadcastSignal(session_manager_,
                             signals_[kSignalSessionStateChanged],
                             kStopping, current_user_.c_str());
  }

  device_policy_->PersistPolicySync();
  if (user_policy_.get())
    user_policy_->PersistPolicySync();
  message_loop_->PostTask(FROM_HERE, MessageLoop::QuitClosure());
  LOG(INFO) << "SessionManagerService quitting run loop";
  return true;
}

void SessionManagerService::RunBrowser() {
  bool first_boot = false;
  if (!file_util::PathExists(FilePath(LoginMetrics::kChromeUptimeFile)))
    first_boot = true;

  login_metrics_->RecordStats("chrome-exec");
  if (first_boot)
    browser_.job->AddOneTimeArgument(kFirstBootFlag);
  LOG(INFO) << "Running child " << browser_.job->GetName() << "...";
  browser_.pid = RunChild(browser_.job.get());
}

int SessionManagerService::RunChild(ChildJobInterface* child_job) {
  child_job->RecordTime();
  pid_t pid = system_->fork();
  if (pid == 0) {
    if (setenv("CROS_SESSION_MANAGER_COOKIE", cookie_.c_str(), 1))
      exit(1);
    RevertHandlers();
    child_job->Run();
    exit(ChildJobInterface::kCantExec);  // Run() is not supposed to return.
  }
  child_job->ClearOneTimeArgument();

  browser_.watcher = g_child_watch_add_full(G_PRIORITY_HIGH_IDLE,
                                            pid,
                                            HandleBrowserExit,
                                            this,
                                            NULL);
  return pid;
}

void SessionManagerService::KillChild(const ChildJobInterface* child_job,
                                      int child_pid) {
  uid_t to_kill_as = getuid();
  if (child_job->IsDesiredUidSet())
    to_kill_as = child_job->GetDesiredUid();
  system_->kill(-child_pid, to_kill_as, SIGKILL);
  // Process will be reaped on the way into HandleBrowserExit.
}

void SessionManagerService::AdoptKeyGeneratorJob(
    scoped_ptr<ChildJobInterface> job,
    pid_t pid,
    guint watcher) {
  generator_.job.swap(job);
  generator_.pid = pid;
  generator_.watcher = watcher;
}

void SessionManagerService::AbandonKeyGeneratorJob() {
  if (generator_.pid > 0) {
    g_source_remove(generator_.watcher);
    generator_.watcher = 0;
  }
  generator_.job.reset(NULL);
  generator_.pid = -1;
}

bool SessionManagerService::IsKnownChild(pid_t pid) {
  return pid == browser_.pid;
}

void SessionManagerService::AllowGracefulExit() {
  shutting_down_ = true;
  if (exit_on_child_done_) {
    LOG(INFO) << "SessionManagerService set to exit on child done";
    message_loop_->PostTask(
        FROM_HERE,
        base::Bind(base::IgnoreResult(&SessionManagerService::Shutdown), this));
  }
}

///////////////////////////////////////////////////////////////////////////////
// SessionManagerService commands

gboolean SessionManagerService::EmitLoginPromptReady(gboolean* OUT_emitted,
                                                     GError** error) {
  login_metrics_->RecordStats("login-prompt-ready");
  // TODO(derat): Stop emitting this signal once no one's listening for it.
  // Jobs that want to run after we're done booting should wait for
  // login-prompt-visible or boot-complete.
  *OUT_emitted =
      upstart_signal_emitter_->EmitSignal("login-prompt-ready", "", error);
  return *OUT_emitted;
}

gboolean SessionManagerService::EmitLoginPromptVisible(GError** error) {
  login_metrics_->RecordStats("login-prompt-visible");
  system_->BroadcastSignalNoArgs(session_manager_,
                                 signals_[kSignalLoginPromptVisible]);
  return upstart_signal_emitter_->EmitSignal("login-prompt-visible", "", error);
}

gboolean SessionManagerService::EnableChromeTesting(gboolean force_relaunch,
                                                    const gchar** extra_args,
                                                    gchar** OUT_filepath,
                                                    GError** error) {
  // Check to see if we already have Chrome testing enabled.
  bool already_enabled = !chrome_testing_path_.empty();

  if (!already_enabled) {
    // Create a write-only temporary directory to put the testing channel in.
    FilePath temp_dir_path;
    if (!file_util::CreateNewTempDirectory(
        FILE_PATH_LITERAL(""), &temp_dir_path))
      return FALSE;
    if (chmod(temp_dir_path.value().c_str(), 0003))
      return FALSE;

    // Get a temporary filename in the temporary directory.
    char* temp_path = tempnam(temp_dir_path.value().c_str(), "");
    if (!temp_path) {
      PLOG(ERROR) << "Can't get temp file name in " << temp_dir_path.value()
                  << ": ";
      return FALSE;
    }
    chrome_testing_path_ = temp_path;
    free(temp_path);
  }

  *OUT_filepath = g_strdup(chrome_testing_path_.c_str());

  if (already_enabled && !force_relaunch)
    return TRUE;

  // Delete testing channel file if it already exists.
  file_util::Delete(FilePath(chrome_testing_path_), false);

  // Kill Chrome.
  KillChild(browser_.job.get(), browser_.pid);

  vector<string> extra_argument_vector;
  // Create extra argument vector.
  while (*extra_args != NULL) {
    extra_argument_vector.push_back(*extra_args);
    ++extra_args;
  }
  // Add testing channel argument to extra arguments.
  string testing_argument = kTestingChannelFlag;
  testing_argument.append(chrome_testing_path_);
  extra_argument_vector.push_back(testing_argument);
  // Add extra arguments to Chrome.
  browser_.job->SetExtraArguments(extra_argument_vector);

  // Run Chrome.
  browser_.pid = RunChild(browser_.job.get());
  return TRUE;
}

gboolean SessionManagerService::StartSession(gchar* email_address,
                                             gchar* unique_identifier,
                                             gboolean* OUT_done,
                                             GError** error) {
  if (session_started_) {
    const char msg[] = "Can't start session while session is already active.";
    LOG(ERROR) << msg;
    system_->SetGError(error, CHROMEOS_LOGIN_ERROR_SESSION_EXISTS, msg);
    return *OUT_done = FALSE;
  }
  if (!ValidateAndCacheUserEmail(email_address, error)) {
    *OUT_done = FALSE;
    return FALSE;
  }

  // Check whether the current user is the owner, and if so make sure she is
  // whitelisted and has an owner key.
  bool user_is_owner = false;
  PolicyService::Error policy_error;
  if (!device_policy_->CheckAndHandleOwnerLogin(current_user_,
                                                &user_is_owner,
                                                &policy_error)) {
    system_->SetGError(error,
                       policy_error.code(),
                       policy_error.message().c_str());
    return *OUT_done = FALSE;
  }

  // Initialize user policy.
  user_policy_ = user_policy_factory_->Create(email_address);
  if (!user_policy_.get() || !user_policy_->Initialize()) {
    LOG(ERROR) << "User policy failed to initialize.";
    return *OUT_done = FALSE;
  }

  // Send each user login event to UMA (right before we start session
  // since the metrics library does not log events in guest mode).
  int dev_mode = system_->IsDevMode();
  if (dev_mode > -1) {
    login_metrics_->SendLoginUserType(dev_mode,
                                      current_user_is_incognito_,
                                      user_is_owner);
  }
  *OUT_done =
      upstart_signal_emitter_->EmitSignal(
          "start-user-session",
          StringPrintf("CHROMEOS_USER=%s", current_user_.c_str()),
          error);

  if (*OUT_done) {
    browser_.job->StartSession(current_user_);
    session_started_ = true;
    DLOG(INFO) << "emitting D-Bus signal SessionStateChanged:" << kStarted;
    system_->BroadcastSignal(session_manager_,
                             signals_[kSignalSessionStateChanged],
                             kStarted, current_user_.c_str());
    if (device_policy_->KeyMissing() &&
        !mitigator_->Mitigating() &&
        !current_user_is_incognito_) {
      key_gen_->Start(set_uid_ ? uid_ : 0, this);
    }
    // Delete the machine-info file. It contains device-identifiable data such
    // as the serial number and shouldn't be around during a user session.
    if (!file_util::Delete(FilePath(machine_info_file_), false))
      PLOG(WARNING) << "Failed to delete " << machine_info_file_.value();
  }

  system_->AtomicFileWrite(FilePath(kLoggedInFlag), "1", 1);

  return *OUT_done;
}

void SessionManagerService::HandleKeygenExit(GPid pid,
                                             gint status,
                                             gpointer data) {
  SessionManagerService* manager = static_cast<SessionManagerService*>(data);
  manager->AbandonKeyGeneratorJob();

  if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
    string key;
    FilePath key_file(manager->key_gen_->temporary_key_filename());
    file_util::ReadFileToString(key_file, &key);
    PLOG_IF(WARNING, !file_util::Delete(key_file, false)) << "Can't delete "
                                                          << key_file.value();
    manager->device_policy_->ValidateAndStoreOwnerKey(manager->current_user_,
                                                      key);
  } else {
    if (WIFSIGNALED(status))
      LOG(ERROR) << "keygen exited on signal " << WTERMSIG(status);
    else
      LOG(ERROR) << "keygen exited with exit code " << WEXITSTATUS(status);
  }
}

gboolean SessionManagerService::StopSession(gchar* unique_identifier,
                                            gboolean* OUT_done,
                                            GError** error) {
  // Most calls to StopSession() will log the reason for the call.
  // If you don't see a log message saying the reason for the call, it is
  // likely a DBUS message. See interface.cc for that call.
  LOG(INFO) << "SessionManagerService StopSession";
  g_idle_add_full(G_PRIORITY_DEFAULT_IDLE,
                  ServiceShutdown,
                  this,
                  NULL);
  DeregisterChildWatchers();
  // TODO(cmasone): re-enable these when we try to enable logout without exiting
  //                the session manager
  // browser_.job->StopSession();
  // user_policy_.reset();
  // session_started_ = false;
  return *OUT_done = TRUE;
}

void SessionManagerService::SendBooleanReply(DBusGMethodInvocation* context,
                                             bool succeeded) {
  if (context)
    dbus_g_method_return(context, succeeded);
}

gboolean SessionManagerService::StorePolicy(GArray* policy_blob,
                                            DBusGMethodInvocation* context) {
  int flags = PolicyService::KEY_ROTATE;
  if (!session_started_)
    flags |= PolicyService::KEY_INSTALL_NEW | PolicyService::KEY_CLOBBER;
  return device_policy_->Store(reinterpret_cast<uint8*>(policy_blob->data),
                               policy_blob->len,
                               new DBusGMethodCompletion(context),
                               flags) ? TRUE : FALSE;
}

gboolean SessionManagerService::RetrievePolicy(GArray** OUT_policy_blob,
                                               GError** error) {
  return RetrievePolicyFromService(device_policy_.get(),
                                   OUT_policy_blob,
                                   error);
}

gboolean SessionManagerService::StoreUserPolicy(
    GArray* policy_blob,
    DBusGMethodInvocation* context) {
  if (session_started_ && user_policy_.get()) {
    return user_policy_->Store(reinterpret_cast<uint8*>(policy_blob->data),
                               policy_blob->len,
                               new DBusGMethodCompletion(context),
                               PolicyService::KEY_INSTALL_NEW |
                               PolicyService::KEY_ROTATE) ? TRUE : FALSE;
  }

  const char msg[] = "Cannot store user policy before session is started.";
  LOG(ERROR) << msg;
  system_->SetAndSendGError(CHROMEOS_LOGIN_ERROR_SESSION_EXISTS, context, msg);
  return FALSE;
}

gboolean SessionManagerService::RetrieveUserPolicy(GArray** OUT_policy_blob,
                                                   GError** error) {
  if (session_started_ && user_policy_.get()) {
    return RetrievePolicyFromService(user_policy_.get(),
                                     OUT_policy_blob,
                                     error);
  }

  const char msg[] = "Cannot retrieve user policy before session is started.";
  LOG(ERROR) << msg;
  system_->SetGError(error, CHROMEOS_LOGIN_ERROR_SESSION_EXISTS, msg);
  return FALSE;
}

gboolean SessionManagerService::RetrieveSessionState(gchar** OUT_state,
                                                     gchar** OUT_user) {
  if (!session_started_)
    *OUT_state = g_strdup(kStopped);
  else
    *OUT_state = g_strdup(session_stopping_ ? kStopping : kStarted);
  *OUT_user = g_strdup(session_started_ && !current_user_.empty() ?
                       current_user_.c_str() : "");
  return TRUE;
}

gboolean SessionManagerService::LockScreen(GError** error) {
  if (current_user_is_incognito_) {
    LOG(WARNING) << "Attempt to lock screen during Guest session.";
    return FALSE;
  }
  system_->SendSignalToChromium(chromium::kLockScreenSignal, NULL);
  LOG(INFO) << "LockScreen";
  return TRUE;
}

gboolean SessionManagerService::HandleLockScreenShown(GError** error) {
  screen_locked_ = true;
  LOG(INFO) << "HandleLockScreenShown";
  system_->BroadcastSignalNoArgs(session_manager_,
                                 signals_[kSignalScreenIsLocked]);
  return TRUE;
}

gboolean SessionManagerService::UnlockScreen(GError** error) {
  system_->SendSignalToChromium(chromium::kUnlockScreenSignal, NULL);
  LOG(INFO) << "UnlockScreen";
  return TRUE;
}

gboolean SessionManagerService::HandleLockScreenDismissed(GError** error) {
  screen_locked_ = false;
  LOG(INFO) << "HandleLockScreenDismissed";
  system_->BroadcastSignalNoArgs(session_manager_,
                                 signals_[kSignalScreenIsUnlocked]);
  return TRUE;
}

gboolean SessionManagerService::RestartJob(gint pid,
                                           gchar* arguments,
                                           gboolean* OUT_done,
                                           GError** error) {
  if (browser_.pid != static_cast<pid_t>(pid)) {
    *OUT_done = FALSE;
    const char msg[] = "Provided pid is unknown.";
    LOG(ERROR) << msg;
    system_->SetGError(error, CHROMEOS_LOGIN_ERROR_UNKNOWN_PID, msg);
    return FALSE;
  }

  // Waiting for Chrome to shutdown takes too much time.
  // We're killing it immediately hoping that data Chrome uses before
  // logging in is not corrupted.
  // TODO(avayvod): Remove RestartJob when crosbug.com/6924 is fixed.
  KillChild(browser_.job.get(), browser_.pid);

  char arguments_buffer[kMaxArgumentsSize + 1];
  snprintf(arguments_buffer, sizeof(arguments_buffer), "%s", arguments);
  arguments_buffer[kMaxArgumentsSize] = '\0';
  string arguments_string(arguments_buffer);

  browser_.job->SetArguments(arguments_string);
  browser_.pid = RunChild(browser_.job.get());

  // To set "logged-in" state for BWSI mode.
  return StartSession(const_cast<gchar*>(kIncognitoUser), NULL,
                      OUT_done, error);
}

gboolean SessionManagerService::RestartJobWithAuth(gint pid,
                                                   gchar* cookie,
                                                   gchar* arguments,
                                                   gboolean* OUT_done,
                                                   GError** error) {
  // This method isn't filtered - instead, we check for cookie validity.
  if (!IsValidCookie(cookie)) {
    *OUT_done = FALSE;
    const char msg[] = "Invalid auth cookie.";
    LOG(ERROR) << msg;
    system_->SetGError(error, CHROMEOS_LOGIN_ERROR_ILLEGAL_SERVICE, msg);
    return FALSE;
  }
  return RestartJob(pid, arguments, OUT_done, error);
}

gboolean SessionManagerService::StartSessionService(gchar *name,
                                                    gboolean *OUT_done,
                                                    GError **error) {
  if (!IsValidSessionService(name)) {
    LOG(ERROR) << "Invalid session service name: " << name;
    system_->SetGError(error, CHROMEOS_LOGIN_ERROR_ILLEGAL_SERVICE,
                       "Invalid session service name.");
    return FALSE;
  }

  LOG(INFO) << "Starting session service: " << name;
  string command = StringPrintf("/sbin/initctl start %s", name);
  int status = system(command.c_str());
  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
    LOG(ERROR) << "Could not start " << name << ": " << status;
    system_->SetGError(error, CHROMEOS_LOGIN_ERROR_START_FAIL,
                       "Starting service failed.");
  }
  return status == 0;
}

gboolean SessionManagerService::StopSessionService(gchar *name,
                                                   gboolean *OUT_done,
                                                   GError **error) {
  if (!IsValidSessionService(name)) {
    LOG(ERROR) << "Invalid session service name: " << name;
    system_->SetGError(error, CHROMEOS_LOGIN_ERROR_ILLEGAL_SERVICE,
                       "Invalid session service name.");
    return FALSE;
  }

  LOG(INFO) << "Stopping session service: " << name;
  string command = StringPrintf("/sbin/initctl stop %s", name);
  int status = system(command.c_str());
  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
    LOG(ERROR) << "Could not stop " << name << ": " << status;
    system_->SetGError(error, CHROMEOS_LOGIN_ERROR_STOP_FAIL,
                       "Stopping service failed.");
  }
  return status == 0;
}

// static
bool SessionManagerService::IsValidSessionService(const gchar *name) {
  for (int i = 0; kValidSessionServices[i]; i++)
    if (!strcmp(name, kValidSessionServices[i]))
      return true;
  return false;
}

///////////////////////////////////////////////////////////////////////////////
// glib event handlers

void SessionManagerService::HandleBrowserExit(GPid pid,
                                              gint status,
                                              gpointer data) {
  // If I could wait for descendants here, I would.  Instead, I kill them.
  kill(-pid, SIGKILL);

  DLOG(INFO) << "Handling child process exit: " << pid;
  if (WIFSIGNALED(status)) {
    DLOG(INFO) << "  Exited with signal " << WTERMSIG(status);
  } else if (WIFEXITED(status)) {
    DLOG(INFO) << "  Exited with exit code " << WEXITSTATUS(status);
    CHECK(WEXITSTATUS(status) != ChildJobInterface::kCantSetUid);
    CHECK(WEXITSTATUS(status) != ChildJobInterface::kCantExec);
  } else {
    DLOG(INFO) << "  Exited...somehow, without an exit code or a signal??";
  }

  // If the child _ever_ exits uncleanly, we want to start it up again.
  SessionManagerService* manager = static_cast<SessionManagerService*>(data);

  // Do nothing if already shutting down.
  if (manager->shutting_down_)
    return;

  ChildJobInterface* child_job = NULL;
  if (manager->IsKnownChild(pid))
    child_job = manager->browser_.job.get();

  LOG(ERROR) << StringPrintf("Process %s(%d) exited.",
                             child_job ? child_job->GetName().c_str() : "",
                             pid);
  if (manager->screen_locked_) {
    LOG(ERROR) << "Screen locked, shutting down";
    manager->SetExitAndServiceShutdown(CRASH_WHILE_SCREEN_LOCKED);
    return;
  }

  if (child_job) {
    if (manager->ShouldStopChild(child_job)) {
      LOG(WARNING) << "Child stopped, shutting down";
      manager->SetExitAndServiceShutdown(CHILD_EXITING_TOO_FAST);
    } else if (manager->ShouldRunBrowser()) {
      // TODO(cmasone): deal with fork failing in RunChild()
      LOG(INFO) << StringPrintf(
          "Running child %s again...", child_job->GetName().data());
      manager->browser_.pid = manager->RunChild(child_job);
    } else {
      LOG(INFO) << StringPrintf(
          "Should NOT run %s again...", child_job->GetName().data());
      manager->AllowGracefulExit();
    }
  } else {
    LOG(ERROR) << "Couldn't find pid of exiting child: " << pid;
  }
}

gboolean SessionManagerService::HandleKill(GIOChannel* source,
                                           GIOCondition condition,
                                           gpointer data) {
  // We only get called if there's data on the pipe.  If there's data, we're
  // supposed to exit.  So, don't even bother to read it.
  LOG(INFO) << "SessionManagerService - data on pipe, so exiting";
  return ServiceShutdown(data);
}

void SessionManagerService::SetExitAndServiceShutdown(ExitCode code) {
  exit_code_ = code;
  ServiceShutdown(this);
}

gboolean SessionManagerService::ServiceShutdown(gpointer data) {
  SessionManagerService* manager = static_cast<SessionManagerService*>(data);
  manager->Shutdown();
  LOG(INFO) << "SessionManagerService exiting";
  return FALSE;  // So that the event source that called this gets removed.
}

///////////////////////////////////////////////////////////////////////////////
// Utility Methods

// This can probably be more efficient, if it needs to be.
// static
bool SessionManagerService::ValidateEmail(const string& email_address) {
  if (email_address.find_first_not_of(kLegalCharacters) != string::npos)
    return false;

  size_t at = email_address.find(kEmailSeparator);
  // it has NO @.
  if (at == string::npos)
    return false;

  // it has more than one @.
  if (email_address.find(kEmailSeparator, at+1) != string::npos)
    return false;

  return true;
}

// static
DBusHandlerResult SessionManagerService::FilterMessage(DBusConnection* conn,
                                                       DBusMessage* message,
                                                       void* data) {
  SessionManagerService* service = static_cast<SessionManagerService*>(data);
  if (::dbus_message_is_method_call(message,
                                    service->service_interface(),
                                    kSessionManagerRestartJob)) {
    const char* sender = ::dbus_message_get_sender(message);
    if (!sender) {
      LOG(ERROR) << "Call to RestartJob has no sender";
      return DBUS_HANDLER_RESULT_HANDLED;
    }
    LOG(INFO) << "Received RestartJob from " << sender;
    DBusMessage* get_pid =
        ::dbus_message_new_method_call("org.freedesktop.DBus",
                                       "/org/freedesktop/DBus",
                                       "org.freedesktop.DBus",
                                       "GetConnectionUnixProcessID");
    CHECK(get_pid);
    ::dbus_message_append_args(get_pid,
                               DBUS_TYPE_STRING, &sender,
                               DBUS_TYPE_INVALID);
    DBusMessage* got_pid =
        ::dbus_connection_send_with_reply_and_block(conn, get_pid, -1, NULL);
    ::dbus_message_unref(get_pid);
    if (!got_pid) {
      LOG(ERROR) << "Could not look up sender of RestartJob";
      return DBUS_HANDLER_RESULT_HANDLED;
    }
    uint32 pid;
    if (!::dbus_message_get_args(got_pid, NULL,
                                 DBUS_TYPE_UINT32, &pid,
                                 DBUS_TYPE_INVALID)) {
      ::dbus_message_unref(got_pid);
      LOG(ERROR) << "Could not extract pid of sender of RestartJob";
      return DBUS_HANDLER_RESULT_HANDLED;
    }
    ::dbus_message_unref(got_pid);
    if (!service->IsKnownChild(pid)) {
      LOG(WARNING) << "Sender of RestartJob is no child of mine!";
      return DBUS_HANDLER_RESULT_HANDLED;
    }
  }
  return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

void SessionManagerService::SetupHandlers() {
  // I have to ignore SIGUSR1, because Xorg sends it to this process when it's
  // got no clients and is ready for new ones.  If we don't ignore it, we die.
  struct sigaction action;
  memset(&action, 0, sizeof(action));
  action.sa_handler = SIG_IGN;
  CHECK(sigaction(SIGUSR1, &action, NULL) == 0);

  action.sa_handler = SessionManagerService::do_nothing;
  CHECK(sigaction(SIGALRM, &action, NULL) == 0);

  // We need to handle SIGTERM, because that is how many POSIX-based distros ask
  // processes to quit gracefully at shutdown time.
  action.sa_handler = SIGTERMHandler;
  CHECK(sigaction(SIGTERM, &action, NULL) == 0);
  // Also handle SIGINT - when the user terminates the browser via Ctrl+C.
  // If the browser process is being debugged, GDB will catch the SIGINT first.
  action.sa_handler = SIGINTHandler;
  CHECK(sigaction(SIGINT, &action, NULL) == 0);
  // And SIGHUP, for when the terminal disappears. On shutdown, many Linux
  // distros send SIGHUP, SIGTERM, and then SIGKILL.
  action.sa_handler = SIGHUPHandler;
  CHECK(sigaction(SIGHUP, &action, NULL) == 0);
}

void SessionManagerService::RevertHandlers() {
  struct sigaction action;
  memset(&action, 0, sizeof(action));
  action.sa_handler = SIG_DFL;
  CHECK(sigaction(SIGUSR1, &action, NULL) == 0);
  CHECK(sigaction(SIGALRM, &action, NULL) == 0);
  CHECK(sigaction(SIGTERM, &action, NULL) == 0);
  CHECK(sigaction(SIGINT, &action, NULL) == 0);
  CHECK(sigaction(SIGHUP, &action, NULL) == 0);
}

gboolean SessionManagerService::ValidateAndCacheUserEmail(
    const gchar* email_address,
    GError** error) {
  // basic validity checking; avoid buffer overflows here, and
  // canonicalize the email address a little.
  char email[kMaxEmailSize + 1];
  snprintf(email, sizeof(email), "%s", email_address);
  email[kMaxEmailSize] = '\0';  // Just to be sure.
  string email_string(email);
  bool user_is_incognito = ((email_string == kIncognitoUser) ||
      (email_string == kDemoUser));
  if (!user_is_incognito && !ValidateEmail(email_string)) {
    const char msg[] = "Provided email address is not valid.  ASCII only.";
    LOG(ERROR) << msg;
    system_->SetGError(error, CHROMEOS_LOGIN_ERROR_INVALID_EMAIL, msg);
    return FALSE;
  }
  current_user_is_incognito_ = user_is_incognito;
  current_user_ = StringToLowerASCII(email_string);
  return TRUE;
}

void SessionManagerService::KillAndRemember(
    const ChildJob::Spec& spec,
    vector<std::pair<pid_t, uid_t> >* to_remember) {
  const pid_t pid = spec.pid;
  if (pid < 0)
    return;

  const uid_t uid = (spec.job->IsDesiredUidSet() ?
                     spec.job->GetDesiredUid() : getuid());
  system_->kill(pid, uid, SIGTERM);
  to_remember->push_back(make_pair(pid, uid));
}

void SessionManagerService::CleanupChildren(int timeout) {
  vector<pair<int, uid_t> > pids_to_abort;
  KillAndRemember(browser_, &pids_to_abort);
  KillAndRemember(generator_, &pids_to_abort);

  for (vector<pair<int, uid_t> >::const_iterator it = pids_to_abort.begin();
       it != pids_to_abort.end(); ++it) {
    const pid_t pid = it->first;
    const uid_t uid = it->second;
    if (!system_->ChildIsGone(pid, timeout)) {
      LOG(WARNING) << "Killing child process " << pid << " " << timeout
                   << " seconds after sending TERM signal";
      system_->kill(pid, uid, SIGABRT);
    } else {
      DLOG(INFO) << "Cleaned up child " << pid;
    }
  }
}

void SessionManagerService::DeregisterChildWatchers() {
  // Remove child exit handlers.
  if (browser_.pid > 0) {
    g_source_remove(browser_.watcher);
    browser_.watcher = 0;
  }
  if (generator_.pid > 0) {
    g_source_remove(generator_.watcher);
    generator_.watcher = 0;
  }
}

void SessionManagerService::SendSignal(const char signal_name[],
                                       bool succeeded) {
  system_->SendStatusSignalToChromium(signal_name, succeeded);
}

// static
vector<string> SessionManagerService::GetArgList(const vector<string>& args) {
  vector<string>::const_iterator start_arg = args.begin();
  if (!args.empty() && *start_arg == "--")
    ++start_arg;
  return vector<string>(start_arg, args.end());
}

gboolean SessionManagerService::RetrievePolicyFromService(
    PolicyService* service,
    GArray** policy_blob,
    GError** error) {
  vector<uint8> policy_data;
  if (service->Retrieve(&policy_data)) {
    *policy_blob = g_array_sized_new(FALSE, FALSE, sizeof(uint8),
                                     policy_data.size());
    if (!*policy_blob) {
      const char msg[] = "Unable to allocate memory for response.";
      LOG(ERROR) << msg;
      system_->SetGError(error, CHROMEOS_LOGIN_ERROR_DECODE_FAIL, msg);
      return FALSE;
    }
    g_array_append_vals(*policy_blob,
                        vector_as_array(&policy_data), policy_data.size());
    return TRUE;
  }

  const char msg[] = "Failed to retrieve policy data.";
  LOG(ERROR) << msg;
  system_->SetGError(error, CHROMEOS_LOGIN_ERROR_ENCODE_FAIL, msg);
  return FALSE;
}

bool SessionManagerService::IsValidCookie(const char *cookie) {
  size_t len = strlen(cookie) < cookie_.size()
             ? strlen(cookie)
             : cookie_.size();
  return chromeos::SafeMemcmp(cookie, cookie_.data(), len) == 0;
}

gboolean SessionManagerService::StartDeviceWipe(gboolean* OUT_done,
                                                GError** error) {
  const char *contents = "fast safe";
  const FilePath reset_path(kResetFile);
  const FilePath session_path(kLoggedInFlag);
  if (system_->Exists(session_path)) {
    const char msg[] = "A user has already logged in this boot.";
    LOG(ERROR) << msg;
    system_->SetGError(error, CHROMEOS_LOGIN_ERROR_ALREADY_SESSION, msg);
    return FALSE;
  }
  system_->AtomicFileWrite(reset_path, contents, strlen(contents));
  system_->CallMethodOnPowerManager(power_manager::kRequestRestartSignal);
  *OUT_done = TRUE;
  return TRUE;
}

}  // namespace login_manager
