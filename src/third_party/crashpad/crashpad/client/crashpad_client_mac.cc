// Copyright 2014 The Crashpad Authors. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "client/crashpad_client.h"

#include <errno.h>
#include <mach/mach.h>
#include <pthread.h>
#include <stdint.h>
#include <sys/wait.h>
#include <unistd.h>

#include <utility>

#include "base/logging.h"
#include "base/mac/mach_logging.h"
#include "base/memory/scoped_ptr.h"
#include "base/posix/eintr_wrapper.h"
#include "base/strings/stringprintf.h"
#include "util/mac/mac_util.h"
#include "util/mach/child_port_handshake.h"
#include "util/mach/exception_ports.h"
#include "util/mach/mach_extensions.h"
#include "util/mach/mach_message.h"
#include "util/mach/notify_server.h"
#include "util/misc/clock.h"
#include "util/misc/implicit_cast.h"
#include "util/posix/close_multiple.h"

namespace crashpad {

namespace {

std::string FormatArgumentString(const std::string& name,
                                 const std::string& value) {
  return base::StringPrintf("--%s=%s", name.c_str(), value.c_str());
}

std::string FormatArgumentInt(const std::string& name, int value) {
  return base::StringPrintf("--%s=%d", name.c_str(), value);
}

// Set the exception handler for EXC_CRASH, EXC_RESOURCE, and EXC_GUARD.
//
// EXC_CRASH is how most crashes are received. Most other exception types such
// as EXC_BAD_ACCESS are delivered to a host-level exception handler in the
// kernel where they are converted to POSIX signals. See 10.9.5
// xnu-2422.115.4/bsd/uxkern/ux_exception.c catch_mach_exception_raise(). If a
// core-generating signal (triggered through this hardware mechanism or a
// software mechanism such as abort() sending SIGABRT) is unhandled and the
// process exits, or if the process is killed with SIGKILL for code-signing
// reasons, an EXC_CRASH exception will be sent. See 10.9.5
// xnu-2422.115.4/bsd/kern/kern_exit.c proc_prepareexit().
//
// EXC_RESOURCE and EXC_GUARD do not become signals or EXC_CRASH exceptions. The
// host-level exception handler in the kernel does not receive these exception
// types, and even if it did, it would not map them to signals. Instead, the
// first Mach service loaded by the root (process ID 1) launchd with a boolean
// “ExceptionServer” property in its job dictionary (regardless of its value) or
// with any subdictionary property will become the host-level exception handler
// for EXC_CRASH, EXC_RESOURCE, and EXC_GUARD. See 10.9.5
// launchd-842.92.1/src/core.c job_setup_exception_port(). Normally, this job is
// com.apple.ReportCrash.Root, the systemwide Apple Crash Reporter. Since it is
// impossible to receive EXC_RESOURCE and EXC_GUARD exceptions through the
// EXC_CRASH mechanism, an exception handler must be registered for them by name
// if it is to receive these exception types. The default task-level handler for
// these exception types is set by launchd in a similar manner.
//
// EXC_MASK_RESOURCE and EXC_MASK_GUARD are not available on all systems, and
// the kernel will reject attempts to use them if it does not understand them,
// so AND them with ExcMaskValid(). EXC_MASK_CRASH is always supported.
bool SetCrashExceptionPorts(exception_handler_t exception_handler) {
  ExceptionPorts exception_ports(ExceptionPorts::kTargetTypeTask, TASK_NULL);
  return exception_ports.SetExceptionPort(
      (EXC_MASK_CRASH | EXC_MASK_RESOURCE | EXC_MASK_GUARD) & ExcMaskValid(),
      exception_handler,
      EXCEPTION_STATE_IDENTITY | MACH_EXCEPTION_CODES,
      MACHINE_THREAD_STATE);
}

class ScopedPthreadAttrDestroy {
 public:
  explicit ScopedPthreadAttrDestroy(pthread_attr_t* pthread_attr)
      : pthread_attr_(pthread_attr) {
  }

  ~ScopedPthreadAttrDestroy() {
    errno = pthread_attr_destroy(pthread_attr_);
    PLOG_IF(WARNING, errno != 0) << "pthread_attr_destroy";
  }

 private:
  pthread_attr_t* pthread_attr_;

