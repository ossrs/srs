// -*- Mode: C++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright (c) 2009, Google Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

// ---
// Author: Sanjay Ghemawat
//         Nabeel Mian
//
// Implements management of profile timers and the corresponding signal handler.

#include "config.h"
#include "profile-handler.h"

#if !(defined(__CYGWIN__) || defined(__CYGWIN32__))

#include <stdio.h>
#include <errno.h>
#include <sys/time.h>

#include <list>
#include <string>

#if HAVE_LINUX_SIGEV_THREAD_ID
// for timer_{create,settime} and associated typedefs & constants
#include <time.h>
// for sigevent
#include <signal.h>
// for SYS_gettid
#include <sys/syscall.h>

// for perftools_pthread_key_create
#include "maybe_threads.h"
#endif

#include "base/dynamic_annotations.h"
#include "base/googleinit.h"
#include "base/logging.h"
#include "base/spinlock.h"
#include "maybe_threads.h"

// Some Linux systems don't have sigev_notify_thread_id defined in
// signal.h (despite having SIGEV_THREAD_ID defined) and also lack
// working linux/signal.h. So lets workaround. Note, we know that at
// least on Linux sigev_notify_thread_id is macro.
//
// See https://sourceware.org/bugzilla/show_bug.cgi?id=27417 and
// https://bugzilla.kernel.org/show_bug.cgi?id=200081
//
#if __linux__ && HAVE_LINUX_SIGEV_THREAD_ID && !defined(sigev_notify_thread_id)
#define sigev_notify_thread_id _sigev_un._tid
#endif

using std::list;
using std::string;

// This structure is used by ProfileHandlerRegisterCallback and
// ProfileHandlerUnregisterCallback as a handle to a registered callback.
struct ProfileHandlerToken {
  // Sets the callback and associated arg.
  ProfileHandlerToken(ProfileHandlerCallback cb, void* cb_arg)
      : callback(cb),
        callback_arg(cb_arg) {
  }

  // Callback function to be invoked on receiving a profile timer interrupt.
  ProfileHandlerCallback callback;
  // Argument for the callback function.
  void* callback_arg;
};

// Blocks a signal from being delivered to the current thread while the object
// is alive. Unblocks it upon destruction.
class ScopedSignalBlocker {
 public:
  ScopedSignalBlocker(int signo) {
    sigemptyset(&sig_set_);
    sigaddset(&sig_set_, signo);
    RAW_CHECK(sigprocmask(SIG_BLOCK, &sig_set_, NULL) == 0,
              "sigprocmask (block)");
  }
  ~ScopedSignalBlocker() {
    RAW_CHECK(sigprocmask(SIG_UNBLOCK, &sig_set_, NULL) == 0,
              "sigprocmask (unblock)");
  }

 private:
  sigset_t sig_set_;
};

// This class manages profile timers and associated signal handler. This is a
// a singleton.
class ProfileHandler {
 public:
  // Registers the current thread with the profile handler.
  void RegisterThread();

  // Registers a callback routine to receive profile timer ticks. The returned
  // token is to be used when unregistering this callback and must not be
  // deleted by the caller.
  ProfileHandlerToken* RegisterCallback(ProfileHandlerCallback callback,
                                        void* callback_arg);

  // Unregisters a previously registered callback. Expects the token returned
  // by the corresponding RegisterCallback routine.
  void UnregisterCallback(ProfileHandlerToken* token)
      NO_THREAD_SAFETY_ANALYSIS;

  // Unregisters all the callbacks and stops the timer(s).
  void Reset();

  // Gets the current state of profile handler.
  void GetState(ProfileHandlerState* state);

  // Initializes and returns the ProfileHandler singleton.
  static ProfileHandler* Instance();

 private:
  ProfileHandler();
  ~ProfileHandler();

  // Largest allowed frequency.
  static const int32 kMaxFrequency = 4000;
  // Default frequency.
  static const int32 kDefaultFrequency = 100;

  // ProfileHandler singleton.
  static ProfileHandler* instance_;

