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

#ifndef SRS_KERNEL_ERROR_HPP
#define SRS_KERNEL_ERROR_HPP

/*
#include <srs_kernel_error.hpp>
*/

#include <srs_core.hpp>

#define ERROR_SUCCESS                       0

#define ERROR_ST_SET_EPOLL                  100
#define ERROR_ST_INITIALIZE                 101
#define ERROR_ST_OPEN_SOCKET                102
#define ERROR_ST_CREATE_LISTEN_THREAD       103
#define ERROR_ST_CREATE_CYCLE_THREAD        104
#define ERROR_ST_CONNECT                    105

#define ERROR_SOCKET_CREATE                 200
#define ERROR_SOCKET_SETREUSE               201
#define ERROR_SOCKET_BIND                   202
#define ERROR_SOCKET_LISTEN                 203
#define ERROR_SOCKET_CLOSED                 204
#define ERROR_SOCKET_GET_PEER_NAME          205
#define ERROR_SOCKET_GET_PEER_IP            206
#define ERROR_SOCKET_READ                   207
#define ERROR_SOCKET_READ_FULLY             208
#define ERROR_SOCKET_WRITE                  209
#define ERROR_SOCKET_WAIT                   210
#define ERROR_SOCKET_TIMEOUT                211
#define ERROR_SOCKET_CONNECT                212
//#define ERROR_SOCKET_GET_LOCAL_IP           212

#define ERROR_RTMP_PLAIN_REQUIRED           300
#define ERROR_RTMP_CHUNK_START              301
#define ERROR_RTMP_MSG_INVLIAD_SIZE         302
#define ERROR_RTMP_AMF0_DECODE              303
#define ERROR_RTMP_AMF0_INVALID             304
#define ERROR_RTMP_REQ_CONNECT              305
#define ERROR_RTMP_REQ_TCURL                306
#define ERROR_RTMP_MESSAGE_DECODE           307
#define ERROR_RTMP_MESSAGE_ENCODE           308
#define ERROR_RTMP_AMF0_ENCODE              309
#define ERROR_RTMP_CHUNK_SIZE               310
#define ERROR_RTMP_TRY_SIMPLE_HS            311
#define ERROR_RTMP_CH_SCHEMA                312
#define ERROR_RTMP_PACKET_SIZE              313
#define ERROR_RTMP_VHOST_NOT_FOUND          314
#define ERROR_RTMP_ACCESS_DENIED            315
#define ERROR_RTMP_HANDSHAKE                316
#define ERROR_RTMP_NO_REQUEST               317
// if user use complex handshake, but without ssl,
// 1. srs is ok, ignore and turn to simple handshake.
// 2. srs-librtmp return error, to terminate the program.
#define ERROR_RTMP_HS_SSL_REQUIRE           318
#define ERROR_RTMP_DURATION_EXCEED          319
// edge specified errors
// invalid state for client to play edge stream.
#define ERROR_RTMP_EDGE_PLAY_STATE          320
// invalid state for client to publish edge stream.
#define ERROR_RTMP_EDGE_PUBLISH_STATE       321
#define ERROR_RTMP_EDGE_PROXY_PULL          322
#define ERROR_RTMP_EDGE_RELOAD              323
// aggregate message parse failed.
#define ERROR_RTMP_AGGREGATE                324

#define ERROR_SYSTEM_STREAM_INIT            400
#define ERROR_SYSTEM_PACKET_INVALID         401
#define ERROR_SYSTEM_CLIENT_INVALID         402
#define ERROR_SYSTEM_ASSERT_FAILED          403
#define ERROR_SYSTEM_SIZE_NEGATIVE          404
#define ERROR_SYSTEM_CONFIG_INVALID         405
#define ERROR_SYSTEM_CONFIG_DIRECTIVE       406
#define ERROR_SYSTEM_CONFIG_BLOCK_START     407
#define ERROR_SYSTEM_CONFIG_BLOCK_END       408
#define ERROR_SYSTEM_CONFIG_EOF             409
#define ERROR_SYSTEM_STREAM_BUSY            410
#define ERROR_SYSTEM_IP_INVALID             411
#define ERROR_SYSTEM_FORWARD_LOOP           412
#define ERROR_SYSTEM_WAITPID                413
#define ERROR_SYSTEM_BANDWIDTH_KEY          414
#define ERROR_SYSTEM_BANDWIDTH_DENIED       415
#define ERROR_SYSTEM_PID_ACQUIRE            416
#define ERROR_SYSTEM_PID_ALREADY_RUNNING    417
#define ERROR_SYSTEM_PID_LOCK               418
#define ERROR_SYSTEM_PID_TRUNCATE_FILE      419
#define ERROR_SYSTEM_PID_WRITE_FILE         420
#define ERROR_SYSTEM_PID_GET_FILE_INFO      421
#define ERROR_SYSTEM_PID_SET_FILE_INFO      422
#define ERROR_SYSTEM_FILE_ALREADY_OPENED    423
#define ERROR_SYSTEM_FILE_OPENE             424
#define ERROR_SYSTEM_FILE_CLOSE             425
#define ERROR_SYSTEM_FILE_READ              426
#define ERROR_SYSTEM_FILE_WRITE             427
#define ERROR_SYSTEM_FILE_EOF               428
#define ERROR_SYSTEM_FILE_RENAME            429
#define ERROR_SYSTEM_CREATE_PIPE            430
#define ERROR_SYSTEM_FILE_SEEK              431
#define ERROR_SYSTEM_IO_INVALID             432

