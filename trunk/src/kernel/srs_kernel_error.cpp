//
// Copyright (c) 2013-2022 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#include <srs_kernel_error.hpp>

#include <srs_kernel_log.hpp>

#include <errno.h>
#include <sstream>
#include <stdarg.h>
#include <unistd.h>

#include <map>
using namespace std;

bool srs_is_system_control_error(srs_error_t err)
{
    int error_code = srs_error_code(err);
    return error_code == ERROR_CONTROL_RTMP_CLOSE
        || error_code == ERROR_CONTROL_REPUBLISH
        || error_code == ERROR_CONTROL_REDIRECT;
}

bool srs_is_client_gracefully_close(srs_error_t err)
{
    int error_code = srs_error_code(err);
    return error_code == ERROR_SOCKET_READ
        || error_code == ERROR_SOCKET_READ_FULLY
        || error_code == ERROR_SOCKET_WRITE;
}

bool srs_is_server_gracefully_close(srs_error_t err)
{
    int code = srs_error_code(err);
    return code == ERROR_HTTP_STREAM_EOF;
}

SrsCplxError::SrsCplxError()
{
    code = ERROR_SUCCESS;
    wrapped = NULL;
    rerrno = line = 0;
}

SrsCplxError::~SrsCplxError()
{
    srs_freep(wrapped);
}

std::string SrsCplxError::description() {
    if (desc.empty()) {
        stringstream ss;
        ss << "code=" << code;

        string code_str = srs_error_code_str(this);
        if (!code_str.empty()) ss << "(" << code_str << ")";

        string code_longstr = srs_error_code_longstr(this);
        if (!code_longstr.empty()) ss << "(" << code_longstr << ")";

        SrsCplxError* next = this;
        while (next) {
            ss << " : " << next->msg;
            next = next->wrapped;
        }
        ss << endl;

        next = this;
        while (next) {
            ss << "thread [" << getpid() << "][" << next->cid.c_str() << "]: "
            << next->func << "() [" << next->file << ":" << next->line << "]"
            << "[errno=" << next->rerrno << "]";

            next = next->wrapped;

            if (next) {
                ss << endl;
            }
        }

        desc = ss.str();
    }

    return desc;
}

std::string SrsCplxError::summary() {
    if (_summary.empty()) {
        stringstream ss;

        SrsCplxError* next = this;
        while (next) {
            ss << " : " << next->msg;
            next = next->wrapped;
        }

        _summary = ss.str();
    }

    return _summary;
}

SrsCplxError* SrsCplxError::create(const char* func, const char* file, int line, int code, const char* fmt, ...) {
    int rerrno = (int)errno;

    va_list ap;
    va_start(ap, fmt);
    static char buffer[4096];
    int r0 = vsnprintf(buffer, sizeof(buffer), fmt, ap);
    va_end(ap);
    
    SrsCplxError* err = new SrsCplxError();
    
    err->func = func;
    err->file = file;
    err->line = line;
    err->code = code;
    err->rerrno = rerrno;
    if (r0 > 0 && r0 < (int)sizeof(buffer)) {
        err->msg = string(buffer, r0);
    }
    err->wrapped = NULL;
    if (_srs_context) {
        err->cid = _srs_context->get_id();
    }
    
    return err;
}

SrsCplxError* SrsCplxError::wrap(const char* func, const char* file, int line, SrsCplxError* v, const char* fmt, ...) {
    int rerrno = (int)errno;
    
    va_list ap;
    va_start(ap, fmt);
    static char buffer[4096];
    int r0 = vsnprintf(buffer, sizeof(buffer), fmt, ap);
    va_end(ap);
    
    SrsCplxError* err = new SrsCplxError();
    
    err->func = func;
    err->file = file;
    err->line = line;
    if (v) {
        err->code = v->code;
    }
    err->rerrno = rerrno;
    if (r0 > 0 && r0 < (int)sizeof(buffer)) {
        err->msg = string(buffer, r0);
    }
    err->wrapped = v;
    if (_srs_context) {
        err->cid = _srs_context->get_id();
    }
    
    return err;
}

SrsCplxError* SrsCplxError::success() {
    return NULL;
}

SrsCplxError* SrsCplxError::copy(SrsCplxError* from)
{
    if (from == srs_success) {
        return srs_success;
    }
    
    SrsCplxError* err = new SrsCplxError();
    
    err->code = from->code;
    err->wrapped = srs_error_copy(from->wrapped);
    err->msg = from->msg;
    err->func = from->func;
    err->file = from->file;
    err->line = from->line;
    err->cid = from->cid;
    err->rerrno = from->rerrno;
    err->desc = from->desc;
    
    return err;
}

string SrsCplxError::description(SrsCplxError* err)
{
    return err? err->description() : "Success";
}

string SrsCplxError::summary(SrsCplxError* err)
{
    return err? err->summary() : "Success";
}

int SrsCplxError::error_code(SrsCplxError* err)
{
    return err? err->code : ERROR_SUCCESS;
}

#define SRS_STRERRNO_GEN(n, v, m, s) {(SrsErrorCode)v, m, s},
static struct
{
    SrsErrorCode code;
    const char* name;
    const char* descripton;
} _srs_strerror_tab[] = {
#ifndef _WIN32
    {ERROR_SUCCESS, "Success", "Success"},
#endif
    SRS_ERRNO_MAP_SYSTEM(SRS_STRERRNO_GEN)
    SRS_ERRNO_MAP_RTMP(SRS_STRERRNO_GEN)
    SRS_ERRNO_MAP_APP(SRS_STRERRNO_GEN)
    SRS_ERRNO_MAP_HTTP(SRS_STRERRNO_GEN)
    SRS_ERRNO_MAP_RTC(SRS_STRERRNO_GEN)
    SRS_ERRNO_MAP_SRT(SRS_STRERRNO_GEN)
    SRS_ERRNO_MAP_USER(SRS_STRERRNO_GEN)
};
#undef SRS_STRERRNO_GEN

std::string SrsCplxError::error_code_str(SrsCplxError* err)
{
    static string not_found = "";
    static std::map<SrsErrorCode, string> error_map;

    // Build map if empty.
    if (error_map.empty()) {
        for (int i = 0; i < (int)(sizeof(_srs_strerror_tab) / sizeof(_srs_strerror_tab[0])); i++) {
            SrsErrorCode code = _srs_strerror_tab[i].code;
            error_map[code] = _srs_strerror_tab[i].name;
        }
    }

    std::map<SrsErrorCode, string>::iterator it = error_map.find((SrsErrorCode)srs_error_code(err));
    if (it == error_map.end()) {
        return not_found;
    }

    return it->second;
}

std::string SrsCplxError::error_code_longstr(SrsCplxError* err)
{
    static string not_found = "";
    static std::map<SrsErrorCode, string> error_map;

    // Build map if empty.
    if (error_map.empty()) {
        for (int i = 0; i < (int)(sizeof(_srs_strerror_tab) / sizeof(_srs_strerror_tab[0])); i++) {
            SrsErrorCode code = _srs_strerror_tab[i].code;
            error_map[code] = _srs_strerror_tab[i].descripton;
        }
    }

    std::map<SrsErrorCode, string>::iterator it = error_map.find((SrsErrorCode)srs_error_code(err));
    if (it == error_map.end()) {
        return not_found;
    }

    return it->second;
}

