package main

import (
	"fmt"
	"strings"
	"time"

	gosrt "github.com/datarhei/gosrt"
)

// srtPort is SRS's SRT listen port in the srt-srt scenario, its default.
const srtPort = "10080"

// srtPayload is the most an SRT live message carries: seven TS packets.
const srtPayload = 7 * 188

// srtStreamID names the stream and the mode, publish or request, in SRS's
// streamid form.
func srtStreamID(app, stream, mode string) string {
	return fmt.Sprintf("#!::r=%s/%s,m=%s", app, stream, mode)
}

// dialCaller connects an SRT caller with gosrt's defaults, such as its 120ms
// latency, like a typical client. It dials again while the handshake times
// out, as when a step adds many players at once, up to tries times (0 for no
// limit) or until stop.
func dialCaller(addr, streamID string, timeout time.Duration, tries int, stop <-chan struct{}) (gosrt.Conn, error) {
	config := gosrt.DefaultConfig()
	config.StreamId = streamID
	config.ConnectionTimeout = timeout
	for try := 1; ; try++ {
		conn, err := gosrt.Dial("srt", addr, config)
		if err == nil || !strings.Contains(err.Error(), "connection timeout") || try == tries {
			return conn, err
		}
		select {
		case <-stop:
			return nil, err
		default:
		}
	}
}

// srtClient is a lean SRT player: TS over one SRT connection, demuxed only to
// rebuild each H.264 access unit for the lag anchor; AAC is counted per PES.
// The streamid names the stream, so the connection is made by play.
type srtClient struct {
	addr    string
	app     string
	timeout time.Duration
	tries   int
}

func dialSRT(addr, app string) (*srtClient, error) {
	return &srtClient{addr: addr, app: app, timeout: 3 * time.Second}, nil
}

// play requests /<app>/<stream> and reports its frames until stop, which
// closes the connection and returns nil. Video is timed from its first PTS;
// an audio PES is reported at the newest video time, as for RTSP.
func (c *srtClient) play(stream string, stop <-chan struct{}, report chan<- rtmpMessage) error {
	conn, err := dialCaller(c.addr, srtStreamID(c.app, stream, "request"), c.timeout, c.tries, stop)
	if err != nil {
		select {
		case <-stop:
			return nil
		default:
			return err
		}
	}
	defer conn.Close()
	done := make(chan struct{})
	defer close(done)
	go func() {
		select {
		case <-stop:
			conn.Close()
		case <-done:
		}
	}()
	send := func(m rtmpMessage) bool {
		select {
		case report <- m:
			return true
		case <-stop:
			return false
		}
	}
	demuxer := newStreamTSDemuxer(send)
	b := make([]byte, 1500)
	for {
		n, err := conn.Read(b)
		if err != nil {
			select {
			case <-stop:
				return nil
			default:
				return err
			}
		}
		if err := demuxer.write(b[:n]); err != nil {
			return err
		}
	}
}

// publishSRT publishes the source to /<app>/<stream> as TS over SRT, paced
// and looped like the RTMP publisher, each tag's TS in messages of at most
// seven packets.
func publishSRT(addr, app, stream string, tags []rtmpMessage, clock *mediaClock, stop <-chan struct{}) error {
	muxer, err := newTSMuxer(tags)
	if err != nil {
		return err
	}
	conn, err := dialCaller(addr, srtStreamID(app, stream, "publish"), 3*time.Second, 1, stop)
	if err != nil {
		return err
	}
	defer conn.Close()
	return paceSource(tags, clock, stop, func(tag rtmpMessage) error {
		ts, err := muxer.mux(tag)
		if err != nil {
			return err
		}
		for len(ts) > 0 {
			n := min(len(ts), srtPayload)
			if _, err := conn.Write(ts[:n]); err != nil {
				return err
			}
			ts = ts[n:]
		}
		return nil
	})
}
