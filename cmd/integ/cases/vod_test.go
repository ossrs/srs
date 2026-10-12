// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package cases

import (
	"bufio"
	"bytes"
	"context"
	"encoding/binary"
	"fmt"
	"io"
	"net/http"
	"net/url"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"testing"
	"time"

	"srs-integ/internal/check"
	"srs-integ/internal/flv"
	"srs-integ/internal/hls"
	"srs-integ/internal/media"
	"srs-integ/internal/report"
	"srs-integ/internal/server"
)

// vodMp4 is the MP4 file the range cases serve.
const vodMp4 = media.H264BaselineOpus

// vodStart is the media time from which the FLV cases ask for a file: the
// first keyframe, or audio frame, at or after it.
const vodStart = 10 * time.Second

// vodMp4Cases adds the MP4 VOD cases to TestHttpStatic_Common. SRS serves an
// .mp4 file of its dirs by byte ranges, asked by the query ?range= or
// ?bytes=, or by the Range header: 206 with the bytes from the start to the
// end, both included, and Content-Range. Each case copies the test file to a
// dir of its own, so each is Normal.
func vodMp4Cases(t *testing.T) {
	// Mp4Whole gets the file with no range: 200 and every byte.
	check.Case(t, "Mp4Whole", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		data, path := writeVod(c, srs.WebRoot(), vodMp4)
		res, body := staticGet(c, srs, path, nil)
		c.CheckEqual(res.StatusCode, 200, "status")
		c.CheckEqual(res.Header.Get("Content-Type"), "video/mp4", "Content-Type")
		c.CheckEqual(res.ContentLength, int64(len(data)), "Content-Length")
		c.Check(bytes.Equal(body, data), "body of %d bytes differs from the file's %d", len(body), len(data))
	})

	// Mp4RangeQuery, Mp4BytesQuery and Mp4RangeHeader ask for the bytes 100
	// to 1099 in each of the three ways: 206 and those 1000 bytes.
	for _, w := range []struct{ name, query, header string }{
		{"Mp4RangeQuery", "?range=100-1099", ""},
		{"Mp4BytesQuery", "?bytes=100-1099", ""},
		{"Mp4RangeHeader", "", "bytes=100-1099"},
	} {
		check.Case(t, w.name, check.Normal, func(c *check.C) {
			srs := server.Common(c)
			data, path := writeVod(c, srs.WebRoot(), vodMp4)
			checkRange(c, srs, path+w.query, w.header, data, 100, 1099)
		})
	}

	// Mp4RangeFromStart asks for the first bytes, which start with the
	// file's ftyp box.
	check.Case(t, "Mp4RangeFromStart", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		data, path := writeVod(c, srs.WebRoot(), vodMp4)
		body := checkRange(c, srs, path, "bytes=0-31", data, 0, 31)
		c.Check(len(body) == 32 && string(body[4:8]) == "ftyp", "the first box %q, want ftyp", body)
	})

	// Mp4RangeOpenEnd asks for the bytes from 1000 with no end: to the end
	// of the file.
	check.Case(t, "Mp4RangeOpenEnd", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		data, path := writeVod(c, srs.WebRoot(), vodMp4)
		checkRange(c, srs, path, "bytes=1000-", data, 1000, len(data)-1)
	})

	// Mp4RangeLastByte asks for the last byte alone.
	check.Case(t, "Mp4RangeLastByte", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		data, path := writeVod(c, srs.WebRoot(), vodMp4)
		n := len(data)
		checkRange(c, srs, path, fmt.Sprintf("bytes=%d-%d", n-1, n-1), data, n-1, n-1)
	})

	// Mp4RangeEndClamped asks for an end at the file size and past it: the
	// end is the last byte of the file, as RFC 9110 has it.
	check.Case(t, "Mp4RangeEndClamped", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		data, path := writeVod(c, srs.WebRoot(), vodMp4)
		n := len(data)
		checkRange(c, srs, path, fmt.Sprintf("bytes=0-%d", n), data, 0, n-1)
		checkRange(c, srs, path, fmt.Sprintf("bytes=%d-%d", n-100, n+1000), data, n-100, n-1)
		checkRange(c, srs, path+fmt.Sprintf("?range=10-%d", n*2), "", data, 10, n-1)
	})

	// Mp4RangeEndBeforeStart asks for a range whose end is before its start,
	// which is invalid, so the header is ignored: 200 and the whole file.
	check.Case(t, "Mp4RangeEndBeforeStart", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		data, path := writeVod(c, srs.WebRoot(), vodMp4)
		res, body := staticGet(c, srs, path, http.Header{"Range": {"bytes=500-100"}})
		c.CheckEqual(res.StatusCode, 200, "status")
		c.CheckEqual(res.Header.Get("Content-Range"), "", "Content-Range")
		c.Check(bytes.Equal(body, data), "body of %d bytes, want the whole file of %d", len(body), len(data))
	})

	// Mp4RangeStartPastEnd asks for a range that starts at the file size, and
	// one past it: 416 with the file size in Content-Range, and the server
	// serves the next request.
	check.Case(t, "Mp4RangeStartPastEnd", check.Normal, func(c *check.C) {
		check.KnownBug(c, "a range that starts past the end gets no answer, the connection closed, not 416")
		srs := server.Common(c)
		data, path := writeVod(c, srs.WebRoot(), vodMp4)
		n := len(data)
		for _, r := range []string{fmt.Sprintf("bytes=%d-", n), fmt.Sprintf("bytes=%d-%d", n+10, n+20)} {
			checkUnsatisfiable(c, srs, path, "Range: "+r, n)
		}
		checkStaticServes(c, srs)
	})

	// Mp4SuffixRange asks for the last 500 bytes, bytes=-500: 206 and those
	// bytes, as RFC 9110 has it.
	check.Case(t, "Mp4SuffixRange", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		data, path := writeVod(c, srs.WebRoot(), vodMp4)
		checkRange(c, srs, path, "bytes=-500", data, len(data)-500, len(data)-1)
	})

	// Mp4RangeVhostMount asks for a range of a file of the vhost's mount,
	// which SRS serves the same way.
	check.Case(t, "Mp4RangeVhostMount", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		data, path := writeVod(c, server.CommonStatic.DirIn(srs), vodMp4)
		checkRange(c, srs, strings.TrimSuffix(server.CommonStatic.URLPath(), "/")+path, "bytes=2000-2999", data, 2000, 2999)
	})

	// Mp4RangeNotFound asks for a range of a file that is not there: 404.
	check.Case(t, "Mp4RangeNotFound", check.Fast, func(c *check.C) {
		srs := server.Common(c)
		res, _ := staticGet(c, srs, "/"+streamName(c)+"/none.mp4", http.Header{"Range": {"bytes=0-99"}})
		c.CheckEqual(res.StatusCode, 404, "status")
	})
}

