// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT
package rtmp

import (
	"bytes"
	"context"
	"encoding/binary"
	"fmt"
	"io"
	"reflect"
	"strings"
	"sync"
	"testing"
)

type errWriter struct{}

func (errWriter) Write([]byte) (int, error) { return 0, io.ErrClosedPipe }

func TestHandshakeSimpleAndErrors(t *testing.T) {
	h := NewHandshake()
	var b bytes.Buffer
	if err := h.WriteC0S0(&b); err != nil {
		t.Fatalf("WriteC0S0 err=%v", err)
	}
	c0, err := h.ReadC0S0(&b)
	if err != nil || !bytes.Equal(c0, []byte{3}) {
		t.Fatalf("ReadC0S0=%v, err=%v", c0, err)
	}
	if err := h.WriteC0S0(errWriter{}); err == nil {
		t.Fatal("WriteC0S0 should fail")
	}
	if _, err := h.ReadC0S0(bytes.NewReader(nil)); err == nil {
		t.Fatal("ReadC0S0 should fail")
	}

	b.Reset()
	if err := h.WriteC1S1(&b); err != nil {
		t.Fatalf("WriteC1S1 err=%v", err)
	}
	if b.Len() != 1536 {
		t.Fatalf("C1S1 len=%v", b.Len())
	}
	c1, err := h.ReadC1S1(&b)
	if err != nil || len(c1) != 1536 || !bytes.Equal(h.C1S1(), c1) {
		t.Fatalf("ReadC1S1 len=%v, cached=%v, err=%v", len(c1), bytes.Equal(h.C1S1(), c1), err)
	}
	if err := h.WriteC1S1(errWriter{}); err == nil {
		t.Fatal("WriteC1S1 should fail")
	}
	if _, err := h.ReadC1S1(bytes.NewReader(make([]byte, 1535))); err == nil {
		t.Fatal("ReadC1S1 should fail")
	}

	b.Reset()
	if err := h.WriteC2S2(&b, c1); err != nil {
		t.Fatalf("WriteC2S2 err=%v", err)
	}
	c2, err := h.ReadC2S2(&b)
	if err != nil || !bytes.Equal(c2, c1) {
		t.Fatalf("ReadC2S2 match=%v, err=%v", bytes.Equal(c2, c1), err)
	}
	if err := h.WriteC2S2(errWriter{}, c1); err == nil {
		t.Fatal("WriteC2S2 should fail")
	}
	if _, err := h.ReadC2S2(bytes.NewReader(make([]byte, 1535))); err == nil {
		t.Fatal("ReadC2S2 should fail")
	}
}

func TestSettingsChunkStreamAndMessageConstructors(t *testing.T) {
	if s := newSettings(); s.chunkSize != defaultChunkSize {
		t.Fatalf("chunk size=%v", s.chunkSize)
	}
	if c := newChunkStream(); c == nil || c.count != 0 {
		t.Fatalf("chunk stream=%#v", c)
	}
	m := NewMessage().asMessage()
	m.messageHeader.MessageType = MessageTypeAudio
	m.messageHeader.Timestamp = 99
	m.payload = []byte{1, 2, 3}
	if m.MessageType() != MessageTypeAudio || m.Timestamp() != 99 || !bytes.Equal(m.Payload(), []byte{1, 2, 3}) || m.asMessage() != m {
		t.Fatalf("bad message accessors")
	}
	sm := NewStreamMessage(7).asMessage()
	if sm.streamID != 7 || sm.betterCid != chunkIDOverStream {
		t.Fatalf("stream message=%#v", sm.messageHeader)
	}
}

func TestBasicHeaderVariantsAndErrors(t *testing.T) {
	ctx := context.Background()
	cases := []struct {
		name string
		data []byte
		fmt  formatType
		cid  chunkID
	}{
		{"one-byte", []byte{0x85}, formatType2, 5},
		{"two-byte", []byte{0x40, 0x0a}, formatType1, 74},
		{"three-byte", []byte{0xc1, 0x01, 0x02}, formatType3, 577},
	}
	for _, tt := range cases {
		t.Run(tt.name, func(t *testing.T) {
			p := NewProtocol(bytes.NewBuffer(tt.data)).(*protocol)
			fmt, cid, err := p.readBasicHeader(ctx)
			if err != nil || fmt != tt.fmt || cid != tt.cid {
				t.Fatalf("fmt=%v cid=%v err=%v", fmt, cid, err)
			}
		})
	}
	for _, data := range [][]byte{{}, {0x00}} {
		p := NewProtocol(bytes.NewBuffer(data)).(*protocol)
		if _, _, err := p.readBasicHeader(ctx); err == nil {
			t.Fatalf("readBasicHeader(%x) should fail", data)
		}
	}
}

func TestReadMessageHeadersPayloadsAndChunks(t *testing.T) {
	ctx := context.Background()
	var in bytes.Buffer
	// fmt0 cid=5, timestamp=10, len=3, type audio, stream=1, payload 010203.
	in.Write([]byte{0x05, 0x00, 0x00, 0x0a, 0x00, 0x00, 0x03, byte(MessageTypeAudio), 0x01, 0x00, 0x00, 0x00, 1, 2, 3})
	// fmt1 same cid, delta=5, len=2, type video, payload 0405.
	in.Write([]byte{0x45, 0x00, 0x00, 0x05, 0x00, 0x00, 0x02, byte(MessageTypeVideo), 4, 5})
	// fmt2 same cid, delta=7, reuses len/type/stream, payload 0607.
	in.Write([]byte{0x85, 0x00, 0x00, 0x07, 6, 7})
	// fmt3 same cid, reuses delta and advances timestamp, payload 0809.
	in.Write([]byte{0xc5, 8, 9})

	p := NewProtocol(&in).(*protocol)
	for i, want := range []struct {
		typ MessageType
		ts  uint64
		pl  []byte
	}{
		{MessageTypeAudio, 10, []byte{1, 2, 3}},
		{MessageTypeVideo, 15, []byte{4, 5}},
		{MessageTypeVideo, 22, []byte{6, 7}},
		{MessageTypeVideo, 29, []byte{8, 9}},
	} {
		m, err := p.ReadMessage(ctx)
		if err != nil {
			t.Fatalf("ReadMessage #%v err=%v", i, err)
		}
		if m.MessageType() != want.typ || m.Timestamp() != want.ts || !bytes.Equal(m.Payload(), want.pl) {
			t.Fatalf("message #%v type=%v ts=%v payload=%x", i, m.MessageType(), m.Timestamp(), m.Payload())
		}
	}
}

func TestReadMessageExtendedTimestampAndChunking(t *testing.T) {
	ctx := context.Background()
	var in bytes.Buffer
	payload := []byte{1, 2, 3, 4, 5}
	// fmt0 cid=5, normal timestamp=0xffffff, extended timestamp has high bit set and should be masked.
	in.Write([]byte{0x05, 0xff, 0xff, 0xff, 0x00, 0x00, byte(len(payload)), byte(MessageTypeVideo), 0x01, 0x00, 0x00, 0x00})
	binary.Write(&in, binary.BigEndian, uint32(0x8000002a))
	in.Write(payload[:2])
	// continuation chunk has fmt3 and extended timestamp too.
	in.Write([]byte{0xc5})
	binary.Write(&in, binary.BigEndian, uint32(0x8000002a))
	in.Write(payload[2:4])
	in.Write([]byte{0xc5})
	binary.Write(&in, binary.BigEndian, uint32(0x8000002a))
	in.Write(payload[4:])

	p := NewProtocol(&in).(*protocol)
	p.input.opt.chunkSize = 2
	m, err := p.ReadMessage(ctx)
	if err != nil {
		t.Fatalf("ReadMessage err=%v", err)
	}
	if m.Timestamp() != 42 || !bytes.Equal(m.Payload(), payload) {
		t.Fatalf("ts=%v payload=%x", m.Timestamp(), m.Payload())
	}
}

func TestReadMessageExtendedTimestampAsDeltaForFmt1(t *testing.T) {
	ctx := context.Background()
	var in bytes.Buffer
	// fmt0 cid=5, timestamp=10, len=1, type video, stream=1, payload AA.
	in.Write([]byte{0x05, 0x00, 0x00, 0x0a, 0x00, 0x00, 0x01, byte(MessageTypeVideo), 0x01, 0x00, 0x00, 0x00, 0xAA})
	// fmt1 cid=5, delta=0xffffff so the real delta is carried in the extended timestamp (=100),
	// len=1, type video, payload BB. For fmt=1/2 the extended timestamp is a delta, so the
	// message timestamp must accumulate: 10 + 100 = 110 (not be replaced by 100).
	in.Write([]byte{0x45, 0xff, 0xff, 0xff, 0x00, 0x00, 0x01, byte(MessageTypeVideo)})
	binary.Write(&in, binary.BigEndian, uint32(100))
	in.Write([]byte{0xBB})

	p := NewProtocol(&in).(*protocol)
	for i, want := range []struct {
		ts uint64
		pl []byte
	}{
		{10, []byte{0xAA}},
		{110, []byte{0xBB}},
	} {
		m, err := p.ReadMessage(ctx)
		if err != nil {
			t.Fatalf("ReadMessage #%v err=%v", i, err)
		}
		if m.Timestamp() != want.ts || !bytes.Equal(m.Payload(), want.pl) {
			t.Fatalf("message #%v ts=%v payload=%x", i, m.Timestamp(), m.Payload())
		}
	}
}

func TestReadMessageType3OmitsExtendedTimestamp(t *testing.T) {
	ctx := context.Background()
	var in bytes.Buffer
	// fmt0 cid=5, timestamp=0xffffff so an extended timestamp (=100) is present, len=8,
	// type video, stream=1, with the first 4 payload bytes.
	in.Write([]byte{0x05, 0xff, 0xff, 0xff, 0x00, 0x00, 0x08, byte(MessageTypeVideo), 0x01, 0x00, 0x00, 0x00})
	binary.Write(&in, binary.BigEndian, uint32(100))
	in.Write([]byte{0x01, 0x02, 0x03, 0x04})
	// fmt3 continuation from a librtmp/ffmpeg-style sender that omits the extended timestamp.
	// The next 4 bytes are payload, not an extended timestamp; the parser must detect the
	// mismatch against the stored value (100) and treat them as payload, keeping ts=100.
	in.Write([]byte{0xc5, 0x05, 0x06, 0x07, 0x08})

	p := NewProtocol(&in).(*protocol)
	p.input.opt.chunkSize = 4
	m, err := p.ReadMessage(ctx)
	if err != nil {
		t.Fatalf("ReadMessage err=%v", err)
	}
	if m.Timestamp() != 100 || !bytes.Equal(m.Payload(), []byte{1, 2, 3, 4, 5, 6, 7, 8}) {
		t.Fatalf("ts=%v payload=%x", m.Timestamp(), m.Payload())
	}
}

func TestReadMessageHeaderErrors(t *testing.T) {
	ctx := context.Background()
	// Fresh non-zero chunk with fmt1 is rejected.
	p := NewProtocol(bytes.NewBuffer([]byte{0x45})).(*protocol)
	if _, err := p.ReadMessage(ctx); err == nil || !strings.Contains(err.Error(), "fresh chunk") {
		t.Fatalf("fresh fmt1 err=%v", err)
	}
	// Existing partial message cannot restart with fmt0.
	p = NewProtocol(bytes.NewBuffer([]byte{0x05, 0, 0, 0, 0, 0, 3, byte(MessageTypeAudio), 1, 0, 0, 0, 1, 0x05})).(*protocol)
	p.input.opt.chunkSize = 1
	if _, err := p.ReadMessage(ctx); err == nil || !strings.Contains(err.Error(), "exists chunk") {
		t.Fatalf("restart err=%v", err)
	}
	// Size change in a continuation header is rejected.
	p = NewProtocol(bytes.NewBuffer([]byte{0x05, 0, 0, 0, 0, 0, 3, byte(MessageTypeAudio), 1, 0, 0, 0, 1, 0x45, 0, 0, 1, 0, 0, 4, byte(MessageTypeAudio)})).(*protocol)
	p.input.opt.chunkSize = 1
	if _, err := p.ReadMessage(ctx); err == nil || !strings.Contains(err.Error(), "message size") {
		t.Fatalf("size change err=%v", err)
	}
	// Short payload and short extended timestamp.
	p = NewProtocol(bytes.NewBuffer([]byte{0x05, 0, 0, 0, 0, 0, 2, byte(MessageTypeAudio), 1, 0, 0, 0, 1})).(*protocol)
	if _, err := p.ReadMessage(ctx); err == nil || !strings.Contains(err.Error(), "read chunk") {
		t.Fatalf("payload err=%v", err)
	}
	p = NewProtocol(bytes.NewBuffer([]byte{0x05, 0xff, 0xff, 0xff, 0, 0, 0, byte(MessageTypeAudio), 1, 0, 0, 0, 1, 2})).(*protocol)
	if _, err := p.ReadMessage(ctx); err == nil || !strings.Contains(err.Error(), "ext-ts") {
		t.Fatalf("ext-ts err=%v", err)
	}
}