  DISALLOW_COPY_AND_ASSIGN(ScopedPthreadAttrDestroy);
};

//! \brief Starts a Crashpad handler, possibly restarting it if it dies.
class HandlerStarter final : public NotifyServer::DefaultInterface {
 public:
  ~HandlerStarter() {}

  //! \brief Starts a Crashpad handler initially, as opposed to starting it for
  //!     subsequent restarts.
  //!
  //! All parameters are as in CrashpadClient::StartHandler().
  //!
  //! \return On success, a send right to the Crashpad handler that has been
  //!     started. On failure, `MACH_PORT_NULL` with a message logged.
  static base::mac::ScopedMachSendRight InitialStart(
      const base::FilePath& handler,
      const base::FilePath& database,
      const std::string& url,
      const std::map<std::string, std::string>& annotations,
      const std::vector<std::string>& arguments,
      bool restartable) {
    base::mac::ScopedMachReceiveRight receive_right(
        NewMachPort(MACH_PORT_RIGHT_RECEIVE));
    if (!receive_right.is_valid()) {
      return base::mac::ScopedMachSendRight();
    }

    mach_port_t port;
    mach_msg_type_name_t right_type;
    kern_return_t kr = mach_port_extract_right(mach_task_self(),
                                               receive_right.get(),
                                               MACH_MSG_TYPE_MAKE_SEND,
                                               &port,
                                               &right_type);
    if (kr != KERN_SUCCESS) {
      MACH_LOG(ERROR, kr) << "mach_port_extract_right";
      return base::mac::ScopedMachSendRight();
    }
    base::mac::ScopedMachSendRight send_right(port);
    DCHECK_EQ(port, receive_right.get());
    DCHECK_EQ(right_type,
              implicit_cast<mach_msg_type_name_t>(MACH_MSG_TYPE_PORT_SEND));

    scoped_ptr<HandlerStarter> handler_restarter;
    if (restartable) {
      handler_restarter.reset(new HandlerStarter());
      if (!handler_restarter->notify_port_.is_valid()) {
        // This is an error that NewMachPort() would have logged. Proceed anyway
        // without the ability to restart.
        handler_restarter.reset();
      }
    }

    if (!CommonStart(handler,
                     database,
                     url,
                     annotations,
                     arguments,
                     std::move(receive_right),
                     handler_restarter.get(),
                     false)) {
      return base::mac::ScopedMachSendRight();
    }

    if (handler_restarter &&
        handler_restarter->StartRestartThread(
            handler, database, url, annotations, arguments)) {
      // The thread owns the object now.
      ignore_result(handler_restarter.release());
    }

    // If StartRestartThread() failed, proceed without the ability to restart.
    // handler_restarter will be released when this function returns.

    return send_right;
  }

  // NotifyServer::DefaultInterface:

  kern_return_t DoMachNotifyPortDestroyed(notify_port_t notify,
                                          mach_port_t rights,
                                          const mach_msg_trailer_t* trailer,
                                          bool* destroy_request) override {
    // The receive right corresponding to this process’ crash exception port is
    // now owned by this process. Any crashes that occur before the receive
    // right is moved to a new handler process will cause the process to hang in
    // an unkillable state prior to OS X 10.10.

    if (notify != notify_port_) {
      LOG(WARNING) << "notify port mismatch";
      return KERN_FAILURE;
    }

    // If CommonStart() fails, the receive right will die, and this will just
    // be called again for another try.
    CommonStart(handler_,
                database_,
                url_,
                annotations_,
                arguments_,
                base::mac::ScopedMachReceiveRight(rights),
                this,
                true);

    return KERN_SUCCESS;
  }

 private:
  HandlerStarter()
      : NotifyServer::DefaultInterface(),
        handler_(),
        database_(),
        url_(),
        annotations_(),
        arguments_(),
        notify_port_(NewMachPort(MACH_PORT_RIGHT_RECEIVE)),
        last_start_time_(0) {
  }

