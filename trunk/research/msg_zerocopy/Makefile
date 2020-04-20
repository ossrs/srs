
.PHONY: default clean

default: server client

server: server.cpp ../../objs/st/libst.a
	g++ -g -O0 -I../../objs/st/ $^ -o $@

client: client.cpp ../../objs/st/libst.a
	g++ -g -O0 -I../../objs/st/ $^ -o $@

../../objs/st/libst.a: ../../Makefile
	(cd ../../ && $(MAKE) st)

clean:
	rm -f server client ../../objs/st/libst.a
