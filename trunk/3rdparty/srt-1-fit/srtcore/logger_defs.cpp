/*
  WARNING: Generated from ../scripts/generate-logging-defs.tcl

  DO NOT MODIFY.

  Copyright applies as per the generator script.
 */


#include "srt.h"
#include "logging.h"
#include "logger_defs.h"

namespace srt_logging { AllFaOn logger_fa_all; }
// We need it outside the namespace to preserve the global name.
// It's a part of "hidden API" (used by applications)
SRT_API srt_logging::LogConfig srt_logger_config(srt_logging::logger_fa_all.allfa);

namespace srt_logging
{
    Logger gglog(SRT_LOGFA_GENERAL, srt_logger_config, "SRT.gg");
    Logger smlog(SRT_LOGFA_SOCKMGMT, srt_logger_config, "SRT.sm");
    Logger cnlog(SRT_LOGFA_CONN, srt_logger_config, "SRT.cn");
    Logger xtlog(SRT_LOGFA_XTIMER, srt_logger_config, "SRT.xt");
    Logger tslog(SRT_LOGFA_TSBPD, srt_logger_config, "SRT.ts");
    Logger rslog(SRT_LOGFA_RSRC, srt_logger_config, "SRT.rs");

    Logger cclog(SRT_LOGFA_CONGEST, srt_logger_config, "SRT.cc");
    Logger pflog(SRT_LOGFA_PFILTER, srt_logger_config, "SRT.pf");

    Logger aclog(SRT_LOGFA_API_CTRL, srt_logger_config, "SRT.ac");

    Logger qclog(SRT_LOGFA_QUE_CTRL, srt_logger_config, "SRT.qc");

    Logger eilog(SRT_LOGFA_EPOLL_UPD, srt_logger_config, "SRT.ei");

    Logger arlog(SRT_LOGFA_API_RECV, srt_logger_config, "SRT.ar");
    Logger brlog(SRT_LOGFA_BUF_RECV, srt_logger_config, "SRT.br");
    Logger qrlog(SRT_LOGFA_QUE_RECV, srt_logger_config, "SRT.qr");
    Logger krlog(SRT_LOGFA_CHN_RECV, srt_logger_config, "SRT.kr");
    Logger grlog(SRT_LOGFA_GRP_RECV, srt_logger_config, "SRT.gr");

    Logger aslog(SRT_LOGFA_API_SEND, srt_logger_config, "SRT.as");
    Logger bslog(SRT_LOGFA_BUF_SEND, srt_logger_config, "SRT.bs");
    Logger qslog(SRT_LOGFA_QUE_SEND, srt_logger_config, "SRT.qs");
    Logger kslog(SRT_LOGFA_CHN_SEND, srt_logger_config, "SRT.ks");
    Logger gslog(SRT_LOGFA_GRP_SEND, srt_logger_config, "SRT.gs");

    Logger inlog(SRT_LOGFA_INTERNAL, srt_logger_config, "SRT.in");

    Logger qmlog(SRT_LOGFA_QUE_MGMT, srt_logger_config, "SRT.qm");
    Logger kmlog(SRT_LOGFA_CHN_MGMT, srt_logger_config, "SRT.km");
    Logger gmlog(SRT_LOGFA_GRP_MGMT, srt_logger_config, "SRT.gm");
    Logger ealog(SRT_LOGFA_EPOLL_API, srt_logger_config, "SRT.ea");
} // namespace srt_logging
