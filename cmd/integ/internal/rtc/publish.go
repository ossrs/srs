// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package rtc

import (
	"context"
	"encoding/binary"
	"errors"
	"fmt"
	"os"
	"slices"
	"sync"
	"time"

	"github.com/pion/rtcp"
	"github.com/pion/rtp"
	"github.com/pion/rtp/codecs/av1/obu"
	"github.com/pion/webrtc/v4"

	"srs-integ/internal/media"
	"srs-integ/internal/rtmp"
)

// A Sample is one frame to publish: H.264 or HEVC in Annex B, with the
// parameter sets before each keyframe, a VP9 frame, an AV1 temporal unit
// with the sequence header before each keyframe, or one Opus or G.711
// packet.
type Sample struct {
	Video bool
	// Codec is "h264", "hevc", "vp9", "av1", "opus", "pcmu" or "pcma".
	Codec string
	DTS   time.Duration
	Key   bool
	Data  []byte
}

// ReadMP4 reads the video and Opus samples of an MP4 file, sorted by decode
// time: H.264 or HEVC converted to Annex B with the parameter sets before
// each keyframe, VP9 as it is, and AV1 with the sequence header of its av1C
// before each keyframe that lacks one.
func ReadMP4(path string) ([]Sample, error) {
	f, err := os.Open(path)
	if err != nil {
		return nil, err
	}
	defer f.Close()
	info, err := f.Stat()
	if err != nil {
		return nil, err
	}
	mf, err := media.ReadMP4(f, info.Size())
	if err != nil {
		return nil, err
	}

	var out []Sample
	if t := mf.Track("vide"); t != nil {
		codec, sets, lengthSize := "", []byte(nil), 0
		switch t.Codec {
		case "avc1", "avc3":
			codec = "h264"
			sets, lengthSize, err = parseAVCC(t.Config)
		case "hvc1", "hev1":
			codec = "hevc"
			sets, lengthSize, err = parseHVCC(t.Config)
		case "vp09":
			codec = "vp9"
		case "av01":
			codec = "av1"
			if len(t.Config) < 4 {
				return nil, errors.New("short av1C")
			}
			sets = t.Config[4:]
		default:
			return nil, fmt.Errorf("video codec %v is not supported", t.Codec)
		}
		if err != nil {
			return nil, err
		}
		for _, s := range t.Samples {
			b, err := mf.ReadSample(s)
			if err != nil {
				return nil, err
			}
			data := b
			switch codec {
			case "h264", "hevc":
				data = nil
				if s.Key {
					data = append(data, sets...)
				}
				if data, err = appendAnnexB(data, b, lengthSize); err != nil {
					return nil, fmt.Errorf("video sample at %v: %w", s.DTS, err)
				}
			case "av1":
				if h, err := obu.ParseOBUHeader(b); s.Key && (err != nil || h.Type != obu.OBUSequenceHeader) {
					data = append(append([]byte(nil), sets...), b...)
				}
			}
			out = append(out, Sample{Video: true, Codec: codec, DTS: at(s.DTS, t.Timescale), Key: s.Key, Data: data})
		}
	}
	if t := mf.Track("soun"); t != nil {
		if t.Codec != "Opus" {
			return nil, fmt.Errorf("audio codec %v, want Opus", t.Codec)
		}
		for _, s := range t.Samples {
			b, err := mf.ReadSample(s)
			if err != nil {
				return nil, err
			}
			out = append(out, Sample{Codec: "opus", DTS: at(s.DTS, t.Timescale), Data: b})
		}
	}
	sortSamples(out)
	return out, nil
}

// at is a time in a timescale.
func at(t uint64, timescale uint32) time.Duration {
	return time.Duration(t) * time.Second / time.Duration(timescale)
}

var startCode = []byte{0, 0, 0, 1}

// parseAVCC returns the SPS and PPS of an avcC in Annex B, and its NAL length
// size.
func parseAVCC(b []byte) (sets []byte, lengthSize int, err error) {
	if len(b) < 6 {
		return nil, 0, errors.New("short avcC")
	}
	lengthSize = int(b[4]&3) + 1
	p := 5
	for _, mask := range []byte{0x1f, 0xff} {
		if p >= len(b) {
			return nil, 0, errors.New("short avcC")
		}
		n := int(b[p] & mask)
		p++
		for range n {
			if p+2 > len(b) {
				return nil, 0, errors.New("short avcC")
			}
			size := int(binary.BigEndian.Uint16(b[p:]))
			p += 2
			if p+size > len(b) {
				return nil, 0, errors.New("short avcC")
			}
			sets = append(append(sets, startCode...), b[p:p+size]...)
			p += size
		}
	}
	return sets, lengthSize, nil
}

