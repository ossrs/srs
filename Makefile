.PHONY: all build test fmt clean run generate integ integ-prepare integ-main integ-timer

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

# Make the test media, then run the timer cases and the other cases as two go
# test processes at once; it fails if either fails. They run in the cases
# directory with no package argument, so go test prints the summary of a
# passing run too. INTEG_ARGS passes flags to both, such as
# INTEG_ARGS="-integ.scale=56 -integ.shards=2" for the scale check.
integ: integ-prepare
	@$(MAKE) --no-print-directory -j2 integ-timer integ-main

# Make the test media from doc/source.flv; only changed files are made again.
# On macOS it builds an ffmpeg with every encoder first, if needed.
integ-prepare: bin/srs-integ
	bin/srs-integ prepare -source doc/source.flv -dir cmd/integ/testdata/media -setup skills/srs-develop/scripts/setup-ffmpeg-with-whip.sh

integ-timer:
	go -C cmd/integ/cases test -count=1 -run '^TestTimer_' -parallel 256 -args -integ.timer -integ.settle=2s $(INTEG_ARGS)

integ-main:
	go -C cmd/integ/cases test -count=1 -skip '^TestTimer_' -parallel 24 -args $(INTEG_ARGS)

clean:
	rm -rf bin .go-formarted

run: fmt
	go run ./cmd/proxy
