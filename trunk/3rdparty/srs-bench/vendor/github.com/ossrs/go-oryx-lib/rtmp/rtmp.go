// The MIT License (MIT)
//
// Copyright (c) 2013-2017 Oryx(ossrs)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to
// use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
// the Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
// FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
// COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
// IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
// CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// The oryx rtmp package support bytes from/to rtmp packets.
package rtmp

import (
	"bufio"
	"bytes"
	"encoding"
	"encoding/binary"
	"fmt"
	"github.com/ossrs/go-oryx-lib/amf0"
	oe "github.com/ossrs/go-oryx-lib/errors"
	"io"
	"math/rand"
	"reflect"
	"sync"
)

// The handshake implements the RTMP handshake protocol.
type Handshake struct {
	r *rand.Rand
}

func NewHandshake(r *rand.Rand) *Handshake {
	return &Handshake{r: r}
}

func (v *Handshake) WriteC0S0(w io.Writer) (err error) {
	r := bytes.NewReader([]byte{0x03})
	if _, err = io.Copy(w, r); err != nil {
		return oe.Wrap(err, "write c0s0")
	}

	return
}

func (v *Handshake) ReadC0S0(r io.Reader) (c0 []byte, err error) {
	b := &bytes.Buffer{}
	if _, err = io.CopyN(b, r, 1); err != nil {
		return nil, oe.Wrap(err, "read c0s0")
	}

	c0 = b.Bytes()

	return
}

func (v *Handshake) WriteC1S1(w io.Writer) (err error) {
	p := make([]byte, 1536)

	for i := 8; i < len(p); i++ {
		p[i] = byte(v.r.Int())
	}

	r := bytes.NewReader(p)
	if _, err = io.Copy(w, r); err != nil {
		return oe.Wrap(err, "write c0s1")
	}

	return
}

func (v *Handshake) ReadC1S1(r io.Reader) (c1 []byte, err error) {
	b := &bytes.Buffer{}
	if _, err = io.CopyN(b, r, 1536); err != nil {
		return nil, oe.Wrap(err, "read c1s1")
	}

	c1 = b.Bytes()

	return
}

func (v *Handshake) WriteC2S2(w io.Writer, s1c1 []byte) (err error) {
	r := bytes.NewReader(s1c1[:])
	if _, err = io.Copy(w, r); err != nil {
		return oe.Wrap(err, "write c2s2")
	}

	return
}

func (v *Handshake) ReadC2S2(r io.Reader) (c2 []byte, err error) {
	b := &bytes.Buffer{}
	if _, err = io.CopyN(b, r, 1536); err != nil {
		return nil, oe.Wrap(err, "read c2s2")
	}

	c2 = b.Bytes()

	return
}

// Please read @doc rtmp_specification_1.0.pdf, @page 16, @section 6.1. Chunk Format
// Extended timestamp: 0 or 4 bytes
// This field MUST be sent when the normal timsestamp is set to
// 0xffffff, it MUST NOT be sent if the normal timestamp is set to
// anything else. So for values less than 0xffffff the normal
// timestamp field SHOULD be used in which case the extended timestamp
// MUST NOT be present. For values greater than or equal to 0xffffff
// the normal timestamp field MUST NOT be used and MUST be set to
// 0xffffff and the extended timestamp MUST be sent.
const extendedTimestamp = uint64(0xffffff)

// The default chunk size of RTMP is 128 bytes.
const defaultChunkSize = 128

// The intput or output settings for RTMP protocol.
type settings struct {
	chunkSize uint32
}

func newSettings() *settings {
	return &settings{
		chunkSize: defaultChunkSize,
	}
}

// The chunk stream which transport a message once.
type chunkStream struct {
	format            formatType
	cid               chunkID
	header            messageHeader
	message           *Message
	count             uint64
	extendedTimestamp bool
}

func newChunkStream() *chunkStream {
	return &chunkStream{}
}

// The protocol implements the RTMP command and chunk stack.
type Protocol struct {
	r     *bufio.Reader
	w     *bufio.Writer
	input struct {
		opt    *settings
		chunks map[chunkID]*chunkStream

		transactions  map[amf0.Number]amf0.String
		ltransactions sync.Mutex
	}
	output struct {
		opt *settings
	}
}

func NewProtocol(rw io.ReadWriter) *Protocol {
	v := &Protocol{
		r: bufio.NewReader(rw),
		w: bufio.NewWriter(rw),
	}

	v.input.opt = newSettings()
	v.input.chunks = map[chunkID]*chunkStream{}
	v.input.transactions = map[amf0.Number]amf0.String{}

	v.output.opt = newSettings()

	return v
}

func (v *Protocol) ExpectPacket(ppkt interface{}) (m *Message, err error) {
	// ppkt must be a **ptr, the elem is *ptr used to check the assignable.
	ppktt := reflect.TypeOf(ppkt).Elem()
	ppktv := reflect.ValueOf(ppkt)

	if required := reflect.TypeOf((*Packet)(nil)).Elem(); !ppktt.Implements(required) {
		return nil, oe.Errorf("%v not implements %v", ppktt, required)
	}

	for {
		if m, err = v.ReadMessage(); err != nil {
			return nil, oe.WithMessage(err, "read message")
		}

		var pkt Packet
		if pkt, err = v.DecodeMessage(m); err != nil {
			return nil, oe.WithMessage(err, "decode message")
		}

		var pktt reflect.Type
		if pktt = reflect.TypeOf(pkt); !pktt.AssignableTo(ppktt) {
			continue
		}

		// It's similar to *ppktv = pkt.
		ppktv.Elem().Set(reflect.ValueOf(pkt))
		break
	}

	return
}

func (v *Protocol) ExpectMessage(types ...MessageType) (m *Message, err error) {
	for {
		if m, err = v.ReadMessage(); err != nil {
			return nil, oe.WithMessage(err, "read message")
		}

		if len(types) == 0 {
			return
		}

		for _, t := range types {
			if m.MessageType == t {
				return
			}
		}
	}

	return
}

