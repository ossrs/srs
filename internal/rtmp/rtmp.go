// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT
package rtmp

import (
	"bufio"
	"bytes"
	"context"
	"crypto/rand"
	"encoding"
	"encoding/binary"
	"fmt"
	"io"
	"sync"

	"srsx/internal/errors"
)

type Handshake interface {
	C1S1() []byte
	WriteC0S0(w io.Writer) error
	ReadC0S0(r io.Reader) ([]byte, error)
	WriteC1S1(w io.Writer) error
	ReadC1S1(r io.Reader) ([]byte, error)
	WriteC2S2(w io.Writer, s1c1 []byte) error
	ReadC2S2(r io.Reader) ([]byte, error)
}

type handshake struct {
	c1s1 []byte
}

func NewHandshake() Handshake {
	return &handshake{}
}

func (v *handshake) C1S1() []byte {
	return v.c1s1
}

func (v *handshake) WriteC0S0(w io.Writer) (err error) {
	r := bytes.NewReader([]byte{0x03})
	if _, err = io.Copy(w, r); err != nil {
		return errors.Wrap(err, "write c0s0")
	}

	return
}

func (v *handshake) ReadC0S0(r io.Reader) (c0 []byte, err error) {
	b := &bytes.Buffer{}
	if _, err = io.CopyN(b, r, 1); err != nil {
		return nil, errors.Wrap(err, "read c0s0")
	}

	c0 = b.Bytes()

	return
}

func (v *handshake) WriteC1S1(w io.Writer) (err error) {
	p := make([]byte, 1536)

	if _, err = rand.Read(p[8:]); err != nil {
		return errors.Wrap(err, "generate random bytes")
	}

	r := bytes.NewReader(p)
	if _, err = io.Copy(w, r); err != nil {
		return errors.Wrap(err, "write c0s1")
	}

	return
}

func (v *handshake) ReadC1S1(r io.Reader) (c1s1 []byte, err error) {
	b := &bytes.Buffer{}
	if _, err = io.CopyN(b, r, 1536); err != nil {
		return nil, errors.Wrap(err, "read c1s1")
	}

	c1s1 = b.Bytes()
	v.c1s1 = c1s1

	return
}

func (v *handshake) WriteC2S2(w io.Writer, s1c1 []byte) (err error) {
	r := bytes.NewReader(s1c1[:])
	if _, err = io.Copy(w, r); err != nil {
		return errors.Wrap(err, "write c2s2")
	}

	return
}

func (v *handshake) ReadC2S2(r io.Reader) (c2 []byte, err error) {
	b := &bytes.Buffer{}
	if _, err = io.CopyN(b, r, 1536); err != nil {
		return nil, errors.Wrap(err, "read c2s2")
	}

	c2 = b.Bytes()

	return
}

// See rtmp_specification_1.0.pdf, page 16, section 6.1 Chunk Format. The 4-byte extended
// timestamp is sent only when the timestamp field is 0xffffff, which marks a timestamp of
// 0xffffff or more.
const extendedTimestamp = uint64(0xffffff)

const defaultChunkSize = 128

type settings struct {
	chunkSize uint32
}

func newSettings() *settings {
	return &settings{
		chunkSize: defaultChunkSize,
	}
}

type chunkStream struct {
	format  formatType
	cid     chunkID
	header  messageHeader
	message *message
	count   uint64

	// Type 3 chunks have no timestamp field and take this from the preceding Type 0, 1 or 2
	// chunk.
	hasExtendedTimestamp bool
	// Kept apart from header.Timestamp, the accumulated message timestamp, because a Type 1
	// or 2 chunk carries a delta here, and a Type 3 chunk that omits the field is found by
	// comparing with it.
	extendedTimestamp uint32
}

func newChunkStream() *chunkStream {
	return &chunkStream{}
}

type Protocol interface {
	// Deprecated: Go does not support generic methods. Please use rtmp.ExpectPacket instead.
	ExpectPacket(ctx context.Context, ppkt any) (Message, error)
	ExpectMessage(ctx context.Context, types ...MessageType) (Message, error)
	DecodeMessage(m Message) (Packet, error)
	ReadMessage(ctx context.Context) (Message, error)
	WritePacket(ctx context.Context, pkt Packet, streamID int) error
	WriteMessage(ctx context.Context, m Message) error
}

type protocol struct {
	r     *bufio.Reader
	w     *bufio.Writer
	input struct {
		opt    *settings
		chunks map[chunkID]*chunkStream

		transactions  map[amf0Number]amf0String
		ltransactions sync.Mutex
	}
	output struct {
		opt *settings
	}
}

func NewProtocol(rw io.ReadWriter) Protocol {
	v := &protocol{
		r: bufio.NewReader(rw),
		w: bufio.NewWriter(rw),
	}

	v.input.opt = newSettings()
	v.input.chunks = map[chunkID]*chunkStream{}
	v.input.transactions = map[amf0Number]amf0String{}

	v.output.opt = newSettings()

	return v
}

func ExpectPacket[T Packet](ctx context.Context, v Protocol, ppkt *T) (m Message, err error) {
	for {
		if m, err = v.ReadMessage(ctx); err != nil {
			return nil, errors.WithMessage(err, "read message")
		}

		var pkt Packet
		if pkt, err = v.DecodeMessage(m); err != nil {
			return nil, errors.WithMessage(err, "decode message")
		}

		if p, ok := pkt.(T); ok {
			*ppkt = p
			break
		}
	}

	return
}

// Deprecated: Go does not support generic methods. Please use rtmp.ExpectPacket instead.
func (v *protocol) ExpectPacket(ctx context.Context, ppkt any) (m Message, err error) {
	panic("Go does not support generic methods; please use rtmp.ExpectPacket instead")
}

func (v *protocol) ExpectMessage(ctx context.Context, types ...MessageType) (m Message, err error) {
	for {
		if m, err = v.ReadMessage(ctx); err != nil {
			return nil, errors.WithMessage(err, "read message")
		}

		if len(types) == 0 {
			return
		}

		for _, t := range types {
			if m.MessageType() == t {
				return
			}
		}
	}
}

