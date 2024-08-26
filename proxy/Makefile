.PHONY: all build test fmt clean run

all: build

build: fmt
	go build -o srs-proxy .

test:
	go test ./...

fmt:
	go fmt ./...

clean:
	rm -f srs-proxy

run: fmt
	go run main.go
