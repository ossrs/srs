/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2019 Haivision Systems Inc.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 */
#pragma once
#ifndef INC_SRT_SYNC_H
#define INC_SRT_SYNC_H

#include "platform_sys.h"

#include <cstdlib>
#include <limits>
#ifdef ENABLE_STDCXX_SYNC
#include <chrono>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#define SRT_SYNC_CLOCK SRT_SYNC_CLOCK_STDCXX_STEADY
#define SRT_SYNC_CLOCK_STR "STDCXX_STEADY"
#else
#include <pthread.h>

// Defile clock type to use
#ifdef IA32
#define SRT_SYNC_CLOCK SRT_SYNC_CLOCK_IA32_RDTSC
#define SRT_SYNC_CLOCK_STR "IA32_RDTSC"
#elif defined(IA64)
#define SRT_SYNC_CLOCK SRT_SYNC_CLOCK_IA64_ITC
#define SRT_SYNC_CLOCK_STR "IA64_ITC"
#elif defined(AMD64)
#define SRT_SYNC_CLOCK SRT_SYNC_CLOCK_AMD64_RDTSC
#define SRT_SYNC_CLOCK_STR "AMD64_RDTSC"
#elif defined(_WIN32)
#define SRT_SYNC_CLOCK SRT_SYNC_CLOCK_WINQPC
#define SRT_SYNC_CLOCK_STR "WINQPC"
#elif TARGET_OS_MAC
#define SRT_SYNC_CLOCK SRT_SYNC_CLOCK_MACH_ABSTIME
#define SRT_SYNC_CLOCK_STR "MACH_ABSTIME"
#elif defined(ENABLE_MONOTONIC_CLOCK)
#define SRT_SYNC_CLOCK SRT_SYNC_CLOCK_GETTIME_MONOTONIC
#define SRT_SYNC_CLOCK_STR "GETTIME_MONOTONIC"
#else
#define SRT_SYNC_CLOCK SRT_SYNC_CLOCK_POSIX_GETTIMEOFDAY
#define SRT_SYNC_CLOCK_STR "POSIX_GETTIMEOFDAY"
#endif

#endif // ENABLE_STDCXX_SYNC

#include "srt.h"
#include "utilities.h"
#include "srt_attr_defs.h"


namespace srt
{

class CUDTException;    // defined in common.h

namespace sync
{

///////////////////////////////////////////////////////////////////////////////
//
// Duration class
//
///////////////////////////////////////////////////////////////////////////////

#if ENABLE_STDCXX_SYNC

template <class Clock>
using Duration = std::chrono::duration<Clock>;

#else

/// Class template srt::sync::Duration represents a time interval.
/// It consists of a count of ticks of _Clock.
/// It is a wrapper of system timers in case of non-C++11 chrono build.
template <class Clock>
class Duration
{
public:
    Duration()
        : m_duration(0)
    {
    }

    explicit Duration(int64_t d)
        : m_duration(d)
    {
    }

public:
    inline int64_t count() const { return m_duration; }

    static Duration zero() { return Duration(); }

public: // Relational operators
    inline bool operator>=(const Duration& rhs) const { return m_duration >= rhs.m_duration; }
    inline bool operator>(const Duration& rhs) const { return m_duration > rhs.m_duration; }
    inline bool operator==(const Duration& rhs) const { return m_duration == rhs.m_duration; }
    inline bool operator!=(const Duration& rhs) const { return m_duration != rhs.m_duration; }
    inline bool operator<=(const Duration& rhs) const { return m_duration <= rhs.m_duration; }
    inline bool operator<(const Duration& rhs) const { return m_duration < rhs.m_duration; }

public: // Assignment operators
    inline void operator*=(const int64_t mult) { m_duration = static_cast<int64_t>(m_duration * mult); }
    inline void operator+=(const Duration& rhs) { m_duration += rhs.m_duration; }
    inline void operator-=(const Duration& rhs) { m_duration -= rhs.m_duration; }

