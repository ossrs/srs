package main

import (
	"encoding/binary"
	"encoding/xml"
	"errors"
	"fmt"
	"io"
	"net/http"
	"net/url"
	"strconv"
	"strings"
	"time"
)

// dashPort is SRS's HTTP server port in the rtmp-dash scenario, which serves
// the DASH files.
const dashPort = "8080"

// dashSegment is one media segment of a track, by its number.
type dashSegment struct {
	number   int64
	duration time.Duration
	uri      string
}

// dashTrack is one Representation: its init, its timescale and its segments.
type dashTrack struct {
	video     bool
	init      string
	timescale int64
	segments  []dashSegment
}

// dashMPD is what a player reads from an MPD: how often to reload it, the
// base of its URLs, and its audio and video tracks, video first.
type dashMPD struct {
	update time.Duration
	base   string
	tracks []dashTrack
}

type mpdTemplate struct {
	Initialization string `xml:"initialization,attr"`
	Media          string `xml:"media,attr"`
	StartNumber    *int64 `xml:"startNumber,attr"`
	Timescale      int64  `xml:"timescale,attr"`
	Timeline       []struct {
		T *int64 `xml:"t,attr"`
		D int64  `xml:"d,attr"`
		R int64  `xml:"r,attr"`
	} `xml:"SegmentTimeline>S"`
}

type mpdXML struct {
	XMLName             xml.Name `xml:"MPD"`
	MinimumUpdatePeriod string   `xml:"minimumUpdatePeriod,attr"`
	BaseURL             string   `xml:"BaseURL"`
	Periods             []struct {
		AdaptationSets []struct {
			MimeType        string       `xml:"mimeType,attr"`
			Template        *mpdTemplate `xml:"SegmentTemplate"`
			Representations []struct {
				ID       string       `xml:"id,attr"`
				MimeType string       `xml:"mimeType,attr"`
				Template *mpdTemplate `xml:"SegmentTemplate"`
			} `xml:"Representation"`
		} `xml:"AdaptationSet"`
	} `xml:"Period"`
}

// parseISODuration reads an MPD duration such as PT5.000S: hours, minutes and
// seconds only, which is what a live MPD's update period uses.
func parseISODuration(s string) (time.Duration, error) {
	rest, ok := strings.CutPrefix(s, "PT")
	if !ok || rest == "" {
		return 0, fmt.Errorf("bad MPD duration %q", s)
	}
	var d time.Duration
	for rest != "" {
		i := strings.IndexAny(rest, "HMS")
		if i <= 0 {
			return 0, fmt.Errorf("bad MPD duration %q", s)
		}
		n, err := strconv.ParseFloat(rest[:i], 64)
		if err != nil || n < 0 {
			return 0, fmt.Errorf("bad MPD duration %q", s)
		}
		unit := map[byte]time.Duration{'H': time.Hour, 'M': time.Minute, 'S': time.Second}[rest[i]]
		d += time.Duration(n * float64(unit))
		rest = rest[i+1:]
	}
	return d, nil
}

// parseMPD reads a live MPD's SegmentTemplate tracks with their timelines:
// segment k is startNumber+k, each S entry repeated r more times, and its
// time continues from the last one when it has no t.
func parseMPD(body []byte) (dashMPD, error) {
	var m mpdXML
	var mpd dashMPD
	if err := xml.Unmarshal(body, &m); err != nil {
		return mpd, fmt.Errorf("not an MPD: %w", err)
	}
	mpd.base = strings.TrimSpace(m.BaseURL)
	if m.MinimumUpdatePeriod != "" {
		d, err := parseISODuration(m.MinimumUpdatePeriod)
		if err != nil {
			return mpd, err
		}
		mpd.update = d
	}
	var audio []dashTrack
	for _, period := range m.Periods {
		for _, set := range period.AdaptationSets {
			for _, r := range set.Representations {
				mime, template := set.MimeType, set.Template
				if r.MimeType != "" {
					mime = r.MimeType
				}
				if r.Template != nil {
					template = r.Template
				}
				video := strings.HasPrefix(mime, "video/")
				if template == nil || !video && !strings.HasPrefix(mime, "audio/") {
					continue
				}
				track := dashTrack{video: video, timescale: template.Timescale}
				if track.timescale <= 0 {
					track.timescale = 1
				}
				expand := func(s string, number, t int64) string {
					return strings.NewReplacer("$RepresentationID$", r.ID, "$Number$", strconv.FormatInt(number, 10), "$Time$", strconv.FormatInt(t, 10), "$$", "$").Replace(s)
				}
				track.init = expand(template.Initialization, 0, 0)
				number := int64(1)
				if template.StartNumber != nil {
					number = *template.StartNumber
				}
				var t int64
				for _, s := range template.Timeline {
					if s.T != nil {
						t = *s.T
					}
					if s.D <= 0 || s.R < 0 {
						return mpd, fmt.Errorf("bad MPD segment timeline in %s", r.ID)
					}
					for i := int64(0); i <= s.R; i++ {
						duration := time.Duration(s.D) * time.Second / time.Duration(track.timescale)
						track.segments = append(track.segments, dashSegment{number: number, duration: duration, uri: expand(template.Media, number, t)})
						number, t = number+1, t+s.D
					}
				}
				if video {
					mpd.tracks = append(mpd.tracks, track)
				} else {
					audio = append(audio, track)
				}
			}
		}
	}
	mpd.tracks = append(mpd.tracks, audio...)
	return mpd, nil
}

