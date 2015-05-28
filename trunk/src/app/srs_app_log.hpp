/*
The MIT License (MIT)

Copyright (c) 2013-2015 SRS(simple-rtmp-server)

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#ifndef SRS_APP_LOG_HPP
#define SRS_APP_LOG_HPP

/*
#include <srs_app_log.hpp>
*/

#include <srs_core.hpp>

#include <srs_app_st.hpp>
#include <srs_kernel_log.hpp>
#include <srs_app_reload.hpp>

#include <string.h>

#include <string>
#include <map>

/**
* st thread context, get_id will get the st-thread id, 
* which identify the client.
*/
class SrsThreadContext : public ISrsThreadContext
{
private:
    std::map<st_thread_t, int> cache;
public:
    SrsThreadContext();
    virtual ~SrsThreadContext();
public:
    virtual int generate_id();
    virtual int get_id();
};

/**
* we use memory/disk cache and donot flush when write log.
* it's ok to use it without config, which will log to console, and default trace level.
* when you want to use different level, override this classs, set the protected _level.
*/
class SrsFastLog : public ISrsLog, public ISrsReloadHandler
{
// for utest to override
protected:
    // defined in SrsLogLevel.
    int _level;
private:
    char* log_data;
    // log to file if specified srs_log_file
    int fd;
    // whether log to file tank
    bool log_to_file_tank;
public:
    SrsFastLog();
    virtual ~SrsFastLog();
public:
    virtual int initialize();
    virtual void verbose(const char* tag, int context_id, const char* fmt, ...);
    virtual void info(const char* tag, int context_id, const char* fmt, ...);
    virtual void trace(const char* tag, int context_id, const char* fmt, ...);
    virtual void warn(const char* tag, int context_id, const char* fmt, ...);
    virtual void error(const char* tag, int context_id, const char* fmt, ...);
// interface ISrsReloadHandler.
public:
    virtual int on_reload_log_tank();
    virtual int on_reload_log_level();
    virtual int on_reload_log_file();
private:
    virtual bool generate_header(bool error, const char* tag, int context_id, const char* level_name, int* header_size);
    virtual void write_log(int& fd, char* str_log, int size, int level);
    virtual void open_log_file();
};

#endif

