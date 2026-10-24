// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

// Package dash parses DASH MPDs with segment templates and timelines, and
// plays DASH: it fetches the init and media segments of a video and an
// audio representation, and analyzes their samples into the stream report,
// merged in time order.
package dash

import (
	"context"
	"encoding/xml"
	"errors"
	"fmt"
	"io"
	"math"
	"net/http"
	"net/url"
	"regexp"
	"sort"
	"strconv"
	"strings"
	"time"

	"srs-integ/internal/mp4"
	"srs-integ/internal/report"
)

// An MPD is a media presentation description.
type MPD struct {
	Profiles string `xml:"profiles,attr"`
	Type     string `xml:"type,attr"`
	// The durations are ISO 8601, such as PT5.000S; see Duration.
	MinimumUpdatePeriod  string `xml:"minimumUpdatePeriod,attr"`
	TimeShiftBufferDepth string `xml:"timeShiftBufferDepth,attr"`
	MinBufferTime        string `xml:"minBufferTime,attr"`
	// The times are UTC, such as 2026-10-10T12:00:00Z.
	AvailabilityStartTime string   `xml:"availabilityStartTime,attr"`
	PublishTime           string   `xml:"publishTime,attr"`
	BaseURL               string   `xml:"BaseURL"`
	Periods               []Period `xml:"Period"`
}

// A Period is a period of the presentation.
type Period struct {
	ID             string          `xml:"id,attr"`
	Start          string          `xml:"start,attr"`
	BaseURL        string          `xml:"BaseURL"`
	AdaptationSets []AdaptationSet `xml:"AdaptationSet"`
}

// An AdaptationSet is a set of representations of one content type.
type AdaptationSet struct {
	MimeType         string           `xml:"mimeType,attr"`
	ContentType      string           `xml:"contentType,attr"`
	BaseURL          string           `xml:"BaseURL"`
	SegmentAlignment bool             `xml:"segmentAlignment,attr"`
	StartWithSAP     int              `xml:"startWithSAP,attr"`
	SegmentTemplate  *SegmentTemplate `xml:"SegmentTemplate"`
	Representations  []Representation `xml:"Representation"`
}

// A Representation is one encoding of the content.
type Representation struct {
	ID              string           `xml:"id,attr"`
	Bandwidth       int              `xml:"bandwidth,attr"`
	Codecs          string           `xml:"codecs,attr"`
	MimeType        string           `xml:"mimeType,attr"`
	Width           int              `xml:"width,attr"`
	Height          int              `xml:"height,attr"`
	BaseURL         string           `xml:"BaseURL"`
	SegmentTemplate *SegmentTemplate `xml:"SegmentTemplate"`
}

// A SegmentTemplate names the init and media segments.
type SegmentTemplate struct {
	Initialization string           `xml:"initialization,attr"`
	Media          string           `xml:"media,attr"`
	StartNumber    *int             `xml:"startNumber,attr"`
	Timescale      uint64           `xml:"timescale,attr"`
	Duration       uint64           `xml:"duration,attr"`
	Timeline       *[]TimelineEntry `xml:"SegmentTimeline>S"`
}

// A TimelineEntry is an S of a SegmentTimeline: a segment at T, or after the
// one before when T is nil, of duration D, repeated R more times.
type TimelineEntry struct {
	T *uint64 `xml:"t,attr"`
	D uint64  `xml:"d,attr"`
	R int     `xml:"r,attr"`
}

// ParseMPD parses an MPD.
func ParseMPD(b []byte) (*MPD, error) {
	m := &MPD{}
	if err := xml.Unmarshal(b, m); err != nil {
		return nil, fmt.Errorf("MPD: %w", err)
	}
	return m, nil
}

// A SegmentURL is a media segment: its number, its start time and duration
// in the timescale of its stream, and its URL.
type SegmentURL struct {
	Number         int
	Time, Duration uint64
	URL            string
}

// A Stream is the first representation of an audio or video adaptation set,
// with the URLs of its init and media segments.
type Stream struct {
	// Kind is "video" or "audio".
	Kind           string
	Representation Representation
	Init           string
	Timescale      uint64
	Segments       []SegmentURL
}

// kind is "video" or "audio" from a content or MIME type, or "".
func kind(types ...string) string {
	for _, t := range types {
		switch {
		case t == "video" || strings.HasPrefix(t, "video/"):
			return "video"
		case t == "audio" || strings.HasPrefix(t, "audio/"):
			return "audio"
		}
	}
	return ""
}

