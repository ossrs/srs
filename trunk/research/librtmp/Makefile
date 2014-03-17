GCC = gcc

.PHONY: default clean help ssl nossl

default: help

help:
	@echo "Usage: make <help>|<clean>|<srs_publish_nossl>|<srs_play_nossl>|<srs_publish_ssl>|<srs_play_ssl>"
	@echo "     help                display this help"
	@echo "     clean               cleanup build"
	@echo "     ssl                 srs_publish_ssl, srs_play_ssl"
	@echo "     nossl               srs_publish_nossl, srs_play_nossl"
	@echo "     srs_publish_nossl   publish program using srs-librtmp, without ssl(simple handshake)"
	@echo "     srs_play_nossl      play program using srs-librtmp, without ssl(simple handshake)"
	@echo "     srs_publish_ssl     publish program using srs-librtmp, with ssl(complex handshake)"
	@echo "     srs_play_ssl        play program using srs-librtmp, with ssl(complex handshake)"
	@echo "Remark: about simple/complex handshake, see: http://blog.csdn.net/win_lin/article/details/13006803"
	@echo "Remark: srs Makefile will auto invoke this by --with/without-ssl, "
	@echo "     that is, if user specified ssl(by --with-ssl), srs will make this by 'make ssl'"
	@echo "     that is, if user not use ssl(by --without-ssl), use 'make nossl'"
	@echo "     see: https://github.com/winlinvip/simple-rtmp-server/wiki/Build"
	@echo "Remark: before make this sample, user must make the srs, with/without ssl"
    
clean:
	@rm -f srs_publish_nossl srs_play_nossl srs_publish_ssl srs_play_ssl

# srs library root
SRS_OBJS = ../../objs
# srs-librtmp for publish/play, built by srs.
SRS_LIBRTMP_I = $(SRS_OBJS)/include/srs_librtmp.h
SRS_LIBRTMP_L = $(SRS_OBJS)/lib/srs_librtmp.a
# openssl for complex handshake, built by srs.
SRS_LIBSSL_L = $(SRS_OBJS)/openssl/lib/libssl.a $(SRS_OBJS)/openssl/lib/libcrypto.a

# for x86/x64 platform
ifeq ($(GCC), gcc)
srs_publish_nossl: srs_publish.c Makefile $(SRS_LIBRTMP_I) $(SRS_LIBRTMP_L)
	$(GCC) srs_publish.c $(SRS_LIBRTMP_L) -g -O0 -lstdc++ -o srs_publish_nossl

srs_play_nossl: srs_play.c Makefile $(SRS_LIBRTMP_I) $(SRS_LIBRTMP_L)
	$(GCC) srs_play.c $(SRS_LIBRTMP_L) -g -O0 -lstdc++ -o srs_play_nossl

srs_publish_ssl: srs_publish.c Makefile $(SRS_LIBRTMP_I) $(SRS_LIBRTMP_L) $(SRS_LIBSSL_L)
	$(GCC) srs_publish.c $(SRS_LIBRTMP_L) $(SRS_LIBSSL_L) -g -O0 -ldl -lstdc++ -o srs_publish_ssl

srs_play_ssl: srs_play.c Makefile $(SRS_LIBRTMP_I) $(SRS_LIBRTMP_L) $(SRS_LIBSSL_L)
	$(GCC) srs_play.c $(SRS_LIBRTMP_L) $(SRS_LIBSSL_L) -g -O0 -ldl -lstdc++ -o srs_play_ssl
# for arm.
else
srs_publish_nossl: srs_publish.c Makefile $(SRS_LIBRTMP_I) $(SRS_LIBRTMP_L)
	$(GCC) srs_publish.c $(SRS_LIBRTMP_L) -g -O0 -static -lstdc++ -o srs_publish_nossl

srs_play_nossl: srs_play.c Makefile $(SRS_LIBRTMP_I) $(SRS_LIBRTMP_L)
	$(GCC) srs_play.c $(SRS_LIBRTMP_L) -g -O0 -static -lstdc++ -o srs_play_nossl

srs_publish_ssl: srs_publish.c Makefile $(SRS_LIBRTMP_I) $(SRS_LIBRTMP_L) $(SRS_LIBSSL_L)
	$(GCC) srs_publish.c $(SRS_LIBRTMP_L) $(SRS_LIBSSL_L) -g -O0 -static -ldl -lstdc++ -o srs_publish_ssl

srs_play_ssl: srs_play.c Makefile $(SRS_LIBRTMP_I) $(SRS_LIBRTMP_L) $(SRS_LIBSSL_L)
	$(GCC) srs_play.c $(SRS_LIBRTMP_L) $(SRS_LIBSSL_L) -g -O0 -static -ldl -lstdc++ -o srs_play_ssl
endif

# alias for publish/play with/without ssl
ssl: srs_publish_ssl srs_play_ssl
nossl: srs_publish_nossl srs_play_nossl
