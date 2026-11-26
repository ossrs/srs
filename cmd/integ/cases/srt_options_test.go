// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package cases

import (
	"context"
	"errors"
	"net"
	"os"
	"strconv"
	"testing"
	"time"

	"srs-integ/internal/check"
	"srs-integ/internal/flv"
	"srs-integ/internal/media"
	"srs-integ/internal/mock"
	"srs-integ/internal/report"
	"srs-integ/internal/server"
	"srs-integ/internal/srt"
)

// srtTransportMSS is the mss of the SrtTransport server, and
// srtTransportMessage the message its publishers send, the most whole TS
// packets of its payload, the MSS less 44 bytes of headers.
const (
	srtTransportMSS     = 1000
	srtTransportMessage = 5 * 188
)

// srtTransportRecvBuf is the recvbuf of the SrtTransport server, in packets
// of its MSS less the IP and UDP headers, 28 bytes.
const srtTransportRecvBuf = 500

// srtTransportMaxBW is the maxbw of the SrtTransport server, in bytes per
// second: about three times the test media's rate, so a real-time stream
// plays, and a burst goes out at that rate.
const srtTransportMaxBW = 200000

// srtTransportConfig is the config of the SrtTransport server: SRT on a second
// port, and on [::] too where the host has IPv6; the transport settings mss,
// payloadsize, maxbw, sendbuf, recvbuf and connect_timeout; and srt_to_rtmp
// off.
func srtTransportConfig(t testing.TB) (server.Config, int) {
	second := server.ReservePort(t)
	cfg := server.WithSRT(server.Config{Listeners: []server.Listener{server.RTMP, server.HTTPAPI}, Hooks: true}, server.SRTOptions{
		MorePorts:      []int{second},
		IPv6:           true,
		MSS:            srtTransportMSS,
		PayloadSize:    srtTransportMessage,
		MaxBW:          srtTransportMaxBW,
		SendBuf:        2 * srtTransportRecvBuf * (srtTransportMSS - 28),
		RecvBuf:        srtTransportRecvBuf * (srtTransportMSS - 28),
		ConnectTimeout: 100 * time.Millisecond,
		NoToRTMP:       true,
	})
	return cfg, second
}

// srtOptionClients are the libraries of the option cases: every option SRS
// has is one each of them can set or see.
var srtOptionClients = []srt.Client{srt.GoSRT, srt.Libsrt, srt.Zsiec}

