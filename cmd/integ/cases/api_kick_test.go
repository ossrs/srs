// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package cases

import (
	"context"
	"fmt"
	"io"
	"net/http"
	"os"
	"strings"
	"sync"
	"testing"
	"time"

	"srs-integ/internal/check"
	"srs-integ/internal/media"
	"srs-integ/internal/rtmp"
	"srs-integ/internal/rtsp"
	"srs-integ/internal/server"
	"srs-integ/internal/srt"
)

// kickWithin is how soon a client is gone, and closed, after its DELETE.
const kickWithin = 300 * time.Millisecond

// TestApi_Kick checks DELETE /api/v1/clients/{id} of RTMP and RTMPS clients,
// on a server of their own with RTMP, RTMPS, hooks and the lowest play
// latency, as Common's: DeletePlayersSending loads it with players of a
// stream published faster than real time.
func TestApi_Kick(t *testing.T) {
	t.Parallel()
	srs := server.Group(t, server.Config{
		Listeners: []server.Listener{server.RTMP, server.HTTPAPI, server.RTMPS},
		Hooks:     true,
		Env: map[string]string{
			"SRS_VHOST_MIN_LATENCY":     "on",
			"SRS_VHOST_PLAY_MW_LATENCY": "0",
		},
	})

	for _, s := range []struct {
		name   string
		scheme bool // RTMPS
	}{{"Rtmp", false}, {"Rtmps", true}} {
		dial := func(c *check.C, srs *server.Server, stream string) (string, *rtmp.Options) {
			if !s.scheme {
				return rtmpURL(srs, "live/"+stream), nil
			}
			tlsConf, err := server.ClientTLS()
			c.Must(err, "TLS")
			return "rtmps://" + srs.Addr(server.RTMPS) + "/live/" + stream, &rtmp.Options{TLS: tlsConf}
		}

		// Delete<Scheme>Publisher: the publisher's connection is closed,
		// the stream unpublished and gone.
		check.Case(t, "Delete"+s.name+"Publisher", check.Normal, func(c *check.C) {
			stream := streamName(c)
			url, opts := dial(c, srs, stream)
			pub := startPublisherWith(c, url, media.Path(c, media.H264HighAAC), 200*time.Millisecond, opts, rtmp.FMLE)
			h := waitHook(c, srs, "on_publish", stream)
			checkKicked(c, apiKick(c, srs, h.Field("client_id")))
			c.CheckNoError(untilEOF(pub, kickWithin), "the publisher's connection closed")
			waitClientHook(c, srs, "on_unpublish", h.Field("client_id"))
			c.CheckNoError(untilGone(c, srs, "/api/v1/clients/"+h.Field("client_id")), "the publisher leaves the API")
			c.CheckNoError(untilGone(c, srs, "/api/v1/streams/"+h.Field("stream_id")), "the stream leaves the API")
		})

		// Delete<Scheme>Player: the player's connection is closed; the
		// stream goes on with its publisher.
		check.Case(t, "Delete"+s.name+"Player", check.Normal, func(c *check.C) {
			stream := streamName(c)
			url, opts := dial(c, srs, stream)
			startPublisherWith(c, url, media.Path(c, media.H264HighAAC), 200*time.Millisecond, opts, rtmp.FMLE)
			streamID := waitHook(c, srs, "on_publish", stream).Field("stream_id")
			player, _ := playRTMPClient(c, url, opts)
			id := waitHook(c, srs, "on_play", stream).Field("client_id")
			checkKicked(c, apiKick(c, srs, id))
			c.CheckNoError(untilEOF(player, kickWithin), "the player's connection closed")
			waitClientHook(c, srs, "on_stop", id)
			c.CheckNoError(untilGone(c, srs, "/api/v1/clients/"+id), "the player leaves the API")
			c.CheckEqual(streamClients(c, srs, "/api/v1/streams/"+streamID), 1, "stream clients, the publisher")
		})
	}

	// DeleteTwice deletes a client that has gone: the client's not-found
	// code.
	check.Case(t, "DeleteTwice", check.Normal, func(c *check.C) {
		_, id, _ := publishRTMP(c, srs, streamName(c), media.H264HighAAC)
		checkKicked(c, apiKick(c, srs, id))
		c.CheckNoError(untilGone(c, srs, "/api/v1/clients/"+id), "the publisher leaves the API")
		c.CheckEqual(codeOf(apiKick(c, srs, id)), 2049, "the second DELETE")
	})

	// DeletePlayersSending deletes RTMP players at once while SRS sends them a
	// stream published at kickSpeed times real time, so each player's
	// coroutine is often runnable between its sends, not blocked, when its
	// DELETE comes, kickRounds times: each DELETE answers code 0, every
	// player is closed and leaves the API, and SRS stays up.
	check.Case(t, "DeletePlayersSending", check.Normal, func(c *check.C) {
		stream := streamName(c)
		url := rtmpURL(srs, "live/"+stream)
		pub := publishOnly(c, url, rtmp.FMLE)
		streamFast(c, pub, media.Path(c, media.H264HighAAC), kickSpeed)
		streamID := waitHook(c, srs, "on_publish", stream).Field("stream_id")
		for round := 0; round < kickRounds && !c.Failed(); round++ {
			kickPlayersSending(c, srs, url, streamID)
		}
	})
}