    inline Duration operator+(const Duration& rhs) const { return Duration(m_duration + rhs.m_duration); }
    inline Duration operator-(const Duration& rhs) const { return Duration(m_duration - rhs.m_duration); }
    inline Duration operator*(const int64_t& rhs) const { return Duration(m_duration * rhs); }
    inline Duration operator/(const int64_t& rhs) const { return Duration(m_duration / rhs); }

private:
    // int64_t range is from -9,223,372,036,854,775,808 to 9,223,372,036,854,775,807
    int64_t m_duration;
};

#endif // ENABLE_STDCXX_SYNC

///////////////////////////////////////////////////////////////////////////////
//
// TimePoint and steadt_clock classes
//
///////////////////////////////////////////////////////////////////////////////

#if ENABLE_STDCXX_SYNC

using steady_clock = std::chrono::steady_clock;

template <class Clock, class Duration = typename Clock::duration>
using time_point = std::chrono::time_point<Clock, Duration>;

template <class Clock>
using TimePoint = std::chrono::time_point<Clock>;

template <class Clock, class Duration = typename Clock::duration>
inline bool is_zero(const time_point<Clock, Duration> &tp)
{
    return tp.time_since_epoch() == Clock::duration::zero();
}

inline bool is_zero(const steady_clock::time_point& t)
{
    return t == steady_clock::time_point();
}

#else
template <class Clock>
class TimePoint;

class steady_clock
{
public:
    typedef Duration<steady_clock>  duration;
    typedef TimePoint<steady_clock> time_point;

public:
    static time_point now();
};

/// Represents a point in time
template <class Clock>
class TimePoint
{
public:
    TimePoint()
        : m_timestamp(0)
    {
    }

    explicit TimePoint(uint64_t tp)
        : m_timestamp(tp)
    {
    }

    TimePoint(const TimePoint<Clock>& other)
        : m_timestamp(other.m_timestamp)
    {
    }

    TimePoint(const Duration<Clock>& duration_since_epoch)
        : m_timestamp(duration_since_epoch.count())
    {
    }

