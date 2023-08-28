//
// Copyright (c) 2013-2023 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#ifndef SRS_KERNEL_ERROR_HPP
#define SRS_KERNEL_ERROR_HPP

#include <srs_core.hpp>

#include <string>

/**************************************************/
/* The system error. */
#define SRS_ERRNO_MAP_SYSTEM(XX) \
    XX(ERROR_SOCKET_CREATE                 , 1000, "SocketCreate", "Create socket fd failed") \
    XX(ERROR_SOCKET_SETREUSE               , 1001, "SocketReuse", "Setup socket reuse option failed") \
    XX(ERROR_SOCKET_BIND                   , 1002, "SocketBind", "Bind socket failed") \
    XX(ERROR_SOCKET_LISTEN                 , 1003, "SocketListen", "Listen at specified port failed") \
    XX(ERROR_SOCKET_CLOSED                 , 1004, "SocketClosed", "Socket is closed") \
    XX(ERROR_SOCKET_GET_PEER_NAME          , 1005, "SocketPeerName", "Socket get peer name failed") \
    XX(ERROR_SOCKET_GET_PEER_IP            , 1006, "SocketPeerIp", "Socket get peer ip failed") \
    XX(ERROR_SOCKET_READ                   , 1007, "SocketRead", "Socket read data failed") \
    XX(ERROR_SOCKET_READ_FULLY             , 1008, "SocketReadFully", "Socket fully read data failed") \
    XX(ERROR_SOCKET_WRITE                  , 1009, "SocketWrite", "Socket write data failed") \
    XX(ERROR_SOCKET_WAIT                   , 1010, "SocketWait", "Socket wait for ready") \
    XX(ERROR_SOCKET_TIMEOUT                , 1011, "SocketTimeout", "Socket io timeout") \
    XX(ERROR_SOCKET_CONNECT                , 1012, "SocketConnect", "Connect to server by socket failed") \
    XX(ERROR_ST_SET_EPOLL                  , 1013, "StSetEpoll", "Setup ST with epoll failed") \
    XX(ERROR_ST_INITIALIZE                 , 1014, "StInitialize", "Initialize ST failed") \
    XX(ERROR_ST_OPEN_SOCKET                , 1015, "StOpenSocket", "ST open socket failed") \
    XX(ERROR_ST_CREATE_LISTEN_THREAD       , 1016, "StListenThread", "ST create listen thread") \
    XX(ERROR_ST_CREATE_CYCLE_THREAD        , 1017, "StCycleThread", "ST create cycle thread") \
    XX(ERROR_ST_CONNECT                    , 1018, "StConnect", "ST connect server failed") \
    XX(ERROR_SYSTEM_PACKET_INVALID         , 1019, "RtmpInvalidPacket", "Got invalid RTMP packet to codec") \
    XX(ERROR_SYSTEM_CLIENT_INVALID         , 1020, "RtmpInvalidClient", "Got invalid RTMP client neither publisher nor player") \
    XX(ERROR_SYSTEM_ASSERT_FAILED          , 1021, "SystemAssert", "System assert failed for fatal error") \
    XX(ERROR_READER_BUFFER_OVERFLOW        , 1022, "FastStreamGrow", "Fast stream buffer grow failed") \
    XX(ERROR_SYSTEM_CONFIG_INVALID         , 1023, "ConfigInvalid", "Configuration is invalid") \
    XX(ERROR_SYSTEM_STREAM_BUSY            , 1028, "StreamBusy", "Stream already exists or busy") \
    XX(ERROR_SYSTEM_IP_INVALID             , 1029, "IpInvalid", "Retrieve IP failed") \
    XX(ERROR_SYSTEM_FORWARD_LOOP           , 1030, "RtmpForwardLoop", "Infinity loop for RTMP forwarding") \
    XX(ERROR_SYSTEM_WAITPID                , 1031, "DaemonWaitPid", "Wait pid failed for daemon process") \
    XX(ERROR_SYSTEM_BANDWIDTH_KEY          , 1032, "BandwidthKey", "Invalid key for RTMP bandwidth check") \
    XX(ERROR_SYSTEM_BANDWIDTH_DENIED       , 1033, "BandwidthDenied", "Denied for RTMP bandwidth check") \
    XX(ERROR_SYSTEM_PID_ACQUIRE            , 1034, "PidFileAcquire", "SRS process exists so that acquire pid file failed") \
    XX(ERROR_SYSTEM_PID_ALREADY_RUNNING    , 1035, "PidFileProcessExists", "Exists SRS process specified by pid file") \
    XX(ERROR_SYSTEM_PID_LOCK               , 1036, "PidFileLock", "SRS process exists so that lock pid file failed") \
    XX(ERROR_SYSTEM_PID_TRUNCATE_FILE      , 1037, "PidFileTruncate", "SRS process exists so that truncate pid file failed") \
    XX(ERROR_SYSTEM_PID_WRITE_FILE         , 1038, "PidFileWrite", "SRS process exists so that write pid file failed") \
    XX(ERROR_SYSTEM_PID_GET_FILE_INFO      , 1039, "PidFileQuery", "SRS process exists so that query pid file failed") \
    XX(ERROR_SYSTEM_PID_SET_FILE_INFO      , 1040, "PidFileUpdate", "SRS process exists so that update pid file failed") \
    XX(ERROR_SYSTEM_FILE_ALREADY_OPENED    , 1041, "FileOpened", "File open failed for already opened") \
    XX(ERROR_SYSTEM_FILE_OPENE             , 1042, "FileOpen", "Failed to open file") \
    XX(ERROR_SYSTEM_FILE_CLOSE             , 1043, "FileClose", "Failed to close file") \
    XX(ERROR_SYSTEM_FILE_READ              , 1044, "FileRead", "Failed to read data from file") \
    XX(ERROR_SYSTEM_FILE_WRITE             , 1045, "FileWrite", "Failed to write data to file") \
    XX(ERROR_SYSTEM_FILE_EOF               , 1046, "FileEof", "File is EOF or end of file") \
    XX(ERROR_SYSTEM_FILE_RENAME            , 1047, "FileRename", "Failed to rename file") \
    XX(ERROR_SYSTEM_CREATE_PIPE            , 1048, "PipeCreate", "Create pipe for signal failed") \
    XX(ERROR_SYSTEM_FILE_SEEK              , 1049, "FileSeek", "Failed to seek in file") \
    XX(ERROR_SYSTEM_IO_INVALID             , 1050, "IOInvalid", "Invalid IO operation") \
    XX(ERROR_ST_EXCEED_THREADS             , 1051, "StThreadsExceed", "ST exceed max threads") \
    XX(ERROR_SYSTEM_SECURITY               , 1052, "SecurityCheck", "Referer security check failed") \
    XX(ERROR_SYSTEM_SECURITY_DENY          , 1053, "SecurityDeny", "Referer security failed for deny rules") \
    XX(ERROR_SYSTEM_SECURITY_ALLOW         , 1054, "SecurityAllow", "Referer security failed for allow rules") \
    XX(ERROR_SYSTEM_TIME                   , 1055, "SystemTime", "Invalid system time") \
    XX(ERROR_SYSTEM_DIR_EXISTS             , 1056, "DirExists", "Directory already exists") \
    XX(ERROR_SYSTEM_CREATE_DIR             , 1057, "DirCreate", "Create directory failed") \
    XX(ERROR_SYSTEM_KILL                   , 1058, "ProcessKill", "Send signal to process by kill failed") \
    XX(ERROR_SYSTEM_CONFIG_PERSISTENCE     , 1059, "ConfigPersistence", "Failed to persistence configuration") \
    XX(ERROR_SYSTEM_CONFIG_RAW             , 1060, "ConfigRaw", "RAW API serves failed") \
    XX(ERROR_SYSTEM_CONFIG_RAW_DISABLED    , 1061, "ConfigRawDisable", "RAW API is disabled") \
    XX(ERROR_SYSTEM_CONFIG_RAW_NOT_ALLOWED , 1062, "ConfigRawNotAllowed", "RAW API is not allowed") \
    XX(ERROR_SYSTEM_CONFIG_RAW_PARAMS      , 1063, "ConfigRawParams", "RAW API parameters are invalid") \
    XX(ERROR_SYSTEM_FILE_NOT_EXISTS        , 1064, "FileNotFound", "Request file does not exists") \
    XX(ERROR_SYSTEM_HOURGLASS_RESOLUTION   , 1065, "TimerResolution", "Resolution for timer or hourglass is invalid") \
    XX(ERROR_SYSTEM_DNS_RESOLVE            , 1066, "DnsResolve", "Failed to parse domain name by DNS") \
    XX(ERROR_SYSTEM_FRAGMENT_UNLINK        , 1067, "FileRemove", "Failed to remove or unlink file") \
    XX(ERROR_SYSTEM_FRAGMENT_RENAME        , 1068, "FileRename", "Failed to rename file segment") \
    XX(ERROR_THREAD_DISPOSED               , 1069, "StThreadDispose", "Failed for ST thread is disposed") \
    XX(ERROR_THREAD_INTERRUPED             , 1070, "StThreadInterrupt", "ST thread is interrupted") \
    XX(ERROR_THREAD_TERMINATED             , 1071, "StThreadTerminate", "ST thread is terminated") \
    XX(ERROR_THREAD_DUMMY                  , 1072, "StThreadDummy", "Can not operate ST dummy thread") \
    XX(ERROR_ASPROCESS_PPID                , 1073, "SystemAsProcess", "As-process does not support pid change") \
    XX(ERROR_EXCEED_CONNECTIONS            , 1074, "ConnectionsExceed", "Failed for exceed system max connections") \
    XX(ERROR_SOCKET_SETKEEPALIVE           , 1075, "SocketKeepAlive", "Failed to set socket option SO_KEEPALIVE") \
    XX(ERROR_SOCKET_NO_NODELAY             , 1076, "SocketNoDelay", "Failed to set socket option TCP_NODELAY") \
    XX(ERROR_SOCKET_SNDBUF                 , 1077, "SocketSendBuffer", "Failed to set socket option SO_SNDBUF") \
    XX(ERROR_THREAD_STARTED                , 1078, "StThreadStarted", "ST thread is already started") \
    XX(ERROR_SOCKET_SETREUSEADDR           , 1079, "SocketReuseAddr", "Failed to set socket option SO_REUSEADDR") \
    XX(ERROR_SOCKET_SETCLOSEEXEC           , 1080, "SocketCloseExec", "Failed to set socket option FD_CLOEXEC") \
    XX(ERROR_SOCKET_ACCEPT                 , 1081, "SocketAccept", "Accpet client socket failed") \
    XX(ERROR_THREAD_CREATE                 , 1082, "StThreadCreate", "Create ST thread failed") \
    XX(ERROR_THREAD_FINISHED               , 1083, "StThreadFinished", "ST thread finished without error") \
    XX(ERROR_PB_NO_SPACE                   , 1084, "ProtobufNoSpace", "Failed to encode protobuf for no buffer space left") \
    XX(ERROR_CLS_INVALID_CONFIG            , 1085, "ClsConfig", "Invalid configuration for TencentCloud CLS") \
    XX(ERROR_CLS_EXCEED_SIZE               , 1086, "ClsExceedSize", "CLS logs exceed max size 5MB") \
    XX(ERROR_APM_EXCEED_SIZE               , 1087, "ApmExceedSize", "APM logs exceed max size 5MB") \
    XX(ERROR_APM_ENDPOINT                  , 1088, "ApmEndpoint", "APM endpoint is invalid") \
    XX(ERROR_APM_AUTH                      , 1089, "ApmAuth", "APM team or token is invalid") \
    XX(ERROR_EXPORTER_DISABLED             , 1090, "ExporterDisable", "Prometheus exporter is disabled") \
    XX(ERROR_ST_SET_SELECT                 , 1091, "StSetSelect", "ST set select failed") \
    XX(ERROR_BACKTRACE_PARSE_NOT_SUPPORT   , 1092, "BacktraceParseNotSupport", "Backtrace parse not supported") \
    XX(ERROR_BACKTRACE_PARSE_OFFSET        , 1093, "BacktraceParseOffset", "Parse backtrace offset failed") \
    XX(ERROR_BACKTRACE_ADDR2LINE           , 1094, "BacktraceAddr2Line", "Backtrace addr2line failed") \
    XX(ERROR_SYSTEM_FILE_NOT_OPEN          , 1095, "FileNotOpen", "File is not opened") \
    XX(ERROR_SYSTEM_FILE_SETVBUF           , 1096, "FileSetVBuf", "Failed to set file vbuf") \