// TestSrt_Transport checks the SRT settings of the transport from outside:
// a second listen port and [::], the MSS a caller gets, the flow window
// recvbuf gives, the rate maxbw sends a burst at, that connect_timeout
// bounds no caller's handshake, and that with srt_to_rtmp off an SRT stream
// is not played over RTMP. Its publishers send messages that fit its MSS.
func TestSrt_Transport(t *testing.T) {
	t.Parallel()
	cfg, second := srtTransportConfig(t)
	srs := server.Group(t, cfg)
	first := srs.Ports[server.SRT]
	// publish is the options of a publisher of a stream: the payload size of
	// its messages, which libsrt reads its input by.
	publish := func(stream string) srt.Options {
		return srt.Options{StreamID: srtID("live/"+stream, "publish"), PayloadSize: srtTransportMessage}
	}

	for _, cl := range srtOptionClients {
		// A publisher on the second port is played on the first: one server
		// behind both.
		check.Case(t, "SecondPort_"+cl.Name(), check.Normal, func(c *check.C) {
			stream := streamName(c)
			play := srtPlayerAt(c, srt.GoSRT, srs, srtLocal(first), srt.Options{StreamID: srtID("live/"+stream, "request")}, stream)
			publishSRTAt(c, cl, srtLocal(second), publish(stream), media.H264HighAACTS, srtTransportMessage)
			checkSRTPlayed(c, srtAnalyze(c, play))
		})

		// A publisher over IPv6 is played over IPv4, and its hooks have its
		// IPv6 address.
		check.Case(t, "IPv6_"+cl.Name(), check.Normal, func(c *check.C) {
			if !server.HasIPv6() {
				c.Skip("the host has no IPv6")
			}
			stream := streamName(c)
			play := srtPlayerAt(c, srt.GoSRT, srs, srtLocal(first), srt.Options{StreamID: srtID("live/"+stream, "request")}, stream)
			publishSRTAt(c, cl, net.JoinHostPort("::1", strconv.Itoa(first)), publish(stream), media.H264HighAACTS, srtTransportMessage)
			c.CheckEqual(waitHook(c, srs, "on_publish", stream).Field("ip"), "::1", "the publisher's ip")
			checkSRTPlayed(c, srtAnalyze(c, play))
		})

		// A caller of the default MSS, 1500, gets SRS's, the smaller.
		check.Case(t, "MssNegotiated_"+cl.Name(), check.Normal, func(c *check.C) {
			conn := dialSRTAt(c, cl, srtLocal(first), srt.Options{StreamID: srtID("live/"+streamName(c), "request")})
			c.CheckEqual(conn.MSS(), srtTransportMSS, "the MSS")
		})

		// A maxbw player gets a burst at that rate: the publisher sends 1s
		// of media at once, about 70KB, which SRS relays as it comes. The
		// player holds what it gets for 20ms, the least zsiec takes, so its
		// TSBPD keeps the pace: SRS stamps each packet as it sends it, and a
		// player of the default 120ms would let the first 120ms out at once.
		check.Case(t, "MaxBwPacesPlayer_"+cl.Name(), check.Normal, func(c *check.C) {
			stream := streamName(c)
			play := srtPlayerAt(c, cl, srs, srtLocal(first), srt.Options{StreamID: srtID("live/"+stream, "request"), Latency: 20 * time.Millisecond}, stream)
			read := readSRTTimed(play, 50*srtTransportMessage)
			publishSRTLeadAt(c, srt.GoSRT, srtLocal(first), publish(stream), media.H264HighAACTS, srtTransportMessage, time.Second)
			d := read.wait(c)
			// libsrt paces a little faster than maxbw, and the player's 20ms
			// and its first read take some of the time, so half is the bound:
			// without maxbw, the burst comes in a few milliseconds.
			want := time.Duration(49*srtTransportMessage) * time.Second / srtTransportMaxBW
			c.Check(d >= want/2, "50 messages in %v, want about %v at maxbw %v B/s", d, want, srtTransportMaxBW)
		})
	}

	// The flow window of a caller is SRS's recvbuf in packets; gosrt keeps
	// none of the peer's.
	for _, cl := range []srt.Client{srt.Libsrt, srt.Zsiec} {
		check.Case(t, "RecvBufFlowWindow_"+cl.Name(), check.Normal, func(c *check.C) {
			conn := dialSRTAt(c, cl, srtLocal(first), srt.Options{StreamID: srtID("live/"+streamName(c), "request")})
			c.CheckEqual(conn.FlowWindow(), srtTransportRecvBuf, "the flow window")
		})
	}

	// connect_timeout is libsrt's timeout of a caller's connect, so it bounds
	// no handshake SRS listens to: a caller whose handshake takes three
	// times as long, over a link that delays each packet to SRS by 150ms,
	// connects. This is libsrt's behaviour and passed from the start.
	check.Case(t, "ConnectTimeoutNotForCallers", check.Normal, func(c *check.C) {
		relay, err := srt.NewImpair(srtLocal(first), 1)
		c.Must(err, "start the relay")
		defer relay.Close()
		relay.Set(srt.Up, srt.Shape{Delay: 150 * time.Millisecond})
		start := time.Now()
		conn := dialSRTAt(c, srt.GoSRT, relay.Addr().String(), srt.Options{StreamID: srtID("live/"+streamName(c), "request")})
		c.Check(time.Since(start) >= 300*time.Millisecond, "connected in %v, want two delayed handshake packets", time.Since(start))
		c.CheckEqual(conn.MSS(), srtTransportMSS, "the MSS")
	})

	// With srt_to_rtmp off, an SRT stream plays over SRT only: an RTMP
	// player, which would get the GOP cache at once, gets nothing.
	check.Case(t, "ToRtmpOff", check.Normal, func(c *check.C) {
		stream := streamName(c)
		play := srtPlayerAt(c, srt.GoSRT, srs, srtLocal(first), srt.Options{StreamID: srtID("live/"+stream, "request")}, stream)
		publishSRTAt(c, srt.GoSRT, srtLocal(first), publish(stream), media.H264HighAACTS, srtTransportMessage)
		checkSRTPlayed(c, srtAnalyze(c, play))

		ctx, cancel := context.WithTimeout(context.Background(), 200*time.Millisecond)
		defer cancel()
		r, err := flv.PlayRTMP(ctx, rtmpURL(srs, "live/"+stream), nil, report.Until{Keyframes: 1})
		c.Check(err != nil && (r == nil || r.Video == nil), "an RTMP player got %v, %v; want no video", r, err)
	})
}

