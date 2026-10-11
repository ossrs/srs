// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

// Package flv analyzes FLV tags into the stream report, from an RTMP player,
// an HTTP-FLV player or an FLV file.
package flv

import (
	"context"
	"fmt"
	"io"
	"net/http"
	"time"

	"srs-integ/internal/media"
	"srs-integ/internal/report"
	"srs-integ/internal/rtmp"
)

// A TagSource gives FLV tags: an RTMP player or an FLV reader.
type TagSource interface {
	ReadTag() (*media.FLVTag, error)
}

// An Analyzer adds FLV tags to its report.
type Analyzer struct {
	Report report.Report
	// LegacyHEVC accepts HEVC in legacy tags, codec id 12; without it such a
	// tag is an error, so a server that sends them instead of enhanced RTMP
	// fails the cases.
	LegacyHEVC bool
}

// Add adds one tag to the report. It fails on a tag it can't parse, such as
// an unknown codec or a broken sequence header.
func (a *Analyzer) Add(tag *media.FLVTag) error {
	r := &a.Report
	ts := time.Duration(tag.Timestamp) * time.Millisecond
	switch tag.Type {
	case media.TagScript:
		if IsMetadata(tag.Data) {
			r.AddMetadata()
		}
	case media.TagVideo:
		v, err := media.ParseVideo(tag.Data)
		if err != nil {
			return fmt.Errorf("video at %v: %w", ts, err)
		}
		if v.Legacy && !a.LegacyHEVC {
			return fmt.Errorf("video at %v: HEVC in a legacy tag, codec id 12, not enhanced RTMP", ts)
		}
		switch {
		case v.SequenceHeader:
			w, h, err := media.VideoSize(v.Codec, v.Payload)
			if err != nil && (v.Codec == "h264" || v.Codec == "hevc") {
				return fmt.Errorf("video sequence header at %v: %w", ts, err)
			}
			r.VideoHeader(v.Codec, media.Profile(v.Codec, v.Payload), w, h)
		case v.EndOfSequence, v.Metadata:
		default:
			r.AddVideo(v.Codec, ts, v.Key)
		}
	case media.TagAudio:
		au, err := media.ParseAudio(tag.Data)
		if err != nil {
			return fmt.Errorf("audio at %v: %w", ts, err)
		}
		if au.SequenceHeader {
			rate, channels := media.AACConfig(au.Payload)
			r.AudioHeader(au.Codec, rate, channels)
			break
		}
		r.AddAudio(au.Codec, ts)
		// MP3 has no sequence header; each frame tells its rate.
		if r.Audio.SampleRate == 0 && au.Codec == "mp3" {
			r.Audio.SampleRate, r.Audio.Channels = media.MP3Header(au.Payload)
		}
	}
	return nil
}

// Analyze reads tags from src until the report meets u, or, for the zero
// Until, until the stream ends. It always returns the report, partial when it
// fails, and the error tells what it waited for and what it got.
func Analyze(src TagSource, u report.Until) (*report.Report, error) {
	var a Analyzer
	r := &a.Report
	for !u.Done(r) {
		tag, err := src.ReadTag()
		if err == io.EOF {
			if u == (report.Until{}) {
				return r, nil
			}
			return r, fmt.Errorf("stream ended before %v: %v", u, r)
		} else if err != nil {
			return r, fmt.Errorf("%w, before %v: %v", err, u, r)
		}
		if err := a.Add(tag); err != nil {
			return r, err
		}
	}
	return r, nil
}

// analyzeUntilDone runs Analyze, and calls stop to unblock the read when ctx
// ends first. The error then names the context's error.
func analyzeUntilDone(ctx context.Context, src TagSource, u report.Until, stop func()) (*report.Report, error) {
	after := context.AfterFunc(ctx, stop)
	defer after()
	r, err := Analyze(src, u)
	if err != nil && ctx.Err() != nil {
		return r, fmt.Errorf("%w before %v: %v", ctx.Err(), u, r)
	}
	return r, err
}

// PlayRTMP plays an rtmp:// URL and analyzes the stream until u, or until ctx
// ends.
func PlayRTMP(ctx context.Context, url string, opts *rtmp.Options, u report.Until) (*report.Report, error) {
	c, err := rtmp.Dial(ctx, url, opts)
	if err != nil {
		return nil, err
	}
	defer c.Close()
	if _, err := c.Play(ctx); err != nil {
		return nil, err
	}
	c.SetDeadline(time.Time{})
	return analyzeUntilDone(ctx, c, u, func() { c.Close() })
}

// PlayHTTP plays an HTTP-FLV URL and analyzes the stream until u, or until
// ctx ends. A nil client is http.DefaultClient.
func PlayHTTP(ctx context.Context, client *http.Client, url string, u report.Until) (*report.Report, error) {
	if client == nil {
		client = http.DefaultClient
	}
	req, err := http.NewRequestWithContext(ctx, http.MethodGet, url, nil)
	if err != nil {
		return nil, err
	}
	res, err := client.Do(req)
	if err != nil {
		return nil, err
	}
	defer res.Body.Close()
	if res.StatusCode != http.StatusOK {
		return nil, fmt.Errorf("GET %v: status %v", url, res.Status)
	}

	fr, err := media.NewFLVReader(res.Body)
	if err != nil {
		if ctx.Err() != nil {
			return &report.Report{}, fmt.Errorf("%w before %v: no FLV header", ctx.Err(), u)
		}
		return nil, fmt.Errorf("GET %v: %w", url, err)
	}
	return analyzeUntilDone(ctx, fr, u, func() { res.Body.Close() })
}

// IsMetadata reports whether a data message is onMetaData, alone or after
// @setDataFrame, and not another one, such as |RtmpSampleAccess.
func IsMetadata(data []byte) bool {
	values, err := rtmp.DecodeAMF0(data)
	if err != nil || len(values) == 0 {
		return false
	}
	if values[0] == "@setDataFrame" && len(values) > 1 {
		return values[1] == "onMetaData"
	}
	return values[0] == "onMetaData"
}
