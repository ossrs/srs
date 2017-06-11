/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2017 OSSRS(winlin)
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

bool srs_is_client_gracefully_close(int error_code)
{
    return error_code == ERROR_SOCKET_READ
        || error_code == ERROR_SOCKET_READ_FULLY
        || error_code == ERROR_SOCKET_WRITE
        || error_code == ERROR_SOCKET_TIMEOUT;
}

SrsError::SrsError()
{
    code = ERROR_SUCCESS;
    wrapped = NULL;
    cid = rerrno = line = 0;
}

SrsError::~SrsError()
{
}

std::string SrsError::description() {
    if (desc.empty()) {
        stringstream ss;
        ss << "code=" << code;
        
        SrsError* next = this;
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

SrsError* SrsError::create(const char* func, const char* file, int line, int code, const char* fmt, ...) {
    int rerrno = (int)errno;

    va_list ap;
    va_start(ap, fmt);
    static char buffer[4096];
    vsnprintf(buffer, sizeof(buffer), fmt, ap);
    va_end(ap);
    
    SrsError* err = new SrsError();
    
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

SrsError* SrsError::wrap(const char* func, const char* file, int line, SrsError* v, const char* fmt, ...) {
    int rerrno = (int)errno;
    
    va_list ap;
    va_start(ap, fmt);
    static char buffer[4096];
    vsnprintf(buffer, sizeof(buffer), fmt, ap);
    va_end(ap);
    
    SrsError* err = new SrsError();
    
    err->func = func;
    err->file = file;
    err->line = line;
    err->code = v->code;
    err->rerrno = rerrno;
    err->msg = buffer;
    err->wrapped = v;
    if (_srs_context) {
        err->cid = _srs_context->get_id();
    }
    
    return err;
}

SrsError* SrsError::success() {
    return NULL;
}

string SrsError::description(SrsError* err)
{
    return err? err->description() : "Success";
}

int SrsError::error_code(SrsError* err)
{
    return err? err->code : ERROR_SUCCESS;
}

