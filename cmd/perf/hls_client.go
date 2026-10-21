package main

import (
	"bufio"
	"errors"
	"fmt"
	"io"
	"math"
	"net"
	"net/http"
	"net/url"
	"strconv"
	"strings"
	"sync"
	"syscall"
	"time"
)

// hlsPort is SRS's HTTP server port in the rtmp-hls scenario, which serves
// the HLS files.
const hlsPort = "8080"

// hlsSegment is one media segment of a playlist, numbered by its media
// sequence.
type hlsSegment struct {
	seq      int64
	duration time.Duration
	uri      string
}

// hlsPlaylist is a master playlist's first variant, or a media playlist's
// target duration and segments.
type hlsPlaylist struct {
	variant  string
	target   time.Duration
	segments []hlsSegment
}

func parsePlaylist(body []byte) (hlsPlaylist, error) {
	var p hlsPlaylist
	lines := strings.Split(string(body), "\n")
	if strings.TrimSpace(lines[0]) != "#EXTM3U" {
		return p, errors.New("not an m3u8 playlist")
	}
	var seq int64
	var duration time.Duration
	inf, variant := false, false
	for _, line := range lines[1:] {
		line = strings.TrimSpace(line)
		switch {
		case line == "":
		case strings.HasPrefix(line, "#EXT-X-STREAM-INF:"):
			variant = true
		case strings.HasPrefix(line, "#EXT-X-TARGETDURATION:"):
			n, err := strconv.Atoi(strings.TrimPrefix(line, "#EXT-X-TARGETDURATION:"))
			if err != nil || n <= 0 {
				return p, fmt.Errorf("bad playlist line %q", line)
			}
			p.target = time.Duration(n) * time.Second
		case strings.HasPrefix(line, "#EXT-X-MEDIA-SEQUENCE:"):
			n, err := strconv.ParseInt(strings.TrimPrefix(line, "#EXT-X-MEDIA-SEQUENCE:"), 10, 64)
			if err != nil {
				return p, fmt.Errorf("bad playlist line %q", line)
			}
			seq = n
		case strings.HasPrefix(line, "#EXTINF:"):
			value, _, _ := strings.Cut(strings.TrimPrefix(line, "#EXTINF:"), ",")
			seconds, err := strconv.ParseFloat(value, 64)
			if err != nil || seconds < 0 {
				return p, fmt.Errorf("bad playlist line %q", line)
			}
			duration, inf = time.Duration(math.Round(seconds*1000))*time.Millisecond, true
		case strings.HasPrefix(line, "#"):
		case variant:
			if p.variant == "" {
				p.variant = line
			}
			variant = false
		case inf:
			p.segments = append(p.segments, hlsSegment{seq: seq, duration: duration, uri: line})
			seq, inf = seq+1, false
		}
	}
	return p, nil
}

// errStopped ends a request of a player that was stopped.
var errStopped = errors.New("player stopped")

// keepAlive is a segmented player's one HTTP/1.1 connection, kept alive
// across its requests, with no HTTP client pool.
type keepAlive struct {
	addr string
	// mu guards the connection, which stop closes from another goroutine.
	mu      sync.Mutex
	conn    net.Conn
	br      *bufio.Reader
	stopped bool
}

// hlsClient is a lean HLS player (D30), which plays like a real one: it
// follows the master playlist to its hls_ctx session, polls the media
// playlist as RFC 8216 says, and fetches each segment new since it joined,
// whole, on one keep-alive connection. A segment streams through the TS
// demuxer of the SRT player, which rebuilds only its H.264 frames, found in
// the source by their first slice; AAC is counted per PES.
type hlsClient struct {
	keepAlive
	app   string
	chunk []byte
}

func dialHLS(addr, app string) (*hlsClient, error) {
	c := &hlsClient{keepAlive: keepAlive{addr: addr}, app: app, chunk: make([]byte, 16*1024)}
	if _, _, err := c.connect(); err != nil {
		return nil, err
	}
	return c, nil
}

// connect gives the kept-alive connection, dialing a new one when SRS closed
// the last; whether it was reused, since only a reused one is retried.
func (c *keepAlive) connect() (net.Conn, bool, error) {
	c.mu.Lock()
	defer c.mu.Unlock()
	if c.stopped {
		return nil, false, errStopped
	}
	if c.conn != nil {
		return c.conn, true, nil
	}
	conn, err := net.DialTimeout("tcp", c.addr, 5*time.Second)
	if err != nil {
		return nil, false, err
	}
	c.conn, c.br = conn, bufio.NewReader(conn)
	return conn, false, nil
}

// drop closes the connection; a stopped player dials no new one.
func (c *keepAlive) drop(stop bool) {
	c.mu.Lock()
	defer c.mu.Unlock()
	if c.conn != nil {
		c.conn.Close()
		c.conn = nil
	}
	c.stopped = c.stopped || stop
}

