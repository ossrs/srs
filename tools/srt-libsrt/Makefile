.PHONY: all build test fmt vet clean

# The libsrt of objs/libsrt, which skills/srs-develop/scripts/srt-libsrt-build.sh builds.
LIBSRT_PC := objs/libsrt/lib/pkgconfig
export CGO_ENABLED := 1
export CGO_CFLAGS = $(shell PKG_CONFIG_PATH=$(LIBSRT_PC) pkg-config --cflags srt)
export CGO_LDFLAGS = $(shell PKG_CONFIG_PATH=$(LIBSRT_PC) pkg-config --static --libs srt)

all: build

build: objs/srt-libsrt

objs/srt-libsrt: *.go go.mod Makefile objs/libsrt/lib/libsrt.a
	go build -o objs/srt-libsrt .

objs/libsrt/lib/libsrt.a:
	@echo "No libsrt in objs/libsrt; build it with: bash skills/srs-develop/scripts/srt-libsrt-build.sh" >&2
	@exit 1

test: objs/libsrt/lib/libsrt.a
	go test $(TEST_ARGS) ./...

fmt:
	gofmt -l -w *.go

vet: objs/libsrt/lib/libsrt.a
	go vet ./...

clean:
	rm -f objs/srt-libsrt