    ~TimePoint() {}

public: // Relational operators
    inline bool operator<(const TimePoint<Clock>& rhs) const { return m_timestamp < rhs.m_timestamp; }
    inline bool operator<=(const TimePoint<Clock>& rhs) const { return m_timestamp <= rhs.m_timestamp; }
    inline bool operator==(const TimePoint<Clock>& rhs) const { return m_timestamp == rhs.m_timestamp; }
    inline bool operator!=(const TimePoint<Clock>& rhs) const { return m_timestamp != rhs.m_timestamp; }
    inline bool operator>=(const TimePoint<Clock>& rhs) const { return m_timestamp >= rhs.m_timestamp; }
    inline bool operator>(const TimePoint<Clock>& rhs) const { return m_timestamp > rhs.m_timestamp; }

public: // Arithmetic operators
    inline Duration<Clock> operator-(const TimePoint<Clock>& rhs) const
    {
        return Duration<Clock>(m_timestamp - rhs.m_timestamp);
    }
    inline TimePoint operator+(const Duration<Clock>& rhs) const { return TimePoint(m_timestamp + rhs.count()); }
    inline TimePoint operator-(const Duration<Clock>& rhs) const { return TimePoint(m_timestamp - rhs.count()); }

public: // Assignment operators
    inline void operator=(const TimePoint<Clock>& rhs) { m_timestamp = rhs.m_timestamp; }
    inline void operator+=(const Duration<Clock>& rhs) { m_timestamp += rhs.count(); }
    inline void operator-=(const Duration<Clock>& rhs) { m_timestamp -= rhs.count(); }

public: //
    static inline ATR_CONSTEXPR TimePoint min() { return TimePoint(std::numeric_limits<uint64_t>::min()); }
    static inline ATR_CONSTEXPR TimePoint max() { return TimePoint(std::numeric_limits<uint64_t>::max()); }

public:
    Duration<Clock> time_since_epoch() const;

private:
    uint64_t m_timestamp;
};

template <>
srt::sync::Duration<srt::sync::steady_clock> srt::sync::TimePoint<srt::sync::steady_clock>::time_since_epoch() const;

inline Duration<steady_clock> operator*(const int& lhs, const Duration<steady_clock>& rhs)
{
    return rhs * lhs;
}

#endif // ENABLE_STDCXX_SYNC

// NOTE: Moved the following class definitions to "atomic_clock.h"
//   template <class Clock>
//      class AtomicDuration;
//   template <class Clock>
//      class AtomicClock;

///////////////////////////////////////////////////////////////////////////////
//
// Duration and timepoint conversions
//
///////////////////////////////////////////////////////////////////////////////

/// Function return number of decimals in a subsecond precision.
/// E.g. for a microsecond accuracy of steady_clock the return would be 6.
/// For a nanosecond accuracy of the steady_clock the return value would be 9.
int clockSubsecondPrecision();

#if ENABLE_STDCXX_SYNC

inline long long count_microseconds(const steady_clock::duration &t)
{
    return std::chrono::duration_cast<std::chrono::microseconds>(t).count();
}

inline long long count_microseconds(const steady_clock::time_point tp)
{
    return std::chrono::duration_cast<std::chrono::microseconds>(tp.time_since_epoch()).count();
}

inline long long count_milliseconds(const steady_clock::duration &t)
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(t).count();
}

inline long long count_seconds(const steady_clock::duration &t)
{
    return std::chrono::duration_cast<std::chrono::seconds>(t).count();
}

inline steady_clock::duration microseconds_from(int64_t t_us)
{
    return std::chrono::microseconds(t_us);
}

inline steady_clock::duration milliseconds_from(int64_t t_ms)
{
    return std::chrono::milliseconds(t_ms);
}

inline steady_clock::duration seconds_from(int64_t t_s)
{
    return std::chrono::seconds(t_s);
}

#else

int64_t count_microseconds(const steady_clock::duration& t);
int64_t count_milliseconds(const steady_clock::duration& t);
int64_t count_seconds(const steady_clock::duration& t);

Duration<steady_clock> microseconds_from(int64_t t_us);
Duration<steady_clock> milliseconds_from(int64_t t_ms);
Duration<steady_clock> seconds_from(int64_t t_s);

inline bool is_zero(const TimePoint<steady_clock>& t)
{
    return t == TimePoint<steady_clock>();
}

#endif // ENABLE_STDCXX_SYNC


///////////////////////////////////////////////////////////////////////////////
//
// Mutex section
//
///////////////////////////////////////////////////////////////////////////////

#if ENABLE_STDCXX_SYNC
using Mutex = std::mutex;
using UniqueLock = std::unique_lock<std::mutex>;
using ScopedLock = std::lock_guard<std::mutex>;
#else
/// Mutex is a class wrapper, that should mimic the std::chrono::mutex class.
/// At the moment the extra function ref() is temporally added to allow calls
/// to pthread_cond_timedwait(). Will be removed by introducing CEvent.
class SRT_ATTR_CAPABILITY("mutex") Mutex
{
    friend class SyncEvent;

public:
    Mutex();
    ~Mutex();

public:
    int lock() SRT_ATTR_ACQUIRE();
    int unlock() SRT_ATTR_RELEASE();

    /// @return     true if the lock was acquired successfully, otherwise false
    bool try_lock() SRT_ATTR_TRY_ACQUIRE(true);

    // TODO: To be removed with introduction of the CEvent.
    pthread_mutex_t& ref() { return m_mutex; }

private:
    pthread_mutex_t m_mutex;
};

/// A pthread version of std::chrono::scoped_lock<mutex> (or lock_guard for C++11)
class SRT_ATTR_SCOPED_CAPABILITY ScopedLock
{
public:
    SRT_ATTR_ACQUIRE(m)
    explicit ScopedLock(Mutex& m);

    SRT_ATTR_RELEASE()
    ~ScopedLock();

private:
    Mutex& m_mutex;
};

/// A pthread version of std::chrono::unique_lock<mutex>
class SRT_ATTR_SCOPED_CAPABILITY UniqueLock
{
    friend class SyncEvent;
    int m_iLocked;
    Mutex& m_Mutex;

public:
    SRT_ATTR_ACQUIRE(m)
    explicit UniqueLock(Mutex &m);

    SRT_ATTR_RELEASE()
    ~UniqueLock();

public:
    SRT_ATTR_ACQUIRE()
    void lock();

    SRT_ATTR_RELEASE()
    void unlock();

