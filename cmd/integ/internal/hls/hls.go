// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

// Package hls parses m3u8 playlists, master and media, with their keys and
// init maps, and plays HLS: it fetches the segments, TS or fmp4, decrypts
// AES-128 ones, and analyzes them into the stream report.
package hls

import (
	"bufio"
	"bytes"
	"context"
	"crypto/aes"
	"crypto/cipher"
	"encoding/binary"
	"encoding/hex"
	"errors"
	"fmt"
	"io"
	"net/http"
	"net/url"
	"strconv"
	"strings"
	"time"

	"srs-integ/internal/mp4"
	"srs-integ/internal/report"
	"srs-integ/internal/ts"
)

// A Playlist is a master playlist, with its variants, or a media playlist,
// with its segments.
type Playlist struct {
	Master                bool
	Version               int
	TargetDuration        int
	MediaSequence         int
	DiscontinuitySequence int
	PlaylistType          string
	EndList               bool
	Variants              []Variant
	Segments              []Segment
}

// A Variant is a stream of a master playlist.
type Variant struct {
	// URI is resolved against the playlist's URL.
	URI                string
	Bandwidth          int
	Codecs, Resolution string
}

// A Segment is a media segment.
type Segment struct {
	// URI is resolved against the playlist's URL.
	URI string
	// Sequence is its media sequence number.
	Sequence      int
	Duration      time.Duration
	Discontinuity bool
	// Key is the key of the last EXT-X-KEY before it, nil when none or
	// METHOD=NONE.
	Key *Key
	// Map is the init of the last EXT-X-MAP before it, for fmp4.
	Map *Map
}

// A Key is an EXT-X-KEY.
type Key struct {
	Method string
	// URI is resolved against the playlist's URL.
	URI string
	// IV is nil when the tag has none: the media sequence number is the IV.
	IV []byte
}

// A Map is an EXT-X-MAP, the init segment of fmp4.
type Map struct {
	URI string
}

// attributes parses an attribute list, such as METHOD=AES-128,URI="a,b".
func attributes(s string) map[string]string {
	out := map[string]string{}
	for len(s) > 0 {
		eq := strings.IndexByte(s, '=')
		if eq < 0 {
			break
		}
		name := strings.TrimSpace(s[:eq])
		s = s[eq+1:]
		var value string
		if strings.HasPrefix(s, `"`) {
			end := strings.IndexByte(s[1:], '"')
			if end < 0 {
				value, s = s[1:], ""
			} else {
				value, s = s[1:1+end], s[2+end:]
			}
		} else if comma := strings.IndexByte(s, ','); comma >= 0 {
			value, s = s[:comma], s[comma:]
		} else {
			value, s = s, ""
		}
		out[name] = value
		s = strings.TrimPrefix(s, ",")
	}
	return out
}