// appendAnnexB appends the NALs of a sample with NAL length prefixes as Annex
// B.
func appendAnnexB(out, b []byte, lengthSize int) ([]byte, error) {
	for len(b) > 0 {
		if len(b) < lengthSize {
			return nil, errors.New("cut NAL length")
		}
		var n int
		for _, c := range b[:lengthSize] {
			n = n<<8 | int(c)
		}
		b = b[lengthSize:]
		if n > len(b) {
			return nil, fmt.Errorf("NAL of %v bytes, %v left", n, len(b))
		}
		out = append(append(out, startCode...), b[:n]...)
		b = b[n:]
	}
	return out, nil
}

// A pliCounter counts the picture loss indications a server sends to a
// publisher.
type pliCounter struct {
	mu         sync.Mutex
	plis       int
	pliChanged chan struct{}
}

func newPLICounter() *pliCounter {
	return &pliCounter{pliChanged: make(chan struct{})}
}

// countPLIs counts the picture loss indications among RTCP packets.
func (p *pliCounter) countPLIs(pkts []rtcp.Packet) {
	n := 0
	for _, pkt := range pkts {
		if _, ok := pkt.(*rtcp.PictureLossIndication); ok {
			n++
		}
	}
	if n == 0 {
		return
	}
	p.mu.Lock()
	defer p.mu.Unlock()
	p.plis += n
	close(p.pliChanged)
	p.pliChanged = make(chan struct{})
}

// PLIs is how many picture loss indications the server has sent, which ask
// for a keyframe, such as when a player starts.
func (p *pliCounter) PLIs() int {
	p.mu.Lock()
	defer p.mu.Unlock()
	return p.plis
}

// WaitPLI waits until the server has sent more than n picture loss
// indications.
func (p *pliCounter) WaitPLI(ctx context.Context, n int) error {
	for {
		p.mu.Lock()
		plis, changed := p.plis, p.pliChanged
		p.mu.Unlock()
		if plis > n {
			return nil
		}
		select {
		case <-changed:
		case <-ctx.Done():
			return fmt.Errorf("%w: %v PLIs, want more than %v", ctx.Err(), plis, n)
		}
	}
}

// send writes samples in order with write, paced by pacer like ffmpeg -re,
// or at once when pacer is nil, until they end or ctx ends.
func send(ctx context.Context, write func(Sample) error, samples []Sample, pacer *rtmp.Pacer) error {
	for _, s := range samples {
		var err error
		if pacer != nil {
			err = pacer.Wait(ctx, uint32(s.DTS/time.Millisecond))
		} else {
			err = ctx.Err()
		}
		if err != nil {
			return err
		}
		if err := write(s); err != nil {
			return err
		}
	}
	return nil
}

// A Publisher sends a video and an audio track to the server: H.264 and
// Opus, or the codecs of its options.
type Publisher struct {
	Session
	*pliCounter
	video, audio *localTrack
}

// Publish connects a publisher by WHIP to url, such as
// http://host:1985/rtc/v1/whip/?app=live&stream=s, or by the legacy API when
// opts.Legacy is set, and waits until ICE and DTLS are connected. Each track
// sends the codec the server chose of those the options offer.
func Publish(ctx context.Context, url string, opts *Options) (*Publisher, error) {
	video, audio, err := sessionCodecs(opts)
	if err != nil {
		return nil, err
	}
	pc, rec, err := newPeerConnection(opts)
	if err != nil {
		return nil, err
	}
	p := &Publisher{Session: Session{PC: pc, Recorder: rec, opts: opts}, pliCounter: newPLICounter()}
	p.video = newLocalTrack(webrtc.RTPCodecTypeVideo, video)
	p.audio = newLocalTrack(webrtc.RTPCodecTypeAudio, audio)
	for _, t := range []*localTrack{p.video, p.audio} {
		sender, err := pc.AddTransceiverFromTrack(t, webrtc.RTPTransceiverInit{Direction: webrtc.RTPTransceiverDirectionSendonly})
		if err != nil {
			pc.Close()
			return nil, err
		}
		// Reading RTCP runs the interceptors, such as the NACK responder.
		go func() {
			for {
				pkts, _, err := sender.Sender().ReadRTCP()
				if err != nil {
					return
				}
				p.countPLIs(pkts)
			}
		}()
	}
	if err := p.connect(ctx, url); err != nil {
		return nil, err
	}
	return p, nil
}

