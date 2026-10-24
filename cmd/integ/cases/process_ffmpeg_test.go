// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package cases

import (
	"fmt"
	"io"
	"os"
	"path/filepath"
	"sync"
	"testing"
	"time"

	"srs-integ/internal/check"
	"srs-integ/internal/flv"
	"srs-integ/internal/media"
	"srs-integ/internal/server"
)

// ffmpegStart is how long SRS takes to run an ffmpeg and that ffmpeg to
// publish or write its first frames: ingest, transcode and exec start their
// ffmpeg at once, and again every 3s while it is not running.
const ffmpegStart = 5 * time.Second

var ffmpegOnce = sync.OnceValues(func() (string, error) {
	ff, err := media.FindFFmpeg()
	if err != nil {
		return "", err
	}
	return ff.Path, nil
})

// ffmpegConf returns the path of the suite's ffmpeg as a config value, or
// skips the case.
func ffmpegConf(t testing.TB) string {
	t.Helper()
	path, err := ffmpegOnce()
	if err != nil {
		t.Skip(err.Error())
	}
	return confValue(path)
}

// confValue quotes a path for SRS's config, with forward slashes, which
// Windows takes too: the config has no escapes, and a path may have spaces.
func confValue(path string) string {
	return `"` + filepath.ToSlash(path) + `"`
}

// TestTimer_Ingest_File runs a server that ingests the H.264/AAC test file
// with ffmpeg, as copy, to its own live/ingest.
func TestTimer_Ingest_File(t *testing.T) {
	t.Parallel()
	cfg := server.PlayConfig(server.Play{})
	cfg.Vhost = `    ingest file {
        enabled on;
        input {
            type file;
            url ` + confValue(media.Path(t, media.H264HighAAC)) + `;
        }
        ffmpeg ` + ffmpegConf(t) + `;
        engine {
            enabled off;
            output rtmp://127.0.0.1:[port]/live/ingest;
        }
    }`
	srs := server.Group(t, cfg)

	// Plays: the ingested stream is published and plays as the file.
	check.Case(t, "Plays", check.Timer, func(c *check.C) {
		c.Must(untilPublishingErr(c, srs, "live/ingest"), "ingest")
		playChecked(c, rtmpURL(srs, "live/ingest"))
	})
}

// TestTimer_Transcode_Copy runs a server that transcodes each stream of app
// live with ffmpeg, as copy, to app out, as <stream>_copy.
func TestTimer_Transcode_Copy(t *testing.T) {
	t.Parallel()
	cfg := server.PlayConfig(server.Play{})
	cfg.Vhost = `    transcode live {
        enabled on;
        ffmpeg ` + ffmpegConf(t) + `;
        engine copy {
            enabled on;
            vcodec copy;
            acodec copy;
            output rtmp://127.0.0.1:[port]/out/[stream]_[engine];
        }
    }`
	srs := server.Group(t, cfg)

	// Plays: the transcoded stream is published and plays as the source.
	check.Case(t, "Plays", check.Timer, func(c *check.C) {
		stream := streamName(c)
		startPublisher(c, rtmpURL(srs, "live/"+stream), media.Path(c, media.H264HighAAC), publishLead)
		c.Must(untilPublishingErr(c, srs, "out/"+stream+"_copy"), "transcode")
		playChecked(c, rtmpURL(srs, "out/"+stream+"_copy"))
	})

	// StopsWithPublisher: when the source's publisher goes, SRS stops its
	// ffmpeg, so the transcoded stream goes too.
	check.Case(t, "StopsWithPublisher", check.Timer, func(c *check.C) {
		stream := streamName(c)
		pub := startPublisher(c, rtmpURL(srs, "live/"+stream), media.Path(c, media.H264HighAAC), publishLead)
		c.Must(untilPublishingErr(c, srs, "out/"+stream+"_copy"), "transcode")
		pub.Close()
		c.Check(untilUnpublished(c, srs, "out/"+stream+"_copy", ffmpegStart), "out/%s_copy still published %v after its source went", stream, ffmpegStart)
	})
}

// TestTimer_Exec_Publish runs a server that execs ffmpeg for each published
// stream, which plays it over RTMP and writes it to <stream>.flv in the
// server's dir, flushed per packet, since ffmpeg otherwise holds its first
// 32 KB.
func TestTimer_Exec_Publish(t *testing.T) {
	t.Parallel()
	cfg := server.PlayConfig(server.Play{})
	cfg.Vhost = `    exec {
        enabled on;
        publish ` + ffmpegConf(t) + ` -hide_banner -loglevel error -i rtmp://127.0.0.1:[port]/[app]/[stream] -c copy -flush_packets 1 -f flv -y [stream].flv;
    }`
	srs := server.Group(t, cfg)

	// Writes: the file the exec'd ffmpeg writes has the stream.
	check.Case(t, "Writes", check.Timer, func(c *check.C) {
		stream := streamName(c)
		startPublisher(c, rtmpURL(srs, "live/"+stream), media.Path(c, media.H264HighAAC), publishLead)

		file := filepath.Join(srs.WorkDir(), stream+".flv")
		deadline := time.Now().Add(check.Scaled(ffmpegStart))
		c.Must(untilFile(file, check.Scaled(ffmpegStart)), "the exec's file")
		f, err := os.Open(file)
		c.Must(err, "open the exec's file")
		defer f.Close()
		r, err := media.NewFLVReader(&tailReader{f: f, deadline: deadline})
		c.Must(err, "read the exec's file")
		rep, err := flv.Analyze(r, playedUntil)
		c.Must(err, "the exec's file")
		checkPlayed(c, rep, "h264", "aac")
	})
}

// untilPublishingErr waits until a stream, such as live/s, is published, for
// at most ffmpegStart scaled by the limit scale.
func untilPublishingErr(c *check.C, srs *server.Server, stream string) error {
	c.Helper()
	if !untilPublishing(c, srs, stream, check.Scaled(ffmpegStart)) {
		return fmt.Errorf("%s not published in %v", stream, check.Scaled(ffmpegStart))
	}
	return nil
}

// tailReader reads a file another process is still writing: at its end, it
// waits for more until the deadline.
type tailReader struct {
	f        *os.File
	deadline time.Time
}

func (r *tailReader) Read(p []byte) (int, error) {
	for {
		n, err := r.f.Read(p)
		if n > 0 || err != io.EOF {
			return n, err
		}
		if time.Now().After(r.deadline) {
			return 0, io.EOF
		}
		time.Sleep(20 * time.Millisecond)
	}
}