  // pthread_once_t for one time initialization of ProfileHandler singleton.
  static pthread_once_t once_;

  // Initializes the ProfileHandler singleton via GoogleOnceInit.
  static void Init();

  // Timer state as configured previously.
  bool timer_running_;

  // The number of profiling signal interrupts received.
  int64 interrupts_ GUARDED_BY(signal_lock_);

  // Profiling signal interrupt frequency, read-only after construction.
  int32 frequency_;

  // ITIMER_PROF (which uses SIGPROF), or ITIMER_REAL (which uses SIGALRM).
  // Translated into an equivalent choice of clock if per_thread_timer_enabled_
  // is true.
  int timer_type_;

  // Signal number for timer signal.
  int signal_number_;

  // Counts the number of callbacks registered.
  int32 callback_count_ GUARDED_BY(control_lock_);

  // Is profiling allowed at all?
  bool allowed_;

  // Must be false if HAVE_LINUX_SIGEV_THREAD_ID is not defined.
  bool per_thread_timer_enabled_;

#ifdef HAVE_LINUX_SIGEV_THREAD_ID
  // this is used to destroy per-thread profiling timers on thread
  // termination
  pthread_key_t thread_timer_key;
#endif

  // This lock serializes the registration of threads and protects the
  // callbacks_ list below.
  // Locking order:
  // In the context of a signal handler, acquire signal_lock_ to walk the
  // callback list. Otherwise, acquire control_lock_, disable the signal
  // handler and then acquire signal_lock_.
  SpinLock control_lock_ ACQUIRED_BEFORE(signal_lock_);
  SpinLock signal_lock_;

  // Holds the list of registered callbacks. We expect the list to be pretty
  // small. Currently, the cpu profiler (base/profiler) and thread module
  // (base/thread.h) are the only two components registering callbacks.
  // Following are the locking requirements for callbacks_:
  // For read-write access outside the SIGPROF handler:
  //  - Acquire control_lock_
  //  - Disable SIGPROF handler.
  //  - Acquire signal_lock_
  // For read-only access in the context of SIGPROF handler
  // (Read-write access is *not allowed* in the SIGPROF handler)
  //  - Acquire signal_lock_
  // For read-only access outside SIGPROF handler:
  //  - Acquire control_lock_
  typedef list<ProfileHandlerToken*> CallbackList;
  typedef CallbackList::iterator CallbackIterator;
  CallbackList callbacks_ GUARDED_BY(signal_lock_);

  // Starts or stops the interval timer.
  // Will ignore any requests to enable or disable when
  // per_thread_timer_enabled_ is true.
  void UpdateTimer(bool enable) EXCLUSIVE_LOCKS_REQUIRED(signal_lock_);

  // Returns true if the handler is not being used by something else.
  // This checks the kernel's signal handler table.
  bool IsSignalHandlerAvailable();

  // Signal handler. Iterates over and calls all the registered callbacks.
  static void SignalHandler(int sig, siginfo_t* sinfo, void* ucontext);

  DISALLOW_COPY_AND_ASSIGN(ProfileHandler);
};

ProfileHandler* ProfileHandler::instance_ = NULL;
pthread_once_t ProfileHandler::once_ = PTHREAD_ONCE_INIT;

const int32 ProfileHandler::kMaxFrequency;
const int32 ProfileHandler::kDefaultFrequency;

// If we are LD_PRELOAD-ed against a non-pthreads app, then these functions
// won't be defined.  We declare them here, for that case (with weak linkage)
// which will cause the non-definition to resolve to NULL.  We can then check
// for NULL or not in Instance.
extern "C" {
int pthread_once(pthread_once_t *, void (*)(void)) ATTRIBUTE_WEAK;
int pthread_kill(pthread_t thread_id, int signo) ATTRIBUTE_WEAK;

#if HAVE_LINUX_SIGEV_THREAD_ID
int timer_create(clockid_t clockid, struct sigevent* evp,
                 timer_t* timerid) ATTRIBUTE_WEAK;
int timer_delete(timer_t timerid) ATTRIBUTE_WEAK;
int timer_settime(timer_t timerid, int flags, const struct itimerspec* value,
                  struct itimerspec* ovalue) ATTRIBUTE_WEAK;
#endif
}

