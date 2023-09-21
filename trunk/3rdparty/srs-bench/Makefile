.PHONY: help default clean bench test

default: bench test

clean:
	rm -rf ./objs

#########################################################################################################
# SRS benchmark tool for SRS, janus, GB28181.
./objs/.format.bench.txt: *.go janus/*.go ./objs/.format.srs.txt ./objs/.format.gb28181.txt
	gofmt -w *.go janus
	mkdir -p objs && echo "done" > ./objs/.format.bench.txt

bench: ./objs/srs_bench ./objs/pcap_simulator

./objs/srs_bench: ./objs/.format.bench.txt *.go janus/*.go srs/*.go vnet/*.go gb28181/*.go Makefile
	go build -mod=vendor -o objs/srs_bench .

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
	@echo "Usage: make [default|bench|test|clean]"
	@echo "     default     The default entry for make is bench+test"
	@echo "     bench       Make the bench to ./objs/srs_bench"
	@echo "     test        Make the test tool to ./objs/srs_test and ./objs/srs_gb28181_test ./objs/srs_blackbox_test"
	@echo "     clean       Remove all tools at ./objs"

