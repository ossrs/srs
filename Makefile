.PHONY: all build test fmt clean run

all: build

build: fmt ./srs-proxy

./srs-proxy: cmd/proxy/*.go internal/**/*.go
	go build -o srs-proxy ./cmd/proxy

test:
	go test ./...

fmt: ./.go-formarted

./.go-formarted: cmd/proxy/*.go internal/**/*.go
	touch .go-formarted
	go fmt ./cmd/... ./internal/...

clean:
	rm -f srs-proxy .go-formarted

run: fmt
	go run ./cmd/proxy