func TestWriteMessageHeadersChunkingAndErrors(t *testing.T) {
	ctx := context.Background()
	var out bytes.Buffer
	p := NewProtocol(&out).(*protocol)
	p.output.opt.chunkSize = 2
	m := NewStreamMessage(7).asMessage()
	m.messageHeader.MessageType = MessageTypeVideo
	m.messageHeader.Timestamp = extendedTimestamp + 9
	m.payload = []byte{1, 2, 3, 4, 5}
	if err := p.WriteMessage(ctx, m); err != nil {
		t.Fatalf("WriteMessage err=%v", err)
	}
	want := []byte{0x05, 0xff, 0xff, 0xff, 0, 0, 5, byte(MessageTypeVideo), 7, 0, 0, 0, 0x01, 0x00, 0x00, 0x08, 1, 2, 0xc5, 0x01, 0x00, 0x00, 0x08, 3, 4, 0xc5, 0x01, 0x00, 0x00, 0x08, 5}
	if !bytes.Equal(out.Bytes(), want) {
		t.Fatalf("written=%x want=%x", out.Bytes(), want)
	}
	if err := p.WriteMessage(ctx, (&message{})); err != nil {
		t.Fatalf("empty WriteMessage err=%v", err)
	}
	canceled, cancel := context.WithCancel(ctx)
	cancel()
	if err := p.WriteMessage(canceled, m); err != context.Canceled {
		t.Fatalf("canceled WriteMessage err=%v", err)
	}
	p = NewProtocol(struct {
		io.Reader
		io.Writer
	}{bytes.NewReader(nil), errWriter{}}).(*protocol)
	if err := p.WriteMessage(ctx, m); err == nil {
		t.Fatal("WriteMessage to bad writer should fail")
	}
}

func TestProtocolDecodeMessageAndControls(t *testing.T) {
	ctx := context.Background()
	p := NewProtocol(&bytes.Buffer{}).(*protocol)
	if _, err := p.DecodeMessage((&message{})); err == nil || !strings.Contains(err.Error(), "Empty packet") {
		t.Fatalf("empty decode err=%v", err)
	}
	unknown := &message{}
	unknown.messageHeader.MessageType = MessageTypeAudio
	unknown.payload = []byte{1}
	if _, err := p.DecodeMessage(unknown); err == nil || !strings.Contains(err.Error(), "Unknown message") {
		t.Fatalf("unknown err=%v", err)
	}
	bad := &message{}
	bad.messageHeader.MessageType = MessageTypeSetChunkSize
	bad.payload = []byte{1, 2}
	if _, err := p.DecodeMessage(bad); err == nil || !strings.Contains(err.Error(), "Unmarshal") {
		t.Fatalf("bad control err=%v", err)
	}

	for _, pkt := range []Packet{
		&SetChunkSize{ChunkSize: 4096},
		&WindowAcknowledgementSize{AckSize: 2500000},
		&SetPeerBandwidth{Bandwidth: 1000, LimitType: LimitTypeSoft},
		&UserControl{EventType: EventTypePingRequest, EventData: 123},
	} {
		data, err := pkt.MarshalBinary()
		if err != nil {
			t.Fatalf("marshal %T err=%v", pkt, err)
		}
		m := &message{payload: data}
		m.messageHeader.MessageType = pkt.Type()
		got, err := p.DecodeMessage(m)
		if err != nil {
			t.Fatalf("DecodeMessage %T err=%v", pkt, err)
		}
		if reflect.TypeOf(got) != reflect.TypeOf(pkt) {
			t.Fatalf("got %T want %T", got, pkt)
		}
	}

	chunk := &SetChunkSize{ChunkSize: 3}
	m := &message{}
	m.messageHeader.MessageType = chunk.Type()
	m.payload, _ = chunk.MarshalBinary()
	if err := p.onMessageArrivated(m); err != nil || p.input.opt.chunkSize != 3 {
		t.Fatalf("onMessageArrivated err=%v chunk=%v", err, p.input.opt.chunkSize)
	}
	if err := p.onMessageArrivated(nil); err != nil {
		t.Fatalf("nil onMessageArrivated err=%v", err)
	}
	bad.Payload()[0] = 1
	if err := p.onMessageArrivated(bad); err == nil {
		t.Fatal("bad onMessageArrivated should fail")
	}
	if _, err := p.ExpectMessage(ctx); err == nil {
		t.Fatal("ExpectMessage on empty reader should fail")
	}
}

func TestProtocolPacketsAndTransactions(t *testing.T) {
	ctx := context.Background()
	var wire bytes.Buffer
	writer := NewProtocol(&wire).(*protocol)
	connect := NewConnectAppPacket()
	connect.CommandObject.Set("tcUrl", NewAmf0String("rtmp://host/live"))
	if connect.Size() == 0 || connect.BetterCid() != chunkIDOverConnection || connect.Type() != MessageTypeAMF0Command || connect.TcUrl() == "" {
		t.Fatalf("connect metadata invalid")
	}
	if err := writer.WritePacket(ctx, connect, 0); err != nil {
		t.Fatalf("WritePacket connect err=%v", err)
	}
	if _, ok := writer.input.transactions[connect.TransactionID]; !ok {
		t.Fatal("connect transaction not tracked")
	}
	create := NewCreateStreamPacket()
	if err := writer.WritePacket(ctx, create, 0); err != nil {
		t.Fatalf("WritePacket create err=%v", err)
	}
	call := NewCallPacket()
	call.CommandName = commandReleaseStream
	call.TransactionID = 3
	call.CommandObject = NewAmf0Null()
	if err := writer.WritePacket(ctx, call, 0); err != nil {
		t.Fatalf("WritePacket call err=%v", err)
	}
	reader := NewProtocol(&wire)
	var gotConnect *ConnectAppPacket
	if _, err := ExpectPacket(ctx, reader, &gotConnect); err != nil || gotConnect.TcUrl() != "rtmp://host/live" {
		t.Fatalf("gotConnect=%v err=%v", gotConnect, err)
	}
	var gotCreate *CallPacket
	if _, err := ExpectPacket(ctx, reader, &gotCreate); err != nil || gotCreate.CommandName != commandCreateStream {
		t.Fatalf("gotCreate=%v err=%v", gotCreate, err)
	}

	decoder := NewProtocol(&bytes.Buffer{}).(*protocol)
	decoder.input.transactions[1] = commandConnect
	if pkt, err := decoder.parseAMFObject(mustPacketBytes(t, NewConnectAppResPacket(1))); err != nil || reflect.TypeOf(pkt) != reflect.TypeOf(NewConnectAppResPacket(1)) {
		t.Fatalf("connect res pkt=%T err=%v", pkt, err)
	}
	decoder.input.transactions[2] = commandCreateStream
	csr := NewCreateStreamResPacket(2)
	csr.SetStreamID(99)
	if pkt, err := decoder.parseAMFObject(mustPacketBytes(t, csr)); err != nil || reflect.TypeOf(pkt) != reflect.TypeOf(csr) {
		t.Fatalf("create res pkt=%T err=%v", pkt, err)
	}
	decoder.input.transactions[3] = commandReleaseStream
	res := NewCallPacket()
	res.CommandName = commandResult
	res.TransactionID = 3
	res.CommandObject = NewAmf0Null()
	if pkt, err := decoder.parseAMFObject(mustPacketBytes(t, res)); err != nil || reflect.TypeOf(pkt) != reflect.TypeOf(res) {
		t.Fatalf("call res pkt=%T err=%v", pkt, err)
	}
	for _, name := range []amf0String{commandPublish, commandPlay, commandOnStatus} {
		pkt := NewCallPacket()
		pkt.CommandName = name
		pkt.TransactionID = 0
		pkt.CommandObject = NewAmf0Null()
		if name == commandPublish {
			pub := NewPublishPacket()
			pub.TransactionID = 0
			pub.StreamName = NewAmf0String("s")
			pub.StreamType = NewAmf0String("live")
			if decoded, err := decoder.parseAMFObject(mustPacketBytes(t, pub)); err != nil || reflect.TypeOf(decoded) != reflect.TypeOf(pub) {
				t.Fatalf("publish decoded=%T err=%v", decoded, err)
			}
			continue
		}
		if name == commandPlay {
			play := NewPlayPacket()
			play.TransactionID = 0
			play.StreamName = NewAmf0String("s")
			if decoded, err := decoder.parseAMFObject(mustPacketBytes(t, play)); err != nil || reflect.TypeOf(decoded) != reflect.TypeOf(play) {
				t.Fatalf("play decoded=%T err=%v", decoded, err)
			}
			continue
		}
		if decoded, err := decoder.parseAMFObject(mustPacketBytes(t, pkt)); err != nil || reflect.TypeOf(decoded) != reflect.TypeOf(pkt) {
			t.Fatalf("call decoded=%T err=%v", decoded, err)
		}
	}
	decoder.input.transactions[9] = commandPause
	errPkt := NewCallPacket()
	errPkt.CommandName = commandError
	errPkt.TransactionID = 9
	errPkt.CommandObject = NewAmf0Null()
	if _, err := decoder.parseAMFObject(mustPacketBytes(t, errPkt)); err == nil || !strings.Contains(err.Error(), "No request") {
		t.Fatalf("unknown request err=%v", err)
	}
	if _, err := decoder.parseAMFObject(mustPacketBytes(t, errPkt)); err == nil || !strings.Contains(err.Error(), "No matched request") {
		t.Fatalf("missing transaction err=%v", err)
	}
	if _, err := decoder.parseAMFObject([]byte{byte(amf0MarkerString), 0, 8, 'c'}); err == nil {
		t.Fatal("bad AMF parse should fail")
	}

	cctx, cancel := context.WithCancel(ctx)
	cancel()
	if err := writer.WritePacket(cctx, connect, 0); err == nil || !strings.Contains(err.Error(), context.Canceled.Error()) {
		t.Fatalf("WritePacket canceled err=%v", err)
	}
}

func TestDeprecatedExpectPacketPanics(t *testing.T) {
	defer func() {
		if recover() == nil {
			t.Fatal("Expected panic")
		}
	}()
	NewProtocol(&bytes.Buffer{}).ExpectPacket(context.Background(), nil)
}

func TestPacketRoundTripsAndErrors(t *testing.T) {
	packets := []Packet{
		NewConnectAppPacket(),
		NewConnectAppResPacket(7),
		NewCallPacket(),
		NewCreateStreamPacket(),
		func() Packet { p := NewCreateStreamResPacket(2); p.SetStreamID(1); return p }(),
		func() Packet {
			p := NewPublishPacket()
			p.TransactionID = 0
			p.StreamName = NewAmf0String("s")
			return p
		}(),
		func() Packet { p := NewPlayPacket(); p.TransactionID = 0; p.StreamName = NewAmf0String("s"); return p }(),
		&SetChunkSize{ChunkSize: 1},
		&WindowAcknowledgementSize{AckSize: 2},
		&SetPeerBandwidth{Bandwidth: 3, LimitType: LimitTypeDynamic},
		&UserControl{EventType: EventTypeFmsEvent0, EventData: 1},
		&UserControl{EventType: EventTypeSetBufferLength, EventData: 1, ExtraData: 2},
	}
	// Initialize the generic call packet so it is marshalable.
	packets[2].(*CallPacket).CommandName = commandOnStatus
	packets[2].(*CallPacket).TransactionID = 0
	packets[2].(*CallPacket).CommandObject = NewAmf0Null()
	packets[2].(*CallPacket).Args = NewAmf0Object().Set("code", NewAmf0String("ok"))
	packets[1].(*ConnectAppResPacket).Args.Set("data", NewAmf0EcmaArray().Set("srs_id", NewAmf0String("sid")))

	for _, pkt := range packets {
		t.Run(reflect.TypeOf(pkt).String(), func(t *testing.T) {
			data, err := pkt.MarshalBinary()
			if err != nil {
				t.Fatalf("MarshalBinary err=%v", err)
			}
			if len(data) != pkt.Size() {
				t.Fatalf("len=%v Size=%v", len(data), pkt.Size())
			}
			fresh := reflect.New(reflect.TypeOf(pkt).Elem()).Interface().(Packet)
			switch v := fresh.(type) {
			case *ConnectAppPacket:
				*v = *NewConnectAppPacket()
			case *ConnectAppResPacket:
				*v = *NewConnectAppResPacket(0)
			case *CreateStreamPacket:
				*v = *NewCreateStreamPacket()
			case *CreateStreamResPacket:
				*v = *NewCreateStreamResPacket(0)
			case *PublishPacket:
				*v = *NewPublishPacket()
			case *PlayPacket:
				*v = *NewPlayPacket()
			}
			if err := fresh.UnmarshalBinary(data); err != nil {
				t.Fatalf("UnmarshalBinary err=%v", err)
			}
		})
	}
	if packets[1].(*ConnectAppResPacket).SrsID() != "sid" || packets[2].(*CallPacket).ArgsCode() != "ok" {
		t.Fatalf("packet helpers failed")
	}
	if NewConnectAppResPacket(1).SrsID() != "" || NewCallPacket().ArgsCode() != "" || NewConnectAppPacket().TcUrl() != "" {
		t.Fatalf("empty helpers failed")
	}

	badConnect := NewConnectAppPacket()
	badConnect.CommandName = commandPlay
	if err := badConnect.UnmarshalBinary(mustPacketBytes(t, badConnect)); err == nil {
		t.Fatal("bad connect name should fail")
	}
	badConnect = NewConnectAppPacket()
	badConnect.TransactionID = 2
	if err := badConnect.UnmarshalBinary(mustPacketBytes(t, badConnect)); err == nil {
		t.Fatal("bad connect tid should fail")
	}
	badRes := NewConnectAppResPacket(1)
	badRes.CommandName = commandPlay
	if err := badRes.UnmarshalBinary(mustPacketBytes(t, badRes)); err == nil {
		t.Fatal("bad connect response name should fail")
	}
	for _, pkt := range []Packet{NewConnectAppPacket(), NewCallPacket(), NewCreateStreamResPacket(1), NewPublishPacket(), NewPlayPacket()} {
		if err := pkt.UnmarshalBinary([]byte{byte(amf0MarkerString)}); err == nil {
			t.Fatalf("%T short unmarshal should fail", pkt)
		}
	}
	for _, pkt := range []Packet{&SetChunkSize{}, &WindowAcknowledgementSize{}, &SetPeerBandwidth{}, &UserControl{}} {
		if err := pkt.UnmarshalBinary([]byte{0, 1}); err == nil {
			t.Fatalf("%T short unmarshal should fail", pkt)
		}
	}
	uc := &UserControl{}
	if err := uc.UnmarshalBinary([]byte{0, byte(EventTypeSetBufferLength), 1, 2, 3, 4, 5}); err == nil {
		t.Fatal("short set-buffer-length should fail")
	}
}