    SRT_ATTR_RETURN_CAPABILITY(m_Mutex)
    Mutex* mutex(); // reflects C++11 unique_lock::mutex()
};
#endif // ENABLE_STDCXX_SYNC

inline void enterCS(Mutex& m) SRT_ATTR_EXCLUDES(m) SRT_ATTR_ACQUIRE(m) { m.lock(); }

inline bool tryEnterCS(Mutex& m) SRT_ATTR_EXCLUDES(m) SRT_ATTR_TRY_ACQUIRE(true, m) { return m.try_lock(); }

inline void leaveCS(Mutex& m) SRT_ATTR_REQUIRES(m) SRT_ATTR_RELEASE(m) { m.unlock(); }

class InvertedLock
{
    Mutex& m_mtx;

public:
    SRT_ATTR_REQUIRES(m) SRT_ATTR_RELEASE(m)
    InvertedLock(Mutex& m)
        : m_mtx(m)
    {
        m_mtx.unlock();
    }

    SRT_ATTR_ACQUIRE(m_mtx)
    ~InvertedLock()
    {
        m_mtx.lock();
    }
};

inline void setupMutex(Mutex&, const char*) {}
inline void releaseMutex(Mutex&) {}

////////////////////////////////////////////////////////////////////////////////
//
// Condition section
//
////////////////////////////////////////////////////////////////////////////////

class Condition
{
public:
    Condition();
    ~Condition();

public:
    /// These functions do not align with C++11 version. They are here hopefully as a temporal solution
    /// to avoud issues with static initialization of CV on windows.
    void init();
    void destroy();

public:
    /// Causes the current thread to block until the condition variable is notified
    /// or a spurious wakeup occurs.
    ///
    /// @param lock Corresponding mutex locked by UniqueLock
    void wait(UniqueLock& lock);

    /// Atomically releases lock, blocks the current executing thread, 
    /// and adds it to the list of threads waiting on *this.
    /// The thread will be unblocked when notify_all() or notify_one() is executed,
    /// or when the relative timeout rel_time expires.
    /// It may also be unblocked spuriously. When unblocked, regardless of the reason,
    /// lock is reacquired and wait_for() exits.
    ///
    /// @returns false if the relative timeout specified by rel_time expired,
    ///          true otherwise (signal or spurious wake up).
    ///
    /// @note Calling this function if lock.mutex()
    /// is not locked by the current thread is undefined behavior.
    /// Calling this function if lock.mutex() is not the same mutex as the one
    /// used by all other threads that are currently waiting on the same
    /// condition variable is undefined behavior.
    bool wait_for(UniqueLock& lock, const steady_clock::duration& rel_time);

    /// Causes the current thread to block until the condition variable is notified,
    /// a specific time is reached, or a spurious wakeup occurs.
    ///
    /// @param[in] lock  an object of type UniqueLock, which must be locked by the current thread 
    /// @param[in] timeout_time an object of type time_point representing the time when to stop waiting 
    ///
    /// @returns false if the relative timeout specified by timeout_time expired,
    ///          true otherwise (signal or spurious wake up).
    bool wait_until(UniqueLock& lock, const steady_clock::time_point& timeout_time);

    /// Calling notify_one() unblocks one of the waiting threads,
    /// if any threads are waiting on this CV.
    void notify_one();

    /// Unblocks all threads currently waiting for this CV.
    void notify_all();

private:
#if ENABLE_STDCXX_SYNC
    std::condition_variable m_cv;
#else
    pthread_cond_t  m_cv;
#endif
};

inline void setupCond(Condition& cv, const char*) { cv.init(); }
inline void releaseCond(Condition& cv) { cv.destroy(); }

///////////////////////////////////////////////////////////////////////////////
//
// Event (CV) section
//
///////////////////////////////////////////////////////////////////////////////

// This class is used for condition variable combined with mutex by different ways.
// This should provide a cleaner API around locking with debug-logging inside.
class CSync
{
protected:
    Condition* m_cond;
    UniqueLock* m_locker;

public:
    // Locked version: must be declared only after the declaration of UniqueLock,
    // which has locked the mutex. On this delegate you should call only
    // signal_locked() and pass the UniqueLock variable that should remain locked.
    // Also wait() and wait_for() can be used only with this socket.
    CSync(Condition& cond, UniqueLock& g)
        : m_cond(&cond), m_locker(&g)
    {
        // XXX it would be nice to check whether the owner is also current thread
        // but this can't be done portable way.

        // When constructed by this constructor, the user is expected
        // to only call signal_locked() function. You should pass the same guard
        // variable that you have used for construction as its argument.
    }

