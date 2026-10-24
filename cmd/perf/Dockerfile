# The Go proxy of perf-up.sh -target proxy, built from the worktree in its own
# stage, so a proxy change does not rebuild SRS and an SRS change not the proxy.
FROM golang:1.25-bookworm AS proxy
# Go's pprof for perf-profile-go.sh, the same tool as go tool pprof, so the
# load container needs no Go toolchain.
RUN CGO_ENABLED=0 go build -o /out/pprof cmd/pprof
WORKDIR /src
COPY go.mod go.sum ./
RUN go mod download
COPY cmd/proxy/ cmd/proxy/
COPY internal/ internal/
RUN CGO_ENABLED=0 go build -o /out/srs-proxy ./cmd/proxy

FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    automake build-essential cmake ffmpeg git linux-tools-common linux-tools-generic \
    patch perl pkg-config strace sysstat tcl unzip which ca-certificates \
    && rm -rf /var/lib/apt/lists/*

RUN git clone --depth 1 https://github.com/brendangregg/FlameGraph.git /opt/FlameGraph
ENV PATH="/opt/FlameGraph:${PATH}"

# The flat CBR source every measurement uses; before the SRS source, so an SRS
# change keeps this layer cached. The build fails when the source is not flat.
COPY skills/internal-perf-for-srs/scripts/perf-source.sh skills/internal-perf-for-srs/scripts/perf-source-check.sh /srs/perf/
RUN bash /srs/perf/perf-source.sh /srs/perf/cbr.flv

# curl fetches the proxy's Go profiles over pprof's HTTP. Its own layer after
# the source, so the source and the packages above stay cached.
RUN apt-get update && apt-get install -y --no-install-recommends curl \
    && rm -rf /var/lib/apt/lists/*

COPY trunk/ /srs/trunk/
WORKDIR /srs/trunk
RUN ./configure --srt=on --rtsp=on --sanitizer=off --utest=off \
    --extra-flags="-g -fno-omit-frame-pointer -O2" && make -j2
RUN ln -s /usr/lib/linux-tools/*/perf /usr/local/bin/perf
COPY --from=proxy /out/srs-proxy /srs/bin/srs-proxy
COPY --from=proxy /out/pprof /usr/local/bin/pprof

CMD ["./objs/srs", "-e"]