func mustPacketBytes(t *testing.T, pkt Packet) []byte {
	t.Helper()
	data, err := pkt.MarshalBinary()
	if err != nil {
		t.Fatalf("MarshalBinary %T err=%v", pkt, err)
	}
	return data
}

type failPacket struct{}

func (failPacket) Size() int                      { return 0 }
func (failPacket) UnmarshalBinary([]byte) error   { return io.ErrUnexpectedEOF }
func (failPacket) MarshalBinary() ([]byte, error) { return nil, io.ErrClosedPipe }
func (failPacket) BetterCid() chunkID             { return chunkIDOverConnection }
func (failPacket) Type() MessageType              { return MessageTypeAMF0Command }

type stepWriter struct {
	writes int
	failAt int
}

func (w *stepWriter) Write(p []byte) (int, error) {
	w.writes++
	if w.writes == w.failAt {
		return 0, io.ErrClosedPipe
	}
	return len(p), nil
}

func TestProtocolAdditionalBranches(t *testing.T) {
	ctx := context.Background()
	p := NewProtocol(&bytes.Buffer{}).(*protocol)
	if NewSetPeerBandwidth().BetterCid() != chunkIDProtocolControl || NewUserControl().BetterCid() != chunkIDProtocolControl {
		t.Fatal("control better cid failed")
	}
	if err := p.WritePacket(ctx, failPacket{}, 0); err == nil || !strings.Contains(err.Error(), "marshal payload") {
		t.Fatalf("WritePacket marshal err=%v", err)
	}

	payloadWriter := &stepWriter{failAt: 1}
	p = NewProtocol(struct {
		io.Reader
		io.Writer
	}{bytes.NewReader(nil), payloadWriter}).(*protocol)
	m := NewStreamMessage(1).asMessage()
	m.messageHeader.MessageType = MessageTypeVideo
	m.payload = bytes.Repeat([]byte{1}, 5000)
	if err := p.WriteMessage(ctx, m); err == nil || !strings.Contains(err.Error(), "write chunk payload") {
		t.Fatalf("WriteMessage payload err=%v", err)
	}
	flushWriter := &stepWriter{failAt: 1}
	p = NewProtocol(struct {
		io.Reader
		io.Writer
	}{bytes.NewReader(nil), flushWriter}).(*protocol)
	m.payload = []byte{1}
	if err := p.WriteMessage(ctx, m); err == nil || !strings.Contains(err.Error(), "flush writer") {
		t.Fatalf("WriteMessage flush err=%v writes=%v", err, flushWriter.writes)
	}

	// Zero-length payload returns a complete message without reading chunk bytes.
	in := bytes.NewBuffer([]byte{0x05, 0, 0, 1, 0, 0, 0, byte(MessageTypeAudio), 1, 0, 0, 0})
	p = NewProtocol(in).(*protocol)
	if msg, err := p.ReadMessage(ctx); err != nil || msg.MessageType() != MessageTypeAudio || len(msg.Payload()) != 0 {
		t.Fatalf("zero payload msg=%v err=%v", msg, err)
	}

	// ExpectMessage skips unwanted message types before returning the desired one.
	var wire bytes.Buffer
	writer := NewProtocol(&wire).(*protocol)
	am := NewStreamMessage(1).asMessage()
	am.messageHeader.MessageType = MessageTypeAudio
	am.payload = []byte{1}
	vm := NewStreamMessage(1).asMessage()
	vm.messageHeader.MessageType = MessageTypeVideo
	vm.payload = []byte{2}
	if err := writer.WriteMessage(ctx, am); err != nil {
		t.Fatal(err)
	}
	if err := writer.WriteMessage(ctx, vm); err != nil {
		t.Fatal(err)
	}
	reader := NewProtocol(&wire)
	if got, err := reader.ExpectMessage(ctx, MessageTypeVideo); err != nil || got.MessageType() != MessageTypeVideo {
		t.Fatalf("ExpectMessage got=%v err=%v", got, err)
	}

	// Generic ExpectPacket skips non-matching packets, then returns matching; it also reports decode/read errors.
	wire.Reset()
	writer = NewProtocol(&wire).(*protocol)
	if err := writer.WritePacket(ctx, &WindowAcknowledgementSize{AckSize: 1}, 0); err != nil {
		t.Fatal(err)
	}
	if err := writer.WritePacket(ctx, &SetChunkSize{ChunkSize: 2}, 0); err != nil {
		t.Fatal(err)
	}
	reader = NewProtocol(&wire)
	var chunk *SetChunkSize
	if _, err := ExpectPacket(ctx, reader, &chunk); err != nil || chunk.ChunkSize != 2 {
		t.Fatalf("ExpectPacket chunk=%v err=%v", chunk, err)
	}
	reader = NewProtocol(bytes.NewBuffer([]byte{0x05, 0, 0, 0, 0, 0, 1, byte(MessageTypeSetChunkSize), 1, 0, 0, 0, 0}))
	if _, err := ExpectPacket(ctx, reader, &chunk); err == nil || !strings.Contains(err.Error(), "decode message") {
		t.Fatalf("ExpectPacket decode err=%v", err)
	}
	reader = NewProtocol(bytes.NewBuffer(nil))
	if _, err := ExpectPacket(ctx, reader, &chunk); err == nil || !strings.Contains(err.Error(), "read message") {
		t.Fatalf("ExpectPacket read err=%v", err)
	}

	// AMF3 strips the leading byte before AMF0 decoding.
	pub := NewPublishPacket()
	pub.TransactionID = 0
	pub.StreamName = NewAmf0String("stream")
	data := append([]byte{0}, mustPacketBytes(t, pub)...)
	msg := &message{payload: data}
	msg.messageHeader.MessageType = MessageTypeAMF3Command
	if pkt, err := NewProtocol(&bytes.Buffer{}).DecodeMessage(msg); err != nil || pkt.(*PublishPacket).StreamName.String() != "stream" {
		t.Fatalf("AMF3 decode pkt=%T err=%v", pkt, err)
	}
}

func TestProtocolErrorBranchesForCoverage(t *testing.T) {
	ctx := context.Background()
	// ExpectMessage without requested types returns the first message.
	var wire bytes.Buffer
	w := NewProtocol(&wire).(*protocol)
	msg := NewStreamMessage(1).asMessage()
	msg.messageHeader.MessageType = MessageTypeAudio
	msg.payload = []byte{1}
	if err := w.WriteMessage(ctx, msg); err != nil {
		t.Fatal(err)
	}
	if got, err := NewProtocol(&wire).ExpectMessage(ctx); err != nil || got.MessageType() != MessageTypeAudio {
		t.Fatalf("ExpectMessage any got=%v err=%v", got, err)
	}
	cctx, cancel := context.WithCancel(ctx)
	cancel()
	if _, err := NewProtocol(bytes.NewBuffer(nil)).ReadMessage(cctx); err != context.Canceled {
		t.Fatalf("ReadMessage canceled err=%v", err)
	}
	if err := w.WriteMessage(cctx, (&message{})); err != context.Canceled {
		t.Fatalf("WriteMessage empty canceled err=%v", err)
	}
	badAMF := &message{payload: []byte{0xff}}
	badAMF.messageHeader.MessageType = MessageTypeAMF0Command
	if _, err := NewProtocol(&bytes.Buffer{}).DecodeMessage(badAMF); err == nil || !strings.Contains(err.Error(), "Parse AMF") {
		t.Fatalf("bad AMF decode err=%v", err)
	}
	rn := commandResult
	resultName, _ := (&rn).MarshalBinary()
	if _, err := NewProtocol(&bytes.Buffer{}).(*protocol).parseAMFObject(append(resultName, 0)); err == nil || !strings.Contains(err.Error(), "unmarshal tid") {
		t.Fatalf("bad result tid err=%v", err)
	}
}

func TestPacketUnmarshalErrorBranchesForCoverage(t *testing.T) {
	cn := commandConnect
	name, _ := (&cn).MarshalBinary()
	tn := amf0Number(1)
	tid, _ := (&tn).MarshalBinary()
	obj, _ := NewAmf0Object().MarshalBinary()
	base := append(append([]byte{}, name...), tid...)
	oc := &objectCallPacket{CommandObject: NewAmf0Object()}
	if err := oc.UnmarshalBinary(append([]byte{}, name...)); err == nil || !strings.Contains(err.Error(), "unmarshal tid") {
		t.Fatalf("object tid err=%v", err)
	}
	if err := oc.UnmarshalBinary(append(append([]byte{}, base...), 0xff)); err == nil || !strings.Contains(err.Error(), "unmarshal command") {
		t.Fatalf("object command err=%v", err)
	}
	withObj := append(append([]byte{}, base...), obj...)
	if err := oc.UnmarshalBinary(append(withObj, 0xff)); err == nil || !strings.Contains(err.Error(), "unmarshal args") {
		t.Fatalf("object args err=%v", err)
	}

	vc := &variantCallPacket{}
	if err := vc.UnmarshalBinary(append([]byte{}, name...)); err == nil || !strings.Contains(err.Error(), "unmarshal tid") {
		t.Fatalf("variant tid err=%v", err)
	}
	if err := vc.UnmarshalBinary(append(append([]byte{}, base...), 0xff)); err == nil || !strings.Contains(err.Error(), "discovery command object") {
		t.Fatalf("variant discovery err=%v", err)
	}
	if err := vc.UnmarshalBinary(append(append([]byte{}, base...), byte(amf0MarkerString), 0, 3, 'a')); err == nil || !strings.Contains(err.Error(), "unmarshal command object") {
		t.Fatalf("variant command object err=%v", err)
	}

	call := NewCallPacket()
	call.CommandName = commandOnStatus
	call.TransactionID = 0
	call.CommandObject = NewAmf0Null()
	callBase := mustPacketBytes(t, call)
	if err := NewCallPacket().UnmarshalBinary(append(callBase, 0xff)); err == nil || !strings.Contains(err.Error(), "discovery args") {
		t.Fatalf("call discovery args err=%v", err)
	}
	if err := NewCallPacket().UnmarshalBinary(append(callBase, byte(amf0MarkerString), 0, 3, 'a')); err == nil || !strings.Contains(err.Error(), "unmarshal args") {
		t.Fatalf("call unmarshal args err=%v", err)
	}
	csr := NewCreateStreamResPacket(2)
	if err := NewCreateStreamResPacket(0).UnmarshalBinary(mustPacketBytes(t, &csr.variantCallPacket)); err == nil || !strings.Contains(err.Error(), "unmarshal sid") {
		t.Fatalf("create stream sid err=%v", err)
	}
	pub := NewPublishPacket()
	pub.TransactionID = 0
	pubPrefix, _ := pub.variantCallPacket.MarshalBinary()
	if err := NewPublishPacket().UnmarshalBinary(append(pubPrefix, 0xff)); err == nil || !strings.Contains(err.Error(), "stream name") {
		t.Fatalf("publish stream name err=%v", err)
	}
	streamName, _ := NewAmf0String("s").MarshalBinary()
	if err := NewPublishPacket().UnmarshalBinary(append(append(pubPrefix, streamName...), 0xff)); err == nil || !strings.Contains(err.Error(), "stream type") {
		t.Fatalf("publish stream type err=%v", err)
	}
	play := NewPlayPacket()
	play.TransactionID = 0
	playPrefix, _ := play.variantCallPacket.MarshalBinary()
	if err := NewPlayPacket().UnmarshalBinary(append(playPrefix, 0xff)); err == nil || !strings.Contains(err.Error(), "stream name") {
		t.Fatalf("play stream name err=%v", err)
	}
}

