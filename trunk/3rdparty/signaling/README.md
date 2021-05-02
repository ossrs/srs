# signaling

WebRTC signaling for https://github.com/ossrs/srs

## Usage

Build and [run SRS](https://github.com/ossrs/srs/tree/4.0release#usage):

```bash
git clone -b 4.0release https://gitee.com/ossrs/srs.git srs &&
cd srs/trunk && ./configure && make && ./objs/srs -c conf/rtc.conf
```

Build and run signaling:

```bash
cd srs/trunk/3rdparty/signaling && make && ./objs/signaling
```

Open the H5 demos:

* [WebRTC: One to One over SFU(SRS)](http://localhost:1989/demos/one2one.html?autostart=true)

Winlin 2021.05
