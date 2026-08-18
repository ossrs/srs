.PHONY: all build test fmt clean run generate

all: build

build: fmt bin/srs-proxy

generate:
	go generate ./...

bin/srs-proxy: cmd/proxy/*.go internal/**/*.go
	@mkdir -p bin
	go build -o bin/srs-proxy ./cmd/proxy

test:
	go test ./...

fmt: ./.go-formarted

./.go-formarted: cmd/proxy/*.go internal/**/*.go
	touch .go-formarted
	go fmt ./cmd/... ./internal/...

clean:
	rm -rf bin .go-formarted

run: fmt
	go run ./cmd/proxy
