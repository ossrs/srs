/*
The MIT License (MIT)

Copyright (c) 2013-2014 winlin

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

#ifndef SRS_CORE_PERFORMANCE_HPP
#define SRS_CORE_PERFORMANCE_HPP

/*
#include <srs_core_performance.hpp>
*/

#include <srs_core.hpp>

/**
* this file defines the perfromance options.
*/

/**
* to improve read performance, merge some packets then read,
* when it on and read small bytes, we sleep to wait more data.,
* that is, we merge some data to read together.
* @see https://github.com/winlinvip/simple-rtmp-server/issues/241
* @remark other macros:
*       SOCKET_MAX_BUF, the max size of user-space buffer.
*       SOCKET_READ_SIZE, the user space buffer for socket.
*       SRS_MR_MAX_BITRATE_KBPS, the kbps of stream for system to guess the sleep time.
*       SRS_MR_AVERAGE_BITRATE_KBPS, the average kbps of system.
*       SRS_MR_MIN_BITRATE_KBPS, the min kbps of system.
*       SRS_MR_MAX_SLEEP_MS, the max sleep time, the latency+ when sleep+.
*       SRS_MR_SMALL_BYTES, sleep when got small bytes, the latency+ when small+.
*       SRS_MR_SMALL_PERCENT, to calc the small bytes = SRS_MR_SOCKET_BUFFER/percent.
*       SRS_MR_SOCKET_BUFFER, the socket buffer to set fd.
* @remark the actual socket buffer used to set the buffer user-space size.
*       buffer = min(SOCKET_MAX_BUF, SRS_MR_SOCKET_BUFFER, SOCKET_READ_SIZE)
*       small bytes = max(buffer/SRS_MR_SMALL_PERCENT, SRS_MR_SMALL_BYTES)
*       sleep = calc the sleep by kbps and buffer.
* @remark the main merged-read algorithm:
*       while true:
*           nread = read from socket.
*           sleep if nread < small bytes
*           process bytes.
* @example, for the default settings, this algorithm will use:
*       socket buffer set to 64KB,
*       user space buffer set to 64KB,
*       buffer=65536B, small=4096B, sleep=780ms
*       that is, when got nread bytes smaller than 4KB, sleep(780ms).
*/
#undef SRS_PERF_MERGED_READ
#define SRS_PERF_MERGED_READ

/**
* the send cache time in ms.
* to improve send performance, cache msgs and send in a time.
* for example, cache 500ms videos and audios, then convert all these
* msgs to iovecs, finally use writev to send.
* @remark this largely improve performance, from 3.5k+ to 7.5k+.
*       the latency+ when cache+.
* @remark the socket send buffer default to 185KB, it large enough.
* @see https://github.com/winlinvip/simple-rtmp-server/issues/194
*/
#define SRS_PERF_SEND_MSGS_CACHE 500

#endif