#if HAVE_LINUX_SIGEV_THREAD_ID

struct timer_id_holder {
  timer_t timerid;
  timer_id_holder(timer_t _timerid) : timerid(_timerid) {}
};

extern "C" {
  static void ThreadTimerDestructor(void *arg) {
    if (!arg) {
      return;
    }
    timer_id_holder *holder = static_cast<timer_id_holder *>(arg);
    timer_delete(holder->timerid);
    delete holder;
  }
}

static void CreateThreadTimerKey(pthread_key_t *pkey) {
  int rv = perftools_pthread_key_create(pkey, ThreadTimerDestructor);
  if (rv) {
    RAW_LOG(FATAL, "aborting due to pthread_key_create error: %s", strerror(rv));
  }
}

static void StartLinuxThreadTimer(int timer_type, int signal_number,
                                  int32 frequency, pthread_key_t timer_key) {
  int rv;
  struct sigevent sevp;
  timer_t timerid;
  struct itimerspec its;
  memset(&sevp, 0, sizeof(sevp));
  sevp.sigev_notify = SIGEV_THREAD_ID;
  sevp.sigev_notify_thread_id = syscall(SYS_gettid);
  sevp.sigev_signo = signal_number;
  clockid_t clock = CLOCK_THREAD_CPUTIME_ID;
  if (timer_type == ITIMER_REAL) {
    clock = CLOCK_MONOTONIC;
  }
  rv = timer_create(clock, &sevp, &timerid);
  if (rv) {
    RAW_LOG(FATAL, "aborting due to timer_create error: %s", strerror(errno));
  }

  timer_id_holder *holder = new timer_id_holder(timerid);
  rv = perftools_pthread_setspecific(timer_key, holder);
  if (rv) {
    RAW_LOG(FATAL, "aborting due to pthread_setspecific error: %s", strerror(rv));
  }

  its.it_interval.tv_sec = 0;
  its.it_interval.tv_nsec = 1000000000 / frequency;
  its.it_value = its.it_interval;
  rv = timer_settime(timerid, 0, &its, 0);
  if (rv) {
    RAW_LOG(FATAL, "aborting due to timer_settime error: %s", strerror(errno));
  }
}
#endif

void ProfileHandler::Init() {
  instance_ = new ProfileHandler();
}

ProfileHandler* ProfileHandler::Instance() {
  if (pthread_once) {
    pthread_once(&once_, Init);
  }
  if (instance_ == NULL) {
    // This will be true on systems that don't link in pthreads,
    // including on FreeBSD where pthread_once has a non-zero address
    // (but doesn't do anything) even when pthreads isn't linked in.
    Init();
    assert(instance_ != NULL);
  }
  return instance_;
}

