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

// success, ok
#define ERROR_SUCCESS                       0

///////////////////////////////////////////////////////
// system error.
///////////////////////////////////////////////////////
#define ERROR_SOCKET_CREATE                 1000
#define ERROR_SOCKET_SETREUSE               1001
#define ERROR_SOCKET_BIND                   1002
#define ERROR_SOCKET_LISTEN                 1003
#define ERROR_SOCKET_CLOSED                 1004
#define ERROR_SOCKET_GET_PEER_NAME          1005
#define ERROR_SOCKET_GET_PEER_IP            1006
#define ERROR_SOCKET_READ                   1007
#define ERROR_SOCKET_READ_FULLY             1008
#define ERROR_SOCKET_WRITE                  1009
#define ERROR_SOCKET_WAIT                   1010
#define ERROR_SOCKET_TIMEOUT                1011
#define ERROR_SOCKET_CONNECT                1012
#define ERROR_ST_SET_EPOLL                  1013
#define ERROR_ST_INITIALIZE                 1014
#define ERROR_ST_OPEN_SOCKET                1015
#define ERROR_ST_CREATE_LISTEN_THREAD       1016
#define ERROR_ST_CREATE_CYCLE_THREAD        1017
#define ERROR_ST_CONNECT                    1018
#define ERROR_SYSTEM_PACKET_INVALID         1019
#define ERROR_SYSTEM_CLIENT_INVALID         1020
#define ERROR_SYSTEM_ASSERT_FAILED          1021
#define ERROR_SYSTEM_SIZE_NEGATIVE          1022
#define ERROR_SYSTEM_CONFIG_INVALID         1023
#define ERROR_SYSTEM_CONFIG_DIRECTIVE       1024
#define ERROR_SYSTEM_CONFIG_BLOCK_START     1025
#define ERROR_SYSTEM_CONFIG_BLOCK_END       1026
#define ERROR_SYSTEM_CONFIG_EOF             1027
#define ERROR_SYSTEM_STREAM_BUSY            1028
#define ERROR_SYSTEM_IP_INVALID             1029
#define ERROR_SYSTEM_FORWARD_LOOP           1030
#define ERROR_SYSTEM_WAITPID                1031
#define ERROR_SYSTEM_BANDWIDTH_KEY          1032
#define ERROR_SYSTEM_BANDWIDTH_DENIED       1033
#define ERROR_SYSTEM_PID_ACQUIRE            1034
#define ERROR_SYSTEM_PID_ALREADY_RUNNING    1035
#define ERROR_SYSTEM_PID_LOCK               1036
#define ERROR_SYSTEM_PID_TRUNCATE_FILE      1037
#define ERROR_SYSTEM_PID_WRITE_FILE         1038
#define ERROR_SYSTEM_PID_GET_FILE_INFO      1039
#define ERROR_SYSTEM_PID_SET_FILE_INFO      1040
#define ERROR_SYSTEM_FILE_ALREADY_OPENED    1041
#define ERROR_SYSTEM_FILE_OPENE             1042
#define ERROR_SYSTEM_FILE_CLOSE             1043
#define ERROR_SYSTEM_FILE_READ              1044
#define ERROR_SYSTEM_FILE_WRITE             1045
#define ERROR_SYSTEM_FILE_EOF               1046
#define ERROR_SYSTEM_FILE_RENAME            1047
#define ERROR_SYSTEM_CREATE_PIPE            1048
#define ERROR_SYSTEM_FILE_SEEK              1049
#define ERROR_SYSTEM_IO_INVALID             1050