func (v *Protocol) parseAMFObject(p []byte) (pkt Packet, err error) {
	var commandName amf0.String
	if err = commandName.UnmarshalBinary(p); err != nil {
		return nil, oe.WithMessage(err, "unmarshal command name")
	}

	switch commandName {
	case commandResult, commandError:
		var transactionID amf0.Number
		if err = transactionID.UnmarshalBinary(p[commandName.Size():]); err != nil {
			return nil, oe.WithMessage(err, "unmarshal tid")
		}

		var requestName amf0.String
		if err = func() error {
			v.input.ltransactions.Lock()
			defer v.input.ltransactions.Unlock()

			var ok bool
			if requestName, ok = v.input.transactions[transactionID]; !ok {
				return oe.Errorf("No matched request for tid=%v", transactionID)
			}
			delete(v.input.transactions, transactionID)

			return nil
		}(); err != nil {
			return nil, oe.WithMessage(err, "discovery request name")
		}

		switch requestName {
		case commandConnect:
			return NewConnectAppResPacket(transactionID), nil
		case commandCreateStream:
			return NewCreateStreamResPacket(transactionID), nil
		default:
			return nil, oe.Errorf("No request for %v", string(requestName))
		}
	case commandConnect:
		return NewConnectAppPacket(), nil
	case commandPublish:
		return NewPublishPacket(), nil
	default:
		return NewCallPacket(), nil
	}
}

func (v *Protocol) DecodeMessage(m *Message) (pkt Packet, err error) {
	p := m.Payload[:]
	if len(p) == 0 {
		return nil, oe.New("Empty packet")
	}

	switch m.MessageType {
	case MessageTypeAMF3Command, MessageTypeAMF3Data:
		p = p[1:]
	}

	switch m.MessageType {
	case MessageTypeSetChunkSize:
		pkt = NewSetChunkSize()
	case MessageTypeWindowAcknowledgementSize:
		pkt = NewWindowAcknowledgementSize()
	case MessageTypeSetPeerBandwidth:
		pkt = NewSetPeerBandwidth()
	case MessageTypeAMF0Command, MessageTypeAMF3Command, MessageTypeAMF0Data, MessageTypeAMF3Data:
		if pkt, err = v.parseAMFObject(p); err != nil {
			return nil, oe.WithMessage(err, fmt.Sprintf("Parse AMF %v", m.MessageType))
		}
	case MessageTypeUserControl:
		pkt = NewUserControl()
	default:
		return nil, oe.Errorf("Unknown message %v", m.MessageType)
	}

	if err = pkt.UnmarshalBinary(p); err != nil {
		return nil, oe.WithMessage(err, fmt.Sprintf("Unmarshal %v", m.MessageType))
	}

	return
}

func (v *Protocol) ReadMessage() (m *Message, err error) {
	for m == nil {
		var cid chunkID
		var format formatType
		if format, cid, err = v.readBasicHeader(); err != nil {
			return nil, oe.WithMessage(err, "read basic header")
		}

		var ok bool
		var chunk *chunkStream
		if chunk, ok = v.input.chunks[cid]; !ok {
			chunk = newChunkStream()
			v.input.chunks[cid] = chunk
			chunk.header.betterCid = cid
		}

		if err = v.readMessageHeader(chunk, format); err != nil {
			return nil, oe.WithMessage(err, "read message header")
		}

		if m, err = v.readMessagePayload(chunk); err != nil {
			return nil, oe.WithMessage(err, "read message payload")
		}

		if err = v.onMessageArrivated(m); err != nil {
			return nil, oe.WithMessage(err, "on message")
		}
	}

	return
}

func (v *Protocol) readMessagePayload(chunk *chunkStream) (m *Message, err error) {
	// Empty payload message.
	if chunk.message.payloadLength == 0 {
		m = chunk.message
		chunk.message = nil
		return
	}

	// Calculate the chunk payload size.
	chunkedPayloadSize := int(chunk.message.payloadLength) - len(chunk.message.Payload)
	if chunkedPayloadSize > int(v.input.opt.chunkSize) {
		chunkedPayloadSize = int(v.input.opt.chunkSize)
	}

	b := make([]byte, chunkedPayloadSize)
	if _, err = io.ReadFull(v.r, b); err != nil {
		return nil, oe.Wrapf(err, "read chunk %vB", chunkedPayloadSize)
	}
	chunk.message.Payload = append(chunk.message.Payload, b...)

	// Got entire RTMP message?
	if int(chunk.message.payloadLength) == len(chunk.message.Payload) {
		m = chunk.message
		chunk.message = nil
	}

	return
}

// Please read @doc rtmp_specification_1.0.pdf, @page 18, @section 6.1.2. Chunk Message Header
// There are four different formats for the chunk message header,
// selected by the "fmt" field in the chunk basic header.
type formatType uint8

const (
	// 6.1.2.1. Type 0
	// Chunks of Type 0 are 11 bytes long. This type MUST be used at the
	// start of a chunk stream, and whenever the stream timestamp goes
	// backward (e.g., because of a backward seek).
	formatType0 formatType = iota
	// 6.1.2.2. Type 1
	// Chunks of Type 1 are 7 bytes long. The message stream ID is not
	// included; this chunk takes the same stream ID as the preceding chunk.
	// Streams with variable-sized messages (for example, many video
	// formats) SHOULD use this format for the first chunk of each new
	// message after the first.
	formatType1
	// 6.1.2.3. Type 2
	// Chunks of Type 2 are 3 bytes long. Neither the stream ID nor the
	// message length is included; this chunk has the same stream ID and
	// message length as the preceding chunk. Streams with constant-sized
	// messages (for example, some audio and data formats) SHOULD use this
	// format for the first chunk of each message after the first.
	formatType2
	// 6.1.2.4. Type 3
	// Chunks of Type 3 have no header. Stream ID, message length and
	// timestamp delta are not present; chunks of this type take values from
	// the preceding chunk. When a single message is split into chunks, all
	// chunks of a message except the first one, SHOULD use this type. Refer
	// to example 2 in section 6.2.2. Stream consisting of messages of
	// exactly the same size, stream ID and spacing in time SHOULD use this
	// type for all chunks after chunk of Type 2. Refer to example 1 in
	// section 6.2.1. If the delta between the first message and the second
	// message is same as the time stamp of first message, then chunk of
	// type 3 would immediately follow the chunk of type 0 as there is no
	// need for a chunk of type 2 to register the delta. If Type 3 chunk
	// follows a Type 0 chunk, then timestamp delta for this Type 3 chunk is
	// the same as the timestamp of Type 0 chunk.
	formatType3
)

// The message header size, index is format.
var messageHeaderSizes = []int{11, 7, 3, 0}

