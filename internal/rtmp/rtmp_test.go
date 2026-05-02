// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT
package rtmp

import (
	"bytes"
	"context"
	"encoding/binary"
	"io"
	"reflect"
	"strings"
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
		{"three-byte-code-path", []byte{0xc1, 0x01, 0x02}, formatType3, 65},
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
