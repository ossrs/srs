# vim: ft=dockerfile
FROM fedora:25
MAINTAINER Ian Cordasco <graffatcolmingov@gmail.com>

# NOTE(sigmavirus24): We need "perl-core" here for the "prove" binary
# required by the test-all Makefile target
RUN dnf install -y \
        automake \
        gcc \
        git \
        make \
        libtool \
        perl-core && \
    mkdir /libyaml

COPY . /libyaml/
WORKDIR /libyaml

ENV LD_LIBRARY_PATH=/libyaml/src/.libs

RUN ./bootstrap && \
    ./configure && \
    make && \
    make install

CMD ["bash"]