  //! \brief Starts a Crashpad handler.
  //!
  //! All parameters are as in CrashpadClient::StartHandler(), with these
  //! additions:
  //!
  //! \param[in] receive_right The receive right to move to the Crashpad
  //!     handler. The handler will use this receive right to run its exception
  //!     server.
  //! \param[in] handler_restarter If CrashpadClient::StartHandler() was invoked
  //!     with \a restartable set to `true`, this is the restart state object.
  //!     Otherwise, this is `nullptr`.
  //! \param[in] restart If CrashpadClient::StartHandler() was invoked with \a
  //!     restartable set to `true` and CommonStart() is being called to restart
  //!     a previously-started handler, this is `true`. Otherwise, this is
  //!     `false`.
  //!
  //! \return `true` on success, `false` on failure, with a message logged.
  //!     Failures include failure to start the handler process and failure to
  //!     rendezvous with it via ChildPortHandshake.
  static bool CommonStart(const base::FilePath& handler,
                          const base::FilePath& database,
                          const std::string& url,
                          const std::map<std::string, std::string>& annotations,
                          const std::vector<std::string>& arguments,
                          base::mac::ScopedMachReceiveRight receive_right,
                          HandlerStarter* handler_restarter,
                          bool restart) {
    DCHECK(!restart || handler_restarter);

    if (handler_restarter) {
      // The port-destroyed notification must be requested each time. It uses
      // a send-once right, so once the notification is received, it won’t be
      // sent again unless re-requested.
      mach_port_t previous;
      kern_return_t kr =
          mach_port_request_notification(mach_task_self(),
                                         receive_right.get(),
                                         MACH_NOTIFY_PORT_DESTROYED,
                                         0,
                                         handler_restarter->notify_port_.get(),
                                         MACH_MSG_TYPE_MAKE_SEND_ONCE,
                                         &previous);
      if (kr != KERN_SUCCESS) {
        MACH_LOG(WARNING, kr) << "mach_port_request_notification";

        // This will cause the restart thread to terminate after this restart
        // attempt. There’s no longer any need for it, because no more
        // port-destroyed notifications can be delivered.
        handler_restarter->notify_port_.reset();
      } else {
        base::mac::ScopedMachSendRight previous_owner(previous);
        DCHECK(restart || !previous_owner.is_valid());
      }

      if (restart) {
        // If the handler was ever started before, don’t restart it too quickly.
        const uint64_t kNanosecondsPerSecond = 1E9;
        const uint64_t kMinimumStartInterval = 1 * kNanosecondsPerSecond;

        const uint64_t earliest_next_start_time =
            handler_restarter->last_start_time_ + kMinimumStartInterval;
        const uint64_t now_time = ClockMonotonicNanoseconds();
        if (earliest_next_start_time > now_time) {
          const uint64_t sleep_time = earliest_next_start_time - now_time;
          LOG(INFO) << "restarting handler"
                    << base::StringPrintf(" in %.3fs",
                                          static_cast<double>(sleep_time) /
                                              kNanosecondsPerSecond);
          SleepNanoseconds(earliest_next_start_time - now_time);
        } else {
          LOG(INFO) << "restarting handler";
        }
      }

      handler_restarter->last_start_time_ = ClockMonotonicNanoseconds();
    }

    // Set up the arguments for execve() first. These aren’t needed until
    // execve() is called, but it’s dangerous to do this in a child process
    // after fork().
    ChildPortHandshake child_port_handshake;
    base::ScopedFD server_write_fd = child_port_handshake.ServerWriteFD();

    // Use handler as argv[0], followed by arguments directed by this method’s
    // parameters and a --handshake-fd argument. |arguments| are added first so
    // that if it erroneously contains an argument such as --url, the actual
    // |url| argument passed to this method will supersede it. In normal
    // command-line processing, the last parameter wins in the case of a
    // conflict.
    std::vector<std::string> argv(1, handler.value());
    argv.reserve(1 + arguments.size() + 2 + annotations.size() + 1);
    for (const std::string& argument : arguments) {
      argv.push_back(argument);
    }
    if (!database.value().empty()) {
      argv.push_back(FormatArgumentString("database", database.value()));
    }
    if (!url.empty()) {
      argv.push_back(FormatArgumentString("url", url));
    }
    for (const auto& kv : annotations) {
      argv.push_back(
          FormatArgumentString("annotation", kv.first + '=' + kv.second));
    }
    argv.push_back(FormatArgumentInt("handshake-fd", server_write_fd.get()));

    const char* handler_c = handler.value().c_str();

    // argv_c contains const char* pointers and is terminated by nullptr. argv
    // is required because the pointers in argv_c need to point somewhere, and
    // they can’t point to temporaries such as those returned by
    // FormatArgumentString().
    std::vector<const char*> argv_c;
    argv_c.reserve(argv.size() + 1);
    for (const std::string& argument : argv) {
      argv_c.push_back(argument.c_str());
    }
    argv_c.push_back(nullptr);

    // Double-fork(). The three processes involved are parent, child, and
    // grandchild. The grandchild will become the handler process. The child
    // exits immediately after spawning the grandchild, so the grandchild
    // becomes an orphan and its parent process ID becomes 1. This relieves the
    // parent and child of the responsibility for reaping the grandchild with
    // waitpid() or similar. The handler process is expected to outlive the
    // parent process, so the parent shouldn’t be concerned with reaping it.
    // This approach means that accidental early termination of the handler
    // process will not result in a zombie process.
    pid_t pid = fork();
    if (pid < 0) {
      PLOG(ERROR) << "fork";
      return false;
    }

    if (pid == 0) {
      // Child process.

      if (restart) {
        // When restarting, reset the system default crash handler first.
        // Otherwise, the crash exception port here will have been inherited
        // from the parent process, which was probably using the exception
        // server now being restarted. The handler can’t monitor itself for its
        // own crashes via this interface.
        CrashpadClient::UseSystemDefaultHandler();
      }

      // Call setsid(), creating a new process group and a new session, both led
      // by this process. The new process group has no controlling terminal.
      // This disconnects it from signals generated by the parent process’
      // terminal.
      //
      // setsid() is done in the child instead of the grandchild so that the
      // grandchild will not be a session leader. If it were a session leader,
      // an accidental open() of a terminal device without O_NOCTTY would make
      // that terminal the controlling terminal.
      //
      // It’s not desirable for the handler to have a controlling terminal. The
      // handler monitors clients on its own and manages its own lifetime,
      // exiting when it loses all clients and when it deems it appropraite to
      // do so. It may serve clients in different process groups or sessions
      // than its original client, and receiving signals intended for its
      // original client’s process group could be harmful in that case.
      PCHECK(setsid() != -1) << "setsid";

      pid = fork();
      if (pid < 0) {
        PLOG(FATAL) << "fork";
      }

      if (pid > 0) {
        // Child process.

        // _exit() instead of exit(), because fork() was called.
        _exit(EXIT_SUCCESS);
      }

      // Grandchild process.

      CloseMultipleNowOrOnExec(STDERR_FILENO + 1, server_write_fd.get());

      // &argv_c[0] is a pointer to a pointer to const char data, but because of
      // how C (not C++) works, execvp() wants a pointer to a const pointer to
      // char data. It modifies neither the data nor the pointers, so the
      // const_cast is safe.
      execvp(handler_c, const_cast<char* const*>(&argv_c[0]));
      PLOG(FATAL) << "execvp " << handler_c;
    }

    // Parent process.

    // Close the write side of the pipe, so that the handler process is the only
    // process that can write to it.
    server_write_fd.reset();

    // waitpid() for the child, so that it does not become a zombie process. The
    // child normally exits quickly.
    int status;
    pid_t wait_pid = HANDLE_EINTR(waitpid(pid, &status, 0));
    PCHECK(wait_pid != -1) << "waitpid";
    DCHECK_EQ(wait_pid, pid);

    if (WIFSIGNALED(status)) {
      LOG(WARNING) << "intermediate process: signal " << WTERMSIG(status);
    } else if (!WIFEXITED(status)) {
      DLOG(WARNING) << "intermediate process: unknown termination " << status;
    } else if (WEXITSTATUS(status) != EXIT_SUCCESS) {
      LOG(WARNING) << "intermediate process: exit status "
                   << WEXITSTATUS(status);
    }

    // Rendezvous with the handler running in the grandchild process.
    if (!child_port_handshake.RunClient(receive_right.get(),
                                        MACH_MSG_TYPE_MOVE_RECEIVE)) {
      return false;
    }

    ignore_result(receive_right.release());
    return true;
  }