func (v *protocol) parseAMFObject(p []byte) (pkt Packet, err error) {
	var commandName amf0String
	if err = commandName.UnmarshalBinary(p); err != nil {
		return nil, errors.WithMessage(err, "unmarshal command name")
	}

	switch commandName {
	case commandResult, commandError:
		var transactionID amf0Number
		if err = transactionID.UnmarshalBinary(p[commandName.Size():]); err != nil {
			return nil, errors.WithMessage(err, "unmarshal tid")
		}

		var requestName amf0String
		if err = func() error {
			v.input.ltransactions.Lock()
			defer v.input.ltransactions.Unlock()

			var ok bool
			if requestName, ok = v.input.transactions[transactionID]; !ok {
				return errors.Errorf("No matched request for tid=%v", transactionID)
			}
			delete(v.input.transactions, transactionID)

			return nil
		}(); err != nil {
			return nil, errors.WithMessage(err, "discovery request name")
		}

		switch requestName {
		case commandConnect:
			return NewConnectAppResPacket(transactionID), nil
		case commandCreateStream:
			return NewCreateStreamResPacket(transactionID), nil
		case commandReleaseStream, commandFCPublish, commandFCUnpublish:
			call := NewCallPacket()
			call.TransactionID = transactionID
			return call, nil
		default:
			return nil, errors.Errorf("No request for %v", string(requestName))
		}
	case commandConnect:
		return NewConnectAppPacket(), nil
	case commandPublish:
		return NewPublishPacket(), nil
	case commandPlay:
		return NewPlayPacket(), nil
	default:
		return NewCallPacket(), nil
	}
}

func (v *protocol) DecodeMessage(m Message) (pkt Packet, err error) {
	p := m.Payload()[:]
	if len(p) == 0 {
		return nil, errors.New("Empty packet")
	}

	switch m.MessageType() {
	case MessageTypeAMF3Command, MessageTypeAMF3Data:
		p = p[1:]
	}

	switch m.MessageType() {
	case MessageTypeSetChunkSize:
		pkt = NewSetChunkSize()
	case MessageTypeWindowAcknowledgementSize:
		pkt = NewWindowAcknowledgementSize()
	case MessageTypeSetPeerBandwidth:
		pkt = NewSetPeerBandwidth()
	case MessageTypeAMF0Command, MessageTypeAMF3Command, MessageTypeAMF0Data, MessageTypeAMF3Data:
		if pkt, err = v.parseAMFObject(p); err != nil {
			return nil, errors.WithMessage(err, fmt.Sprintf("Parse AMF %v", m.MessageType()))
		}
	case MessageTypeUserControl:
		pkt = NewUserControl()
	default:
		return nil, errors.Errorf("Unknown message %v", m.MessageType())
	}

	if err = pkt.UnmarshalBinary(p); err != nil {
		return nil, errors.WithMessage(err, fmt.Sprintf("Unmarshal %v", m.MessageType()))
	}

	return
}

func (v *protocol) ReadMessage(ctx context.Context) (m Message, err error) {
	for m == nil {
		// TODO: We should convert buffered io to async io, because we will be stuck in block io here,
		// TODO: but the risk is acceptable because we literally will set the underlay io timeout.
		if ctx.Err() != nil {
			return nil, ctx.Err()
		}

		var cid chunkID
		var format formatType
		if format, cid, err = v.readBasicHeader(ctx); err != nil {
			return nil, errors.WithMessage(err, "read basic header")
		}

		var ok bool
		var chunk *chunkStream
		if chunk, ok = v.input.chunks[cid]; !ok {
			chunk = newChunkStream()
			v.input.chunks[cid] = chunk
			chunk.header.betterCid = cid
		}

		if err = v.readMessageHeader(ctx, chunk, format); err != nil {
			return nil, errors.WithMessage(err, "read message header")
		}

		if m, err = v.readMessagePayload(ctx, chunk); err != nil {
			return nil, errors.WithMessage(err, "read message payload")
		}

		if m != nil {
			if err = v.onMessageArrivated(m.asMessage()); err != nil {
				return nil, errors.WithMessage(err, "on message")
			}
		}
	}

	return
}

func (v *protocol) readMessagePayload(ctx context.Context, chunk *chunkStream) (m Message, err error) {
	if chunk.message.payloadLength == 0 {
		m = chunk.message
		chunk.message = nil
		return
	}

	chunkedPayloadSize := int(chunk.message.payloadLength) - len(chunk.message.payload)
	if chunkedPayloadSize > int(v.input.opt.chunkSize) {
		chunkedPayloadSize = int(v.input.opt.chunkSize)
	}

	b := make([]byte, chunkedPayloadSize)
	if _, err = io.ReadFull(v.r, b); err != nil {
		return nil, errors.Wrapf(err, "read chunk %vB", chunkedPayloadSize)
	}
	chunk.message.payload = append(chunk.message.payload, b...)

	if int(chunk.message.payloadLength) == len(chunk.message.payload) {
		m = chunk.message
		chunk.message = nil
	}

	return
}

// See rtmp_specification_1.0.pdf, page 18, section 6.1.2 Chunk Message Header, selected by
// the fmt field in the chunk basic header.
type formatType uint8

const (
	// 6.1.2.1. Type 0, 11 bytes. It MUST start a chunk stream, and is used whenever the
	// stream timestamp goes backward, such as after a backward seek.
	formatType0 formatType = iota
	// 6.1.2.2. Type 1, 7 bytes, with the stream ID of the preceding chunk. For the first
	// chunk of each message after the first, for variable-sized messages such as video.
	formatType1
	// 6.1.2.3. Type 2, 3 bytes, with the stream ID and message length of the preceding
	// chunk. For the first chunk of each message after the first, for constant-sized
	// messages such as audio.
	formatType2
	// 6.1.2.4. Type 3, no header; all values come from the preceding chunk. For every chunk
	// of a message but the first, and for messages of the same size, stream ID and time
	// spacing. After a Type 0 chunk, its timestamp delta is the timestamp of the Type 0
	// chunk.
	formatType3
)

var messageHeaderSizes = []int{11, 7, 3, 0}

