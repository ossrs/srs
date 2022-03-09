# vim: ft=dockerfile
FROM ubuntu:14.04
MAINTAINER Ian Cordasco <graffatcolmingov@gmail.com>

RUN apt-get update && \
    apt-get install -y \
        software-properties-common \
        python-software-properties && \
    add-apt-repository ppa:git-core/ppa && \
    apt-get update && \
    apt-get install -y \
        autoconf \
        build-essential \
        git \
        libtool && \
    rm -rf /var/lib/apt/lists/* && \
    mkdir /libyaml

COPY . /libyaml/
WORKDIR /libyaml

ENV LD_LIBRARY_PATH=/libyaml/src/.libs

RUN ./bootstrap && \
    ./configure && \
    make && \
    make install

CMD ["bash"]
