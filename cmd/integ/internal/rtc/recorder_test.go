// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package rtc

import (
	"context"
	"encoding/binary"
	"io"
	"reflect"
	"strings"
	"testing"
	"time"

	"github.com/pion/interceptor"
	"github.com/pion/rtcp"
	"github.com/pion/rtp"
	"github.com/pion/sdp/v3"
)

// remotePackets is a reader of these RTP packets, one per read, then EOF.
func remotePackets(t *testing.T, pkts ...*rtp.Packet) interceptor.RTPReader {
	t.Helper()
	var raw [][]byte
	for _, p := range pkts {
		b, err := p.Marshal()
		if err != nil {
			t.Fatal(err)
		}
		raw = append(raw, b)
	}
	return interceptor.RTPReaderFunc(func(b []byte, a interceptor.Attributes) (int, interceptor.Attributes, error) {
		if len(raw) == 0 {
			return 0, a, io.EOF
		}
		n := copy(b, raw[0])
		raw = raw[1:]
		return n, a, nil
	})
}

func readAll(t *testing.T, r interceptor.RTPReader) {
	t.Helper()
	b := make([]byte, 1500)
	for {
		if _, _, err := r.Read(b, nil); err != nil {
			return
		}
	}
}

func seqPacket(ssrc uint32, seq uint16, payload ...byte) *rtp.Packet {
	return &rtp.Packet{Header: rtp.Header{Version: 2, SSRC: ssrc, SequenceNumber: seq}, Payload: payload}
}

// TestRecorderFirstTimestamp checks a stream logs the RTP time of its first
// packet, not of a later one.
func TestRecorderFirstTimestamp(t *testing.T) {
	r := NewRecorder()
	first := seqPacket(1, 10, 1)
	first.Timestamp = 3000
	next := seqPacket(1, 11, 1)
	next.Timestamp = 6000
	readAll(t, r.BindRemoteStream(&interceptor.StreamInfo{SSRC: 1, MimeType: "audio/opus"}, remotePackets(t, first, next)))
	if s := r.Record().Received("audio/opus"); s == nil || s.FirstTimestamp != 3000 {
		t.Errorf("stream %+v, want the first RTP time 3000", s)
	}
}

func osn(seq uint16) []byte {
	return binary.BigEndian.AppendUint16(nil, seq)
}

// TestRecorderRemote checks the log of received streams: the sequence
// numbers a later packet skipped, the ones that came late, a duplicate, past
// the 16-bit wrap, and the original sequence numbers of an RTX stream.
func TestRecorderRemote(t *testing.T) {
	r := NewRecorder()
	media := r.BindRemoteStream(&interceptor.StreamInfo{SSRC: 1, MimeType: "video/H264"}, remotePackets(t,
		seqPacket(1, 65534, 1), seqPacket(1, 65535, 1), seqPacket(1, 2, 1), seqPacket(1, 0, 1),
		seqPacket(1, 3, 1), seqPacket(1, 3, 1), seqPacket(1, 1, 1)))
	readAll(t, media)
	rtx := r.BindRemoteStream(&interceptor.StreamInfo{SSRC: 2, MimeType: "video/rtx"}, remotePackets(t,
		seqPacket(2, 100, osn(0)...), seqPacket(2, 101, osn(1)...)))
	readAll(t, rtx)

	rec := r.Record()
	s := rec.Received("video/h264")
	if s == nil {
		t.Fatalf("no video/H264 stream in %v", rec.In)
	}
	want := &Stream{SSRC: 1, MimeType: "video/H264", Packets: 7, Missed: []uint16{0, 1}, Late: []uint16{0, 1}, Repeated: []uint16{3}}
	if !reflect.DeepEqual(s, want) {
		t.Errorf("media stream %+v, want %+v", s, want)
	}
	x := rec.Received("video/rtx")
	if x == nil || x.SSRC != 2 || x.Packets != 2 || !reflect.DeepEqual(x.OSNs, []uint16{0, 1}) {
		t.Errorf("RTX stream %+v, want SSRC 2 with 2 packets of OSNs 0 and 1", x)
	}
	if rec.Sent("video/H264") != nil {
		t.Error("a sent stream, want none")
	}
}