// TestTimer_Srt_Transport checks FFmpeg on the SrtTransport server, in the
// timer process, since FFmpeg takes about 1s to connect and 1s to close: its
// payload_size sets the messages it publishes, which must fit SRS's MSS, and
// it plays what SRS relays.
func TestTimer_Srt_Transport(t *testing.T) {
	t.Parallel()
	cfg, second := srtTransportConfig(t)
	srs := server.Group(t, cfg)
	first := srs.Ports[server.SRT]
	ff := srt.FFmpeg

	check.Case(t, "FfmpegPublishes", check.Timer, func(c *check.C) {
		stream := streamName(c)
		play := srtPlayerAt(c, srt.GoSRT, srs, srtLocal(first), srt.Options{StreamID: srtID("live/"+stream, "request")}, stream)
		publishSRTAt(c, ff, srtLocal(second), srt.Options{StreamID: srtID("live/"+stream, "publish"), PayloadSize: srtTransportMessage}, media.H264HighAACTS, srt.MessageSize)
		checkSRTPlayed(c, srtAnalyzeUntil(c, play, srtPlayedUntil, 3*time.Second))
	})

	check.Case(t, "FfmpegPlays", check.Timer, func(c *check.C) {
		stream := streamName(c)
		play := srtPlayerAt(c, ff, srs, srtLocal(first), srt.Options{StreamID: srtID("live/"+stream, "request")}, stream)
		publishSRTAt(c, srt.GoSRT, srtLocal(second), srt.Options{StreamID: srtID("live/"+stream, "publish")}, media.H264HighAACTS, srtTransportMessage)
		checkSRTPlayed(c, srtAnalyzeUntil(c, play, srtPlayedUntil, 3*time.Second))
	})
}

// TestSrt_TsbpdOff checks tsbpdmode off: SRS delivers what a caller sends as
// it comes, holding it for no latency, whatever the caller asks for, while
// the caller still holds what SRS sends for its own. gosrt, which needs the
// peer to receive by TSBPD, refuses the handshake; zsiec tells only the
// latencies it asked for, so only libsrt shows the delays.
func TestSrt_TsbpdOff(t *testing.T) {
	t.Parallel()
	srs := server.Group(t, server.WithSRT(server.Config{Listeners: []server.Listener{server.HTTPAPI}, Hooks: true},
		server.SRTOptions{TSBPDOff: true}))

	check.Case(t, "NoDelay_libsrt", check.Normal, func(c *check.C) {
		conn := dialSRTAt(c, srt.Libsrt, srs.Addr(server.SRT), srt.Options{StreamID: srtID("live/"+streamName(c), "publish"), Latency: 200 * time.Millisecond})
		recv, send := conn.TSBPD()
		c.CheckEqual(recv, 200*time.Millisecond, "the caller's receive delay")
		c.CheckEqual(send, time.Duration(0), "SRS's receive delay")
	})

	for _, cl := range []srt.Client{srt.Libsrt, srt.Zsiec} {
		check.Case(t, "PublishPlay_"+cl.Name(), check.Normal, func(c *check.C) {
			stream := streamName(c)
			opts := func(mode string) srt.Options {
				return srt.Options{StreamID: srtID("live/"+stream, mode), Latency: 200 * time.Millisecond}
			}
			play := srtPlayerAt(c, cl, srs, srs.Addr(server.SRT), opts("request"), stream)
			publishSRTAt(c, cl, srs.Addr(server.SRT), opts("publish"), media.H264HighAACTS, srt.MessageSize)
			checkSRTPlayed(c, srtAnalyze(c, play))
		})
	}

	check.Case(t, "GosrtRefuses", check.Normal, func(c *check.C) {
		checkSRTFlagsRefused(c, srs)
	})
}

// sharedSrtTlpktdropOffConfig is the config of the SrtTlpktdropOff server:
// tlpktdrop off, which the cases of packets too late to play share.
func sharedSrtTlpktdropOffConfig() server.Config {
	return server.WithSRT(server.Config{Listeners: []server.Listener{server.HTTPAPI}, Hooks: true},
		server.SRTOptions{TLPktDropOff: true})
}

