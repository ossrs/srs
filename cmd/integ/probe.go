// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT
package main

import (
	"context"
	"flag"
	"fmt"
	"io"
	"net/url"
	"os"
	"os/signal"
	"strings"
	"syscall"
	"time"

	"srs-integ/internal/audio"
	"srs-integ/internal/dash"
	"srs-integ/internal/flv"
	"srs-integ/internal/hls"
	"srs-integ/internal/media"
	"srs-integ/internal/mp4"
	"srs-integ/internal/report"
	"srs-integ/internal/rtc"
	"srs-integ/internal/srt"
	"srs-integ/internal/ts"
)

const probeUsage = `Usage: srs-integ probe [flags] <url>

Read a stream and print its stream report: rtmp://host/app/stream,
rtmps://host:port/app/stream (the certificate is not verified), http://host/app/stream.flv, .ts, .aac, .mp3, .m3u8 (HLS) or .mpd (DASH),
WebRTC by WHEP, http://host:1985/rtc/v1/whep/?app=live&stream=s (or
/rtc/v1/whip-play/), SRT, srt://host:port?streamid=#!::r=live/s,m=request
(also passphrase, pbkeylen and latency in ms), or an FLV, TS, AAC, MP3 or MP4 file. It stops after -duration of media, and fails
when -timeout passes first. HLS and DASH stop after the segment that reaches
it.

`

// probe runs the probe command and returns its exit code.
func probe(args []string, stdout, stderr io.Writer) int {
	fs := flag.NewFlagSet("probe", flag.ContinueOnError)
	fs.SetOutput(io.Discard)
	duration := fs.Duration("duration", time.Second, "the media time to read; 0 reads until the stream ends")
	timeout := fs.Duration("timeout", 10*time.Second, "the longest time to wait")
	usage := func(w io.Writer) {
		fmt.Fprint(w, probeUsage)
		fs.SetOutput(w)
		fs.PrintDefaults()
	}
	if err := fs.Parse(args); err != nil {
		if err == flag.ErrHelp {
			usage(stdout)
			return 0
		}
		fmt.Fprintln(stderr, err)
		usage(stderr)
		return 2
	}
	if fs.NArg() != 1 {
		usage(stderr)
		return 2
	}

	ctx, cancel := context.WithTimeout(context.Background(), *timeout)
	defer cancel()
	ctx, stop := signal.NotifyContext(ctx, os.Interrupt, syscall.SIGTERM)
	defer stop()

	start := time.Now()
	r, err := probeURL(ctx, fs.Arg(0), report.Until{Duration: *duration})
	if r != nil {
		fmt.Fprintf(stdout, "%v\nread in %v\n", strings.ReplaceAll(r.String(), "; ", "\n"), time.Since(start).Round(time.Millisecond))
	}
	if err != nil {
		return fail(stderr, fmt.Errorf("probe: %w", err))
	}
	return 0
}

// probeURL reads a stream by its URL scheme and extension, or a file by its
// extension: .ts, .aac, .mp3, .mp4, and FLV for any other.
func probeURL(ctx context.Context, raw string, u report.Until) (*report.Report, error) {
	if !strings.Contains(raw, "://") {
		return probeFile(raw, u)
	}
	if strings.HasPrefix(raw, "srt://") {
		addr, opts, err := srt.ParseURL(raw)
		if err != nil {
			return nil, err
		}
		return srt.Play(ctx, addr, opts, u)
	}

	parsed, err := url.Parse(raw)
	if err != nil {
		return nil, err
	}
	switch parsed.Scheme {
	case "rtmp", "rtmps":
		return flv.PlayRTMP(ctx, raw, cliRTMPOptions, u)
	case "http", "https":
		switch {
		case strings.HasSuffix(parsed.Path, ".flv"):
			return flv.PlayHTTP(ctx, nil, raw, u)
		case strings.HasSuffix(parsed.Path, ".ts"):
			return ts.PlayHTTP(ctx, nil, raw, u)
		case strings.HasSuffix(parsed.Path, ".m3u8"):
			return hls.PlayHTTP(ctx, nil, raw, u)
		case strings.HasSuffix(parsed.Path, ".mpd"):
			return dash.PlayHTTP(ctx, nil, raw, u)
		case audio.CodecOf(parsed.Path) != "":
			return audio.PlayHTTP(ctx, nil, raw, u)
		case strings.HasSuffix(parsed.Path, "/rtc/v1/whep/") || strings.HasSuffix(parsed.Path, "/rtc/v1/whip-play/"):
			return probeWHEP(ctx, raw, u)
		}
	}
	return nil, fmt.Errorf("unsupported URL %v, want rtmp://, rtmps://, http://...{flv,ts,aac,mp3,m3u8,mpd}, a WHEP URL, srt:// or a file", raw)
}

// probeWHEP plays a stream by WHEP, offering every codec, so SRS answers the
// stream's. It asks for a keyframe once connected, so a player that joins a
// running stream starts without waiting for the publisher's next one.
func probeWHEP(ctx context.Context, raw string, u report.Until) (*report.Report, error) {
	player, err := rtc.Play(ctx, raw, rtc.Every())
	if err != nil {
		return nil, err
	}
	defer player.Close()
	if err := player.RequestKeyframe(); err != nil {
		return player.Report(), err
	}
	return player.Until(ctx, u)
}

// probeFile reads a file by its extension.
func probeFile(path string, u report.Until) (*report.Report, error) {
	if strings.HasSuffix(path, ".mp4") {
		return mp4.AnalyzeFile(path, u)
	}
	f, err := os.Open(path)
	if err != nil {
		return nil, err
	}
	defer f.Close()
	if strings.HasSuffix(path, ".ts") {
		return ts.Analyze(ts.NewDemuxer(f), u)
	}
	if codec := audio.CodecOf(path); codec != "" {
		return audio.Analyze(audio.NewReader(f, codec), u)
	}
	r, err := media.NewFLVReader(f)
	if err != nil {
		return nil, fmt.Errorf("%v: %w", path, err)
	}
	return flv.Analyze(r, u)
}