// The chunk message header:
//
//	3bytes: timestamp delta,    fmt=0,1,2
//	3bytes: payload length,     fmt=0,1
//	1bytes: message type,       fmt=0,1
//	4bytes: stream id,          fmt=0
//
// where:
//
//	fmt=0, 0x0X
//	fmt=1, 0x4X
//	fmt=2, 0x8X
//	fmt=3, 0xCX
func (v *protocol) readMessageHeader(ctx context.Context, chunk *chunkStream, format formatType) (err error) {
	// Any fmt may start a message, not only 0. FMLE sends a fmt=3 chunk, such as 0xc4, to
	// start an audio message after a fmt=0 one (04, timestamp 26, length 157, audio, stream
	// 1). It is a new message with the same header and the timestamp 26+26=52, so the first
	// chunk of a message updates the timestamp even for fmt=3.
	var isFirstChunkOfMsg bool
	if chunk.message == nil {
		isFirstChunkOfMsg = true
	}

	// A fresh chunk stream must start with fmt=0.
	if chunk.count == 0 && format != formatType0 {
		// But librtmp sends a ping on a fresh chunk stream with fmt=1:
		// 0x42             where: fmt=1, cid=2, protocol control user-control message
		// 0x00 0x00 0x00   where: timestamp=0
		// 0x00 0x00 0x06   where: payload_length=6
		// 0x04             where: message_type=4(protocol control user-control message)
		// 0x00 0x06            where: event Ping(0x06)
		// 0x00 0x00 0x0d 0x0f  where: event data 4bytes ping timestamp.
		// @see: https://github.com/ossrs/srs/issues/98
		if chunk.cid == chunkIDProtocolControl && format == formatType1 {
		} else {
			return errors.Errorf("For fresh chunk, fmt %v != %v(required), cid is %v", format, formatType0, chunk.cid)
		}
	}

	if chunk.message != nil && format == formatType0 {
		return errors.Errorf("For exists chunk, fmt is %v, cid is %v", format, chunk.cid)
	}

	if chunk.message == nil {
		chunk.message = newMessage()
	}

	p := make([]byte, messageHeaderSizes[format])
	if _, err = io.ReadFull(v.r, p); err != nil {
		return errors.Wrapf(err, "read %vB message header", len(p))
	}

	if format <= formatType2 {
		chunk.header.timestampDelta = uint32(p[0])<<16 | uint32(p[1])<<8 | uint32(p[2])
		p = p[3:]

		chunk.hasExtendedTimestamp = uint64(chunk.header.timestampDelta) >= extendedTimestamp

		if format <= formatType1 {
			payloadLength := uint32(p[0])<<16 | uint32(p[1])<<8 | uint32(p[2])
			p = p[3:]

			// A fmt=1 chunk may change the payload length for a new message, but not within a
			// message.
			if !isFirstChunkOfMsg && chunk.header.payloadLength != payloadLength {
				return errors.Errorf("Chunk message size %v != %v(required)", payloadLength, chunk.header.payloadLength)
			}
			chunk.header.payloadLength = payloadLength

			chunk.header.MessageType = MessageType(p[0])
			p = p[1:]

			if format == formatType0 {
				chunk.header.streamID = uint32(p[0]) | uint32(p[1])<<8 | uint32(p[2])<<16 | uint32(p[3])<<24
				p = p[4:]
			}
		}
	}

	if chunk.hasExtendedTimestamp {
		// A peek, so the 4 bytes stay in the reader when a Type 3 chunk omits the field.
		var b []byte
		if b, err = v.r.Peek(4); err != nil {
			return errors.Wrapf(err, "read ext-ts, pkt-ts=%v", chunk.header.Timestamp)
		}

		// Some servers use a 32-bit extended timestamp, so always use 31 bits.
		// @see https://github.com/ossrs/srs/issues/111
		timestamp := binary.BigEndian.Uint32(b) & 0x7fffffff

		// The RTMP v1 2009 version, 6.1.3. Extended Timestamp: Type 3 chunks MUST NOT have
		// this field. The RTMP v1 2012 version, 5.3.1.3. Extended Timestamp: it is present in
		// Type 3 chunks when the most recent Type 0, 1 or 2 chunk indicated one.
		//
		// FMLE, FMS and Flash Player follow the 2012 version and send it in Type 3 chunks;
		// librtmp and FFmpeg may not. In the 2012 version it equals the previous one of the
		// message, so a different value means the field is not present and the 4 bytes are
		// payload.
		// @see http://blog.csdn.net/win_lin/article/details/13363699
		// @see https://github.com/veovera/enhanced-rtmp/issues/42
		if !isFirstChunkOfMsg && chunk.extendedTimestamp > 0 && chunk.extendedTimestamp != timestamp {
		} else {
			if _, err = v.r.Discard(4); err != nil {
				return errors.Wrapf(err, "discard ext-ts, pkt-ts=%v", chunk.header.Timestamp)
			}
			chunk.extendedTimestamp = timestamp
		}
	}

	timestamp := chunk.header.timestampDelta
	if chunk.hasExtendedTimestamp {
		timestamp = chunk.extendedTimestamp
	}
	if format == formatType0 {
		// 6.1.2.1. Type 0 sends the absolute timestamp.
		chunk.header.Timestamp = uint64(timestamp)
	} else if isFirstChunkOfMsg {
		// 6.1.2.2. Type 1 and 6.1.2.3. Type 2 send the delta from the previous chunk.
		chunk.header.Timestamp += uint64(timestamp)
	}

	// A 31-bit timestamp is enough. The RTMP spec, 3. Byte Order, Alignment, and Time Format,
	// says 32-bit timestamps roll over after fewer than 50 days, but its sample assumes
	// adjacent timestamps are within 2^31 milliseconds, and the FLV spec makes the timestamp
	// an SI32 (signed). A 24-bit timestamp is 4.66h, 32-bit is 49.71d.
	chunk.header.Timestamp &= 0x7fffffff

	chunk.message.messageHeader = chunk.header

	// The chunk stream now accepts fmt=1/2/3.
	chunk.count++

	return
}

// See rtmp_specification_1.0.pdf, page 17, section 6.1.1 Chunk Basic Header, 1, 2 or 3
// bytes, by the chunk stream ID.
//
// Chunk stream IDs 2-63:
//
//	 0 1 2 3 4 5 6 7
//	+-+-+-+-+-+-+-+-+
//	|fmt|   cs id   |
//	+-+-+-+-+-+-+-+-+
//	Figure 6 Chunk basic header 1
//
// Chunk stream IDs 64-319:
//
//	0                   1
//	0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5
//	+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//	|fmt|    0      | cs id - 64    |
//	+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//	Figure 7 Chunk basic header 2
//
// Chunk stream IDs 64-65599, with the cs id - 64 in little-endian:
//
//	 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3
//	+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//	|fmt|     1     |         cs id - 64            |
//	+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//	Figure 8 Chunk basic header 3
func (v *protocol) readBasicHeader(ctx context.Context) (format formatType, cid chunkID, err error) {
	var t uint8
	if err = binary.Read(v.r, binary.BigEndian, &t); err != nil {
		return format, cid, errors.Wrap(err, "read basic header")
	}
	cid = chunkID(t & 0x3f)
	format = formatType((t >> 6) & 0x03)

	if cid > 1 {
		return
	}

	// cid is overwritten below, so keep the 0 or 1 that selects the 2-byte or 3-byte form.
	marker := cid

	if err = binary.Read(v.r, binary.BigEndian, &t); err != nil {
		return format, cid, errors.Wrapf(err, "read basic header for cid=%v", cid)
	}
	cid = chunkID(64 + uint32(t))

	if marker == 1 {
		if err = binary.Read(v.r, binary.BigEndian, &t); err != nil {
			return format, cid, errors.Wrapf(err, "read basic header for cid=%v", cid)
		}
		cid += chunkID(uint32(t) * 256)
	}

	return
}