// TestSrt_TlpktdropOff checks that SRS with tlpktdrop off takes callers that
// leave the drop to it, and that gosrt, which needs the peer to drop packets
// too late to play, refuses the handshake.
func TestSrt_TlpktdropOff(t *testing.T) {
	t.Parallel()
	srs := server.Shared(t, "SrtTlpktdropOff", sharedSrtTlpktdropOffConfig())

	for _, cl := range []srt.Client{srt.Libsrt, srt.Zsiec} {
		check.Case(t, "PublishPlay_"+cl.Name(), check.Normal, func(c *check.C) {
			stream := streamName(c)
			play := srtPlayerAt(c, cl, srs, srs.Addr(server.SRT), srt.Options{StreamID: srtID("live/"+stream, "request")}, stream)
			publishSRTAt(c, cl, srs.Addr(server.SRT), srt.Options{StreamID: srtID("live/"+stream, "publish")}, media.H264HighAACTS, srt.MessageSize)
			checkSRTPlayed(c, srtAnalyze(c, play))
		})
	}

	check.Case(t, "GosrtRefuses", check.Normal, func(c *check.C) {
		checkSRTFlagsRefused(c, srs)
	})
}

// TestSrt_VhostOff checks SRT off on the vhost, with the listener on: SRS
// takes the handshake, then closes the connection, before any hook.
func TestSrt_VhostOff(t *testing.T) {
	t.Parallel()
	srs := server.Group(t, server.WithSRT(server.Config{Listeners: []server.Listener{server.HTTPAPI}, Hooks: true},
		server.SRTOptions{VhostOff: true}))
	checkSRTVhostRefuses(t, srs)
}

// TestSrt_Edge checks SRT on an edge vhost, which SRS turns off: SRS takes
// the handshake, then closes the connection, before any hook, and never
// asks the origin.
func TestSrt_Edge(t *testing.T) {
	t.Parallel()
	cfg := server.WithEdge(server.Config{Listeners: []server.Listener{server.HTTPAPI}, Hooks: true}, server.Edge{Origins: []string{server.DownAddr(t)}})
	srs := server.Group(t, server.WithSRT(cfg, server.SRTOptions{}))
	checkSRTVhostRefuses(t, srs)
}

// srtCloseBug is F2: SRS closes a caller it refuses after the handshake, and
// in some runs the caller sees no shutdown, so libsrt goes on sending into
// nothing until its own break, about 5s; gosrt, by its idle probe, ends
// sooner. It is a race, so a case marks it only when it sees it.
const srtCloseBug = "F2 a caller SRS closes after the handshake sees no shutdown"

// checkSRTVhostRefuses adds the cases of a server whose vhost refuses SRT:
// a publisher and a player of gosrt, and a publisher of libsrt, are closed
// after the handshake, with no hook.
func checkSRTVhostRefuses(t *testing.T, srs *server.Server) {
	for _, cl := range []srt.Client{srt.GoSRT, srt.Libsrt} {
		for _, mode := range []string{"publish", "request"} {
			if cl == srt.Libsrt && mode == "request" {
				continue
			}
			check.Case(t, "Closes_"+mode+"_"+cl.Name(), check.Normal, func(c *check.C) {
				stream := streamName(c)
				opts := srt.Options{StreamID: srtID("live/"+stream, mode), PeerIdleTimeout: srtProbeIdle}
				if cl == srt.GoSRT {
					conn := dialSRTAt(c, cl, srs.Addr(server.SRT), opts)
					ctx, cancel := context.WithTimeout(context.Background(), 400*time.Millisecond)
					defer cancel()
					c.CheckNoError(conn.WaitClosedProbing(ctx, 50*time.Millisecond), "the server closes the connection")
				} else {
					// libsrt needs no probe to see a shutdown; it sends nothing,
					// and holds for 20ms, so its close at the timeout is quick.
					opts.Latency = 20 * time.Millisecond
					conn := dialSRTAt(c, cl, srs.Addr(server.SRT), opts)
					ctx, cancel := context.WithTimeout(context.Background(), 200*time.Millisecond)
					defer cancel()
					err := conn.WaitClosed(ctx)
					if err != nil {
						check.KnownBug(c, srtCloseBug)
					}
					c.CheckNoError(err, "the server closes the connection")
				}
				c.CheckEqual(len(srs.Hooks.Requests(mock.Match{Stream: stream})), 0, "hooks for the stream")
			})
		}
	}
}

