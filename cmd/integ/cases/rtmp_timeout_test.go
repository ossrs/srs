package cases

import (
	"fmt"
	"testing"
	"time"

	"srs-integ/internal/check"
	"srs-integ/internal/flv"
	"srs-integ/internal/media"
	"srs-integ/internal/report"
	"srs-integ/internal/rtmp"
	"srs-integ/internal/server"
)

// The publish timeouts of TestTimer_Rtmp_PublishTimeout. SRS checks a
// publisher once per wait: the first wait is the first-packet timeout, each
// later one the normal timeout. So a publisher that sends nothing is closed
// at 100ms, one that stops after its first messages at the next 200ms check,
// and an idle one, with no player, at the first check after 300ms idle.
const (
	firstPacketTimeout = 100 * time.Millisecond
	normalTimeout      = 200 * time.Millisecond
	kickoffForIdle     = 300 * time.Millisecond
)

// timerSlack is how much earlier than its timeout the case may see SRS close
// a publisher: the case starts its clock a little after SRS starts its own.
const timerSlack = 20 * time.Millisecond

// TestTimer_Rtmp_PublishTimeout publishes on a variant with short publish
// timeouts and kickoff_for_idle. The timeouts are milliseconds, so their cases
// are Normal; the kick waits on the publish checks after a player leaves,
// which SRS sees only after its play receive thread wakes, so its cases are
// Timer. They share one server because the kick acts only on the checks the
// timeouts pace.
func TestTimer_Rtmp_PublishTimeout(t *testing.T) {
	t.Parallel()
	cfg := server.PublishConfig(server.Publish{
		FirstPacketTimeout: firstPacketTimeout,
		NormalTimeout:      normalTimeout,
		KickoffForIdle:     kickoffForIdle,
	})
	cfg.Hooks = true
	srs := server.Group(t, cfg)

	// FirstPacketTimeout publishes and sends nothing: SRS closes the publisher
	// after the first-packet timeout, not the longer normal timeout.
	check.Case(t, "FirstPacketTimeout", check.Normal, func(c *check.C) {
		stream := streamName(c)
		pub := publishOnly(c, rtmpURL(srs, "live/"+stream), rtmp.FMLE)
		start := time.Now()
		closed, err := untilClosed(pub, time.Second)
		c.Must(err, "the publisher")
		d := closed.Sub(start)
		c.Check(d >= firstPacketTimeout-timerSlack, "closed after %v, want at least %v", d, firstPacketTimeout)
		c.Check(d < normalTimeout-timerSlack, "closed after %v, want before the normal timeout %v", d, normalTimeout)
		waitHook(c, srs, "on_unpublish", stream)
	})

	// NormalTimeout sends the first 200ms of the file at once and then
	// nothing: SRS closes the publisher at least the normal timeout after its
	// last message, not at the first-packet check.
	check.Case(t, "NormalTimeout", check.Normal, func(c *check.C) {
		stream := streamName(c)
		url := rtmpURL(srs, "live/"+stream)
		playerOf(c, url)
		pub := publishTags(c, url, readTags(c, media.H264HighAAC, 0, 200*time.Millisecond))
		last := time.Now()
		closed, err := untilClosed(pub, time.Second)
		c.Must(err, "the publisher")
		d := closed.Sub(last)
		c.Check(d >= normalTimeout-timerSlack, "closed %v after the last message, want at least %v", d, normalTimeout)
		waitHook(c, srs, "on_unpublish", stream)
	})

	// PauseShorterThanNormal sends the first 100ms at once, pauses 250ms,
	// longer than the first-packet timeout and shorter than the normal one,
	// then sends the next 100ms: the player gets all of it, so the later
	// messages are timed by the normal timeout.
	check.Case(t, "PauseShorterThanNormal", check.Normal, func(c *check.C) {
		url := rtmpURL(srs, "live/"+streamName(c))
		player := playerOf(c, url)
		first := readTags(c, media.H264HighAAC, 0, 100*time.Millisecond)
		later := readTags(c, media.H264HighAAC, 100*time.Millisecond, 200*time.Millisecond)
		pub := publishTags(c, url, first)
		time.Sleep(250 * time.Millisecond)
		c.Must(writeTags(pub, later), "publish after the pause")

		ref := analyzeTags(c, append(append([]*media.FLVTag{}, first...), later...))
		var a flv.Analyzer
		u := report.Until{VideoFrames: ref.Video.Frames, AudioFrames: ref.Audio.Frames}
		c.CheckNoError(readUntil(player, &a, u, time.Second), "the media after the pause")
	})

	// SteadyStreamKept publishes in real time to a player for 350ms, past
	// the first-packet check and the first normal check: the gaps between
	// the messages of a real-time stream don't close it.
	check.Case(t, "SteadyStreamKept", check.Normal, func(c *check.C) {
		url := rtmpURL(srs, "live/"+streamName(c))
		player := playerOf(c, url)
		startPublisher(c, url, media.Path(c, media.H264HighAAC), 0)
		var a flv.Analyzer
		u := report.Until{Metadata: true, VideoHeader: true, AudioHeader: true, Duration: 350 * time.Millisecond}
		c.Must(readUntil(player, &a, u, time.Second), "play")
		checkPlayed(c, &a.Report, "h264", "aac")
	})

	// KickedWhenIdle publishes in real time with no player: SRS closes the
	// publisher once it has been idle for kickoff_for_idle.
	check.Case(t, "KickedWhenIdle", check.Timer, func(c *check.C) {
		stream := streamName(c)
		start := time.Now()
		pub := startPublisher(c, rtmpURL(srs, "live/"+stream), media.Path(c, media.H264HighAAC), 0)
		closed, err := untilClosed(pub, 2*time.Second)
		c.Must(err, "the publisher")
		d := closed.Sub(start)
		c.Check(d >= kickoffForIdle-timerSlack, "kicked after %v, want at least %v", d, kickoffForIdle)
		waitHook(c, srs, "on_unpublish", stream)
	})

	// KickedAfterLastPlayerLeaves publishes in real time to a player for
	// 600ms, past the check that kicks an idle publisher, then closes the
	// player: SRS kicks the publisher at least kickoff_for_idle later.
	check.Case(t, "KickedAfterLastPlayerLeaves", check.Timer, func(c *check.C) {
		stream := streamName(c)
		url := rtmpURL(srs, "live/"+stream)
		player := playerOf(c, url)
		pub := startPublisher(c, url, media.Path(c, media.H264HighAAC), 0)
		var a flv.Analyzer
		u := report.Until{Metadata: true, VideoHeader: true, AudioHeader: true, Duration: 600 * time.Millisecond}
		c.Must(readUntil(player, &a, u, time.Second), "play while the player keeps the publisher")

		player.Close()
		left := time.Now()
		closed, err := untilClosed(pub, 2*time.Second)
		c.Must(err, "the publisher")
		d := closed.Sub(left)
		c.Check(d >= kickoffForIdle-timerSlack, "kicked %v after the player left, want at least %v", d, kickoffForIdle)
		waitHook(c, srs, "on_unpublish", stream)
	})
}

// untilClosed reads a publisher's connection until SRS closes it, and
// returns when. It fails when the connection is still open after within.
func untilClosed(pub *rtmp.Client, within time.Duration) (time.Time, error) {
	pub.SetDeadline(time.Now().Add(within))
	for {
		if _, err := pub.ReadTag(); isTimeout(err) {
			return time.Time{}, fmt.Errorf("still open after %v", within)
		} else if err != nil {
			return time.Now(), nil
		}
	}
}
