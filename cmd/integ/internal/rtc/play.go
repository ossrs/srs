// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package rtc

import (
	"context"
	"errors"
	"fmt"
	"sync"
	"time"

	"github.com/pion/rtcp"
	"github.com/pion/rtp"
	"github.com/pion/rtp/codecs"
	"github.com/pion/rtp/codecs/av1/obu"
	"github.com/pion/webrtc/v4"

	"srs-integ/internal/media"
	"srs-integ/internal/report"
	"srs-integ/internal/ts"
)

// An Analyzer fills the stream report from the RTP packets of a player's
// tracks. Each track's decode times count from its own first packet. Video
// frames are rebuilt from their packets. A sequence header is counted when
// the parameter sets of H.264 or HEVC, the size of a VP9 keyframe, or the AV1
// sequence header first appear or change. Each Opus or G.711 packet is an
// audio frame.
type Analyzer struct {
	// ts reads the rebuilt H.264 and HEVC frames, as TS carries them, into
	// its report.
	ts           ts.Analyzer
	video, audio clock
	h264         codecs.H264Packet
	h265         codecs.H265Depacketizer
	av1          codecs.AV1Depacketizer
	frame        []byte
	frameTS      uint32
	frameCodec   string
	inFrame      bool
	// config is the last VP9 keyframe's profile and size, or the last AV1
	// sequence header.
	config string
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

// Add adds an RTP packet of a track with a codec, "h264", "h265", "vp9",
// "av1", "opus", "pcmu" or "pcma", and its clock rate.
func (a *Analyzer) Add(codec string, rate uint32, p *rtp.Packet) error {
	r := &a.ts.Report
	switch codec {
	case "h264", "h265", "vp9", "av1":
		if a.inFrame && p.Timestamp != a.frameTS {
			// The last packet of the frame before, with its marker, is lost.
			if err := a.flush(rate); err != nil {
				return err
			}
		}
		data, err := a.depacketize(codec, p.Payload)
		if err != nil {
			return fmt.Errorf("%v packet %v: %w", codec, p.SequenceNumber, err)
		}
		a.frame, a.frameTS, a.frameCodec, a.inFrame = append(a.frame, data...), p.Timestamp, codec, true
		if p.Marker {
			return a.flush(rate)
		}
	case "opus":
		r.AddAudio("opus", a.audio.at(p.Timestamp, rate))
		if r.Audio.SampleRate == 0 && len(p.Payload) > 0 {
			r.Audio.SampleRate, r.Audio.Channels = 48000, 1
			if p.Payload[0]&0x04 != 0 {
				r.Audio.Channels = 2
			}
		}
	case "pcmu", "pcma":
		r.AddAudio(codec, a.audio.at(p.Timestamp, rate))
		if r.Audio.SampleRate == 0 {
			r.Audio.SampleRate, r.Audio.Channels = int(rate), 1
		}
	default:
		return fmt.Errorf("codec %v is not supported", codec)
	}
	return nil
}

// depacketize returns the part of a frame a video packet carries: H.264 and
// HEVC NALs in Annex B, VP9 frame data, or AV1 OBUs with their sizes.
func (a *Analyzer) depacketize(codec string, payload []byte) ([]byte, error) {
	switch codec {
	case "h265":
		return a.h265.Unmarshal(payload)
	case "vp9":
		var p codecs.VP9Packet
		return p.Unmarshal(payload)
	case "av1":
		return a.av1.Unmarshal(payload)
	}
	return a.h264.Unmarshal(payload)
}

// flush adds the frame rebuilt so far.
func (a *Analyzer) flush(rate uint32) error {
	frame, codec := a.frame, a.frameCodec
	a.frame, a.inFrame = nil, false
	dts := a.video.at(a.frameTS, rate)
	switch codec {
	case "h265":
		return a.ts.Add(&ts.PES{Codec: "hevc", DTS: dts, Data: frame})
	case "vp9":
		return a.vp9(dts, frame)
	case "av1":
		return a.av1Frame(dts, frame)
	}
	return a.ts.Add(&ts.PES{Codec: "h264", DTS: dts, Data: frame})
}

// vp9 adds a VP9 frame; a keyframe has the profile and size.
func (a *Analyzer) vp9(dts time.Duration, frame []byte) error {
	key, profile, w, h, err := vp9Frame(frame)
	if err != nil {
		return fmt.Errorf("vp9 at %v: %w", dts, err)
	}
	r := &a.ts.Report
	if config := fmt.Sprint(profile, w, h); key && config != a.config {
		a.config = config
		r.VideoHeader("vp9", profile, w, h)
	}
	r.AddVideo("vp9", dts, key)
	return nil
}

// av1Frame adds an AV1 temporal unit: its sequence header, and a frame whose
// header tells a keyframe.
func (a *Analyzer) av1Frame(dts time.Duration, tu []byte) error {
	obus, err := av1OBUs(tu)
	if err != nil {
		return fmt.Errorf("av1 at %v: %w", dts, err)
	}
	r := &a.ts.Report
	key, frame := false, false
	for _, o := range obus {
		switch o.Header.Type {
		case obu.OBUSequenceHeader:
			if string(o.Payload) == a.config {
				continue
			}
			profile, w, h, err := media.AV1SequenceSize(o.Payload)
			if err != nil {
				return fmt.Errorf("av1 at %v: %w", dts, err)
			}
			a.config = string(o.Payload)
			r.VideoHeader("av1", profile, w, h)
		case obu.OBUFrame, obu.OBUFrameHeader:
			if len(o.Payload) == 0 || frame {
				continue
			}
			// show_existing_frame, then frame_type, KEY_FRAME is 0.
			frame = true
			key = o.Payload[0]&0x80 == 0 && (o.Payload[0]>>5)&3 == 0
		}
	}
	if frame {
		r.AddVideo("av1", dts, key)
	}
	return nil
}

// Report returns the report so far.
func (a *Analyzer) Report() *report.Report {
	return &a.ts.Report
}

// An analysis is the analyzer of a player, shared with its waiters.
type analysis struct {
	mu       sync.Mutex
	analyzer Analyzer
	err      error
	changed  chan struct{}
	packets  int
}

func newAnalysis() *analysis {
	return &analysis{changed: make(chan struct{})}
}

// add analyzes a packet; the first error stops the analysis.
func (a *analysis) add(codec string, rate uint32, pkt *rtp.Packet) {
	a.mu.Lock()
	defer a.mu.Unlock()
	a.packets++
	if a.err == nil {
		a.err = a.analyzer.Add(codec, rate, pkt)
	}
	close(a.changed)
	a.changed = make(chan struct{})
}

// Packets is how many RTP packets the player got.
func (a *analysis) Packets() int {
	a.mu.Lock()
	defer a.mu.Unlock()
	return a.packets
}

// Report returns a copy of the report so far.
func (a *analysis) Report() *report.Report {
	a.mu.Lock()
	defer a.mu.Unlock()
	return a.copyReport()
}

func (a *analysis) copyReport() *report.Report {
	r := *a.analyzer.Report()
	if r.Video != nil {
		v := *r.Video
		r.Video = &v
	}
	if r.Audio != nil {
		au := *r.Audio
		r.Audio = &au
	}
	return &r
}

// Until waits until the report meets u, and returns a copy of it, partial
// with an error when ctx ends first or a packet can't be analyzed.
func (a *analysis) Until(ctx context.Context, u report.Until) (*report.Report, error) {
	for {
		a.mu.Lock()
		r, err, changed := a.copyReport(), a.err, a.changed
		a.mu.Unlock()
		if err != nil {
			return r, err
		}
		if u.Done(r) {
			return r, nil
		}
		select {
		case <-changed:
		case <-ctx.Done():
			return r, fmt.Errorf("%w before %v: %v", ctx.Err(), u, r)
		}
	}
}

// A Player gets a stream from the server and analyzes it.
type Player struct {
	Session
	*analysis
}

// Play connects a player by WHEP to url, such as
// http://host:1985/rtc/v1/whep/?app=live&stream=s, or by the legacy API when
// opts.Legacy is set, and waits until ICE and DTLS are connected. It offers
// H.264 and Opus, or the codecs of the options, no section of a kind they
// have none of, and analyzes what it gets
// until it closes.
func Play(ctx context.Context, url string, opts *Options) (*Player, error) {
	video, audio, err := sessionCodecs(opts)
	if err != nil {
		return nil, err
	}
	pc, rec, err := newPeerConnection(opts)
	if err != nil {
		return nil, err
	}
	p := &Player{Session: Session{PC: pc, Recorder: rec, opts: opts}, analysis: newAnalysis()}
	for _, kind := range []webrtc.RTPCodecType{webrtc.RTPCodecTypeVideo, webrtc.RTPCodecTypeAudio} {
		if (kind == webrtc.RTPCodecTypeVideo && len(video) == 0) || (kind == webrtc.RTPCodecTypeAudio && len(audio) == 0) {
			continue
		}
		if _, err := pc.AddTransceiverFromKind(kind, webrtc.RTPTransceiverInit{Direction: webrtc.RTPTransceiverDirectionRecvonly}); err != nil {
			pc.Close()
			return nil, err
		}
	}
	pc.OnTrack(p.read)
	if err := p.connect(ctx, url); err != nil {
		return nil, err
	}
	return p, nil
}

// read analyzes the RTP packets of a track until it ends. Reading RTCP runs
// the interceptors, such as the receiver reports.
func (p *Player) read(track *webrtc.TrackRemote, receiver *webrtc.RTPReceiver) {
	go func() {
		for {
			if _, _, err := receiver.ReadRTCP(); err != nil {
				return
			}
		}
	}()
	codec := track.Codec()
	name := mimeKey(codec.MimeType)
	for {
		pkt, _, err := track.ReadRTP()
		if err != nil {
			return
		}
		p.add(name, codec.ClockRate, pkt)
	}
}

// RequestKeyframe sends a picture loss indication for the video, which asks
// the publisher for a keyframe through the server.
func (p *Player) RequestKeyframe() error {
	var pkts []rtcp.Packet
	for _, t := range p.PC.GetTransceivers() {
		if t.Kind() != webrtc.RTPCodecTypeVideo || t.Receiver() == nil {
			continue
		}
		for _, track := range t.Receiver().Tracks() {
			if track.SSRC() != 0 {
				pkts = append(pkts, &rtcp.PictureLossIndication{MediaSSRC: uint32(track.SSRC())})
			}
		}
	}
	if len(pkts) == 0 {
		return errors.New("no video track to request a keyframe of")
	}
	return p.PC.WriteRTCP(pkts)
}
