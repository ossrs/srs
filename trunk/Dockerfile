FROM ossrs/srs:dev AS build

# Install depends tools.
RUN yum install -y gcc make gcc-c++ patch unzip perl git

# Build and install SRS.
COPY . /srs
WORKDIR /srs/trunk
RUN ./configure --jobs=2 && make -j2 && make install

# All config files for SRS.
RUN cp -R conf /usr/local/srs/conf
# The default index.html and srs-console.
RUN cp research/api-server/static-dir/index.html /usr/local/srs/objs/nginx/html/
RUN cp research/api-server/static-dir/favicon.ico /usr/local/srs/objs/nginx/html/
RUN cp research/players/crossdomain.xml /usr/local/srs/objs/nginx/html/
RUN cp -R research/console /usr/local/srs/objs/nginx/html/
RUN cp -R research/players /usr/local/srs/objs/nginx/html/
#RUN cp -R 3rdparty/signaling/www/demos /usr/local/srs/objs/nginx/html/

############################################################
# dist
############################################################
FROM centos:7 AS dist

# Expose ports for live streaming
EXPOSE 1935 1985 8080

# FFMPEG 4.1
COPY --from=build /usr/local/bin/ffmpeg /usr/local/srs/objs/ffmpeg/bin/ffmpeg
# SRS binary, config files and srs-console.
COPY --from=build /usr/local/srs /usr/local/srs

# Default workdir and command.
WORKDIR /usr/local/srs
CMD ["./objs/srs", "-c", "conf/srs.conf"]