// TestHttpStatic_RemuxOff serves FLV files on the shared RemuxOff server
// (see sharedRemuxOff), since with the HTTP remux on, as on Common, every
// .flv path is a live stream. SRS serves an .flv file from a byte offset,
// ?start=, as a file of its own: the FLV header, the file's audio and video
// sequence headers, then the file from the offset.
func TestHttpStatic_RemuxOff(t *testing.T) {
	t.Parallel()
	srs := server.Shared(t, "RemuxOff", sharedRemuxOff())

	// FlvWhole gets the file with no start: 200 and every byte.
	check.Case(t, "FlvWhole", check.Normal, func(c *check.C) {
		data, path := writeVod(c, srs.WebRoot(), media.H264HighAAC)
		res, body := staticGet(c, srs, path, nil)
		c.CheckEqual(res.StatusCode, 200, "status")
		c.CheckEqual(res.Header.Get("Content-Type"), "video/x-flv", "Content-Type")
		c.CheckEqual(res.ContentLength, int64(len(data)), "Content-Length")
		c.Check(bytes.Equal(body, data), "body of %d bytes differs from the file's %d", len(body), len(data))
	})

	// FlvStartZero asks from the offset 0: the whole file.
	check.Case(t, "FlvStartZero", check.Normal, func(c *check.C) {
		data, path := writeVod(c, srs.WebRoot(), media.H264HighAAC)
		res, body := staticGet(c, srs, path+"?start=0", nil)
		c.CheckEqual(res.StatusCode, 200, "status")
		c.Check(bytes.Equal(body, data), "body of %d bytes, want the whole file of %d", len(body), len(data))
	})

	// FlvStart<Codecs> asks from the offset of the first keyframe at or
	// after 10s, or of the first audio frame for audio only: the stream
	// plays from there, with its sequence headers and no frame before it.
	for _, w := range []struct{ name, file, vcodec, acodec string }{
		{"FlvStartH264Aac", media.H264HighAAC, "h264", "aac"},
		{"FlvStartHevcAac", media.HEVCAAC, "hevc", "aac"},
		{"FlvStartH264Only", media.H264Only, "h264", ""},
		{"FlvStartAacOnly", media.AACOnly, "", "aac"},
	} {
		check.Case(t, w.name, check.Normal, func(c *check.C) {
			data, path := writeVod(c, srs.WebRoot(), w.file)
			tags := flvTagsOf(c, data)
			off, at := flvStartOffset(c, tags, w.vcodec != "")
			res, body := staticGet(c, srs, path+"?start="+strconv.Itoa(off), nil)
			c.CheckEqual(res.StatusCode, 200, "status")
			c.CheckEqual(res.Header.Get("Content-Type"), "video/x-flv", "Content-Type")
			want := flvFromOffset(data, tags, off)
			c.CheckEqual(res.ContentLength, int64(len(want)), "Content-Length")
			c.Check(bytes.Equal(body, want), "body of %d bytes, want the header, the sequence headers and the file from %d, %d bytes", len(body), off, len(want))

			r, err := flv.Analyze(flvReaderOf(c, body), report.Until{})
			c.Must(err, "analyze the body")
			if w.vcodec != "" {
				checkVideo(c, r, w.vcodec)
				c.CheckEqual(r.Video.LeadingFrames, 0, "frames before the first keyframe")
				c.CheckEqual(r.Video.FirstDTS, at, "the first video frame's time")
			} else {
				c.Check(r.Video == nil, "video %v, want none", r)
			}
			if w.acodec != "" {
				checkAudio(c, r, w.acodec, 1)
				c.Check(r.Audio.FirstDTS >= at-100*time.Millisecond, "the first audio frame at %v, want from %v", r.Audio.FirstDTS, at)
			} else {
				c.Check(r.Audio == nil, "audio %v, want none", r)
			}
			c.CheckNoError(r.CheckTimestamps(100*time.Millisecond), "timestamps")
		})
	}

	// FlvStartPastEnd asks from an offset past the end of the file: 416 with
	// the file size in Content-Range, and the server serves the next
	// request.
	check.Case(t, "FlvStartPastEnd", check.Normal, func(c *check.C) {
		check.KnownBug(c, "a start past the end gets no answer, the connection closed, not 416")
		data, path := writeVod(c, srs.WebRoot(), media.H264HighAAC)
		checkUnsatisfiable(c, srs, path+"?start="+strconv.Itoa(len(data)+1000), "", len(data))
		checkStaticServes(c, srs)
	})

	// FlvStartNotFound asks from an offset of a file that is not there: 404.
	check.Case(t, "FlvStartNotFound", check.Fast, func(c *check.C) {
		res, _ := staticGet(c, srs, "/"+streamName(c)+"/none.flv?start=1000", nil)
		c.CheckEqual(res.StatusCode, 404, "status")
	})
}

