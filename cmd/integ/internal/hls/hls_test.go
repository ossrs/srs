// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT
package hls

import (
	"bytes"
	"context"
	"crypto/aes"
	"crypto/cipher"
	"errors"
	"fmt"
	"net/http"
	"net/http/httptest"
	"net/url"
	"os"
	"path/filepath"
	"strings"
	"sync"
	"sync/atomic"
	"testing"
	"time"

	"srs-integ/internal/media"
	"srs-integ/internal/report"
)

func mustURL(t *testing.T, s string) *url.URL {
	t.Helper()
	u, err := url.Parse(s)
	if err != nil {
		t.Fatal(err)
	}
	return u
}

// TestParseMedia parses a media playlist like SRS writes, with a key, a
// discontinuity and relative URIs.
func TestParseMedia(t *testing.T) {
	text := `#EXTM3U
#EXT-X-VERSION:3
#EXT-X-MEDIA-SEQUENCE:5
#EXT-X-TARGETDURATION:10
#EXT-X-DISCONTINUITY-SEQUENCE:1
#EXT-X-PLAYLIST-TYPE:EVENT
#EXT-X-KEY:METHOD=AES-128,URI="/live/s-5.key?token=1",IV=0x000102030405060708090a0b0c0d0e0f
#EXTINF:10.000, no desc
s-5.ts?hls_ctx=abc
#EXT-X-DISCONTINUITY
#EXTINF:9.960,
s-6.ts?hls_ctx=abc

#EXT-X-KEY:METHOD=NONE
#EXTINF:3.5,
http://other/s-7.ts
#EXT-X-ENDLIST
`
	p, err := Parse([]byte(text), mustURL(t, "http://127.0.0.1:8080/live/s.m3u8?hls_ctx=abc"))
	if err != nil {
		t.Fatal(err)
	}
	if p.Master || p.Version != 3 || p.TargetDuration != 10 || p.MediaSequence != 5 || p.DiscontinuitySequence != 1 || p.PlaylistType != "EVENT" || !p.EndList {
		t.Errorf("playlist %+v", p)
	}
	if len(p.Segments) != 3 {
		t.Fatalf("%v segments, want 3", len(p.Segments))
	}
	s0, s1, s2 := p.Segments[0], p.Segments[1], p.Segments[2]
	if s0.URI != "http://127.0.0.1:8080/live/s-5.ts?hls_ctx=abc" || s0.Sequence != 5 || s0.Duration != 10*time.Second || s0.Discontinuity {
		t.Errorf("segment 0 %+v", s0)
	}
	if s1.Sequence != 6 || s1.Duration != 9960*time.Millisecond || !s1.Discontinuity {
		t.Errorf("segment 1 %+v", s1)
	}
	if s2.URI != "http://other/s-7.ts" || s2.Sequence != 7 || s2.Duration != 3500*time.Millisecond {
		t.Errorf("segment 2 %+v", s2)
	}
	for i, s := range []Segment{s0, s1} {
		k := s.Key
		if k == nil || k.Method != "AES-128" || k.URI != "http://127.0.0.1:8080/live/s-5.key?token=1" || len(k.IV) != 16 || k.IV[15] != 0x0f {
			t.Errorf("segment %v key %+v", i, k)
		}
	}
	if s2.Key != nil {
		t.Errorf("segment 2 key %+v after METHOD=NONE", s2.Key)
	}
}

// TestParseMap parses an fmp4 playlist: every segment has the init of the
// EXT-X-MAP before it.
func TestParseMap(t *testing.T) {
	text := "#EXTM3U\n#EXT-X-VERSION:7\n#EXT-X-TARGETDURATION:2\n#EXT-X-MAP:URI=\"init.mp4\"\n#EXTINF:2,\na0.m4s\n#EXTINF:2,\na1.m4s\n"
	p, err := Parse([]byte(text), mustURL(t, "http://h/live/a.m3u8"))
	if err != nil {
		t.Fatal(err)
	}
	if p.EndList || len(p.Segments) != 2 {
		t.Fatalf("playlist %+v", p)
	}
	for _, s := range p.Segments {
		if s.Map == nil || s.Map.URI != "http://h/live/init.mp4" {
			t.Errorf("segment %v map %+v", s.URI, s.Map)
		}
	}
}

