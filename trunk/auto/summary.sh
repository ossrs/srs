#!/bin/bash

# colorful summary
SrsHlsSummaryColor="\${YELLOW}{disabled} "; if [ $SRS_HLS = YES ]; then SrsHlsSummaryColor="\${GREEN}"; fi
SrsDvrSummaryColor="\${YELLOW}{disabled} "; if [ $SRS_DVR = YES ]; then SrsDvrSummaryColor="\${GREEN}"; fi
SrsNginxSummaryColor="\${GREEN}{disabled} "; if [ $SRS_NGINX = YES ]; then SrsNginxSummaryColor="\${GREEN}"; fi
SrsSslSummaryColor="\${YELLOW}{disabled} "; if [ $SRS_SSL = YES ]; then SrsSslSummaryColor="\${GREEN}"; fi
SrsFfmpegSummaryColor="\${YELLOW}{disabled} "; if [ $SRS_FFMPEG_TOOL = YES ]; then SrsFfmpegSummaryColor="\${GREEN}"; fi
SrsTranscodeSummaryColor="\${YELLOW}{disabled} "; if [ $SRS_TRANSCODE = YES ]; then SrsTranscodeSummaryColor="\${GREEN}"; fi
SrsIngestSummaryColor="\${YELLOW}{disabled} "; if [ $SRS_INGEST = YES ]; then SrsIngestSummaryColor="\${GREEN}"; fi
SrsHttpCallbackSummaryColor="\${YELLOW}{disabled} "; if [ $SRS_HTTP_CALLBACK = YES ]; then SrsHttpCallbackSummaryColor="\${GREEN}"; fi
SrsHttpServerSummaryColor="\${YELLOW}{disabled} "; if [ $SRS_HTTP_SERVER = YES ]; then SrsHttpServerSummaryColor="\${GREEN}"; fi
SrsHttpApiSummaryColor="\${YELLOW}{disabled} "; if [ $SRS_HTTP_API = YES ]; then SrsHttpApiSummaryColor="\${GREEN}"; fi
SrsStreamCasterSummaryColor="\${YELLOW}{disabled} "; if [ $SRS_STREAM_CASTER = YES ]; then SrsStreamCasterSummaryColor="\${GREEN}"; fi
SrsKafkaSummaryColor="\${YELLOW}{disabled} "; if [ $SRS_KAFKA = YES ]; then SrsKafkaSummaryColor="\${GREEN}"; fi
SrsLibrtmpSummaryColor="\${YELLOW}{disabled} "; if [ $SRS_LIBRTMP = YES ]; then SrsLibrtmpSummaryColor="\${GREEN}"; fi
SrsLibrtmpSSLSummaryColor="\${YELLOW}{disabled} "; if [ $SRS_LIBRTMP = YES ]; then if [ $SRS_SSL = YES ]; then SrsLibrtmpSSLSummaryColor="\${GREEN}"; fi fi
SrsResearchSummaryColor="\${GREEN}{disabled} "; if [ $SRS_RESEARCH = YES ]; then SrsResearchSummaryColor="\${GREEN}"; fi
SrsUtestSummaryColor="\${YELLOW}{disabled} "; if [ $SRS_UTEST = YES ]; then SrsUtestSummaryColor="\${GREEN}"; fi
SrsGperfSummaryColor="\${GREEN}{disabled} "; if [ $SRS_GPERF = YES ]; then SrsGperfSummaryColor="\${GREEN}"; fi
SrsGperfMCSummaryColor="\${GREEN}{disabled} "; if [ $SRS_GPERF_MC = YES ]; then SrsGperfMCSummaryColor="\${YELLOW}"; fi
SrsGperfMDSummaryColor="\${GREEN}{disabled} "; if [ $SRS_GPERF_MD = YES ]; then SrsGperfMDSummaryColor="\${YELLOW}"; fi
SrsGperfMPSummaryColor="\${GREEN}{disabled} "; if [ $SRS_GPERF_MP = YES ]; then SrsGperfMPSummaryColor="\${YELLOW}"; fi
SrsGperfCPSummaryColor="\${GREEN}{disabled} "; if [ $SRS_GPERF_CP = YES ]; then SrsGperfCPSummaryColor="\${YELLOW}"; fi
SrsGprofSummaryColor="\${GREEN}{disabled} "; if [ $SRS_GPROF = YES ]; then SrsGprofSummaryColor="\${YELLOW}"; fi

