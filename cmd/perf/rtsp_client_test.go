package main

import (
	"bytes"
	"net"
	"strings"
	"sync"
	"testing"
	"time"

	"github.com/bluenviron/gortsplib/v5"
	"github.com/bluenviron/gortsplib/v5/pkg/base"
	"github.com/bluenviron/gortsplib/v5/pkg/description"
	"github.com/bluenviron/gortsplib/v5/pkg/format"
	"github.com/bluenviron/mediacommon/v2/pkg/codecs/mpeg4audio"
	"github.com/pion/rtp"
)

// avcc is an FLV AVC video payload of NALUs with 4-byte lengths.
func avcc(frame byte, nalus ...[]byte) []byte {
	b := []byte{frame, 1, 0, 0, 0}
	for _, n := range nalus {
		b = append(b, byte(len(n)>>24), byte(len(n)>>16), byte(len(n)>>8), byte(len(n)))
		b = append(b, n...)
	}
	return b
}

// rtspTestServer serves one H.264 and AAC stream at /live/<stream> and sends
// its packets to each player once its PLAY is answered.
type rtspTestServer struct {
	server  *gortsplib.Server
	stream  *gortsplib.ServerStream
	addr    string
	path    string
	packets []rtspTestPacket
	mu      sync.Mutex
}

type rtspTestPacket struct {
	media *description.Media
	pkt   *rtp.Packet
}

func startRTSPTestServer(t *testing.T, path string, build func(video, audio *description.Media) []rtspTestPacket) *rtspTestServer {
	t.Helper()
	video := &description.Media{Type: description.MediaTypeVideo, Formats: []format.Format{&format.H264{PayloadTyp: 96, PacketizationMode: 1}}}
	audio := &description.Media{Type: description.MediaTypeAudio, Formats: []format.Format{&format.MPEG4Audio{PayloadTyp: 97, Config: &mpeg4audio.AudioSpecificConfig{Type: 2, SampleRate: 44100, ChannelCount: 2}, SizeLength: 13, IndexLength: 3, IndexDeltaLength: 3}}}
	s := &rtspTestServer{path: path, packets: build(video, audio)}
	ln := make(chan net.Listener, 1)
	s.server = &gortsplib.Server{
		Handler:     s,
		RTSPAddress: "127.0.0.1:0",
		Listen: func(network, address string) (net.Listener, error) {
			l, err := net.Listen(network, address)
			if err == nil {
				ln <- l
			}
			return l, err
		},
	}
	if err := s.server.Start(); err != nil {
		t.Fatal(err)
	}
	s.addr = (<-ln).Addr().String()
	s.stream = &gortsplib.ServerStream{Server: s.server, Desc: &description.Session{Medias: []*description.Media{video, audio}}}
	if err := s.stream.Initialize(); err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() {
		s.stream.Close()
		s.server.Close()
	})
	return s
}

func (s *rtspTestServer) OnDescribe(ctx *gortsplib.ServerHandlerOnDescribeCtx) (*base.Response, *gortsplib.ServerStream, error) {
	if ctx.Path != s.path {
		return &base.Response{StatusCode: base.StatusNotFound}, nil, nil
	}
	return &base.Response{StatusCode: base.StatusOK}, s.stream, nil
}

func (s *rtspTestServer) OnSetup(ctx *gortsplib.ServerHandlerOnSetupCtx) (*base.Response, *gortsplib.ServerStream, error) {
	return &base.Response{StatusCode: base.StatusOK}, s.stream, nil
}

func (s *rtspTestServer) OnPlay(ctx *gortsplib.ServerHandlerOnPlayCtx) (*base.Response, error) {
	go func() {
		time.Sleep(50 * time.Millisecond)
		for _, p := range s.packets {
			pkt := *p.pkt
			s.stream.WritePacketRTP(p.media, &pkt)
		}
	}()
	return &base.Response{StatusCode: base.StatusOK}, nil
}