func (v *protocol) WritePacket(ctx context.Context, pkt Packet, streamID int) (err error) {
	m := newMessage()

	if m.payload, err = pkt.MarshalBinary(); err != nil {
		return errors.WithMessage(err, "marshal payload")
	}

	m.messageHeader.MessageType = pkt.Type()
	m.streamID = uint32(streamID)
	m.betterCid = pkt.BetterCid()

	if err = v.WriteMessage(ctx, m); err != nil {
		return errors.WithMessage(err, "write message")
	}

	if err = v.onPacketWriten(m, pkt); err != nil {
		return errors.WithMessage(err, "on write packet")
	}

	return
}

func (v *protocol) onPacketWriten(m *message, pkt Packet) (err error) {
	var tid amf0Number
	var name amf0String

	switch pkt := pkt.(type) {
	case *ConnectAppPacket:
		tid, name = pkt.TransactionID, pkt.CommandName
	case *CreateStreamPacket:
		tid, name = pkt.TransactionID, pkt.CommandName
	case *CallPacket:
		tid, name = pkt.TransactionID, pkt.CommandName
	}

	if tid > 0 && len(name) > 0 {
		v.input.ltransactions.Lock()
		defer v.input.ltransactions.Unlock()

		v.input.transactions[tid] = name
	}

	return
}

func (v *protocol) onMessageArrivated(m *message) (err error) {
	if m == nil {
		return
	}

	var pkt Packet
	switch m.MessageType() {
	case MessageTypeSetChunkSize, MessageTypeUserControl, MessageTypeWindowAcknowledgementSize:
		if pkt, err = v.DecodeMessage(m); err != nil {
			return errors.Errorf("decode message %v", m.MessageType())
		}
	}

	switch pkt := pkt.(type) {
	case *SetChunkSize:
		v.input.opt.chunkSize = pkt.ChunkSize
	}

	return
}

func (v *protocol) WriteMessage(ctx context.Context, m Message) (err error) {
	msg := m.asMessage()
	msg.payloadLength = uint32(len(msg.payload))

	var c0h, c3h []byte
	if c0h, err = msg.generateC0Header(); err != nil {
		return errors.WithMessage(err, "generate c0 header")
	}
	if c3h, err = msg.generateC3Header(); err != nil {
		return errors.WithMessage(err, "generate c3 header")
	}

	var h []byte
	p := msg.payload
	for len(p) > 0 {
		// TODO: We should convert buffered io to async io, because we will be stuck in block io here,
		// TODO: but the risk is acceptable because we literally will set the underlay io timeout.
		if ctx.Err() != nil {
			return ctx.Err()
		}

		if h == nil {
			h = c0h
		} else {
			h = c3h
		}

		if _, err = io.Copy(v.w, bytes.NewReader(h)); err != nil {
			return errors.Wrapf(err, "write c0c3 header %x", h)
		}

		size := len(p)
		if size > int(v.output.opt.chunkSize) {
			size = int(v.output.opt.chunkSize)
		}

		if _, err = io.Copy(v.w, bytes.NewReader(p[:size])); err != nil {
			return errors.Wrapf(err, "write chunk payload %vB", size)
		}
		p = p[size:]
	}

	// TODO: We should convert buffered io to async io, because we will be stuck in block io here,
	// TODO: but the risk is acceptable because we literally will set the underlay io timeout.
	if ctx.Err() != nil {
		return ctx.Err()
	}

	// TODO: FIXME: Use writev to write for high performance.
	if err = v.w.Flush(); err != nil {
		return errors.Wrapf(err, "flush writer")
	}

	return
}

// See rtmp_specification_1.0.pdf, page 30, section 4.1 Message Header.
type MessageType uint8

const (
	// See rtmp_specification_1.0.pdf, page 30, section 5 Protocol Control Messages: IDs 1
	// and 2 are for the chunk stream protocol, 3 to 6 for RTMP, and 7 between an edge server
	// and an origin server.
	MessageTypeSetChunkSize               MessageType = 0x01
	MessageTypeAbort                      MessageType = 0x02
	MessageTypeAcknowledgement            MessageType = 0x03
	MessageTypeUserControl                MessageType = 0x04
	MessageTypeWindowAcknowledgementSize  MessageType = 0x05
	MessageTypeSetPeerBandwidth           MessageType = 0x06
	MessageTypeEdgeAndOriginServerCommand MessageType = 0x07
	// See rtmp_specification_1.0.pdf, page 38, section 3 Types of messages, and page 41,
	// section 3.4 Audio message.
	MessageTypeAudio MessageType = 0x08
	// See rtmp_specification_1.0.pdf, page 41, section 3.5 Video message. Video messages
	// are large and can delay other messages, so the spec gives them the lowest priority.
	MessageTypeVideo MessageType = 0x09
	// See rtmp_specification_1.0.pdf, page 38, section 3.1 Command message.
	MessageTypeAMF3Command MessageType = 17
	MessageTypeAMF0Command MessageType = 20
	// See rtmp_specification_1.0.pdf, page 38, section 3.2 Data message.
	MessageTypeAMF0Data MessageType = 18
	MessageTypeAMF3Data MessageType = 15
)

type messageHeader struct {
	// 3 bytes, only used to decode a message from the chunk stream.
	timestampDelta uint32
	// 3 bytes, big-endian.
	payloadLength uint32
	// 1 byte.
	MessageType MessageType
	// 4 bytes, little-endian.
	streamID uint32

	betterCid chunkID

	// 4 bytes, big-endian. A uint64 for jitter detection and for a large timebase, such as
	// HLS.
	Timestamp uint64
}

// See rtmp_specification_1.0.pdf, page 30, section 4.1 Message Header.
type Message interface {
	MessageType() MessageType
	Timestamp() uint64
	Payload() []byte
	asMessage() *message
}