// Streams returns a stream for each audio and video adaptation set of the
// first period, its URLs resolved against base, the MPD's URL, and the
// BaseURL of each level.
func (m *MPD) Streams(base *url.URL) ([]Stream, error) {
	if len(m.Periods) == 0 {
		return nil, errors.New("MPD without a Period")
	}
	resolve := func(u *url.URL, ref string) (*url.URL, error) {
		if ref == "" {
			return u, nil
		}
		r, err := url.Parse(strings.TrimSpace(ref))
		if err != nil {
			return nil, err
		}
		return u.ResolveReference(r), nil
	}

	p := m.Periods[0]
	pbase, err := resolve(base, m.BaseURL)
	if err == nil {
		pbase, err = resolve(pbase, p.BaseURL)
	}
	if err != nil {
		return nil, fmt.Errorf("BaseURL: %w", err)
	}

	var streams []Stream
	for _, as := range p.AdaptationSets {
		if len(as.Representations) == 0 {
			continue
		}
		rep := as.Representations[0]
		k := kind(as.ContentType, as.MimeType, rep.MimeType)
		if k == "" {
			continue
		}
		rbase, err := resolve(pbase, as.BaseURL)
		if err == nil {
			rbase, err = resolve(rbase, rep.BaseURL)
		}
		if err != nil {
			return nil, fmt.Errorf("representation %v BaseURL: %w", rep.ID, err)
		}
		st := rep.SegmentTemplate
		if st == nil {
			st = as.SegmentTemplate
		}
		if st == nil {
			return nil, fmt.Errorf("representation %v has no SegmentTemplate", rep.ID)
		}
		if st.Timeline == nil {
			return nil, fmt.Errorf("representation %v has no SegmentTimeline", rep.ID)
		}

		s := Stream{Kind: k, Representation: rep, Timescale: max(st.Timescale, 1)}
		url := func(template string, number int, t uint64) (string, error) {
			u, err := resolve(rbase, Expand(template, rep.ID, rep.Bandwidth, number, t))
			if err != nil {
				return "", err
			}
			return u.String(), nil
		}
		if s.Init, err = url(st.Initialization, 0, 0); err != nil {
			return nil, err
		}
		number := 1
		if st.StartNumber != nil {
			number = *st.StartNumber
		}
		entries := *st.Timeline
		var t uint64
		for i, e := range entries {
			if e.T != nil {
				t = *e.T
			}
			repeat := e.R
			if repeat < 0 {
				if i+1 >= len(entries) || entries[i+1].T == nil || e.D == 0 {
					return nil, fmt.Errorf("representation %v: an open repeat without the next time", rep.ID)
				}
				repeat = int((*entries[i+1].T-t)/e.D) - 1
			}
			for range repeat + 1 {
				u, err := url(st.Media, number, t)
				if err != nil {
					return nil, err
				}
				s.Segments = append(s.Segments, SegmentURL{Number: number, Time: t, Duration: e.D, URL: u})
				number++
				t += e.D
			}
		}
		streams = append(streams, s)
	}
	return streams, nil
}

var identifier = regexp.MustCompile(`\$(RepresentationID|Number|Time|Bandwidth)(%0(\d+)d)?\$|\$\$`)

// Expand fills a segment template: $RepresentationID$, $Number$, $Time$ and
// $Bandwidth$, each number with an optional width such as %05d, and $$.
func Expand(template, id string, bandwidth, number int, t uint64) string {
	return identifier.ReplaceAllStringFunc(template, func(m string) string {
		if m == "$$" {
			return "$"
		}
		g := identifier.FindStringSubmatch(m)
		var v string
		switch g[1] {
		case "RepresentationID":
			return id
		case "Number":
			v = strconv.Itoa(number)
		case "Time":
			v = strconv.FormatUint(t, 10)
		case "Bandwidth":
			v = strconv.Itoa(bandwidth)
		}
		if w, err := strconv.Atoi(g[3]); err == nil && len(v) < w {
			v = strings.Repeat("0", w-len(v)) + v
		}
		return v
	})
}

// pollInterval is how often a dynamic MPD is fetched again while it has no
// new segment, or is not there yet.
const pollInterval = 50 * time.Millisecond