if [ $SRS_EXPORT_LIBRTMP_PROJECT = NO ]; then
    cat <<END > ${SRS_OBJS}/${SRS_BUILD_SUMMARY}
#!/bin/bash

#####################################################################################
# linux shell color support.
RED="\\${RED}"
GREEN="\\${GREEN}"
YELLOW="\\${YELLOW}"
BLACK="\\${BLACK}"

echo -e "\${GREEN}build summary:\${BLACK}"
echo -e "     \${BLACK}+------------------------------------------------------------------------------------\${BLACK}"
echo -e "     |${SrsGperfSummaryColor}gperf @see: https://github.com/ossrs/srs/wiki/v1_CN_GPERF\${BLACK}"
echo -e "     |     ${SrsGperfMDSummaryColor}gmd @see: http://blog.csdn.net/win_lin/article/details/50461709\${BLACK}"
echo -e "     |     ${SrsGperfMDSummaryColor}gmd: gperf memory defense, or memory corrupt detect\${BLACK}"
echo -e "     |             ${SrsGperfMDSummaryColor}env TCMALLOC_PAGE_FENCE=1 ./objs/srs -c conf/console.conf\${BLACK}"
echo -e "     |     ${SrsGperfMCSummaryColor}gmc @see: http://google-perftools.googlecode.com/svn/trunk/doc/heap_checker.html\${BLACK}"
echo -e "     |     ${SrsGperfMCSummaryColor}gmc: gperf memory check, or memory leak detect\${BLACK}"
echo -e "     |             ${SrsGperfMCSummaryColor}env PPROF_PATH=./objs/pprof HEAPCHECK=normal ./objs/srs -c conf/console.conf 2>gmc.log # start gmc\${BLACK}"
echo -e "     |             ${SrsGperfMCSummaryColor}killall -2 srs # or CTRL+C to stop gmc\${BLACK}"
echo -e "     |             ${SrsGperfMCSummaryColor}cat gmc.log # to analysis memory leak\${BLACK}"
echo -e "     |     ${SrsGperfMPSummaryColor}gmp @see: http://google-perftools.googlecode.com/svn/trunk/doc/heapprofile.html\${BLACK}"
echo -e "     |     ${SrsGperfMPSummaryColor}gmp: gperf memory profile, similar to gcp\${BLACK}"
echo -e "     |             ${SrsGperfMPSummaryColor}rm -f gperf.srs.gmp*; ./objs/srs -c conf/console.conf # start gmp\${BLACK}"
echo -e "     |             ${SrsGperfMPSummaryColor}killall -2 srs # or CTRL+C to stop gmp\${BLACK}"
echo -e "     |             ${SrsGperfMPSummaryColor}./objs/pprof --text objs/srs gperf.srs.gmp* # to analysis memory profile\${BLACK}"
echo -e "     |     ${SrsGperfCPSummaryColor}gcp @see: http://google-perftools.googlecode.com/svn/trunk/doc/cpuprofile.html\${BLACK}"
echo -e "     |     ${SrsGperfCPSummaryColor}gcp: gperf cpu profile\${BLACK}"
echo -e "     |             ${SrsGperfCPSummaryColor}rm -f gperf.srs.gcp*; ./objs/srs -c conf/console.conf # start gcp\${BLACK}"
echo -e "     |             ${SrsGperfCPSummaryColor}killall -2 srs # or CTRL+C to stop gcp\${BLACK}"
echo -e "     |             ${SrsGperfCPSummaryColor}./objs/pprof --text objs/srs gperf.srs.gcp* # to analysis cpu profile\${BLACK}"
echo -e "     \${BLACK}+------------------------------------------------------------------------------------\${BLACK}"
echo -e "     |${SrsGprofSummaryColor}gprof @see: https://github.com/ossrs/srs/wiki/v1_CN_GPROF\${BLACK}"
echo -e "     |${SrsGprofSummaryColor}gprof: GNU profile tool, @see: http://www.cs.utah.edu/dept/old/texinfo/as/gprof.html\${BLACK}"
echo -e "     |     ${SrsGprofSummaryColor}rm -f gmon.out; ./objs/srs -c conf/console.conf # start gprof\${BLACK}"
echo -e "     |     ${SrsGprofSummaryColor}killall -2 srs # or CTRL+C to stop gprof\${BLACK}"
echo -e "     |     ${SrsGprofSummaryColor}gprof -b ./objs/srs gmon.out > gprof.srs.log && rm -f gmon.out # gprof report to gprof.srs.log\${BLACK}"
echo -e "     \${BLACK}+------------------------------------------------------------------------------------\${BLACK}"
echo -e "     |${SrsUtestSummaryColor}utest: ./objs/srs_utest, the utest for srs\${BLACK}"
echo -e "     \${BLACK}+------------------------------------------------------------------------------------\${BLACK}"
echo -e "     |${SrsLibrtmpSummaryColor}librtmp @see: https://github.com/ossrs/srs/wiki/v1_CN_SrsLibrtmp\${BLACK}"
echo -e "     |${SrsLibrtmpSummaryColor}librtmp: ./objs/include, ./objs/lib, the srs-librtmp library\${BLACK}"
echo -e "     |     ${SrsLibrtmpSummaryColor}simple handshake: publish/play stream with simple handshake to server\${BLACK}"
echo -e "     |     ${SrsLibrtmpSSLSummaryColor}complex handshake: it's not required for client, recommend disable it\${BLACK}"
echo -e "     |     ${SrsLibrtmpSummaryColor}librtmp-sample: ./research/librtmp, the srs-librtmp client sample\${BLACK}"
echo -e "     |     ${SrsLibrtmpSummaryColor}librtmp-sample: ./research/librtmp/objs/srs_ingest_flv\${BLACK}"
echo -e "     |     ${SrsLibrtmpSummaryColor}librtmp-sample: ./research/librtmp/objs/srs_ingest_rtmp\${BLACK}"
echo -e "     |     ${SrsLibrtmpSummaryColor}librtmp-sample: ./research/librtmp/objs/srs_detect_rtmp\${BLACK}"
echo -e "     |     ${SrsLibrtmpSummaryColor}librtmp-sample: ./research/librtmp/objs/srs_bandwidth_check\${BLACK}"
echo -e "     \${BLACK}+------------------------------------------------------------------------------------\${BLACK}"
echo -e "     |${SrsResearchSummaryColor}research: ./objs/research, api server, players, ts info, librtmp.\${BLACK}"
echo -e "     |     ${SrsResearchSummaryColor} @see https://github.com/ossrs/srs/wiki/v2_CN_SrsLibrtmp#srs-librtmp-examples\${BLACK}"
echo -e "     \${BLACK}+------------------------------------------------------------------------------------\${BLACK}"
echo -e "     |\${GREEN}tools: important tool, others @see https://github.com/ossrs/srs/wiki/v2_CN_SrsLibrtmp#srs-librtmp-examples\${BLACK}"
echo -e "     |     \${GREEN}./objs/srs_ingest_hls -i http://ossrs.net/live/livestream.m3u8 -y rtmp://127.0.0.1/live/livestream\${BLACK}"
echo -e "     \${BLACK}+------------------------------------------------------------------------------------\${BLACK}"
echo -e "     |\${GREEN}server: ./objs/srs -c conf/srs.conf, start the srs server\${BLACK}"
echo -e "     |     ${SrsHlsSummaryColor}hls @see: https://github.com/ossrs/srs/wiki/v2_CN_DeliveryHLS\${BLACK}"
echo -e "     |     ${SrsHlsSummaryColor}hls: generate m3u8 and ts from rtmp stream\${BLACK}"
echo -e "     |     ${SrsDvrSummaryColor}dvr @see: https://github.com/ossrs/srs/wiki/v2_CN_DVR\${BLACK}"
echo -e "     |     ${SrsDvrSummaryColor}dvr: record RTMP stream to flv files.\${BLACK}"
echo -e "     |     ${SrsNginxSummaryColor}nginx @see: https://github.com/ossrs/srs/wiki/v2_CN_DeliveryHLS\${BLACK}"
echo -e "     |     ${SrsNginxSummaryColor}nginx: delivery HLS stream by nginx\${BLACK}"
echo -e "     |     ${SrsNginxSummaryColor}nginx: sudo ./objs/nginx/sbin/nginx\${BLACK}"
echo -e "     |     ${SrsSslSummaryColor}ssl @see: https://github.com/ossrs/srs/wiki/v1_CN_RTMPHandshake\${BLACK}"
echo -e "     |     ${SrsSslSummaryColor}ssl: support RTMP complex handshake for client required, for instance, flash\${BLACK}"
echo -e "     |     ${SrsFfmpegSummaryColor}ffmpeg @see: https://github.com/ossrs/srs/wiki/v1_CN_FFMPEG\${BLACK}"
echo -e "     |     ${SrsFfmpegSummaryColor}ffmpeg: transcode, mux, ingest tool\${BLACK}"
echo -e "     |     ${SrsFfmpegSummaryColor}ffmpeg: ./objs/ffmpeg/bin/ffmpeg\${BLACK}"
echo -e "     |     ${SrsTranscodeSummaryColor}transcode @see: https://github.com/ossrs/srs/wiki/v1_CN_FFMPEG\${BLACK}"
echo -e "     |     ${SrsTranscodeSummaryColor}transcode: support transcoding RTMP stream\${BLACK}"
echo -e "     |     ${SrsIngestSummaryColor}ingest @see: https://github.com/ossrs/srs/wiki/v1_CN_Ingest\${BLACK}"
echo -e "     |     ${SrsIngestSummaryColor}ingest: support ingest file/stream/device then push to SRS by RTMP stream\${BLACK}"
echo -e "     |     ${SrsHttpCallbackSummaryColor}http-callback @see: https://github.com/ossrs/srs/wiki/v2_CN_HTTPCallback\${BLACK}"
echo -e "     |     ${SrsHttpCallbackSummaryColor}http-callback: support http callback for authentication and event injection\${BLACK}"
echo -e "     |     ${SrsHttpServerSummaryColor}http-server @see: https://github.com/ossrs/srs/wiki/v2_CN_HTTPServer\${BLACK}"
echo -e "     |     ${SrsHttpServerSummaryColor}http-server: support http server to delivery http stream\${BLACK}"
echo -e "     |     ${SrsHttpApiSummaryColor}http-api @see: https://github.com/ossrs/srs/wiki/v2_CN_HTTPApi\${BLACK}"
echo -e "     |     ${SrsHttpApiSummaryColor}http-api: support http api to manage server\${BLACK}"
echo -e "     |     ${SrsStreamCasterSummaryColor}stream-caster @see: https://github.com/ossrs/srs/wiki/v2_CN_Streamer\${BLACK}"
echo -e "     |     ${SrsStreamCasterSummaryColor}stream-caster: start server to cast stream over other protocols.\${BLACK}"
echo -e "     |     ${SrsKafkaSummaryColor}kafka @see: https://github.com/ossrs/srs/wiki/v3_CN_Kafka\${BLACK}"
echo -e "     |     ${SrsKafkaSummaryColor}kafka: start srs kafka producer to report to kafka.\${BLACK}"
echo -e "     \${BLACK}+------------------------------------------------------------------------------------\${BLACK}"
echo -e "\${GREEN}binaries @see: https://github.com/ossrs/srs/wiki/v2_CN_Build\${BLACK}"

echo "You can:"
echo "      ./objs/srs -c conf/srs.conf"
echo "                  to start the srs server, with config conf/srs.conf."
END
else
    cat <<END > ${SRS_OBJS}/${SRS_BUILD_SUMMARY}
#!/bin/bash

#####################################################################################
# linux shell color support.
RED="\\${RED}"
GREEN="\\${GREEN}"
YELLOW="\\${YELLOW}"
BLACK="\\${BLACK}"

echo -e "\${BLACK}You can use srs-librtmp at:\${BLACK}"
echo -e "\${GREEN}      objs/include/srs_librtmp.h\${BLACK}"
echo -e "\${GREEN}      objs/lib/srs_librtmp.a\${BLACK}"
echo -e "\${BLACK}Examples for srs-librtmp at:\${BLACK}"
echo -e "\${GREEN}      objs/research/librtmp\${BLACK}"
echo -e "\${GREEN}      Examples: https://github.com/ossrs/srs/wiki/v2_CN_SrsLibrtmp#srs-librtmp-examples\${BLACK}"
END
fi