// Parse parses a playlist, and resolves its URIs against base, the
// playlist's URL.
func Parse(b []byte, base *url.URL) (*Playlist, error) {
	resolve := func(ref string) (string, error) {
		u, err := url.Parse(ref)
		if err != nil {
			return "", err
		}
		return base.ResolveReference(u).String(), nil
	}
	atoi := func(tag, v string) (int, error) {
		n, err := strconv.Atoi(strings.TrimSpace(v))
		if err != nil {
			return 0, fmt.Errorf("%v: %w", tag, err)
		}
		return n, nil
	}

	p := &Playlist{}
	sc := bufio.NewScanner(bytes.NewReader(b))
	sc.Buffer(nil, 1<<20)
	header := false
	var key *Key
	var init *Map
	var duration *time.Duration
	var discontinuity bool
	var variant *Variant
	seq := 0
	for n := 1; sc.Scan(); n++ {
		line := strings.TrimSpace(sc.Text())
		if line == "" {
			continue
		}
		if !header {
			if line != "#EXTM3U" {
				return nil, fmt.Errorf("line %v: %q, want #EXTM3U first", n, line)
			}
			header = true
			continue
		}

		if !strings.HasPrefix(line, "#") {
			uri, err := resolve(line)
			if err != nil {
				return nil, fmt.Errorf("line %v: %w", n, err)
			}
			if variant != nil {
				variant.URI = uri
				p.Variants = append(p.Variants, *variant)
				variant = nil
				continue
			}
			if duration == nil {
				return nil, fmt.Errorf("line %v: segment %v without #EXTINF", n, line)
			}
			p.Segments = append(p.Segments, Segment{
				URI: uri, Sequence: p.MediaSequence + seq, Duration: *duration,
				Discontinuity: discontinuity, Key: key, Map: init,
			})
			seq++
			duration, discontinuity = nil, false
			continue
		}

		tag, value, _ := strings.Cut(line, ":")
		var err error
		switch tag {
		case "#EXT-X-VERSION":
			p.Version, err = atoi(tag, value)
		case "#EXT-X-TARGETDURATION":
			p.TargetDuration, err = atoi(tag, value)
		case "#EXT-X-MEDIA-SEQUENCE":
			p.MediaSequence, err = atoi(tag, value)
		case "#EXT-X-DISCONTINUITY-SEQUENCE":
			p.DiscontinuitySequence, err = atoi(tag, value)
		case "#EXT-X-PLAYLIST-TYPE":
			p.PlaylistType = value
		case "#EXT-X-ENDLIST":
			p.EndList = true
		case "#EXT-X-DISCONTINUITY":
			discontinuity = true
		case "#EXTINF":
			d, _, _ := strings.Cut(value, ",")
			f, ferr := strconv.ParseFloat(strings.TrimSpace(d), 64)
			if ferr != nil || f < 0 {
				err = fmt.Errorf("#EXTINF %q: bad duration", value)
				break
			}
			v := time.Duration(f*1000+0.5) * time.Millisecond
			duration = &v
		case "#EXT-X-KEY":
			key, err = parseKey(value, resolve)
		case "#EXT-X-MAP":
			a := attributes(value)
			if a["URI"] == "" {
				err = errors.New("#EXT-X-MAP without URI")
				break
			}
			var uri string
			if uri, err = resolve(a["URI"]); err == nil {
				init = &Map{URI: uri}
			}
		case "#EXT-X-STREAM-INF":
			a := attributes(value)
			p.Master = true
			variant = &Variant{Codecs: a["CODECS"], Resolution: a["RESOLUTION"]}
			if v, ok := a["BANDWIDTH"]; ok {
				variant.Bandwidth, err = atoi("BANDWIDTH", v)
			}
		}
		if err != nil {
			return nil, fmt.Errorf("line %v: %w", n, err)
		}
	}
	if err := sc.Err(); err != nil {
		return nil, err
	}
	if !header {
		return nil, errors.New("empty playlist, want #EXTM3U first")
	}
	if variant != nil {
		return nil, errors.New("#EXT-X-STREAM-INF without its URI")
	}
	return p, nil
}

// parseKey parses the attributes of an EXT-X-KEY; METHOD=NONE is nil.
func parseKey(value string, resolve func(string) (string, error)) (*Key, error) {
	a := attributes(value)
	if a["METHOD"] == "NONE" {
		return nil, nil
	}
	if a["URI"] == "" {
		return nil, fmt.Errorf("#EXT-X-KEY %v without URI", value)
	}
	uri, err := resolve(a["URI"])
	if err != nil {
		return nil, err
	}
	k := &Key{Method: a["METHOD"], URI: uri}
	if v, ok := a["IV"]; ok {
		h := strings.TrimPrefix(strings.TrimPrefix(v, "0x"), "0X")
		if k.IV, err = hex.DecodeString(h); err != nil || len(k.IV) != 16 {
			return nil, fmt.Errorf("#EXT-X-KEY IV %q, want 16 hex bytes", v)
		}
	}
	return k, nil
}

// pollInterval is how often a live playlist is fetched again while it has
// no new segment, or is not there yet.
const pollInterval = 50 * time.Millisecond

// A player fetches playlists and segments, and analyzes them.
type player struct {
	client *http.Client
	u      report.Until
	keys   map[string][]byte
	inits  map[string]*mp4.Init
	ts     *ts.Analyzer
	mp4    *mp4.Analyzer
	report *report.Report
}