// vodM3u8Cases adds the static HLS cases to TestHttpStatic_Common: a VOD
// playlist and its TS segment, files of the HTTP server's dir. Common keeps
// hls_ctx on, so the playlist is a session's: a master playlist that links
// the file with the session's hls_ctx, and the file with the hls_ctx on each
// segment URI.
func vodM3u8Cases(t *testing.T) {
	// M3u8Session gets a VOD playlist: a master playlist of the file with an
	// hls_ctx; the file through it, with the hls_ctx on its segment.
	check.Case(t, "M3u8Session", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		dir, playlist := writeVodHLS(c, srs.WebRoot())
		u := flvURL(srs, dir+"/index.m3u8")

		res, body := httpGet(c, httpClient, u)
		c.CheckEqual(res.StatusCode, 200, "status")
		c.CheckEqual(res.Header.Get("Content-Type"), "application/vnd.apple.mpegurl", "Content-Type")
		base, err := url.Parse(u)
		c.Must(err, "parse "+u)
		master, err := hls.Parse(body, base)
		c.Must(err, "parse the master playlist")
		if !c.Check(master.Master && len(master.Variants) == 1, "playlist %q, want a master playlist of one variant", body) {
			return
		}
		uri := strings.TrimPrefix(master.Variants[0].URI, "http://"+srs.Addr(server.HTTPServer))
		c.Check(strings.HasPrefix(uri, "/"+dir+"/index.m3u8?hls_ctx="), "media playlist %q, want /%v/index.m3u8?hls_ctx=<session>", uri, dir)

		res, body = httpGet(c, httpClient, flvURL(srs, strings.TrimPrefix(uri, "/")))
		c.CheckEqual(res.StatusCode, 200, "status of the media playlist")
		c.CheckEqual(res.Header.Get("Content-Type"), "application/vnd.apple.mpegurl", "Content-Type of the media playlist")
		ctx := query(c, uri).Get("hls_ctx")
		want := strings.ReplaceAll(playlist, ".ts", ".ts?hls_ctx="+ctx)
		c.CheckEqual(string(body), want, "the media playlist")
	})

	// M3u8Plays plays the VOD playlist through its session: every frame of
	// the TS file, to the end of the playlist.
	check.Case(t, "M3u8Plays", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		dir, _ := writeVodHLS(c, srs.WebRoot())
		checkVodHLSPlays(c, flvURL(srs, dir+"/index.m3u8"))
	})

	// M3u8NotFound asks for a playlist that is not there: 404.
	check.Case(t, "M3u8NotFound", check.Fast, func(c *check.C) {
		srs := server.Common(c)
		res, _ := staticGet(c, srs, "/"+streamName(c)+"/none.m3u8", nil)
		c.CheckEqual(res.StatusCode, 404, "status")
	})
}

