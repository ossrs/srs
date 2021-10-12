.PHONY: clean

CFLAGS=-g -O0

./porting:  porting.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -Wall -o $@ $^ $(LDLIBS)

clean:
	rm -f porting