  bool StartRestartThread(const base::FilePath& handler,
                          const base::FilePath& database,
                          const std::string& url,
                          const std::map<std::string, std::string>& annotations,
                          const std::vector<std::string>& arguments) {
    handler_ = handler;
    database_ = database;
    url_ = url;
    annotations_ = annotations;
    arguments_ = arguments;

    pthread_attr_t pthread_attr;
    errno = pthread_attr_init(&pthread_attr);
    if (errno != 0) {
      PLOG(WARNING) << "pthread_attr_init";
      return false;
    }
    ScopedPthreadAttrDestroy pthread_attr_owner(&pthread_attr);

    errno = pthread_attr_setdetachstate(&pthread_attr, PTHREAD_CREATE_DETACHED);
    if (errno != 0) {
      PLOG(WARNING) << "pthread_attr_setdetachstate";
      return false;
    }

    pthread_t pthread;
    errno = pthread_create(&pthread, &pthread_attr, RestartThreadMain, this);
    if (errno != 0) {
      PLOG(WARNING) << "pthread_create";
      return false;
    }

    return true;
  }

  static void* RestartThreadMain(void* argument) {
    HandlerStarter* self = reinterpret_cast<HandlerStarter*>(argument);

    NotifyServer notify_server(self);
    mach_msg_return_t mr;
    do {
      mr = MachMessageServer::Run(&notify_server,
                                  self->notify_port_.get(),
                                  0,
                                  MachMessageServer::kPersistent,
                                  MachMessageServer::kReceiveLargeError,
                                  kMachMessageTimeoutWaitIndefinitely);
      MACH_LOG_IF(ERROR, mr != MACH_MSG_SUCCESS, mr)
          << "MachMessageServer::Run";
    } while (self->notify_port_.is_valid() && mr == MACH_MSG_SUCCESS);

    delete self;

    return nullptr;
  }

