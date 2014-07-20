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

#ifndef SRS_KERNEL_CONSTS_HPP
#define SRS_KERNEL_CONSTS_HPP

/*
#include <srs_kernel_consts.hpp>
*/

#include <srs_core.hpp>

///////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////
// RTMP consts values
///////////////////////////////////////////////////////////
// default vhost of rtmp
#define SRS_CONSTS_RTMP_DEFAULT_VHOST "__defaultVhost__"
// default port of rtmp
#define SRS_CONSTS_RTMP_DEFAULT_PORT "1935"

// the default chunk size for system.
#define SRS_CONSTS_RTMP_SRS_CHUNK_SIZE 60000
// 6. Chunking, RTMP protocol default chunk size.
#define SRS_CONSTS_RTMP_PROTOCOL_CHUNK_SIZE 128
 
// the following is the timeout for rtmp protocol, 
// to avoid death connection.

// the timeout to wait client data,
// if timeout, close the connection.
#define SRS_CONSTS_RTMP_SEND_TIMEOUT_US (int64_t)(30*1000*1000LL)

// the timeout to send data to client,
// if timeout, close the connection.
#define SRS_CONSTS_RTMP_RECV_TIMEOUT_US (int64_t)(30*1000*1000LL)

// the timeout to wait for client control message,
// if timeout, we generally ignore and send the data to client,
// generally, it's the pulse time for data seding.
#define SRS_CONSTS_RTMP_PULSE_TIMEOUT_US (int64_t)(200*1000LL)

/**
* max rtmp header size:
*     1bytes basic header,
*     11bytes message header,
*     4bytes timestamp header,
* that is, 1+11+4=16bytes.
*/
#define SRS_CONSTS_RTMP_MAX_FMT0_HEADER_SIZE 16
/**
* max rtmp header size:
*     1bytes basic header,
*     4bytes timestamp header,
* that is, 1+4=5bytes.
*/
// always use fmt0 as cache.
//#define SRS_CONSTS_RTMP_MAX_FMT3_HEADER_SIZE 5

///////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////
// SRS consts values
///////////////////////////////////////////////////////////
#define SRS_CONSTS_NULL_FILE "/dev/null"
#define SRS_CONSTS_LOCALHOST "127.0.0.1"

///////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////
// log consts values
///////////////////////////////////////////////////////////
// downloading speed-up, play to edge, ingest from origin
#define SRS_CONSTS_LOG_EDGE_PLAY "EIG"
// uploading speed-up, publish to edge, foward to origin
#define SRS_CONSTS_LOG_EDGE_PUBLISH "EFW"
// edge/origin forwarder.
#define SRS_CONSTS_LOG_FOWARDER "FWR"
// play stream on edge/origin.
#define SRS_CONSTS_LOG_PLAY "PLA"
// client publish to edge/origin
#define SRS_CONSTS_LOG_CLIENT_PUBLISH "CPB"
// web/flash publish to edge/origin
#define SRS_CONSTS_LOG_WEB_PUBLISH "WPB"
// ingester for edge(play)/origin
#define SRS_CONSTS_LOG_INGESTER "IGS"
// hls log id.
#define SRS_CONSTS_LOG_HLS "HLS"
// encoder log id.
#define SRS_CONSTS_LOG_ENCODER "ENC"

///////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////
// pithy-print consts values
///////////////////////////////////////////////////////////
// the pithy stage for all play clients.
#define SRS_CONSTS_STAGE_PLAY_USER 1
// the pithy stage for all publish clients.
#define SRS_CONSTS_STAGE_PUBLISH_USER 2
// the pithy stage for all forward clients.
#define SRS_CONSTS_STAGE_FORWARDER 3
// the pithy stage for all encoders.
#define SRS_CONSTS_STAGE_ENCODER 4
// the pithy stage for all hls.
#define SRS_CONSTS_STAGE_HLS 5
// the pithy stage for all ingesters.
#define SRS_CONSTS_STAGE_INGESTER 6
// the pithy stage for all edge.
#define SRS_CONSTS_STAGE_EDGE 7

#endif
