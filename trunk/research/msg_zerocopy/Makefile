
default: server client

server: server.cpp ../../objs/st/libst.a
	gcc -g -O0 -I../../objs/st/ $^ -o $@

client: client.cpp ../../objs/st/libst.a
	gcc -g -O0 -I../../objs/st/ $^ -o $@