/**************************************************/
/* RTMP protocol error. */
#define SRS_ERRNO_MAP_RTMP(XX) \
    XX(ERROR_RTMP_PLAIN_REQUIRED           , 2000, "RtmpPlainRequired", "RTMP handshake requires plain text") \
    XX(ERROR_RTMP_CHUNK_START              , 2001, "RtmpChunkStart", "RTMP packet must be fresh chunk") \
    XX(ERROR_RTMP_MSG_INVALID_SIZE         , 2002, "RtmpMsgSize", "Invalid RTMP message size") \
    XX(ERROR_RTMP_AMF0_DECODE              , 2003, "Amf0Decode", "Decode AMF0 message failed") \
    XX(ERROR_RTMP_AMF0_INVALID             , 2004, "Amf0Invalid", "Invalid AMF0 message type") \
    XX(ERROR_RTMP_REQ_CONNECT              , 2005, "RtmpConnect", "Invalid RTMP connect packet without tcUrl") \
    XX(ERROR_RTMP_REQ_TCURL                , 2006, "RtmpTcUrl", "Failed to discover tcUrl from request") \
    XX(ERROR_RTMP_MESSAGE_DECODE           , 2007, "RtmpDecode", "Failed to decode RTMP packet") \
    XX(ERROR_RTMP_MESSAGE_ENCODE           , 2008, "RtmpEncode", "Failed to encode RTMP packet") \
    XX(ERROR_RTMP_AMF0_ENCODE              , 2009, "Amf0Encode", "Encode AMF0 message failed") \
    XX(ERROR_RTMP_CHUNK_SIZE               , 2010, "RtmpChunkSize", "Invalid RTMP chunk size") \
    XX(ERROR_RTMP_TRY_SIMPLE_HS            , 2011, "RtmpTrySimple", "Handshake failed please try simple strategy") \
    XX(ERROR_RTMP_CH_SCHEMA                , 2012, "RtmpSchema", "RTMP handshake failed for schema changed") \
    XX(ERROR_RTMP_PACKET_SIZE              , 2013, "RtmpPacketSize", "RTMP packet size changed in chunks") \
    XX(ERROR_RTMP_VHOST_NOT_FOUND          , 2014, "NoVhost", "Request vhost not found") \
    XX(ERROR_RTMP_ACCESS_DENIED            , 2015, "SecurityDenied", "Referer check failed and access denied") \
    XX(ERROR_RTMP_HANDSHAKE                , 2016, "RtmpHandshake", "RTMP handshake failed") \
    XX(ERROR_RTMP_NO_REQUEST               , 2017, "RtmpNoRequest", "Invalid RTMP response for no request found") \
    XX(ERROR_RTMP_HS_SSL_REQUIRE           , 2018, "RtmpSslRequire", "RTMP handshake failed for SSL required") \
    XX(ERROR_RTMP_DURATION_EXCEED          , 2019, "DurationExceed", "Failed for exceed max service duration") \
    XX(ERROR_RTMP_EDGE_PLAY_STATE          , 2020, "EdgePlayState", "Invalid edge state for play") \
    XX(ERROR_RTMP_EDGE_PUBLISH_STATE       , 2021, "EdgePublishState", "Invalid edge state for publish") \
    XX(ERROR_RTMP_EDGE_PROXY_PULL          , 2022, "EdgeProxyPull", "Failed for edge pull state") \
    XX(ERROR_RTMP_EDGE_RELOAD              , 2023, "EdgeReload", "Vhost with edge does not support reload") \
    XX(ERROR_RTMP_AGGREGATE                , 2024, "RtmpAggregate", "Failed to handle RTMP aggregate message") \
    XX(ERROR_RTMP_BWTC_DATA                , 2025, "BandwidthData", "Invalid data for bandwidth check") \
    XX(ERROR_OpenSslCreateDH               , 2026, "SslCreateDh", "Failed to create DH for SSL") \
    XX(ERROR_OpenSslCreateP                , 2027, "SslCreateP", "Failed to create P by BN for SSL") \
    XX(ERROR_OpenSslCreateG                , 2028, "SslCreateG", "Failed to create G by BN for SSL") \
    XX(ERROR_OpenSslParseP1024             , 2029, "SslParseP1024", "Failed to parse P1024 for SSL") \
    XX(ERROR_OpenSslSetG                   , 2030, "SslSetG", "Failed to set G by BN for SSL") \
    XX(ERROR_OpenSslGenerateDHKeys         , 2031, "SslDhKeys", "Failed to generate DH keys for SSL") \
    XX(ERROR_OpenSslCopyKey                , 2032, "SslCopyKey", "Failed to copy key for SSL") \
    XX(ERROR_OpenSslSha256Update           , 2033, "SslSha256Update", "Failed to update HMAC sha256 for SSL") \
    XX(ERROR_OpenSslSha256Init             , 2034, "SslSha256Init", "Failed to init HMAC sha256 for SSL") \
    XX(ERROR_OpenSslSha256Final            , 2035, "SslSha256Final", "Failed to final HMAC sha256 for SSL") \
    XX(ERROR_OpenSslSha256EvpDigest        , 2036, "SslSha256Digest", "Failed to calculate evp digest of HMAC sha256 for SSL") \
    XX(ERROR_OpenSslSha256DigestSize       , 2037, "SslSha256Size", "Invalid digest size of HMAC sha256 of SSL") \
    XX(ERROR_OpenSslGetPeerPublicKey       , 2038, "SslPublicKey", "Failed to get peer public key of SSL") \
    XX(ERROR_OpenSslComputeSharedKey       , 2039, "SslShareKey", "Failed to get shared key of SSL") \
    XX(ERROR_RTMP_MIC_CHUNKSIZE_CHANGED    , 2040, "RtmpMicChunk", "Invalid RTMP mic for chunk size changed") \
    XX(ERROR_RTMP_MIC_CACHE_OVERFLOW       , 2041, "RtmpMicCache", "Invalid RTMP mic for cache overflow") \
    XX(ERROR_RTSP_TOKEN_NOT_NORMAL         , 2042, "RtspToken", "Invalid RTSP token state not normal") \
    XX(ERROR_RTSP_REQUEST_HEADER_EOF       , 2043, "RtspHeaderEof", "Invalid RTSP request for header EOF") \
    XX(ERROR_RTP_HEADER_CORRUPT            , 2044, "RtspHeaderCorrupt", "Invalid RTSP RTP packet for header corrupt") \
    XX(ERROR_RTP_TYPE96_CORRUPT            , 2045, "RtspP96Corrupt", "Invalid RTSP RTP packet for P96 corrupt") \
    XX(ERROR_RTP_TYPE97_CORRUPT            , 2046, "RtspP97Corrupt", "Invalid RTSP RTP packet for P97 corrupt") \
    XX(ERROR_RTSP_AUDIO_CONFIG             , 2047, "RtspAudioConfig", "RTSP no audio sequence header config") \
    XX(ERROR_RTMP_STREAM_NOT_FOUND         , 2048, "StreamNotFound", "Request stream is not found") \
    XX(ERROR_RTMP_CLIENT_NOT_FOUND         , 2049, "ClientNotFound", "Request client is not found") \
    XX(ERROR_OpenSslCreateHMAC             , 2050, "SslCreateHmac", "Failed to create HMAC for SSL") \
    XX(ERROR_RTMP_STREAM_NAME_EMPTY        , 2051, "StreamNameEmpty", "Invalid stream for name is empty") \
    XX(ERROR_HTTP_HIJACK                   , 2052, "HttpHijack", "Failed to hijack HTTP handler") \
    XX(ERROR_RTMP_MESSAGE_CREATE           , 2053, "MessageCreate", "Failed to create shared pointer message") \
    XX(ERROR_RTMP_PROXY_EXCEED             , 2054, "RtmpProxy", "Failed to decode message of RTMP proxy") \
    XX(ERROR_RTMP_CREATE_STREAM_DEPTH      , 2055, "RtmpIdentify", "Failed to identify RTMP client") \
    XX(ERROR_KICKOFF_FOR_IDLE              , 2056, "KickoffForIdle", "Kickoff for publisher is idle") \
    XX(ERROR_CONTROL_REDIRECT              , 2997, "RtmpRedirect", "RTMP 302 redirection") \
    XX(ERROR_CONTROL_RTMP_CLOSE            , 2998, "RtmpClose", "RTMP connection is closed") \
    XX(ERROR_CONTROL_REPUBLISH             , 2999, "RtmpRepublish", "RTMP stream is republished")

