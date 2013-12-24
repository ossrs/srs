/*
The MIT License (MIT)

Copyright (c) 2013 winlin

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

#ifndef SRS_CORE_LOG_HPP
#define SRS_CORE_LOG_HPP

/*
#include <srs_core_log.hpp>
*/

#include <srs_core.hpp>

#include <stdio.h>

#include <errno.h>
#include <string.h>

// the context for multiple clients.
class ILogContext
{
public:
    ILogContext();
    virtual ~ILogContext();
public:
    virtual void generate_id() = 0;
    virtual int get_id() = 0;
public:
    virtual const char* format_time() = 0;
};

// user must implements the LogContext and define a global instance.
extern ILogContext* log_context;

// donot print method
#if 0
    #define srs_verbose(msg, ...) printf("[%s][%d][verbs] ", log_context->format_time(), log_context->get_id());printf(msg, ##__VA_ARGS__);printf("\n")
    #define srs_info(msg, ...)    printf("[%s][%d][infos] ", log_context->format_time(), log_context->get_id());printf(msg, ##__VA_ARGS__);printf("\n")
    #define srs_trace(msg, ...)   printf("[%s][%d][trace] ", log_context->format_time(), log_context->get_id());printf(msg, ##__VA_ARGS__);printf("\n")
    #define srs_warn(msg, ...)    printf("[%s][%d][warns] ", log_context->format_time(), log_context->get_id());printf(msg, ##__VA_ARGS__);printf(" errno=%d(%s)", errno, strerror(errno));printf("\n")
    #define srs_error(msg, ...)   printf("[%s][%d][error] ", log_context->format_time(), log_context->get_id());printf(msg, ##__VA_ARGS__);printf(" errno=%d(%s)", errno, strerror(errno));printf("\n")
// use __FUNCTION__ to print c method
#elif 1
    #define srs_verbose(msg, ...) printf("[%s][%d][verbs][%s] ", log_context->format_time(), log_context->get_id(), __FUNCTION__);printf(msg, ##__VA_ARGS__);printf("\n")
    #define srs_info(msg, ...)    printf("[%s][%d][infos][%s] ", log_context->format_time(), log_context->get_id(), __FUNCTION__);printf(msg, ##__VA_ARGS__);printf("\n")
    #define srs_trace(msg, ...)   printf("[%s][%d][trace][%s] ", log_context->format_time(), log_context->get_id(), __FUNCTION__);printf(msg, ##__VA_ARGS__);printf("\n")
    #define srs_warn(msg, ...)    printf("[%s][%d][warns][%s] ", log_context->format_time(), log_context->get_id(), __FUNCTION__);printf(msg, ##__VA_ARGS__);printf(" errno=%d(%s)", errno, strerror(errno));printf("\n")
    #define srs_error(msg, ...)   printf("[%s][%d][error][%s] ", log_context->format_time(), log_context->get_id(), __FUNCTION__);printf(msg, ##__VA_ARGS__);printf(" errno=%d(%s)", errno, strerror(errno));printf("\n")
// use __PRETTY_FUNCTION__ to print c++ class:method
#else
    #define srs_verbose(msg, ...) printf("[%s][%d][verbs][%s] ", log_context->format_time(), log_context->get_id(), __PRETTY_FUNCTION__);printf(msg, ##__VA_ARGS__);printf("\n")
    #define srs_info(msg, ...)    printf("[%s][%d][infos][%s] ", log_context->format_time(), log_context->get_id(), __PRETTY_FUNCTION__);printf(msg, ##__VA_ARGS__);printf("\n")
    #define srs_trace(msg, ...)   printf("[%s][%d][trace][%s] ", log_context->format_time(), log_context->get_id(), __PRETTY_FUNCTION__);printf(msg, ##__VA_ARGS__);printf("\n")
    #define srs_warn(msg, ...)    printf("[%s][%d][warns][%s] ", log_context->format_time(), log_context->get_id(), __PRETTY_FUNCTION__);printf(msg, ##__VA_ARGS__);printf(" errno=%d(%s)", errno, strerror(errno));printf("\n")
    #define srs_error(msg, ...)   printf("[%s][%d][error][%s] ", log_context->format_time(), log_context->get_id(), __PRETTY_FUNCTION__);printf(msg, ##__VA_ARGS__);printf(" errno=%d(%s)", errno, strerror(errno));printf("\n")
#endif

#if 1
	#undef srs_verbose
	#define srs_verbose(msg, ...) (void)0
#endif
#if 1
	#undef srs_info
	#define srs_info(msg, ...) (void)0
#endif
#if 0
	#undef srs_trace
	#define srs_trace(msg, ...) (void)0
#endif

#endif
