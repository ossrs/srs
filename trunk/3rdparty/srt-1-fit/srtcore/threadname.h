/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2018 Haivision Systems Inc.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 */

/*****************************************************************************
written by
   Haivision Systems Inc.
 *****************************************************************************/

#ifndef INC_SRT_THREADNAME_H
#define INC_SRT_THREADNAME_H

// NOTE:
//    HAVE_PTHREAD_GETNAME_NP_IN_PTHREAD_NP_H
//    HAVE_PTHREAD_SETNAME_NP_IN_PTHREAD_NP_H
//    HAVE_PTHREAD_GETNAME_NP
//    HAVE_PTHREAD_GETNAME_NP
//    Are detected and set in ../CMakeLists.txt.
//    OS Availability of pthread_getname_np(..) and pthread_setname_np(..)::
//       MacOS(10.6)
//       iOS(3.2)
//       AIX(7.1)
//       FreeBSD(version?), OpenBSD(Version?)
//       Linux-GLIBC(GLIBC-2.12).
//       Linux-MUSL(MUSL-1.1.20 Partial Implementation. See below).
//       MINGW-W64(4.0.6)

#if defined(HAVE_PTHREAD_GETNAME_NP_IN_PTHREAD_NP_H) \
   || defined(HAVE_PTHREAD_SETNAME_NP_IN_PTHREAD_NP_H)
   #include <pthread_np.h>
   #if defined(HAVE_PTHREAD_GETNAME_NP_IN_PTHREAD_NP_H) \
      && !defined(HAVE_PTHREAD_GETNAME_NP)
      #define HAVE_PTHREAD_GETNAME_NP 1
   #endif
   #if defined(HAVE_PTHREAD_SETNAME_NP_IN_PTHREAD_NP_H) \
      && !defined(HAVE_PTHREAD_SETNAME_NP)
      #define HAVE_PTHREAD_SETNAME_NP 1
   #endif
#endif

#if (defined(HAVE_PTHREAD_GETNAME_NP) && defined(HAVE_PTHREAD_GETNAME_NP)) \
   || defined(__linux__)
   // NOTE:
   //    Linux pthread_getname_np() and pthread_setname_np() became available
   //       in GLIBC-2.12 and later.
   //    Some Linux runtimes do not have pthread_getname_np(), but have
   //       pthread_setname_np(), for instance MUSL at least as of v1.1.20.
   //    So using the prctl() for Linux is more portable.
   #if defined(__linux__)
      #include <sys/prctl.h>
   #endif
   #include <pthread.h>
#endif

#include <cstdio>
#include <cstring>
#include <string>

#include "common.h"
#include "sync.h"

namespace srt {

class ThreadName
{

#if (defined(HAVE_PTHREAD_GETNAME_NP) && defined(HAVE_PTHREAD_GETNAME_NP)) \
   || defined(__linux__)

    class ThreadNameImpl
    {
    public:
        static const size_t BUFSIZE    = 64;
        static const bool   DUMMY_IMPL = false;

        static bool get(char* namebuf)
        {
#if defined(__linux__)
            // since Linux 2.6.11. The buffer should allow space for up to 16
            // bytes; the returned string will be null-terminated.
            return prctl(PR_GET_NAME, (unsigned long)namebuf, 0, 0) != -1;
#elif defined(HAVE_PTHREAD_GETNAME_NP)
            return pthread_getname_np(pthread_self(), namebuf, BUFSIZE) == 0;
#else
#error "unsupported platform"
#endif
        }

        static bool set(const char* name)
        {
            SRT_ASSERT(name != NULL);
#if defined(__linux__)
            // The name can be up to 16 bytes long, including the terminating
            // null byte. (If the length of the string, including the terminating
            // null byte, exceeds 16 bytes, the string is silently truncated.)
            return prctl(PR_SET_NAME, (unsigned long)name, 0, 0) != -1;
#elif defined(HAVE_PTHREAD_SETNAME_NP)
    #if defined(__APPLE__)
            return pthread_setname_np(name) == 0;
    #else
            return pthread_setname_np(pthread_self(), name) == 0;
    #endif
#else
#error "unsupported platform"
#endif
        }

        explicit ThreadNameImpl(const std::string& name)
            : reset(false)
        {
            tid   = pthread_self();

            if (!get(old_name))
                return;

            reset = set(name.c_str());
            if (reset)
                return;

            // Try with a shorter name. 15 is the upper limit supported by Linux,
            // other platforms should support a larger value. So 15 should works
            // on all platforms.
            const size_t max_len = 15;
            if (name.size() > max_len)
                reset = set(name.substr(0, max_len).c_str());
        }

        ~ThreadNameImpl()
        {
            if (!reset)
                return;

            // ensure it's called on the right thread
            if (tid == pthread_self())
                set(old_name);
        }
    
    private:
        ThreadNameImpl(ThreadNameImpl& other);
        ThreadNameImpl& operator=(const ThreadNameImpl& other);

    private:
        bool      reset;
        pthread_t tid;
        char      old_name[BUFSIZE];
    };

#else

    class ThreadNameImpl
    {
    public:
        static const bool   DUMMY_IMPL = true;
        static const size_t BUFSIZE    = 64;

        static bool get(char* output)
        {
            // The default implementation will simply try to get the thread ID
            std::ostringstream bs;
            bs << "T" << sync::this_thread::get_id();
            size_t s  = bs.str().copy(output, BUFSIZE - 1);
            output[s] = '\0';
            return true;
        }

        static bool set(const char*) { return false; }

        ThreadNameImpl(const std::string&) {}

        ~ThreadNameImpl() // just to make it "non-trivially-destructible" for compatibility with normal version
        {
        }
    };

#endif // platform dependent impl

    // Why delegate to impl:
    // 1. to make sure implementation on different platforms have the same interface.
    // 2. it's simple to add some wrappers like get(const std::string &).
    ThreadNameImpl impl;

public:
    static const bool   DUMMY_IMPL = ThreadNameImpl::DUMMY_IMPL;
    static const size_t BUFSIZE    = ThreadNameImpl::BUFSIZE;

    /// @brief Print thread ID to the provided buffer.
    /// The size of the destination buffer is assumed to be at least ThreadName::BUFSIZE.
    /// @param [out] output destination buffer to get thread name
    /// @return true on success, false on failure
    static bool get(char* output) {
        return ThreadNameImpl::get(output);
    }

    static bool get(std::string& name)
    {
        char buf[BUFSIZE];
        bool ret = get(buf);
        if (ret)
            name = buf;
        return ret;
    }

    static bool set(const std::string& name) { return ThreadNameImpl::set(name.c_str()); }

    explicit ThreadName(const std::string& name)
        : impl(name)
    {
    }

private:
    ThreadName(const ThreadName&);
    ThreadName(const char*);
    ThreadName& operator=(const ThreadName& other);
};

} // namespace srt

#endif