/**************************************************/
/* The application level errors. */
#define SRS_ERRNO_MAP_APP(XX) \
    XX(ERROR_HLS_METADATA                  , 3000, "HlsMetadata", "HLS metadata is invalid") \
    XX(ERROR_HLS_DECODE_ERROR              , 3001, "HlsDecode", "HLS decode av stream failed") \
    XX(ERROR_HLS_CREATE_DIR                , 3002, "HlsCreateDir", "HLS create directory failed") \
    XX(ERROR_HLS_OPEN_FAILED               , 3003, "HlsOpenFile", "HLS open m3u8 file failed") \
    XX(ERROR_HLS_WRITE_FAILED              , 3004, "HlsWriteFile", "HLS write m3u8 file failed") \
    XX(ERROR_HLS_AAC_FRAME_LENGTH          , 3005, "HlsAacFrame", "HLS decode aac frame size failed") \
    XX(ERROR_HLS_AVC_SAMPLE_SIZE           , 3006, "HlsAvcFrame", "HLS decode avc sample size failed") \
    XX(ERROR_HTTP_PARSE_URI                , 3007, "HttpParseUrl", "HTTP parse url failed") \
    XX(ERROR_HTTP_DATA_INVALID             , 3008, "HttpResponseData", "HTTP response data invalid") \
    XX(ERROR_HTTP_PARSE_HEADER             , 3009, "HttpParseHeader", "HTTP parse header failed") \
    XX(ERROR_HTTP_HANDLER_MATCH_URL        , 3010, "HttpHandlerUrl", "HTTP handler invalid url") \
    XX(ERROR_HTTP_HANDLER_INVALID          , 3011, "HttpHandlerInvalid", "HTTP handler invalid") \
    XX(ERROR_HTTP_API_LOGS                 , 3012, "HttpApiLogs", "HTTP API logs invalid") \
    XX(ERROR_HTTP_REMUX_SEQUENCE_HEADER    , 3013, "VodNoSequence", "HTTP VoD sequence header not found") \
    XX(ERROR_HTTP_REMUX_OFFSET_OVERFLOW    , 3014, "VodOffset", "HTTP VoD request offset overflow") \
    XX(ERROR_ENCODER_VCODEC                , 3015, "FFmpegNoCodec", "FFmpeg no audio and video codec") \
    XX(ERROR_ENCODER_OUTPUT                , 3016, "FFmpegNoOutput", "FFmpeg no output url") \
    XX(ERROR_ENCODER_ACHANNELS             , 3017, "FFmpegChannels", "Invalid audio channels for FFmpeg") \
    XX(ERROR_ENCODER_ASAMPLE_RATE          , 3018, "FFmpegSampleRate", "Invalid audio sample rate for FFmpeg") \
    XX(ERROR_ENCODER_ABITRATE              , 3019, "FFmpegAudioBitrate", "Invalid audio bitrate for FFmpeg") \
    XX(ERROR_ENCODER_ACODEC                , 3020, "FFmpegAudioCodec", "Invalid audio codec for FFmpeg") \
    XX(ERROR_ENCODER_VPRESET               , 3021, "FFmpegPreset", "Invalid video preset for FFmpeg") \
    XX(ERROR_ENCODER_VPROFILE              , 3022, "FFmpegProfile", "Invalid video profile for FFmpeg") \
    XX(ERROR_ENCODER_VTHREADS              , 3023, "FFmpegThreads", "Invalid threads config for FFmpeg") \
    XX(ERROR_ENCODER_VHEIGHT               , 3024, "FFmpegHeight", "Invalid video height for FFmpeg") \
    XX(ERROR_ENCODER_VWIDTH                , 3025, "FFmpegWidth", "Invalid video width for FFmpeg") \
    XX(ERROR_ENCODER_VFPS                  , 3026, "FFmpegFps", "Invalid video FPS for FFmpeg") \
    XX(ERROR_ENCODER_VBITRATE              , 3027, "FFmpegVideoBitrate", "Invalid video bitrate for FFmpeg") \
    XX(ERROR_ENCODER_FORK                  , 3028, "FFmpegFork", "Failed to fork FFmpeg trancoder process") \
    XX(ERROR_ENCODER_LOOP                  , 3029, "FFmpegLoop", "FFmpeg transcoder infinite loop detected") \
    XX(ERROR_FORK_OPEN_LOG                 , 3030, "FFmpegLog", "Open log file failed for FFmpeg") \
    XX(ERROR_FORK_DUP2_LOG                 , 3031, "FFmpegDup2", "Dup2 redirect log failed for FFmpeg") \
    XX(ERROR_ENCODER_PARSE                 , 3032, "FFmpegBinary", "FFmpeg binary not found") \
    XX(ERROR_ENCODER_NO_INPUT              , 3033, "FFmpegNoInput", "No input url for FFmpeg") \
    XX(ERROR_ENCODER_NO_OUTPUT             , 3034, "FFmpegNoOutput", "No output url for FFmpeg") \
    XX(ERROR_ENCODER_INPUT_TYPE            , 3035, "FFmpegInputType", "Invalid input type for FFmpeg") \
    XX(ERROR_KERNEL_FLV_HEADER             , 3036, "FlvHeader", "FLV decode header failed") \
    XX(ERROR_KERNEL_FLV_STREAM_CLOSED      , 3037, "VodFlvClosed", "FLV file closed for HTTP VoD") \
    XX(ERROR_KERNEL_STREAM_INIT            , 3038, "StreamInit", "Init kernel stream failed") \
    XX(ERROR_EDGE_VHOST_REMOVED            , 3039, "EdgeVhostRemoved", "Vhost is removed for edge") \
    XX(ERROR_HLS_AVC_TRY_OTHERS            , 3040, "AvcTryOthers", "Should try other strategies for AVC decoder") \
    XX(ERROR_H264_API_NO_PREFIXED          , 3041, "AvcAnnexbPrefix", "No annexb prefix for AVC decoder") \
    XX(ERROR_FLV_INVALID_VIDEO_TAG         , 3042, "FlvInvalidTag", "Invalid video tag for FLV") \
    XX(ERROR_H264_DROP_BEFORE_SPS_PPS      , 3043, "DropBeforeSequence", "Drop frames before get sps and pps") \
    XX(ERROR_H264_DUPLICATED_SPS           , 3044, "SpsDuplicate", "Got duplicated sps for video") \
    XX(ERROR_H264_DUPLICATED_PPS           , 3045, "PpsDuplicate", "Got duplicated pps for video") \
    XX(ERROR_AAC_REQUIRED_ADTS             , 3046, "AacAdtsRequire", "ADTS header is required for AAC") \
    XX(ERROR_AAC_ADTS_HEADER               , 3047, "AacAdtsHeader", "Failed to parse ADTS header for AAC") \
    XX(ERROR_AAC_DATA_INVALID              , 3048, "AacData", "Failed to parse data for AAC") \
    XX(ERROR_HLS_TRY_MP3                   , 3049, "HlsTryMp3", "Should try mp3 when codec detected") \
    XX(ERROR_HTTP_DVR_DISABLED             , 3050, "DvrDisabled", "Failed for DVR disabled") \
    XX(ERROR_HTTP_DVR_REQUEST              , 3051, "DvrRequest", "Failed for DVR request") \
    XX(ERROR_HTTP_JSON_REQUIRED            , 3052, "JsonRequired", "Failed for JSON required") \
    XX(ERROR_HTTP_DVR_CREATE_REQUEST       , 3053, "DvrCreate", "Failed for DVR create request") \
    XX(ERROR_HTTP_DVR_NO_TAEGET            , 3054, "DvrNoTarget", "Failed for DVR no target") \
    XX(ERROR_ADTS_ID_NOT_AAC               , 3055, "AacAdtsId", "Failed for ADTS id not AAC") \
    XX(ERROR_HDS_OPEN_F4M_FAILED           , 3056, "HdsOpenF4m", "Failed to open F4m file for HDS") \
    XX(ERROR_HDS_WRITE_F4M_FAILED          , 3057, "HdsWriteF4m", "Failed to write F4m file for HDS") \
    XX(ERROR_HDS_OPEN_BOOTSTRAP_FAILED     , 3058, "HdsOpenBoot", "Failed to open bootstrap for HDS") \
    XX(ERROR_HDS_WRITE_BOOTSTRAP_FAILED    , 3059, "HdsWriteBoot", "Failed to write bootstrap for HDS") \
    XX(ERROR_HDS_OPEN_FRAGMENT_FAILED      , 3060, "HdsOpenFragment", "Failed to open fragment file for HDS") \
    XX(ERROR_HDS_WRITE_FRAGMENT_FAILED     , 3061, "HdsWriteFragment", "Failed to write fragment file for HDS") \
    XX(ERROR_HLS_NO_STREAM                 , 3062, "HlsNoStream", "No stream configured for HLS") \
    XX(ERROR_JSON_LOADS                    , 3063, "JsonLoads", "Failed for JSOn loads") \
    XX(ERROR_RESPONSE_CODE                 , 3064, "HttpResponseCode", "No code in HTTP response json object") \
    XX(ERROR_RESPONSE_DATA                 , 3065, "HttpResponseData", "Invalid HTTP response body data") \
    XX(ERROR_REQUEST_DATA                  , 3066, "HttpRequestData", "Invalid HTTP request body data") \
    XX(ERROR_EDGE_PORT_INVALID             , 3067, "EdgePort", "Invalid edge port") \
    XX(ERROR_EXPECT_FILE_IO                , 3068, "VodNotFile", "HTTP VoD not file stream") \
    XX(ERROR_MP4_BOX_OVERFLOW              , 3069, "Mp4BoxOverflow", "Only support 32 bits box for MP4") \
    XX(ERROR_MP4_BOX_REQUIRE_SPACE         , 3070, "Mp4BoxNoSpace", "Failed to decode MP4 box for no buffer space") \
    XX(ERROR_MP4_BOX_ILLEGAL_TYPE          , 3071, "Mp4BoxType", "Invalid box type for MP4") \
    XX(ERROR_MP4_BOX_ILLEGAL_SCHEMA        , 3072, "Mp4BoxNoFtyp", "Missing box FTYP for MP4") \
    XX(ERROR_MP4_BOX_STRING                , 3073, "Mp4BoxString", "MP4 string corrupt") \
    XX(ERROR_MP4_BOX_ILLEGAL_BRAND         , 3074, "Mp4BoxBrand", "Invalid FTYP brand for MP4") \
    XX(ERROR_MP4_ESDS_SL_Config            , 3075, "Mp4BoxEsdsSl", "Invalid SL config for ESDS box for MP4") \
    XX(ERROR_MP4_ILLEGAL_MOOV              , 3076, "Mp4BoxMoov", "Invalid MOOV box for MP4") \
    XX(ERROR_MP4_ILLEGAL_HANDLER           , 3077, "Mp4BoxHandler", "Invalid handler for MP4") \
    XX(ERROR_MP4_ILLEGAL_TRACK             , 3078, "Mp4BoxTrack", "Invalid track box for MP4") \
    XX(ERROR_MP4_MOOV_OVERFLOW             , 3079, "Mp4StszOverflow", "STSZ box size overflow for MP4") \
    XX(ERROR_MP4_ILLEGAL_SAMPLES           , 3080, "Mp4StszSamples", "STSZ box samples invalid") \
    XX(ERROR_MP4_ILLEGAL_TIMESTAMP         , 3081, "Mp4Timestamp", "Invalid timestamp of ctts or stts for MP4") \
    XX(ERROR_DVR_CANNOT_APPEND             , 3082, "DvrAppend", "DVR append data to file failed") \
    XX(ERROR_DVR_ILLEGAL_PLAN              , 3083, "DvrPlan", "Invalid DVR plan") \
    XX(ERROR_FLV_REQUIRE_SPACE             , 3084, "FlvNoSpace", "Failed to decode FLV for no buffer space") \
    XX(ERROR_MP4_AVCC_CHANGE               , 3085, "Mp4AvccChange", "MP4 does not support video AVCC change") \
    XX(ERROR_MP4_ASC_CHANGE                , 3086, "Mp4AscChange", "MP4 does not support audio ASC change") \
    XX(ERROR_DASH_WRITE_FAILED             , 3087, "DashMpdWrite", "DASH write mpd file failed") \
    XX(ERROR_TS_CONTEXT_NOT_READY          , 3088, "TsContexNotReady", "TS context not ready") \
    XX(ERROR_MP4_ILLEGAL_MOOF              , 3089, "Mp4BoxNoMoof", "Missing MP4 box MOOF or no audio video track") \
    XX(ERROR_MP4_ILLEGAL_MDAT              , 3090, "Mp4BoxMdat", "Invalid MDAT header size") \
    XX(ERROR_OCLUSTER_DISCOVER             , 3091, "OriginClusterDiscover", "Failed to discover origin cluster service") \
    XX(ERROR_OCLUSTER_REDIRECT             , 3092, "OriginClusterRedirect", "Failed to redirect origin cluster node") \
    XX(ERROR_INOTIFY_CREATE                , 3093, "InotifyCreate", "Failed to create inotify for config listener") \
    XX(ERROR_INOTIFY_OPENFD                , 3094, "InotifyOpenFd", "Failed to open inotify fd for config listener") \
    XX(ERROR_INOTIFY_WATCH                 , 3095, "InotfyWatch", "Failed to watch inotify for config listener") \
    XX(ERROR_HTTP_URL_UNESCAPE             , 3096, "HttpUrlUnescape", "Failed to unescape URL for HTTP") \
    XX(ERROR_HTTP_WITH_BODY                , 3097, "HttpWithBody", "Failed for HTTP body") \
    XX(ERROR_HEVC_DISABLED                 , 3098, "HevcDisabled", "HEVC is disabled") \
    XX(ERROR_HEVC_DECODE_ERROR             , 3099, "HevcDecode", "HEVC decode av stream failed")  \
    XX(ERROR_MP4_HVCC_CHANGE               , 3100, "Mp4HvcCChange", "MP4 does not support video HvcC change") \
    XX(ERROR_HEVC_API_NO_PREFIXED          , 3101, "HevcAnnexbPrefix", "No annexb prefix for HEVC decoder")