type message struct {
	messageHeader

	payload []byte
}

func NewMessage() Message {
	return newMessage()
}

func newMessage() *message {
	return &message{}
}

func NewStreamMessage(streamID int) Message {
	v := newMessage()
	v.streamID = uint32(streamID)
	v.betterCid = chunkIDOverStream
	return v
}

func (v *message) MessageType() MessageType {
	return v.messageHeader.MessageType
}

func (v *message) Timestamp() uint64 {
	return v.messageHeader.Timestamp
}

func (v *message) Payload() []byte {
	return v.payload
}

func (v *message) asMessage() *message {
	return v
}

func (v *message) generateC3Header() ([]byte, error) {
	var c3h []byte
	if v.messageHeader.Timestamp < extendedTimestamp {
		c3h = make([]byte, 1)
	} else {
		c3h = make([]byte, 1+4)
	}

	p := c3h
	p[0] = 0xc0 | byte(v.betterCid&0x3f)
	p = p[1:]

	// The RTMP spec puts no timestamp in the C3 header, but Adobe products, such as FMS, AMS,
	// Flash Player and FMLE, always send the extended timestamp in it.
	// @see: http://blog.csdn.net/win_lin/article/details/13363699
	if v.messageHeader.Timestamp >= extendedTimestamp {
		p[0] = byte(v.messageHeader.Timestamp >> 24)
		p[1] = byte(v.messageHeader.Timestamp >> 16)
		p[2] = byte(v.messageHeader.Timestamp >> 8)
		p[3] = byte(v.messageHeader.Timestamp)
	}

	return c3h, nil
}

func (v *message) generateC0Header() ([]byte, error) {
	var c0h []byte
	if v.messageHeader.Timestamp < extendedTimestamp {
		c0h = make([]byte, 1+3+3+1+4)
	} else {
		c0h = make([]byte, 1+3+3+1+4+4)
	}

	p := c0h
	p[0] = byte(v.betterCid) & 0x3f
	p = p[1:]

	if v.messageHeader.Timestamp < extendedTimestamp {
		p[0] = byte(v.messageHeader.Timestamp >> 16)
		p[1] = byte(v.messageHeader.Timestamp >> 8)
		p[2] = byte(v.messageHeader.Timestamp)
	} else {
		p[0] = 0xff
		p[1] = 0xff
		p[2] = 0xff
	}
	p = p[3:]

	p[0] = byte(v.payloadLength >> 16)
	p[1] = byte(v.payloadLength >> 8)
	p[2] = byte(v.payloadLength)
	p = p[3:]

	p[0] = byte(v.messageHeader.MessageType)
	p = p[1:]

	p[0] = byte(v.streamID)
	p[1] = byte(v.streamID >> 8)
	p[2] = byte(v.streamID >> 16)
	p[3] = byte(v.streamID >> 24)
	p = p[4:]

	if v.messageHeader.Timestamp >= extendedTimestamp {
		p[0] = byte(v.messageHeader.Timestamp >> 24)
		p[1] = byte(v.messageHeader.Timestamp >> 16)
		p[2] = byte(v.messageHeader.Timestamp >> 8)
		p[3] = byte(v.messageHeader.Timestamp)
	}

	return c0h, nil
}

// The cs id of rtmp_specification_1.0.pdf, page 17, section 6.1.1 Chunk Basic Header.
type chunkID uint32

const (
	chunkIDProtocolControl chunkID = 0x02
	chunkIDOverConnection  chunkID = 0x03
	chunkIDOverConnection2 chunkID = 0x04
	chunkIDOverStream      chunkID = 0x05
	chunkIDOverStream2     chunkID = 0x06
	chunkIDVideo           chunkID = 0x07
	chunkIDAudio           chunkID = 0x08
)

const (
	commandConnect          amf0String = amf0String("connect")
	commandCreateStream     amf0String = amf0String("createStream")
	commandCloseStream      amf0String = amf0String("closeStream")
	commandPlay             amf0String = amf0String("play")
	commandPause            amf0String = amf0String("pause")
	commandOnBWDone         amf0String = amf0String("onBWDone")
	commandOnStatus         amf0String = amf0String("onStatus")
	commandResult           amf0String = amf0String("_result")
	commandError            amf0String = amf0String("_error")
	commandReleaseStream    amf0String = amf0String("releaseStream")
	commandFCPublish        amf0String = amf0String("FCPublish")
	commandFCUnpublish      amf0String = amf0String("FCUnpublish")
	commandPublish          amf0String = amf0String("publish")
	commandRtmpSampleAccess amf0String = amf0String("|RtmpSampleAccess")
)

type Packet interface {
	Size() int
	encoding.BinaryUnmarshaler
	encoding.BinaryMarshaler

	BetterCid() chunkID
	Type() MessageType
}

type objectCallPacket struct {
	CommandName   amf0String
	TransactionID amf0Number
	CommandObject Amf0Object
	Args          Amf0Object
}

func (v *objectCallPacket) BetterCid() chunkID {
	return chunkIDOverConnection
}

func (v *objectCallPacket) Type() MessageType {
	return MessageTypeAMF0Command
}

func (v *objectCallPacket) Size() int {
	size := v.CommandName.Size() + v.TransactionID.Size() + v.CommandObject.Size()
	if v.Args != nil {
		size += v.Args.Size()
	}
	return size
}

func (v *objectCallPacket) UnmarshalBinary(data []byte) (err error) {
	p := data

	if err = v.CommandName.UnmarshalBinary(p); err != nil {
		return errors.WithMessage(err, "unmarshal command name")
	}
	p = p[v.CommandName.Size():]

	if err = v.TransactionID.UnmarshalBinary(p); err != nil {
		return errors.WithMessage(err, "unmarshal tid")
	}
	p = p[v.TransactionID.Size():]

	if err = v.CommandObject.UnmarshalBinary(p); err != nil {
		return errors.WithMessage(err, "unmarshal command")
	}
	p = p[v.CommandObject.Size():]

	if len(p) == 0 {
		return
	}

	v.Args = newAmf0Object()
	if err = v.Args.UnmarshalBinary(p); err != nil {
		return errors.WithMessage(err, "unmarshal args")
	}

	return
}

