.PHONY: clean

LDLIBS=../../obj/libst.a
CFLAGS=-g -O0 -I../../obj

./helloworld:  helloworld.c $(LDLIBS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -Wall -o helloworld helloworld.c $(LDLIBS)

clean:
	rm -f helloworld