// TestRecorderLocal checks the log of sent streams: a packet sent again,
// the RTX packets the NACK responder sends on the media stream's writer with
// the RTX SSRC, and the transport-wide sequence numbers.
func TestRecorderLocal(t *testing.T) {
	r := NewRecorder()
	info := &interceptor.StreamInfo{SSRC: 3, SSRCRetransmission: 4, MimeType: "video/H264",
		RTPHeaderExtensions: []interceptor.RTPHeaderExtension{{URI: sdp.TransportCCURI, ID: 5}}}
	var wrote int
	w := r.BindLocalStream(info, interceptor.RTPWriterFunc(func(h *rtp.Header, p []byte, _ interceptor.Attributes) (int, error) {
		wrote++
		return len(p), nil
	}))
	twcc := uint16(100)
	write := func(p *rtp.Packet) {
		b, err := (&rtp.TransportCCExtension{TransportSequence: twcc}).Marshal()
		if err != nil {
			t.Fatal(err)
		}
		if err := p.Header.SetExtension(5, b); err != nil {
			t.Fatal(err)
		}
		twcc++
		if _, err := w.Write(&p.Header, p.Payload, nil); err != nil {
			t.Fatal(err)
		}
	}
	for _, seq := range []uint16{1, 2, 3, 2} {
		write(seqPacket(3, seq, 1))
	}
	write(seqPacket(4, 50, osn(2)...))

	if wrote != 5 {
		t.Errorf("%v packets reached the writer, want 5", wrote)
	}
	rec := r.Record()
	s := rec.Sent("video/H264")
	want := &Stream{SSRC: 3, MimeType: "video/H264", Packets: 4, Repeated: []uint16{2}}
	if !reflect.DeepEqual(s, want) {
		t.Errorf("media stream %+v, want %+v", s, want)
	}
	x := rec.Sent("video/rtx")
	if x == nil || x.SSRC != 4 || !reflect.DeepEqual(x.OSNs, []uint16{2}) {
		t.Errorf("RTX stream %+v, want SSRC 4 with OSN 2", x)
	}
	if want := []uint16{100, 101, 102, 103, 104}; !reflect.DeepEqual(rec.TWCC, want) {
		t.Errorf("TWCC %v, want %v", rec.TWCC, want)
	}
}

// TestRecorderRTCP checks the RTCP packets sent and received, the sequence
// numbers NACKs ask for, and the TWCC feedback.
func TestRecorderRTCP(t *testing.T) {
	r := NewRecorder()
	nack := &rtcp.TransportLayerNack{MediaSSRC: 7, Nacks: rtcp.NackPairsFromSequenceNumbers([]uint16{10, 12, 13})}
	w := r.BindRTCPWriter(interceptor.RTCPWriterFunc(func(p []rtcp.Packet, _ interceptor.Attributes) (int, error) { return 0, nil }))
	if _, err := w.Write([]rtcp.Packet{nack, &rtcp.TransportLayerNack{MediaSSRC: 7, Nacks: rtcp.NackPairsFromSequenceNumbers([]uint16{12})},
		&rtcp.TransportLayerNack{MediaSSRC: 8, Nacks: rtcp.NackPairsFromSequenceNumbers([]uint16{1})}}, nil); err != nil {
		t.Fatal(err)
	}
	cc := &rtcp.TransportLayerCC{SenderSSRC: 1, MediaSSRC: 2, BaseSequenceNumber: 100, PacketStatusCount: 1, FbPktCount: 3,
		PacketChunks: []rtcp.PacketStatusChunk{&rtcp.RunLengthChunk{PacketStatusSymbol: rtcp.TypeTCCPacketReceivedSmallDelta, RunLength: 1}},
		RecvDeltas:   []*rtcp.RecvDelta{{Type: rtcp.TypeTCCPacketReceivedSmallDelta, Delta: 250}}}
	// The packet marshals the header it has, padded to 32 bits.
	cc.Header = rtcp.Header{Padding: true, Count: rtcp.FormatTCC, Type: rtcp.TypeTransportSpecificFeedback, Length: uint16(cc.Len()/4 - 1)}
	raw, err := rtcp.Marshal([]rtcp.Packet{cc, &rtcp.PictureLossIndication{MediaSSRC: 2}})
	if err != nil {
		t.Fatal(err)
	}
	sent := false
	rd := r.BindRTCPReader(interceptor.RTCPReaderFunc(func(b []byte, a interceptor.Attributes) (int, interceptor.Attributes, error) {
		if sent {
			return 0, a, io.EOF
		}
		sent = true
		return copy(b, raw), a, nil
	}))
	if _, _, err := rd.Read(make([]byte, 1500), nil); err != nil {
		t.Fatal(err)
	}

	rec := r.Record()
	if len(rec.RTCPOut) != 3 || len(rec.RTCPIn) != 2 {
		t.Fatalf("%v RTCP packets out and %v in, want 3 and 2", len(rec.RTCPOut), len(rec.RTCPIn))
	}
	if got := Nacked(rec.RTCPOut, 7); !reflect.DeepEqual(got, []uint16{10, 12, 13}) {
		t.Errorf("Nacked = %v, want [10 12 13]", got)
	}
	if got := Nacked(rec.RTCPIn, 7); len(got) != 0 {
		t.Errorf("Nacked of the received = %v, want none", got)
	}
	fb := TWCCFeedback(rec.RTCPIn)
	if len(fb) != 1 || fb[0].BaseSequenceNumber != 100 || fb[0].FbPktCount != 3 {
		t.Errorf("TWCC feedback %+v, want one with base 100 and count 3", fb)
	}
}