func (v *objectCallPacket) MarshalBinary() (data []byte, err error) {
	var pb []byte
	if pb, err = v.CommandName.MarshalBinary(); err != nil {
		return nil, errors.WithMessage(err, "marshal command name")
	}
	data = append(data, pb...)

	if pb, err = v.TransactionID.MarshalBinary(); err != nil {
		return nil, errors.WithMessage(err, "marshal tid")
	}
	data = append(data, pb...)

	if pb, err = v.CommandObject.MarshalBinary(); err != nil {
		return nil, errors.WithMessage(err, "marshal command object")
	}
	data = append(data, pb...)

	if v.Args != nil {
		if pb, err = v.Args.MarshalBinary(); err != nil {
			return nil, errors.WithMessage(err, "marshal args")
		}
		data = append(data, pb...)
	}

	return
}

// See rtmp_specification_1.0.pdf, page 45, section 4.1.1 connect.
type ConnectAppPacket struct {
	objectCallPacket
}

func NewConnectAppPacket() *ConnectAppPacket {
	v := &ConnectAppPacket{}
	v.CommandName = commandConnect
	v.CommandObject = NewAmf0Object()
	v.TransactionID = amf0Number(1.0)
	return v
}

func (v *ConnectAppPacket) UnmarshalBinary(data []byte) (err error) {
	if err = v.objectCallPacket.UnmarshalBinary(data); err != nil {
		return errors.WithMessage(err, "unmarshal call")
	}

	if v.CommandName != commandConnect {
		return errors.Errorf("Invalid command name %v", string(v.CommandName))
	}

	if v.TransactionID != 1.0 {
		return errors.Errorf("Invalid transaction ID %v", float64(v.TransactionID))
	}

	return
}

func (v *ConnectAppPacket) TcUrl() string {
	if v.CommandObject != nil {
		if v, ok := v.CommandObject.Get("tcUrl").(Amf0String); ok {
			return v.String()
		}
	}
	return ""
}

type ConnectAppResPacket struct {
	objectCallPacket
}

func NewConnectAppResPacket(tid amf0Number) *ConnectAppResPacket {
	v := &ConnectAppResPacket{}
	v.CommandName = commandResult
	v.CommandObject = NewAmf0Object()
	v.Args = NewAmf0Object()
	v.TransactionID = tid
	return v
}

func (v *ConnectAppResPacket) SrsID() string {
	if v.Args != nil {
		if v, ok := v.Args.Get("data").(Amf0EcmaArray); ok {
			if v, ok := v.Get("srs_id").(Amf0String); ok {
				return v.String()
			}
		}
	}
	return ""
}

func (v *ConnectAppResPacket) UnmarshalBinary(data []byte) (err error) {
	if err = v.objectCallPacket.UnmarshalBinary(data); err != nil {
		return errors.WithMessage(err, "unmarshal call")
	}

	if v.CommandName != commandResult {
		return errors.Errorf("Invalid command name %v", string(v.CommandName))
	}

	return
}

type variantCallPacket struct {
	CommandName   amf0String
	TransactionID amf0Number
	CommandObject Amf0Any // object or null
}

func (v *variantCallPacket) BetterCid() chunkID {
	return chunkIDOverConnection
}

func (v *variantCallPacket) Type() MessageType {
	return MessageTypeAMF0Command
}

func (v *variantCallPacket) Size() int {
	size := v.CommandName.Size() + v.TransactionID.Size()

	if v.CommandObject != nil {
		size += v.CommandObject.Size()
	}

	return size
}

func (v *variantCallPacket) UnmarshalBinary(data []byte) (err error) {
	p := data

	if err = v.CommandName.UnmarshalBinary(p); err != nil {
		return errors.WithMessage(err, "unmarshal command name")
	}
	p = p[v.CommandName.Size():]

	if err = v.TransactionID.UnmarshalBinary(p); err != nil {
		return errors.WithMessage(err, "unmarshal tid")
	}
	p = p[v.TransactionID.Size():]

	// A constructor may preset a default, such as null. When the object is absent from the
	// wire, a stale default makes Size count bytes that were never parsed, and the caller's
	// p[Size():] overruns on truncated input.
	v.CommandObject = nil
	if len(p) > 0 {
		if v.CommandObject, err = Amf0Discovery(p); err != nil {
			return errors.WithMessage(err, "discovery command object")
		}
		if err = v.CommandObject.UnmarshalBinary(p); err != nil {
			return errors.WithMessage(err, "unmarshal command object")
		}
		p = p[v.CommandObject.Size():]
	}

	return
}

func (v *variantCallPacket) MarshalBinary() (data []byte, err error) {
	var pb []byte
	if pb, err = v.CommandName.MarshalBinary(); err != nil {
		return nil, errors.WithMessage(err, "marshal command name")
	}
	data = append(data, pb...)

	if pb, err = v.TransactionID.MarshalBinary(); err != nil {
		return nil, errors.WithMessage(err, "marshal tid")
	}
	data = append(data, pb...)

	if v.CommandObject != nil {
		if pb, err = v.CommandObject.MarshalBinary(); err != nil {
			return nil, errors.WithMessage(err, "marshal command object")
		}
		data = append(data, pb...)
	}

	return
}

// See rtmp_specification_1.0.pdf, page 51, section 4.1.2 Call. onStatus is a call packet too.
type CallPacket struct {
	variantCallPacket
	Args Amf0Any // optional or object or null
}

func NewCallPacket() *CallPacket {
	return &CallPacket{}
}

func (v *CallPacket) ArgsCode() string {
	if v.Args != nil {
		if v, ok := v.Args.(Amf0Object); ok {
			if code, ok := v.Get("code").(Amf0String); ok {
				return code.String()
			}
		}
	}
	return ""
}

func (v *CallPacket) Size() int {
	size := v.variantCallPacket.Size()

	if v.Args != nil {
		size += v.Args.Size()
	}

	return size
}

// On malformed input, the decoded Size of a field can exceed the bytes present, so this
// turns a slice out-of-range panic into an error.
func advanceBytes(p []byte, n int) ([]byte, error) {
	if n < 0 || n > len(p) {
		return nil, errors.Errorf("advance %v exceeds remaining %v bytes", n, len(p))
	}
	return p[n:], nil
}

func (v *CallPacket) UnmarshalBinary(data []byte) (err error) {
	p := data

	if err = v.variantCallPacket.UnmarshalBinary(p); err != nil {
		return errors.WithMessage(err, "unmarshal call")
	}
	if p, err = advanceBytes(p, v.variantCallPacket.Size()); err != nil {
		return errors.WithMessage(err, "advance call")
	}

	// Reset, for the same reason as variantCallPacket.CommandObject.
	v.Args = nil
	if len(p) > 0 {
		if v.Args, err = Amf0Discovery(p); err != nil {
			return errors.WithMessage(err, "discovery args")
		}
		if err = v.Args.UnmarshalBinary(p); err != nil {
			return errors.WithMessage(err, "unmarshal args")
		}
	}

	return
}