    // COPY CONSTRUCTOR: DEFAULT!

    // Wait indefinitely, until getting a signal on CV.
    void wait()
    {
        m_cond->wait(*m_locker);
    }

    /// Block the call until either @a timestamp time achieved
    /// or the conditional is signaled.
    /// @param [in] delay Maximum time to wait since the moment of the call
    /// @retval false if the relative timeout specified by rel_time expired,
    /// @retval true if condition is signaled or spurious wake up.
    bool wait_for(const steady_clock::duration& delay)
    {
        return m_cond->wait_for(*m_locker, delay);
    }

    // Wait until the given time is achieved.
    /// @param [in] exptime The target time to wait until.
    /// @retval false if the target wait time is reached.
    /// @retval true if condition is signal or spurious wake up.
    bool wait_until(const steady_clock::time_point& exptime)
    {
        return m_cond->wait_until(*m_locker, exptime);
    }

    // Static ad-hoc version
    static void lock_notify_one(Condition& cond, Mutex& m)
    {
        ScopedLock lk(m); // XXX with thread logging, don't use ScopedLock directly!
        cond.notify_one();
    }

    static void lock_notify_all(Condition& cond, Mutex& m)
    {
        ScopedLock lk(m); // XXX with thread logging, don't use ScopedLock directly!
        cond.notify_all();
    }

    void notify_one_locked(UniqueLock& lk SRT_ATR_UNUSED)
    {
        // EXPECTED: lk.mutex() is LOCKED.
        m_cond->notify_one();
    }

    void notify_all_locked(UniqueLock& lk SRT_ATR_UNUSED)
    {
        // EXPECTED: lk.mutex() is LOCKED.
        m_cond->notify_all();
    }

    // The *_relaxed functions are to be used in case when you don't care
    // whether the associated mutex is locked or not (you accept the case that
    // a mutex isn't locked and the condition notification gets effectively
    // missed), or you somehow know that the mutex is locked, but you don't
    // have access to the associated UniqueLock object. This function, although
    // it does the same thing as CSync::notify_one_locked etc. here for the
    // user to declare explicitly that notifying is done without being
    // prematurely certain that the associated mutex is locked.
    //
    // It is then expected that whenever these functions are used, an extra
    // comment is provided to explain, why the use of the relaxed notification
    // is correctly used.

    void notify_one_relaxed() { notify_one_relaxed(*m_cond); }
    static void notify_one_relaxed(Condition& cond) { cond.notify_one(); }
    static void notify_all_relaxed(Condition& cond) { cond.notify_all(); }
};

////////////////////////////////////////////////////////////////////////////////
//
// CEvent class
//
////////////////////////////////////////////////////////////////////////////////

// XXX Do not use this class now, there's an unknown issue
// connected to object management with the use of release* functions.
// Until this is solved, stay with separate *Cond and *Lock fields.
class CEvent
{
public:
    CEvent();
    ~CEvent();

public:
    Mutex& mutex() { return m_lock; }
    Condition& cond() { return m_cond; }

public:
    /// Causes the current thread to block until
    /// a specific time is reached.
    ///
    /// @return true  if condition occurred or spuriously woken up
    ///         false on timeout
    bool lock_wait_until(const steady_clock::time_point& tp);

    /// Blocks the current executing thread,
    /// and adds it to the list of threads waiting on* this.
    /// The thread will be unblocked when notify_all() or notify_one() is executed,
    /// or when the relative timeout rel_time expires.
    /// It may also be unblocked spuriously.
    /// Uses internal mutex to lock.
    ///
    /// @return true  if condition occurred or spuriously woken up
    ///         false on timeout
    bool lock_wait_for(const steady_clock::duration& rel_time);

    /// Atomically releases lock, blocks the current executing thread,
    /// and adds it to the list of threads waiting on* this.
    /// The thread will be unblocked when notify_all() or notify_one() is executed,
    /// or when the relative timeout rel_time expires.
    /// It may also be unblocked spuriously.
    /// When unblocked, regardless of the reason, lock is reacquiredand wait_for() exits.
    ///
    /// @return true  if condition occurred or spuriously woken up
    ///         false on timeout
    bool wait_for(UniqueLock& lk, const steady_clock::duration& rel_time);