// TestReadMessageInterleavedMultiStream covers P1: two or more chunk streams interleaved on the
// wire, each reassembling independently via protocol.input.chunks. All other read tests use a
// single cid (5), so the per-cid map and per-cid header state are never exercised under
// interleaving. Mirrors the C++ srs_utest_manual_protocol.cpp ProtocolRecvVAVMessage /
// ProtocolRecvVAVFmt1/2/3 family.
func TestReadMessageInterleavedMultiStream(t *testing.T) {
	ctx := context.Background()

	// Chunk-by-chunk interleave: three multi-chunk messages on cid 6 (video), 7 (audio) and 8
	// (data) are split at chunkSize=3 and woven together on the wire. Each message must reassemble
	// from its own cid's chunks regardless of the chunks belonging to other cids in between, and
	// surface in the order its final chunk arrives (V, then A, then D).
	t.Run("chunk-by-chunk", func(t *testing.T) {
		var in bytes.Buffer
		// V fmt0 cid=6, ts=100, len=6, video, stream=1, first 3 payload bytes.
		in.Write([]byte{0x06, 0x00, 0x00, 0x64, 0x00, 0x00, 0x06, byte(MessageTypeVideo), 0x01, 0x00, 0x00, 0x00, 0xa1, 0xa2, 0xa3})
		// A fmt0 cid=7, ts=200, len=4, audio, stream=1, first 3 payload bytes.
		in.Write([]byte{0x07, 0x00, 0x00, 0xc8, 0x00, 0x00, 0x04, byte(MessageTypeAudio), 0x01, 0x00, 0x00, 0x00, 0xb1, 0xb2, 0xb3})
		// D fmt0 cid=8, ts=300, len=5, data, stream=1, first 3 payload bytes.
		in.Write([]byte{0x08, 0x00, 0x01, 0x2c, 0x00, 0x00, 0x05, byte(MessageTypeAMF0Data), 0x01, 0x00, 0x00, 0x00, 0xc1, 0xc2, 0xc3})
		// V fmt3 cid=6 continuation, last 3 payload bytes -> V completes.
		in.Write([]byte{0xc6, 0xa4, 0xa5, 0xa6})
		// A fmt3 cid=7 continuation, last payload byte -> A completes.
		in.Write([]byte{0xc7, 0xb4})
		// D fmt3 cid=8 continuation, last 2 payload bytes -> D completes.
		in.Write([]byte{0xc8, 0xc4, 0xc5})

		p := NewProtocol(&in).(*protocol)
		p.input.opt.chunkSize = 3
		for i, want := range []struct {
			typ MessageType
			ts  uint64
			pl  []byte
		}{
			{MessageTypeVideo, 100, []byte{0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6}},
			{MessageTypeAudio, 200, []byte{0xb1, 0xb2, 0xb3, 0xb4}},
			{MessageTypeAMF0Data, 300, []byte{0xc1, 0xc2, 0xc3, 0xc4, 0xc5}},
		} {
			m, err := p.ReadMessage(ctx)
			if err != nil {
				t.Fatalf("ReadMessage #%v err=%v", i, err)
			}
			if m.MessageType() != want.typ || m.Timestamp() != want.ts || !bytes.Equal(m.Payload(), want.pl) {
				t.Fatalf("message #%v type=%v ts=%v payload=%x", i, m.MessageType(), m.Timestamp(), m.Payload())
			}
		}
	})

	// Per-cid header-state isolation: single-chunk messages alternate between the video cid (6)
	// and audio cid (7). The second and later messages on each cid use fmt1/2/3 headers, which
	// inherit timestamp delta / payload length / type from the *previous message on the same cid*.
	// An interleaved message on the other cid must not perturb that state, so the video deltas
	// accumulate only over video (1000 -> 1010 -> 1015) and audio only over audio
	// (5000 -> 5020 -> 5040).
	t.Run("per-cid-header-state", func(t *testing.T) {
		var in bytes.Buffer
		// V1 fmt0 cid=6, ts=1000, len=2, video, stream=1.
		in.Write([]byte{0x06, 0x00, 0x03, 0xe8, 0x00, 0x00, 0x02, byte(MessageTypeVideo), 0x01, 0x00, 0x00, 0x00, 0x11, 0x12})
		// A1 fmt0 cid=7, ts=5000, len=2, audio, stream=1.
		in.Write([]byte{0x07, 0x00, 0x13, 0x88, 0x00, 0x00, 0x02, byte(MessageTypeAudio), 0x01, 0x00, 0x00, 0x00, 0x21, 0x22})
		// V2 fmt1 cid=6, delta=10, len=2, video -> ts 1000+10=1010 (inherits from V1, not A1).
		in.Write([]byte{0x46, 0x00, 0x00, 0x0a, 0x00, 0x00, 0x02, byte(MessageTypeVideo), 0x13, 0x14})
		// A2 fmt1 cid=7, delta=20, len=2, audio -> ts 5000+20=5020 (inherits from A1).
		in.Write([]byte{0x47, 0x00, 0x00, 0x14, 0x00, 0x00, 0x02, byte(MessageTypeAudio), 0x23, 0x24})
		// V3 fmt2 cid=6, delta=5 (len/type reused from V2) -> ts 1010+5=1015.
		in.Write([]byte{0x86, 0x00, 0x00, 0x05, 0x15, 0x16})
		// A3 fmt3 cid=7 (delta=20, len/type reused from A2) -> ts 5020+20=5040.
		in.Write([]byte{0xc7, 0x25, 0x26})

		p := NewProtocol(&in).(*protocol)
		for i, want := range []struct {
			typ MessageType
			ts  uint64
			pl  []byte
		}{
			{MessageTypeVideo, 1000, []byte{0x11, 0x12}},
			{MessageTypeAudio, 5000, []byte{0x21, 0x22}},
			{MessageTypeVideo, 1010, []byte{0x13, 0x14}},
			{MessageTypeAudio, 5020, []byte{0x23, 0x24}},
			{MessageTypeVideo, 1015, []byte{0x15, 0x16}},
			{MessageTypeAudio, 5040, []byte{0x25, 0x26}},
		} {
			m, err := p.ReadMessage(ctx)
			if err != nil {
				t.Fatalf("ReadMessage #%v err=%v", i, err)
			}
			if m.MessageType() != want.typ || m.Timestamp() != want.ts || !bytes.Equal(m.Payload(), want.pl) {
				t.Fatalf("message #%v type=%v ts=%v payload=%x", i, m.MessageType(), m.Timestamp(), m.Payload())
			}
		}
	})

	// Per-cid payload-length change: successive messages on the video cid (6) carry *different*
	// payload lengths via fmt1 headers (2 -> 3 -> 1), while audio (cid 7) is interleaved in
	// between. fmt1 begins a new message (chunk.message is nil), so the length check is skipped
	// and the chunkStream must adopt each new length rather than reusing the previous one. Mirrors
	// the C++ srs_utest_manual_protocol2.cpp ProtocolRecvVAVVFmt11Length / Fmt12Length cases.
	t.Run("per-cid-length-change", func(t *testing.T) {
		var in bytes.Buffer
		// V1 fmt0 cid=6, ts=0x10, len=2, video, stream=1.
		in.Write([]byte{0x06, 0x00, 0x00, 0x10, 0x00, 0x00, 0x02, byte(MessageTypeVideo), 0x01, 0x00, 0x00, 0x00, 0x11, 0x12})
		// A1 fmt0 cid=7, ts=0x15, len=2, audio, stream=1.
		in.Write([]byte{0x07, 0x00, 0x00, 0x15, 0x00, 0x00, 0x02, byte(MessageTypeAudio), 0x01, 0x00, 0x00, 0x00, 0x21, 0x22})
		// V2 fmt1 cid=6, delta=0x10, len=3 (changed 2->3), video -> ts 0x20.
		in.Write([]byte{0x46, 0x00, 0x00, 0x10, 0x00, 0x00, 0x03, byte(MessageTypeVideo), 0x13, 0x14, 0x15})
		// V3 fmt1 cid=6, delta=0x20, len=1 (changed 3->1), video -> ts 0x40.
		in.Write([]byte{0x46, 0x00, 0x00, 0x20, 0x00, 0x00, 0x01, byte(MessageTypeVideo), 0x16})
		// A2 fmt1 cid=7, delta=0x05, len=4 (changed 2->4), audio -> ts 0x1a.
		in.Write([]byte{0x47, 0x00, 0x00, 0x05, 0x00, 0x00, 0x04, byte(MessageTypeAudio), 0x23, 0x24, 0x25, 0x26})

		p := NewProtocol(&in).(*protocol)
		for i, want := range []struct {
			typ MessageType
			ts  uint64
			pl  []byte
		}{
			{MessageTypeVideo, 0x10, []byte{0x11, 0x12}},
			{MessageTypeAudio, 0x15, []byte{0x21, 0x22}},
			{MessageTypeVideo, 0x20, []byte{0x13, 0x14, 0x15}},
			{MessageTypeVideo, 0x40, []byte{0x16}},
			{MessageTypeAudio, 0x1a, []byte{0x23, 0x24, 0x25, 0x26}},
		} {
			m, err := p.ReadMessage(ctx)
			if err != nil {
				t.Fatalf("ReadMessage #%v err=%v", i, err)
			}
			if m.MessageType() != want.typ || m.Timestamp() != want.ts || !bytes.Equal(m.Payload(), want.pl) {
				t.Fatalf("message #%v type=%v ts=%v payload=%x", i, m.MessageType(), m.Timestamp(), m.Payload())
			}
		}
	})
}

// TestReadMessageLargeChunkStreamID covers P2: reading a complete message (basic header + message
// header + payload) whose chunks carry the chunk-stream ID in the 2-byte (cid 64-319) and 3-byte
// (cid 64-65599) basic-header forms. Every other read test uses a 1-byte cid (5), so
// readBasicHeader's multi-byte cid decode is exercised for header decode only
// (TestBasicHeaderVariantsAndErrors) and never end-to-end through ReadMessage carrying a real
// payload. Asserting the decoded cid keys input.chunks also proves the encode/decode are inverses
// (a swapped 2nd/3rd byte would land on a different cid). Mirrors the C++
// srs_utest_manual_protocol.cpp / protocol2.cpp ProtocolRecvVCid2B* / Cid3B* family.
func TestReadMessageLargeChunkStreamID(t *testing.T) {
	ctx := context.Background()

	// basicHeader encodes fmt+cid in the smallest basic-header form that fits cid: 1 byte for
	// 2-63, 2 bytes for 64-319, 3 bytes for 320-65599. The ID math is the inverse of
	// readBasicHeader: 2-byte cid = 64 + b1; 3-byte cid = 64 + b1 + b2*256.
	basicHeader := func(format formatType, cid chunkID) []byte {
		f := byte(format) << 6
		switch {
		case cid <= 63:
			return []byte{f | byte(cid)}
		case cid <= 319:
			return []byte{f, byte(cid - 64)} // 2-byte marker is 0 in the low 6 bits
		default:
			v := uint32(cid) - 64
			return []byte{f | 0x01, byte(v % 256), byte(v / 256)}
		}
	}

	// Single-chunk fmt0 message on each boundary cid: 2-byte min (64) and max (319), 3-byte first
	// value (320) and max (65599). The cid must decode to the right value (which keys input.chunks)
	// and the payload must reassemble.
	t.Run("single-chunk-boundaries", func(t *testing.T) {
		for _, cid := range []chunkID{64, 319, 320, 65599} {
			var in bytes.Buffer
			in.Write(basicHeader(formatType0, cid))
			// fmt0 message header: ts=0x40, len=3, video, stream=1, then payload a1a2a3.
			in.Write([]byte{0x00, 0x00, 0x40, 0x00, 0x00, 0x03, byte(MessageTypeVideo), 0x01, 0x00, 0x00, 0x00})
			in.Write([]byte{0xa1, 0xa2, 0xa3})

			p := NewProtocol(&in).(*protocol)
			m, err := p.ReadMessage(ctx)
			if err != nil {
				t.Fatalf("cid=%v ReadMessage err=%v", cid, err)
			}
			if m.MessageType() != MessageTypeVideo || m.Timestamp() != 0x40 || !bytes.Equal(m.Payload(), []byte{0xa1, 0xa2, 0xa3}) {
				t.Fatalf("cid=%v type=%v ts=%v payload=%x", cid, m.MessageType(), m.Timestamp(), m.Payload())
			}
			if _, ok := p.input.chunks[cid]; !ok {
				t.Fatalf("cid=%v not keyed in chunks map: %v", cid, p.input.chunks)
			}
		}
	})

	// Multi-chunk message on a 3-byte cid: the fmt3 continuation must re-encode the same large cid
	// in its basic header, so readBasicHeader is invoked again mid-message and must resolve to the
	// same chunkStream for the payload to reassemble.
	t.Run("multi-chunk-large-cid", func(t *testing.T) {
		const cid chunkID = 65599
		var in bytes.Buffer
		// fmt0 on the 3-byte cid: ts=0x50, len=5, video, stream=1, first 3 payload bytes.
		in.Write(basicHeader(formatType0, cid))
		in.Write([]byte{0x00, 0x00, 0x50, 0x00, 0x00, 0x05, byte(MessageTypeVideo), 0x01, 0x00, 0x00, 0x00})
		in.Write([]byte{0xb1, 0xb2, 0xb3})
		// fmt3 continuation, same 3-byte cid re-encoded, last 2 payload bytes -> message completes.
		in.Write(basicHeader(formatType3, cid))
		in.Write([]byte{0xb4, 0xb5})

		p := NewProtocol(&in).(*protocol)
		p.input.opt.chunkSize = 3
		m, err := p.ReadMessage(ctx)
		if err != nil {
			t.Fatalf("ReadMessage err=%v", err)
		}
		if m.Timestamp() != 0x50 || !bytes.Equal(m.Payload(), []byte{0xb1, 0xb2, 0xb3, 0xb4, 0xb5}) {
			t.Fatalf("ts=%v payload=%x", m.Timestamp(), m.Payload())
		}
	})

	// The spec allows cid 64-319 in either the 2-byte or 3-byte form, and both must decode to the
	// same cid. Read cid 64 first via its 2-byte form, then a second message via the (non-minimal)
	// 3-byte form, and confirm both land on a single chunk stream so fmt1's delta accumulates over
	// the first message (0x10 -> 0x15) rather than starting a second stream.
	t.Run("cid-64-both-forms", func(t *testing.T) {
		const cid chunkID = 64
		var in bytes.Buffer
		// 2-byte form fmt0: ts=0x10, len=1, video, stream=1, payload c1.
		in.Write([]byte{0x00, 0x00}) // fmt0, 2-byte cid 64
		in.Write([]byte{0x00, 0x00, 0x10, 0x00, 0x00, 0x01, byte(MessageTypeVideo), 0x01, 0x00, 0x00, 0x00, 0xc1})
		// 3-byte form fmt1 on the same cid: delta=5, len=1, video, payload c2 -> ts 0x10+5=0x15.
		in.Write([]byte{0x41, 0x00, 0x00}) // fmt1, 3-byte cid 64
		in.Write([]byte{0x00, 0x00, 0x05, 0x00, 0x00, 0x01, byte(MessageTypeVideo), 0xc2})

		p := NewProtocol(&in).(*protocol)
		for i, want := range []struct {
			ts uint64
			pl []byte
		}{
			{0x10, []byte{0xc1}},
			{0x15, []byte{0xc2}},
		} {
			m, err := p.ReadMessage(ctx)
			if err != nil {
				t.Fatalf("message #%v ReadMessage err=%v", i, err)
			}
			if m.Timestamp() != want.ts || !bytes.Equal(m.Payload(), want.pl) {
				t.Fatalf("message #%v ts=%v payload=%x", i, m.Timestamp(), m.Payload())
			}
		}
		if _, ok := p.input.chunks[cid]; !ok || len(p.input.chunks) != 1 {
			t.Fatalf("both forms must share one chunk stream at cid %v: %v", cid, p.input.chunks)
		}
	})
}