func (v *CallPacket) MarshalBinary() (data []byte, err error) {
	var pb []byte
	if pb, err = v.variantCallPacket.MarshalBinary(); err != nil {
		return nil, errors.WithMessage(err, "marshal call")
	}
	data = append(data, pb...)

	if v.Args != nil {
		if pb, err = v.Args.MarshalBinary(); err != nil {
			return nil, errors.WithMessage(err, "marshal args")
		}
		data = append(data, pb...)
	}

	return
}

// See rtmp_specification_1.0.pdf, page 52, section 4.1.3 createStream.
type CreateStreamPacket struct {
	variantCallPacket
}

func NewCreateStreamPacket() *CreateStreamPacket {
	v := &CreateStreamPacket{}
	v.CommandName = commandCreateStream
	v.TransactionID = amf0Number(2)
	v.CommandObject = NewAmf0Null()
	return v
}

type CreateStreamResPacket struct {
	variantCallPacket
	StreamID amf0Number
}

func NewCreateStreamResPacket(tid amf0Number) *CreateStreamResPacket {
	v := &CreateStreamResPacket{}
	v.CommandName = commandResult
	v.TransactionID = tid
	v.CommandObject = NewAmf0Null()
	v.StreamID = 0
	return v
}

func (v *CreateStreamResPacket) SetStreamID(streamID int) {
	v.StreamID = amf0Number(streamID)
}

func (v *CreateStreamResPacket) Size() int {
	return v.variantCallPacket.Size() + v.StreamID.Size()
}

func (v *CreateStreamResPacket) UnmarshalBinary(data []byte) (err error) {
	p := data

	if err = v.variantCallPacket.UnmarshalBinary(p); err != nil {
		return errors.WithMessage(err, "unmarshal call")
	}
	if p, err = advanceBytes(p, v.variantCallPacket.Size()); err != nil {
		return errors.WithMessage(err, "advance call")
	}

	if err = v.StreamID.UnmarshalBinary(p); err != nil {
		return errors.WithMessage(err, "unmarshal sid")
	}

	return
}

func (v *CreateStreamResPacket) MarshalBinary() (data []byte, err error) {
	var pb []byte
	if pb, err = v.variantCallPacket.MarshalBinary(); err != nil {
		return nil, errors.WithMessage(err, "marshal call")
	}
	data = append(data, pb...)

	if pb, err = v.StreamID.MarshalBinary(); err != nil {
		return nil, errors.WithMessage(err, "marshal sid")
	}
	data = append(data, pb...)

	return
}

// See rtmp_specification_1.0.pdf, page 64, section 4.2.6 Publish.
type PublishPacket struct {
	variantCallPacket
	StreamName Amf0String
	StreamType Amf0String
}

func NewPublishPacket() *PublishPacket {
	v := &PublishPacket{}
	v.CommandName = commandPublish
	v.CommandObject = NewAmf0Null()
	v.StreamName = NewAmf0String("")
	v.StreamType = NewAmf0String("live")
	return v
}

func (v *PublishPacket) Size() int {
	return v.variantCallPacket.Size() + v.StreamName.Size() + v.StreamType.Size()
}

func (v *PublishPacket) UnmarshalBinary(data []byte) (err error) {
	p := data

	if err = v.variantCallPacket.UnmarshalBinary(p); err != nil {
		return errors.WithMessage(err, "unmarshal call")
	}
	if p, err = advanceBytes(p, v.variantCallPacket.Size()); err != nil {
		return errors.WithMessage(err, "advance call")
	}

	v.StreamName = newAmf0String("")
	if err = v.StreamName.UnmarshalBinary(p); err != nil {
		return errors.WithMessage(err, "unmarshal stream name")
	}
	p = p[v.StreamName.Size():]

	v.StreamType = newAmf0String("")
	if err = v.StreamType.UnmarshalBinary(p); err != nil {
		return errors.WithMessage(err, "unmarshal stream type")
	}

	return
}

func (v *PublishPacket) MarshalBinary() (data []byte, err error) {
	var pb []byte
	if pb, err = v.variantCallPacket.MarshalBinary(); err != nil {
		return nil, errors.WithMessage(err, "marshal call")
	}
	data = append(data, pb...)

	if pb, err = v.StreamName.MarshalBinary(); err != nil {
		return nil, errors.WithMessage(err, "marshal stream name")
	}
	data = append(data, pb...)

	if pb, err = v.StreamType.MarshalBinary(); err != nil {
		return nil, errors.WithMessage(err, "marshal stream type")
	}
	data = append(data, pb...)

	return
}

// See rtmp_specification_1.0.pdf, page 54, section 4.2.1 play.
type PlayPacket struct {
	variantCallPacket
	StreamName Amf0String
}

func NewPlayPacket() *PlayPacket {
	v := &PlayPacket{}
	v.CommandName = commandPlay
	v.CommandObject = NewAmf0Null()
	v.StreamName = NewAmf0String("")
	return v
}

func (v *PlayPacket) Size() int {
	return v.variantCallPacket.Size() + v.StreamName.Size()
}

func (v *PlayPacket) UnmarshalBinary(data []byte) (err error) {
	p := data

	if err = v.variantCallPacket.UnmarshalBinary(p); err != nil {
		return errors.WithMessage(err, "unmarshal call")
	}
	if p, err = advanceBytes(p, v.variantCallPacket.Size()); err != nil {
		return errors.WithMessage(err, "advance call")
	}

	v.StreamName = newAmf0String("")
	if err = v.StreamName.UnmarshalBinary(p); err != nil {
		return errors.WithMessage(err, "unmarshal stream name")
	}
	p = p[v.StreamName.Size():]

	return
}

func (v *PlayPacket) MarshalBinary() (data []byte, err error) {
	var pb []byte
	if pb, err = v.variantCallPacket.MarshalBinary(); err != nil {
		return nil, errors.WithMessage(err, "marshal call")
	}
	data = append(data, pb...)

	if pb, err = v.StreamName.MarshalBinary(); err != nil {
		return nil, errors.WithMessage(err, "marshal stream name")
	}
	data = append(data, pb...)

	return
}

// See rtmp_specification_1.0.pdf, page 31, section 5.1 Set Chunk Size.
type SetChunkSize struct {
	ChunkSize uint32
}

