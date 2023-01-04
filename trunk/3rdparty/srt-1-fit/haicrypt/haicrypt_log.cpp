/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2018 Haivision Systems Inc.
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 * 
 */

#if ENABLE_HAICRYPT_LOGGING

#include "haicrypt_log.h"

#include "hcrypt.h"
#include "haicrypt.h"
#include "../srtcore/srt.h"
#include "../srtcore/logging.h"

extern srt_logging::LogConfig srt_logger_config;

// LOGFA symbol defined in srt.h
srt_logging::Logger hclog(SRT_LOGFA_HAICRYPT, srt_logger_config, "SRT.hc");

extern "C" {

int HaiCrypt_SetLogLevel(int level, int logfa)
{
    srt_setloglevel(level);
    if (logfa != SRT_LOGFA_GENERAL) // General can't be turned on or off
    {
        srt_addlogfa(logfa);
    }
    return 0;
}

// HaiCrypt will be using its own FA, which will be turned off by default.

// Templates made C way.
// It's tempting to use the HAICRYPT_DEFINE_LOG_DISPATCHER macro here because it would provide the
// exact signature that is needed here, the problem is though that this would expand the LOGLEVEL
// parameter, which is also a macro, into the value that the macro designates, which would generate
// the HaiCrypt_LogF_0 instead of HaiCrypt_LogF_LOG_DEBUG, for example.
#define HAICRYPT_DEFINE_LOG_DISPATCHER(LOGLEVEL, dispatcher) \
    int HaiCrypt_LogF_##LOGLEVEL ( const char* file, int line, const char* function, const char* format, ...) \
{ \
    va_list ap; \
    va_start(ap, format); \
    srt_logging::LogDispatcher& lg = hclog.dispatcher; \
    if (!lg.CheckEnabled()) return -1; \
    lg().setloc(file, line, function).vform(format, ap); \
    va_end(ap); \
    return 0; \
}


HAICRYPT_DEFINE_LOG_DISPATCHER(LOG_DEBUG, Debug);
HAICRYPT_DEFINE_LOG_DISPATCHER(LOG_NOTICE, Note);
HAICRYPT_DEFINE_LOG_DISPATCHER(LOG_INFO, Note);
HAICRYPT_DEFINE_LOG_DISPATCHER(LOG_WARNING, Warn);
HAICRYPT_DEFINE_LOG_DISPATCHER(LOG_ERR, Error);
HAICRYPT_DEFINE_LOG_DISPATCHER(LOG_CRIT, Fatal);
HAICRYPT_DEFINE_LOG_DISPATCHER(LOG_ALERT, Fatal);
HAICRYPT_DEFINE_LOG_DISPATCHER(LOG_EMERG, Fatal);


static void DumpCfgFlags(int flags, std::ostream& out)
{
    static struct { int flg; const char* desc; } flgtable [] = {
#define HCRYPTF(name) { HAICRYPT_CFG_F_##name, #name }
        HCRYPTF(TX),
        HCRYPTF(CRYPTO),
        HCRYPTF(FEC)
#undef HCRYPTF
    };
    size_t flgtable_size = sizeof(flgtable)/sizeof(flgtable[0]);
    size_t i;

    out << "{";
    const char* sep = "";
    const char* sep_bar = " | ";
    for (i = 0; i < flgtable_size; ++i)
    {
        if ( (flgtable[i].flg & flags) != 0 )
        {
            out << sep << flgtable[i].desc;
            sep = sep_bar;
        }
    }
    out << "}";
}

void HaiCrypt_DumpConfig(const HaiCrypt_Cfg* cfg)
{
    std::ostringstream cfg_flags;

    DumpCfgFlags(cfg->flags, cfg_flags);

    LOGC(hclog.Debug, log << "CFG DUMP: flags=" << cfg_flags.str()
            << " xport=" << (cfg->xport == HAICRYPT_XPT_SRT ? "SRT" : "INVALID")
            << " cipher="
            << CRYSPR_IMPL_DESC
            << " key_len=" << cfg->key_len << " data_max_len=" << cfg->data_max_len);


    LOGC(hclog.Debug, log << "CFG DUMP: txperiod="
            << cfg->km_tx_period_ms << "ms kmrefresh=" << cfg->km_refresh_rate_pkt
            << " kmpreannounce=" << cfg->km_pre_announce_pkt
            << " secret "
            << "{tp=" << (cfg->secret.typ == 1 ? "PSK" : cfg->secret.typ == 2 ? "PWD" : "???")
            << " len=" << cfg->secret.len << " pwd=" << cfg->secret.str << "}");

}

} // extern "C"

#endif // Block for the whole file
