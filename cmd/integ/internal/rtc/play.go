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

// burstGap is how long no packet comes before a player has a burst of them
// in full. The packets of one burst arrive within about 1ms; between bursts,
// real-time media leaves gaps of 10ms or more, since an Opus packet comes
// every 20ms.
const burstGap = 10 * time.Millisecond

// A burst is the decode times of the frames each track added since the
// player last waited for the network, after the latest frame of each track
// before them.
type burst struct {
	video, audio         []time.Duration
	lastVideo, lastAudio time.Duration
	hasVideo, hasAudio   bool
}

// sync is the largest distance between the latest video and the latest
// audio decode time, once both tracks have a frame, as the frames of the
// burst come in the order that makes it smallest: each track's frames come
// in their own order, and nothing tells how the tracks interleaved. With
// B-frames, a video track's times are not in order, so no merge by time
// fits.
func (b *burst) sync() time.Duration {
	// best[j] is, for the i video frames so far, the smallest largest
	// distance of an order that has taken them and j audio frames.
	best := make([]time.Duration, len(b.audio)+1)
	for i := range len(b.video) + 1 {
		for j := range best {
			d := b.distance(i, j)
			switch {
			case i == 0 && j == 0:
				best[j] = d
			case i == 0:
				best[j] = max(d, best[j-1])
			case j == 0:
				best[j] = max(d, best[j])
			default:
				best[j] = max(d, min(best[j], best[j-1]))
			}
		}
	}
	return best[len(b.audio)]
}

// distance is the distance between the latest video and the latest audio
// decode time after i video and j audio frames of the burst, or 0 while a
// track has no frame.
func (b *burst) distance(i, j int) time.Duration {
	v, hasV := b.lastVideo, b.hasVideo
	if i > 0 {
		v, hasV = b.video[i-1], true
	}
	a, hasA := b.lastAudio, b.hasAudio
	if j > 0 {
		a, hasA = b.audio[j-1], true
	}
	if !hasV || !hasA {
		return 0
	}
	return (v - a).Abs()
}

// next starts the next burst.
func (b *burst) next() {
	if n := len(b.video); n > 0 {
		b.lastVideo, b.hasVideo = b.video[n-1], true
	}
	if n := len(b.audio); n > 0 {
		b.lastAudio, b.hasAudio = b.audio[n-1], true
	}
	b.video, b.audio = b.video[:0], b.audio[:0]
}

// An analysis is the analyzer of a player, shared with its waiters.
//
// Each track's reader adds its packets in its own goroutine, so the packets
// of a burst, such as the media a stall held, reach the analyzer in no set
// order across the tracks: one track may be added in full before the other.
// So its A/V sync measures each burst as a whole, once it is over, burstGap
// after its last packet, by the order of its frames that makes the sync
// smallest; a burst of one track, or one frame, has only the order it came
// in.
type analysis struct {
	mu       sync.Mutex
	analyzer Analyzer
	err      error
	changed  chan struct{}
	packets  int
	now      func() time.Time
	// last is when the latest packet was added; settled is whether its burst
	// has been measured since.
	last    time.Time
	settled bool
	burst   burst
	sync    time.Duration
}

func newAnalysis() *analysis {
	return &analysis{changed: make(chan struct{}), now: time.Now}
}

// add analyzes a packet; the first error stops the analysis.
func (a *analysis) add(codec string, rate uint32, pkt *rtp.Packet) {
	a.mu.Lock()
	defer a.mu.Unlock()
	a.settle()
	a.packets++
	if a.err == nil {
		r := a.analyzer.Report()
		var videos, audios int
		if r.Video != nil {
			videos = r.Video.Frames
		}
		if r.Audio != nil {
			audios = r.Audio.Frames
		}
		a.err = a.analyzer.Add(codec, rate, pkt)
		if r.Video != nil && r.Video.Frames > videos {
			a.burst.video = append(a.burst.video, r.Video.LastDTS)
		}
		if r.Audio != nil && r.Audio.Frames > audios {
			a.burst.audio = append(a.burst.audio, r.Audio.LastDTS)
		}
	}
	a.last, a.settled = a.now(), false
	close(a.changed)
	a.changed = make(chan struct{})
}

// settle measures the A/V sync of the burst once no packet has come for
// burstGap, and returns how long until then, or 0 when it is measured.
func (a *analysis) settle() time.Duration {
	if a.settled {
		return 0
	}
	if left := burstGap - a.now().Sub(a.last); left > 0 {
		return left
	}
	a.sync = max(a.sync, a.burst.sync())
	a.burst.next()
	a.settled = true
	return 0
}

// Packets is how many RTP packets the player got.
func (a *analysis) Packets() int {
	a.mu.Lock()
	defer a.mu.Unlock()
	return a.packets
}

// Report returns a copy of the report so far, with the A/V sync of the
// bursts that are over.
func (a *analysis) Report() *report.Report {
	a.mu.Lock()
	defer a.mu.Unlock()
	a.settle()
	return a.copyReport()
}

func (a *analysis) copyReport() *report.Report {
	r := *a.analyzer.Report()
	r.AVSync = a.sync
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

// Until waits until the report meets u and the burst that met it is over,
// and returns a copy of it, partial with an error when ctx ends before it
// meets u or a packet can't be analyzed.
func (a *analysis) Until(ctx context.Context, u report.Until) (*report.Report, error) {
	for {
		a.mu.Lock()
		left := a.settle()
		r, err, changed := a.copyReport(), a.err, a.changed
		a.mu.Unlock()
		if err != nil {
			return r, err
		}
		done := u.Done(r)
		if done && left == 0 {
			return r, nil
		}
		var settled <-chan time.Time
		if done {
			settled = time.After(left)
		}
		select {
		case <-changed:
		case <-settled:
		case <-ctx.Done():
			if done {
				return r, nil
			}
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