    void lock_wait();

    void wait(UniqueLock& lk);

    void notify_one();

    void notify_all();

    void lock_notify_one()
    {
        ScopedLock lk(m_lock); // XXX with thread logging, don't use ScopedLock directly!
        m_cond.notify_one();
    }

    void lock_notify_all()
    {
        ScopedLock lk(m_lock); // XXX with thread logging, don't use ScopedLock directly!
        m_cond.notify_all();
    }

private:
    Mutex      m_lock;
    Condition  m_cond;
};


// This class binds together the functionality of
// UniqueLock and CSync. It provides a simple interface of CSync
// while having already the UniqueLock applied in the scope,
// so a safe statement can be made about the mutex being locked
// when signalling or waiting.
class CUniqueSync: public CSync
{
    UniqueLock m_ulock;

public:

    UniqueLock& locker() { return m_ulock; }

    CUniqueSync(Mutex& mut, Condition& cnd)
        : CSync(cnd, m_ulock)
        , m_ulock(mut)
    {
    }

    CUniqueSync(CEvent& event)
        : CSync(event.cond(), m_ulock)
        , m_ulock(event.mutex())
    {
    }

    // These functions can be used safely because
    // this whole class guarantees that whatever happens
    // while its object exists is that the mutex is locked.

    void notify_one()
    {
        m_cond->notify_one();
    }

    void notify_all()
    {
        m_cond->notify_all();
    }
};

class CTimer
{
public:
    CTimer();
    ~CTimer();

public:
    /// Causes the current thread to block until
    /// the specified time is reached.
    /// Sleep can be interrupted by calling interrupt()
    /// or woken up to recheck the scheduled time by tick()
    /// @param tp target time to sleep until
    ///
    /// @return true  if the specified time was reached
    ///         false should never happen
    bool sleep_until(steady_clock::time_point tp);

    /// Resets target wait time and interrupts waiting
    /// in sleep_until(..)
    void interrupt();

    /// Wakes up waiting thread (sleep_until(..)) without
    /// changing the target waiting time to force a recheck
    /// of the current time in comparisson to the target time.
    void tick();

private:
    CEvent m_event;
    steady_clock::time_point m_tsSchedTime;
};


/// Print steady clock timepoint in a human readable way.
/// days HH:MM:SS.us [STD]
/// Example: 1D 02:12:56.123456
///
/// @param [in] steady clock timepoint
/// @returns a string with a formatted time representation
std::string FormatTime(const steady_clock::time_point& time);

/// Print steady clock timepoint relative to the current system time
/// Date HH:MM:SS.us [SYS]
/// @param [in] steady clock timepoint
/// @returns a string with a formatted time representation
std::string FormatTimeSys(const steady_clock::time_point& time);

enum eDurationUnit {DUNIT_S, DUNIT_MS, DUNIT_US};

template <eDurationUnit u>
struct DurationUnitName;

template<>
struct DurationUnitName<DUNIT_US>
{
    static const char* name() { return "us"; }
    static double count(const steady_clock::duration& dur) { return static_cast<double>(count_microseconds(dur)); }
};

template<>
struct DurationUnitName<DUNIT_MS>
{
    static const char* name() { return "ms"; }
    static double count(const steady_clock::duration& dur) { return static_cast<double>(count_microseconds(dur))/1000.0; }
};

template<>
struct DurationUnitName<DUNIT_S>
{
    static const char* name() { return "s"; }
    static double count(const steady_clock::duration& dur) { return static_cast<double>(count_microseconds(dur))/1000000.0; }
};

template<eDurationUnit UNIT>
inline std::string FormatDuration(const steady_clock::duration& dur)
{
    return Sprint(DurationUnitName<UNIT>::count(dur)) + DurationUnitName<UNIT>::name();
}

inline std::string FormatDuration(const steady_clock::duration& dur)
{
    return FormatDuration<DUNIT_US>(dur);
}

////////////////////////////////////////////////////////////////////////////////
//
// CGlobEvent class
//
////////////////////////////////////////////////////////////////////////////////

class CGlobEvent
{
public:
    /// Triggers the event and notifies waiting threads.
    /// Simply calls notify_one().
    static void triggerEvent();