func NewSetChunkSize() *SetChunkSize {
	return &SetChunkSize{
		ChunkSize: defaultChunkSize,
	}
}

func (v *SetChunkSize) BetterCid() chunkID {
	return chunkIDProtocolControl
}

func (v *SetChunkSize) Type() MessageType {
	return MessageTypeSetChunkSize
}

func (v *SetChunkSize) Size() int {
	return 4
}

func (v *SetChunkSize) UnmarshalBinary(data []byte) (err error) {
	if len(data) < 4 {
		return errors.Errorf("requires 4 only %v bytes, %x", len(data), data)
	}
	v.ChunkSize = binary.BigEndian.Uint32(data)

	return
}

func (v *SetChunkSize) MarshalBinary() (data []byte, err error) {
	data = make([]byte, 4)
	binary.BigEndian.PutUint32(data, v.ChunkSize)

	return
}

// See rtmp_specification_1.0.pdf, page 33, section 5.5 Window Acknowledgement Size (5).
type WindowAcknowledgementSize struct {
	AckSize uint32
}

func NewWindowAcknowledgementSize() *WindowAcknowledgementSize {
	return &WindowAcknowledgementSize{}
}

func (v *WindowAcknowledgementSize) BetterCid() chunkID {
	return chunkIDProtocolControl
}

func (v *WindowAcknowledgementSize) Type() MessageType {
	return MessageTypeWindowAcknowledgementSize
}

func (v *WindowAcknowledgementSize) Size() int {
	return 4
}

func (v *WindowAcknowledgementSize) UnmarshalBinary(data []byte) (err error) {
	if len(data) < 4 {
		return errors.Errorf("requires 4 only %v bytes, %x", len(data), data)
	}
	v.AckSize = binary.BigEndian.Uint32(data)

	return
}

func (v *WindowAcknowledgementSize) MarshalBinary() (data []byte, err error) {
	data = make([]byte, 4)
	binary.BigEndian.PutUint32(data, v.AckSize)

	return
}

// The Limit type of rtmp_specification_1.0.pdf, page 33, section 5.6 Set Peer Bandwidth (6).
type LimitType uint8

const (
	LimitTypeHard LimitType = iota
	LimitTypeSoft
	LimitTypeDynamic
)

// See rtmp_specification_1.0.pdf, page 33, section 5.6 Set Peer Bandwidth (6).
type SetPeerBandwidth struct {
	Bandwidth uint32
	LimitType LimitType
}

func NewSetPeerBandwidth() *SetPeerBandwidth {
	return &SetPeerBandwidth{}
}

func (v *SetPeerBandwidth) BetterCid() chunkID {
	return chunkIDProtocolControl
}

func (v *SetPeerBandwidth) Type() MessageType {
	return MessageTypeSetPeerBandwidth
}

func (v *SetPeerBandwidth) Size() int {
	return 4 + 1
}

func (v *SetPeerBandwidth) UnmarshalBinary(data []byte) (err error) {
	if len(data) < 5 {
		return errors.Errorf("requires 5 only %v bytes, %x", len(data), data)
	}
	v.Bandwidth = binary.BigEndian.Uint32(data)
	v.LimitType = LimitType(data[4])

	return
}

func (v *SetPeerBandwidth) MarshalBinary() (data []byte, err error) {
	data = make([]byte, 5)
	binary.BigEndian.PutUint32(data, v.Bandwidth)
	data[4] = byte(v.LimitType)

	return
}

type EventType uint16

// The event data is a 4-byte stream ID, unless noted.
const (
	// Sent by the server on stream 0 after the connect command by default.
	EventTypeStreamBegin = 0x00

	// The playback is over; the client discards the messages it receives for the stream.
	EventTypeStreamEOF = 0x01

	// No message on the stream for a while.
	EventTypeStreamDry = 0x02

	// Sent by the client before the server processes the stream. The event data is the
	// 4-byte stream ID, then the 4-byte buffer length in ms.
	EventTypeSetBufferLength = 0x03

	EventTypeStreamIsRecorded = 0x04

	// The event data is a 4-byte timestamp, the server time when sent. The client answers
	// with EventTypePingResponse, which echoes the timestamp.
	EventTypePingRequest = 0x06

	EventTypePingResponse = 0x07

	// A FMS control event with a 3-byte payload, such as "00 1A 01": the event data is only
	// 1 byte.
	EventTypeFmsEvent0 = 0x1a
)

// See rtmp_specification_1.0.pdf, page 32, section 5.4 User Control Message (4).
type UserControl struct {
	EventType EventType
	EventData int32
	// The buffer length for EventTypeSetBufferLength, else 0.
	ExtraData int32
}

func NewUserControl() *UserControl {
	return &UserControl{}
}

func (v *UserControl) BetterCid() chunkID {
	return chunkIDProtocolControl
}

func (v *UserControl) Type() MessageType {
	return MessageTypeUserControl
}

func (v *UserControl) Size() int {
	size := 2

	if v.EventType == EventTypeFmsEvent0 {
		size += 1
	} else {
		size += 4
	}

	if v.EventType == EventTypeSetBufferLength {
		size += 4
	}

	return size
}

func (v *UserControl) UnmarshalBinary(data []byte) (err error) {
	if len(data) < 3 {
		return errors.Errorf("requires 5 only %v bytes, %x", len(data), data)
	}

	v.EventType = EventType(binary.BigEndian.Uint16(data))
	if len(data) < v.Size() {
		return errors.Errorf("requires %v only %v bytes, %x", v.Size(), len(data), data)
	}

	if v.EventType == EventTypeFmsEvent0 {
		v.EventData = int32(uint8(data[2]))
	} else {
		v.EventData = int32(binary.BigEndian.Uint32(data[2:]))
	}

	if v.EventType == EventTypeSetBufferLength {
		v.ExtraData = int32(binary.BigEndian.Uint32(data[6:]))
	}

	return
}

func (v *UserControl) MarshalBinary() (data []byte, err error) {
	data = make([]byte, v.Size())
	binary.BigEndian.PutUint16(data, uint16(v.EventType))

	if v.EventType == EventTypeFmsEvent0 {
		data[2] = uint8(v.EventData)
	} else {
		binary.BigEndian.PutUint32(data[2:], uint32(v.EventData))
	}

	if v.EventType == EventTypeSetBufferLength {
		binary.BigEndian.PutUint32(data[6:], uint32(v.ExtraData))
	}

	return
}