  base::FilePath handler_;
  base::FilePath database_;
  std::string url_;
  std::map<std::string, std::string> annotations_;
  std::vector<std::string> arguments_;
  base::mac::ScopedMachReceiveRight notify_port_;
  uint64_t last_start_time_;

  DISALLOW_COPY_AND_ASSIGN(HandlerStarter);
};

}  // namespace

CrashpadClient::CrashpadClient()
    : exception_port_() {
}

CrashpadClient::~CrashpadClient() {
}

bool CrashpadClient::StartHandler(
    const base::FilePath& handler,
    const base::FilePath& database,
    const std::string& url,
    const std::map<std::string, std::string>& annotations,
    const std::vector<std::string>& arguments,
    bool restartable) {
  DCHECK(!exception_port_.is_valid());

  // The “restartable” behavior can only be selected on OS X 10.10 and later. In
  // previous OS versions, if the initial client were to crash while attempting
  // to restart the handler, it would become an unkillable process.
  base::mac::ScopedMachSendRight exception_port(
      HandlerStarter::InitialStart(handler,
                                   database,
                                   url,
                                   annotations,
                                   arguments,
                                   restartable && MacOSXMinorVersion() >= 10));
  if (!exception_port.is_valid()) {
    return false;
  }

  SetHandlerMachPort(std::move(exception_port));
  return true;
}

bool CrashpadClient::SetHandlerMachService(const std::string& service_name) {
  base::mac::ScopedMachSendRight exception_port(BootstrapLookUp(service_name));
  if (!exception_port.is_valid()) {
    return false;
  }

  SetHandlerMachPort(std::move(exception_port));
  return true;
}

void CrashpadClient::SetHandlerMachPort(
    base::mac::ScopedMachSendRight exception_port) {
  DCHECK(exception_port.is_valid());
  exception_port_ = std::move(exception_port);
}

bool CrashpadClient::UseHandler() {
  DCHECK(exception_port_.is_valid());

  return SetCrashExceptionPorts(exception_port_.get());
}

// static
void CrashpadClient::UseSystemDefaultHandler() {
  base::mac::ScopedMachSendRight
      system_crash_reporter_handler(SystemCrashReporterHandler());

  // Proceed even if SystemCrashReporterHandler() failed, setting MACH_PORT_NULL
  // to clear the current exception ports.
  if (!SetCrashExceptionPorts(system_crash_reporter_handler.get())) {
    SetCrashExceptionPorts(MACH_PORT_NULL);
  }
}

}  // namespace crashpad
