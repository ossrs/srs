.PHONY: all build test fmt clean run generate integ integ-main integ-timer

all: build

build: fmt bin/srs-proxy bin/srs-integ

generate:
	go generate ./...

bin/srs-proxy: cmd/proxy/*.go internal/**/*.go
	@mkdir -p bin
	go build -o bin/srs-proxy ./cmd/proxy

# The black-box integration suite is its own Go module in cmd/integ.
INTEG_GO_FILES := $(shell find cmd/integ -name '*.go')

bin/srs-integ: cmd/integ/go.mod $(INTEG_GO_FILES)
	@mkdir -p bin
	go -C cmd/integ build -o ../../bin/srs-integ .

test:
	go test ./...

fmt: ./.go-formarted

./.go-formarted: cmd/proxy/*.go internal/**/*.go $(INTEG_GO_FILES)
	touch .go-formarted
	go fmt ./cmd/... ./internal/...
	go -C cmd/integ fmt ./...

# Run the timer cases and the other cases as two go test processes at once;
# it fails if either fails.
integ:
	@$(MAKE) --no-print-directory -j2 integ-timer integ-main

integ-timer:
	go -C cmd/integ test ./cases/... -count=1 -run '^TestTimer_' -parallel 256

integ-main:
	go -C cmd/integ test ./cases/... -count=1 -skip '^TestTimer_' -parallel 64

clean:
	rm -rf bin .go-formarted

run: fmt
	go run ./cmd/proxy
