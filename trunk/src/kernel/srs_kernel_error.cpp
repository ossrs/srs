//
// Copyright (c) 2013-2023 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#include <srs_kernel_error.hpp>

#include <srs_kernel_log.hpp>
#include <srs_kernel_utility.hpp>

#include <errno.h>
#include <sstream>
#include <stdarg.h>
#include <unistd.h>
#include <assert.h>

#include <map>
#include <vector>
using namespace std;

const int maxLogBuf = 4 * 1024 * 1024;

#if defined(SRS_BACKTRACE) && defined(__linux)
#include <execinfo.h>
#include <dlfcn.h>
#include <string.h>
#include <stdlib.h>

void* parse_symbol_offset(char* frame)
{
    char* p = NULL;
    char* p_symbol = NULL;
    int nn_symbol = 0;
    char* p_offset = NULL;
    int nn_offset = 0;

    // Read symbol and offset, for example:
    //      /tools/backtrace(foo+0x1820) [0x555555555820]
    for (p = frame; *p; p++) {
        if (*p == '(') {
            p_symbol = p + 1;
        } else if (*p == '+') {
            if (p_symbol) nn_symbol = p - p_symbol;
            p_offset = p + 1;
        } else if (*p == ')') {
            if (p_offset) nn_offset = p - p_offset;
        }
    }
    if (!nn_symbol && !nn_offset) {
        return NULL;
    }

    // Convert offset(0x1820) to pointer, such as 0x1820.
    char tmp[128];
    if (!nn_offset || nn_offset >= (int)sizeof(tmp)) {
        return NULL;
    }

    int r0 = EOF;
    void* offset = NULL;
    tmp[nn_offset] = 0;
    if ((r0 = sscanf(strncpy(tmp, p_offset, nn_offset), "%p", &offset)) == EOF) {
        return NULL;
    }

    // Covert symbol(foo) to offset, such as 0x2fba.
    if (!nn_symbol || nn_symbol >= (int)sizeof(tmp)) {
        return offset;
    }

    void* object_file;
    if ((object_file = dlopen(NULL, RTLD_LAZY)) == NULL) {
        return offset;
    }

    void* address;
    tmp[nn_symbol] = 0;
    if ((address = dlsym(object_file, strncpy(tmp, p_symbol, nn_symbol))) == NULL) {
        dlclose(object_file);
        return offset;
    }

    Dl_info symbol_info;
    if ((r0 = dladdr(address, &symbol_info)) == 0) {
        dlclose(object_file);
        return offset;
    }

    dlclose(object_file);
    return (char*)symbol_info.dli_saddr - (char*)symbol_info.dli_fbase + (char*)offset;
}

extern const char* _srs_binary;

char* addr2line_format(void* addr, char* symbol, char* buffer, int nn_buffer)
{
    char cmd[512] = {0};
    int r0 = snprintf(cmd, sizeof(cmd), "addr2line -C -p -s -f -a -e %s %p", _srs_binary, (void*)((char*)addr - 1));
    if (r0 < 0 || r0 >= (int)sizeof(cmd)) return symbol;

    FILE* fp = popen(cmd, "r");
    if (!fp) return symbol;

    char* p = fgets(buffer, nn_buffer, fp);
    pclose(fp);

    if (p == NULL) return symbol;
    if ((r0 = strlen(p)) == 0) return symbol;

    // Trait the last newline if exists.
    if (p[r0 - 1] == '\n') p[r0 - 1] = '\0';

    // Find symbol not match by addr2line, like
    //      0x0000000000021c87: ?? ??:0
    //      0x0000000000002ffa: _start at ??:?
    for (p = buffer; p < buffer + r0 - 1; p++) {
        if (p[0] == '?' && p[1] == '?') return symbol;
    }

    return buffer;
}
#endif

int srs_parse_asan_backtrace_symbols(char* symbol, char* out_buf)
{
#if defined(SRS_BACKTRACE) && defined(__linux)
    void* frame = parse_symbol_offset(symbol);
    if (!frame) {
        return ERROR_BACKTRACE_PARSE_OFFSET;
    }

    char* fmt = addr2line_format(frame, symbol, out_buf, sizeof(out_buf));
    if (fmt != out_buf) {
        return ERROR_BACKTRACE_ADDR2LINE;
    }

    return ERROR_SUCCESS;
#endif
    return ERROR_BACKTRACE_PARSE_NOT_SUPPORT;
}

#ifdef SRS_SANITIZER_LOG
void asan_report_callback(const char* str)
{
    static char buf[256];

    // No error code for assert failed.
    errno = 0;

    std::vector<std::string> asan_logs = srs_string_split(string(str), "\n");
    size_t log_count = asan_logs.size();
    for (size_t i = 0; i < log_count; i++) {
        std::string log = asan_logs[i];

        if (!srs_string_starts_with(srs_string_trim_start(log, " "), "#")) {
            srs_error("%s", log.c_str());
            continue;
        }

        buf[0] = 0;
        int r0 = srs_parse_asan_backtrace_symbols((char*)log.c_str(), buf);
        if (r0 != ERROR_SUCCESS) {
            srs_error("%s, r0=%d", log.c_str(), r0);
        } else {
            srs_error("%s, %s", log.c_str(), buf);
        }
    }
}
#endif

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

        ss << "code=" << code;

        string code_str = srs_error_code_str(this);
        if (!code_str.empty()) ss << "(" << code_str << ")";

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
    static char* buffer = new char[maxLogBuf];
    int r0 = vsnprintf(buffer, maxLogBuf, fmt, ap);
    va_end(ap);
    
    SrsCplxError* err = new SrsCplxError();
    
    err->func = func;
    err->file = file;
    err->line = line;
    err->code = code;
    err->rerrno = rerrno;
    if (r0 > 0 && r0 < maxLogBuf) {
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
    static char* buffer = new char[maxLogBuf];
    int r0 = vsnprintf(buffer, maxLogBuf, fmt, ap);
    va_end(ap);
    
    SrsCplxError* err = new SrsCplxError();
    
    err->func = func;
    err->file = file;
    err->line = line;
    if (v) {
        err->code = v->code;
    }
    err->rerrno = rerrno;
    if (r0 > 0 && r0 < maxLogBuf) {
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

void SrsCplxError::srs_assert(bool expression)
{
#if defined(SRS_BACKTRACE) && defined(__linux)
    if (!expression) {
        void* addresses[64];
        int nn_addresses = backtrace(addresses, sizeof(addresses) / sizeof(void*));
        char** symbols = backtrace_symbols(addresses, nn_addresses);

        // No error code for assert failed.
        errno = 0;

        char buffer[128];
        srs_error("backtrace %d frames of %s %s", nn_addresses, _srs_binary, RTMP_SIG_SRS_SERVER);
        for (int i = 0; i < nn_addresses; i++) {
            void* frame = parse_symbol_offset(symbols[i]);
            char* fmt = addr2line_format(frame, symbols[i], buffer, sizeof(buffer));
            int parsed = (fmt == buffer);
            srs_error("#%d %p %d %s", i, frame, parsed, fmt);
        }

        free(symbols);
    }
#endif

    assert(expression);
}

