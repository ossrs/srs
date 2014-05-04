GCC = gcc

.PHONY: default clean help ssl nossl

default: help

help:
	@echo "Usage: make <help>|<clean>|<srs_publish_nossl>|<srs_play_nossl>|<srs_ingest_flv_nossl>|<srs_ingest_rtmp_nossl>|<srs_publish_ssl>|<srs_play_ssl>|<srs_ingest_flv_ssl>|<srs_ingest_rtmp_ssl>"
	@echo "     help                    display this help"
	@echo "     clean                   cleanup build"
	@echo "     ssl                     srs_publish_ssl, srs_play_ssl, srs_ingest_flv, srs_ingest_rtmp"
	@echo "     nossl                   srs_publish_nossl, srs_play_nossl, srs_ingest_flv, srs_ingest_rtmp"
	@echo "     srs_publish_nossl       publish program using srs-librtmp, without ssl(simple handshake)"
	@echo "     srs_play_nossl          play program using srs-librtmp, without ssl(simple handshake)"
	@echo "     srs_publish_ssl         publish program using srs-librtmp, with ssl(complex handshake)"
	@echo "     srs_play_ssl            play program using srs-librtmp, with ssl(complex handshake)"
	@echo "     srs_ingest_flv_nossl    ingest flv file and publish to RTMP server. (simple handshake)"
	@echo "     srs_ingest_rtmp_nossl   ingest RTMP and publish to RTMP server. (simple handshake)"
	@echo "     srs_ingest_flv_ssl      ingest flv file and publish to RTMP server. (complex handshake)"
	@echo "     srs_ingest_rtmp_ssl     ingest RTMP and publish to RTMP server. (complex handshake)"
	@echo "Remark: about simple/complex handshake, see: http://blog.csdn.net/win_lin/article/details/13006803"
	@echo "Remark: srs Makefile will auto invoke this by --with/without-ssl, "
	@echo "     that is, if user specified ssl(by --with-ssl), srs will make this by 'make ssl'"
	@echo "     that is, if user not use ssl(by --without-ssl), use 'make nossl'"
	@echo "     see: https://github.com/winlinvip/simple-rtmp-server/wiki/Build"
	@echo "Remark: before make this sample, user must make the srs, with/without ssl"
    
clean:
	@rm -f srs_publish_nossl srs_play_nossl srs_publish_ssl srs_play_ssl srs_ingest_flv_ssl srs_ingest_rtmp_ssl srs_ingest_flv_nossl srs_ingest_rtmp_nossl

# srs library root
SRS_OBJS = ../../objs
# srs-librtmp for publish/play, built by srs.
SRS_LIBRTMP_I = $(SRS_OBJS)/include/srs_librtmp.h
SRS_LIBRTMP_L = $(SRS_OBJS)/lib/srs_librtmp.a
# openssl for complex handshake, built by srs.
SRS_LIBSSL_L = $(SRS_OBJS)/openssl/lib/libssl.a $(SRS_OBJS)/openssl/lib/libcrypto.a
# the research public headers
SRS_RESEARCH_DEPS = Makefile srs_research_public.h

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

srs_publish_nossl: srs_publish.c $(SRS_RESEARCH_DEPS) $(SRS_LIBRTMP_I) $(SRS_LIBRTMP_L)
	$(GCC) srs_publish.c $(SRS_LIBRTMP_L) $(EXTRA_CXX_FLAG) -o srs_publish_nossl

srs_play_nossl: srs_play.c $(SRS_RESEARCH_DEPS) $(SRS_LIBRTMP_I) $(SRS_LIBRTMP_L)
	$(GCC) srs_play.c $(SRS_LIBRTMP_L) $(EXTRA_CXX_FLAG) -o srs_play_nossl

srs_ingest_flv_nossl: srs_ingest_flv.c $(SRS_RESEARCH_DEPS) $(SRS_LIBRTMP_I) $(SRS_LIBRTMP_L)
	$(GCC) srs_ingest_flv.c $(SRS_LIBRTMP_L) $(EXTRA_CXX_FLAG) -o srs_ingest_flv_nossl

srs_ingest_rtmp_nossl: srs_ingest_rtmp.c $(SRS_RESEARCH_DEPS) $(SRS_LIBRTMP_I) $(SRS_LIBRTMP_L)
	$(GCC) srs_ingest_rtmp.c $(SRS_LIBRTMP_L) $(EXTRA_CXX_FLAG) -o srs_ingest_rtmp_nossl

srs_publish_ssl: srs_publish.c $(SRS_RESEARCH_DEPS) $(SRS_LIBRTMP_I) $(SRS_LIBRTMP_L) $(SRS_LIBSSL_L)
	$(GCC) srs_publish.c $(SRS_LIBRTMP_L) $(SRS_LIBSSL_L) $(EXTRA_CXX_FLAG) -o srs_publish_ssl

srs_play_ssl: srs_play.c $(SRS_RESEARCH_DEPS) $(SRS_LIBRTMP_I) $(SRS_LIBRTMP_L) $(SRS_LIBSSL_L)
	$(GCC) srs_play.c $(SRS_LIBRTMP_L) $(SRS_LIBSSL_L) $(EXTRA_CXX_FLAG) -o srs_play_ssl

srs_ingest_flv_ssl: srs_ingest_flv.c $(SRS_RESEARCH_DEPS) $(SRS_LIBRTMP_I) $(SRS_LIBRTMP_L) $(SRS_LIBSSL_L)
	$(GCC) srs_ingest_flv.c $(SRS_LIBRTMP_L) $(SRS_LIBSSL_L) $(EXTRA_CXX_FLAG) -o srs_ingest_flv_ssl

srs_ingest_rtmp_ssl: srs_ingest_rtmp.c $(SRS_RESEARCH_DEPS) $(SRS_LIBRTMP_I) $(SRS_LIBRTMP_L) $(SRS_LIBSSL_L)
	$(GCC) srs_ingest_rtmp.c $(SRS_LIBRTMP_L) $(SRS_LIBSSL_L) $(EXTRA_CXX_FLAG) -o srs_ingest_rtmp_ssl

# alias for publish/play with/without ssl
ssl: srs_publish_ssl srs_play_ssl srs_ingest_flv_ssl srs_ingest_rtmp_ssl
nossl: srs_publish_nossl srs_play_nossl srs_ingest_flv_nossl srs_ingest_rtmp_nossl