ProfileHandler::ProfileHandler()
    : timer_running_(false),
      interrupts_(0),
      callback_count_(0),
      allowed_(true),
      per_thread_timer_enabled_(false) {
  SpinLockHolder cl(&control_lock_);

  timer_type_ = (getenv("CPUPROFILE_REALTIME") ? ITIMER_REAL : ITIMER_PROF);
  signal_number_ = (timer_type_ == ITIMER_PROF ? SIGPROF : SIGALRM);

  // Get frequency of interrupts (if specified)
  char junk;
  const char* fr = getenv("CPUPROFILE_FREQUENCY");
  if (fr != NULL && (sscanf(fr, "%u%c", &frequency_, &junk) == 1) &&
      (frequency_ > 0)) {
    // Limit to kMaxFrequency
    frequency_ = (frequency_ > kMaxFrequency) ? kMaxFrequency : frequency_;
  } else {
    frequency_ = kDefaultFrequency;
  }

  if (!allowed_) {
    return;
  }

#if HAVE_LINUX_SIGEV_THREAD_ID
  // Do this early because we might be overriding signal number.

  const char *per_thread = getenv("CPUPROFILE_PER_THREAD_TIMERS");
  const char *signal_number = getenv("CPUPROFILE_TIMER_SIGNAL");

  if (per_thread || signal_number) {
    if (timer_create && pthread_once) {
      CreateThreadTimerKey(&thread_timer_key);
      per_thread_timer_enabled_ = true;
      // Override signal number if requested.
      if (signal_number) {
        signal_number_ = strtol(signal_number, NULL, 0);
      }
    } else {
      RAW_LOG(INFO,
              "Ignoring CPUPROFILE_PER_THREAD_TIMERS and\n"
              " CPUPROFILE_TIMER_SIGNAL due to lack of timer_create().\n"
              " Preload or link to librt.so for this to work");
    }
  }
#endif

  // If something else is using the signal handler,
  // assume it has priority over us and stop.
  if (!IsSignalHandlerAvailable()) {
    RAW_LOG(INFO, "Disabling profiler because signal %d handler is already in use.",
            signal_number_);
    allowed_ = false;
    return;
  }

  // Install the signal handler.
  struct sigaction sa;
  sa.sa_sigaction = SignalHandler;
  sa.sa_flags = SA_RESTART | SA_SIGINFO;
  sigemptyset(&sa.sa_mask);
  RAW_CHECK(sigaction(signal_number_, &sa, NULL) == 0, "sigprof (enable)");
}

ProfileHandler::~ProfileHandler() {
  Reset();
#ifdef HAVE_LINUX_SIGEV_THREAD_ID
  if (per_thread_timer_enabled_) {
    perftools_pthread_key_delete(thread_timer_key);
  }
#endif
}

void ProfileHandler::RegisterThread() {
  SpinLockHolder cl(&control_lock_);

  if (!allowed_) {
    return;
  }

  // Record the thread identifier and start the timer if profiling is on.
  ScopedSignalBlocker block(signal_number_);
  SpinLockHolder sl(&signal_lock_);
#if HAVE_LINUX_SIGEV_THREAD_ID
  if (per_thread_timer_enabled_) {
    StartLinuxThreadTimer(timer_type_, signal_number_, frequency_,
                          thread_timer_key);
    return;
  }
#endif
  UpdateTimer(callback_count_ > 0);
}

ProfileHandlerToken* ProfileHandler::RegisterCallback(
    ProfileHandlerCallback callback, void* callback_arg) {

  ProfileHandlerToken* token = new ProfileHandlerToken(callback, callback_arg);

  SpinLockHolder cl(&control_lock_);
  {
    ScopedSignalBlocker block(signal_number_);
    SpinLockHolder sl(&signal_lock_);
    callbacks_.push_back(token);
    ++callback_count_;
    UpdateTimer(true);
  }
  return token;
}

void ProfileHandler::UnregisterCallback(ProfileHandlerToken* token) {
  SpinLockHolder cl(&control_lock_);
  for (CallbackIterator it = callbacks_.begin(); it != callbacks_.end();
       ++it) {
    if ((*it) == token) {
      RAW_CHECK(callback_count_ > 0, "Invalid callback count");
      {
        ScopedSignalBlocker block(signal_number_);
        SpinLockHolder sl(&signal_lock_);
        delete *it;
        callbacks_.erase(it);
        --callback_count_;
        if (callback_count_ == 0)
          UpdateTimer(false);
      }
      return;
    }
  }
  // Unknown token.
  RAW_LOG(FATAL, "Invalid token");
}

void ProfileHandler::Reset() {
  SpinLockHolder cl(&control_lock_);
  {
    ScopedSignalBlocker block(signal_number_);
    SpinLockHolder sl(&signal_lock_);
    CallbackIterator it = callbacks_.begin();
    while (it != callbacks_.end()) {
      CallbackIterator tmp = it;
      ++it;
      delete *tmp;
      callbacks_.erase(tmp);
    }
    callback_count_ = 0;
    UpdateTimer(false);
  }
}

