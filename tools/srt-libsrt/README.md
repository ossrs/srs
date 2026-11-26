# srt-libsrt

An SRT peer in live mode on Haivision libsrt, for the SRS tests. It links its own libsrt, v1.5.7 with bonding and
encryption, built from source into `objs/libsrt`, not the libsrt SRS bundles in `trunk/3rdparty`, so a bug of one
libsrt release can't hide in both SRS and its test client. The srs-integ suite runs it as a process, its `Libsrt`
client, since the suite itself needs no cgo.

Build libsrt once and the tool:

```bash
bash skills/srs-develop/scripts/srt-libsrt-build.sh
```

Later, `cd tools/srt-libsrt && make` rebuilds the tool, `make test` runs its tests, which call its own listener, and
`make clean` removes the tool but keeps libsrt.

## Commands

```bash
./objs/srt-libsrt publish [options] host:port   # stdin to the peer, the peer to stdout
./objs/srt-libsrt play [options] host:port      # the peer to stdout
./objs/srt-libsrt listen [options] host:port    # one caller, then as publish; port 0 for any
```

- `publish` and `listen` send what each read of stdin gives as one message, at most 7 TS packets, and end at the
  end of stdin, once the peer has acknowledged it all and held it for its TSBPD delay.
- Each command also ends when the peer closes, and on SIGINT or SIGTERM, at once.
- stdin is sent as it arrives, so pace it, such as with `ffmpeg -re`.
- libsrt gives up a handshake nobody answers after about 1s at the least, whatever `-conntimeo` asks.

Publish a file to SRS, and play it back:

```bash
ffmpeg -re -i trunk/doc/source.flv -c copy -f mpegts - | ./objs/srt-libsrt publish -streamid '#!::r=live/livestream,m=publish' 127.0.0.1:10080
./objs/srt-libsrt play -streamid '#!::r=live/livestream,m=request' 127.0.0.1:10080 | ffprobe -
```

## Options

Each option is libsrt's `SRTO_` option of the same name, set only when given; times are milliseconds and sizes bytes:
`-streamid`, `-passphrase`, `-pbkeylen`, `-latency`, `-rcvlatency`, `-peerlatency`, `-conntimeo`, `-peeridletimeo`,
`-mss`, `-payloadsize`, `-maxbw`, `-sndbuf`, `-rcvbuf`, `-tsbpdmode on|off` and `-tlpktdrop on|off`. A listener's
options apply to the caller it accepts. A value libsrt refuses ends the command with an error naming the option.

## Events

The tool writes one JSON object per line to stderr, among libsrt's own error logs:

```
{"event":"listening","addr":"127.0.0.1:50000"}
{"event":"connected","local":"...","peer":"...","streamid":"...","msRcvTsbPdDelay":120,"msSndTsbPdDelay":120,"mss":1500,"flowWindow":8192}
{"event":"closed","reason":"eof|peer|signal|stdout","msgSent":500,"msgRecv":0,"stats":{"pktSent":500,...}}
{"event":"error","error":"connect: ...","reject":1010}
```

- `connected` gives the TSBPD delays the handshake agreed: how long this end holds what it receives, and how long the
  peer holds what this end sends; the MSS, the smaller of the two ends'; and the flow window, the packets the peer takes
  in flight, its receiver buffer in packets.
- `closed` gives the messages moved and libsrt's totals of the connection, by their `SRT_TRACEBSTATS` names without
  `Total`.
- `error` gives `reject`, the reason on the wire (1000 and more, such as 1010 for a wrong passphrase), when the peer
  refused the handshake.

The exit code is 0 after `closed`, 1 after `error`, and 2 for bad usage.
