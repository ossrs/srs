// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package cases

import (
	"context"
	"os"
	"strings"
	"testing"
	"time"

	"srs-integ/internal/check"
	"srs-integ/internal/flv"
	"srs-integ/internal/media"
	"srs-integ/internal/report"
	"srs-integ/internal/server"
	"srs-integ/internal/srt"
)

// srtInteropClients are the libraries of the interop matrix, as publishers
// and as players: the two in-process Go libraries, libsrt v1.5.7, FFmpeg, and
// srt-live-transmit v1.5.7 in Docker.
var srtInteropClients = []srt.Client{srt.GoSRT, srt.Zsiec, srt.Libsrt, srt.FFmpeg, srt.LiveTransmit}

// srtInteropSlow tells whether a library takes seconds to connect or close:
// FFmpeg reads 1s of media before it connects and takes 1s to close, and a
// container takes a second to start. A case with one is Timer class.
func srtInteropSlow(cl srt.Client) bool { return cl == srt.FFmpeg || cl == srt.LiveTransmit }

// srtInteropWithin is how long a case with a slow library waits for each
// step, to dial, publish and play: FFmpeg plays about 3.8s after its
// publisher starts.
const srtInteropWithin = 5 * time.Second

// An srtInteropMode is how each pair connects: plain, or encrypted, the
// publisher with a key of 16 bytes and the player of 32, so SRS decrypts and
// encrypts each connection with its own key.
type srtInteropMode struct {
	name                    string
	srsOf                   func(c *check.C) *server.Server
	publisherOpts, playOpts func(stream string) srt.Options
}

// srtInteropModes are the modes of a group, on the Common server, which
// converts SRT to RTMP, and on enc, a server of the group with a passphrase.
func srtInteropModes(enc *server.Server) []srtInteropMode {
	opts := func(passphrase string, keylen int, mode string) func(stream string) srt.Options {
		return func(stream string) srt.Options {
			return srt.Options{StreamID: srtID("live/"+stream, mode), Passphrase: passphrase, PBKeyLen: keylen}
		}
	}
	return []srtInteropMode{
		{
			name: "Plain", srsOf: func(c *check.C) *server.Server { return server.Common(c) },
			publisherOpts: opts("", 0, "publish"), playOpts: opts("", 0, "request"),
		},
		{
			name: "Aes", srsOf: func(c *check.C) *server.Server { return enc },
			publisherOpts: opts(srtPassphrase, 16, "publish"), playOpts: opts(srtPassphrase, 32, "request"),
		},
	}
}

// srtInteropConfig is the config of the encrypted server of a group: SRT
// with the passphrase and a key of 16 bytes, and hooks.
func srtInteropConfig() server.Config {
	return server.WithSRT(server.Config{Listeners: []server.Listener{server.HTTPAPI}, Hooks: true}, server.SRTOptions{
		Passphrase: srtPassphrase,
		PBKeyLen:   16,
	})
}

// TestSrt_Interop runs each pair of gosrt, zsiec and libsrt v1.5.7, as the
// publisher and the player of a stream through SRS, plain and encrypted, and
// each of them as the publisher of a stream played over RTMP and HTTP-FLV:
// the player gets the test file's codecs and frames.
func TestSrt_Interop(t *testing.T) {
	t.Parallel()
	srtInteropCases(t, server.Group(t, srtInteropConfig()), false, check.Normal)
}

// TestTimer_Srt_Interop is TestSrt_Interop for the pairs with FFmpeg or
// srt-live-transmit, which take seconds: the pairs of srt-live-transmit run
// with SRT_INTEROP=on, as the cross-check's do.
func TestTimer_Srt_Interop(t *testing.T) {
	t.Parallel()
	srtInteropCases(t, server.Group(t, srtInteropConfig()), true, check.Timer)
}