// fmp4Sample is one sample of a moof's track run, timed in its timescale.
type fmp4Sample struct {
	dts  int64
	cts  int32
	size uint32
}

// fmp4Demuxer reads a media segment's boxes as they stream in: it keeps only
// the moof, which lists the samples, then reads each sample of the mdat into
// a buffer it reuses, so a segment is never held whole.
type fmp4Demuxer struct {
	head    [16]byte
	box     []byte
	sample  []byte
	samples []fmp4Sample
	// offset is the trun's data_offset, from the moof's start; moofSize the
	// size of the moof it came in.
	offset, moofSize int64
	hasOffset        bool
}

// read gives each sample's decode time and composition offset, in its
// track's timescale, and its bytes, which are valid only in the callback. A
// segment is styp, sidx, moof and mdat as SRS writes it, one track run per
// moof.
func (d *fmp4Demuxer) read(r io.Reader, sample func(dts int64, cts int32, data []byte)) error {
	for {
		if _, err := io.ReadFull(r, d.head[:8]); err != nil {
			if err == io.EOF {
				return nil
			}
			return fmt.Errorf("fMP4 box header: %w", err)
		}
		size, kind, header := int64(binary.BigEndian.Uint32(d.head[:4])), string(d.head[4:8]), int64(8)
		if size == 1 {
			if _, err := io.ReadFull(r, d.head[8:16]); err != nil {
				return fmt.Errorf("fMP4 box header: %w", err)
			}
			size, header = int64(binary.BigEndian.Uint64(d.head[8:16])), 16
		}
		// A size of 0 runs to the end of the file.
		payload := int64(-1)
		if size != 0 {
			if size < header {
				return fmt.Errorf("bad fMP4 %s box size %d", kind, size)
			}
			payload = size - header
		}
		switch kind {
		case "moof":
			if payload < 0 || payload > 1<<20 {
				return fmt.Errorf("bad fMP4 moof size %d", size)
			}
			d.box = grow(d.box, int(payload))
			if _, err := io.ReadFull(r, d.box); err != nil {
				return fmt.Errorf("fMP4 moof: %w", err)
			}
			if err := d.parseMoof(d.box); err != nil {
				return err
			}
			d.moofSize = size
		case "mdat":
			if err := d.readMdat(r, header, payload, sample); err != nil {
				return err
			}
		default:
			if err := discard(r, payload); err != nil {
				return fmt.Errorf("fMP4 %s box: %w", kind, err)
			}
		}
	}
}

func (d *fmp4Demuxer) readMdat(r io.Reader, header, payload int64, sample func(int64, int32, []byte)) error {
	var skip int64
	if d.hasOffset {
		skip = d.offset - d.moofSize - header
	}
	if skip < 0 || payload >= 0 && skip > payload {
		return fmt.Errorf("bad fMP4 data offset %d", d.offset)
	}
	if err := discard(r, skip); err != nil {
		return fmt.Errorf("fMP4 mdat: %w", err)
	}
	read := skip
	for _, s := range d.samples {
		if payload >= 0 && read+int64(s.size) > payload {
			return errors.New("fMP4 sample beyond its mdat")
		}
		if s.size > 16<<20 {
			return fmt.Errorf("bad fMP4 sample size %d", s.size)
		}
		d.sample = grow(d.sample, int(s.size))
		if _, err := io.ReadFull(r, d.sample); err != nil {
			return fmt.Errorf("fMP4 mdat: %w", err)
		}
		sample(s.dts, s.cts, d.sample)
		read += int64(s.size)
	}
	d.samples, d.hasOffset = d.samples[:0], false
	if payload < 0 {
		return discard(r, -1)
	}
	return discard(r, payload-read)
}

