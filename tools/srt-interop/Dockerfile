# libsrt's own tools, srt-live-transmit and srt-test-live, on Haivision libsrt v1.5.7 with bonding and encryption,
# for the SRS SRT interop tests. Build it with skills/srs-develop/scripts/srt-interop-image.sh, and run the
# srs-integ cross-check on it with: SRT_INTEROP=on make integ-unit
ARG SRT_VERSION=1.5.7
ARG SRT_SHA256=017cd1e437ef2073a4dd10ddf7b55e86bc3d6ebac0393d13bd22f6a57055d32b

FROM ubuntu:24.04 AS build
ARG SRT_VERSION
ARG SRT_SHA256
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
      build-essential cmake curl ca-certificates libssl-dev pkg-config tclsh \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
RUN curl -fsSL -o srt.tar.gz "https://github.com/Haivision/srt/archive/refs/tags/v${SRT_VERSION}.tar.gz" \
    && echo "${SRT_SHA256}  srt.tar.gz" | sha256sum -c - \
    && tar -xzf srt.tar.gz
RUN cmake -S "srt-${SRT_VERSION}" -B build -DCMAKE_BUILD_TYPE=Release \
      -DENABLE_BONDING=ON -DENABLE_ENCRYPTION=ON -DUSE_ENCLIB=openssl-evp \
      -DENABLE_APPS=ON -DENABLE_TESTING=ON -DENABLE_SHARED=OFF -DENABLE_STATIC=ON \
    && cmake --build build --parallel "$(nproc)" --target srt-live-transmit srt-test-live

FROM ubuntu:24.04
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends libssl3t64 \
    && rm -rf /var/lib/apt/lists/*
COPY --from=build /src/build/srt-live-transmit /src/build/srt-test-live /usr/local/bin/