// TestRecorderUntil checks Until returns once the record meets the
// condition, and fails when the context ends first.
func TestRecorderUntil(t *testing.T) {
	r := NewRecorder()
	go func() {
		time.Sleep(20 * time.Millisecond)
		readAll(t, r.BindRemoteStream(&interceptor.StreamInfo{SSRC: 1, MimeType: "audio/opus"},
			remotePackets(t, seqPacket(1, 1), seqPacket(1, 2))))
	}()
	ctx, cancel := context.WithTimeout(context.Background(), time.Second)
	defer cancel()
	rec, err := r.Until(ctx, func(rec *Record) bool {
		s := rec.Received("audio/opus")
		return s != nil && s.Packets == 2
	})
	if err != nil || rec.Received("audio/opus").Packets != 2 {
		t.Fatalf("Until = %v, %v; want 2 packets", rec, err)
	}

	short, cancel2 := context.WithTimeout(context.Background(), 30*time.Millisecond)
	defer cancel2()
	if _, err := r.Until(short, func(rec *Record) bool { return false }); err == nil {
		t.Error("Until of a condition never met: no error")
	}
}

// TestUsername checks the session's STUN username: the server's ufrag, then
// the client's.
func TestUsername(t *testing.T) {
	s := &Session{Offer: "v=0\r\na=ice-ufrag:cli1\r\na=ice-pwd:clientpassword\r\n",
		Answer: "v=0\r\na=ice-ufrag:srv1\r\na=ice-pwd:serverpassword\r\n"}
	if got, err := s.Username(); err != nil || got != "srv1:cli1" {
		t.Errorf("Username = %q, %v; want srv1:cli1", got, err)
	}
	if _, err := (&Session{Offer: s.Offer}).Username(); err == nil {
		t.Error("Username without an answer: no error")
	}
}

// TestPlayerNack plays from the fake server, which skips a packet and then
// sends it: the player's NACK generator asks for it, and the recorder sees
// it missed, the NACK, and the packet late.
func TestPlayerNack(t *testing.T) {
	sfu := newFakeSFU(t)
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	player, err := Play(ctx, sfu.URL+"/rtc/v1/whep/?app=live&stream=s", nil)
	if err != nil {
		t.Fatal(err)
	}
	defer player.Close()

	// The fake server sends packet 1 until the player gets it: its side of
	// the connection may not be up yet.
	aud := []byte{0x09, 0xf0}
	got := false
	for !got {
		if err := sfu.video.WriteRTP(&rtp.Packet{Header: rtp.Header{Version: 2, SequenceNumber: 1, Timestamp: 1, Marker: true}, Payload: aud}); err != nil {
			t.Fatal(err)
		}
		wait, cancel := context.WithTimeout(ctx, 10*time.Millisecond)
		_, err := player.Recorder.Until(wait, func(rec *Record) bool { return rec.Received("video/H264") != nil })
		cancel()
		got = err == nil
		if ctx.Err() != nil {
			t.Fatal("the player gets no packet")
		}
	}
	for _, seq := range []uint16{2, 4, 5} {
		if err := sfu.video.WriteRTP(&rtp.Packet{Header: rtp.Header{Version: 2, SequenceNumber: seq, Timestamp: uint32(seq), Marker: true}, Payload: aud}); err != nil {
			t.Fatal(err)
		}
	}
	rec, err := player.Recorder.Until(ctx, func(rec *Record) bool {
		s := rec.Received("video/H264")
		return s != nil && len(Nacked(rec.RTCPOut, s.SSRC)) > 0
	})
	if err != nil {
		t.Fatalf("no NACK: %v", err)
	}
	s := rec.Received("video/H264")
	if !reflect.DeepEqual(s.Missed, []uint16{3}) || !reflect.DeepEqual(Nacked(rec.RTCPOut, s.SSRC), []uint16{3}) {
		t.Fatalf("missed %v, NACKed %v; want [3] and [3]", s.Missed, Nacked(rec.RTCPOut, s.SSRC))
	}
	if err := sfu.video.WriteRTP(&rtp.Packet{Header: rtp.Header{Version: 2, SequenceNumber: 3, Timestamp: 3, Marker: true}, Payload: aud}); err != nil {
		t.Fatal(err)
	}
	if _, err := player.Recorder.Until(ctx, func(rec *Record) bool {
		s := rec.Received("video/H264")
		return reflect.DeepEqual(s.Late, []uint16{3})
	}); err != nil {
		t.Errorf("the packet sent again is not late: %v", err)
	}
}

