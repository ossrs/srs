.PHONY: help default clean bench pcap test all

default: bench test

clean:
	rm -rf ./objs

all: bench test pcap test

#########################################################################################################
# SRS benchmark tool for SRS, janus, GB28181.
./objs/.format.bench.txt: *.go janus/*.go ./objs/.format.srs.txt ./objs/.format.gb28181.txt
	gofmt -w *.go janus
	mkdir -p objs && echo "done" > ./objs/.format.bench.txt

bench: ./objs/srs_bench

UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
    SRT_PREFIX := $(shell brew --prefix srt)
    CGO_CFLAGS := -I$(SRT_PREFIX)/include
    CGO_LDFLAGS := -L$(SRT_PREFIX)/lib -lsrt
else ifeq ($(UNAME_S),Linux)
    CGO_CFLAGS := -I/usr/local/include
    CGO_LDFLAGS := -L/usr/local/lib -lsrt -L/usr/local/ssl/lib -lcrypto -lstdc++ -lm -ldl
endif

./objs/srs_bench: ./objs/.format.bench.txt *.go janus/*.go srs/*.go vnet/*.go gb28181/*.go live/*.go Makefile
	CGO_CFLAGS="$(CGO_CFLAGS)" CGO_LDFLAGS="$(CGO_LDFLAGS)" go build -mod=vendor -o objs/srs_bench .

#########################################################################################################
# For all regression tests.
test: ./objs/srs_test ./objs/srs_gb28181_test ./objs/srs_blackbox_test

#########################################################################################################
# For SRS regression test.
./objs/.format.srs.txt: srs/*.go vnet/*.go
	gofmt -w srs vnet
	mkdir -p objs && echo "done" > ./objs/.format.srs.txt

./objs/srs_test: ./objs/.format.srs.txt *.go srs/*.go vnet/*.go Makefile
	go test ./srs -mod=vendor -c -o ./objs/srs_test

#########################################################################################################
# For pcap simulator test.
./objs/.format.pcap.txt: pcap/*.go
	gofmt -w pcap
	mkdir -p objs && echo "done" > ./objs/.format.pcap.txt

pcap: ./objs/pcap_simulator

./objs/pcap_simulator: ./objs/.format.pcap.txt pcap/*.go Makefile
	go build -mod=vendor -o ./objs/pcap_simulator ./pcap

#########################################################################################################
# For gb28181 test.
./objs/.format.gb28181.txt: gb28181/*.go
	gofmt -w gb28181
	mkdir -p objs && echo "done" > ./objs/.format.gb28181.txt

./objs/srs_gb28181_test: ./objs/.format.gb28181.txt *.go gb28181/*.go Makefile
	go test ./gb28181 -mod=vendor -c -o ./objs/srs_gb28181_test

#########################################################################################################
# For blackbox test.
./objs/.format.blackbox.txt: blackbox/*.go
	gofmt -w blackbox
	mkdir -p objs && echo "done" > ./objs/.format.blackbox.txt

./objs/srs_blackbox_test: ./objs/.format.blackbox.txt *.go blackbox/*.go Makefile
	go test ./blackbox -mod=vendor -c -o ./objs/srs_blackbox_test

#########################################################################################################
# Help menu.
help:
	@echo "Usage: make [default|bench|pcap|test|clean]"
	@echo "     default     The default entry for make is bench+test"
	@echo "     bench       Make the bench to ./objs/srs_bench"
	@echo "     pcap        Make the pcap simulator to ./objs/pcap_simulator"
	@echo "     test        Make the test tool to ./objs/srs_test and ./objs/srs_gb28181_test ./objs/srs_blackbox_test"
	@echo "     clean       Remove all tools at ./objs"