void ProfileHandler::GetState(ProfileHandlerState* state) {
  SpinLockHolder cl(&control_lock_);
  {
    ScopedSignalBlocker block(signal_number_);
    SpinLockHolder sl(&signal_lock_);  // Protects interrupts_.
    state->interrupts = interrupts_;
  }
  state->frequency = frequency_;
  state->callback_count = callback_count_;
  state->allowed = allowed_;
}

void ProfileHandler::UpdateTimer(bool enable) {
  if (per_thread_timer_enabled_) {
    // Ignore any attempts to disable it because that's not supported, and it's
    // always enabled so enabling is always a NOP.
    return;
  }

  if (enable == timer_running_) {
    return;
  }
  timer_running_ = enable;

  struct itimerval timer;
  static const int kMillion = 1000000;
  int interval_usec = enable ? kMillion / frequency_ : 0;
  timer.it_interval.tv_sec = interval_usec / kMillion;
  timer.it_interval.tv_usec = interval_usec % kMillion;
  timer.it_value = timer.it_interval;
  setitimer(timer_type_, &timer, 0);
}

bool ProfileHandler::IsSignalHandlerAvailable() {
  struct sigaction sa;
  RAW_CHECK(sigaction(signal_number_, NULL, &sa) == 0, "is-signal-handler avail");

  // We only take over the handler if the current one is unset.
  // It must be SIG_IGN or SIG_DFL, not some other function.
  // SIG_IGN must be allowed because when profiling is allowed but
  // not actively in use, this code keeps the handler set to SIG_IGN.
  // That setting will be inherited across fork+exec.  In order for
  // any child to be able to use profiling, SIG_IGN must be treated
  // as available.
  return sa.sa_handler == SIG_IGN || sa.sa_handler == SIG_DFL;
}

void ProfileHandler::SignalHandler(int sig, siginfo_t* sinfo, void* ucontext) {
  int saved_errno = errno;
  // At this moment, instance_ must be initialized because the handler is
  // enabled in RegisterThread or RegisterCallback only after
  // ProfileHandler::Instance runs.
  ProfileHandler* instance = instance_;
  RAW_CHECK(instance != NULL, "ProfileHandler is not initialized");
  {
    SpinLockHolder sl(&instance->signal_lock_);
    ++instance->interrupts_;
    for (CallbackIterator it = instance->callbacks_.begin();
         it != instance->callbacks_.end();
         ++it) {
      (*it)->callback(sig, sinfo, ucontext, (*it)->callback_arg);
    }
  }
  errno = saved_errno;
}

// This module initializer registers the main thread, so it must be
// executed in the context of the main thread.
REGISTER_MODULE_INITIALIZER(profile_main, ProfileHandlerRegisterThread());

void ProfileHandlerRegisterThread() {
  ProfileHandler::Instance()->RegisterThread();
}

ProfileHandlerToken* ProfileHandlerRegisterCallback(
    ProfileHandlerCallback callback, void* callback_arg) {
  return ProfileHandler::Instance()->RegisterCallback(callback, callback_arg);
}

void ProfileHandlerUnregisterCallback(ProfileHandlerToken* token) {
  ProfileHandler::Instance()->UnregisterCallback(token);
}

void ProfileHandlerReset() {
  return ProfileHandler::Instance()->Reset();
}

void ProfileHandlerGetState(ProfileHandlerState* state) {
  ProfileHandler::Instance()->GetState(state);
}

#else  // OS_CYGWIN

// ITIMER_PROF doesn't work under cygwin.  ITIMER_REAL is available, but doesn't
// work as well for profiling, and also interferes with alarm().  Because of
// these issues, unless a specific need is identified, profiler support is
// disabled under Cygwin.
void ProfileHandlerRegisterThread() {
}

ProfileHandlerToken* ProfileHandlerRegisterCallback(
    ProfileHandlerCallback callback, void* callback_arg) {
  return NULL;
}

void ProfileHandlerUnregisterCallback(ProfileHandlerToken* token) {
}

void ProfileHandlerReset() {
}

void ProfileHandlerGetState(ProfileHandlerState* state) {
}

#endif  // OS_CYGWIN
