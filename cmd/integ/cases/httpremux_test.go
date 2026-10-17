// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package cases

import (
	"context"
	"crypto/tls"
	"fmt"
	"net/http"
	pathpkg "path"
	"strings"
	"sync"
	"testing"
	"time"

	"srs-integ/internal/audio"
	"srs-integ/internal/check"
	"srs-integ/internal/flv"
	"srs-integ/internal/httpplay"
	"srs-integ/internal/media"
	"srs-integ/internal/mock"
	"srs-integ/internal/report"
	"srs-integ/internal/rtmp"
	"srs-integ/internal/server"
	"srs-integ/internal/ts"
)

// A remuxMount is how a case plays one of the HTTP remux mounts other than
// FLV: its extension, the content type of its answer, the test file that has
// its codecs, when a player has seen it play, and the checks of that report.
type remuxMount struct {
	ext, contentType string
	file             string
	until            report.Until
	check            func(c *check.C, r *report.Report)
}

var (
	// tsMount plays HTTP-TS. TS has no metadata, and carries the sequence
	// headers in band, which the report counts once while they don't change.
	tsMount = remuxMount{"ts", "video/MP2T", media.H264HighAAC,
		report.Until{VideoHeader: true, AudioHeader: true, Keyframes: 1, VideoFrames: 5},
		func(c *check.C, r *report.Report) { checkPlayed(c, r, "h264", "aac") }}
	// aacMount plays the audio of a stream as raw AAC, ADTS frames.
	aacMount = remuxMount{"aac", "audio/x-aac", media.H264HighAAC,
		report.Until{AudioHeader: true, AudioFrames: 5},
		func(c *check.C, r *report.Report) { checkPlayedAudio(c, r, "aac", 1) }}
	// mp3Mount plays the audio of a stream as MP3, which has no sequence
	// header.
	mp3Mount = remuxMount{"mp3", "audio/mpeg", media.H264HighMP3,
		report.Until{AudioFrames: 5},
		func(c *check.C, r *report.Report) { checkPlayedAudio(c, r, "mp3", 0) }}
)

// remuxGroup is the config of a remux mount's variant: RemuxConfig with the
// hook mock, which the cases read the players from.
func remuxGroup(ext string, r server.Remux) server.Config {
	cfg := server.RemuxVariant(ext, r)
	cfg.Hooks = true
	return cfg
}

// A remuxPlayer plays an HTTP-TS, AAC or MP3 stream and adds what it reads
// to its report.
type remuxPlayer struct {
	responder
	close func() error
	// tls is the TLS state of the connection, nil over http://.
	tls func() *tls.ConnectionState
	// next reads one PES or frame and adds it to the report.
	next   func() error
	Report *report.Report
	// Programs are the codecs of the PMT of each PES an HTTP-TS player read.
	Programs [][]string
}

// remuxPlayerOf sends a request for a path on the server, which ends in .ts,
// .aac, .mp3 or .flv, and returns the player, closed when the case ends,
// after it waits for SRS to drop it like httpFlvPlayer. A player with a TLS
// config asks the HTTPS server.
func remuxPlayerOf(c *check.C, srs *server.Server, path string, opts *httpplay.Options) *remuxPlayer {
	c.Helper()
	ctx, cancel := context.WithTimeout(context.Background(), time.Second)
	defer cancel()
	url := streamURL(srs, path, opts)
	p := &remuxPlayer{}
	switch pathpkg.Ext(strings.SplitN(path, "?", 2)[0]) {
	case ".flv":
		hp, err := flv.DialHTTP(ctx, url, opts)
		c.Must(err, "dial the HTTP-FLV player")
		var a flv.Analyzer
		p.responder, p.close, p.tls, p.Report = hp, hp.Close, hp.TLS, &a.Report
		p.next = func() error {
			tag, err := hp.ReadTag()
			if err != nil {
				return err
			}
			return a.Add(tag)
		}
	case ".ts":
		hp, err := ts.DialHTTP(ctx, url, opts)
		c.Must(err, "dial the HTTP-TS player")
		var a ts.Analyzer
		p.responder, p.close, p.tls, p.Report = hp, hp.Close, hp.TLS, &a.Report
		p.next = func() error {
			pes, err := hp.Read()
			if err != nil {
				return err
			}
			p.Programs = append(p.Programs, pes.Program)
			return a.Add(pes)
		}
	default:
		hp, err := audio.DialHTTP(ctx, url, opts)
		c.Must(err, "dial the HTTP audio player")
		var a audio.Analyzer
		p.responder, p.close, p.tls, p.Report = hp, hp.Close, hp.TLS, &a.Report
		p.next = func() error {
			f, err := hp.Read()
			if err != nil {
				return err
			}
			a.Add(f)
			return nil
		}
	}
	c.Cleanup(func() {
		p.close()
		if srs.Hooks != nil {
			untilPlayersGone(srs, p, path)
		}
	})
	return p
}

