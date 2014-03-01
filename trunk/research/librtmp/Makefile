.PHONY: clean

srs_publish: srs_publish.c Makefile ../../objs/include/srs_librtmp.h ../../objs/lib/srs_librtmp.a
	gcc srs_publish.c ../../objs/lib/srs_librtmp.a -g -O0 -lstdc++ -o srs_publish
    
clean:
	@rm -f srs_publish
