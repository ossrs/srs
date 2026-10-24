// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package cases

import (
	"fmt"
	"sync"
	"testing"
	"time"

	"srs-integ/internal/check"
	"srs-integ/internal/flv"
	"srs-integ/internal/media"
	"srs-integ/internal/mock"
	"srs-integ/internal/report"
	"srs-integ/internal/server"
)

const (
	// roundGrace is how long a restart round waits for the rest of its cases
	// after the first one reaches a stage, when some never come, such as
	// cases a -run filter leaves out.
	roundGrace = 2 * time.Second
	// roundWait is how long a case waits for a stage of its round to open.
	roundWait = 6 * time.Second
	// destinationAway is how long the destination stays stopped once every
	// case has seen its forward retrying, so media the origin got while it was
	// away is at least this old when the forward reconnects.
	destinationAway = time.Second
)

// A restartRound is a stop and a start of a server that several cases share:
// it stops the server once every case has reached the first stage, and starts
// it again once every case has reached the second. A case that ends early
// reaches every stage it didn't, and a stage opens a grace time after its
// first case even when the others never come.
type restartRound struct {
	n      int
	stages [2]roundStage
}

type roundStage struct {
	mu      sync.Mutex
	arrived int
	once    sync.Once
	open    chan struct{}
	act     func() error
	err     error
}

func newRestartRound(n int, stop, start func() error) *restartRound {
	r := &restartRound{n: n}
	for i, act := range []func() error{stop, start} {
		r.stages[i].open, r.stages[i].act = make(chan struct{}), act
	}
	return r
}

// arrive counts a case at a stage, and opens it with the last case, or a
// grace time after the first.
func (r *restartRound) arrive(i int) {
	st := &r.stages[i]
	st.mu.Lock()
	st.arrived++
	n := st.arrived
	st.mu.Unlock()
	openIt := func() {
		st.once.Do(func() {
			st.err = st.act()
			close(st.open)
		})
	}
	if n == 1 {
		time.AfterFunc(roundGrace, openIt)
	}
	if n == r.n {
		go openIt()
	}
}

// A roundSeat is one case's place in a round.
type roundSeat struct {
	r       *restartRound
	mu      sync.Mutex
	reached [2]bool
}

// seat takes a case's place in the round; when the case ends, it reaches
// every stage it didn't, so it never holds the others.
func (r *restartRound) seat(c *check.C) *roundSeat {
	s := &roundSeat{r: r}
	c.Cleanup(func() {
		for i := range s.reached {
			s.reach(i)
		}
	})
	return s
}

func (s *roundSeat) reach(i int) {
	s.mu.Lock()
	defer s.mu.Unlock()
	if !s.reached[i] {
		s.reached[i] = true
		s.r.arrive(i)
	}
}

// wait reaches a stage and waits until it opens, and returns the error of
// its stop or start.
func (s *roundSeat) wait(i int) error {
	s.reach(i)
	st := &s.r.stages[i]
	select {
	case <-st.open:
		return st.err
	case <-time.After(roundWait):
		return fmt.Errorf("stage %d of the round not open after %v", i, roundWait)
	}
}

