# HTTPX

A HTTP/HTTPS Server, support letsencrypt or self-sign HTTPS and proxying HTTP as HTTPS.

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

> Remark: Click `ADVANCED` => `Proceed to localhost (unsafe)`.

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

## History

* v0.0.3, 2017-11-03, Support multiple proxy HTTP to HTTPS.

Winlin 2017
