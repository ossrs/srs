# signaling

WebRTC signaling for https://github.com/ossrs/srs

## Usage

[Run SRS](https://github.com/ossrs/srs/tree/4.0release#usage) in docker:

```bash
docker run --rm --env CANDIDATE=$(ifconfig en0 inet| grep 'inet '|awk '{print $2}') \
  -p 1935:1935 -p 8080:8080 -p 1985:1985 -p 8000:8000/udp \
  registry.cn-hangzhou.aliyuncs.com/ossrs/srs:v4.0.95 \
  objs/srs -c conf/rtc.conf
```

> Note: More images and version is [here](https://cr.console.aliyun.com/repository/cn-hangzhou/ossrs/srs/images).

Run signaling in docker:

```bash
docker run --rm -p 1989:1989 registry.cn-hangzhou.aliyuncs.com/ossrs/signaling:v1.0.4
```

> Note: More images and version is [here](https://cr.console.aliyun.com/repository/cn-hangzhou/ossrs/signaling/images). 

Open the H5 demos:

* [WebRTC: One to One over SFU(SRS)](http://localhost:1989/demos/one2one.html?autostart=true)

## Build from source

Build and [run SRS](https://github.com/ossrs/srs/tree/4.0release#usage):

```bash
cd ~/git && git clone -b 4.0release https://gitee.com/ossrs/srs.git srs &&
cd ~/git/srs/trunk && ./configure && make && ./objs/srs -c conf/rtc.conf
```

Build and run signaling:

```bash
cd ~/git/srs/trunk/3rdparty/signaling && make && ./objs/signaling
```

Open demos by localhost: http://localhost:1989/demos

Build and run httpx-static for HTTPS/WSS:

```bash
cd ~/git/srs/trunk/3rdparty/httpx-static && make &&
./objs/httpx-static -http 80 -https 443 -ssk server.key -ssc server.crt \
    -proxy http://127.0.0.1:1989/sig -proxy http://127.0.0.1:1985/rtc \
    -proxy http://127.0.0.1:8080/
```

Open demos by HTTPS or IP:

* http://localhost/demos/
* https://localhost/demos/
* https://192.168.3.6/demos/

Winlin 2021.05
