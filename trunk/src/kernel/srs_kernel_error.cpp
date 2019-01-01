/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2019 Winlin
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <srs_kernel_error.hpp>

#include <srs_kernel_log.hpp>

#include <errno.h>
#include <sstream>
#include <stdarg.h>
using namespace std;

bool srs_is_system_control_error(int error_code)
{
    return error_code == ERROR_CONTROL_RTMP_CLOSE
        || error_code == ERROR_CONTROL_REPUBLISH
        || error_code == ERROR_CONTROL_REDIRECT;
}

bool srs_is_system_control_error(srs_error_t err)
{
    int error_code = srs_error_code(err);
    return srs_is_system_control_error(error_code);
}

bool srs_is_client_gracefully_close(int error_code)
{
    return error_code == ERROR_SOCKET_READ
        || error_code == ERROR_SOCKET_READ_FULLY
        || error_code == ERROR_SOCKET_WRITE;
}

bool srs_is_client_gracefully_close(srs_error_t err)
{
    int error_code = srs_error_code(err);
    return srs_is_client_gracefully_close(error_code);
}

SrsCplxError::SrsCplxError()
{
    code = ERROR_SUCCESS;
    wrapped = NULL;
    cid = rerrno = line = 0;
}

SrsCplxError::~SrsCplxError()
{
}

std::string SrsCplxError::description() {
    if (desc.empty()) {
        stringstream ss;
        ss << "code=" << code;
        
        SrsCplxError* next = this;
        while (next) {
            ss << " : " << next->msg;
            next = next->wrapped;
        }
        ss << endl;
        
        next = this;
        while (next) {
            ss << "thread #" << next->cid << ": "
            << next->func << "() [" << next->file << ":" << next->line << "]"
            << "[errno=" << next->rerrno << "]"
            << endl;
            next = next->wrapped;
        }
        
        desc = ss.str();
    }
    
    return desc;
}

SrsCplxError* SrsCplxError::create(const char* func, const char* file, int line, int code, const char* fmt, ...) {
    int rerrno = (int)errno;

    va_list ap;
    va_start(ap, fmt);
    static char buffer[4096];
    vsnprintf(buffer, sizeof(buffer), fmt, ap);
    va_end(ap);
    
    SrsCplxError* err = new SrsCplxError();
    
    err->func = func;
    err->file = file;
    err->line = line;
    err->code = code;
    err->rerrno = rerrno;
    err->msg = buffer;
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
    vsnprintf(buffer, sizeof(buffer), fmt, ap);
    va_end(ap);
    
    SrsCplxError* err = new SrsCplxError();
    
    err->func = func;
    err->file = file;
    err->line = line;
    if (v) {
        err->code = v->code;
    }
    err->rerrno = rerrno;
    err->msg = buffer;
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

int SrsCplxError::error_code(SrsCplxError* err)
{
    return err? err->code : ERROR_SUCCESS;
}