// TestProtocolWritePacketReadMessageRoundTrip covers P3: every Packet type round-tripped through
// the full chunk-stream layer at a representative span of chunk sizes — 1 (every payload byte in
// its own chunk, the extreme case), 2 (small), 128 (default), and a value larger than any payload
// here (never chunks). For each (packet, chunk size) pair the packet is written via WritePacket,
// read back via ReadMessage, and decoded via DecodeMessage; the decoded packet must have the
// expected concrete type and re-marshal to the same wire bytes as the original. This proves the
// Go encoder and decoder agree end-to-end. The existing TestPacketRoundTripsAndErrors only checks
// MarshalBinary↔UnmarshalBinary in isolation (no chunk layer), and TestProtocolPacketsAndTransactions
// covers only a handful of packets at the default chunk size — so neither exercises the cross
// product of every typed packet against multi-chunk reassembly. Mirrors the C++
// srs_utest_manual_protocol.cpp ProtocolSendSrs*Packet family and
// srs_utest_manual_rtmp.cpp ProtocolRTMPTest.DecodeMessages / OnDecodeMessages family.
//
// Wire-byte equivalence is the equality check rather than reflect.DeepEqual: AMF0 packets carry an
// amf0ObjectBase.bufFactory func field that is non-nil in both sides, and reflect.DeepEqual treats
// any pair of non-nil funcs as not-equal. Comparing MarshalBinary() output is also the strongest
// practical assertion for "encoder and decoder agree on the wire," which is what P3 is testing.
//
// Some packets decode to a different concrete type than what we wrote. parseAMFObject's default
// branch returns NewCallPacket() for any AMF0 command name not in its special-case switch, so
// e.g. createStream comes back as *CallPacket. The variantCallPacket marshal layout is identical
// between CreateStreamPacket and a CallPacket carrying the same fields (Args nil is skipped on
// marshal), so the wire-bytes check still holds. wantType records the expected decoded type per
// case so the asymmetry is explicit.
func TestProtocolWritePacketReadMessageRoundTrip(t *testing.T) {
	ctx := context.Background()

	// Builder functions for each packet, populated with non-default fields so the round-trip
	// exercises real values instead of zero values. Use closures so the test can rebuild a fresh
	// orig per (chunk size) iteration without state leaking between subtests.
	makeConnect := func() Packet {
		p := NewConnectAppPacket()
		p.CommandObject.Set("tcUrl", NewAmf0String("rtmp://host/live"))
		p.CommandObject.Set("app", NewAmf0String("live"))
		return p
	}
	makeConnectRes := func() Packet {
		p := NewConnectAppResPacket(1) // tid matches makeConnect's default TransactionID=1
		p.Args.Set("data", NewAmf0EcmaArray().Set("srs_id", NewAmf0String("sid")))
		return p
	}
	makeCreateStream := func() Packet { return NewCreateStreamPacket() }
	makeCreateStreamRes := func() Packet {
		p := NewCreateStreamResPacket(2) // tid=2 matches makeCreateStream's TransactionID
		p.SetStreamID(99)
		return p
	}
	makeOnStatus := func() Packet {
		p := NewCallPacket()
		p.CommandName = commandOnStatus
		p.TransactionID = 0
		p.CommandObject = NewAmf0Null()
		p.Args = NewAmf0Object().Set("level", NewAmf0String("status")).Set("code", NewAmf0String("NetStream.Play.Start"))
		return p
	}
	makeReleaseStreamRes := func() Packet {
		// _result for a releaseStream call: parseAMFObject returns NewCallPacket() with the tid
		// pre-filled. We send the response shape that the C++ side sends back to FMLE.
		p := NewCallPacket()
		p.CommandName = commandResult
		p.TransactionID = 4
		p.CommandObject = NewAmf0Null()
		return p
	}
	makePublish := func() Packet {
		p := NewPublishPacket()
		p.TransactionID = 0
		p.StreamName = NewAmf0String("livestream")
		p.StreamType = NewAmf0String("live")
		return p
	}
	makePlay := func() Packet {
		p := NewPlayPacket()
		p.TransactionID = 0
		p.StreamName = NewAmf0String("livestream")
		return p
	}

	type seedFn func(*protocol)
	seedConnect := func(p *protocol) { p.input.transactions[1] = commandConnect }
	seedCreateStream := func(p *protocol) { p.input.transactions[2] = commandCreateStream }
	seedRelease := func(p *protocol) { p.input.transactions[4] = commandReleaseStream }

	cases := []struct {
		name     string
		build    func() Packet
		wantType reflect.Type
		// seed registers a tid → request-name mapping on the reader's transactions map so that
		// parseAMFObject can resolve the concrete response type for *_result/_error packets.
		seed seedFn
	}{
		{"connect-app", makeConnect, reflect.TypeOf((*ConnectAppPacket)(nil)), nil},
		{"connect-app-res", makeConnectRes, reflect.TypeOf((*ConnectAppResPacket)(nil)), seedConnect},
		{"create-stream", makeCreateStream, reflect.TypeOf((*CallPacket)(nil)), nil},
		{"create-stream-res", makeCreateStreamRes, reflect.TypeOf((*CreateStreamResPacket)(nil)), seedCreateStream},
		{"call-onstatus", makeOnStatus, reflect.TypeOf((*CallPacket)(nil)), nil},
		{"call-result-releaseStream", makeReleaseStreamRes, reflect.TypeOf((*CallPacket)(nil)), seedRelease},
		{"publish", makePublish, reflect.TypeOf((*PublishPacket)(nil)), nil},
		{"play", makePlay, reflect.TypeOf((*PlayPacket)(nil)), nil},
		{"set-chunk-size", func() Packet { return &SetChunkSize{ChunkSize: 4096} }, reflect.TypeOf((*SetChunkSize)(nil)), nil},
		{"window-ack-size", func() Packet { return &WindowAcknowledgementSize{AckSize: 2500000} }, reflect.TypeOf((*WindowAcknowledgementSize)(nil)), nil},
		{"set-peer-bandwidth", func() Packet {
			return &SetPeerBandwidth{Bandwidth: 2500000, LimitType: LimitTypeDynamic}
		}, reflect.TypeOf((*SetPeerBandwidth)(nil)), nil},
		{"user-control-ping", func() Packet {
			return &UserControl{EventType: EventTypePingRequest, EventData: 12345}
		}, reflect.TypeOf((*UserControl)(nil)), nil},
		{"user-control-buffer-len", func() Packet {
			return &UserControl{EventType: EventTypeSetBufferLength, EventData: 1, ExtraData: 1500}
		}, reflect.TypeOf((*UserControl)(nil)), nil},
		{"user-control-fms-event0", func() Packet {
			return &UserControl{EventType: EventTypeFmsEvent0, EventData: 1}
		}, reflect.TypeOf((*UserControl)(nil)), nil},
	}

	// Chunk sizes: 1 forces every payload byte into its own chunk (maximum c3 continuations);
	// 2 is a small non-trivial chunking; 128 is the protocol default; 4096 is larger than every
	// payload in this table (the connect packet is ~60 bytes), so the message is sent as a single
	// chunk with no c3 continuations.
	chunkSizes := []uint32{1, 2, 128, 4096}

	for _, c := range cases {
		for _, chunkSize := range chunkSizes {
			t.Run(fmt.Sprintf("%s/chunk=%d", c.name, chunkSize), func(t *testing.T) {
				orig := c.build()
				origBytes, err := orig.MarshalBinary()
				if err != nil {
					t.Fatalf("MarshalBinary orig err=%v", err)
				}

				var wire bytes.Buffer
				writer := NewProtocol(&wire).(*protocol)
				writer.output.opt.chunkSize = chunkSize
				if err := writer.WritePacket(ctx, orig, 1); err != nil {
					t.Fatalf("WritePacket err=%v", err)
				}

				// Verify the writer actually chunked at this size: a payload longer than
				// chunkSize must produce at least one c3 continuation chunk on the wire. The
				// continuation header is a single byte 0xc0|cid; the basic-header byte for the
				// initial c0 chunk is 0x0X|cid (fmt=0). Counting bytes that match the c3 form is
				// a coarse but adequate check that the chunk path was actually exercised.
				if uint32(len(origBytes)) > chunkSize {
					wantContinuations := (uint32(len(origBytes))-1)/chunkSize + 1 - 1
					var got uint32
					for _, b := range wire.Bytes() {
						if b == 0xc0|byte(orig.BetterCid()) {
							got++
						}
					}
					if got < wantContinuations {
						t.Fatalf("expected >=%v c3 continuations on the wire, saw %v (wire=%x)", wantContinuations, got, wire.Bytes())
					}
				}

				reader := NewProtocol(&wire).(*protocol)
				reader.input.opt.chunkSize = chunkSize
				if c.seed != nil {
					c.seed(reader)
				}
				m, err := reader.ReadMessage(ctx)
				if err != nil {
					t.Fatalf("ReadMessage err=%v", err)
				}
				if m.MessageType() != orig.Type() {
					t.Fatalf("message type=%v want=%v", m.MessageType(), orig.Type())
				}
				got, err := reader.DecodeMessage(m)
				if err != nil {
					t.Fatalf("DecodeMessage err=%v", err)
				}
				if reflect.TypeOf(got) != c.wantType {
					t.Fatalf("decoded type=%T want=%v", got, c.wantType)
				}

				gotBytes, err := got.MarshalBinary()
				if err != nil {
					t.Fatalf("MarshalBinary got err=%v", err)
				}
				if !bytes.Equal(origBytes, gotBytes) {
					t.Fatalf("round-trip mismatch:\n  orig=%x\n  got =%x", origBytes, gotBytes)
				}
			})
		}
	}
}