// TestParseMaster parses a master playlist like SRS writes for a session.
func TestParseMaster(t *testing.T) {
	text := "#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=1,AVERAGE-BANDWIDTH=1,CODECS=\"avc1.64001e,mp4a.40.2\",RESOLUTION=768x320\n/live/s.m3u8?hls_ctx=abc\n"
	p, err := Parse([]byte(text), mustURL(t, "http://127.0.0.1:8080/live/s.m3u8"))
	if err != nil {
		t.Fatal(err)
	}
	if !p.Master || len(p.Variants) != 1 || len(p.Segments) != 0 {
		t.Fatalf("playlist %+v", p)
	}
	v := p.Variants[0]
	if v.URI != "http://127.0.0.1:8080/live/s.m3u8?hls_ctx=abc" || v.Bandwidth != 1 || v.Codecs != "avc1.64001e,mp4a.40.2" || v.Resolution != "768x320" {
		t.Errorf("variant %+v", v)
	}
}

func TestParseErrors(t *testing.T) {
	cases := []struct {
		name, text, want string
	}{
		{"NoHeader", "#EXT-X-VERSION:3\n", "#EXTM3U"},
		{"BadDuration", "#EXTM3U\n#EXTINF:abc,\na.ts\n", "EXTINF"},
		{"NoDuration", "#EXTM3U\na.ts\n", "EXTINF"},
		{"BadIV", "#EXTM3U\n#EXT-X-KEY:METHOD=AES-128,URI=\"k\",IV=0x12\n", "IV"},
		{"KeyNoURI", "#EXTM3U\n#EXT-X-KEY:METHOD=AES-128\n", "URI"},
		{"BadSequence", "#EXTM3U\n#EXT-X-MEDIA-SEQUENCE:x\n", "MEDIA-SEQUENCE"},
		{"StreamNoURI", "#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=1\n", "STREAM-INF"},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			_, err := Parse([]byte(tc.text), mustURL(t, "http://h/a.m3u8"))
			if err == nil || !strings.Contains(err.Error(), tc.want) {
				t.Errorf("error %v, want it to contain %q", err, tc.want)
			}
		})
	}
}

// tsSync is the A/V sync of the test file in TS made by ffmpeg, whose muxer
// writes audio up to about 300ms ahead of the video.
const tsSync = 400 * time.Millisecond

// requests counts the requests of a test server by path.
type requests struct {
	mu sync.Mutex
	n  map[string]int
}

func (r *requests) add(p string) {
	r.mu.Lock()
	defer r.mu.Unlock()
	if r.n == nil {
		r.n = map[string]int{}
	}
	r.n[p]++
}

func (r *requests) count(suffix string) int {
	r.mu.Lock()
	defer r.mu.Unlock()
	n := 0
	for p, c := range r.n {
		if strings.HasSuffix(p, suffix) {
			n += c
		}
	}
	return n
}

// serve serves dir, counting the requests, with handlers by path first.
func serve(t *testing.T, dir string, handlers map[string]http.HandlerFunc) (*httptest.Server, *requests) {
	t.Helper()
	reqs := &requests{}
	files := http.FileServer(http.Dir(dir))
	s := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		reqs.add(r.URL.Path)
		if h, ok := handlers[r.URL.Path]; ok {
			h(w, r)
			return
		}
		files.ServeHTTP(w, r)
	}))
	t.Cleanup(s.Close)
	return s, reqs
}