// parseMoof reads the samples of the moof's track runs, timed from the
// tfdt, with the tfhd's defaults for what a run leaves out.
func (d *fmp4Demuxer) parseMoof(b []byte) error {
	d.samples, d.hasOffset = d.samples[:0], false
	return eachBox(b, func(kind string, traf []byte) error {
		if kind != "traf" {
			return nil
		}
		var defDuration, defSize uint32
		var dts int64
		return eachBox(traf, func(kind string, b []byte) error {
			if len(b) < 4 {
				return fmt.Errorf("short fMP4 %s box", kind)
			}
			version, flags := b[0], binary.BigEndian.Uint32(b[:4])&0xffffff
			at := 4
			field := func(n int) ([]byte, error) {
				if at+n > len(b) {
					return nil, fmt.Errorf("short fMP4 %s box", kind)
				}
				at += n
				return b[at-n : at], nil
			}
			switch kind {
			case "tfhd":
				if _, err := field(4); err != nil { // track_ID
					return err
				}
				for _, f := range []struct {
					flag uint32
					n    int
					to   *uint32
				}{{0x1, 8, nil}, {0x2, 4, nil}, {0x8, 4, &defDuration}, {0x10, 4, &defSize}, {0x20, 4, nil}} {
					if flags&f.flag == 0 {
						continue
					}
					v, err := field(f.n)
					if err != nil {
						return err
					}
					if f.to != nil {
						*f.to = binary.BigEndian.Uint32(v)
					}
				}
			case "tfdt":
				n := 4
				if version == 1 {
					n = 8
				}
				v, err := field(n)
				if err != nil {
					return err
				}
				if n == 8 {
					dts = int64(binary.BigEndian.Uint64(v))
				} else {
					dts = int64(binary.BigEndian.Uint32(v))
				}
			case "trun":
				v, err := field(4)
				if err != nil {
					return err
				}
				count := binary.BigEndian.Uint32(v)
				if flags&0x1 != 0 {
					v, err := field(4)
					if err != nil {
						return err
					}
					d.offset, d.hasOffset = int64(int32(binary.BigEndian.Uint32(v))), true
				}
				if flags&0x4 != 0 {
					if _, err := field(4); err != nil {
						return err
					}
				}
				for i := uint32(0); i < count; i++ {
					s := fmp4Sample{dts: dts, size: defSize}
					duration := defDuration
					for _, f := range []struct {
						flag uint32
						to   func(uint32)
					}{{0x100, func(v uint32) { duration = v }}, {0x200, func(v uint32) { s.size = v }}, {0x400, nil}, {0x800, func(v uint32) { s.cts = int32(v) }}} {
						if flags&f.flag == 0 {
							continue
						}
						v, err := field(4)
						if err != nil {
							return err
						}
						if f.to != nil {
							f.to(binary.BigEndian.Uint32(v))
						}
					}
					d.samples = append(d.samples, s)
					dts += int64(duration)
				}
			}
			return nil
		})
	})
}

// eachBox calls fn with each child box of b, its type and payload.
func eachBox(b []byte, fn func(kind string, payload []byte) error) error {
	for len(b) > 0 {
		if len(b) < 8 {
			return errors.New("short fMP4 box")
		}
		size := int(binary.BigEndian.Uint32(b[:4]))
		if size < 8 || size > len(b) {
			return fmt.Errorf("bad fMP4 %s box size %d", string(b[4:8]), size)
		}
		if err := fn(string(b[4:8]), b[8:size]); err != nil {
			return err
		}
		b = b[size:]
	}
	return nil
}

func grow(b []byte, n int) []byte {
	if cap(b) < n {
		return make([]byte, n)
	}
	return b[:n]
}

// discard skips n bytes, or to the end when n is negative.
func discard(r io.Reader, n int64) error {
	if n < 0 {
		_, err := io.Copy(io.Discard, r)
		return err
	}
	if _, err := io.CopyN(io.Discard, r, n); err != nil {
		return err
	}
	return nil
}

// dashClient is a lean DASH player (D30), which plays like a real one: it
// fetches each track's init once, reloads the MPD after its
// minimumUpdatePeriod, and fetches each segment new since it joined, whole,
// on one keep-alive connection, the video segment before the audio one. A
// segment streams through the fMP4 demuxer; each H.264 sample is reported as
// an FLV AVC payload, found in the source by its payload; AAC is counted per
// sample.
type dashClient struct {
	keepAlive
	app  string
	fmp4 fmp4Demuxer
}