// srtInteropCases adds the cases whose pair has a slow library, or none.
func srtInteropCases(t *testing.T, enc *server.Server, slow bool, class check.Class) {
	modes := srtInteropModes(enc)
	for _, pub := range srtInteropClients {
		for _, player := range srtInteropClients {
			if (srtInteropSlow(pub) || srtInteropSlow(player)) != slow {
				continue
			}
			for _, m := range modes {
				check.Case(t, pub.Name()+"To"+srtInteropTitle(player)+"_"+m.name, class, func(c *check.C) {
					srtInteropNeed(c, pub, player)
					srs := m.srsOf(c)
					stream := streamName(c)
					addr := srs.Addr(server.SRT)
					play := dialSRTInterop(c, player, addr, m.playOpts(stream))
					waitHookWithin(c, srs, "on_play", stream, srtInteropStep(player))
					publishSRTInterop(c, pub, addr, m.publisherOpts(stream))
					checkSRTPlayed(c, srtAnalyzeUntil(c, play, srtPlayedUntil, srtInteropStep(pub, player)))
				})
			}
		}
	}

	m := modes[0]
	for _, pub := range srtInteropClients {
		if srtInteropSlow(pub) != slow {
			continue
		}
		for _, proto := range []string{"Rtmp", "HttpFlv"} {
			check.Case(t, pub.Name()+"To"+proto, class, func(c *check.C) {
				srtInteropNeed(c, pub)
				srs := m.srsOf(c)
				stream := streamName(c)
				publishSRTInterop(c, pub, srs.Addr(server.SRT), m.publisherOpts(stream))
				waitHookWithin(c, srs, "on_publish", stream, srtInteropStep(pub))
				w := convertWant{video: "h264", audio: "aac"}
				ctx, cancel := context.WithTimeout(context.Background(), check.Scaled(srtInteropStep(pub)))
				defer cancel()
				path := "live/" + stream
				var r *report.Report
				var err error
				if proto == "Rtmp" {
					r, err = flv.PlayRTMP(ctx, rtmpURL(srs, path), nil, convertUntil(w))
				} else {
					r, err = flv.PlayHTTP(ctx, httpClient, flvURL(srs, path+".flv"), convertUntil(w))
				}
				c.Must(err, "play "+proto)
				checkConverted(c, r, w, 100*time.Millisecond, srtSync)
			})
		}
	}
}

// srtInteropTitle is a library's name with its first letter upper case.
func srtInteropTitle(cl srt.Client) string {
	return strings.ToUpper(cl.Name()[:1]) + cl.Name()[1:]
}

// srtInteropNeed skips a case of srt-live-transmit unless the interop image
// runs: SRT_INTEROP=on and the image. FFmpeg and libsrt fail with how to get
// them, when they are missing, as their other cases do.
func srtInteropNeed(c *check.C, cls ...srt.Client) {
	c.Helper()
	for _, cl := range cls {
		if cl != srt.LiveTransmit {
			continue
		}
		if err := srt.InteropReady(); err != nil {
			c.Skip(err)
		}
	}
}

// srtInteropStep is how long a step of the libraries takes at most: 1s, or
// srtInteropWithin with a slow one.
func srtInteropStep(cls ...srt.Client) time.Duration {
	for _, cl := range cls {
		if srtInteropSlow(cl) {
			return srtInteropWithin
		}
	}
	return time.Second
}

// dialSRTInterop is dialSRTAt with the time a library takes to dial.
func dialSRTInterop(c *check.C, cl srt.Client, addr string, opts srt.Options) *srt.Conn {
	c.Helper()
	ctx, cancel := context.WithTimeout(context.Background(), check.Scaled(srtInteropStep(cl)))
	defer cancel()
	conn, err := cl.Dial(ctx, addr, opts)
	c.Must(err, cl.Name()+" dial "+addr+" with streamid "+opts.StreamID)
	c.Cleanup(func() { conn.Close() })
	return conn
}

// publishSRTInterop is publishSRTLeadAt with the time a library takes to
// dial: the TS test file in messages of 1316 bytes, srtLead of it at once,
// then the rest in real time until the case ends.
func publishSRTInterop(c *check.C, cl srt.Client, addr string, opts srt.Options) {
	c.Helper()
	conn := dialSRTInterop(c, cl, addr, opts)
	f, err := os.Open(media.Path(c, media.H264HighAACTS))
	c.Must(err, "open the media")
	s := srt.NewSender(f)
	if err := s.SendThrough(conn, srtLead); err != nil {
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
}