///////////////////////////////////////////////////////
// RTMP protocol error.
///////////////////////////////////////////////////////
#define ERROR_RTMP_PLAIN_REQUIRED           2000
#define ERROR_RTMP_CHUNK_START              2001
#define ERROR_RTMP_MSG_INVLIAD_SIZE         2002
#define ERROR_RTMP_AMF0_DECODE              2003
#define ERROR_RTMP_AMF0_INVALID             2004
#define ERROR_RTMP_REQ_CONNECT              2005
#define ERROR_RTMP_REQ_TCURL                2006
#define ERROR_RTMP_MESSAGE_DECODE           2007
#define ERROR_RTMP_MESSAGE_ENCODE           2008
#define ERROR_RTMP_AMF0_ENCODE              2009
#define ERROR_RTMP_CHUNK_SIZE               2010
#define ERROR_RTMP_TRY_SIMPLE_HS            2011
#define ERROR_RTMP_CH_SCHEMA                2012
#define ERROR_RTMP_PACKET_SIZE              2013
#define ERROR_RTMP_VHOST_NOT_FOUND          2014
#define ERROR_RTMP_ACCESS_DENIED            2015
#define ERROR_RTMP_HANDSHAKE                2016
#define ERROR_RTMP_NO_REQUEST               2017
#define ERROR_RTMP_HS_SSL_REQUIRE           2018
#define ERROR_RTMP_DURATION_EXCEED          2019
#define ERROR_RTMP_EDGE_PLAY_STATE          2020
#define ERROR_RTMP_EDGE_PUBLISH_STATE       2021
#define ERROR_RTMP_EDGE_PROXY_PULL          2022
#define ERROR_RTMP_EDGE_RELOAD              2023
#define ERROR_RTMP_AGGREGATE                2024
#define ERROR_RTMP_BWTC_DATA                2025
#define ERROR_OpenSslCreateDH               2026
#define ERROR_OpenSslCreateP                2027
#define ERROR_OpenSslCreateG                2028
#define ERROR_OpenSslParseP1024             2029
#define ERROR_OpenSslSetG                   2030
#define ERROR_OpenSslGenerateDHKeys         2031
#define ERROR_OpenSslCopyKey                2032
#define ERROR_OpenSslSha256Update           2033
#define ERROR_OpenSslSha256Init             2034
#define ERROR_OpenSslSha256Final            2035
#define ERROR_OpenSslSha256EvpDigest        2036
#define ERROR_OpenSslSha256DigestSize       2037
#define ERROR_OpenSslGetPeerPublicKey       2038
#define ERROR_OpenSslComputeSharedKey       2039
//                                           
// system control message, 
// not an error, but special control logic.
// sys ctl: rtmp close stream, support replay.
#define ERROR_CONTROL_RTMP_CLOSE            2998
// FMLE stop publish and republish.
#define ERROR_CONTROL_REPUBLISH             2999

///////////////////////////////////////////////////////
// application level
///////////////////////////////////////////////////////
#define ERROR_HLS_METADATA                  3000
#define ERROR_HLS_DECODE_ERROR              3001
#define ERROR_HLS_CREATE_DIR                3002
#define ERROR_HLS_OPEN_FAILED               3003
#define ERROR_HLS_WRITE_FAILED              3004
#define ERROR_HLS_AAC_FRAME_LENGTH          3005
#define ERROR_HLS_AVC_SAMPLE_SIZE           3006
#define ERROR_HTTP_PARSE_URI                3007
#define ERROR_HTTP_DATA_INVLIAD             3008
#define ERROR_HTTP_PARSE_HEADER             3009
#define ERROR_HTTP_HANDLER_MATCH_URL        3010
#define ERROR_HTTP_HANDLER_INVALID          3011
#define ERROR_HTTP_API_LOGS                 3012
#define ERROR_HTTP_FLV_SEQUENCE_HEADER      3013
#define ERROR_HTTP_FLV_OFFSET_OVERFLOW      3014
#define ERROR_ENCODER_VCODEC                3015
#define ERROR_ENCODER_OUTPUT                3016
#define ERROR_ENCODER_ACHANNELS             3017
#define ERROR_ENCODER_ASAMPLE_RATE          3018
#define ERROR_ENCODER_ABITRATE              3019
#define ERROR_ENCODER_ACODEC                3020
#define ERROR_ENCODER_VPRESET               3021
#define ERROR_ENCODER_VPROFILE              3022
#define ERROR_ENCODER_VTHREADS              3023
#define ERROR_ENCODER_VHEIGHT               3024
#define ERROR_ENCODER_VWIDTH                3025
#define ERROR_ENCODER_VFPS                  3026
#define ERROR_ENCODER_VBITRATE              3027
#define ERROR_ENCODER_FORK                  3028
#define ERROR_ENCODER_LOOP                  3029
#define ERROR_ENCODER_OPEN                  3030
#define ERROR_ENCODER_DUP2                  3031
#define ERROR_ENCODER_PARSE                 3032
#define ERROR_ENCODER_NO_INPUT              3033
#define ERROR_ENCODER_NO_OUTPUT             3034
#define ERROR_ENCODER_INPUT_TYPE            3035
#define ERROR_KERNEL_FLV_HEADER             3036
#define ERROR_KERNEL_FLV_STREAM_CLOSED      3037
#define ERROR_KERNEL_STREAM_INIT            3038
#define ERROR_EDGE_VHOST_REMOVED            3039

/**
* whether the error code is an system control error.
*/
extern bool srs_is_system_control_error(int error_code);
extern bool srs_is_client_gracefully_close(int error_code);

/**
@remark: use column copy to generate the new error codes.
01234567890
01234567891
01234567892
01234567893
01234567894
01234567895
01234567896
01234567897
01234567898
01234567899
*/

#endif

