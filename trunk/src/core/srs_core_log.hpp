#ifndef SRS_CORE_LOG_HPP
#define SRS_CORE_LOG_HPP

/*
#include <srs_core_log.hpp>
*/

#include <srs_core.hpp>

#include <stdio.h>

#include <errno.h>
#include <string.h>

class ILogContext
{
public:
    ILogContext();
    virtual ~ILogContext();
public:
    virtual void SetId() = 0;
    virtual int GetId() = 0;
public:
    virtual const char* FormatTime() = 0;
};

// user must implements the LogContext and define a global instance.
extern ILogContext* log_context;

#if 0
    #define SrsVerbose(msg, ...) printf("[%s][%d][verbs] ", log_context->FormatTime(), log_context->GetId());printf(msg, ##__VA_ARGS__);printf("\n")
    #define SrsInfo(msg, ...)    printf("[%s][%d][infos] ", log_context->FormatTime(), log_context->GetId());printf(msg, ##__VA_ARGS__);printf("\n")
    #define SrsTrace(msg, ...)   printf("[%s][%d][trace] ", log_context->FormatTime(), log_context->GetId());printf(msg, ##__VA_ARGS__);printf("\n")
    #define SrsWarn(msg, ...)    printf("[%s][%d][warns] ", log_context->FormatTime(), log_context->GetId());printf(msg, ##__VA_ARGS__);printf(" errno=%d(%s)", errno, strerror(errno));printf("\n")
    #define SrsError(msg, ...)   printf("[%s][%d][error] ", log_context->FormatTime(), log_context->GetId());printf(msg, ##__VA_ARGS__);printf(" errno=%d(%s)", errno, strerror(errno));printf("\n")
#else
    #define SrsVerbose(msg, ...) printf("[%s][%d][verbs][%s] ", log_context->FormatTime(), log_context->GetId(), __FUNCTION__);printf(msg, ##__VA_ARGS__);printf("\n")
    #define SrsInfo(msg, ...)    printf("[%s][%d][infos][%s] ", log_context->FormatTime(), log_context->GetId(), __FUNCTION__);printf(msg, ##__VA_ARGS__);printf("\n")
    #define SrsTrace(msg, ...)   printf("[%s][%d][trace][%s] ", log_context->FormatTime(), log_context->GetId(), __FUNCTION__);printf(msg, ##__VA_ARGS__);printf("\n")
    #define SrsWarn(msg, ...)    printf("[%s][%d][warns][%s] ", log_context->FormatTime(), log_context->GetId(), __FUNCTION__);printf(msg, ##__VA_ARGS__);printf(" errno=%d(%s)", errno, strerror(errno));printf("\n")
    #define SrsError(msg, ...)   printf("[%s][%d][error][%s] ", log_context->FormatTime(), log_context->GetId(), __FUNCTION__);printf(msg, ##__VA_ARGS__);printf(" errno=%d(%s)", errno, strerror(errno));printf("\n")
#endif

#endif