// Parse the chunk message header.
//   3bytes: timestamp delta,    fmt=0,1,2
//   3bytes: payload length,     fmt=0,1
//   1bytes: message type,       fmt=0,1
//   4bytes: stream id,          fmt=0
// where:
//   fmt=0, 0x0X
//   fmt=1, 0x4X
//   fmt=2, 0x8X
//   fmt=3, 0xCX
func (v *Protocol) readMessageHeader(chunk *chunkStream, format formatType) (err error) {
	// We should not assert anything about fmt, for the first packet.
	// (when first packet, the chunk.message is nil).
	// the fmt maybe 0/1/2/3, the FMLE will send a 0xC4 for some audio packet.
	// the previous packet is:
	//     04                // fmt=0, cid=4
	//     00 00 1a          // timestamp=26
	//     00 00 9d          // payload_length=157
	//     08                // message_type=8(audio)
	//     01 00 00 00       // stream_id=1
	// the current packet maybe:
	//     c4             // fmt=3, cid=4
	// it's ok, for the packet is audio, and timestamp delta is 26.
	// the current packet must be parsed as:
	//     fmt=0, cid=4
	//     timestamp=26+26=52
	//     payload_length=157
	//     message_type=8(audio)
	//     stream_id=1
	// so we must update the timestamp even fmt=3 for first packet.
	//
	// The fresh packet used to update the timestamp even fmt=3 for first packet.
	// fresh packet always means the chunk is the first one of message.
	var isFirstChunkOfMsg bool
	if chunk.message == nil {
		isFirstChunkOfMsg = true
	}

	// But, we can ensure that when a chunk stream is fresh,
	// the fmt must be 0, a new stream.
	if chunk.count == 0 && format != formatType0 {
		// For librtmp, if ping, it will send a fresh stream with fmt=1,
		// 0x42             where: fmt=1, cid=2, protocol contorl user-control message
		// 0x00 0x00 0x00   where: timestamp=0
		// 0x00 0x00 0x06   where: payload_length=6
		// 0x04             where: message_type=4(protocol control user-control message)
		// 0x00 0x06            where: event Ping(0x06)
		// 0x00 0x00 0x0d 0x0f  where: event data 4bytes ping timestamp.
		// @see: https://github.com/ossrs/srs/issues/98
		if chunk.cid == chunkIDProtocolControl && format == formatType1 {
			// We accept cid=2, fmt=1 to make librtmp happy.
		} else {
			return oe.Errorf("For fresh chunk, fmt %v != %v(required), cid is %v", format, formatType0, chunk.cid)
		}
	}

	// When exists cache msg, means got an partial message,
	// the fmt must not be type0 which means new message.
	if chunk.message != nil && format == formatType0 {
		return oe.Errorf("For exists chunk, fmt is %v, cid is %v", format, chunk.cid)
	}

	// Create msg when new chunk stream start
	if chunk.message == nil {
		chunk.message = NewMessage()
	}

	// Read the message header.
	p := make([]byte, messageHeaderSizes[format])
	if _, err = io.ReadFull(v.r, p); err != nil {
		return oe.Wrapf(err, "read %vB message header", len(p))
	}

	// Prse the message header.
	//   3bytes: timestamp delta,    fmt=0,1,2
	//   3bytes: payload length,     fmt=0,1
	//   1bytes: message type,       fmt=0,1
	//   4bytes: stream id,          fmt=0
	// where:
	//   fmt=0, 0x0X
	//   fmt=1, 0x4X
	//   fmt=2, 0x8X
	//   fmt=3, 0xCX
	if format <= formatType2 {
		chunk.header.timestampDelta = uint32(p[0])<<16 | uint32(p[1])<<8 | uint32(p[2])
		p = p[3:]

		// fmt: 0
		// timestamp: 3 bytes
		// If the timestamp is greater than or equal to 16777215
		// (hexadecimal 0x00ffffff), this value MUST be 16777215, and the
		// 'extended timestamp header' MUST be present. Otherwise, this value
		// SHOULD be the entire timestamp.
		//
		// fmt: 1 or 2
		// timestamp delta: 3 bytes
		// If the delta is greater than or equal to 16777215 (hexadecimal
		// 0x00ffffff), this value MUST be 16777215, and the 'extended
		// timestamp header' MUST be present. Otherwise, this value SHOULD be
		// the entire delta.
		chunk.extendedTimestamp = false
		if uint64(chunk.header.timestampDelta) >= extendedTimestamp {
			chunk.extendedTimestamp = true

			// Extended timestamp: 0 or 4 bytes
			// This field MUST be sent when the normal timsestamp is set to
			// 0xffffff, it MUST NOT be sent if the normal timestamp is set to
			// anything else. So for values less than 0xffffff the normal
			// timestamp field SHOULD be used in which case the extended timestamp
			// MUST NOT be present. For values greater than or equal to 0xffffff
			// the normal timestamp field MUST NOT be used and MUST be set to
			// 0xffffff and the extended timestamp MUST be sent.
			if format == formatType0 {
				// 6.1.2.1. Type 0
				// For a type-0 chunk, the absolute timestamp of the message is sent
				// here.
				chunk.header.Timestamp = uint64(chunk.header.timestampDelta)
			} else {
				// 6.1.2.2. Type 1
				// 6.1.2.3. Type 2
				// For a type-1 or type-2 chunk, the difference between the previous
				// chunk's timestamp and the current chunk's timestamp is sent here.
				chunk.header.Timestamp += uint64(chunk.header.timestampDelta)
			}
		}

		if format <= formatType1 {
			payloadLength := uint32(p[0])<<16 | uint32(p[1])<<8 | uint32(p[2])
			p = p[3:]

			// For a message, if msg exists in cache, the size must not changed.
			// always use the actual msg size to compare, for the cache payload length can changed,
			// for the fmt type1(stream_id not changed), user can change the payload
			// length(it's not allowed in the continue chunks).
			if !isFirstChunkOfMsg && chunk.header.payloadLength != payloadLength {
				return oe.Errorf("Chunk message size %v != %v(required)", payloadLength, chunk.header.payloadLength)
			}
			chunk.header.payloadLength = payloadLength

			chunk.header.MessageType = MessageType(p[0])
			p = p[1:]

			if format == formatType0 {
				chunk.header.streamID = uint32(p[0]) | uint32(p[1])<<8 | uint32(p[2])<<16 | uint32(p[3])<<24
				p = p[4:]
			}
		}
	} else {
		// Update the timestamp even fmt=3 for first chunk packet
		if isFirstChunkOfMsg && !chunk.extendedTimestamp {
			chunk.header.Timestamp += uint64(chunk.header.timestampDelta)
		}
	}

	// Read extended-timestamp
	if chunk.extendedTimestamp {
		var timestamp uint32
		if err = binary.Read(v.r, binary.BigEndian, &timestamp); err != nil {
			return oe.Wrapf(err, "read ext-ts, pkt-ts=%v", chunk.header.Timestamp)
		}

		// We always use 31bits timestamp, for some server may use 32bits extended timestamp.
		// @see https://github.com/ossrs/srs/issues/111
		timestamp &= 0x7fffffff

		// TODO: FIXME: Support detect the extended timestamp.
		// @see http://blog.csdn.net/win_lin/article/details/13363699
		chunk.header.Timestamp = uint64(timestamp)
	}

	// The extended-timestamp must be unsigned-int,
	//         24bits timestamp: 0xffffff = 16777215ms = 16777.215s = 4.66h
	//         32bits timestamp: 0xffffffff = 4294967295ms = 4294967.295s = 1193.046h = 49.71d
	// because the rtmp protocol says the 32bits timestamp is about "50 days":
	//         3. Byte Order, Alignment, and Time Format
	//                Because timestamps are generally only 32 bits long, they will roll
	//                over after fewer than 50 days.
	//
	// but, its sample says the timestamp is 31bits:
	//         An application could assume, for example, that all
	//        adjacent timestamps are within 2^31 milliseconds of each other, so
	//        10000 comes after 4000000000, while 3000000000 comes before
	//        4000000000.
	// and flv specification says timestamp is 31bits:
	//        Extension of the Timestamp field to form a SI32 value. This
	//        field represents the upper 8 bits, while the previous
	//        Timestamp field represents the lower 24 bits of the time in
	//        milliseconds.
	// in a word, 31bits timestamp is ok.
	// convert extended timestamp to 31bits.
	chunk.header.Timestamp &= 0x7fffffff

	// Copy header to msg
	chunk.message.messageHeader = chunk.header

	// Increase the msg count, the chunk stream can accept fmt=1/2/3 message now.
	chunk.count++

	return
}

