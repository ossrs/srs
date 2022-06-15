ARG ARCH
FROM ${ARCH}ossrs/srs:ubuntu20 AS build

ARG BUILDPLATFORM
ARG TARGETPLATFORM
ARG JOBS=2
ARG SRS_AUTO_PACKAGER
RUN echo "BUILDPLATFORM: $BUILDPLATFORM, TARGETPLATFORM: $TARGETPLATFORM, JOBS: $JOBS, PACKAGER: ${#SRS_AUTO_PACKAGER}"

# https://serverfault.com/questions/949991/how-to-install-tzdata-on-a-ubuntu-docker-image
ENV DEBIAN_FRONTEND noninteractive

# Install depends tools.
RUN apt-get update && apt-get install -y gcc make g++ patch unzip perl git

# Build and install SRS.
COPY . /srs
WORKDIR /srs/trunk
RUN ./configure --srt=on --jobs=${JOBS} && make -j${JOBS} && make install

# All config files for SRS.
RUN cp -R conf /usr/local/srs/conf && \
    cp research/api-server/static-dir/index.html /usr/local/srs/objs/nginx/html/ && \
    cp research/api-server/static-dir/favicon.ico /usr/local/srs/objs/nginx/html/ && \
    cp research/players/crossdomain.xml /usr/local/srs/objs/nginx/html/ && \
    cp -R research/console /usr/local/srs/objs/nginx/html/ && \
    cp -R research/players /usr/local/srs/objs/nginx/html/ && \
    cp -R 3rdparty/signaling/www/demos /usr/local/srs/objs/nginx/html/

############################################################
# dist
############################################################
FROM ${ARCH}ubuntu:focal AS dist

ARG BUILDPLATFORM
ARG TARGETPLATFORM
RUN echo "BUILDPLATFORM: $BUILDPLATFORM, TARGETPLATFORM: $TARGETPLATFORM"

# Expose ports for streaming @see https://github.com/ossrs/srs#ports
EXPOSE 1935 1985 8080 8000/udp 10080/udp

# FFMPEG 4.1
COPY --from=build /usr/local/bin/ffmpeg /usr/local/srs/objs/ffmpeg/bin/ffmpeg
# SRS binary, config files and srs-console.
COPY --from=build /usr/local/srs /usr/local/srs

# Default workdir and command.
WORKDIR /usr/local/srs
CMD ["./objs/srs", "-c", "conf/docker.conf"]