// get sends one GET and gives its response to read, then reads the rest of
// the body, so the connection can serve the next request. A request on a
// reused connection that SRS closed is sent again once on a new one.
func (c *keepAlive) get(path string, timeout time.Duration, read func(*http.Response) error) error {
	conn, reused, err := c.connect()
	if err != nil {
		return err
	}
	conn.SetDeadline(time.Now().Add(timeout))
	request := fmt.Sprintf("GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: srs-perf/1\r\nAccept: */*\r\n\r\n", path, c.addr)
	var response *http.Response
	if _, err = io.WriteString(conn, request); err == nil {
		response, err = http.ReadResponse(c.br, nil)
	}
	if err != nil {
		c.drop(false)
		if reused && (errors.Is(err, io.EOF) || errors.Is(err, io.ErrUnexpectedEOF) || errors.Is(err, syscall.ECONNRESET) || errors.Is(err, syscall.EPIPE)) {
			return c.get(path, timeout, read)
		}
		return err
	}
	err = read(response)
	if err == nil {
		_, err = io.Copy(io.Discard, response.Body)
	}
	response.Body.Close()
	if err != nil || response.Close {
		c.drop(false)
	}
	return err
}

// play polls /<app>/<stream>.m3u8 and reports each new segment's frames, then
// the segment itself, until stop, which closes the connection and returns
// nil. Before SRS has the playlist, at the stream's start, it asks again each
// second; then every segment of its first playlist is new. A request that
// fails, or a segment fetch that takes longer than the segment's duration,
// fails the player.
func (c *hlsClient) play(stream string, stop <-chan struct{}, report chan<- rtmpMessage) error {
	done := make(chan struct{})
	defer close(done)
	defer c.drop(true)
	go func() {
		select {
		case <-stop:
			c.drop(true)
		case <-done:
		}
	}()
	stopped := func(err error) error {
		select {
		case <-stop:
			return nil
		default:
			return err
		}
	}
	send := func(m rtmpMessage) bool {
		select {
		case report <- m:
			return true
		case <-stop:
			return false
		}
	}
	wait := func(d time.Duration) bool {
		timer := time.NewTimer(d)
		defer timer.Stop()
		select {
		case <-stop:
			return false
		case <-timer.C:
			return true
		}
	}
	var clock ptsClock
	var newest uint32
	demuxer := newTSDemuxer(func(pts int64, au [][]byte) {
		newest = clock.millis(pts)
		send(rtmpMessage{typeID: 9, timestamp: newest, payload: avcPayload(au)})
	}, func(pts int64, data []byte) {
		send(rtmpMessage{typeID: 8, timestamp: newest, payload: data})
	})
	base := &url.URL{Path: "/" + c.app + "/" + stream + ".m3u8"}
	// The shipped hls_fragment, until a playlist gives its target duration;
	// a request may take up to the gap rule's three.
	target := 10 * time.Second
	joined, missing := false, false
	var next int64
	for {
		status := 0
		var body []byte
		err := c.get(base.RequestURI(), 3*target, func(r *http.Response) error {
			status = r.StatusCode
			if status != http.StatusOK {
				return nil
			}
			var err error
			body, err = io.ReadAll(io.LimitReader(r.Body, 1<<20))
			return err
		})
		if err != nil {
			return stopped(err)
		}
		if status == http.StatusNotFound && !joined {
			missing = true
			if !wait(time.Second) {
				return nil
			}
			continue
		}
		if status != http.StatusOK {
			return fmt.Errorf("HLS playlist %s status %d", base.RequestURI(), status)
		}
		playlist, err := parsePlaylist(body)
		if err != nil {
			return err
		}
		if playlist.variant != "" {
			ref, err := url.Parse(playlist.variant)
			if err != nil {
				return err
			}
			base = base.ResolveReference(ref)
			continue
		}
		if playlist.target > 0 {
			target = playlist.target
		}
		if n := len(playlist.segments); !joined && n > 0 {
			next = playlist.segments[n-1].seq + 1
			if missing {
				next--
			}
		}
		joined = true
		changed := false
		for _, segment := range playlist.segments {
			if segment.seq < next {
				continue
			}
			ref, err := url.Parse(segment.uri)
			if err != nil {
				return err
			}
			at := time.Now()
			err = c.get(base.ResolveReference(ref).RequestURI(), 3*target, func(r *http.Response) error {
				if r.StatusCode != http.StatusOK {
					return fmt.Errorf("HLS segment %s status %s", segment.uri, r.Status)
				}
				for {
					n, err := r.Body.Read(c.chunk)
					if n > 0 {
						if err := demuxer.write(c.chunk[:n]); err != nil {
							return err
						}
					}
					if err == io.EOF {
						return nil
					}
					if err != nil {
						return err
					}
				}
			})
			if err != nil {
				return stopped(err)
			}
			demuxer.flush()
			if took := time.Since(at); took > segment.duration {
				return fmt.Errorf("HLS segment %s took %v, longer than its %v", segment.uri, took.Round(time.Millisecond), segment.duration)
			}
			if !send(rtmpMessage{typeID: segmentMessage, timestamp: newest}) {
				return nil
			}
			next, changed = segment.seq+1, true
		}
		// RFC 8216 6.3.4: reload after the target duration, or half of it when
		// the playlist had nothing new.
		interval := target
		if !changed {
			interval = target / 2
		}
		if !wait(interval) {
			return nil
		}
	}
}