/**************************************************/
/* HTTP/StreamConverter protocol error. */
#define SRS_ERRNO_MAP_HTTP(XX) \
    XX(ERROR_HTTP_PATTERN_EMPTY            , 4000, "HttpPatternEmpty", "Failed to handle HTTP request for no pattern") \
    XX(ERROR_HTTP_PATTERN_DUPLICATED       , 4001, "HttpPatternDuplicated", "Failed to handle HTTP request for pattern duplicated") \
    XX(ERROR_HTTP_URL_NOT_CLEAN            , 4002, "HttpUrlNotClean", "Failed to handle HTTP request for URL not clean") \
    XX(ERROR_HTTP_CONTENT_LENGTH           , 4003, "HttpContentLength", "Exceed HTTP content length") \
    XX(ERROR_HTTP_LIVE_STREAM_EXT          , 4004, "HttpStreamExt", "Invalid HTTP stream extension") \
    XX(ERROR_HTTP_STATUS_INVALID           , 4005, "HttpStatus", "Invalid HTTP status code") \
    XX(ERROR_KERNEL_AAC_STREAM_CLOSED      , 4006, "AacStreamClosed", "AAC stream is closed") \
    XX(ERROR_AAC_DECODE_ERROR              , 4007, "AacStreamDecode", "Failed to decode AAC stream") \
    XX(ERROR_KERNEL_MP3_STREAM_CLOSED      , 4008, "Mp3StreamClosed", "MP3 stream is closed") \
    XX(ERROR_MP3_DECODE_ERROR              , 4009, "Mp3StreamDecode", "Failed to decode MP3 stream") \
    XX(ERROR_STREAM_CASTER_ENGINE          , 4010, "CasterEngine", "Invalid engine config for stream caster") \
    XX(ERROR_STREAM_CASTER_PORT            , 4011, "CasterPort", "Invalid port config for stream caster") \
    XX(ERROR_STREAM_CASTER_TS_HEADER       , 4012, "CasterTsHeader", "Invalid ts header for stream caster") \
    XX(ERROR_STREAM_CASTER_TS_SYNC_BYTE    , 4013, "CasterTsSync", "Invalid ts sync byte for stream caster") \
    XX(ERROR_STREAM_CASTER_TS_AF           , 4014, "CasterTsAdaption", "Invalid ts adaption field for stream caster") \
    XX(ERROR_STREAM_CASTER_TS_CRC32        , 4015, "CasterTsCrc32", "Invalid ts CRC32 checksum for stream caster") \
    XX(ERROR_STREAM_CASTER_TS_PSI          , 4016, "CasterTsPsi", "Invalid ts PSI payload for stream caster") \
    XX(ERROR_STREAM_CASTER_TS_PAT          , 4017, "CasterTsPat", "Invalid ts PAT program for stream caster") \
    XX(ERROR_STREAM_CASTER_TS_PMT          , 4018, "CasterTsPmt", "Invalid ts PMT information for stream caster") \
    XX(ERROR_STREAM_CASTER_TS_PSE          , 4019, "CasterTsPse", "Invalid ts PSE payload for stream caster") \
    XX(ERROR_STREAM_CASTER_TS_ES           , 4020, "CasterTsEs", "Invalid ts ES stream for stream caster") \
    XX(ERROR_STREAM_CASTER_TS_CODEC        , 4021, "CasterTsCodec", "Invalid ts codec for stream caster") \
    XX(ERROR_STREAM_CASTER_AVC_SPS         , 4022, "CasterTsAvcSps", "Invalid ts AVC SPS for stream caster") \
    XX(ERROR_STREAM_CASTER_AVC_PPS         , 4023, "CasterTsAvcPps", "Invalid ts AVC PPS for stream caster") \
    XX(ERROR_STREAM_CASTER_FLV_TAG         , 4024, "CasterFlvTag", "Invalid flv tag for stream caster") \
    XX(ERROR_HTTP_RESPONSE_EOF             , 4025, "HttpResponseEof", "HTTP response stream is EOF") \
    XX(ERROR_HTTP_INVALID_CHUNK_HEADER     , 4026, "HttpChunkHeader", "Invalid HTTP chunk header") \
    XX(ERROR_AVC_NALU_UEV                  , 4027, "AvcNaluUev", "Failed to read UEV for AVC NALU") \
    XX(ERROR_AAC_BYTES_INVALID             , 4028, "AacBytesInvalid", "Invalid bytes for AAC stream") \
    XX(ERROR_HTTP_REQUEST_EOF              , 4029, "HttpRequestEof", "HTTP request stream is EOF") \
    XX(ERROR_HTTP_302_INVALID              , 4038, "HttpRedirectDepth", "Exceed max depth for HTTP redirect") \
    XX(ERROR_BASE64_DECODE                 , 4039, "Base64Decode", "Failed to decode the BASE64 content") \
    XX(ERROR_HTTP_STREAM_EOF               , 4040, "HttpStreamEof", "HTTP stream is EOF") \
    XX(ERROR_HTTPS_NOT_SUPPORTED           , 4041, "HttpsNotSupported", "HTTPS is not supported") \
    XX(ERROR_HTTPS_HANDSHAKE               , 4042, "HttpsHandshake", "Failed to do handshake for HTTPS") \
    XX(ERROR_HTTPS_READ                    , 4043, "HttpsRead", "Failed to read data from HTTPS stream") \
    XX(ERROR_HTTPS_WRITE                   , 4044, "HttpsWrite", "Failed to write data to HTTPS stream") \
    XX(ERROR_HTTPS_KEY_CRT                 , 4045, "HttpsSslFile", "Failed to load SSL key or crt file for HTTPS") \
    XX(ERROR_GB_SIP_HEADER                 , 4046, "GbHeaderCallId", "Missing field of SIP header for GB28181") \
    XX(ERROR_GB_SIP_MESSAGE                , 4047, "GbHeaderCallId", "Invalid SIP message for GB28181") \
    XX(ERROR_GB_PS_HEADER                  , 4048, "GbPsHeader", "Invalid PS header for GB28181") \
    XX(ERROR_GB_PS_PSE                     , 4049, "GbPsPSE", "Invalid PS PSE for GB28181") \
    XX(ERROR_GB_PS_MEDIA                   , 4050, "GbPsMedia", "Invalid PS Media packet for GB28181") \
    XX(ERROR_GB_SSRC_GENERATE              , 4051, "GbSsrcGenerate", "Failed to generate SSRC for GB28181") \
    XX(ERROR_GB_CONFIG                     , 4052, "GbConfig", "Invalid configuration for GB28181") \
    XX(ERROR_GB_TIMEOUT                    , 4053, "GbTimeout", "SIP or media connection timeout for GB28181") \
    XX(ERROR_HEVC_NALU_UEV                 , 4054, "HevcNaluUev", "Failed to read UEV for HEVC NALU") \
    XX(ERROR_HEVC_NALU_SEV                 , 4055, "HevcNaluSev", "Failed to read SEV for HEVC NALU") \
    XX(ERROR_STREAM_CASTER_HEVC_VPS        , 4054, "CasterTsHevcVps", "Invalid ts HEVC VPS for stream caster") \
    XX(ERROR_STREAM_CASTER_HEVC_SPS        , 4055, "CasterTsHevcSps", "Invalid ts HEVC SPS for stream caster") \
    XX(ERROR_STREAM_CASTER_HEVC_PPS        , 4056, "CasterTsHevcPps", "Invalid ts HEVC PPS for stream caster") \
    XX(ERROR_STREAM_CASTER_HEVC_FORMAT     , 4057, "CasterTsHevcFormat", "Invalid ts HEVC Format for stream caster")