// Please read @doc rtmp_specification_1.0.pdf, @page 17, @section 6.1.1. Chunk Basic Header
// The Chunk Basic Header encodes the chunk stream ID and the chunk
// type(represented by fmt field in the figure below). Chunk type
// determines the format of the encoded message header. Chunk Basic
// Header field may be 1, 2, or 3 bytes, depending on the chunk stream
// ID.
//
// The bits 0-5 (least significant) in the chunk basic header represent
// the chunk stream ID.
//
// Chunk stream IDs 2-63 can be encoded in the 1-byte version of this
// field.
//    0 1 2 3 4 5 6 7
//   +-+-+-+-+-+-+-+-+
//   |fmt|   cs id   |
//   +-+-+-+-+-+-+-+-+
//   Figure 6 Chunk basic header 1
//
// Chunk stream IDs 64-319 can be encoded in the 2-byte version of this
// field. ID is computed as (the second byte + 64).
//   0                   1
//   0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5
//   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//   |fmt|    0      | cs id - 64    |
//   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//   Figure 7 Chunk basic header 2
//
// Chunk stream IDs 64-65599 can be encoded in the 3-byte version of
// this field. ID is computed as ((the third byte)*256 + the second byte
// + 64).
//    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3
//   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//   |fmt|     1     |         cs id - 64            |
//   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//   Figure 8 Chunk basic header 3
//
// cs id: 6 bits
// fmt: 2 bits
// cs id - 64: 8 or 16 bits
//
// Chunk stream IDs with values 64-319 could be represented by both 2-
// byte version and 3-byte version of this field.
func (v *Protocol) readBasicHeader() (format formatType, cid chunkID, err error) {
	// 2-63, 1B chunk header
	var t uint8
	if err = binary.Read(v.r, binary.BigEndian, &t); err != nil {
		return format, cid, oe.Wrap(err, "read basic header")
	}
	cid = chunkID(t & 0x3f)
	format = formatType((t >> 6) & 0x03)

	if cid > 1 {
		return
	}

	// 64-319, 2B chunk header
	if err = binary.Read(v.r, binary.BigEndian, &t); err != nil {
		return format, cid, oe.Wrapf(err, "read basic header for cid=%v", cid)
	}
	cid = chunkID(64 + uint32(t))

	// 64-65599, 3B chunk header
	if cid == 1 {
		if err = binary.Read(v.r, binary.BigEndian, &t); err != nil {
			return format, cid, oe.Wrapf(err, "read basic header for cid=%v", cid)
		}
		cid += chunkID(uint32(t) * 256)
	}

	return
}

func (v *Protocol) WritePacket(pkt Packet, streamID int) (err error) {
	m := NewMessage()

	if m.Payload, err = pkt.MarshalBinary(); err != nil {
		return oe.WithMessage(err, "marshal payload")
	}

	m.MessageType = pkt.Type()
	m.streamID = uint32(streamID)
	m.betterCid = pkt.BetterCid()

	if err = v.WriteMessage(m); err != nil {
		return oe.WithMessage(err, "write message")
	}

	if err = v.onPacketWriten(m, pkt); err != nil {
		return oe.WithMessage(err, "on write packet")
	}

	return
}

func (v *Protocol) onPacketWriten(m *Message, pkt Packet) (err error) {
	var tid amf0.Number
	var name amf0.String

	switch pkt := pkt.(type) {
	case *ConnectAppPacket:
		tid, name = pkt.TransactionID, pkt.CommandName
	case *CreateStreamPacket:
		tid, name = pkt.TransactionID, pkt.CommandName
	}

	if tid > 0 && len(name) > 0 {
		v.input.ltransactions.Lock()
		defer v.input.ltransactions.Unlock()

		v.input.transactions[tid] = name
	}

	return
}

func (v *Protocol) onMessageArrivated(m *Message) (err error) {
	var pkt Packet
	switch m.MessageType {
	case MessageTypeSetChunkSize, MessageTypeUserControl, MessageTypeWindowAcknowledgementSize:
		if pkt, err = v.DecodeMessage(m); err != nil {
			return oe.Errorf("decode message %v", m.MessageType)
		}
	}

	switch pkt := pkt.(type) {
	case *SetChunkSize:
		v.input.opt.chunkSize = pkt.ChunkSize
	}

	return
}