// errNotFound is a 404, which an MPD that is not there yet answers.
var errNotFound = errors.New("not found")

func get(ctx context.Context, client *http.Client, url string) ([]byte, error) {
	req, err := http.NewRequestWithContext(ctx, http.MethodGet, url, nil)
	if err != nil {
		return nil, err
	}
	res, err := client.Do(req)
	if err != nil {
		return nil, err
	}
	defer res.Body.Close()
	if res.StatusCode == http.StatusNotFound {
		return nil, fmt.Errorf("GET %v: status %v: %w", url, res.Status, errNotFound)
	}
	if res.StatusCode != http.StatusOK {
		return nil, fmt.Errorf("GET %v: status %v", url, res.Status)
	}
	b, err := io.ReadAll(res.Body)
	if err != nil {
		return nil, fmt.Errorf("GET %v: %w", url, err)
	}
	return b, nil
}

// at is a time in a timescale as a duration.
func at(t, timescale uint64) time.Duration {
	return time.Duration(t/timescale)*time.Second + time.Duration(t%timescale)*time.Second/time.Duration(timescale)
}

// A track is the play state of one stream.
type track struct {
	stream Stream
	init   *mp4.Init
	// initURL is the URL of init.
	initURL string
	// last is the number of the last segment fetched, and end the time its
	// media ends.
	last    int
	fetched bool
	end     time.Duration
}

// pending returns the next segment of the stream to fetch, if any.
func (t *track) pending() (SegmentURL, bool) {
	for _, s := range t.stream.Segments {
		if !t.fetched || s.Number > t.last {
			return s, true
		}
	}
	return SegmentURL{}, false
}

// A player fetches the segments of every stream in time order, and adds
// their samples to the analyzer up to the time every stream has reached.
type player struct {
	client  *http.Client
	u       report.Until
	tracks  map[string]*track
	a       mp4.Analyzer
	pending []mp4.Sample
}

// flush adds the buffered samples before until, in time order.
func (p *player) flush(until time.Duration) error {
	sort.SliceStable(p.pending, func(i, j int) bool { return p.pending[i].Time() < p.pending[j].Time() })
	n := sort.Search(len(p.pending), func(i int) bool { return p.pending[i].Time() >= until })
	ready := p.pending[:n]
	p.pending = append([]mp4.Sample{}, p.pending[n:]...)
	return p.a.Add(ready, p.u)
}

func (p *player) done() bool {
	return p.u != (report.Until{}) && p.u.Done(&p.a.Report)
}

// fetch fetches every pending segment, the earliest first, until the report
// is done. It reports whether it fetched any.
func (p *player) fetch(ctx context.Context, static bool) (bool, error) {
	progressed := false
	for !p.done() {
		var next *track
		var seg SegmentURL
		for _, t := range p.tracks {
			if s, ok := t.pending(); ok && (next == nil || at(s.Time, t.stream.Timescale) < at(seg.Time, next.stream.Timescale)) {
				next, seg = t, s
			}
		}
		if next == nil {
			return progressed, nil
		}

		if next.init == nil || next.initURL != next.stream.Init {
			b, err := get(ctx, p.client, next.stream.Init)
			if err != nil {
				return progressed, err
			}
			if next.init, err = mp4.ParseInit(b); err != nil {
				return progressed, fmt.Errorf("%v: %w", next.stream.Init, err)
			}
			next.initURL = next.stream.Init
		}
		b, err := get(ctx, p.client, seg.URL)
		if err != nil {
			return progressed, err
		}
		samples, err := mp4.ParseFragments(next.init, b)
		if err != nil {
			return progressed, fmt.Errorf("%v: %w", seg.URL, err)
		}
		p.pending = append(p.pending, samples...)
		next.last, next.fetched, progressed = seg.Number, true, true
		next.end = at(seg.Time+seg.Duration, next.stream.Timescale)

		// Every stream has its media up to the earliest end; in a static
		// presentation, a stream with nothing left to fetch has all of it.
		safe := time.Duration(math.MaxInt64)
		for _, t := range p.tracks {
			if _, more := t.pending(); more || !static {
				safe = min(safe, t.end)
			}
		}
		if err := p.flush(safe); err != nil {
			return progressed, err
		}
	}
	return progressed, nil
}