// TestHttpStatic_CtxOff serves static HLS on the shared Fmp4Keys server (see
// sharedFmp4Keys), whose hls_ctx is off: the playlist is the file as it is.
func TestHttpStatic_CtxOff(t *testing.T) {
	t.Parallel()
	srs := server.Shared(t, "Fmp4Keys", sharedFmp4Keys())

	// M3u8Served gets a VOD playlist: the file as it is.
	check.Case(t, "M3u8Served", check.Normal, func(c *check.C) {
		dir, playlist := writeVodHLS(c, srs.WebRoot())
		res, body := httpGet(c, httpClient, flvURL(srs, dir+"/index.m3u8"))
		c.CheckEqual(res.StatusCode, 200, "status")
		c.CheckEqual(res.Header.Get("Content-Type"), "application/vnd.apple.mpegurl", "Content-Type")
		c.CheckEqual(string(body), playlist, "the playlist")
	})

	// M3u8Plays plays the VOD playlist: every frame of the TS file, to the
	// end of the playlist.
	check.Case(t, "M3u8Plays", check.Normal, func(c *check.C) {
		dir, _ := writeVodHLS(c, srs.WebRoot())
		checkVodHLSPlays(c, flvURL(srs, dir+"/index.m3u8"))
	})
}

// writeVod copies a test file to a dir of the case's own under root, and
// returns its bytes and its URL path, relative to the dir served at root.
func writeVod(c *check.C, root, name string) ([]byte, string) {
	c.Helper()
	data, err := os.ReadFile(media.Path(c, name))
	c.Must(err, "read the media")
	dir := streamName(c)
	writeStatic(c, filepath.Join(root, dir, name), data)
	return data, "/" + dir + "/" + name
}