// The RTSP player sets up the H.264 and AAC media over TCP, plays, and reports
// each H.264 access unit as an FLV AVC payload, in milliseconds from its
// first video packet, and each AAC packet as audio at the newest video time,
// since the two RTP clocks start at unrelated values.
func TestRTSPPlayerReportsAccessUnits(t *testing.T) {
	idr := append([]byte{0x65}, bytes.Repeat([]byte{0xaa}, 3000)...)
	slice := []byte{0x41, 0x9a, 0x02}
	server := startRTSPTestServer(t, "/live/perf-run", func(video, audio *description.Media) []rtspTestPacket {
		enc, err := video.Formats[0].(*format.H264).CreateEncoder()
		if err != nil {
			t.Fatal(err)
		}
		var packets []rtspTestPacket
		// The RTP timestamps start near the 32-bit wrap, as a server may pick.
		base := uint32(0xffffff00)
		for i, au := range [][][]byte{{{0x67, 0x64}, {0x68, 0xee}, idr}, {slice}} {
			pkts, err := enc.Encode(au)
			if err != nil {
				t.Fatal(err)
			}
			for _, p := range pkts {
				p.Timestamp = base + uint32(i)*3600
				packets = append(packets, rtspTestPacket{video, p})
			}
			if i == 0 {
				packets = append(packets, rtspTestPacket{audio, &rtp.Packet{Header: rtp.Header{Version: 2, PayloadType: 97, Marker: true, Timestamp: 12345, SequenceNumber: 1}, Payload: []byte{0, 16, 0, 8, 0x21, 0x22}}})
			}
		}
		return packets
	})
	c, err := dialRTSP(server.addr, "live")
	if err != nil {
		t.Fatal(err)
	}
	stop := make(chan struct{})
	report := make(chan rtmpMessage, 16)
	done := make(chan error, 1)
	go func() { done <- c.play("perf-run", stop, report) }()
	// Each media has its own callback, so audio may come before or after the
	// first video frame; either way it is at the newest video time, 0.
	want := []rtmpMessage{
		{typeID: 9, timestamp: 0, payload: avcc(0x17, []byte{0x67, 0x64}, []byte{0x68, 0xee}, idr)},
		{typeID: 9, timestamp: 40, payload: avcc(0x27, slice)},
	}
	var videos []rtmpMessage
	audios := 0
	for len(videos) < len(want) || audios < 1 {
		select {
		case m := <-report:
			switch {
			case m.typeID == 9:
				videos = append(videos, m)
			case m.typeID == 8 && m.timestamp == 0 && len(m.payload) > 0:
				audios++
			default:
				t.Fatalf("message = type %d ts %d %d bytes", m.typeID, m.timestamp, len(m.payload))
			}
		case err := <-done:
			t.Fatalf("player ended: %v", err)
		case <-time.After(3 * time.Second):
			t.Fatalf("got %d video and %d audio messages", len(videos), audios)
		}
	}
	for i, w := range want {
		m := videos[i]
		if m.timestamp != w.timestamp || !bytes.Equal(m.payload, w.payload) {
			t.Fatalf("video %d = ts %d %d bytes, want ts %d %d bytes", i, m.timestamp, len(m.payload), w.timestamp, len(w.payload))
		}
	}
	close(stop)
	select {
	case err := <-done:
		if err != nil {
			t.Fatalf("stopped player returned %v", err)
		}
	case <-time.After(3 * time.Second):
		t.Fatal("player did not stop")
	}
}

// A stream SRS does not serve is an error, not a player that waits.
func TestRTSPPlayerRejectsMissingStream(t *testing.T) {
	server := startRTSPTestServer(t, "/live/perf-run", func(video, audio *description.Media) []rtspTestPacket { return nil })
	c, err := dialRTSP(server.addr, "live")
	if err != nil {
		t.Fatal(err)
	}
	err = c.play("missing", make(chan struct{}), make(chan rtmpMessage, 1))
	if err == nil || !strings.Contains(err.Error(), "404") {
		t.Fatalf("err = %v, want a 404 error", err)
	}
}

// A dial that times out is a net timeout error, so dialPlayer dials again.
func TestRTSPDialErrorIsNetError(t *testing.T) {
	ln, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	addr := ln.Addr().String()
	ln.Close()
	if _, err := dialRTSP(addr, "live"); err == nil {
		t.Fatal("dial to a closed port succeeded")
	} else if _, ok := err.(net.Error); !ok {
		t.Fatalf("err = %T %v, want a net.Error", err, err)
	}
}

// An RTSP player's video payload has the access unit's NALUs, which may drop
// or add NALUs around the slice, such as an SEI or an AUD, so the source
// timeline also names a frame by its first slice NALU.
func TestTimelineMatchesFrameByFirstSlice(t *testing.T) {
	sei := []byte{0x06, 0x05, 0x01, 0x02}
	tags := []rtmpMessage{
		{timestamp: 0, typeID: 9, payload: []byte{0x17, 0, 0, 0, 0, 1, 0x64}},
		{timestamp: 0, typeID: 9, payload: avcc(0x17, sei, []byte{0x65, 1, 2, 3})},
		{timestamp: 40, typeID: 9, payload: avcc(0x27, []byte{0x41, 4, 5})},
		{timestamp: 80, typeID: 9, payload: avcc(0x27, []byte{0x41, 6, 7})},
		// Two frames with one slice name no time.
		{timestamp: 120, typeID: 9, payload: avcc(0x27, sei, []byte{0x41, 8})},
		{timestamp: 160, typeID: 9, payload: avcc(0x27, []byte{0x41, 8})},
	}
	s := newSourceTimeline(tags)
	cases := []struct {
		payload []byte
		at      uint32
		ok      bool
	}{
		{avcc(0x17, []byte{0x67, 0x64}, []byte{0x68, 0xee}, []byte{0x65, 1, 2, 3}), 0, true},
		{avcc(0x27, []byte{0x09, 0xf0}, []byte{0x41, 6, 7}), 80, true},
		{avcc(0x27, []byte{0x41, 4, 5}), 40, true},
		{avcc(0x27, []byte{0x09, 0xf0}, []byte{0x41, 8}), 0, false},
		{avcc(0x27, []byte{0x41, 9}), 0, false},
		{avcc(0x27, sei), 0, false},
		{[]byte{0x27, 1, 0, 0, 0, 0, 0, 0, 9, 0x41}, 0, false},
	}
	for i, c := range cases {
		at, ok := s.inLoop(rtmpMessage{typeID: 9, payload: c.payload})
		if at != c.at || ok != c.ok {
			t.Errorf("case %d: inLoop = %d, %v; want %d, %v", i, at, ok, c.at, c.ok)
		}
	}
}