/**************************************************/
/* RTC protocol error. */
#define SRS_ERRNO_MAP_RTC(XX) \
    XX(ERROR_RTC_PORT                      , 5000, "RtcPort", "Invalid RTC config for listen port") \
    XX(ERROR_RTP_PACKET_CREATE             , 5001, "RtcPacketCreate", "Failed to create RTP packet for RTC") \
    XX(ERROR_OpenSslCreateSSL              , 5002, "RtcSslCreate", "RTC create SSL context failed") \
    XX(ERROR_OpenSslBIOReset               , 5003, "RtcSslReset", "RTC reset SSL BIO context failed") \
    XX(ERROR_OpenSslBIOWrite               , 5004, "RtcSslWrite", "RTC write SSL BIO stream failed") \
    XX(ERROR_OpenSslBIONew                 , 5005, "RtcSslNew", "RTC create new SSL BIO context failed") \
    XX(ERROR_RTC_RTP                       , 5006, "RtcRtpHeader", "Invalid RTP header of packet for RTC") \
    XX(ERROR_RTC_RTCP                      , 5007, "RtcRtcpType", "Invalid RTCP packet type for RTC") \
    XX(ERROR_RTC_STUN                      , 5008, "RtcStun", "RTC do STUN or ICE failed") \
    XX(ERROR_RTC_DTLS                      , 5009, "RtcDtls", "RTC do DTLS handshake failed") \
    XX(ERROR_RTC_UDP                       , 5010, "RtcUdpPacket", "Invalid UDP packet for RTC") \
    XX(ERROR_RTC_RTP_MUXER                 , 5011, "RtcRtpMuxer", "Failed to mux RTP packet for RTC") \
    XX(ERROR_RTC_SDP_DECODE                , 5012, "RtcSdpDecode", "Failed to decode SDP for RTC") \
    XX(ERROR_RTC_SRTP_INIT                 , 5013, "RtcSrtpInit", "Failed to init SRTP context for RTC") \
    XX(ERROR_RTC_SRTP_PROTECT              , 5014, "RtcSrtpProtect", "Failed to crypt data by SRTP for RTC") \
    XX(ERROR_RTC_SRTP_UNPROTECT            , 5015, "RtcSrtpUnprotect", "Failed to decrypt data by SRTP for RTC") \
    XX(ERROR_RTC_RTCP_CHECK                , 5016, "RtcRtcpPacket", "Invalid RTCP packet for RTC") \
    XX(ERROR_RTC_SOURCE_CHECK              , 5017, "RtcSourceCheck", "Invalid source for RTC") \
    XX(ERROR_RTC_SDP_EXCHANGE              , 5018, "RtcSdpNegotiate", "RTC do SDP negotiate failed") \
    XX(ERROR_RTC_API_BODY                  , 5019, "RtcApiJson", "Body of RTC API should be JSON format") \
    XX(ERROR_RTC_SOURCE_BUSY               , 5020, "RtcStreamBusy", "RTC stream already exists or busy") \
    XX(ERROR_RTC_DISABLED                  , 5021, "RtcDisabled", "RTC is disabled by configuration") \
    XX(ERROR_RTC_NO_SESSION                , 5022, "RtcNoSession", "Invalid packet for no RTC session matched") \
    XX(ERROR_RTC_INVALID_PARAMS            , 5023, "RtcInvalidParams", "Invalid API parameters for RTC") \
    XX(ERROR_RTC_DUMMY_BRIDGE              , 5024, "RtcDummyBridge", "RTC dummy bridge error") \
    XX(ERROR_RTC_STREM_STARTED             , 5025, "RtcStreamStarted", "RTC stream already started") \
    XX(ERROR_RTC_TRACK_CODEC               , 5026, "RtcTrackCodec", "RTC track codec error") \
    XX(ERROR_RTC_NO_PLAYER                 , 5027, "RtcNoPlayer", "RTC player not found") \
    XX(ERROR_RTC_NO_PUBLISHER              , 5028, "RtcNoPublisher", "RTC publisher not found") \
    XX(ERROR_RTC_DUPLICATED_SSRC           , 5029, "RtcSsrcDuplicated", "Invalid RTC packet for SSRC is duplicated") \
    XX(ERROR_RTC_NO_TRACK                  , 5030, "RtcNoTrack", "Drop RTC packet for track not found") \
    XX(ERROR_RTC_RTCP_EMPTY_RR             , 5031, "RtcEmptyRr", "Invalid RTCP packet for RR is empty") \
    XX(ERROR_RTC_TCP_SIZE                  , 5032, "RtcTcpSize", "RTC TCP packet size is invalid") \
    XX(ERROR_RTC_TCP_PACKET                , 5033, "RtcTcpStun", "RTC TCP first packet must be STUN") \
    XX(ERROR_RTC_TCP_STUN                  , 5034, "RtcTcpSession", "RTC TCP packet is invalid for session not found") \
    XX(ERROR_RTC_TCP_UNIQUE                , 5035, "RtcUnique", "RTC only support one UDP or TCP network") \
    XX(ERROR_RTC_INVALID_SESSION           , 5036, "RtcInvalidSession", "Invalid request for no RTC session matched")

