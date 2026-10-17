// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT
package main

import (
	"context"
	"crypto/tls"
	"fmt"
	"io"
	"net/url"
	"os"
	"os/signal"
	"strings"
	"syscall"
	"time"

	"srs-integ/internal/media"
	"srs-integ/internal/rtc"
	"srs-integ/internal/rtmp"
	"srs-integ/internal/srt"
)

// cliRTMPOptions are the client options of probe and publish. An rtmps://
// URL doesn't verify the server's certificate, like ffmpeg by default, since
// test servers use a self-signed one.
var cliRTMPOptions = &rtmp.Options{TLS: &tls.Config{InsecureSkipVerify: true}}

// publish sends an FLV file to an RTMP or RTMPS URL in real time, until the
// file ends or the process is interrupted.
func publish(file, url string, stdout io.Writer) error {
	if strings.HasPrefix(url, "srt://") {
		return publishSRT(file, url, stdout)
	}
	if isWHIP(url) {
		return publishWHIP(file, url, stdout)
	}
	if _, err := rtmp.ParseURL(url); err != nil {
		return fmt.Errorf("publish: %w", err)
	}
	f, err := os.Open(file)
	if err != nil {
		return fmt.Errorf("publish: %w", err)
	}
	defer f.Close()
	r, err := media.NewFLVReader(f)
	if err != nil {
		return fmt.Errorf("publish: %v: %w", file, err)
	}

	ctx, stop := signal.NotifyContext(context.Background(), os.Interrupt, syscall.SIGTERM)
	defer stop()
	c, err := rtmp.Dial(ctx, url, cliRTMPOptions)
	if err != nil {
		return fmt.Errorf("publish: %w", err)
	}
	defer c.Close()
	if _, err := c.Publish(ctx, rtmp.FMLE); err != nil {
		return fmt.Errorf("publish: %w", err)
	}
	fmt.Fprintf(stdout, "publishing %v to %v, %v handshake\n", file, url, c.Handshake.Kind())

	start := time.Now()
	stats, err := rtmp.PublishFLV(ctx, c, r, &rtmp.Pacer{})
	fmt.Fprintf(stdout, "sent %v tags (%v video, %v audio), %v of media in %v\n",
		stats.Tags, stats.Video, stats.Audio, stats.Duration, time.Since(start).Round(time.Millisecond))
	if err != nil && ctx.Err() == nil {
		return fmt.Errorf("publish: %w", err)
	}
	return nil
}

// isWHIP reports whether a URL is a WHIP endpoint, such as
// http://host:1985/rtc/v1/whip/?app=live&stream=s.
func isWHIP(raw string) bool {
	u, err := url.Parse(raw)
	return err == nil && (u.Scheme == "http" || u.Scheme == "https") && strings.HasSuffix(u.Path, "/rtc/v1/whip/")
}

// publishWHIP sends an MP4 file with H.264, HEVC, VP9 or AV1 and Opus by
// WHIP in real time, until the file ends or the process is interrupted, then
// deletes the session. It offers the file's codecs, and asks SRS for a video
// codec other than H.264 with vcodec, unless the URL already has one.
func publishWHIP(file, rawURL string, stdout io.Writer) error {
	if !strings.HasSuffix(file, ".mp4") {
		return fmt.Errorf("publish: WHIP takes an MP4 file with H.264, HEVC, VP9 or AV1 and Opus, not %v", file)
	}
	samples, err := rtc.ReadMP4(file)
	if err != nil {
		return fmt.Errorf("publish: %w", err)
	}
	opts := rtc.OptionsOf(samples)
	url, err := withVcodec(rawURL, opts)
	if err != nil {
		return fmt.Errorf("publish: %w", err)
	}

	ctx, stop := signal.NotifyContext(context.Background(), os.Interrupt, syscall.SIGTERM)
	defer stop()
	pub, err := rtc.Publish(ctx, url, opts)
	if err != nil {
		return fmt.Errorf("publish: %w", err)
	}
	defer pub.Close()
	fmt.Fprintf(stdout, "publishing %v to %v by WHIP\n", file, url)

	start := time.Now()
	err = pub.Send(ctx, samples, &rtmp.Pacer{})
	fmt.Fprintf(stdout, "sent %v of media in %v\n", sentMedia(samples, time.Since(start), err), time.Since(start).Round(time.Millisecond))
	if err != nil && ctx.Err() == nil {
		return fmt.Errorf("publish: %w", err)
	}
	dctx, cancel := context.WithTimeout(context.Background(), time.Second)
	defer cancel()
	if _, err := pub.Delete(dctx); err != nil {
		return fmt.Errorf("publish: delete the session: %w", err)
	}
	return nil
}

// withVcodec adds vcodec to a WHIP URL for a video codec other than H.264,
// which SRS takes only when asked, unless the URL has one.
func withVcodec(rawURL string, opts *rtc.Options) (string, error) {
	if len(opts.Video) == 0 || opts.Video[0] == "H264" {
		return rawURL, nil
	}
	u, err := url.Parse(rawURL)
	if err != nil {
		return "", err
	}
	q := u.Query()
	if q.Get("vcodec") != "" || q.Get("codec") != "" {
		return rawURL, nil
	}
	sep := "&"
	if u.RawQuery == "" {
		sep = "?"
	}
	return rawURL + sep + "vcodec=" + strings.ToLower(opts.Video[0]), nil
}

// sentMedia is the media time of the samples sent: all of them, or about as
// much as the wall time when the publish stopped early.
func sentMedia(samples []rtc.Sample, wall time.Duration, err error) time.Duration {
	if len(samples) == 0 {
		return 0
	}
	if last := samples[len(samples)-1].DTS; err == nil || wall > last {
		return last
	}
	return wall.Round(time.Millisecond)
}

// publishSRT sends an MPEG-TS file to an SRT URL in real time, by its PCR,
// until the file ends or the process is interrupted. The streamid of the URL
// says what to do, such as #!::r=live/s,m=publish.
func publishSRT(file, rawURL string, stdout io.Writer) error {
	addr, opts, err := srt.ParseURL(rawURL)
	if err != nil {
		return fmt.Errorf("publish: %w", err)
	}
	if !strings.HasSuffix(file, ".ts") {
		return fmt.Errorf("publish: SRT takes an MPEG-TS file, .ts, not %v", file)
	}
	f, err := os.Open(file)
	if err != nil {
		return fmt.Errorf("publish: %w", err)
	}
	defer f.Close()

	ctx, stop := signal.NotifyContext(context.Background(), os.Interrupt, syscall.SIGTERM)
	defer stop()
	c, err := srt.Dial(ctx, addr, opts)
	if err != nil {
		return fmt.Errorf("publish: %w", err)
	}
	defer c.Close()
	fmt.Fprintf(stdout, "publishing %v to %v, streamid %q\n", file, addr, opts.StreamID)

	start := time.Now()
	s := srt.NewSender(f)
	err = s.Stream(ctx, c, nil, nil)
	msgs, n := s.Sent()
	fmt.Fprintf(stdout, "sent %v messages, %v bytes in %v\n", msgs, n, time.Since(start).Round(time.Millisecond))
	if err != nil && ctx.Err() == nil {
		return fmt.Errorf("publish: %w", err)
	}
	return nil
}