// checkWhole checks the report of the whole test file, with A/V sync within
// maxSync.
func checkWhole(t *testing.T, r *report.Report, maxSync time.Duration) {
	t.Helper()
	if r.Video == nil || r.Audio == nil {
		t.Fatalf("report %v", r)
	}
	v, a := r.Video, r.Audio
	if v.Codec != "h264" || v.Profile != "High" || v.Width != 768 || v.Height != 320 || v.SequenceHeaders != 1 {
		t.Errorf("video %v", r)
	}
	if v.Frames != 750 || v.Keyframes != 30 {
		t.Errorf("%v frames and %v keyframes, want 750 and 30", v.Frames, v.Keyframes)
	}
	if a.Codec != "aac" || a.SampleRate != 44100 || a.Channels != 2 || a.SequenceHeaders != 1 {
		t.Errorf("audio %v", r)
	}
	if err := r.CheckTimestamps(100 * time.Millisecond); err != nil {
		t.Error(err)
	}
	if r.AVSync > maxSync {
		t.Errorf("A/V sync %v", r.AVSync)
	}
}

// TestPlayHTTP plays the test file as a VOD playlist of TS and of fmp4
// segments, to the end.
func TestPlayHTTP(t *testing.T) {
	cases := []struct {
		name string
		args []string
		sync time.Duration
	}{
		{"Ts", nil, tsSync},
		{"Fmp4", []string{"-hls_segment_type", "fmp4"}, 100 * time.Millisecond},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			args := append([]string{"-hls_time", "2", "-hls_playlist_type", "vod"}, tc.args...)
			dir := media.RemuxDir(t, media.H264HighAAC, "hls", "index.m3u8", args...)
			s, _ := serve(t, dir, nil)
			r, err := PlayHTTP(context.Background(), nil, s.URL+"/index.m3u8", report.Until{})
			if err != nil {
				t.Fatal(err)
			}
			checkWhole(t, r, tc.sync)
		})
	}
}

// TestPlayHTTPUntil stops after the segment that meets the condition, and
// fetches no more.
func TestPlayHTTPUntil(t *testing.T) {
	dir := media.RemuxDir(t, media.H264HighAAC, "hls", "index.m3u8", "-hls_time", "2", "-hls_playlist_type", "vod")
	s, reqs := serve(t, dir, nil)
	r, err := PlayHTTP(context.Background(), nil, s.URL+"/index.m3u8", report.Until{Keyframes: 3})
	if err != nil {
		t.Fatal(err)
	}
	if r.Video.Keyframes < 3 || r.Video.Frames >= 750 {
		t.Errorf("%v keyframes and %v frames, want at least 3 and not the whole file", r.Video.Keyframes, r.Video.Frames)
	}
	if n := reqs.count(".ts"); n != 2 {
		t.Errorf("%v segments fetched, want 2", n)
	}

	_, err = PlayHTTP(context.Background(), nil, s.URL+"/index.m3u8", report.Until{Keyframes: 100})
	if err == nil || !strings.Contains(err.Error(), "100 keyframes") {
		t.Errorf("error %v, want the playlist to end before 100 keyframes", err)
	}
}