func dialDASH(addr, app string) (*dashClient, error) {
	c := &dashClient{keepAlive: keepAlive{addr: addr}, app: app}
	if _, _, err := c.connect(); err != nil {
		return nil, err
	}
	return c, nil
}

// play polls /<app>/<stream>.mpd and reports each new segment's frames, and
// the video segment itself, until stop, which closes the connection and
// returns nil. Before SRS has the MPD, at the stream's start, it asks again
// each second; then every segment of its first MPD is new. A request that
// fails, or a segment fetch that takes longer than the segment's duration,
// fails the player.
func (c *dashClient) play(stream string, stop <-chan struct{}, report chan<- rtmpMessage) error {
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
	var newest uint32
	mpdURL := &url.URL{Path: "/" + c.app + "/" + stream + ".mpd"}
	// The shipped dash_fragment, until an MPD gives its segments; a request
	// may take up to the gap rule's three.
	target := 10 * time.Second
	joined, missing := false, false
	var next []int64
	for {
		status := 0
		var body []byte
		err := c.get(mpdURL.RequestURI(), 3*target, func(r *http.Response) error {
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
			return fmt.Errorf("DASH MPD %s status %d", mpdURL.RequestURI(), status)
		}
		mpd, err := parseMPD(body)
		if err != nil {
			return err
		}
		base, err := url.Parse(mpd.base)
		if err != nil {
			return err
		}
		base = mpdURL.ResolveReference(base)
		fetch := func(uri string, read func(*http.Response) error) error {
			ref, err := url.Parse(uri)
			if err != nil {
				return err
			}
			return c.get(base.ResolveReference(ref).RequestURI(), 3*target, func(r *http.Response) error {
				if r.StatusCode != http.StatusOK {
					return fmt.Errorf("DASH file %s status %s", uri, r.Status)
				}
				return read(r)
			})
		}
		for _, track := range mpd.tracks {
			for _, segment := range track.segments {
				target = max(target, segment.duration)
			}
		}
		if !joined {
			// A real player loads each track's init before its segments.
			next = make([]int64, len(mpd.tracks))
			for i, track := range mpd.tracks {
				if err := fetch(track.init, func(*http.Response) error { return nil }); err != nil {
					return stopped(err)
				}
				if n := len(track.segments); n > 0 {
					next[i] = track.segments[n-1].number + 1
					if missing {
						next[i] = track.segments[0].number
					}
				}
			}
			joined = true
		}
		for i, track := range mpd.tracks {
			if i >= len(next) {
				break
			}
			for _, segment := range track.segments {
				if segment.number < next[i] {
					continue
				}
				at := time.Now()
				err := fetch(segment.uri, func(r *http.Response) error {
					return c.fmp4.read(r.Body, func(dts int64, cts int32, data []byte) {
						if !track.video {
							send(rtmpMessage{typeID: 8, timestamp: newest, payload: data})
							return
						}
						// Milliseconds, as FLV times a frame and SRS's MPD timescale is.
						millis := func(t int64) int64 { return t * 1000 / track.timescale }
						newest = uint32(millis(dts + int64(cts)))
						send(rtmpMessage{typeID: 9, timestamp: newest, payload: avcSample(data, uint32(max(0, millis(int64(cts)))))})
					})
				})
				if err != nil {
					return stopped(err)
				}
				if took := time.Since(at); took > segment.duration {
					return fmt.Errorf("DASH segment %s took %v, longer than its %v", segment.uri, took.Round(time.Millisecond), segment.duration)
				}
				if track.video && !send(rtmpMessage{typeID: segmentMessage, timestamp: newest}) {
					return nil
				}
				next[i] = segment.number + 1
			}
		}
		// A live player reloads the MPD after its minimumUpdatePeriod, or a
		// segment duration when it has none.
		interval := mpd.update
		if interval <= 0 {
			interval = target
		}
		if !wait(interval) {
			return nil
		}
	}
}

// avcSample makes an FLV AVC NALU payload of an MP4 sample, which holds the
// same length-prefixed NALUs: a key frame when it has an IDR slice.
func avcSample(sample []byte, cts uint32) []byte {
	frame := byte(0x27)
	for b := sample; len(b) >= 5; {
		size := int(binary.BigEndian.Uint32(b[:4]))
		if size <= 0 || size > len(b)-4 {
			break
		}
		if b[4]&0x1f == 5 {
			frame = 0x17
			break
		}
		b = b[4+size:]
	}
	payload := make([]byte, 5, 5+len(sample))
	payload[0], payload[1] = frame, 1
	put24(payload[2:], cts)
	return append(payload, sample...)
}
