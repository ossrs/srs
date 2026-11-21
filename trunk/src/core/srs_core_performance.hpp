//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_CORE_PERFORMANCE_HPP
#define SRS_CORE_PERFORMANCE_HPP

// Merged read (MR) improves read performance by sleeping to let data pile up and reading it
// together, which adds latency up to the sleep time.
// @see https://github.com/ossrs/srs/issues/241
// @see https://github.com/ossrs/srs/issues/241#issuecomment-65554690
#define SRS_PERF_MERGED_READ
// Defaults of the vhost publish mr and mr_latency config.
#define SRS_PERF_MR_ENABLED false
#define SRS_PERF_MR_SLEEP (350 * SRS_UTIME_MILLISECONDS)

// @see https://gperftools.github.io/gperftools/tcmalloc.html
#define SRS_PERF_TCMALLOC_RELEASE_RATE 0.8

/**
 * Merged write (MW) caches messages for this time and sends them in one writev. It raised
 * performance from 3.5k+ to 7.5k+ players, and the latency grows with the cache. It is the
 * default of the mw_latency config. The default socket send buffer, 185KB, is large enough.
 * Messages and iovecs per send by mw sleep in ms:
 *       mw_sleep        msgs        iovs
 *       350             43          86
 *       400             44          88
 *       500             46          92
 *       600             46          92
 *       700             82          164
 *       800             81          162
 *       900             80          160
 *       1000            88          176
 *       1100            91          182
 *       1200            89          178
 *       1300            119         238
 *       1400            120         240
 *       1500            119         238
 *       1600            131         262
 *       1700            131         262
 *       1800            133         266
 *       1900            141         282
 *       2000            150         300
 */
#define SRS_PERF_MW_SLEEP (350 * SRS_UTIME_MILLISECONDS)
// A play client sends the messages it got at once; a 1800ms mw sleep gives about 133.
#define SRS_PERF_MW_MSGS 128

#define SRS_PERF_MW_SO_SNDBUF

#undef SRS_PERF_MW_SO_RCVBUF
#define SRS_PERF_QUEUE_FAST_VECTOR
// Cond wait improves performance for many connections.
// TODO: FIXME: Should always enable it.
#define SRS_PERF_QUEUE_COND_WAIT
#ifdef SRS_PERF_QUEUE_COND_WAIT
#define SRS_PERF_MW_MIN_MSGS 8
#define SRS_PERF_MW_MIN_MSGS_FOR_RTC 1
#define SRS_PERF_MW_MIN_MSGS_REALTIME 0
#endif
// Default of the vhost min_latency config.
// @see https://github.com/ossrs/srs/issues/257
#define SRS_PERF_MIN_LATENCY_ENABLED false

// Improves performance about 10% for small chunk sizes and 5% for large ones.
// @see https://github.com/ossrs/srs/issues/249
#define SRS_PERF_CHUNK_STREAM_CACHE 16

#define SRS_PERF_GOP_CACHE true
#define SRS_PERF_PLAY_QUEUE (30 * SRS_UTIME_SECONDS)

// Some networks do not support the complex send.
// @see https://github.com/ossrs/srs/issues/320
// #undef SRS_PERF_COMPLEX_SEND
#define SRS_PERF_COMPLEX_SEND
// Some networks need small TCP packets.
// @see https://github.com/ossrs/srs/issues/320
#undef SRS_PERF_TCP_NODELAY
#define SRS_PERF_TCP_NODELAY
// A fixed socket send buffer forces the server to send smaller TCP packets.
// @see https://github.com/ossrs/srs/issues/320
#ifdef SRS_PERF_MW_SO_SNDBUF
// #define SRS_PERF_SO_SNDBUF_SIZE 1024
#undef SRS_PERF_SO_SNDBUF_SIZE
#endif

#define SRS_PERF_GLIBC_MEMORY_CHECK
#undef SRS_PERF_GLIBC_MEMORY_CHECK

#endif
