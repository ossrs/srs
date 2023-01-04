/*
  WARNING: Generated from ../scripts/generate-logging-defs.tcl

  DO NOT MODIFY.

  Copyright applies as per the generator script.
 */


#ifndef INC_SRT_LOGGER_DEFS_H
#define INC_SRT_LOGGER_DEFS_H

#include "srt.h"
#include "logging.h"

namespace srt_logging
{
    struct AllFaOn
    {
        LogConfig::fa_bitset_t allfa;
        AllFaOn();
    };

    extern Logger gglog;
    extern Logger smlog;
    extern Logger cnlog;
    extern Logger xtlog;
    extern Logger tslog;
    extern Logger rslog;

    extern Logger cclog;
    extern Logger pflog;

    extern Logger aclog;

    extern Logger qclog;

    extern Logger eilog;

    extern Logger arlog;
    extern Logger brlog;
    extern Logger qrlog;
    extern Logger krlog;
    extern Logger grlog;

    extern Logger aslog;
    extern Logger bslog;
    extern Logger qslog;
    extern Logger kslog;
    extern Logger gslog;

    extern Logger inlog;

    extern Logger qmlog;
    extern Logger kmlog;
    extern Logger gmlog;
    extern Logger ealog;

} // namespace srt_logging

#endif