func (v *Protocol) WriteMessage(m *Message) (err error) {
	m.payloadLength = uint32(len(m.Payload))

	var c0h, c3h []byte
	if c0h, err = m.generateC0Header(); err != nil {
		return oe.WithMessage(err, "generate c0 header")
	}
	if c3h, err = m.generateC3Header(); err != nil {
		return oe.WithMessage(err, "generate c3 header")
	}

	var h []byte
	p := m.Payload
	for len(p) > 0 {
		if h == nil {
			h = c0h
		} else {
			h = c3h
		}

		if _, err = io.Copy(v.w, bytes.NewReader(h)); err != nil {
			return oe.Wrapf(err, "write c0c3 header %x", h)
		}

		size := len(p)
		if size > int(v.output.opt.chunkSize) {
			size = int(v.output.opt.chunkSize)
		}

		if _, err = io.Copy(v.w, bytes.NewReader(p[:size])); err != nil {
			return oe.Wrapf(err, "write chunk payload %vB", size)
		}
		p = p[size:]
	}

	// TODO: FIXME: Use writev to write for high performance.
	if err = v.w.Flush(); err != nil {
		return oe.Wrapf(err, "flush writer")
	}

	return
}

// Please read @doc rtmp_specification_1.0.pdf, @page 30, @section 4.1. Message Header
// 1byte. One byte field to represent the message type. A range of type IDs
// (1-7) are reserved for protocol control messages.
type MessageType uint8

const (
	// Please read @doc rtmp_specification_1.0.pdf, @page 30, @section 5. Protocol Control Messages
	// RTMP reserves message type IDs 1-7 for protocol control messages.
	// These messages contain information needed by the RTM Chunk Stream
	// protocol or RTMP itself. Protocol messages with IDs 1 & 2 are
	// reserved for usage with RTM Chunk Stream protocol. Protocol messages
	// with IDs 3-6 are reserved for usage of RTMP. Protocol message with ID
	// 7 is used between edge server and origin server.
	MessageTypeSetChunkSize               MessageType = 0x01
	MessageTypeAbort                      MessageType = 0x02 // 0x02
	MessageTypeAcknowledgement            MessageType = 0x03 // 0x03
	MessageTypeUserControl                MessageType = 0x04 // 0x04
	MessageTypeWindowAcknowledgementSize  MessageType = 0x05 // 0x05
	MessageTypeSetPeerBandwidth           MessageType = 0x06 // 0x06
	MessageTypeEdgeAndOriginServerCommand MessageType = 0x07 // 0x07
	// Please read @doc rtmp_specification_1.0.pdf, @page 38, @section 3. Types of messages
	// The server and the client send messages over the network to
	// communicate with each other. The messages can be of any type which
	// includes audio messages, video messages, command messages, shared
	// object messages, data messages, and user control messages.
	//
	// Please read @doc rtmp_specification_1.0.pdf, @page 41, @section 3.4. Audio message
	// The client or the server sends this message to send audio data to the
	// peer. The message type value of 8 is reserved for audio messages.
	MessageTypeAudio MessageType = 0x08
	// Please read @doc rtmp_specification_1.0.pdf, @page 41, @section 3.5. Video message
	// The client or the server sends this message to send video data to the
	// peer. The message type value of 9 is reserved for video messages.
	// These messages are large and can delay the sending of other type of
	// messages. To avoid such a situation, the video message is assigned
	// the lowest priority.
	MessageTypeVideo MessageType = 0x09 // 0x09
	// Please read @doc rtmp_specification_1.0.pdf, @page 38, @section 3.1. Command message
	// Command messages carry the AMF-encoded commands between the client
	// and the server. These messages have been assigned message type value
	// of 20 for AMF0 encoding and message type value of 17 for AMF3
	// encoding. These messages are sent to perform some operations like
	// connect, createStream, publish, play, pause on the peer. Command
	// messages like onstatus, result etc. are used to inform the sender
	// about the status of the requested commands. A command message
	// consists of command name, transaction ID, and command object that
	// contains related parameters. A client or a server can request Remote
	// Procedure Calls (RPC) over streams that are communicated using the
	// command messages to the peer.
	MessageTypeAMF3Command MessageType = 17 // 0x11
	MessageTypeAMF0Command MessageType = 20 // 0x14
	// Please read @doc rtmp_specification_1.0.pdf, @page 38, @section 3.2. Data message
	// The client or the server sends this message to send Metadata or any
	// user data to the peer. Metadata includes details about the
	// data(audio, video etc.) like creation time, duration, theme and so
	// on. These messages have been assigned message type value of 18 for
	// AMF0 and message type value of 15 for AMF3.
	MessageTypeAMF0Data MessageType = 18 // 0x12
	MessageTypeAMF3Data MessageType = 15 // 0x0f
)

// The header of message.
type messageHeader struct {
	// 3bytes.
	// Three-byte field that contains a timestamp delta of the message.
	// @remark, only used for decoding message from chunk stream.
	timestampDelta uint32
	// 3bytes.
	// Three-byte field that represents the size of the payload in bytes.
	// It is set in big-endian format.
	payloadLength uint32
	// 1byte.
	// One byte field to represent the message type. A range of type IDs
	// (1-7) are reserved for protocol control messages.
	MessageType MessageType
	// 4bytes.
	// Four-byte field that identifies the stream of the message. These
	// bytes are set in little-endian format.
	streamID uint32

	// The chunk stream id over which transport.
	betterCid chunkID

	// Four-byte field that contains a timestamp of the message.
	// The 4 bytes are packed in the big-endian order.
	// @remark, we use 64bits for large time for jitter detect and for large tbn like HLS.
	Timestamp uint64
}

// The RTMP message, transport over chunk stream in RTMP.
// Please read the cs id of @doc rtmp_specification_1.0.pdf, @page 30, @section 4.1. Message Header
type Message struct {
	messageHeader

	// The payload which carries the RTMP packet.
	Payload []byte
}

func NewMessage() *Message {
	return &Message{}
}

func NewStreamMessage(streamID int) *Message {
	v := NewMessage()
	v.streamID = uint32(streamID)
	v.betterCid = chunkIDOverStream
	return v
}

func (v *Message) generateC3Header() ([]byte, error) {
	var c3h []byte
	if v.Timestamp < extendedTimestamp {
		c3h = make([]byte, 1)
	} else {
		c3h = make([]byte, 1+4)
	}

	p := c3h
	p[0] = 0xc0 | byte(v.betterCid&0x3f)
	p = p[1:]

	// In RTMP protocol, there must not any timestamp in C3 header,
	// but actually all products from adobe, such as FMS/AMS and Flash player and FMLE,
	// always carry a extended timestamp in C3 header.
	// @see: http://blog.csdn.net/win_lin/article/details/13363699
	if v.Timestamp >= extendedTimestamp {
		p[0] = byte(v.Timestamp >> 24)
		p[1] = byte(v.Timestamp >> 16)
		p[2] = byte(v.Timestamp >> 8)
		p[3] = byte(v.Timestamp)
	}

	return c3h, nil
}

