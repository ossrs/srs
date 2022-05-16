# HTTPX

A HTTP/HTTPS Server, support letsencrypt or self-sign HTTPS and proxying HTTP as HTTPS.

Docker for https://github.com/ossrs/go-oryx

Build at https://code.aliyun.com/ossrs/go-oryx

Images at https://cr.console.aliyun.com/repository/cn-hangzhou/ossrs/httpx/images

> Remark: Requires GO1.8+

## Usage

*HTTP*: Start a HTTP static server

```
go get github.com/ossrs/go-oryx/httpx-static &&
cd $GOPATH/src/github.com/ossrs/go-oryx/httpx-static &&
$GOPATH/bin/httpx-static -http 8080 -root `pwd`/html
```

Open http://localhost:8080/ in browser.

*HTTPS self-sign*: Start a HTTPS static server

```
go get github.com/ossrs/go-oryx/httpx-static &&
cd $GOPATH/src/github.com/ossrs/go-oryx/httpx-static &&
openssl genrsa -out server.key 2048 &&
subj="/C=CN/ST=Beijing/L=Beijing/O=Me/OU=Me/CN=me.org" &&
openssl req -new -x509 -key server.key -out server.crt -days 365 -subj $subj &&
$GOPATH/bin/httpx-static -https 8443 -root `pwd`/html
```

Open https://localhost:8443/ in browser.

> Remark: Click `ADVANCED` => `Proceed to localhost (unsafe)`, or type `thisisunsafe` in page.

*HTTPS proxy*: Proxy http as https

```
go get github.com/ossrs/go-oryx/httpx-static &&
cd $GOPATH/src/github.com/ossrs/go-oryx/httpx-static &&
openssl genrsa -out server.key 2048 &&
subj="/C=CN/ST=Beijing/L=Beijing/O=Me/OU=Me/CN=me.org" &&
openssl req -new -x509 -key server.key -out server.crt -days 365 -subj $subj &&
$GOPATH/bin/httpx-static -https 8443 -root `pwd`/html -proxy http://ossrs.net:1985/api/v1
```

Open https://localhost:8443/api/v1/summaries in browser.

## Docker

Run httpx-static in docker:

```bash
docker run --rm -p 80:80 -p 443:443 registry.cn-hangzhou.aliyuncs.com/ossrs/httpx:v1.0.19
```

> Note: More images and version is [here](https://cr.console.aliyun.com/repository/cn-hangzhou/ossrs/httpx/images).

To proxy to other dockers, in macOS:

```bash
CANDIDATE=$(ifconfig en0 inet| grep 'inet '|awk '{print $2}') &&
docker run --rm -p 80:80 -p 443:443 registry.cn-hangzhou.aliyuncs.com/ossrs/httpx:v1.0.19 \
    ./bin/httpx-static -http 80 -https 443 -ssk ./etc/server.key -ssc ./etc/server.crt \
        -proxy http://$CANDIDATE:8080/
```

## History

* v0.0.3, 2017-11-03, Support multiple proxy HTTP to HTTPS.

Winlin 2017
