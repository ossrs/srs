// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package rtsp

import (
	"errors"
	"fmt"
	"strings"
	"time"

	"github.com/bluenviron/gortsplib/v5/pkg/description"
	"github.com/bluenviron/gortsplib/v5/pkg/format"
	"github.com/bluenviron/gortsplib/v5/pkg/format/rtpmpeg4audio"
	"github.com/bluenviron/mediacommon/v2/pkg/codecs/mpeg4audio"
	"github.com/pion/rtp"

	"srs-integ/internal/report"
	"srs-integ/internal/rtc"
)

// A Track is a media of a session: its codec, "h264", "h265" or "aac", and
// its clock rate.
type Track struct {
	Media  *description.Media
	Format format.Format
	Codec  string
	Rate   uint32
	// aac reads the AAC frames of RFC 3640 packets, and config is the
	// AudioSpecificConfig of the SDP.
	aac    *rtpmpeg4audio.Decoder
	config *mpeg4audio.AudioSpecificConfig
}

// TrackOf returns the track of a media's format, or an error for a codec the
// analyzer doesn't read. A format gortsplib leaves generic, such as HEVC with
// a payload type outside the dynamic range, is known by its rtpmap.
func TrackOf(m *description.Media, f format.Format) (*Track, error) {
	t := &Track{Media: m, Format: f, Rate: uint32(f.ClockRate())}
	switch f := f.(type) {
	case *format.H264:
		t.Codec = "h264"
	case *format.H265:
		t.Codec = "h265"
	case *format.MPEG4Audio:
		if f.Config == nil {
			return nil, errors.New("MPEG-4 audio without a config")
		}
		d, err := f.CreateDecoder()
		if err != nil {
			return nil, err
		}
		t.Codec, t.aac, t.config = "aac", d, f.Config
	case *format.Generic:
		if !strings.HasPrefix(strings.ToUpper(f.RTPMap()), "H265/") {
			return nil, fmt.Errorf("codec %q is not supported", f.RTPMap())
		}
		t.Codec = "h265"
	default:
		return nil, fmt.Errorf("codec %v is not supported", f.Codec())
	}
	if t.Rate == 0 {
		return nil, fmt.Errorf("%v has no clock rate", t.Codec)
	}
	return t, nil
}

// An Analyzer fills the stream report from the RTP packets of a session's
// tracks. Each track's decode times count from its own first packet. H.264
// and HEVC frames are rebuilt from their packets, and a sequence header is
// counted when their parameter sets first appear or change, in band. AAC
// has its sequence header, the config, in the SDP, counted with the first
// packet; each frame of an RFC 3640 packet is 1024 samples after the one
// before.
type Analyzer struct {
	video rtc.Analyzer
	audio clock
	// header is whether the AAC config is counted.
	header bool
}

// Add adds an RTP packet of a track.
func (a *Analyzer) Add(t *Track, p *rtp.Packet) error {
	switch t.Codec {
	case "h264", "h265":
		return a.video.Add(t.Codec, t.Rate, p)
	case "aac":
		return a.addAAC(t, p)
	}
	return fmt.Errorf("codec %v is not supported", t.Codec)
}

func (a *Analyzer) addAAC(t *Track, p *rtp.Packet) error {
	r := a.Report()
	if !a.header {
		a.header = true
		r.AudioHeader("aac", t.config.SampleRate, t.config.ChannelCount)
	}
	aus, err := t.aac.Decode(p)
	if errors.Is(err, rtpmpeg4audio.ErrMorePacketsNeeded) {
		return nil
	}
	if err != nil {
		return fmt.Errorf("aac packet %v: %w", p.SequenceNumber, err)
	}
	dts := a.audio.at(p.Timestamp, t.Rate)
	for i := range aus {
		r.AddAudio("aac", dts+time.Duration(i)*mpeg4audio.SamplesPerAccessUnit*time.Second/time.Duration(t.Rate))
	}
	return nil
}

// Report returns the report so far.
func (a *Analyzer) Report() *report.Report {
	return a.video.Report()
}

// A clock turns a track's RTP timestamps into decode times from its first
// packet, past the 32-bit wrap.
type clock struct {
	started bool
	last    uint32
	ticks   int64
}

func (c *clock) at(ts, rate uint32) time.Duration {
	if !c.started {
		c.started, c.last = true, ts
	}
	c.ticks += int64(int32(ts - c.last))
	c.last = ts
	return time.Duration(c.ticks * int64(time.Second) / int64(rate))
}