// kickPlayers players of a stream are deleted at once, kickRounds times, and
// the stream is published at kickSpeed times real time.
const (
	kickPlayers = 20
	kickRounds  = 4
	kickSpeed   = 20
)

// kickPlayersSending plays a stream with kickPlayers RTMP players that read
// all SRS sends, deletes them at once and checks each DELETE, that SRS is up,
// and that every player is closed and leaves the API.
func kickPlayersSending(c *check.C, srs *server.Server, url, streamID string) {
	c.Helper()
	clients := make([]*rtmp.Client, kickPlayers)
	errs := make([]error, kickPlayers)
	var played sync.WaitGroup
	for i := range clients {
		played.Add(1)
		go func(i int) {
			defer played.Done()
			clients[i], errs[i] = playNow(url)
		}(i)
	}
	played.Wait()
	closed := make([]chan struct{}, kickPlayers)
	for i, player := range clients {
		if player != nil {
			c.Cleanup(func() { player.Close() })
		}
		c.Must(errs[i], "play")
		closed[i] = make(chan struct{})
		go func(done chan struct{}) {
			drainRTMP(player)
			close(done)
		}(closed[i])
	}
	var players []string
	if !c.Check(until(kickWithin, func() bool {
		players = streamPlayers(c, srs, streamID)
		return len(players) == kickPlayers
	}), "%d players listed, want %d", len(players), kickPlayers) {
		return
	}

	replies := make([]error, len(players))
	var wg sync.WaitGroup
	for i, id := range players {
		wg.Add(1)
		go func(i int, id string) {
			defer wg.Done()
			replies[i] = kickOK(srs, id)
		}(i, id)
	}
	wg.Wait()
	for i, err := range replies {
		c.CheckNoError(err, "DELETE of player "+players[i])
	}
	if code, err := srs.Wait(time.Millisecond); err == nil {
		c.Fatalf("SRS exited with %d after the DELETEs", code)
	}
	for i, done := range closed {
		select {
		case <-done:
		case <-time.After(kickWithin):
			c.Errorf("player %d not closed within %v", i, kickWithin)
		}
	}
	c.Check(until(kickWithin, func() bool { return len(streamPlayers(c, srs, streamID)) == 0 }), "the players leave the API")
}

// streamFast publishes a file on a client that publishes, speed times real
// time and again from its start at its end, until the case ends.
func streamFast(c *check.C, pub *rtmp.Client, file string, speed int) {
	c.Helper()
	ctx, cancel := context.WithCancel(context.Background())
	done := make(chan struct{})
	go func() {
		defer close(done)
		for ctx.Err() == nil {
			f, err := os.Open(file)
			if err != nil {
				return
			}
			r, err := media.NewFLVReader(f)
			if err == nil {
				start := time.Now()
				p := &rtmp.Pacer{
					Now: func() time.Time { return start.Add(time.Since(start) * time.Duration(speed)) },
					Sleep: func(ctx context.Context, d time.Duration) error {
						select {
						case <-ctx.Done():
							return ctx.Err()
						case <-time.After(d / time.Duration(speed)):
							return nil
						}
					},
				}
				_, err = rtmp.PublishFLV(ctx, pub, r, p)
			}
			f.Close()
			if err != nil {
				return
			}
		}
	}()
	c.Cleanup(func() {
		cancel()
		pub.Close()
		<-done
	})
}

// playNow plays a stream over RTMP, from any goroutine, and returns the
// player, or the client and the error when the play fails.
func playNow(url string) (*rtmp.Client, error) {
	ctx, cancel := context.WithTimeout(context.Background(), time.Second)
	defer cancel()
	player, err := rtmp.Dial(ctx, url, nil)
	if err != nil {
		return nil, err
	}
	if _, err := player.Play(ctx); err != nil {
		return player, err
	}
	return player, nil
}

// streamPlayers is the ids of the players of a stream in the API.
func streamPlayers(c *check.C, srs *server.Server, streamID string) []string {
	c.Helper()
	clients, _ := apiList(c, srs, "clients", "?count=100")
	var out []string
	for _, cl := range clients {
		if cl["stream"] == streamID && cl["publish"] == false {
			out = append(out, cl["id"].(string))
		}
	}
	return out
}