// see librtmp.
// failed when open ssl create the dh
#define ERROR_OpenSslCreateDH               500
// failed when open ssl create the Private key.
#define ERROR_OpenSslCreateP                501
// when open ssl create G.
#define ERROR_OpenSslCreateG                502
// when open ssl parse P1024
#define ERROR_OpenSslParseP1024             503
// when open ssl set G
#define ERROR_OpenSslSetG                   504
// when open ssl generate DHKeys
#define ERROR_OpenSslGenerateDHKeys         505
// when open ssl share key already computed.
#define ERROR_OpenSslShareKeyComputed       506
// when open ssl get shared key size.
#define ERROR_OpenSslGetSharedKeySize       507
// when open ssl get peer public key.
#define ERROR_OpenSslGetPeerPublicKey       508
// when open ssl compute shared key.
#define ERROR_OpenSslComputeSharedKey       509
// when open ssl is invalid DH state.
#define ERROR_OpenSslInvalidDHState         510
// when open ssl copy key
#define ERROR_OpenSslCopyKey                511
// when open ssl sha256 digest key invalid size.
#define ERROR_OpenSslSha256DigestSize       512

#define ERROR_HLS_METADATA                  600
#define ERROR_HLS_DECODE_ERROR              601
#define ERROR_HLS_CREATE_DIR                602
#define ERROR_HLS_OPEN_FAILED               603
#define ERROR_HLS_WRITE_FAILED              604
#define ERROR_HLS_AAC_FRAME_LENGTH          605
#define ERROR_HLS_AVC_SAMPLE_SIZE           606

#define ERROR_ENCODER_VCODEC                700
#define ERROR_ENCODER_OUTPUT                701
#define ERROR_ENCODER_ACHANNELS             702
#define ERROR_ENCODER_ASAMPLE_RATE          703
#define ERROR_ENCODER_ABITRATE              704
#define ERROR_ENCODER_ACODEC                705
#define ERROR_ENCODER_VPRESET               706
#define ERROR_ENCODER_VPROFILE              707
#define ERROR_ENCODER_VTHREADS              708
#define ERROR_ENCODER_VHEIGHT               709
#define ERROR_ENCODER_VWIDTH                710
#define ERROR_ENCODER_VFPS                  711
#define ERROR_ENCODER_VBITRATE              712
#define ERROR_ENCODER_FORK                  713
#define ERROR_ENCODER_LOOP                  714
#define ERROR_ENCODER_OPEN                  715
#define ERROR_ENCODER_DUP2                  716
#define ERROR_ENCODER_PARSE                 717
#define ERROR_ENCODER_NO_INPUT              718
#define ERROR_ENCODER_NO_OUTPUT             719
#define ERROR_ENCODER_INPUT_TYPE            720

#define ERROR_HTTP_PARSE_URI                800
#define ERROR_HTTP_DATA_INVLIAD             801
#define ERROR_HTTP_PARSE_HEADER             802
#define ERROR_HTTP_HANDLER_MATCH_URL        803
#define ERROR_HTTP_HANDLER_INVALID          804
#define ERROR_HTTP_API_LOGS                 805
#define ERROR_HTTP_FLV_SEQUENCE_HEADER      806
#define ERROR_HTTP_FLV_OFFSET_OVERFLOW      807

#define ERROR_KERNEL_FLV_HEADER             900
#define ERROR_KERNEL_FLV_STREAM_CLOSED      901

// system control message, 
// not an error, but special control logic.
// sys ctl: rtmp close stream, support replay.
#define ERROR_CONTROL_RTMP_CLOSE            2000
// FMLE stop publish and republish.
#define ERROR_CONTROL_REPUBLISH             2001

/**
* whether the error code is an system control error.
*/
extern bool srs_is_system_control_error(int error_code);
extern bool srs_is_client_gracefully_close(int error_code);

#endif
