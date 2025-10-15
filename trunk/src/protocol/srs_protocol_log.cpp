//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_protocol_log.hpp>

#include <sstream>
#include <stdarg.h>
#include <sys/time.h>
#include <unistd.h>
using namespace std;

#include <srs_kernel_error.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_utility.hpp>

#include <srs_kernel_kbps.hpp>

SrsPps *_srs_pps_cids_get = NULL;
SrsPps *_srs_pps_cids_set = NULL;

#define SRS_BASIC_LOG_SIZE 8192

SrsThreadContext::SrsThreadContext()
{
}

SrsThreadContext::~SrsThreadContext()
{
}

SrsContextId SrsThreadContext::generate_id()
{
    SrsRand rand;
    SrsContextId cid;
    return cid.set_value(rand.gen_str(8));
}

static SrsContextId _srs_context_default;
static int _srs_context_key = -1;
void _srs_context_destructor(void *arg)
{
    SrsContextId *cid = (SrsContextId *)arg;
    srs_freep(cid);
}

extern bool _srs_global_initialized;

const SrsContextId &SrsThreadContext::get_id()
{
    if (!_srs_global_initialized) {
        return _srs_context_default;
    }

    ++_srs_pps_cids_get->sugar_;

    if (!srs_thread_self()) {
        return _srs_context_default;
    }

    void *cid = srs_thread_getspecific(_srs_context_key);
    if (!cid) {
        return _srs_context_default;
    }

    return *(SrsContextId *)cid;
}

const SrsContextId &SrsThreadContext::set_id(const SrsContextId &v)
{
    return srs_context_set_cid_of(srs_thread_self(), v);
}

void SrsThreadContext::clear_cid()
{
}

extern bool _srs_global_initialized;
const SrsContextId &srs_context_set_cid_of(srs_thread_t trd, const SrsContextId &v)
{
    if (!_srs_global_initialized) {
        _srs_context_default = v;
        return v;
    }

    ++_srs_pps_cids_set->sugar_;

    if (!trd) {
        _srs_context_default = v;
        return v;
    }

    SrsContextId *cid = new SrsContextId();
    *cid = v;

    if (_srs_context_key < 0) {
        int r0 = srs_key_create(&_srs_context_key, _srs_context_destructor);
        srs_assert(r0 == 0);
    }

    int r0 = srs_thread_setspecific2(trd, _srs_context_key, cid);
    srs_assert(r0 == 0);

    return v;
}

bool srs_log_header(char *buffer, int size, bool utc, bool dangerous, const char *tag, SrsContextId cid, const char *level, int *psize)
{
    // clock time
    timeval tv;
    if (gettimeofday(&tv, NULL) == -1) {
        return false;
    }

    // to calendar time
    struct tm now;
    // Each of these functions returns NULL in case an error was detected. @see https://linux.die.net/man/3/localtime_r
    if (utc) {
        if (gmtime_r(&tv.tv_sec, &now) == NULL) {
            return false;
        }
    } else {
        if (localtime_r(&tv.tv_sec, &now) == NULL) {
            return false;
        }
    }

    int written = -1;
    if (dangerous) {
        if (tag) {
            written = snprintf(buffer, size,
                               "[%d-%02d-%02d %02d:%02d:%02d.%03d][%s][%d][%s][%d][%s] ",
                               1900 + now.tm_year, 1 + now.tm_mon, now.tm_mday, now.tm_hour, now.tm_min, now.tm_sec, (int)(tv.tv_usec / 1000),
                               level, getpid(), cid.c_str(), errno, tag);
        } else {
            written = snprintf(buffer, size,
                               "[%d-%02d-%02d %02d:%02d:%02d.%03d][%s][%d][%s][%d] ",
                               1900 + now.tm_year, 1 + now.tm_mon, now.tm_mday, now.tm_hour, now.tm_min, now.tm_sec, (int)(tv.tv_usec / 1000),
                               level, getpid(), cid.c_str(), errno);
        }
    } else {
        if (tag) {
            written = snprintf(buffer, size,
                               "[%d-%02d-%02d %02d:%02d:%02d.%03d][%s][%d][%s][%s] ",
                               1900 + now.tm_year, 1 + now.tm_mon, now.tm_mday, now.tm_hour, now.tm_min, now.tm_sec, (int)(tv.tv_usec / 1000),
                               level, getpid(), cid.c_str(), tag);
        } else {
            written = snprintf(buffer, size,
                               "[%d-%02d-%02d %02d:%02d:%02d.%03d][%s][%d][%s] ",
                               1900 + now.tm_year, 1 + now.tm_mon, now.tm_mday, now.tm_hour, now.tm_min, now.tm_sec, (int)(tv.tv_usec / 1000),
                               level, getpid(), cid.c_str());
        }
    }

    // Exceed the size, ignore this log.
    // Check size to avoid security issue https://github.com/ossrs/srs/issues/1229
    if (written <= 0 || written >= size) {
        return false;
    }

    // write the header size.
    *psize = written;

    return true;
}