// kickOK deletes a client by the API, from any goroutine, and fails unless
// SRS answers 200 with code 0.
func kickOK(srs *server.Server, id string) error {
	req, err := http.NewRequest(http.MethodDelete, srs.APIURL("/api/v1/clients/"+id), nil)
	if err != nil {
		return err
	}
	res, err := httpClient.Do(req)
	if err != nil {
		return err
	}
	defer res.Body.Close()
	body, err := io.ReadAll(res.Body)
	if err != nil {
		return err
	}
	if res.StatusCode != http.StatusOK || !strings.Contains(string(body), `"code":0`) {
		return fmt.Errorf("status %d, answer %s, want 200 with code 0", res.StatusCode, body)
	}
	return nil
}

// apiKicks adds the cases of DELETE /api/v1/clients/{id} on the Common server
// of the clients of every protocol but RTMP, which has TestApi_Kick: SRS
// answers code 0, ends the client's session, sends its on_unpublish or
// on_stop, and the client leaves the API.
func apiKicks(t *testing.T) {
	// DeleteHttpFlvPlayer: the HTTP-FLV response ends at once, like the
	// connection of every other protocol's client.
	check.Case(t, "DeleteHttpFlvPlayer", check.Normal, func(c *check.C) {
		check.KnownBug(c, "a DELETE of an HTTP-FLV player ends it, with its on_stop and its leaving the API, only up to 3s later")
		srs := server.Common(c)
		stream := streamName(c)
		startPublisher(c, rtmpURL(srs, "live/"+stream), media.Path(c, media.H264HighAAC), 200*time.Millisecond)
		untilCodecs(c, srs, waitHook(c, srs, "on_publish", stream).Field("stream_id"))
		p := remuxPlayerOf(c, srs, "live/"+stream+".flv", nil)
		c.Must(p.readUntil(playedUntil, time.Second), "play")
		id := waitHook(c, srs, "on_play", stream).Field("client_id")
		checkKicked(c, apiKick(c, srs, id))
		if !c.CheckNoError(p.untilEnds(kickWithin), "the player's response ends") {
			return
		}
		waitClientHook(c, srs, "on_stop", id)
		c.CheckNoError(untilGone(c, srs, "/api/v1/clients/"+id), "the player leaves the API")
	})

	// DeleteHlsSession: the session leaves the API, and its playlist is 404
	// from then on; a new session plays.
	check.Case(t, "DeleteHlsSession", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		publishHLSHooked(c, srs, stream, "")
		ctx := hlsSession(c, srs, stream, "", nil)
		playlist := flvURL(srs, "live/"+stream+".m3u8?hls_ctx="+ctx)
		res, _ := httpGet(c, httpClient, playlist)
		c.CheckEqual(res.StatusCode, http.StatusOK, "the session's playlist before the DELETE")
		checkKicked(c, apiKick(c, srs, ctx))
		c.CheckNoError(untilGone(c, srs, "/api/v1/clients/"+ctx), "the session leaves the API")
		res, _ = httpGet(c, httpClient, playlist)
		c.CheckEqual(res.StatusCode, http.StatusNotFound, "the session's playlist after the DELETE")
		c.Check(hlsSession(c, srs, stream, "", nil) != ctx, "a new session got the deleted hls_ctx")
	})

	// DeleteWhipPublisher: the publisher's session ends, its peer
	// connection leaves connected, and the stream is gone.
	check.Case(t, "DeleteWhipPublisher", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		h, pub := whipStream(c, srs, stream, 200*time.Millisecond)
		checkKicked(c, apiKick(c, srs, h.client))
		waitClientHook(c, srs, "on_unpublish", h.client)
		c.CheckNoError(untilGone(c, srs, "/api/v1/clients/"+h.client), "the publisher leaves the API")
		c.CheckNoError(untilGone(c, srs, "/api/v1/streams/"+h.stream), "the stream leaves the API")
		c.CheckNoError(untilPCLeaves(pub.PC, kickWithin), "the publisher's peer connection")
	})

	// DeleteWhepPlayer: the player's session ends and its peer connection
	// leaves connected; the stream goes on.
	check.Case(t, "DeleteWhepPlayer", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		h, _ := whipStream(c, srs, stream, 200*time.Millisecond)
		player := rtcPlayer(c, whepURL(srs, "live", stream), nil)
		id := waitHook(c, srs, "on_play", stream).Field("client_id")
		checkKicked(c, apiKick(c, srs, id))
		waitClientHook(c, srs, "on_stop", id)
		c.CheckNoError(untilGone(c, srs, "/api/v1/clients/"+id), "the player leaves the API")
		c.CheckNoError(untilPCLeaves(player.PC, kickWithin), "the player's peer connection")
		c.CheckEqual(streamClients(c, srs, "/api/v1/streams/"+h.stream), 1, "stream clients, the publisher")
	})

	// DeleteSrtPublisher: the publisher's connection is closed and the
	// stream is gone.
	check.Case(t, "DeleteSrtPublisher", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		pub := startSRTPublisher(c, srs, srt.Options{StreamID: srtID("live/"+stream, "publish")}, media.H264HighAACTS)
		h := waitHook(c, srs, "on_publish", stream)
		checkKicked(c, apiKick(c, srs, h.Field("client_id")))
		c.CheckNoError(srtClosedWithin(pub, kickWithin), "the publisher's connection closed")
		waitClientHook(c, srs, "on_unpublish", h.Field("client_id"))
		c.CheckNoError(untilGone(c, srs, "/api/v1/clients/"+h.Field("client_id")), "the publisher leaves the API")
		c.CheckNoError(untilGone(c, srs, "/api/v1/streams/"+h.Field("stream_id")), "the stream leaves the API")
	})

	// DeleteSrtPlayer: the player's connection is closed.
	check.Case(t, "DeleteSrtPlayer", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		startSRTPublisher(c, srs, srt.Options{StreamID: srtID("live/"+stream, "publish")}, media.H264HighAACTS)
		waitHook(c, srs, "on_publish", stream)
		player := srtPlayer(c, srs, srt.Options{StreamID: srtID("live/"+stream, "request")}, stream)
		srtReadSome(c, player)
		id := waitHook(c, srs, "on_play", stream).Field("client_id")
		checkKicked(c, apiKick(c, srs, id))
		c.CheckNoError(srtClosedWithin(player, kickWithin), "the player's connection closed")
		waitClientHook(c, srs, "on_stop", id)
		c.CheckNoError(untilGone(c, srs, "/api/v1/clients/"+id), "the player leaves the API")
	})

	// DeleteRtspPlayer: the player leaves the API. Its connection is
	// DeleteRtspPlayerCloses's.
	check.Case(t, "DeleteRtspPlayer", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		id, _ := rtspRawPlayer(c, srs)
		checkKicked(c, apiKick(c, srs, id))
		c.CheckNoError(untilGone(c, srs, "/api/v1/clients/"+id), "the player leaves the API")
	})

	// DeleteRtspPlayerCloses: SRS closes the player's connection, as it does
	// a client of every other protocol.
	check.Case(t, "DeleteRtspPlayerCloses", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		id, conn := rtspRawPlayer(c, srs)
		checkKicked(c, apiKick(c, srs, id))
		c.CheckNoError(conn.WaitClosed(kickWithin), "the player's connection closed")
	})

	// DeleteGbPublisher deletes the RTMP client SRS publishes a GB28181
	// stream with: the stream is unpublished, and the device's media
	// connection stays. It sends no more media, which SRS would publish again.
	check.Case(t, "DeleteGbPublisher", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		s := gbStart(c, srs, media.H264HighAAC, gbSSRC())
		s.sendThrough(c, time.Second)
		h := waitHook(c, srs, "on_publish", s.id)
		checkKicked(c, apiKick(c, srs, h.Field("client_id")))
		waitClientHook(c, srs, "on_unpublish", h.Field("client_id"))
		c.CheckNoError(untilGone(c, srs, "/api/v1/clients/"+h.Field("client_id")), "the publisher leaves the API")
		c.CheckNoError(untilGone(c, srs, "/api/v1/streams/"+h.Field("stream_id")), "the stream leaves the API")
		s.conn.SetReadDeadline(time.Now().Add(100 * time.Millisecond))
		_, err := s.conn.Read(make([]byte, 1))
		c.Check(isTimeout(err), "the device's media connection: %v, want it open", err)
	})
}

// srtClosedWithin waits until the server closes an SRT connection, for at
// most d.
func srtClosedWithin(conn *srt.Conn, d time.Duration) error {
	ctx, cancel := context.WithTimeout(context.Background(), d)
	defer cancel()
	return conn.WaitClosed(ctx)
}

// rtspRawPlayer plays a stream on a raw RTSP connection and returns the
// player's client id and the connection.
func rtspRawPlayer(c *check.C, srs *server.Server) (string, *rtsp.Conn) {
	c.Helper()
	stream := streamName(c)
	startPublisher(c, rtmpURL(srs, "live/"+stream), media.Path(c, media.H264BaselineAAC), 200*time.Millisecond)
	waitHook(c, srs, "on_publish", stream)
	conn := rtspRawPlay(c, srs, "live/"+stream)
	return waitHook(c, srs, "on_play", stream).Field("client_id"), conn
}