// TestOptionsRTX checks a session with RTX offers an rtx codec for its video
// codec, and a publisher an RTX stream, while one without offers neither.
func TestOptionsRTX(t *testing.T) {
	sfu := newFakeSFU(t)
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	pub, err := Publish(ctx, sfu.URL+"/rtc/v1/whip/?app=live&stream=s", &Options{RTX: true})
	if err != nil {
		t.Fatal(err)
	}
	defer pub.Close()
	player, err := Play(ctx, sfu.URL+"/rtc/v1/whep/?app=live&stream=s", &Options{RTX: true, Video: []string{"H264", "VP9"}})
	if err != nil {
		t.Fatal(err)
	}
	defer player.Close()
	plain, err := Play(ctx, sfu.URL+"/rtc/v1/whep/?app=live&stream=s", nil)
	if err != nil {
		t.Fatal(err)
	}
	defer plain.Close()

	for _, want := range []string{"rtx/90000", "apt=106", "a=ssrc-group:FID "} {
		if !strings.Contains(pub.Offer, want) {
			t.Errorf("the publisher's offer has no %q", want)
		}
	}
	for _, want := range []string{"a=rtpmap:107 rtx/90000", "a=fmtp:107 apt=106", "a=rtpmap:99 rtx/90000", "a=fmtp:99 apt=98"} {
		if !strings.Contains(player.Offer, want) {
			t.Errorf("the player's offer has no %q", want)
		}
	}
	if strings.Contains(plain.Offer, "rtx") {
		t.Error("an offer without RTX has rtx")
	}
}

// TestPublishTWCC checks a publisher with TWCC sends a transport-wide
// sequence number in each packet, from one counter for both tracks, while
// one without sends none.
func TestPublishTWCC(t *testing.T) {
	sfu := newFakeSFU(t)
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	samples := readSamples(t, 200*time.Millisecond)
	for _, twcc := range []bool{true, false} {
		pub, err := Publish(ctx, sfu.URL+"/rtc/v1/whip/?app=live&stream=s", &Options{TWCC: twcc})
		if err != nil {
			t.Fatal(err)
		}
		if err := pub.Send(ctx, samples, nil); err != nil {
			t.Fatal(err)
		}
		rec := pub.Recorder.Record()
		v, a := rec.Sent("video/H264"), rec.Sent("audio/opus")
		if v == nil || a == nil {
			t.Fatalf("TWCC %v: sent streams %v, want video and audio", twcc, rec.Out)
		}
		if !twcc {
			if len(rec.TWCC) != 0 {
				t.Errorf("without TWCC: %v transport-wide sequence numbers, want none", len(rec.TWCC))
			}
			pub.Close()
			continue
		}
		if len(rec.TWCC) != v.Packets+a.Packets {
			t.Errorf("%v transport-wide sequence numbers, want one for each of %v packets", len(rec.TWCC), v.Packets+a.Packets)
		}
		for i := 1; i < len(rec.TWCC); i++ {
			if rec.TWCC[i] != rec.TWCC[i-1]+1 {
				t.Errorf("transport-wide sequence %v after %v", rec.TWCC[i], rec.TWCC[i-1])
				break
			}
		}
		pub.Close()
	}
}
