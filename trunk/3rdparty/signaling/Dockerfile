
############################################################
# build
############################################################
FROM registry.cn-hangzhou.aliyuncs.com/ossrs/srs:dev AS build

COPY . /tmp/signaling
RUN cd /tmp/signaling && make
RUN cp /tmp/signaling/objs/signaling /usr/local/bin/signaling
RUN cp -R /tmp/signaling/www /usr/local/

############################################################
# dist
############################################################
FROM centos:7 AS dist

# HTTP/1989
EXPOSE 1989
# SRS binary, config files and srs-console.
COPY --from=build /usr/local/bin/signaling /usr/local/bin/
COPY --from=build /usr/local/www /usr/local/www
# Default workdir and command.
WORKDIR /usr/local
CMD ["./bin/signaling"]