// readUntil reads until the report meets u, for at most timeout.
func (p *remuxPlayer) readUntil(u report.Until, timeout time.Duration) error {
	p.SetDeadline(time.Now().Add(timeout))
	defer p.SetDeadline(time.Time{})
	for !u.Done(p.Report) {
		if err := p.next(); err != nil {
			return fmt.Errorf("%w, before %v: %v", err, u, p.Report)
		}
	}
	return nil
}

// readNothing checks that nothing comes for d: no media, and no end.
func (p *remuxPlayer) readNothing(d time.Duration) error {
	p.SetDeadline(time.Now().Add(d))
	defer p.SetDeadline(time.Time{})
	if err := p.next(); !isTimeout(err) {
		return fmt.Errorf("read %v, want nothing for %v: %v", err, d, p.Report)
	}
	return nil
}

// untilEnds reads until the stream ends, for at most d.
func (p *remuxPlayer) untilEnds(d time.Duration) error {
	p.SetDeadline(time.Now().Add(d))
	defer p.SetDeadline(time.Time{})
	for {
		err := p.next()
		if isTimeout(err) {
			return fmt.Errorf("the stream didn't end within %v", d)
		} else if err != nil {
			return nil
		}
	}
}

// remuxPlay plays a path on the server until m.until, for at most 1s, and
// returns the player.
func remuxPlay(c *check.C, srs *server.Server, path string, opts *httpplay.Options, m remuxMount) *remuxPlayer {
	c.Helper()
	p := remuxPlayerOf(c, srs, path, opts)
	c.Must(p.readUntil(m.until, time.Second), "play")
	return p
}