// writeVodHLS writes a VOD playlist, index.m3u8, of one segment, the TS test
// file, in a dir of the case's own under root, and returns the dir and the
// playlist.
func writeVodHLS(c *check.C, root string) (string, string) {
	c.Helper()
	data, err := os.ReadFile(media.Path(c, media.H264HighAACTS))
	c.Must(err, "read the media")
	dir := streamName(c)
	playlist := "#EXTM3U\n#EXT-X-VERSION:3\n#EXT-X-PLAYLIST-TYPE:VOD\n#EXT-X-TARGETDURATION:30\n#EXT-X-MEDIA-SEQUENCE:0\n#EXTINF:30.000,\nseg-0.ts\n#EXT-X-ENDLIST\n"
	writeStatic(c, filepath.Join(root, dir, "seg-0.ts"), data)
	writeStatic(c, filepath.Join(root, dir, "index.m3u8"), []byte(playlist))
	return dir, playlist
}

// checkVodHLSPlays plays the VOD playlist of writeVodHLS to its end: the
// test file's codecs, every keyframe of its 1s GOP, and its 30s. The A/V
// sync is the file's, whose muxer interleaves the tracks up to 200ms apart,
// so it is not checked.
func checkVodHLSPlays(c *check.C, u string) {
	c.Helper()
	ctx, cancel := context.WithTimeout(context.Background(), time.Second)
	defer cancel()
	r, err := hls.PlayHTTP(ctx, httpClient, u, report.Until{})
	c.Must(err, "play "+u)
	checkVideo(c, r, "h264")
	checkAudio(c, r, "aac", 1)
	c.CheckNoError(r.CheckTimestamps(100*time.Millisecond), "timestamps")
	c.CheckEqual(r.Video.Keyframes, 30, "keyframes")
	c.Check(near(r.Duration(), 30*time.Second, 100*time.Millisecond), "duration %v, want 30s", r.Duration())
}

// checkRange gets a path with a Range header, when not empty, and checks the
// answer is the bytes from start to end, both included: 206, Content-Range
// and Content-Length. It returns the body.
func checkRange(c *check.C, srs *server.Server, path, rangeHeader string, data []byte, start, end int) []byte {
	c.Helper()
	var h http.Header
	if rangeHeader != "" {
		h = http.Header{"Range": {rangeHeader}}
	}
	res, body := staticGet(c, srs, path, h)
	what := path + " " + rangeHeader
	c.CheckEqual(res.StatusCode, 206, what+": status")
	c.CheckEqual(res.Header.Get("Content-Type"), "video/mp4", what+": Content-Type")
	c.CheckEqual(res.Header.Get("Content-Range"), fmt.Sprintf("bytes %d-%d/%d", start, end, len(data)), what+": Content-Range")
	c.CheckEqual(res.ContentLength, int64(end-start+1), what+": Content-Length")
	if start <= end && end < len(data) {
		c.Check(bytes.Equal(body, data[start:end+1]), "%s: body of %d bytes differs from the file's bytes %d to %d", what, len(body), start, end)
	}
	return body
}

