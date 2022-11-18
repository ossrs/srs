#!/bin/bash

# colorful summary
SrsHlsSummaryColor="\${YELLOW}(Disabled) "; if [[ $SRS_HLS == YES ]]; then SrsHlsSummaryColor="\${GREEN}"; fi
SrsDvrSummaryColor="\${YELLOW}(Disabled) "; if [[ $SRS_DVR == YES ]]; then SrsDvrSummaryColor="\${GREEN}"; fi
SrsNginxSummaryColor="\${GREEN}(Disabled) "; if [[ $SRS_NGINX == YES ]]; then SrsNginxSummaryColor="\${GREEN}"; fi
SrsSslSummaryColor="\${YELLOW}(Disabled) "; if [[ $SRS_SSL == YES ]]; then SrsSslSummaryColor="\${GREEN}"; fi
SrsFfmpegSummaryColor="\${YELLOW}(Disabled) "; if [[ $SRS_FFMPEG_TOOL == YES ]]; then SrsFfmpegSummaryColor="\${GREEN}"; fi
SrsTranscodeSummaryColor="\${YELLOW}(Disabled) "; if [[ $SRS_TRANSCODE == YES ]]; then SrsTranscodeSummaryColor="\${GREEN}"; fi
SrsIngestSummaryColor="\${YELLOW}(Disabled) "; if [[ $SRS_INGEST == YES ]]; then SrsIngestSummaryColor="\${GREEN}"; fi
SrsHttpCallbackSummaryColor="\${YELLOW}(Disabled) "; if [[ $SRS_HTTP_CALLBACK == YES ]]; then SrsHttpCallbackSummaryColor="\${GREEN}"; fi
SrsHttpServerSummaryColor="\${YELLOW}(Disabled) "; if [[ $SRS_HTTP_SERVER == YES ]]; then SrsHttpServerSummaryColor="\${GREEN}"; fi
SrsHttpApiSummaryColor="\${YELLOW}(Disabled) "; if [[ $SRS_HTTP_API == YES ]]; then SrsHttpApiSummaryColor="\${GREEN}"; fi
SrsStreamConverterSummaryColor="\${YELLOW}(Disabled) "; if [[ $SRS_STREAM_CASTER == YES ]]; then SrsStreamConverterSummaryColor="\${GREEN}"; fi
SrsUtestSummaryColor="\${YELLOW}(Disabled) "; if [[ $SRS_UTEST == YES ]]; then SrsUtestSummaryColor="\${GREEN}"; fi
SrsGperfSummaryColor="\${GREEN}(Disabled) "; if [[ $SRS_GPERF == YES ]]; then SrsGperfSummaryColor="\${GREEN}"; fi
SrsGperfMCSummaryColor="\${GREEN}(Disabled) "; if [[ $SRS_GPERF_MC == YES ]]; then SrsGperfMCSummaryColor="\${YELLOW}"; fi
SrsGperfMDSummaryColor="\${GREEN}(Disabled) "; if [[ $SRS_GPERF_MD == YES ]]; then SrsGperfMDSummaryColor="\${YELLOW}"; fi
SrsGperfMPSummaryColor="\${GREEN}(Disabled) "; if [[ $SRS_GPERF_MP == YES ]]; then SrsGperfMPSummaryColor="\${YELLOW}"; fi
SrsGperfCPSummaryColor="\${GREEN}(Disabled) "; if [[ $SRS_GPERF_CP == YES ]]; then SrsGperfCPSummaryColor="\${YELLOW}"; fi
SrsGprofSummaryColor="\${GREEN}(Disabled) "; if [[ $SRS_GPROF == YES ]]; then SrsGprofSummaryColor="\${YELLOW}"; fi
SrsValgrindSummaryColor="\${YELLOW}(Disabled) "; if [[ $SRS_VALGRIND == YES ]]; then SrsValgrindSummaryColor="\${GREEN}"; fi

cat <<END > ${SRS_OBJS}/${SRS_BUILD_SUMMARY}
#!/bin/bash

#####################################################################################
# linux shell color support.
RED="\\${RED}"
GREEN="\\${GREEN}"
YELLOW="\\${YELLOW}"
BLACK="\\${BLACK}"

echo -e "\${GREEN}The build summary:\${BLACK}"
echo -e "     \${BLACK}+------------------------------------------------------------------------------------\${BLACK}"
echo -e "     \${GREEN}For SRS benchmark, gperf, gprof and valgrind, please read:\${BLACK}"
echo -e "     \${GREEN}     https://www.jianshu.com/p/6d4a89359352\${BLACK}"
echo -e "     \${BLACK}+------------------------------------------------------------------------------------\${BLACK}"
echo -e "     |\${GREEN}The main server usage: ./objs/srs -c conf/srs.conf, start the srs server\${BLACK}"
echo -e "     |     ${SrsHlsSummaryColor}About HLS, please read https://ossrs.net/lts/zh-cn/docs/v4/doc/delivery-hls\${BLACK}"
echo -e "     |     ${SrsDvrSummaryColor}About DVR, please read https://ossrs.net/lts/zh-cn/docs/v4/doc/dvr\${BLACK}"
echo -e "     |     ${SrsSslSummaryColor}About SSL, please read https://ossrs.net/lts/zh-cn/docs/v4/doc/rtmp-handshake\${BLACK}"
echo -e "     |     ${SrsTranscodeSummaryColor}About transcoding, please read https://ossrs.net/lts/zh-cn/docs/v4/doc/ffmpeg\${BLACK}"
echo -e "     |     ${SrsIngestSummaryColor}About ingester, please read https://ossrs.net/lts/zh-cn/docs/v4/doc/ingest\${BLACK}"
echo -e "     |     ${SrsHttpCallbackSummaryColor}About http-callback, please read https://ossrs.net/lts/zh-cn/docs/v4/doc/http-callback\${BLACK}"
echo -e "     |     ${SrsHttpServerSummaryColor}Aoubt http-server, please read https://ossrs.net/lts/zh-cn/docs/v4/doc/http-server\${BLACK}"
echo -e "     |     ${SrsHttpApiSummaryColor}About http-api, please read https://ossrs.net/lts/zh-cn/docs/v4/doc/http-api\${BLACK}"
echo -e "     |     ${SrsStreamConverterSummaryColor}About stream-caster, please read https://ossrs.net/lts/zh-cn/docs/v4/doc/streamer\${BLACK}"
echo -e "     |     ${SrsValgrindSummaryColor}About VALGRIND, please read https://github.com/ossrs/state-threads/issues/2\${BLACK}"
echo -e "     \${BLACK}+------------------------------------------------------------------------------------\${BLACK}"
echo -e "\${GREEN}binaries, please read https://ossrs.net/lts/zh-cn/docs/v4/doc/install\${BLACK}"

echo "You can:"
echo "      ./objs/srs -c conf/srs.conf"
echo "                  to start the srs server, with config conf/srs.conf."
END

