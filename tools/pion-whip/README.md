# pion-whip

A WHIP publisher on [pion/webrtc](https://github.com/pion/webrtc) with FFmpeg-style options, for the SRS E2E scripts.
It publishes one MP4 file, H.264 video and Opus audio, and logs every NACK it receives and every resend it sends
back, plain or RTX, so a script can verify retransmission from the tool's log alone.

Build:

```bash
cd tools/pion-whip && make
```

`make test` runs the unit tests and `make clean` removes `objs/`.

Publish a file at its native rate, looping forever, with the NACK lines:

```bash
./objs/pion-whip -hide_banner -loglevel debug -re -stream_loop -1 -i source.mp4 -f whip 'http://localhost:1985/rtc/v1/whip/?app=live&stream=livestream'
```

The input must be a progressive MP4 with H.264 baseline video, so no B-frames, and Opus audio. Make one from the
test source with FFmpeg:

```bash
ffmpeg -i trunk/doc/source.flv -c:v libx264 -profile:v baseline -level 3.1 -pix_fmt yuv420p -r 25 -c:a libopus -ar 48000 -ac 2 -movflags +faststart -f mp4 source.mp4
```

The mp4 input format is inferred from the `.mp4` extension, so `-f mp4` is optional. It publishes until the input
ends, or until interrupted with `-stream_loop -1`.

The offer carries `rtx` for every video codec and an `a=ssrc-group:FID` for the video, as a browser's or FFmpeg's
WHIP offer does, and SRS chooses the retransmission format by its `nack_prefer_rtx`. FFmpeg has no option for this,
so the environment variable `PION_WHIP_RTX` selects it: `on`, or unset, offers `rtx`; `off` registers no `rtx` codec,
so the offer carries no `rtx`, no `apt` and no FID group, like a publisher without RTX, and the session can only use
plain retransmission whatever SRS prefers. The same running SRS can so be verified in both formats:

```bash
PION_WHIP_RTX=off ./objs/pion-whip -hide_banner -loglevel debug -re -stream_loop -1 -i source.mp4 -f whip 'http://localhost:1985/rtc/v1/whip/?app=live&stream=livestream'
```

The log lines a script greps, printed at `verbose` and above:

```
NACK received ssrc=<media>, seqs=[<seq>,...]
Resend plain seq=<seq>, ssrc=<media>, pt=<pt>
Resend RTX seq=<rtx seq>, ssrc=<rtx>, pt=<rtx pt>, osn=<seq>
```

At `debug` it also prints the offer, the answer, the ICE and DTLS states, a frame and packet count every second,
and `Session URL: <url>`, the WHIP delete URL, whose `session=` query is the SRS username that the NACK API of a
`--simulator=on` build takes to drop packets on this session.