// checkUnsatisfiable sends a GET of the path, with a raw header line when not
// empty, and checks the answer is 416 with Content-Range bytes */<size>.
func checkUnsatisfiable(c *check.C, srs *server.Server, path, header string, size int) {
	c.Helper()
	conn := rawConnTo(c, srs.Addr(server.HTTPServer))
	conn.SetDeadline(time.Now().Add(time.Second))
	req := "GET " + path + " HTTP/1.1\r\nHost: localhost\r\n"
	if header != "" {
		req += header + "\r\n"
	}
	_, err := io.WriteString(conn, req+"\r\n")
	c.Must(err, "send the request")
	res, err := http.ReadResponse(bufio.NewReader(conn), nil)
	if !c.CheckNoError(err, path+" "+header+": the answer") {
		return
	}
	res.Body.Close()
	c.CheckEqual(res.StatusCode, 416, path+" "+header+": status")
	c.CheckEqual(res.Header.Get("Content-Range"), fmt.Sprintf("bytes */%d", size), path+" "+header+": Content-Range")
}

// flvTag is a tag of an FLV file: where it starts, its type, its time and
// its body.
type flvTag struct {
	offset int
	tag    media.FLVTag
}

// flvTagsOf returns the tags of an FLV file with their offsets.
func flvTagsOf(c *check.C, data []byte) []flvTag {
	c.Helper()
	c.Must(checkFLVHeader(data), "the FLV header")
	var tags []flvTag
	for off := 13; off < len(data); {
		if len(data)-off < 11 {
			c.Fatalf("a tag header of %d bytes at %d", len(data)-off, off)
		}
		h := data[off : off+11]
		size := int(h[1])<<16 | int(h[2])<<8 | int(h[3])
		end := off + 11 + size + 4
		if end > len(data) {
			c.Fatalf("a tag of %d bytes at %d past the end %d", size, off, len(data))
		}
		tags = append(tags, flvTag{offset: off, tag: media.FLVTag{
			Type:      h[0] & 0x1f,
			Timestamp: uint32(h[7])<<24 | uint32(h[4])<<16 | uint32(h[5])<<8 | uint32(h[6]),
			Data:      data[off+11 : off+11+size],
		}})
		off = end
	}
	return tags
}

// checkFLVHeader checks the file starts with an FLV header of 9 bytes and a
// zero previous tag size.
func checkFLVHeader(data []byte) error {
	if len(data) < 13 || string(data[:3]) != "FLV" || binary.BigEndian.Uint32(data[5:9]) != 9 {
		return fmt.Errorf("not an FLV file with a 9-byte header")
	}
	return nil
}

// flvStartOffset returns the offset and time of the first keyframe at or
// after vodStart, or of the first audio frame for a file with no video.
func flvStartOffset(c *check.C, tags []flvTag, video bool) (int, time.Duration) {
	c.Helper()
	for _, t := range tags {
		at := time.Duration(t.tag.Timestamp) * time.Millisecond
		if at < vodStart {
			continue
		}
		if video && t.tag.Type == media.TagVideo {
			if v, err := media.ParseVideo(t.tag.Data); err == nil && v.Key && !v.SequenceHeader {
				return t.offset, at
			}
		}
		if !video && t.tag.Type == media.TagAudio {
			return t.offset, at
		}
	}
	c.Fatalf("no frame to start from at or after %v", vodStart)
	return 0, 0
}

// flvFromOffset is the FLV that a server sends of a file from an offset: the
// file's header, its first video and audio tags, the sequence headers, then
// the file from the offset.
func flvFromOffset(data []byte, tags []flvTag, off int) []byte {
	out := append([]byte{}, data[:13]...)
	var video, audio bool
	for i, t := range tags {
		if t.tag.Type == media.TagVideo && !video {
			video = true
		} else if t.tag.Type == media.TagAudio && !audio {
			audio = true
		} else {
			continue
		}
		end := len(data)
		if i+1 < len(tags) {
			end = tags[i+1].offset
		}
		out = append(out, data[t.offset:end]...)
	}
	return append(out, data[off:]...)
}

// flvReaderOf reads the tags of an FLV body.
func flvReaderOf(c *check.C, body []byte) *media.FLVReader {
	c.Helper()
	r, err := media.NewFLVReader(bytes.NewReader(body))
	c.Must(err, "read the FLV header of the body")
	return r
}