// TestPlayHTTPEncrypted plays AES-128 segments: with the IV of the key tag,
// and with the media sequence number as the IV. ffmpeg takes the 16
// characters of -hls_enc_key and -hls_enc_iv as the bytes, not as hex.
func TestPlayHTTPEncrypted(t *testing.T) {
	key := []byte("0123456789abcdef")
	keyHandler := func(w http.ResponseWriter, r *http.Request) { w.Write(key) }

	t.Run("IV", func(t *testing.T) {
		dir := media.RemuxDir(t, media.H264HighAAC, "hls", "index.m3u8", "-hls_time", "2", "-hls_playlist_type", "vod",
			"-hls_enc", "1", "-hls_enc_key", string(key), "-hls_enc_key_url", "key.bin",
			"-hls_enc_iv", "fedcba9876543210")
		s, _ := serve(t, dir, map[string]http.HandlerFunc{"/key.bin": keyHandler})
		r, err := PlayHTTP(context.Background(), nil, s.URL+"/index.m3u8", report.Until{})
		if err != nil {
			t.Fatal(err)
		}
		checkWhole(t, r, tsSync)
	})

	t.Run("SequenceIV", func(t *testing.T) {
		dir := media.RemuxDir(t, media.H264HighAAC, "hls", "index.m3u8", "-hls_time", "10", "-hls_playlist_type", "vod")
		playlist := "#EXTM3U\n#EXT-X-TARGETDURATION:10\n#EXT-X-MEDIA-SEQUENCE:7\n#EXT-X-KEY:METHOD=AES-128,URI=\"key.bin\"\n"
		segments, _ := filepath.Glob(filepath.Join(dir, "index*.ts"))
		if len(segments) < 3 {
			t.Fatalf("segments %v, want at least 3", segments)
		}
		for i := range segments {
			b, err := os.ReadFile(filepath.Join(dir, fmt.Sprintf("index%v.ts", i)))
			if err != nil {
				t.Fatal(err)
			}
			iv := make([]byte, 16)
			iv[15] = byte(7 + i)
			if err := os.WriteFile(filepath.Join(dir, fmt.Sprintf("enc%v.ts", i)), encrypt(t, key, iv, b), 0o644); err != nil {
				t.Fatal(err)
			}
			playlist += fmt.Sprintf("#EXTINF:10,\nenc%v.ts\n", i)
		}
		playlist += "#EXT-X-ENDLIST\n"
		s, _ := serve(t, dir, map[string]http.HandlerFunc{
			"/key.bin": keyHandler,
			"/enc.m3u8": func(w http.ResponseWriter, r *http.Request) {
				w.Write([]byte(playlist))
			},
		})
		r, err := PlayHTTP(context.Background(), nil, s.URL+"/enc.m3u8", report.Until{})
		if err != nil {
			t.Fatal(err)
		}
		checkWhole(t, r, tsSync)
	})

	t.Run("WrongKey", func(t *testing.T) {
		dir := media.RemuxDir(t, media.H264HighAAC, "hls", "index.m3u8", "-hls_time", "2", "-hls_playlist_type", "vod",
			"-hls_enc", "1", "-hls_enc_key", string(key), "-hls_enc_key_url", "key.bin")
		s, _ := serve(t, dir, map[string]http.HandlerFunc{"/key.bin": func(w http.ResponseWriter, r *http.Request) {
			w.Write([]byte("fedcba9876543210"))
		}})
		if _, err := PlayHTTP(context.Background(), nil, s.URL+"/index.m3u8", report.Until{}); err == nil {
			t.Error("segments decrypted with the wrong key")
		}
	})
}

// encrypt encrypts b with AES-128-CBC and PKCS#7 padding.
func encrypt(t *testing.T, key, iv, b []byte) []byte {
	t.Helper()
	block, err := aes.NewCipher(key)
	if err != nil {
		t.Fatal(err)
	}
	pad := 16 - len(b)%16
	out := append(append([]byte{}, b...), bytes.Repeat([]byte{byte(pad)}, pad)...)
	cipher.NewCBCEncrypter(block, iv).CryptBlocks(out, out)
	return out
}

// TestDecryptNoPadding decrypts a segment of whole TS packets that was
// encrypted without the PKCS#7 padding block: the error says so, apart from
// a wrong key.
func TestDecryptNoPadding(t *testing.T) {
	key := []byte("0123456789abcdef")
	iv := []byte("ivivivivivivivii")
	plain := bytes.Repeat(append([]byte{0x47}, bytes.Repeat([]byte{0xff}, 187)...), 4)
	block, err := aes.NewCipher(key)
	if err != nil {
		t.Fatal(err)
	}
	b := make([]byte, len(plain))
	cipher.NewCBCEncrypter(block, iv).CryptBlocks(b, plain)

	s := Segment{Key: &Key{Method: "AES-128", IV: iv}}
	if _, err := Decrypt(key, s, b); !errors.Is(err, ErrNoPadding) {
		t.Errorf("no padding: %v, want ErrNoPadding", err)
	}
	if _, err := Decrypt([]byte("fedcba9876543210"), s, b); err == nil || errors.Is(err, ErrNoPadding) {
		t.Errorf("wrong key: %v, want an error that is not ErrNoPadding", err)
	}
	if _, err := Decrypt(key, s, encrypt(t, key, iv, plain)); err != nil {
		t.Errorf("with padding: %v", err)
	}
}

