---
title: RTMP Handshake
sidebar_label: RTMP Handshake
hide_title: false
hide_table_of_contents: false
---

# RTMP Handshake

The rtmp specification 1.0 defines the RTMP handshake:
* c0/s0: 1 bytes, specifies the protocol is RTMP or RTMPE/RTMPS.
* c1/s1: 1536 bytes, first 4 bytes is time, next 4 bytes is 0x00, 1528 random bytes.
* c2/s2: 1536 bytes, first 4 bytes is time echo, next 4 bytes is time, 1528 bytes c2==s1 and s2==c1.
This is the simple handshake, the standard handshake, and the FMLE use this handshake.

While the server connected by flash player only support simple handshake, the flash player can only play the vp6 codec, and do not support h.264+aac. Adobe changed the simple handshake to encrypted complex handshake, see: [Changed Handshake of RTMP](http://blog.csdn.net/win_lin/article/details/13006803)

The handshake summary: | 

| Handshake | Depends | Player | Client | SRS | Use Scenario |
| ---- | ----- | --------------------- | -------- | --- | ---- |
| Simple<br/>Standard | No | vp6+mp3/speex | All | Supprted | Encoder, for examle, FMLE, FFMPEG |
| Complex | openssl | vp6+mp3/speex<br/>h264+aac | Flash | Supported | Flash player requires complex handshake to play h.264+aac codec. |

Player(Flash palyer): The supported codec for flash player.

Notes: When compile SRS with SSL, SRS will try complex, then simple.

Winlin 2014.10

![](https://ossrs.io/gif/v1/sls.gif?site=ossrs.io&path=/lts/doc/en/v7/rtmp-handshake)