/**************************************************/
/* SRT protocol error. */
#define SRS_ERRNO_MAP_SRT(XX) \
    XX(ERROR_SRT_EPOLL                     , 6000, "SrtEpoll", "SRT epoll operation failed") \
    XX(ERROR_SRT_IO                        , 6001, "SrtIo", "SRT read or write failed") \
    XX(ERROR_SRT_TIMEOUT                   , 6002, "SrtTimeout", "SRT connection is timeout") \
    XX(ERROR_SRT_INTERRUPT                 , 6003, "SrtInterrupt", "SRT connection is interrupted") \
    XX(ERROR_SRT_LISTEN                    , 6004, "SrtListen", "SRT listen failed") \
    XX(ERROR_SRT_SOCKOPT                   , 6005, "SrtSetSocket", "SRT set socket option failed") \
    XX(ERROR_SRT_CONN                      , 6006, "SrtConnection", "SRT connectin level error") \
    XX(ERROR_SRT_SOURCE_BUSY               , 6007, "SrtStreamBusy", "SRT stream already exists or busy") \
    XX(ERROR_RTMP_TO_SRT                   , 6008, "SrtFromRtmp", "Covert RTMP to SRT failed") \
    XX(ERROR_SRT_STATS                     , 6009, "SrtStats", "SRT get statistic data failed") \
    XX(ERROR_SRT_TO_RTMP_EMPTY_SPS_PPS     , 6010, "SrtToRtmpEmptySpsPps", "SRT to rtmp have empty sps or pps")