// ErrNotFound is a 404, which a playlist that is not there yet answers.
var ErrNotFound = errors.New("not found")

func (p *player) get(ctx context.Context, url string) ([]byte, error) {
	req, err := http.NewRequestWithContext(ctx, http.MethodGet, url, nil)
	if err != nil {
		return nil, err
	}
	res, err := p.client.Do(req)
	if err != nil {
		return nil, err
	}
	defer res.Body.Close()
	if res.StatusCode == http.StatusNotFound {
		return nil, fmt.Errorf("GET %v: status %v: %w", url, res.Status, ErrNotFound)
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

// playlist fetches and parses a playlist, and follows a master playlist to
// its first variant. The URL it returns is the media playlist's.
func (p *player) playlist(ctx context.Context, raw string) (*Playlist, string, error) {
	for range 2 {
		b, err := p.get(ctx, raw)
		if err != nil {
			return nil, "", err
		}
		base, err := url.Parse(raw)
		if err != nil {
			return nil, "", err
		}
		pl, err := Parse(b, base)
		if err != nil {
			return nil, "", fmt.Errorf("%v: %w", raw, err)
		}
		if !pl.Master {
			return pl, raw, nil
		}
		if len(pl.Variants) == 0 {
			return nil, "", fmt.Errorf("%v: master playlist without variants", raw)
		}
		raw = pl.Variants[0].URI
	}
	return nil, "", fmt.Errorf("%v: a master playlist links to another", raw)
}

// segment fetches, decrypts and analyzes a segment.
func (p *player) segment(ctx context.Context, s Segment) error {
	b, err := p.get(ctx, s.URI)
	if err != nil {
		return err
	}
	if s.Key != nil {
		if b, err = p.decrypt(ctx, s, b); err != nil {
			return fmt.Errorf("%v: %w", s.URI, err)
		}
	}

	if s.Map != nil {
		init, ok := p.inits[s.Map.URI]
		if !ok {
			ib, err := p.get(ctx, s.Map.URI)
			if err != nil {
				return err
			}
			if init, err = mp4.ParseInit(ib); err != nil {
				return fmt.Errorf("%v: %w", s.Map.URI, err)
			}
			p.inits[s.Map.URI] = init
		}
		if p.ts != nil {
			return fmt.Errorf("%v: fmp4 after TS segments", s.URI)
		}
		if p.mp4 == nil {
			p.mp4 = &mp4.Analyzer{}
			p.report = &p.mp4.Report
		}
		samples, err := mp4.ParseFragments(init, b)
		if err != nil {
			return fmt.Errorf("%v: %w", s.URI, err)
		}
		if err := p.mp4.Add(samples, p.u); err != nil {
			return fmt.Errorf("%v: %w", s.URI, err)
		}
		return nil
	}

	if p.mp4 != nil {
		return fmt.Errorf("%v: TS after fmp4 segments", s.URI)
	}
	if p.ts == nil {
		p.ts = &ts.Analyzer{}
		p.report = &p.ts.Report
	}
	d := ts.NewDemuxer(bytes.NewReader(b))
	for p.u == (report.Until{}) || !p.u.Done(p.report) {
		pes, err := d.ReadPES()
		if err == io.EOF {
			break
		} else if err != nil {
			return fmt.Errorf("%v: %w", s.URI, err)
		}
		if err := p.ts.Add(pes); err != nil {
			return fmt.Errorf("%v: %w", s.URI, err)
		}
	}
	return nil
}

// decrypt decrypts an AES-128 segment with the key of its key URI, fetched
// once.
func (p *player) decrypt(ctx context.Context, s Segment, b []byte) ([]byte, error) {
	if s.Key.Method != "AES-128" {
		return nil, fmt.Errorf("key method %v is not supported", s.Key.Method)
	}
	key, ok := p.keys[s.Key.URI]
	if !ok {
		var err error
		if key, err = p.get(ctx, s.Key.URI); err != nil {
			return nil, err
		}
		p.keys[s.Key.URI] = key
	}
	return Decrypt(key, s, b)
}

// tsPacketSize is the size of a TS packet.
const tsPacketSize = 188

// ErrNoPadding is a segment of whole TS packets that decrypts with its key
// but ends without the PKCS#7 padding block.
var ErrNoPadding = errors.New("TS packets with no PKCS#7 padding")

// Decrypt decrypts the data of an AES-128 segment with key: CBC with PKCS#7
// padding, with the IV of the segment's key or, without one, its media
// sequence number.
func Decrypt(key []byte, s Segment, b []byte) ([]byte, error) {
	if s.Key == nil || s.Key.Method != "AES-128" {
		return nil, fmt.Errorf("segment %v: key %+v, want AES-128", s.Sequence, s.Key)
	}
	block, err := aes.NewCipher(key)
	if err != nil {
		return nil, fmt.Errorf("key %v: %w", s.Key.URI, err)
	}
	iv := s.Key.IV
	if iv == nil {
		iv = make([]byte, 16)
		binary.BigEndian.PutUint64(iv[8:], uint64(s.Sequence))
	}
	if len(b) == 0 || len(b)%aes.BlockSize != 0 {
		return nil, fmt.Errorf("%v bytes, not whole AES blocks", len(b))
	}
	out := make([]byte, len(b))
	cipher.NewCBCDecrypter(block, iv).CryptBlocks(out, b)
	pad := int(out[len(out)-1])
	if pad < 1 || pad > aes.BlockSize || !bytes.Equal(out[len(out)-pad:], bytes.Repeat([]byte{byte(pad)}, pad)) {
		if len(out)%tsPacketSize == 0 && out[0] == 0x47 && out[len(out)-tsPacketSize] == 0x47 {
			return nil, fmt.Errorf("%v bytes: %w", len(out), ErrNoPadding)
		}
		return nil, errors.New("bad padding, a wrong key or IV")
	}
	return out[:len(out)-pad], nil
}

// PlayHTTP plays an HLS URL, a master or media playlist, and analyzes its
// segments in order until the report meets u, checked after each PES of a
// TS segment and after each fmp4 segment, or, for the zero Until, until the
// playlist ends. A live playlist, or one that is not there yet (404), is
// fetched again until it has new segments or ctx ends. A nil client is
// http.DefaultClient.
func PlayHTTP(ctx context.Context, client *http.Client, raw string, u report.Until) (*report.Report, error) {
	if client == nil {
		client = http.DefaultClient
	}
	p := &player{client: client, u: u, keys: map[string][]byte{}, inits: map[string]*mp4.Init{}, report: &report.Report{}}
	done := func() bool { return u != (report.Until{}) && u.Done(p.report) }
	// The report is nil until a playlist is read, like the other players
	// before they connect.
	read := false
	fail := func(err error) (*report.Report, error) {
		r := p.report
		if !read {
			r = nil
		}
		if ctx.Err() != nil {
			return r, fmt.Errorf("%w before %v: %v; last %v", ctx.Err(), u, p.report, err)
		}
		return r, err
	}

	next := -1 // The sequence number of the next segment.
	for {
		pl, media, err := p.playlist(ctx, raw)
		if err != nil && !errors.Is(err, ErrNotFound) {
			return fail(err)
		}
		if err == nil {
			raw = media
			read = true
			fresh := false
			for _, s := range pl.Segments {
				if s.Sequence < next {
					continue
				}
				if err := p.segment(ctx, s); err != nil {
					return fail(err)
				}
				next, fresh = s.Sequence+1, true
				if done() {
					return p.report, nil
				}
			}
			if pl.EndList || pl.PlaylistType == "VOD" {
				if u == (report.Until{}) {
					return p.report, nil
				}
				return p.report, fmt.Errorf("playlist ended before %v: %v", u, p.report)
			}
			if fresh {
				continue
			}
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

// Fetch fetches a playlist and follows a master playlist to its first
// variant. It returns the media playlist and its URL. A 404 is ErrNotFound.
// A nil client is http.DefaultClient.
func Fetch(ctx context.Context, client *http.Client, raw string) (*Playlist, string, error) {
	if client == nil {
		client = http.DefaultClient
	}
	p := &player{client: client}
	return p.playlist(ctx, raw)
}