func (v *Message) generateC0Header() ([]byte, error) {
	var c0h []byte
	if v.Timestamp < extendedTimestamp {
		c0h = make([]byte, 1+3+3+1+4)
	} else {
		c0h = make([]byte, 1+3+3+1+4+4)
	}

	p := c0h
	p[0] = byte(v.betterCid) & 0x3f
	p = p[1:]

	if v.Timestamp < extendedTimestamp {
		p[0] = byte(v.Timestamp >> 16)
		p[1] = byte(v.Timestamp >> 8)
		p[2] = byte(v.Timestamp)
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

	p[0] = byte(v.MessageType)
	p = p[1:]

	p[0] = byte(v.streamID)
	p[1] = byte(v.streamID >> 8)
	p[2] = byte(v.streamID >> 16)
	p[3] = byte(v.streamID >> 24)
	p = p[4:]

	if v.Timestamp >= extendedTimestamp {
		p[0] = byte(v.Timestamp >> 24)
		p[1] = byte(v.Timestamp >> 16)
		p[2] = byte(v.Timestamp >> 8)
		p[3] = byte(v.Timestamp)
	}

	return c0h, nil
}

// Please read the cs id of @doc rtmp_specification_1.0.pdf, @page 17, @section 6.1.1. Chunk Basic Header
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

// The Command Name of message.
const (
	commandConnect          amf0.String = amf0.String("connect")
	commandCreateStream     amf0.String = amf0.String("createStream")
	commandCloseStream      amf0.String = amf0.String("closeStream")
	commandPlay             amf0.String = amf0.String("play")
	commandPause            amf0.String = amf0.String("pause")
	commandOnBWDone         amf0.String = amf0.String("onBWDone")
	commandOnStatus         amf0.String = amf0.String("onStatus")
	commandResult           amf0.String = amf0.String("_result")
	commandError            amf0.String = amf0.String("_error")
	commandReleaseStream    amf0.String = amf0.String("releaseStream")
	commandFCPublish        amf0.String = amf0.String("FCPublish")
	commandFCUnpublish      amf0.String = amf0.String("FCUnpublish")
	commandPublish          amf0.String = amf0.String("publish")
	commandRtmpSampleAccess amf0.String = amf0.String("|RtmpSampleAccess")
)

// The RTMP packet, transport as payload of RTMP message.
type Packet interface {
	// Marshaler and unmarshaler
	Size() int
	encoding.BinaryUnmarshaler
	encoding.BinaryMarshaler

	// RTMP protocol fields for each packet.
	BetterCid() chunkID
	Type() MessageType
}

// A Call packet, both object and args are AMF0 objects.
type objectCallPacket struct {
	CommandName   amf0.String
	TransactionID amf0.Number
	CommandObject *amf0.Object
	Args          *amf0.Object
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
		return oe.WithMessage(err, "unmarshal command name")
	}
	p = p[v.CommandName.Size():]

	if err = v.TransactionID.UnmarshalBinary(p); err != nil {
		return oe.WithMessage(err, "unmarshal tid")
	}
	p = p[v.TransactionID.Size():]

	if err = v.CommandObject.UnmarshalBinary(p); err != nil {
		return oe.WithMessage(err, "unmarshal command")
	}
	p = p[v.CommandObject.Size():]

	if len(p) == 0 {
		return
	}

	v.Args = amf0.NewObject()
	if err = v.Args.UnmarshalBinary(p); err != nil {
		return oe.WithMessage(err, "unmarshal args")
	}

	return
}

func (v *objectCallPacket) MarshalBinary() (data []byte, err error) {
	var pb []byte
	if pb, err = v.CommandName.MarshalBinary(); err != nil {
		return nil, oe.WithMessage(err, "marshal command name")
	}
	data = append(data, pb...)

	if pb, err = v.TransactionID.MarshalBinary(); err != nil {
		return nil, oe.WithMessage(err, "marshal tid")
	}
	data = append(data, pb...)

	if pb, err = v.CommandObject.MarshalBinary(); err != nil {
		return nil, oe.WithMessage(err, "marshal command object")
	}
	data = append(data, pb...)

	if v.Args != nil {
		if pb, err = v.Args.MarshalBinary(); err != nil {
			return nil, oe.WithMessage(err, "marshal args")
		}
		data = append(data, pb...)
	}

	return
}

// Please read @doc rtmp_specification_1.0.pdf, @page 45, @section 4.1.1. connect
// The client sends the connect command to the server to request
// connection to a server application instance.
type ConnectAppPacket struct {
	objectCallPacket
}

func NewConnectAppPacket() *ConnectAppPacket {
	v := &ConnectAppPacket{}
	v.CommandName = commandConnect
	v.CommandObject = amf0.NewObject()
	v.TransactionID = amf0.Number(1.0)
	return v
}

func (v *ConnectAppPacket) UnmarshalBinary(data []byte) (err error) {
	if err = v.objectCallPacket.UnmarshalBinary(data); err != nil {
		return oe.WithMessage(err, "unmarshal call")
	}

	if v.CommandName != commandConnect {
		return oe.Errorf("Invalid command name %v", string(v.CommandName))
	}

	if v.TransactionID != 1.0 {
		return oe.Errorf("Invalid transaction ID %v", float64(v.TransactionID))
	}

	return
}

// The response for ConnectAppPacket.
type ConnectAppResPacket struct {
	objectCallPacket
}

func NewConnectAppResPacket(tid amf0.Number) *ConnectAppResPacket {
	v := &ConnectAppResPacket{}
	v.CommandName = commandResult
	v.CommandObject = amf0.NewObject()
	v.TransactionID = tid
	return v
}

func (v *ConnectAppResPacket) UnmarshalBinary(data []byte) (err error) {
	if err = v.objectCallPacket.UnmarshalBinary(data); err != nil {
		return oe.WithMessage(err, "unmarshal call")
	}

	if v.CommandName != commandResult {
		return oe.Errorf("Invalid command name %v", string(v.CommandName))
	}

	return
}

