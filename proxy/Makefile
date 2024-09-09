.PHONY: all build test fmt clean run

all: build

build: fmt ./srs-proxy

./srs-proxy: *.go
	go build -o srs-proxy .

test:
	go test ./...

fmt: ./.go-formarted

./.go-formarted: *.go
	touch .go-formarted
	go fmt ./...

clean:
	rm -f srs-proxy .go-formarted

run: fmt
	go run .