// TestTimer_Forward_Retry checks a forward whose destination stops and
// starts again on the same port: the forward is retrying with the same id
// while the destination is away, and forwarding again once the forwarder,
// which retries every 3s, reconnects. The destination's player then gets the
// metadata and each sequence header once, then media from after the
// reconnect, video from a keyframe, and none of the media the origin got
// while the destination was away. Two origins forward to one destination,
// by destination and by a backend, and its cases share one stop and start.
// The destination keeps the timestamps the forwarder sends (time_jitter off),
// so a case can tell old media from new.
func TestTimer_Forward_Retry(t *testing.T) {
	t.Parallel()
	// Shared servers, though only this group uses them, so they stop with the
	// others at the end, at once, rather than one after another, each about
	// a second, at the end of the group.
	dest := server.Shared(t, "ForwardRetryDest", server.PlayConfig(server.Play{TimeJitter: "off"}))
	byDest := server.Shared(t, "ForwardRetryByDest", server.WithForward(forwardServerConfig(), server.Forward{
		Destinations: []string{dest.Addr(server.RTMP)},
	}))
	cfg := forwardServerConfig()
	cfg.ForwardBackend = true
	byBackend := server.Shared(t, "ForwardRetryByBackend", cfg)
	byBackend.Hooks.Script(mock.Match{Action: "on_forward"}, forwardTo())

	var restartAt time.Time
	round := newRestartRound(4, dest.Stop, func() error {
		time.Sleep(destinationAway)
		restartAt = time.Now()
		return dest.Restart()
	})
	av := report.Until{Metadata: true, VideoHeader: true, AudioHeader: true, Keyframes: 1, VideoFrames: 5}
	retry := func(c *check.C, origin *server.Server, backend bool, file string, u report.Until) *report.Report {
		return forwardRetry(c, round, &restartAt, origin, dest, backend, file, u)
	}

	check.Case(t, "DestinationReconnect", check.Timer, func(c *check.C) {
		r := retry(c, byDest, false, media.H264HighAAC, av)
		checkVideo(c, r, "h264")
		checkAudio(c, r, "aac", 1)
	})

	check.Case(t, "AudioOnlyReconnect", check.Timer, func(c *check.C) {
		r := retry(c, byDest, false, media.AACOnly, report.Until{Metadata: true, AudioHeader: true, AudioFrames: 5})
		c.Check(r.Video == nil, "video in a forwarded audio-only stream: %v", r)
		checkAudio(c, r, "aac", 1)
	})

	check.Case(t, "VideoOnlyReconnect", check.Timer, func(c *check.C) {
		r := retry(c, byDest, false, media.H264Only, report.Until{Metadata: true, VideoHeader: true, Keyframes: 1, VideoFrames: 5})
		c.Check(r.Audio == nil, "audio in a forwarded video-only stream: %v", r)
		checkVideo(c, r, "h264")
	})

	check.Case(t, "BackendReconnect", check.Timer, func(c *check.C) {
		r := retry(c, byBackend, true, media.H264HighAAC, av)
		checkVideo(c, r, "h264")
		checkAudio(c, r, "aac", 1)
	})
}

// forwardRetry publishes a file on the origin, forwarded to the destination,
// by destination or by the backend to another stream name, and goes through
// the round: the forward is retrying with its id while the destination is
// away, and forwarding with it again after the destination starts. A player
// of the destination, from before the forwarder reconnects, gets the stream
// until u: the metadata and each sequence header once, video from a
// keyframe, and no frame older than the restart. It returns the report.
func forwardRetry(c *check.C, round *restartRound, restartAt *time.Time, origin, dest *server.Server, backend bool, file string, u report.Until) *report.Report {
	c.Helper()
	seat := round.seat(c)
	stream := streamName(c)
	path := "live/" + stream
	if backend {
		path += "-to"
		origin.Hooks.Script(mock.Match{Action: "on_forward", Stream: stream}, forwardTo(rtmpURL(dest, path)))
	}
	url := rtmpURL(dest, path)
	_, streamID := publishForwarded(c, origin, "live/"+stream, file, []string{url})
	// The publisher sent publishLead at once, then sends in real time: the
	// media it sends at a time is about this far into the file.
	published := time.Now()
	mediaAt := func(t time.Time) time.Duration { return publishLead + t.Sub(published) }
	id := ids(forwardsOf(c, origin, streamID))

	c.Must(seat.wait(0), "stop the destination")
	retrying := waitForwards(c, origin, streamID, []string{url}, "retrying", 2*time.Second)
	c.CheckEqual(ids(retrying), id, "the forward's id while retrying")

	c.Must(seat.wait(1), "start the destination again")
	player := playerOf(c, url)
	forwarding := waitForwards(c, origin, streamID, []string{url}, "forwarding", 4*time.Second)
	c.CheckEqual(ids(forwarding), id, "the forward's id after the reconnect")

	var a flv.Analyzer
	c.Must(readUntil(player, &a, u, 2*time.Second), "play the destination after the reconnect")
	r := &a.Report
	c.CheckEqual(r.Metadata, 1, "metadata messages")
	oldest := mediaAt(*restartAt) - 200*time.Millisecond
	if v := r.Video; v != nil {
		c.CheckEqual(v.SequenceHeaders, 1, "video sequence headers")
		c.CheckEqual(v.LeadingFrames, 0, "video frames before the first keyframe")
		c.Check(v.FirstDTS >= oldest, "the first video frame at %v, from before the restart at %v", v.FirstDTS, oldest)
	}
	if au := r.Audio; au != nil {
		c.CheckEqual(au.SequenceHeaders, 1, "audio sequence headers")
		c.Check(au.FirstDTS >= oldest, "the first audio frame at %v, from before the restart at %v", au.FirstDTS, oldest)
	}
	c.CheckNoError(r.CheckTimestamps(100*time.Millisecond), "timestamps")
	return r
}