// TestPlayHTTPLive plays a live playlist through a master playlist, like an
// SRS session: the media playlist is first missing, then gains a segment at
// each request, until the condition is met.
func TestPlayHTTPLive(t *testing.T) {
	dir := media.RemuxDir(t, media.H264HighAAC, "hls", "index.m3u8", "-hls_time", "2", "-hls_playlist_type", "vod")
	var n atomic.Int32
	live := func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Query().Get("ctx") != "1" {
			http.Error(w, "no ctx", http.StatusBadRequest)
			return
		}
		k := int(n.Add(1)) - 1
		if k == 0 {
			http.NotFound(w, r)
			return
		}
		text := "#EXTM3U\n#EXT-X-TARGETDURATION:2\n#EXT-X-MEDIA-SEQUENCE:0\n"
		for i := range min(k, 15) {
			text += fmt.Sprintf("#EXTINF:2,\nindex%v.ts\n", i)
		}
		w.Write([]byte(text))
	}
	s, reqs := serve(t, dir, map[string]http.HandlerFunc{
		"/master.m3u8": func(w http.ResponseWriter, r *http.Request) {
			w.Write([]byte("#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=1\nlive.m3u8?ctx=1\n"))
		},
		"/live.m3u8": live,
	})

	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	r, err := PlayHTTP(ctx, nil, s.URL+"/master.m3u8", report.Until{Keyframes: 6})
	if err != nil {
		t.Fatal(err)
	}
	if r.Video.Keyframes != 6 {
		t.Errorf("%v keyframes, want 6 from three 2s segments", r.Video.Keyframes)
	}
	if got := reqs.count(".ts"); got != 3 {
		t.Errorf("%v segments fetched, want 3, each once", got)
	}

	// A live playlist that stops growing ends with the context, which the
	// error names with what it waited for.
	ctx, cancel = context.WithTimeout(context.Background(), 300*time.Millisecond)
	defer cancel()
	stuck := func(w http.ResponseWriter, r *http.Request) {
		w.Write([]byte("#EXTM3U\n#EXT-X-TARGETDURATION:2\n#EXTINF:2,\nindex0.ts\n"))
	}
	s2, _ := serve(t, dir, map[string]http.HandlerFunc{"/stuck.m3u8": stuck})
	r, err = PlayHTTP(ctx, nil, s2.URL+"/stuck.m3u8", report.Until{Keyframes: 6})
	if !errors.Is(err, context.DeadlineExceeded) || !strings.Contains(err.Error(), "6 keyframes") {
		t.Errorf("error %v, want the deadline before 6 keyframes", err)
	}
	if r == nil || r.Video == nil || r.Video.Keyframes != 2 {
		t.Errorf("report %v, want the 2 keyframes of the one segment", r)
	}
}

// TestPlayHTTPErrors fails on a missing segment, and on a playlist that
// is never there.
func TestPlayHTTPErrors(t *testing.T) {
	dir := media.RemuxDir(t, media.H264HighAAC, "hls", "index.m3u8", "-hls_time", "2", "-hls_playlist_type", "vod")
	if err := os.Remove(filepath.Join(dir, "index1.ts")); err != nil {
		t.Fatal(err)
	}
	s, _ := serve(t, dir, nil)
	_, err := PlayHTTP(context.Background(), nil, s.URL+"/index.m3u8", report.Until{})
	if err == nil || !strings.Contains(err.Error(), "index1.ts") || !strings.Contains(err.Error(), "404") {
		t.Errorf("error %v, want the 404 of index1.ts", err)
	}

	ctx, cancel := context.WithTimeout(context.Background(), 200*time.Millisecond)
	defer cancel()
	_, err = PlayHTTP(ctx, nil, s.URL+"/nothing.m3u8", report.Until{Keyframes: 1})
	if !errors.Is(err, context.DeadlineExceeded) || !strings.Contains(err.Error(), "404") {
		t.Errorf("error %v, want the deadline after a 404", err)
	}
}