// TestReadMessageTimestampDiscontinuity covers P5: timestamp continuity edges on the per-cid
// chunkStream that the existing monotonically-increasing tests don't exercise. The C++
// ProtocolStackTest suite has no equivalent coverage either, so this is new coverage beyond the
// C++ reference.
//
//  1. backward-jump-fmt0: a new fmt0 message whose absolute timestamp is *smaller* than the
//     previous message's timestamp must replace the stored Timestamp, not add to it. fmt0 is
//     absolute by the spec; fmt1/2/3 deltas are unsigned, so a real backward jump can only ride
//     on fmt0.
//  2. wraparound-31bit-mask: delta accumulation crossing the 31-bit boundary must wrap to the
//     low 31 bits via `chunk.header.Timestamp &= 0x7fffffff`. ts 0x7ffffff0 + delta 0x20 =
//     0x80000010 -> masked 0x10. Easy to regress if anyone splits the accumulate-then-mask
//     sequence.
//  3. forward-jump-fmt0: a new fmt0 message whose absolute timestamp is much *larger* than the
//     previous one (carried in the extended timestamp because the 3-byte field saturates at
//     0xffffff) must also replace, not add. Mirror image of case 1; together they prove fmt0 is
//     absolute regardless of direction.
func TestReadMessageTimestampDiscontinuity(t *testing.T) {
	ctx := context.Background()

	t.Run("backward-jump-fmt0", func(t *testing.T) {
		var in bytes.Buffer
		// M1 fmt0 cid=5, ts=2000=0x7d0, len=2, video, stream=1, payload AA BB.
		in.Write([]byte{0x05, 0x00, 0x07, 0xd0, 0x00, 0x00, 0x02, byte(MessageTypeVideo), 0x01, 0x00, 0x00, 0x00, 0xAA, 0xBB})
		// M2 fmt0 cid=5, ts=1000=0x3e8 (< 2000), len=2, video, stream=1, payload CC DD.
		// fmt0 sets the message timestamp absolutely; if it were ever accidentally accumulated,
		// M2.Timestamp would be 2000+1000=3000 instead of 1000.
		in.Write([]byte{0x05, 0x00, 0x03, 0xe8, 0x00, 0x00, 0x02, byte(MessageTypeVideo), 0x01, 0x00, 0x00, 0x00, 0xCC, 0xDD})

		p := NewProtocol(&in).(*protocol)
		for i, want := range []struct {
			ts uint64
			pl []byte
		}{
			{2000, []byte{0xAA, 0xBB}},
			{1000, []byte{0xCC, 0xDD}},
		} {
			m, err := p.ReadMessage(ctx)
			if err != nil {
				t.Fatalf("ReadMessage #%v err=%v", i, err)
			}
			if m.Timestamp() != want.ts || !bytes.Equal(m.Payload(), want.pl) {
				t.Fatalf("message #%v ts=%v payload=%x", i, m.Timestamp(), m.Payload())
			}
		}
	})

	t.Run("wraparound-31bit-mask", func(t *testing.T) {
		var in bytes.Buffer
		// M1 fmt0 cid=5, ts(3B)=0xffffff so an extended timestamp is present, len=1, video,
		// stream=1, ext-ts=0x7ffffff0 (just below the 31-bit edge), payload AA. The 31-bit mask
		// at the end of readMessageHeader leaves 0x7ffffff0 unchanged because bit 31 is clear,
		// so chunk.header.Timestamp settles at 0x7ffffff0 between messages.
		in.Write([]byte{0x05, 0xff, 0xff, 0xff, 0x00, 0x00, 0x01, byte(MessageTypeVideo), 0x01, 0x00, 0x00, 0x00})
		binary.Write(&in, binary.BigEndian, uint32(0x7ffffff0))
		in.Write([]byte{0xAA})
		// M2 fmt1 cid=5, delta=0x20 (3-byte, no ext-ts because 0x20 < 0xffffff), len=1, video,
		// payload BB. Accumulation: 0x7ffffff0 + 0x20 = 0x80000010 (bit 31 set), then the
		// `chunk.header.Timestamp &= 0x7fffffff` mask drops bit 31 -> 0x10. Drop the mask and
		// M2.Timestamp would surface as 0x80000010 instead.
		in.Write([]byte{0x45, 0x00, 0x00, 0x20, 0x00, 0x00, 0x01, byte(MessageTypeVideo), 0xBB})

		p := NewProtocol(&in).(*protocol)
		for i, want := range []struct {
			ts uint64
			pl []byte
		}{
			{0x7ffffff0, []byte{0xAA}},
			{0x10, []byte{0xBB}},
		} {
			m, err := p.ReadMessage(ctx)
			if err != nil {
				t.Fatalf("ReadMessage #%v err=%v", i, err)
			}
			if m.Timestamp() != want.ts || !bytes.Equal(m.Payload(), want.pl) {
				t.Fatalf("message #%v ts=%v payload=%x", i, m.Timestamp(), m.Payload())
			}
		}
	})

	t.Run("forward-jump-fmt0", func(t *testing.T) {
		var in bytes.Buffer
		// M1 fmt0 cid=5, ts=10, len=1, video, stream=1, payload AA.
		in.Write([]byte{0x05, 0x00, 0x00, 0x0a, 0x00, 0x00, 0x01, byte(MessageTypeVideo), 0x01, 0x00, 0x00, 0x00, 0xAA})
		// M2 fmt0 cid=5, ts(3B)=0xffffff (sentinel), len=1, video, stream=1,
		// ext-ts=0x12345678 (large forward absolute, bit 31 clear so no mask interaction),
		// payload BB. fmt0 replaces absolutely, so M2.Timestamp must be 0x12345678, not
		// 10 + 0x12345678 = 0x12345682.
		in.Write([]byte{0x05, 0xff, 0xff, 0xff, 0x00, 0x00, 0x01, byte(MessageTypeVideo), 0x01, 0x00, 0x00, 0x00})
		binary.Write(&in, binary.BigEndian, uint32(0x12345678))
		in.Write([]byte{0xBB})

		p := NewProtocol(&in).(*protocol)
		for i, want := range []struct {
			ts uint64
			pl []byte
		}{
			{10, []byte{0xAA}},
			{0x12345678, []byte{0xBB}},
		} {
			m, err := p.ReadMessage(ctx)
			if err != nil {
				t.Fatalf("ReadMessage #%v err=%v", i, err)
			}
			if m.Timestamp() != want.ts || !bytes.Equal(m.Payload(), want.pl) {
				t.Fatalf("message #%v ts=%v payload=%x", i, m.Timestamp(), m.Payload())
			}
		}
	})
}

// TestReadWriteLargePayloadChunkBoundaries covers P4: large-payload and chunk-boundary stress on
// both the write (chunking) and read (reassembly) paths. The existing tests only write a single
// 5000-byte message and never read a large multi-chunk message back, nor exercise the exact
// payload==chunkSize / payload==N*chunkSize boundaries where an off-by-one in the chunking loop
// would surface as a spurious empty trailing chunk. The 3-byte max length (0xffffff) parse is also
// pinned here; its DoS/truncation behavior is separately covered by
// TestPacketUnmarshalAdversarialInputs (P8, oversized-length-truncated).
//
// C++ reference:
//   - srs_utest_manual_rtmp.cpp     :: TEST(ProtocolRTMPTest, HugeMessages) — a 256B audio payload
//     at chunkSize=128 serializes to exactly 269 wire bytes (12B c0 header + 128 + 1B c3 + 128),
//     i.e. no empty trailing chunk.
//   - srs_utest_manual_rtmp.cpp     :: TEST(ProtocolRTMPTest, SendHugePacket) — a 1024B send.
//   - srs_utest_manual_protocol.cpp :: TEST(ProtocolStackTest, ProtocolRecvVMessage2Trunk) — read a
//     272B video message split across 3 chunks (2 c3 continuations) at chunkSize=128.
func TestReadWriteLargePayloadChunkBoundaries(t *testing.T) {
	ctx := context.Background()

	// payload-equals-chunksize: a payload of exactly chunkSize fits in a single chunk. The write
	// loop (`for len(p) > 0`) must emit zero c3 continuations — emitting an empty trailing chunk
	// for the 0 bytes "remaining" after the first full chunk would be an off-by-one bug. The exact
	// wire length (1B basic + 11B msg header + chunkSize payload) proves the single-chunk shape,
	// and the read path reassembles back to the original payload.
	t.Run("payload-equals-chunksize", func(t *testing.T) {
		const chunkSize = 128
		payload := make([]byte, chunkSize)
		for i := range payload {
			payload[i] = byte(i)
		}

		var wire bytes.Buffer
		w := NewProtocol(&wire).(*protocol)
		w.output.opt.chunkSize = chunkSize
		m := NewStreamMessage(1).asMessage()
		m.messageHeader.MessageType = MessageTypeVideo
		m.messageHeader.Timestamp = 40
		m.payload = payload
		if err := w.WriteMessage(ctx, m); err != nil {
			t.Fatalf("WriteMessage err=%v", err)
		}
		if want := 1 + 11 + chunkSize; wire.Len() != want {
			t.Fatalf("wire len=%v want=%v (a spurious empty trailing chunk would add a c3 byte)", wire.Len(), want)
		}

		r := NewProtocol(&wire).(*protocol)
		r.input.opt.chunkSize = chunkSize
		got, err := r.ReadMessage(ctx)
		if err != nil {
			t.Fatalf("ReadMessage err=%v", err)
		}
		if got.Timestamp() != 40 || !bytes.Equal(got.Payload(), payload) {
			t.Fatalf("ts=%v payloadLen=%v", got.Timestamp(), len(got.Payload()))
		}
	})

	// payload-exact-multiple: a payload that is an exact multiple of chunkSize (256 = 2*128) must
	// serialize to exactly two chunks — c0+128 then c3+128 — and NOT a third empty trailing chunk.
	// This mirrors the C++ HugeMessages golden: 269 wire bytes for a 256B payload with ts<0xffffff
	// (no extended timestamp). The continuation header at offset 140 must be the 1-byte c3 form,
	// and the message reassembles back to the original payload.
	t.Run("payload-exact-multiple", func(t *testing.T) {
		const chunkSize = 128
		payload := make([]byte, 2*chunkSize)
		for i := range payload {
			payload[i] = byte(i)
		}

		var wire bytes.Buffer
		w := NewProtocol(&wire).(*protocol)
		w.output.opt.chunkSize = chunkSize
		m := NewStreamMessage(1).asMessage()
		m.messageHeader.MessageType = MessageTypeAudio
		m.messageHeader.Timestamp = 1000
		m.payload = payload
		if err := w.WriteMessage(ctx, m); err != nil {
			t.Fatalf("WriteMessage err=%v", err)
		}
		// 12B c0 header + 128 + 1B c3 header + 128 = 269 (the C++ HugeMessages value). 270 would
		// mean an empty trailing chunk was emitted.
		if want := 1 + 11 + chunkSize + 1 + chunkSize; wire.Len() != want {
			t.Fatalf("wire len=%v want=%v", wire.Len(), want)
		}
		if got := wire.Bytes()[1+11+chunkSize]; got != 0xc0|byte(chunkIDOverStream) {
			t.Fatalf("continuation header=%#x want=%#x (1-byte c3 form)", got, 0xc0|byte(chunkIDOverStream))
		}

		r := NewProtocol(&wire).(*protocol)
		r.input.opt.chunkSize = chunkSize
		got, err := r.ReadMessage(ctx)
		if err != nil {
			t.Fatalf("ReadMessage err=%v", err)
		}
		if got.Timestamp() != 1000 || !bytes.Equal(got.Payload(), payload) {
			t.Fatalf("ts=%v payloadLen=%v", got.Timestamp(), len(got.Payload()))
		}
	})

	// read-multichunk-handbuilt: read a 300-byte video message split across 3 chunks (c0 + two c3
	// continuations) at chunkSize=128, against a hand-built wire layout rather than this package's
	// own writer output — directly mirroring the C++ ProtocolRecvVMessage2Trunk reassembly test.
	// Proves readMessagePayload accumulates across multiple c3 continuations into one message.
	t.Run("read-multichunk-handbuilt", func(t *testing.T) {
		const chunkSize = 128
		payload := make([]byte, 300)
		for i := range payload {
			payload[i] = byte(i)
		}

		var in bytes.Buffer
		// fmt0 cid=3, ts=0, len=300 (0x00012c), video, stream=0, then payload[0:128].
		in.Write([]byte{0x03, 0x00, 0x00, 0x00, 0x00, 0x01, 0x2c, byte(MessageTypeVideo), 0x00, 0x00, 0x00, 0x00})
		in.Write(payload[0:chunkSize])
		in.Write([]byte{0xc3}) // fmt3 continuation, cid=3.
		in.Write(payload[chunkSize : 2*chunkSize])
		in.Write([]byte{0xc3}) // fmt3 continuation, cid=3.
		in.Write(payload[2*chunkSize:])

		r := NewProtocol(&in).(*protocol)
		r.input.opt.chunkSize = chunkSize
		got, err := r.ReadMessage(ctx)
		if err != nil {
			t.Fatalf("ReadMessage err=%v", err)
		}
		if got.MessageType() != MessageTypeVideo || !bytes.Equal(got.Payload(), payload) {
			t.Fatalf("type=%v payloadLen=%v", got.MessageType(), len(got.Payload()))
		}
	})

	// read-large-roundtrip: a 5000-byte payload at chunkSize=128 spans 40 chunks (39 c3
	// continuations). The exact wire length (12B c0 + 5000 payload + 39 c3 bytes = 5051) proves the
	// writer chunked into exactly 40 chunks with no empty trailing chunk, and the reader reassembles
	// the full 5000 bytes back. This is the "many c3 continuations + large multi-chunk read" case
	// the existing 5000-byte write test never read back.
	t.Run("read-large-roundtrip", func(t *testing.T) {
		const chunkSize = 128
		payload := make([]byte, 5000)
		for i := range payload {
			payload[i] = byte(i % 251)
		}
		// 5000 bytes: first chunk carries 128, the remaining 4872 take ceil(4872/128)=39 c3 chunks.
		const wantContinuations = 39

		var wire bytes.Buffer
		w := NewProtocol(&wire).(*protocol)
		w.output.opt.chunkSize = chunkSize
		m := NewStreamMessage(1).asMessage()
		m.messageHeader.MessageType = MessageTypeVideo
		m.messageHeader.Timestamp = 12345
		m.payload = payload
		if err := w.WriteMessage(ctx, m); err != nil {
			t.Fatalf("WriteMessage err=%v", err)
		}
		if want := 12 + len(payload) + wantContinuations; wire.Len() != want {
			t.Fatalf("wire len=%v want=%v", wire.Len(), want)
		}

		r := NewProtocol(&wire).(*protocol)
		r.input.opt.chunkSize = chunkSize
		got, err := r.ReadMessage(ctx)
		if err != nil {
			t.Fatalf("ReadMessage err=%v", err)
		}
		if got.Timestamp() != 12345 || !bytes.Equal(got.Payload(), payload) {
			t.Fatalf("ts=%v payloadLen=%v", got.Timestamp(), len(got.Payload()))
		}
	})

	// max-3byte-length-parse: a fmt0 header declaring payloadLength = 0xffffff (the 3-byte field
	// maxed out) must decode to exactly 0xffffff. Drive the header parse directly — reassembling
	// 16MiB of payload is pointless, and the DoS/truncation behavior at this length is covered by
	// TestPacketUnmarshalAdversarialInputs (P8). A shift/mask regression in the length decode would
	// corrupt the value, which is what this pins.
	t.Run("max-3byte-length-parse", func(t *testing.T) {
		// fmt0 cid=5, ts=0 (so no extended timestamp), len=0xffffff, video, stream=1.
		in := bytes.NewBuffer([]byte{0x05, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, byte(MessageTypeVideo), 0x01, 0x00, 0x00, 0x00})
		p := NewProtocol(in).(*protocol)
		format, cid, err := p.readBasicHeader(ctx)
		if err != nil {
			t.Fatalf("readBasicHeader err=%v", err)
		}
		// Mirror ReadMessage's chunkStream setup.
		chunk := newChunkStream()
		p.input.chunks[cid] = chunk
		chunk.header.betterCid = cid
		if err := p.readMessageHeader(ctx, chunk, format); err != nil {
			t.Fatalf("readMessageHeader err=%v", err)
		}
		if chunk.message.payloadLength != 0xffffff {
			t.Fatalf("payloadLength=%#x want=0xffffff", chunk.message.payloadLength)
		}
	})
}

