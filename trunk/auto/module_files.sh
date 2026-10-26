# The source files of each SRS module, as basenames of the .cpp and .hpp in the module's directory.
# ./configure sources this file, and the Windows and IDE CMake builds read it, so all builds compile
# the same files, in the order listed.
#
# SRS_<MODULE>_FILES always build. SRS_<MODULE>_<FEATURE>_FILES build only when the feature is on,
# such as SRS_APP_RTSP_FILES for --rtsp=on, and come after the module's other files.

SRS_CORE_FILES=(srs_core srs_core_version srs_core_version8 srs_core_autofree srs_core_time
    srs_core_platform srs_core_deprecated srs_core_performance)

SRS_KERNEL_FILES=(srs_kernel_error srs_kernel_log srs_kernel_buffer srs_kernel_utility
    srs_kernel_flv srs_kernel_codec srs_kernel_io srs_kernel_consts srs_kernel_aac srs_kernel_mp3
    srs_kernel_ts srs_kernel_stream srs_kernel_balance srs_kernel_mp4 srs_kernel_file
    srs_kernel_kbps srs_kernel_rtc_rtp srs_kernel_rtc_rtcp srs_kernel_packet srs_kernel_st
    srs_kernel_factory srs_kernel_hourglass srs_kernel_ps srs_kernel_pithy_print
    srs_kernel_rtc_queue srs_kernel_resource)

SRS_PROTOCOL_FILES=(srs_protocol_amf0 srs_protocol_io srs_protocol_conn srs_protocol_rtmp_handshake
    srs_protocol_rtmp_stack srs_protocol_utility srs_protocol_rtmp_msg_array srs_protocol_raw_avc
    srs_protocol_http_stack srs_protocol_json srs_protocol_stream srs_protocol_format
    srs_protocol_log srs_protocol_st srs_protocol_http_client srs_protocol_http_conn
    srs_protocol_rtmp_conn srs_protocol_protobuf srs_protocol_http_stack_llhttp
    srs_protocol_http_stack_llhttpapi srs_protocol_http_stack_llhttpadapter
    srs_protocol_http_stack_llhttphttp srs_protocol_srt srs_protocol_rtc_stun srs_protocol_rtp
    srs_protocol_sdp)
SRS_PROTOCOL_RTSP_FILES=(srs_protocol_rtsp_stack)

SRS_APP_FILES=(srs_app_server srs_app_rtmp_conn srs_app_rtmp_source srs_app_hls srs_app_forward
    srs_app_encoder srs_app_st srs_app_log srs_app_config srs_app_stream_bridge srs_app_reload
    srs_app_http_api srs_app_http_conn srs_app_http_hooks srs_app_ingest srs_app_ffmpeg
    srs_app_utility srs_app_edge srs_app_heartbeat srs_app_http_client srs_app_http_static
    srs_app_recv_thread srs_app_security srs_app_statistic srs_app_mpegts_udp srs_app_listener
    srs_app_async_call srs_app_process srs_app_dash srs_app_fragment srs_app_dvr srs_app_ng_exec
    srs_app_coworkers srs_app_circuit_breaker srs_app_factory srs_app_stream_token
    srs_app_http_stream srs_app_srt_server srs_app_srt_listener srs_app_srt_conn srs_app_srt_source
    srs_app_rtc_conn srs_app_rtc_dtls srs_app_rtc_network srs_app_rtc_server srs_app_rtc_source
    srs_app_rtc_api)
SRS_APP_RTSP_FILES=(srs_app_rtsp_source srs_app_rtsp_conn)
SRS_APP_FFMPEG_FIT_FILES=(srs_app_rtc_codec)
SRS_APP_GB28181_FILES=(srs_app_gb28181)

SRS_SERVER_FILES=(srs_main_server)

SRS_UTEST_FILES=(srs_utest srs_utest_manual_amf0 srs_utest_manual_kernel srs_utest_manual_core
    srs_utest_manual_srt srs_utest_manual_config srs_utest_manual_rtmp srs_utest_manual_http
    srs_utest_manual_avc srs_utest_manual_reload srs_utest_manual_mp4 srs_utest_manual_service
    srs_utest_manual_app_rtc2rtmp srs_utest_manual_rtc srs_utest_manual_config2
    srs_utest_manual_protocol srs_utest_manual_protocol2 srs_utest_manual_kernel2
    srs_utest_manual_st srs_utest_manual_fmp4 srs_utest_manual_source_lock
    srs_utest_manual_stream_token srs_utest_manual_rtc_recv_track srs_utest_manual_st2
    srs_utest_manual_hevc_structs srs_utest_manual_coworkers srs_utest_manual_pithy_print
    srs_utest_manual_protocol3 srs_utest_manual_app srs_utest_manual_mock
    srs_utest_workflow_rtc_playstream srs_utest_workflow_rtc_publishstream
    srs_utest_workflow_rtc_conn srs_utest_workflow_rtmp_conn srs_utest_workflow_srt_conn
    srs_utest_workflow_http_conn srs_utest_workflow_forward srs_utest_workflow_rtc2rtmp
    srs_utest_workflow_rtmp2rtc srs_utest_ai01 srs_utest_ai02 srs_utest_ai03 srs_utest_ai04
    srs_utest_ai05 srs_utest_ai06 srs_utest_ai07 srs_utest_ai08 srs_utest_ai09 srs_utest_ai10
    srs_utest_ai11 srs_utest_ai12 srs_utest_ai13 srs_utest_ai14 srs_utest_ai15 srs_utest_ai16
    srs_utest_ai17 srs_utest_ai18 srs_utest_ai19 srs_utest_ai20 srs_utest_ai24 srs_utest_ai25
    srs_utest_ai26 srs_utest_ai27 srs_utest_ai28 srs_utest_ai29 srs_utest_ai30 srs_utest_ai31
    srs_utest_ai32 srs_utest_ai33 srs_utest_ai34 srs_utest_ai35 srs_utest_ai36 srs_utest_ai37
    srs_utest_ai38 srs_utest_ai39 srs_utest_ai40)
SRS_UTEST_GB28181_FILES=(srs_utest_manual_gb28181 srs_utest_ai23)
SRS_UTEST_RTSP_FILES=(srs_utest_ai21 srs_utest_ai22)