// checkSRTFlagsRefused checks that gosrt refuses a server that turns
// tsbpdmode or tlpktdrop off: it fails the handshake for the flags, not for
// a reason SRS sends.
func checkSRTFlagsRefused(c *check.C, srs *server.Server) {
	c.Helper()
	ctx, cancel := context.WithTimeout(context.Background(), time.Second)
	defer cancel()
	conn, err := srt.GoSRT.Dial(ctx, srs.Addr(server.SRT), srt.Options{StreamID: srtID("live/"+streamName(c), "request")})
	if conn != nil {
		conn.Close()
	}
	var rej *srt.RejectError
	c.Check(err != nil && !errors.As(err, &rej), "dial: %v, want gosrt to refuse the flags", err)
}

// srtLocal is the IPv4 loopback address of a port.
func srtLocal(port int) string {
	return net.JoinHostPort("127.0.0.1", strconv.Itoa(port))
}

// dialSRTAt calls an SRT address with a library; the connection is closed
// when the case ends.
func dialSRTAt(c *check.C, cl srt.Client, addr string, opts srt.Options) *srt.Conn {
	c.Helper()
	ctx, cancel := context.WithTimeout(context.Background(), time.Second)
	defer cancel()
	conn, err := cl.Dial(ctx, addr, opts)
	c.Must(err, cl.Name()+" dial "+addr+" with streamid "+opts.StreamID)
	c.Cleanup(func() { conn.Close() })
	return conn
}

// srtPlayerAt is srtPlayer with a library and an address.
func srtPlayerAt(c *check.C, cl srt.Client, srs *server.Server, addr string, opts srt.Options, stream string) *srt.Conn {
	c.Helper()
	conn := dialSRTAt(c, cl, addr, opts)
	waitHook(c, srs, "on_play", stream)
	return conn
}

// publishSRTAt is startSRTPublisher with a library, an address, and
// messages of size bytes.
func publishSRTAt(c *check.C, cl srt.Client, addr string, opts srt.Options, file string, size int) *srt.Conn {
	c.Helper()
	return publishSRTLeadAt(c, cl, addr, opts, file, size, srtLead)
}

// publishSRTLeadAt is publishSRTAt with lead of the file at once.
func publishSRTLeadAt(c *check.C, cl srt.Client, addr string, opts srt.Options, file string, size int, lead time.Duration) *srt.Conn {
	c.Helper()
	conn := dialSRTAt(c, cl, addr, opts)
	f, err := os.Open(media.Path(c, file))
	c.Must(err, "open the media")
	s := srt.NewSenderSize(f, size)
	if err := s.SendThrough(conn, lead); err != nil {
		f.Close()
		c.Fatalf("%v publish the lead: %v", cl.Name(), err)
	}
	ctx, cancel := context.WithCancel(context.Background())
	done := make(chan error, 1)
	go func() { done <- s.Stream(ctx, conn, nil, nil) }()
	c.Cleanup(func() {
		cancel()
		conn.Close()
		<-done
		f.Close()
	})
	return conn
}

// srtTimedRead reads a player until it has n bytes, and is how long that took
// from its first byte.
type srtTimedRead struct {
	done chan struct{}
	d    time.Duration
	err  error
}

// readSRTTimed starts reading n bytes of a player.
func readSRTTimed(play *srt.Conn, n int) *srtTimedRead {
	r := &srtTimedRead{done: make(chan struct{})}
	go func() {
		defer close(r.done)
		buf := make([]byte, 64*1024)
		var first time.Time
		for got := 0; got < n; {
			k, err := play.Read(buf)
			if err != nil {
				r.err = err
				return
			}
			if got == 0 {
				first = time.Now()
			}
			got += k
		}
		r.d = time.Since(first)
	}()
	return r
}

// wait waits up to 1s, scaled with the limits, for the read to end.
func (r *srtTimedRead) wait(c *check.C) time.Duration {
	c.Helper()
	select {
	case <-r.done:
	case <-time.After(check.Scaled(time.Second)):
		c.Fatalf("the player got too little in %v", check.Scaled(time.Second))
	}
	c.Must(r.err, "read the player")
	return r.d
}