// PlayHTTP plays a DASH MPD URL, and analyzes the segments of its first
// video and audio representations in time order until the report meets u,
// checked after each segment, or, for the zero Until, until a static
// presentation ends. A dynamic MPD, or one that is not there yet (404), is
// fetched again until it has new segments or ctx ends. A nil client is
// http.DefaultClient.
func PlayHTTP(ctx context.Context, client *http.Client, raw string, u report.Until) (*report.Report, error) {
	if client == nil {
		client = http.DefaultClient
	}
	base, err := url.Parse(raw)
	if err != nil {
		return nil, err
	}
	p := &player{client: client, u: u, tracks: map[string]*track{}}
	r := &p.a.Report
	// The report is nil until an MPD is read, like the other players before
	// they connect.
	read := false
	fail := func(err error) (*report.Report, error) {
		if !read {
			return nil, err
		}
		if ferr := p.flush(time.Duration(math.MaxInt64)); ferr != nil && err == nil {
			err = ferr
		}
		if ctx.Err() != nil {
			return r, fmt.Errorf("%w before %v: %v; last %v", ctx.Err(), u, r, err)
		}
		return r, err
	}

	for {
		b, err := get(ctx, client, raw)
		if err != nil && !errors.Is(err, errNotFound) {
			return fail(err)
		}
		progressed := false
		if err == nil {
			read = true
			m, err := ParseMPD(b)
			if err != nil {
				return fail(fmt.Errorf("%v: %w", raw, err))
			}
			streams, err := m.Streams(base)
			if err != nil {
				return fail(fmt.Errorf("%v: %w", raw, err))
			}
			if len(streams) == 0 {
				return fail(fmt.Errorf("%v: no audio or video representation", raw))
			}
			for _, s := range streams {
				key := s.Kind + "/" + s.Representation.ID
				if t, ok := p.tracks[key]; ok {
					t.stream = s
				} else {
					p.tracks[key] = &track{stream: s}
				}
			}

			static := m.Type != "dynamic"
			if progressed, err = p.fetch(ctx, static); err != nil {
				return fail(err)
			}
			if p.done() {
				return r, nil
			}
			if static {
				if err := p.flush(time.Duration(math.MaxInt64)); err != nil {
					return r, err
				}
				if u == (report.Until{}) || p.done() {
					return r, nil
				}
				return r, fmt.Errorf("presentation ended before %v: %v", u, r)
			}
		}
		if progressed {
			continue
		}

		select {
		case <-ctx.Done():
			if err == nil {
				err = errors.New("no new segment")
			}
			return fail(err)
		case <-time.After(pollInterval):
		}
	}
}

var isoDuration = regexp.MustCompile(`^P(?:(\d+)D)?(?:T(?:(\d+)H)?(?:(\d+)M)?(?:(\d+(?:\.\d+)?)S)?)?$`)

// Duration parses an ISO 8601 duration of an MPD attribute, such as
// PT5.000S, with days, hours, minutes and seconds.
func Duration(s string) (time.Duration, error) {
	g := isoDuration.FindStringSubmatch(s)
	if g == nil || s == "P" || strings.HasSuffix(s, "T") {
		return 0, fmt.Errorf("duration %q: not P[nD][T[nH][nM][nS]]", s)
	}
	var d time.Duration
	for i, unit := range []time.Duration{24 * time.Hour, time.Hour, time.Minute} {
		if g[i+1] != "" {
			n, err := strconv.ParseInt(g[i+1], 10, 64)
			if err != nil {
				return 0, fmt.Errorf("duration %q: %w", s, err)
			}
			d += time.Duration(n) * unit
		}
	}
	if g[4] != "" {
		f, err := strconv.ParseFloat(g[4], 64)
		if err != nil {
			return 0, fmt.Errorf("duration %q: %w", s, err)
		}
		d += time.Duration(math.Round(f * float64(time.Second)))
	}
	return d, nil
}

// startClockLag is how long before a publish begins a server may take its
// availabilityStartTime: it reads a clock it updates only now and then, such
// as SRS's clock cached by a 20ms timer that runs late under load, so a
// publish that begins just after a second boundary can get the second
// before it.
const startClockLag = time.Second

// IsPublishStart reports whether available, an availabilityStartTime in
// whole seconds, is the start of a publish that began at start, allowing
// the server's clock lag, and is no later than now.
func IsPublishStart(available, start, now time.Time) bool {
	return !available.Before(start.Add(-startClockLag).Truncate(time.Second)) && !available.After(now)
}
