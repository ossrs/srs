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

COPY trunk/ /srs/trunk/
WORKDIR /srs/trunk
RUN ./configure --srt=on --rtsp=on --sanitizer=off --utest=off \
    --extra-flags="-g -fno-omit-frame-pointer -O2" && make -j2
RUN ln -s /usr/lib/linux-tools/*/perf /usr/local/bin/perf

CMD ["./objs/srs", "-e"]
