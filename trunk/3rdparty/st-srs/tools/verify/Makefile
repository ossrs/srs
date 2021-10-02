.PHONY: clean

LDLIBS=../../obj/libst.a
CFLAGS=-g -O0

./verify:  verify.c $(LDLIBS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -Wall -o verify verify.c $(LDLIBS)

clean:
	rm -f verify

