GCC = gcc

ifeq ($(HANDSHAKE),)
    ST_ALL      = help
else
    ST_ALL      = objs/srs_flv_parser \
                objs/srs_flv_injecter objs/srs_publish objs/srs_play \
                objs/srs_ingest_flv objs/srs_ingest_rtmp objs/srs_detect_rtmp \
                objs/srs_bandwidth_check objs/srs_h264_raw_publish
endif

.PHONY: default clean help ssl nossl

default: $(ST_ALL)

help:
	@echo "Usage: make <help>|<clean>|<ssl>|<nossl>"
	@echo "     help                    display this help"
	@echo "     clean                   cleanup build"
	@echo "     ssl                     all tools link ssl"
	@echo "     nossl                   all tools never link ssl"
	@echo "ssl/nossl will build the following tools:"
	@echo "     srs_flv_parser          parse flv file, print detail info."
	@echo "     srs_flv_injecter        inject keyframes information to metadata."
	@echo "     srs_publish             publish program using srs-librtmp"
	@echo "     srs_h264_raw_publish    publish raw h.264 stream to SSR by srs-librtmp"
	@echo "     srs_play                play program using srs-librtmp"
	@echo "     srs_ingest_flv          ingest flv file and publish to RTMP server."
	@echo "     srs_ingest_rtmp         ingest RTMP and publish to RTMP server."
	@echo "     srs_detect_rtmp         detect RTMP stream info."
	@echo "     srs_bandwidth_check     bandwidth check/test tool."
	@echo "Remark: about simple/complex handshake, see: http://blog.csdn.net/win_lin/article/details/13006803"
	@echo "Remark: srs Makefile will auto invoke this by --with/without-ssl, "
	@echo "     that is, if user specified ssl(by --with-ssl), srs will make this by 'make ssl'"
	@echo "     that is, if user not use ssl(by --without-ssl), use 'make nossl'"
	@echo "     see: https://github.com/winlinvip/simple-rtmp-server/wiki/v1_CN_v1_Build"
	@echo "Remark: before make this sample, user must make the srs, with/without ssl"
    
clean:
	@rm -rf objs

# srs library root
SRS_OBJS = ../../objs
# srs-librtmp for publish/play, built by srs.
SRS_LIBRTMP_I = $(SRS_OBJS)/include/srs_librtmp.h
SRS_LIBRTMP_L = $(SRS_OBJS)/lib/srs_librtmp.a
# openssl for complex handshake, built by srs.
SRS_LIBSSL_L =
# public depends, the Makefile or public headers.
SRS_RESEARCH_DEPS = Makefile

# for x86/x64 platform
ifeq ($(GCC), gcc)
    EXTRA_CXX_FLAG = -g -O0 -ldl -lstdc++
endif
# for arm.
ifeq ($(GCC), arm-linux-gnueabi-gcc)
    EXTRA_CXX_FLAG = -g -O0 -ldl -static -lstdc++
endif
# for mips, add -lgcc_eh, or stl compile failed.
ifeq ($(GCC), mipsel-openwrt-linux-gcc)
    EXTRA_CXX_FLAG = -g -O0 -ldl -lstdc++ -lgcc_eh
endif
# for ssl or nossl
ifeq ($(HANDSHAKE), SSL)
    SRS_LIBSSL_L = $(SRS_OBJS)/openssl/lib/libssl.a $(SRS_OBJS)/openssl/lib/libcrypto.a
endif

ssl:
	@mkdir -p objs
	$(MAKE) HANDSHAKE="SSL"
nossl:
	@mkdir -p objs
	$(MAKE) HANDSHAKE="NOSSL"

objs/srs_flv_parser: srs_flv_parser.c $(SRS_RESEARCH_DEPS) $(SRS_LIBRTMP_I) $(SRS_LIBRTMP_L)
	$(GCC) srs_flv_parser.c $(SRS_LIBRTMP_L) $(SRS_LIBSSL_L) $(EXTRA_CXX_FLAG) -o objs/srs_flv_parser

objs/srs_flv_injecter: srs_flv_injecter.c $(SRS_RESEARCH_DEPS) $(SRS_LIBRTMP_I) $(SRS_LIBRTMP_L)
	$(GCC) srs_flv_injecter.c $(SRS_LIBRTMP_L) $(SRS_LIBSSL_L) $(EXTRA_CXX_FLAG) -o objs/srs_flv_injecter

objs/srs_publish: srs_publish.c $(SRS_RESEARCH_DEPS) $(SRS_LIBRTMP_I) $(SRS_LIBRTMP_L) $(SRS_LIBSSL_L)
	$(GCC) srs_publish.c $(SRS_LIBRTMP_L) $(SRS_LIBSSL_L) $(EXTRA_CXX_FLAG) -o objs/srs_publish

objs/srs_h264_raw_publish: srs_h264_raw_publish.c $(SRS_RESEARCH_DEPS) $(SRS_LIBRTMP_I) $(SRS_LIBRTMP_L) $(SRS_LIBSSL_L)
	$(GCC) srs_h264_raw_publish.c $(SRS_LIBRTMP_L) $(SRS_LIBSSL_L) $(EXTRA_CXX_FLAG) -o objs/srs_h264_raw_publish

objs/srs_play: srs_play.c $(SRS_RESEARCH_DEPS) $(SRS_LIBRTMP_I) $(SRS_LIBRTMP_L) $(SRS_LIBSSL_L)
	$(GCC) srs_play.c $(SRS_LIBRTMP_L) $(SRS_LIBSSL_L) $(EXTRA_CXX_FLAG) -o objs/srs_play

objs/srs_ingest_flv: srs_ingest_flv.c $(SRS_RESEARCH_DEPS) $(SRS_LIBRTMP_I) $(SRS_LIBRTMP_L) $(SRS_LIBSSL_L)
	$(GCC) srs_ingest_flv.c $(SRS_LIBRTMP_L) $(SRS_LIBSSL_L) $(EXTRA_CXX_FLAG) -o objs/srs_ingest_flv

objs/srs_ingest_rtmp: srs_ingest_rtmp.c $(SRS_RESEARCH_DEPS) $(SRS_LIBRTMP_I) $(SRS_LIBRTMP_L) $(SRS_LIBSSL_L)
	$(GCC) srs_ingest_rtmp.c $(SRS_LIBRTMP_L) $(SRS_LIBSSL_L) $(EXTRA_CXX_FLAG) -o objs/srs_ingest_rtmp

objs/srs_detect_rtmp: srs_detect_rtmp.c $(SRS_RESEARCH_DEPS) $(SRS_LIBRTMP_I) $(SRS_LIBRTMP_L) $(SRS_LIBSSL_L)
	$(GCC) srs_detect_rtmp.c $(SRS_LIBRTMP_L) $(SRS_LIBSSL_L) $(EXTRA_CXX_FLAG) -o objs/srs_detect_rtmp

objs/srs_bandwidth_check: srs_bandwidth_check.c $(SRS_RESEARCH_DEPS) $(SRS_LIBRTMP_I) $(SRS_LIBRTMP_L) $(SRS_LIBSSL_L)
	$(GCC) srs_bandwidth_check.c $(SRS_LIBRTMP_L) $(SRS_LIBSSL_L) $(EXTRA_CXX_FLAG) -o objs/srs_bandwidth_check