/**************************************************/
/* For user-define error. */
#define SRS_ERRNO_MAP_USER(XX) \
    XX(ERROR_USER_START                    , 9000, "UserStart", "Start error code for user") \
    XX(ERROR_USER_DISCONNECT               , 9001, "UserDisconnect", "User requires to disconnect connection") \
    XX(ERROR_SOURCE_NOT_FOUND              , 9002, "UserNoSource", "Stream source not found") \
    XX(ERROR_USER_END                      , 9999, "UserEnd", "The last error code of user")

// For human readable error generation. Generate integer error code.
#define SRS_ERRNO_GEN(n, v, m, s) n = v,
enum SrsErrorCode {
#ifndef _WIN32
    ERROR_SUCCESS = 0,
#endif
    SRS_ERRNO_MAP_SYSTEM(SRS_ERRNO_GEN)
    SRS_ERRNO_MAP_RTMP(SRS_ERRNO_GEN)
    SRS_ERRNO_MAP_APP(SRS_ERRNO_GEN)
    SRS_ERRNO_MAP_HTTP(SRS_ERRNO_GEN)
    SRS_ERRNO_MAP_RTC(SRS_ERRNO_GEN)
    SRS_ERRNO_MAP_SRT(SRS_ERRNO_GEN)
    SRS_ERRNO_MAP_USER(SRS_ERRNO_GEN)
};
#undef SRS_ERRNO_GEN

