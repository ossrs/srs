.PHONY: help default clean httpx-static

default: httpx-static

clean:
	rm -f ./objs/httpx-static

.format.txt: *.go
	gofmt -w .
	echo "done" > .format.txt

httpx-static: ./objs/httpx-static

./objs/httpx-static: .format.txt *.go Makefile
	go build -mod=vendor -o objs/httpx-static .

help:
	@echo "Usage: make [httpx-static]"
	@echo "     httpx-static       Make the httpx-static to ./objs/httpx-static"
