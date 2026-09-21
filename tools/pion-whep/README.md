# pion-whep

A WHEP player on [pion/webrtc](https://github.com/pion/webrtc) with FFmpeg-style options, for the SRS E2E scripts.
It plays one stream, prints what it receives, and logs every NACK it sends and every retransmission it gets back,
plain or RTX, so a script can verify retransmission from the tool's log alone.

Build:

```bash
cd tools/pion-whep && make
```

`make test` runs the unit tests and `make clean` removes `objs/`.

Play a stream for ten seconds, with the offer, the answer, the ICE and DTLS states, a packet count every second and
the NACK lines:

```bash
./objs/pion-whep -hide_banner -loglevel debug -f whep -i 'http://localhost:1985/rtc/v1/whep/?app=live&stream=livestream' -t 10 -f null -
```

Without `-t` it plays until interrupted. The whep input format is inferred from an http or https URL, so `-f whep`
is optional. The output is always `-f null -`.

The offer carries `rtx` for every video codec, as a browser's does, and SRS chooses the retransmission format by its
`nack_prefer_rtx`. FFmpeg has no option for this, so the environment variable `PION_WHEP_RTX` selects it: `on`, or
unset, offers `rtx`; `off` omits it, so the session can only use plain retransmission whatever SRS prefers, and the
same running SRS can be verified in both formats:

```bash
PION_WHEP_RTX=off ./objs/pion-whep -hide_banner -loglevel debug -i 'http://localhost:1985/rtc/v1/whep/?app=live&stream=livestream' -t 10 -f null -
```

The log lines a script greps, printed at `verbose` and above:

```
NACK sent ssrc=<media>, seqs=[<seq>,...]
Recovered plain seq=<seq>, ssrc=<media>, pt=<pt>
Recovered RTX seq=<rtx seq>, ssrc=<rtx>, pt=<rtx pt>, osn=<seq>
```

At `debug` it also prints `Session URL: <url>`, the WHEP delete URL, whose `session=` query is the SRS username
that the NACK API of a `--simulator=on` build takes to drop packets on this session.