// Whether the error code is an system control error.
// TODO: FIXME: Remove it from underlayer for confused with error and logger.
extern bool srs_is_system_control_error(srs_error_t err);
// It's closed by client.
extern bool srs_is_client_gracefully_close(srs_error_t err);
// It's closed by server, such as streaming EOF.
extern bool srs_is_server_gracefully_close(srs_error_t err);

// The complex error carries code, message, callstack and instant variables,
// which is more strong and easy to locate problem by log,
// please @read https://github.com/ossrs/srs/issues/913
class SrsCplxError
{
private:
    int code;
    SrsCplxError* wrapped;
    std::string msg;
    
    std::string func;
    std::string file;
    int line;
    
    SrsContextId cid;
    int rerrno;
    
    std::string desc;
    std::string _summary;
private:
    SrsCplxError();
public:
    virtual ~SrsCplxError();
private:
    virtual std::string description();
    virtual std::string summary();
public:
    static SrsCplxError* create(const char* func, const char* file, int line, int code, const char* fmt, ...);
    static SrsCplxError* wrap(const char* func, const char* file, int line, SrsCplxError* err, const char* fmt, ...);
    static SrsCplxError* success();
    static SrsCplxError* copy(SrsCplxError* from);
    static std::string description(SrsCplxError* err);
    static std::string summary(SrsCplxError* err);
    static int error_code(SrsCplxError* err);
    static std::string error_code_str(SrsCplxError* err);
    static std::string error_code_longstr(SrsCplxError* err);
public:
    static void srs_assert(bool expression);
};

// Error helpers, should use these functions to new or wrap an error.
#define srs_success 0 // SrsCplxError::success()
#define srs_error_new(ret, fmt, ...) SrsCplxError::create(__FUNCTION__, __FILE__, __LINE__, ret, fmt, ##__VA_ARGS__)
#define srs_error_wrap(err, fmt, ...) SrsCplxError::wrap(__FUNCTION__, __FILE__, __LINE__, err, fmt, ##__VA_ARGS__)
#define srs_error_copy(err) SrsCplxError::copy(err)
#define srs_error_desc(err) SrsCplxError::description(err)
#define srs_error_summary(err) SrsCplxError::summary(err)
#define srs_error_code(err) SrsCplxError::error_code(err)
#define srs_error_code_str(err) SrsCplxError::error_code_str(err)
#define srs_error_code_longstr(err) SrsCplxError::error_code_longstr(err)
#define srs_error_reset(err) srs_freep(err); err = srs_success

#ifndef srs_assert
#define srs_assert(expression) SrsCplxError::srs_assert(expression)
#endif

#endif