// TestFetch follows a master playlist to its media playlist, and returns it
// with its URL, query and all.
func TestFetch(t *testing.T) {
	master := "#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=1\n/live/s.m3u8?hls_ctx=abc\n"
	media := "#EXTM3U\n#EXT-X-VERSION:3\n#EXT-X-MEDIA-SEQUENCE:4\n#EXT-X-TARGETDURATION:3\n#EXTINF:3.000, no desc\ns-4.ts?hls_ctx=abc\n"
	s, _ := serve(t, t.TempDir(), map[string]http.HandlerFunc{
		"/live/s.m3u8": func(w http.ResponseWriter, r *http.Request) {
			if r.URL.Query().Get("hls_ctx") == "" {
				w.Write([]byte(master))
				return
			}
			w.Write([]byte(media))
		},
	})

	pl, u, err := Fetch(context.Background(), nil, s.URL+"/live/s.m3u8")
	if err != nil {
		t.Fatal(err)
	}
	if want := s.URL + "/live/s.m3u8?hls_ctx=abc"; u != want {
		t.Errorf("URL %v, want %v", u, want)
	}
	if pl.Master || pl.MediaSequence != 4 || pl.TargetDuration != 3 || len(pl.Segments) != 1 {
		t.Fatalf("playlist %+v, want the media playlist", pl)
	}
	if want := s.URL + "/live/s-4.ts?hls_ctx=abc"; pl.Segments[0].URI != want || pl.Segments[0].Sequence != 4 {
		t.Errorf("segment %+v, want %v as 4", pl.Segments[0], want)
	}

	// A media playlist is returned as it is.
	pl, u, err = Fetch(context.Background(), nil, s.URL+"/live/s.m3u8?hls_ctx=abc")
	if err != nil || pl.Master || u != s.URL+"/live/s.m3u8?hls_ctx=abc" {
		t.Errorf("playlist %+v at %v, %v, want the media playlist", pl, u, err)
	}

	_, _, err = Fetch(context.Background(), nil, s.URL+"/live/none.m3u8")
	if !errors.Is(err, ErrNotFound) {
		t.Errorf("error %v, want ErrNotFound", err)
	}
}

// TestDecrypt decrypts AES-128 segment data: with the key's IV, or the media
// sequence number when the key has none; a wrong key or a partial block is
// an error.
func TestDecrypt(t *testing.T) {
	key := []byte("0123456789abcdef")
	iv := []byte("ivivivivivivivii")
	plain := bytes.Repeat([]byte{0x47, 1, 2, 3}, 47)

	got, err := Decrypt(key, Segment{Sequence: 9, Key: &Key{Method: "AES-128", IV: iv}}, encrypt(t, key, iv, plain))
	if err != nil || !bytes.Equal(got, plain) {
		t.Errorf("with the key's IV: %v, %x", err, got)
	}

	seqIV := make([]byte, 16)
	seqIV[15] = 9
	got, err = Decrypt(key, Segment{Sequence: 9, Key: &Key{Method: "AES-128"}}, encrypt(t, key, seqIV, plain))
	if err != nil || !bytes.Equal(got, plain) {
		t.Errorf("with the sequence as IV: %v, %x", err, got)
	}

	for _, tc := range []struct {
		name string
		key  []byte
		s    Segment
		b    []byte
	}{
		{"WrongKey", []byte("fedcba9876543210"), Segment{Key: &Key{Method: "AES-128", IV: iv}}, encrypt(t, key, iv, plain)},
		{"PartialBlock", key, Segment{Key: &Key{Method: "AES-128", IV: iv}}, encrypt(t, key, iv, plain)[1:]},
		{"Empty", key, Segment{Key: &Key{Method: "AES-128", IV: iv}}, nil},
		{"SampleAES", key, Segment{Key: &Key{Method: "SAMPLE-AES", IV: iv}}, encrypt(t, key, iv, plain)},
		{"ShortKey", key[:8], Segment{Key: &Key{Method: "AES-128", IV: iv}}, encrypt(t, key, iv, plain)},
		{"NoKey", key, Segment{}, encrypt(t, key, iv, plain)},
	} {
		if _, err := Decrypt(tc.key, tc.s, tc.b); err == nil {
			t.Errorf("%v: no error", tc.name)
		}
	}
}