// Write sends one sample, as the RTP packets of its track. It waits until
// the track is bound to the connection, so no packet is lost before, and
// fails on a sample of another codec than the track sends.
func (p *Publisher) Write(s Sample) error {
	t := p.audio
	if s.Video {
		t = p.video
	}
	return t.write(s)
}

// Send writes samples in order, paced by pacer like ffmpeg -re, or at once
// when pacer is nil, until they end or ctx ends.
func (p *Publisher) Send(ctx context.Context, samples []Sample, pacer *rtmp.Pacer) error {
	return send(ctx, p.Write, samples, pacer)
}

// A localTrack is a track the publisher writes RTP packets to, with the
// sequence numbers and timestamps of its sender, which is made for the codec
// the track is bound to.
type localTrack struct {
	kind   webrtc.RTPCodecType
	codecs []string

	bound  chan struct{}
	once   sync.Once
	mu     sync.Mutex
	key    string
	sender *sender
	ssrc   webrtc.SSRC
	pt     webrtc.PayloadType
	out    webrtc.TrackLocalWriter
}

// newLocalTrack makes a track that may send any of these codecTable codecs.
func newLocalTrack(kind webrtc.RTPCodecType, codecs []string) *localTrack {
	return &localTrack{kind: kind, codecs: codecs, bound: make(chan struct{})}
}

// Bind takes the first negotiated codec the track may send.
func (t *localTrack) Bind(ctx webrtc.TrackLocalContext) (webrtc.RTPCodecParameters, error) {
	for _, c := range ctx.CodecParameters() {
		key := mimeKey(c.MimeType)
		if !slices.Contains(t.codecs, key) {
			continue
		}
		t.mu.Lock()
		if t.sender == nil || t.key != key {
			t.key, t.sender = key, newCodecSender(key)
		}
		t.ssrc, t.pt, t.out = ctx.SSRC(), c.PayloadType, ctx.WriteStream()
		t.mu.Unlock()
		t.once.Do(func() { close(t.bound) })
		return c, nil
	}
	return webrtc.RTPCodecParameters{}, webrtc.ErrUnsupportedCodec
}

func (t *localTrack) Unbind(webrtc.TrackLocalContext) error {
	t.mu.Lock()
	defer t.mu.Unlock()
	t.out = nil
	return nil
}

func (t *localTrack) ID() string                { return t.kind.String() }
func (t *localTrack) RID() string               { return "" }
func (t *localTrack) StreamID() string          { return "srs-integ" }
func (t *localTrack) Kind() webrtc.RTPCodecType { return t.kind }

func (t *localTrack) write(s Sample) error {
	select {
	case <-t.bound:
	case <-time.After(time.Second):
		return fmt.Errorf("%v track not bound after 1s", t.kind)
	}
	t.mu.Lock()
	defer t.mu.Unlock()
	if t.out == nil {
		return fmt.Errorf("%v track is unbound", t.kind)
	}
	if s.Codec != "" && sdpKey(s.Codec) != t.key {
		return fmt.Errorf("a %v sample on a %v track that sends %v", s.Codec, t.kind, t.key)
	}
	for _, p := range t.sender.packets(s) {
		p.SSRC, p.PayloadType = uint32(t.ssrc), uint8(t.pt)
		if _, err := t.out.WriteRTP(&p.Header, p.Payload); err != nil {
			return err
		}
	}
	return nil
}

// A sender makes the RTP packets of one track: the payloads of each sample,
// with sequence numbers and timestamps from random starts, the timestamp from
// the sample's decode time.
type sender struct {
	payloader rtp.Payloader
	clock     uint32
	video     bool
	seq       uint16
	base      uint32
}

// mtu is the largest RTP payload.
const mtu = 1200

// packets returns the RTP packets of a sample. The last packet of a video
// frame has the marker bit.
func (s *sender) packets(sample Sample) []*rtp.Packet {
	ts := s.base + uint32(int64(sample.DTS)*int64(s.clock)/int64(time.Second))
	payloads := s.payloader.Payload(mtu, sample.Data)
	out := make([]*rtp.Packet, 0, len(payloads))
	for i, p := range payloads {
		out = append(out, &rtp.Packet{
			Header: rtp.Header{
				Version: 2, SequenceNumber: s.seq, Timestamp: ts,
				Marker: s.video && i == len(payloads)-1,
			},
			Payload: p,
		})
		s.seq++
	}
	return out
}