// TestGoldenWireBytes covers P6: golden wire-byte regression for the chunk headers and control
// packets. The existing TestWriteMessageHeadersChunkingAndErrors pins golden bytes for one
// extended-timestamp video message only; this locks the remaining wire shapes a refactor could
// silently change — both forms of the C0 and C3 headers, and the full on-wire framing of every
// control packet.
//
// C++ reference (send/golden):
//
//	srs_utest_manual_protocol.cpp :: TEST(ProtocolStackTest,
//	  ProtocolSendSrsSetChunkSizePacket / ProtocolSendSrsSetWindowAckSizePacket /
//	  ProtocolSendSrsSetPeerBandwidthPacket / ProtocolSendSrsUserControlPacket)
//
// The control-packet payload bytes below match those C++ goldens exactly; the only difference is
// the chunk basic-header byte — Go frames protocol-control packets on cid=2 (-> 0x02) where the
// C++ goldens show 0x03 — so the payload portion is the cross-implementation wire-format invariant.
func TestGoldenWireBytes(t *testing.T) {
	ctx := context.Background()

	// C0/C3 chunk headers, both timestamp forms. A message on cid=5, video, stream=7, payload
	// length 5. With ts < 0xffffff the C0 header is 12 bytes carrying the timestamp inline; with
	// ts >= 0xffffff the 3-byte field saturates to 0xffffff and a 4-byte extended timestamp is
	// appended (16 bytes total). The C3 header is the 1-byte continuation form normally, but
	// inherits the same extended-timestamp quirk (Adobe always re-sends it), so it grows to 5
	// bytes when ts >= 0xffffff.
	t.Run("chunk-headers", func(t *testing.T) {
		// MessageType and Timestamp are method names on *message, shadowing the promoted
		// messageHeader fields, so set those two through the embedded struct explicitly.
		shortTs := &message{}
		shortTs.betterCid = chunkIDOverStream // 5
		shortTs.messageHeader.MessageType = MessageTypeVideo
		shortTs.streamID = 7
		shortTs.payloadLength = 5
		shortTs.messageHeader.Timestamp = 0x0a

		extTs := &message{}
		extTs.betterCid = chunkIDOverStream
		extTs.messageHeader.MessageType = MessageTypeVideo
		extTs.streamID = 7
		extTs.payloadLength = 5
		extTs.messageHeader.Timestamp = extendedTimestamp + 9 // 0x01000008, >= 0xffffff

		cases := []struct {
			name string
			gen  func() ([]byte, error)
			want []byte
		}{
			// basic(0x05) | ts(00 00 0a) | len(00 00 05) | type(09) | streamID LE(07 00 00 00)
			{"c0-short-ts", shortTs.generateC0Header, []byte{0x05, 0x00, 0x00, 0x0a, 0x00, 0x00, 0x05, byte(MessageTypeVideo), 0x07, 0x00, 0x00, 0x00}},
			// ts field saturates to ff ff ff; ext-ts(01 00 00 08) appended after streamID.
			{"c0-ext-ts", extTs.generateC0Header, []byte{0x05, 0xff, 0xff, 0xff, 0x00, 0x00, 0x05, byte(MessageTypeVideo), 0x07, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x08}},
			// 1-byte continuation: 0xc0 | cid.
			{"c3-short-ts", shortTs.generateC3Header, []byte{0xc5}},
			// continuation + re-sent 4-byte ext-ts.
			{"c3-ext-ts", extTs.generateC3Header, []byte{0xc5, 0x01, 0x00, 0x00, 0x08}},
		}
		for _, c := range cases {
			t.Run(c.name, func(t *testing.T) {
				got, err := c.gen()
				if err != nil {
					t.Fatalf("gen err=%v", err)
				}
				if !bytes.Equal(got, c.want) {
					t.Fatalf("got=%x want=%x", got, c.want)
				}
			})
		}
	})

	// Control packets, full on-wire framing via WritePacket. WritePacket frames each control packet
	// on cid=2 (chunkIDProtocolControl) with ts=0 and streamID=0, so the wire is the 12-byte
	// short-ts C0 header followed by the marshaled payload (all shorter than the default chunk size,
	// hence a single chunk). The payload values mirror the C++ send goldens.
	t.Run("control-packets", func(t *testing.T) {
		cases := []struct {
			name string
			pkt  Packet
			want []byte
		}{
			// SetChunkSize 1024=0x00000400, type 0x01, len 4.
			{
				"set-chunk-size",
				&SetChunkSize{ChunkSize: 1024},
				[]byte{0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00},
			},
			// WindowAcknowledgementSize 102400=0x00019000, type 0x05, len 4.
			{
				"window-ack-size",
				&WindowAcknowledgementSize{AckSize: 102400},
				[]byte{0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x90, 0x00},
			},
			// SetPeerBandwidth 1024=0x00000400 + limit soft(0x01), type 0x06, len 5.
			{
				"set-peer-bandwidth",
				&SetPeerBandwidth{Bandwidth: 1024, LimitType: LimitTypeSoft},
				[]byte{0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x01},
			},
			// UserControl SetBufferLength: event-type 0x0003, event-data 0x00000001,
			// extra-data 0x00000010; type 0x04, len 10=0x0a.
			{
				"user-control-set-buffer-length",
				&UserControl{EventType: EventTypeSetBufferLength, EventData: 0x01, ExtraData: 0x10},
				[]byte{0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0a, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x10},
			},
		}
		for _, c := range cases {
			t.Run(c.name, func(t *testing.T) {
				var wire bytes.Buffer
				p := NewProtocol(&wire).(*protocol)
				if err := p.WritePacket(ctx, c.pkt, 0); err != nil {
					t.Fatalf("WritePacket err=%v", err)
				}
				if !bytes.Equal(wire.Bytes(), c.want) {
					t.Fatalf("got=%x want=%x", wire.Bytes(), c.want)
				}
			})
		}
	})

	// UserControl payload, all three Size() branches. The marshaler has three event-data shapes: a
	// normal 4-byte event-data (e.g. PingRequest), an 8-byte event-data with the extra
	// buffer-length word (SetBufferLength), and the special 1-byte event-data for FmsEvent0
	// (0x001a). Pin the raw payload bytes for each.
	t.Run("user-control-event-forms", func(t *testing.T) {
		cases := []struct {
			name string
			pkt  *UserControl
			want []byte
		}{
			// event-type 0x0006, event-data 0x12345678 (4 bytes).
			{"ping-request-4byte", &UserControl{EventType: EventTypePingRequest, EventData: 0x12345678}, []byte{0x00, 0x06, 0x12, 0x34, 0x56, 0x78}},
			// event-type 0x0003, event-data 0x00000001, extra 0x000005dc (8 bytes total).
			{"set-buffer-length-8byte", &UserControl{EventType: EventTypeSetBufferLength, EventData: 1, ExtraData: 1500}, []byte{0x00, 0x03, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x05, 0xdc}},
			// event-type 0x001a, event-data 0x01 (1 byte).
			{"fms-event0-1byte", &UserControl{EventType: EventTypeFmsEvent0, EventData: 0x01}, []byte{0x00, 0x1a, 0x01}},
		}
		for _, c := range cases {
			t.Run(c.name, func(t *testing.T) {
				got, err := c.pkt.MarshalBinary()
				if err != nil {
					t.Fatalf("MarshalBinary err=%v", err)
				}
				if !bytes.Equal(got, c.want) {
					t.Fatalf("got=%x want=%x", got, c.want)
				}
			})
		}
	})
}

// === P7: Fuzz targets ===
//
// Three Go native fuzz targets covering the untrusted-input parsers in this package.
//   FuzzReadMessage     — (*protocol).ReadMessage on arbitrary wire bytes.
//   FuzzDecodeMessage   — (*protocol).DecodeMessage on arbitrary (MessageType, payload).
//   FuzzPacketUnmarshal — *Packet.UnmarshalBinary on arbitrary bytes across every Packet type.
//
// Each target's contract is "no panic". Termination is guaranteed: every fuzz body reads
// from a finite bytes.Buffer and caps input size to keep iterations cheap. Real OOM /
// resource-exhaustion surfaces (e.g. an attacker-controlled SetChunkSize followed by a
// large payload length forcing a multi-MB make) are intentionally NOT pre-guarded here —
// fuzzing is how P8's adversarial cases get discovered.
//
// Seeds come from the existing happy-path tests so the fuzzer starts at valid wire bytes
// and explores the nearby malformed space.

// fuzzInputCap bounds the input fuzz can feed each iteration. The cap keeps single
// iterations under a millisecond on a laptop and stops the corpus from growing
// arbitrarily — it is not a security boundary.
const fuzzInputCap = 8 * 1024

// FuzzReadMessage drives the full chunk-stream reader against arbitrary bytes. The
// target asserts no panic across readBasicHeader (1/2/3-byte cid), readMessageHeader
// (every fmt + ext-ts), readMessagePayload (chunked reassembly), and onMessageArrivated
// (SetChunkSize side effect on subsequent reads).
func FuzzReadMessage(f *testing.F) {
	// Seed 1: a single fmt0 audio message on cid=5, ts=10, len=3.
	f.Add([]byte{0x05, 0x00, 0x00, 0x0a, 0x00, 0x00, 0x03, byte(MessageTypeAudio), 0x01, 0x00, 0x00, 0x00, 1, 2, 3})

	// Seed 2: fmt0 -> fmt1 -> fmt2 -> fmt3 sequence on cid=5
	// (lifted from TestReadMessageHeadersPayloadsAndChunks).
	{
		var s bytes.Buffer
		s.Write([]byte{0x05, 0x00, 0x00, 0x0a, 0x00, 0x00, 0x03, byte(MessageTypeAudio), 0x01, 0x00, 0x00, 0x00, 1, 2, 3})
		s.Write([]byte{0x45, 0x00, 0x00, 0x05, 0x00, 0x00, 0x02, byte(MessageTypeVideo), 4, 5})
		s.Write([]byte{0x85, 0x00, 0x00, 0x07, 6, 7})
		s.Write([]byte{0xc5, 8, 9})
		f.Add(s.Bytes())
	}

	// Seed 3: extended-timestamp fmt0 with payload split across chunks at the default
	// chunk size. Exercises the ext-ts read + accumulate path.
	{
		var s bytes.Buffer
		s.Write([]byte{0x05, 0xff, 0xff, 0xff, 0x00, 0x00, 0x05, byte(MessageTypeVideo), 0x01, 0x00, 0x00, 0x00})
		binary.Write(&s, binary.BigEndian, uint32(42))
		s.Write([]byte{1, 2, 3, 4, 5})
		f.Add(s.Bytes())
	}

	// Seed 4: 2-byte cid header (cid=74) wrapping a complete fmt0 message. Exercises
	// the 2-byte readBasicHeader branch end-to-end.
	f.Add([]byte{0x00, 0x0a, 0x00, 0x00, 0x05, 0x00, 0x00, 0x01, byte(MessageTypeVideo), 0x01, 0x00, 0x00, 0x00, 0xAA})

	// Seed 5: SetChunkSize=128 followed by an audio message on cid=5. Exercises the
	// onMessageArrivated -> input.chunkSize update path.
	{
		var s bytes.Buffer
		s.Write([]byte{0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, byte(MessageTypeSetChunkSize), 0x00, 0x00, 0x00, 0x00})
		binary.Write(&s, binary.BigEndian, uint32(128))
		s.Write([]byte{0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, byte(MessageTypeAudio), 0x01, 0x00, 0x00, 0x00, 0xAA})
		f.Add(s.Bytes())
	}

	f.Fuzz(func(t *testing.T, data []byte) {
		if len(data) > fuzzInputCap {
			return
		}
		ctx := context.Background()
		p := NewProtocol(bytes.NewBuffer(append([]byte(nil), data...)))
		// bytes.Buffer EOFs deterministically once drained, so a small cap on the
		// number of messages we accept here is enough to bound the iteration.
		for range 16 {
			if _, err := p.ReadMessage(ctx); err != nil {
				return
			}
		}
	})
}