// remuxCases adds the cases every remux mount shares, on the mount's
// variant srs: the request and the mount, the lifecycle and bad input of the
// protocol checklist. Each publishes in real time, joining 200ms in, because
// the audio mounts have no cache for a stream that is held.
func remuxCases(t *testing.T, srs *server.Server, m remuxMount) {
	ext := "." + m.ext
	// publish publishes the mount's file in real time on a stream of its
	// own, and returns the stream's name and path.
	publish := func(c *check.C, app string) (name, stream string, pub *rtmp.Client) {
		name = streamName(c)
		stream = app + "/" + name
		pub = startPublisher(c, rtmpURL(srs, stream), media.Path(c, m.file), 200*time.Millisecond)
		return name, stream, pub
	}

	check.Case(t, "ResponseHeaders", check.Normal, func(c *check.C) {
		_, stream, _ := publish(c, "live")
		p := remuxPlay(c, srs, stream+ext, nil, m)
		res := httpResponse(c, p)
		c.CheckEqual(res.StatusCode, 200, "status")
		c.CheckEqual(res.Header.Get("Content-Type"), m.contentType, "content type")
		m.check(c, p.Report)
	})

	// MultiLevelApp plays live/a/b/<stream>: the [app] of the mount spans the
	// folders, as the app of the publisher does.
	check.Case(t, "MultiLevelApp", check.Normal, func(c *check.C) {
		name, stream, _ := publish(c, "live/a/b")
		m.check(c, remuxPlay(c, srs, stream+ext, nil, m).Report)
		c.CheckEqual(waitHook(c, srs, "on_play", name).Field("app"), "live/a/b", "on_play app")
	})

	// ParamInUrl plays with a query: on_play has it as its param.
	check.Case(t, "ParamInUrl", check.Normal, func(c *check.C) {
		name, stream, _ := publish(c, "live")
		m.check(c, remuxPlay(c, srs, stream+ext+"?token=abc", nil, m).Report)
		h := waitHook(c, srs, "on_play", name)
		c.CheckEqual(h.Field("param"), "?token=abc", "on_play param")
		c.CheckEqual(h.Field("stream"), name, "on_play stream")
	})

	// VhostFromHost plays with a Host header naming a vhost SRS doesn't have:
	// the request goes to the default vhost, where the stream is.
	check.Case(t, "VhostFromHost", check.Normal, func(c *check.C) {
		name, stream, _ := publish(c, "live")
		m.check(c, remuxPlay(c, srs, stream+ext, &httpplay.Options{Host: "example.com"}, m).Report)
		c.CheckEqual(waitHook(c, srs, "on_play", name).Field("vhost"), "__defaultVhost__", "on_play vhost")
	})

	// NoAppNotFound asks for /<stream><ext>, with no app: 404, though
	// live/<stream> is published.
	check.Case(t, "NoAppNotFound", check.Normal, func(c *check.C) {
		name, _, _ := publish(c, "live")
		c.CheckEqual(httpResponse(c, remuxPlayerOf(c, srs, name+ext, nil)).StatusCode, 404, "status")
	})

	// WrongExtensionNotFound asks for a published stream as .flv, which the
	// mount doesn't serve.
	check.Case(t, "WrongExtensionNotFound", check.Normal, func(c *check.C) {
		_, stream, _ := publish(c, "live")
		checkHTTPStatus(c, srs, stream+".flv", nil, 404)
	})

	// OnPlayRejected has the on_play hook reject the player: 401.
	check.Case(t, "OnPlayRejected", check.Normal, func(c *check.C) {
		name := streamName(c)
		srs.Hooks.Script(mock.Match{Action: "on_play", Stream: name}, mock.Reject(403))
		startPublisher(c, rtmpURL(srs, "live/"+name), media.Path(c, m.file), 200*time.Millisecond)
		c.CheckEqual(httpResponse(c, remuxPlayerOf(c, srs, "live/"+name+ext, nil)).StatusCode, 401, "status")
		c.CheckEqual(len(srs.Hooks.Requests(mock.Match{Action: "on_play", Stream: name})), 1, "on_play requests")
	})

	// XForwardedFor plays through a proxy that sets X-Forwarded-For: the
	// first address is the client's IP, in on_play and in the API.
	check.Case(t, "XForwardedFor", check.Normal, func(c *check.C) {
		name, stream, _ := publish(c, "live")
		opts := &httpplay.Options{Header: http.Header{"X-Forwarded-For": {"10.1.2.3, 10.0.0.1"}}}
		remuxPlay(c, srs, stream+ext, opts, m)
		h := waitHook(c, srs, "on_play", name)
		c.CheckEqual(h.Field("ip"), "10.1.2.3", "on_play ip")
		var v struct {
			Client struct {
				IP string `json:"ip"`
			} `json:"client"`
		}
		apiGet(c, srs, "/api/v1/clients/"+h.Field("client_id"), &v)
		c.CheckEqual(v.Client.IP, "10.1.2.3", "the client's ip in the API")
	})

	// CorsHeaders plays with an Origin header: crossdomain is on by default,
	// so the answer allows any origin.
	check.Case(t, "CorsHeaders", check.Normal, func(c *check.C) {
		_, stream, _ := publish(c, "live")
		p := remuxPlay(c, srs, stream+ext, &httpplay.Options{Header: originHeader}, m)
		c.CheckEqual(httpResponse(c, p).Header.Get("Access-Control-Allow-Origin"), "*", "Access-Control-Allow-Origin")
	})

	// PlayBeforePublish asks for the stream before it is published: the
	// player gets it when the publisher comes.
	check.Case(t, "PlayBeforePublish", check.Normal, func(c *check.C) {
		stream := "live/" + streamName(c)
		p := remuxPlayerOf(c, srs, stream+ext, nil)
		startPublisher(c, rtmpURL(srs, stream), media.Path(c, m.file), 0)
		c.Must(p.readUntil(m.until, time.Second), "play")
		m.check(c, p.Report)
	})

	// RepublishPlays unpublishes and publishes the stream again: a new player
	// plays the new publish. SRS refuses a publisher while it still tears
	// down the old HTTP mount, so the publisher retries, as an encoder does.
	check.Case(t, "RepublishPlays", check.Normal, func(c *check.C) {
		name, stream, pub := publish(c, "live")
		pub.Close()
		waitHook(c, srs, "on_unpublish", name)

		pub = republish(c, rtmpURL(srs, stream), 300*time.Millisecond)
		streamFile(c, pub, media.Path(c, m.file), 200*time.Millisecond)
		m.check(c, remuxPlay(c, srs, stream+ext, nil, m).Report)
	})

	check.Case(t, "ManyPlayers", check.Normal, func(c *check.C) {
		_, stream, _ := publish(c, "live")
		const n = 8
		players := make([]*remuxPlayer, n)
		for i := range n {
			players[i] = remuxPlayerOf(c, srs, stream+ext, nil)
		}
		errs := make([]error, n)
		var wg sync.WaitGroup
		for i := range n {
			wg.Add(1)
			go func() {
				defer wg.Done()
				errs[i] = players[i].readUntil(m.until, time.Second)
			}()
		}
		wg.Wait()
		for i := range n {
			if c.CheckNoError(errs[i], fmt.Sprintf("player %d", i)) {
				m.check(c, players[i].Report)
			}
		}
	})

	// PlayerCloseCleanup closes a player: its client is gone from the API,
	// and the stream counts only the publisher again.
	check.Case(t, "PlayerCloseCleanup", check.Normal, func(c *check.C) {
		name, stream, _ := publish(c, "live")
		p := remuxPlay(c, srs, stream+ext, nil, m)
		h := waitHook(c, srs, "on_play", name)
		clientPath, streamPath := "/api/v1/clients/"+h.Field("client_id"), "/api/v1/streams/"+h.Field("stream_id")
		c.CheckEqual(apiCode(c, srs, clientPath), 0, "the player in the API")
		c.CheckEqual(streamClients(c, srs, streamPath), 2, "stream clients with a player")

		p.close()
		waitHook(c, srs, "on_stop", name)
		c.CheckNoError(untilGone(c, srs, clientPath), "the player after it closed")
		c.CheckEqual(streamClients(c, srs, streamPath), 1, "stream clients after the player closed")
	})

	// UnpublishEndsStream closes the publisher: the stream of a player ends,
	// and on_stop comes.
	check.Case(t, "UnpublishEndsStream", check.Normal, func(c *check.C) {
		name, stream, pub := publish(c, "live")
		p := remuxPlay(c, srs, stream+ext, nil, m)
		pub.Close()
		c.CheckNoError(p.untilEnds(time.Second), "the player after the unpublish")
		waitHook(c, srs, "on_stop", name)
	})

	// CloseBeforePublish asks for a stream nobody publishes, then closes: the
	// waiting player is gone from the API, and the stream plays for the next
	// player once published.
	check.Case(t, "CloseBeforePublish", check.Normal, func(c *check.C) {
		name := streamName(c)
		stream := "live/" + name
		p := remuxPlayerOf(c, srs, stream+ext, nil)
		h := waitHook(c, srs, "on_play", name)
		clientPath := "/api/v1/clients/" + h.Field("client_id")
		p.close()
		c.CheckNoError(untilGone(c, srs, clientPath), "the player after it closed")

		startPublisher(c, rtmpURL(srs, stream), media.Path(c, m.file), 200*time.Millisecond)
		m.check(c, remuxPlay(c, srs, stream+ext, nil, m).Report)
	})
}

// republish dials and publishes, retrying every 10ms for at most within
// while SRS refuses the publisher, and returns the client, closed when the
// case ends.
func republish(c *check.C, url string, within time.Duration) *rtmp.Client {
	c.Helper()
	deadline := time.Now().Add(within)
	for tries := 1; ; tries++ {
		pub, err := tryPublish(url)
		if err == nil {
			c.Cleanup(func() { pub.Close() })
			return pub
		}
		if time.Now().After(deadline) {
			c.Fatalf("publish refused %d times within %v: %v", tries, within, err)
		}
		time.Sleep(10 * time.Millisecond)
	}
}