// A Call object, command object is variant.
type variantCallPacket struct {
	CommandName   amf0.String
	TransactionID amf0.Number
	CommandObject amf0.Amf0 // object or null
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
		return oe.WithMessage(err, "unmarshal command name")
	}
	p = p[v.CommandName.Size():]

	if err = v.TransactionID.UnmarshalBinary(p); err != nil {
		return oe.WithMessage(err, "unmarshal tid")
	}
	p = p[v.TransactionID.Size():]

	if len(p) > 0 {
		if v.CommandObject, err = amf0.Discovery(p); err != nil {
			return oe.WithMessage(err, "discovery command object")
		}
		if err = v.CommandObject.UnmarshalBinary(p); err != nil {
			return oe.WithMessage(err, "unmarshal command object")
		}
		p = p[v.CommandObject.Size():]
	}

	return
}

func (v *variantCallPacket) MarshalBinary() (data []byte, err error) {
	var pb []byte
	if pb, err = v.CommandName.MarshalBinary(); err != nil {
		return nil, oe.WithMessage(err, "marshal command name")
	}
	data = append(data, pb...)

	if pb, err = v.TransactionID.MarshalBinary(); err != nil {
		return nil, oe.WithMessage(err, "marshal tid")
	}
	data = append(data, pb...)

	if v.CommandObject != nil {
		if pb, err = v.CommandObject.MarshalBinary(); err != nil {
			return nil, oe.WithMessage(err, "marshal command object")
		}
		data = append(data, pb...)
	}

	return
}

// Please read @doc rtmp_specification_1.0.pdf, @page 51, @section 4.1.2. Call
// The call method of the NetConnection object runs remote procedure
// calls (RPC) at the receiving end. The called RPC name is passed as a
// parameter to the call command.
// @remark onStatus packet is a call packet.
type CallPacket struct {
	variantCallPacket
	Args amf0.Amf0 // optional or object or null
}

func NewCallPacket() *CallPacket {
	return &CallPacket{}
}

func NewCloseStreamPacket() *CallPacket {
	v := NewCallPacket()
	v.CommandName = commandCloseStream
	v.CommandObject = amf0.NewNull()
	return v
}

func (v *CallPacket) Size() int {
	size := v.variantCallPacket.Size()

	if v.Args != nil {
		size += v.Args.Size()
	}

	return size
}

func (v *CallPacket) UnmarshalBinary(data []byte) (err error) {
	p := data

	if err = v.variantCallPacket.UnmarshalBinary(p); err != nil {
		return oe.WithMessage(err, "unmarshal call")
	}
	p = p[v.variantCallPacket.Size():]

	if len(p) > 0 {
		if v.Args, err = amf0.Discovery(p); err != nil {
			return oe.WithMessage(err, "discovery args")
		}
		if err = v.Args.UnmarshalBinary(p); err != nil {
			return oe.WithMessage(err, "unmarshal args")
		}
	}

	return
}

func (v *CallPacket) MarshalBinary() (data []byte, err error) {
	var pb []byte
	if pb, err = v.variantCallPacket.MarshalBinary(); err != nil {
		return nil, oe.WithMessage(err, "marshal call")
	}
	data = append(data, pb...)

	if v.Args != nil {
		if pb, err = v.Args.MarshalBinary(); err != nil {
			return nil, oe.WithMessage(err, "marshal args")
		}
		data = append(data, pb...)
	}

	return
}

// Please read @doc rtmp_specification_1.0.pdf, @page 52, @section 4.1.3. createStream
// The client sends this command to the server to create a logical
// channel for message communication The publishing of audio, video, and
// metadata is carried out over stream channel created using the
// createStream command.
type CreateStreamPacket struct {
	variantCallPacket
}

func NewCreateStreamPacket() *CreateStreamPacket {
	v := &CreateStreamPacket{}
	v.CommandName = commandCreateStream
	v.TransactionID = amf0.Number(2)
	v.CommandObject = amf0.NewNull()
	return v
}

// The response for create stream
type CreateStreamResPacket struct {
	variantCallPacket
	StreamID amf0.Number
}

func NewCreateStreamResPacket(tid amf0.Number) *CreateStreamResPacket {
	v := &CreateStreamResPacket{}
	v.CommandName = commandResult
	v.TransactionID = tid
	v.CommandObject = amf0.NewNull()
	return v
}

func (v *CreateStreamResPacket) Size() int {
	return v.variantCallPacket.Size() + v.StreamID.Size()
}

func (v *CreateStreamResPacket) UnmarshalBinary(data []byte) (err error) {
	p := data

	if err = v.variantCallPacket.UnmarshalBinary(p); err != nil {
		return oe.WithMessage(err, "unmarshal call")
	}
	p = p[v.variantCallPacket.Size():]

	if err = v.StreamID.UnmarshalBinary(p); err != nil {
		return oe.WithMessage(err, "unmarshal sid")
	}

	return
}

func (v *CreateStreamResPacket) MarshalBinary() (data []byte, err error) {
	var pb []byte
	if pb, err = v.variantCallPacket.MarshalBinary(); err != nil {
		return nil, oe.WithMessage(err, "marshal call")
	}
	data = append(data, pb...)

	if pb, err = v.StreamID.MarshalBinary(); err != nil {
		return nil, oe.WithMessage(err, "marshal sid")
	}
	data = append(data, pb...)

	return
}

// Please read @doc rtmp_specification_1.0.pdf, @page 64, @section 4.2.6. Publish
type PublishPacket struct {
	variantCallPacket
	StreamName amf0.String
	StreamType amf0.String
}

func NewPublishPacket() *PublishPacket {
	v := &PublishPacket{}
	v.CommandName = commandPublish
	v.CommandObject = amf0.NewNull()
	v.StreamType = amf0.String("live")
	return v
}

func (v *PublishPacket) Size() int {
	return v.variantCallPacket.Size() + v.StreamName.Size() + v.StreamType.Size()
}

func (v *PublishPacket) UnmarshalBinary(data []byte) (err error) {
	p := data

	if err = v.variantCallPacket.UnmarshalBinary(p); err != nil {
		return oe.WithMessage(err, "unmarshal call")
	}
	p = p[v.variantCallPacket.Size():]

	if err = v.StreamName.UnmarshalBinary(p); err != nil {
		return oe.WithMessage(err, "unmarshal stream name")
	}
	p = p[v.StreamName.Size():]

	if err = v.StreamType.UnmarshalBinary(p); err != nil {
		return oe.WithMessage(err, "unmarshal stream type")
	}

	return
}

func (v *PublishPacket) MarshalBinary() (data []byte, err error) {
	var pb []byte
	if pb, err = v.variantCallPacket.MarshalBinary(); err != nil {
		return nil, oe.WithMessage(err, "marshal call")
	}
	data = append(data, pb...)

	if pb, err = v.StreamName.MarshalBinary(); err != nil {
		return nil, oe.WithMessage(err, "marshal stream name")
	}
	data = append(data, pb...)

	if pb, err = v.StreamType.MarshalBinary(); err != nil {
		return nil, oe.WithMessage(err, "marshal stream type")
	}
	data = append(data, pb...)

	return
}