// FuzzDecodeMessage drives DecodeMessage with an arbitrary (MessageType, payload) pair.
// It covers the SetChunkSize / WindowAcknowledgementSize / SetPeerBandwidth / UserControl
// branches and, for AMF0/AMF3 command/data types, the parseAMFObject dispatch into every
// concrete *Packet's UnmarshalBinary.
func FuzzDecodeMessage(f *testing.F) {
	// Seed control-message payloads at their exact required sizes.
	f.Add(uint8(MessageTypeSetChunkSize), []byte{0, 0, 0, 128})
	f.Add(uint8(MessageTypeWindowAcknowledgementSize), []byte{0, 0, 0x10, 0})
	f.Add(uint8(MessageTypeSetPeerBandwidth), []byte{0, 0, 0x10, 0, byte(LimitTypeDynamic)})
	// UserControl PingRequest: 2B event-type + 4B data.
	f.Add(uint8(MessageTypeUserControl), []byte{0x00, byte(EventTypePingRequest), 0x00, 0x00, 0x00, 0x01})

	// Seed an AMF0 command with a well-formed publish packet. Also seed the AMF3 form,
	// which differs only by a leading byte that DecodeMessage strips.
	pub := NewPublishPacket()
	pub.TransactionID = 0
	pub.StreamName = NewAmf0String("s")
	pubBytes, err := pub.MarshalBinary()
	if err != nil {
		f.Fatalf("seed marshal publish: %v", err)
	}
	f.Add(uint8(MessageTypeAMF0Command), pubBytes)
	f.Add(uint8(MessageTypeAMF3Command), append([]byte{0}, pubBytes...))

	f.Fuzz(func(t *testing.T, mtype uint8, payload []byte) {
		if len(payload) > fuzzInputCap {
			return
		}
		p := NewProtocol(&bytes.Buffer{})
		m := &message{payload: payload}
		m.messageHeader.MessageType = MessageType(mtype)
		_, _ = p.DecodeMessage(m)
	})
}

// FuzzPacketUnmarshal drives every Packet's UnmarshalBinary against arbitrary bytes.
// One target, dispatched by a kind discriminator, so the fuzzer can share a corpus and
// mutate the kind alongside the bytes.
func FuzzPacketUnmarshal(f *testing.F) {
	// Build a seed per packet type from its own round-trippable bytes.
	type seed struct {
		kind uint8
		pkt  Packet
	}
	connRes := NewConnectAppResPacket(7)
	connRes.Args.Set("data", NewAmf0EcmaArray().Set("srs_id", NewAmf0String("sid")))
	call := NewCallPacket()
	call.CommandName = commandOnStatus
	call.TransactionID = 0
	call.CommandObject = NewAmf0Null()
	pub := NewPublishPacket()
	pub.TransactionID = 0
	pub.StreamName = NewAmf0String("s")
	play := NewPlayPacket()
	play.TransactionID = 0
	play.StreamName = NewAmf0String("s")
	seeds := []seed{
		{0, NewConnectAppPacket()},
		{1, connRes},
		{2, call},
		{3, NewCreateStreamPacket()},
		{4, NewCreateStreamResPacket(2)},
		{5, pub},
		{6, play},
		{7, &SetChunkSize{ChunkSize: 128}},
		{8, &WindowAcknowledgementSize{AckSize: 2500000}},
		{9, &SetPeerBandwidth{Bandwidth: 2500000, LimitType: LimitTypeDynamic}},
		{10, &UserControl{EventType: EventTypePingRequest, EventData: 1}},
	}
	for _, s := range seeds {
		b, err := s.pkt.MarshalBinary()
		if err != nil {
			f.Fatalf("seed marshal %T: %v", s.pkt, err)
		}
		f.Add(s.kind, b)
	}

	f.Fuzz(func(t *testing.T, kind uint8, data []byte) {
		if len(data) > fuzzInputCap {
			return
		}
		var pkt Packet
		switch kind % 11 {
		case 0:
			pkt = NewConnectAppPacket()
		case 1:
			pkt = NewConnectAppResPacket(0)
		case 2:
			pkt = NewCallPacket()
		case 3:
			pkt = NewCreateStreamPacket()
		case 4:
			pkt = NewCreateStreamResPacket(0)
		case 5:
			pkt = NewPublishPacket()
		case 6:
			pkt = NewPlayPacket()
		case 7:
			pkt = NewSetChunkSize()
		case 8:
			pkt = NewWindowAcknowledgementSize()
		case 9:
			pkt = NewSetPeerBandwidth()
		case 10:
			pkt = NewUserControl()
		}
		_ = pkt.UnmarshalBinary(data)
	})
}

// TestPacketUnmarshalAdversarialInputs covers P8: malformed and truncated wire input
// must never panic the parser, only error. The P7 fuzzers found a panic class where a
// New*Packet constructor pre-set an optional AMF0 field (variantCallPacket.CommandObject
// or CallPacket.Args), and Size() then counted that phantom default even when the wire
// was exhausted before it — so the caller's p = p[Size():] advance sliced out of range
// (rtmp.go:1512, "slice bounds out of range"). These cases lock in the fix; the two
// minimized fuzz inputs are also committed under testdata/fuzz as regression corpus.
func TestPacketUnmarshalAdversarialInputs(t *testing.T) {
	// safeUnmarshal runs UnmarshalBinary and turns any panic into an immediate failure.
	safeUnmarshal := func(t *testing.T, pkt Packet, data []byte) (err error) {
		defer func() {
			if r := recover(); r != nil {
				t.Fatalf("panic on %T with %x: %v", pkt, data, r)
			}
		}()
		return pkt.UnmarshalBinary(data)
	}

	// The two inputs the P7 fuzzers minimized to. Both are a "publish" command name + a
	// number transaction id with nothing after, so the optional command object is absent
	// and must not be counted by Size(). They previously panicked; expect a clean error.
	t.Run("fuzz-crashers", func(t *testing.T) {
		// FuzzPacketUnmarshal/2b0534f8182fac96: direct PublishPacket.UnmarshalBinary.
		if err := safeUnmarshal(t, NewPublishPacket(), []byte("\x02\x00\x00\x0000000000")); err == nil {
			t.Fatalf("truncated publish: want error, got nil")
		}
		// FuzzDecodeMessage/20ed1884f5b4f009: the AMF3 form reaches the same packet via
		// DecodeMessage, which strips the leading AMF3 byte ('0'=0x30) before dispatching.
		func() {
			defer func() {
				if r := recover(); r != nil {
					t.Fatalf("DecodeMessage panic: %v", r)
				}
			}()
			p := NewProtocol(&bytes.Buffer{})
			m := &message{payload: []byte("0\x02\x00\apublish\x0000000000")}
			m.messageHeader.MessageType = MessageTypeAMF3Command
			if _, err := p.DecodeMessage(m); err == nil {
				t.Fatalf("truncated AMF3 publish: want error, got nil")
			}
		}()
	})

	// For every variantCallPacket-derived packet, marshal a valid instance then feed every
	// truncation of its bytes back to a fresh packet. No prefix may panic, and the
	// full-length bytes must still round-trip.
	t.Run("truncations", func(t *testing.T) {
		call := NewCallPacket()
		call.CommandName = commandOnStatus
		call.TransactionID = 0
		call.CommandObject = NewAmf0Null()
		pub := NewPublishPacket()
		pub.TransactionID = 0
		pub.StreamName = NewAmf0String("s")
		play := NewPlayPacket()
		play.TransactionID = 0
		play.StreamName = NewAmf0String("s")
		cases := []struct {
			name  string
			full  Packet
			fresh func() Packet
		}{
			{"call", call, func() Packet { return NewCallPacket() }},
			{"publish", pub, func() Packet { return NewPublishPacket() }},
			{"play", play, func() Packet { return NewPlayPacket() }},
			{"createStreamRes", NewCreateStreamResPacket(2), func() Packet { return NewCreateStreamResPacket(0) }},
		}
		for _, c := range cases {
			b, err := c.full.MarshalBinary()
			if err != nil {
				t.Fatalf("%v marshal: %v", c.name, err)
			}
			for n := 0; n <= len(b); n++ {
				err := safeUnmarshal(t, c.fresh(), b[:n])
				if n == len(b) && err != nil {
					t.Fatalf("%v full unmarshal: %v", c.name, err)
				}
			}
		}
	})

	// An oversized declared message length with a truncated stream must error via the
	// incremental chunk read (readMessagePayload caps each read at chunkSize and lets
	// io.ReadFull fail), not allocate ~16MB up front or hang. The header declares
	// payloadLength = 0xffffff but only four payload bytes follow.
	t.Run("oversized-length-truncated", func(t *testing.T) {
		var in bytes.Buffer
		in.Write([]byte{0x05, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, byte(MessageTypeVideo), 0x01, 0x00, 0x00, 0x00})
		in.Write([]byte{1, 2, 3, 4})
		p := NewProtocol(&in)
		if _, err := p.ReadMessage(context.Background()); err == nil {
			t.Fatalf("oversized truncated message: want error, got nil")
		}
	})
}

// P9 — concurrency / race on the transaction map.
//
// WritePacket registers a tid -> request-name entry under input.ltransactions
// (via onPacketWriten); parseAMFObject, reached from DecodeMessage when a
// _result/_error arrives, reads and deletes that entry under the same lock.
// This test hammers both paths from two goroutines over an overlapping tid
// space so the writes and reads/deletes genuinely interleave on
// input.transactions. Run with -race to validate the locking; even without it,
// the Go runtime panics on unsynchronized concurrent map access, so a dropped
// lock in either path fails the test.
//
// The shared state is only the transactions map: WritePacket touches just the
// writer (io.Discard here) and DecodeMessage operates on the Message it is
// handed, never the reader. Read-side "No matched request" errors are expected
// and tolerated — a tid may not be registered yet or may already be consumed;
// the assertion is no race and no panic, not that every lookup hits.
//
// No C++ reference: the C++ ProtocolStackTest suite has no concurrency test.
// New coverage.
func TestProtocolTransactionMapConcurrency(t *testing.T) {
	const (
		keys       = 16   // tid space both goroutines cycle through (overlap forces hits)
		iterations = 4000 // per goroutine
	)

	// WritePacket only writes to v.w; DecodeMessage only reads the Message it is
	// given. io.Discard keeps the single-writer goroutine from growing a buffer.
	rw := struct {
		io.Reader
		io.Writer
	}{strings.NewReader(""), io.Discard}
	p := NewProtocol(rw).(*protocol)

	// Pre-build the _result bytes per tid so the read loop exercises only the
	// map access in parseAMFObject, not AMF marshaling. releaseStream is one of
	// the request names parseAMFObject resolves a _result against, so a hit
	// returns a CallPacket and deletes the entry.
	resultBytes := make([][]byte, keys+1)
	for tid := 1; tid <= keys; tid++ {
		res := NewCallPacket()
		res.CommandName = commandResult
		res.TransactionID = amf0Number(tid)
		res.CommandObject = NewAmf0Null()
		resultBytes[tid] = mustPacketBytes(t, res)
	}

	ctx := context.Background()
	var wg sync.WaitGroup
	wg.Add(2)

	// Writer: registers tid -> releaseStream under the lock, cycling the tid space.
	go func() {
		defer wg.Done()
		for i := 0; i < iterations; i++ {
			call := NewCallPacket()
			call.CommandName = commandReleaseStream
			call.TransactionID = amf0Number(i%keys + 1)
			call.CommandObject = NewAmf0Null()
			if err := p.WritePacket(ctx, call, 0); err != nil {
				t.Errorf("WritePacket err=%v", err)
				return
			}
		}
	}()

	// Reader: decodes _result messages whose tids overlap the writer's range.
	// Hits delete the entry; misses return "No matched request". Both take the lock.
	go func() {
		defer wg.Done()
		for i := 0; i < iterations; i++ {
			msg := &message{payload: resultBytes[i%keys+1]}
			msg.messageHeader.MessageType = MessageTypeAMF0Command
			if _, err := p.DecodeMessage(msg); err != nil &&
				!strings.Contains(err.Error(), "No matched request") {
				t.Errorf("DecodeMessage err=%v", err)
				return
			}
		}
	}()

	wg.Wait()
}
