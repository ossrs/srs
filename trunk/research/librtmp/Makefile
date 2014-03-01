.PHONY: default clean

default: srs_publish srs_play
    
clean:
	@rm -f srs_publish srs_play

srs_publish: srs_publish.c Makefile ../../objs/include/srs_librtmp.h ../../objs/lib/srs_librtmp.a
	gcc srs_publish.c ../../objs/lib/srs_librtmp.a -g -O0 -lstdc++ -o srs_publish

srs_play: srs_play.c Makefile ../../objs/include/srs_librtmp.h ../../objs/lib/srs_librtmp.a
	gcc srs_play.c ../../objs/lib/srs_librtmp.a -g -O0 -lstdc++ -o srs_play