// Please read @doc rtmp_specification_1.0.pdf, @page 54, @section 4.2.1. play
type PlayPacket struct {
	variantCallPacket
	StreamName amf0.String
}

func NewPlayPacket() *PlayPacket {
	v := &PlayPacket{}
	v.CommandName = commandPlay
	v.CommandObject = amf0.NewNull()
	return v
}

func (v *PlayPacket) Size() int {
	return v.variantCallPacket.Size() + v.StreamName.Size()
}

func (v *PlayPacket) UnmarshalBinary(data []byte) (err error) {
	p := data

	if err = v.variantCallPacket.UnmarshalBinary(p); err != nil {
		return oe.WithMessage(err, "unmarshal call")
	}
	p = p[v.variantCallPacket.Size():]

	if err = v.StreamName.UnmarshalBinary(p); err != nil {
		return oe.WithMessage(err, "unmarshal stream name")
	}
	p = p[v.StreamName.Size():]

	return
}

func (v *PlayPacket) MarshalBinary() (data []byte, err error) {
	var pb []byte
	if pb, err = v.variantCallPacket.MarshalBinary(); err != nil {
		return nil, oe.WithMessage(err, "marshal call")
	}
	data = append(data, pb...)

	if pb, err = v.StreamName.MarshalBinary(); err != nil {
		return nil, oe.WithMessage(err, "marshal stream name")
	}
	data = append(data, pb...)

	return
}

// Please read @doc rtmp_specification_1.0.pdf, @page 31, @section 5.1. Set Chunk Size
// Protocol control message 1, Set Chunk Size, is used to notify the
// peer about the new maximum chunk size.
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
		return oe.Errorf("requires 4 only %v bytes, %x", len(data), data)
	}
	v.ChunkSize = binary.BigEndian.Uint32(data)

	return
}

func (v *SetChunkSize) MarshalBinary() (data []byte, err error) {
	data = make([]byte, 4)
	binary.BigEndian.PutUint32(data, v.ChunkSize)

	return
}

// Please read @doc rtmp_specification_1.0.pdf, @page 33, @section 5.5. Window Acknowledgement Size (5)
// The client or the server sends this message to inform the peer which
// window size to use when sending acknowledgment.
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
		return oe.Errorf("requires 4 only %v bytes, %x", len(data), data)
	}
	v.AckSize = binary.BigEndian.Uint32(data)

	return
}

func (v *WindowAcknowledgementSize) MarshalBinary() (data []byte, err error) {
	data = make([]byte, 4)
	binary.BigEndian.PutUint32(data, v.AckSize)

	return
}

// Please read @doc rtmp_specification_1.0.pdf, @page 33, @section 5.6. Set Peer Bandwidth (6)
// The sender can mark this message hard (0), soft (1), or dynamic (2)
// using the Limit type field.
type LimitType uint8

const (
	LimitTypeHard LimitType = iota
	LimitTypeSoft
	LimitTypeDynamic
)

// Please read @doc rtmp_specification_1.0.pdf, @page 33, @section 5.6. Set Peer Bandwidth (6)
// The client or the server sends this message to update the output
// bandwidth of the peer.
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
		return oe.Errorf("requires 5 only %v bytes, %x", len(data), data)
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

const (
	// Generally, 4bytes event-data

	// The server sends this event to notify the client
	// that a stream has become functional and can be
	// used for communication. By default, this event
	// is sent on ID 0 after the application connect
	// command is successfully received from the
	// client. The event data is 4-byte and represents
	// The stream ID of the stream that became
	// Functional.
	EventTypeStreamBegin = 0x00

	// The server sends this event to notify the client
	// that the playback of data is over as requested
	// on this stream. No more data is sent without
	// issuing additional commands. The client discards
	// The messages received for the stream. The
	// 4 bytes of event data represent the ID of the
	// stream on which playback has ended.
	EventTypeStreamEOF = 0x01

	// The server sends this event to notify the client
	// that there is no more data on the stream. If the
	// server does not detect any message for a time
	// period, it can notify the subscribed clients
	// that the stream is dry. The 4 bytes of event
	// data represent the stream ID of the dry stream.
	EventTypeStreamDry = 0x02

	// The client sends this event to inform the server
	// of the buffer size (in milliseconds) that is
	// used to buffer any data coming over a stream.
	// This event is sent before the server starts
	// processing the stream. The first 4 bytes of the
	// event data represent the stream ID and the next
	// 4 bytes represent the buffer length, in
	// milliseconds.
	EventTypeSetBufferLength = 0x03 // 8bytes event-data

	// The server sends this event to notify the client
	// that the stream is a recorded stream. The
	// 4 bytes event data represent the stream ID of
	// The recorded stream.
	EventTypeStreamIsRecorded = 0x04

	// The server sends this event to test whether the
	// client is reachable. Event data is a 4-byte
	// timestamp, representing the local server time
	// When the server dispatched the command. The
	// client responds with kMsgPingResponse on
	// receiving kMsgPingRequest.
	EventTypePingRequest = 0x06

	// The client sends this event to the server in
	// Response  to the ping request. The event data is
	// a 4-byte timestamp, which was received with the
	// kMsgPingRequest request.
	EventTypePingResponse = 0x07

	// For PCUC size=3, for example the payload is "00 1A 01",
	// it's a FMS control event, where the event type is 0x001a and event data is 0x01,
	// please notice that the event data is only 1 byte for this event.
	EventTypeFmsEvent0 = 0x1a
)

// Please read @doc rtmp_specification_1.0.pdf, @page 32, @5.4. User Control Message (4)
// The client or the server sends this message to notify the peer about the user control events.
// This message carries Event type and Event data.
type UserControl struct {
	// Event type is followed by Event data.
	// @see: SrcPCUCEventType
	EventType EventType
	// The event data generally in 4bytes.
	// @remark for event type is 0x001a, only 1bytes.
	// @see SrsPCUCFmsEvent0
	EventData int32
	// 4bytes if event_type is SetBufferLength; otherwise 0.
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
		return oe.Errorf("requires 5 only %v bytes, %x", len(data), data)
	}

	v.EventType = EventType(binary.BigEndian.Uint16(data))
	if len(data) < v.Size() {
		return oe.Errorf("requires %v only %v bytes, %x", v.Size(), len(data), data)
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
