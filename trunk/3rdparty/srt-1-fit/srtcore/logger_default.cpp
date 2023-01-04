/*
  WARNING: Generated from ../scripts/generate-logging-defs.tcl

  DO NOT MODIFY.

  Copyright applies as per the generator script.
 */


#include "srt.h"
#include "logging.h"
#include "logger_defs.h"

namespace srt_logging
{
    AllFaOn::AllFaOn()
    {
        allfa.set(SRT_LOGFA_GENERAL, true);
        allfa.set(SRT_LOGFA_SOCKMGMT, true);
        allfa.set(SRT_LOGFA_CONN, true);
        allfa.set(SRT_LOGFA_XTIMER, true);
        allfa.set(SRT_LOGFA_TSBPD, true);
        allfa.set(SRT_LOGFA_RSRC, true);

        allfa.set(SRT_LOGFA_CONGEST, true);
        allfa.set(SRT_LOGFA_PFILTER, true);

        allfa.set(SRT_LOGFA_API_CTRL, true);

        allfa.set(SRT_LOGFA_QUE_CTRL, true);

        allfa.set(SRT_LOGFA_EPOLL_UPD, true);

        allfa.set(SRT_LOGFA_API_RECV, true);
        allfa.set(SRT_LOGFA_BUF_RECV, true);
        allfa.set(SRT_LOGFA_QUE_RECV, true);
        allfa.set(SRT_LOGFA_CHN_RECV, true);
        allfa.set(SRT_LOGFA_GRP_RECV, true);

        allfa.set(SRT_LOGFA_API_SEND, true);
        allfa.set(SRT_LOGFA_BUF_SEND, true);
        allfa.set(SRT_LOGFA_QUE_SEND, true);
        allfa.set(SRT_LOGFA_CHN_SEND, true);
        allfa.set(SRT_LOGFA_GRP_SEND, true);

        allfa.set(SRT_LOGFA_INTERNAL, true);

        allfa.set(SRT_LOGFA_QUE_MGMT, true);
        allfa.set(SRT_LOGFA_CHN_MGMT, true);
        allfa.set(SRT_LOGFA_GRP_MGMT, true);
        allfa.set(SRT_LOGFA_EPOLL_API, true);
    }
} // namespace srt_logging
