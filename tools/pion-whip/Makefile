.PHONY: all build test fmt vet clean

all: build

build: objs/pion-whip

objs/pion-whip: *.go go.mod go.sum Makefile
	@mkdir -p objs
	go build -o objs/pion-whip .

test:
	go test ./...

fmt:
	gofmt -l -w *.go

vet:
	go vet ./...

clean:
	rm -rf objs