    /// Waits for the event to be triggered with 10ms timeout.
    /// Simply calls wait_for().
    static bool waitForEvent();
};

////////////////////////////////////////////////////////////////////////////////
//
// CThread class
//
////////////////////////////////////////////////////////////////////////////////

#ifdef ENABLE_STDCXX_SYNC
typedef std::system_error CThreadException;
using CThread = std::thread;
namespace this_thread = std::this_thread;
#else // pthreads wrapper version
typedef CUDTException CThreadException;

class CThread
{
public:
    CThread();
    /// @throws std::system_error if the thread could not be started.
    CThread(void *(*start_routine) (void *), void *arg);

#if HAVE_FULL_CXX11
    CThread& operator=(CThread &other) = delete;
    CThread& operator=(CThread &&other);
#else
    CThread& operator=(CThread &other);
    /// To be used only in StartThread function.
    /// Creates a new stread and assigns to this.
    /// @throw CThreadException
    void create_thread(void *(*start_routine) (void *), void *arg);
#endif

public: // Observers
    /// Checks if the CThread object identifies an active thread of execution.
    /// A default constructed thread is not joinable.
    /// A thread that has finished executing code, but has not yet been joined
    /// is still considered an active thread of execution and is therefore joinable.
    bool joinable() const;

    struct id
    {
        explicit id(const pthread_t t)
            : value(t)
        {}

        const pthread_t value;
        inline bool operator==(const id& second) const
        {
            return pthread_equal(value, second.value) != 0;
        }
    };

    /// Returns the id of the current thread.
    /// In this implementation the ID is the pthread_t.
    const id get_id() const { return id(m_thread); }

public:
    /// Blocks the current thread until the thread identified by *this finishes its execution.
    /// If that thread has already terminated, then join() returns immediately.
    ///
    /// @throws std::system_error if an error occurs
    void join();

public: // Internal
    /// Calls pthread_create, throws exception on failure.
    /// @throw CThreadException
    void create(void *(*start_routine) (void *), void *arg);

private:
    pthread_t m_thread;
};

template <class Stream>
inline Stream& operator<<(Stream& str, const CThread::id& cid)
{
#if defined(_WIN32) && (defined(PTW32_VERSION) || defined (__PTW32_VERSION))
    // This is a version specific for pthread-win32 implementation
    // Here pthread_t type is a structure that is not convertible
    // to a number at all.
    return str << pthread_getw32threadid_np(cid.value);
#else
    return str << cid.value;
#endif
}

namespace this_thread
{
    const inline CThread::id get_id() { return CThread::id (pthread_self()); }

    inline void sleep_for(const steady_clock::duration& t)
    {
#if !defined(_WIN32)
        usleep(count_microseconds(t)); // microseconds
#else
        Sleep((DWORD) count_milliseconds(t));
#endif
    }
}

#endif

/// StartThread function should be used to do CThread assignments:
/// @code
/// CThread a();
/// a = CThread(func, args);
/// @endcode
///
/// @returns true if thread was started successfully,
///          false on failure
///
#ifdef ENABLE_STDCXX_SYNC
typedef void* (&ThreadFunc) (void*);
bool StartThread(CThread& th, ThreadFunc&& f, void* args, const std::string& name);
#else
bool StartThread(CThread& th, void* (*f) (void*), void* args, const std::string& name);
#endif

////////////////////////////////////////////////////////////////////////////////
//
// CThreadError class - thread local storage wrapper
//
////////////////////////////////////////////////////////////////////////////////

/// Set thread local error
/// @param e new CUDTException
void SetThreadLocalError(const CUDTException& e);

/// Get thread local error
/// @returns CUDTException pointer
CUDTException& GetThreadLocalError();

////////////////////////////////////////////////////////////////////////////////
//
// Random distribution functions.
//
////////////////////////////////////////////////////////////////////////////////

/// Generate a uniform-distributed random integer from [minVal; maxVal].
/// If HAVE_CXX11, uses std::uniform_distribution(std::random_device).
/// @param[in] minVal minimum allowed value of the resulting random number.
/// @param[in] maxVal maximum allowed value of the resulting random number.
int genRandomInt(int minVal, int maxVal);

} // namespace sync
} // namespace srt

#include "atomic_clock.h"

#endif // INC_SRT_SYNC_H
