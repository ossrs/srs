package recordlayer

import (
	"encoding/binary"

	"github.com/pion/dtls/v2/pkg/protocol"
	"github.com/pion/dtls/v2/pkg/protocol/alert"
	"github.com/pion/dtls/v2/pkg/protocol/handshake"
)

// RecordLayer which handles all data transport.
// The record layer is assumed to sit directly on top of some
// reliable transport such as TCP. The record layer can carry four types of content:
//
// 1. Handshake messages—used for algorithm negotiation and key establishment.
// 2. ChangeCipherSpec messages—really part of the handshake but technically a separate kind of message.
// 3. Alert messages—used to signal that errors have occurred
// 4. Application layer data
//
// The DTLS record layer is extremely similar to that of TLS 1.1.  The
// only change is the inclusion of an explicit sequence number in the
// record.  This sequence number allows the recipient to correctly
// verify the TLS MAC.
//
// https://tools.ietf.org/html/rfc4347#section-4.1
type RecordLayer struct {
	Header  Header
	Content protocol.Content
}

// Marshal encodes the RecordLayer to binary
func (r *RecordLayer) Marshal() ([]byte, error) {
	contentRaw, err := r.Content.Marshal()
	if err != nil {
		return nil, err
	}

	r.Header.ContentLen = uint16(len(contentRaw))
	r.Header.ContentType = r.Content.ContentType()

	headerRaw, err := r.Header.Marshal()
	if err != nil {
		return nil, err
	}

	return append(headerRaw, contentRaw...), nil
}

// Unmarshal populates the RecordLayer from binary
func (r *RecordLayer) Unmarshal(data []byte) error {
	if len(data) < HeaderSize {
		return errBufferTooSmall
	}
	if err := r.Header.Unmarshal(data); err != nil {
		return err
	}

	switch protocol.ContentType(data[0]) {
	case protocol.ContentTypeChangeCipherSpec:
		r.Content = &protocol.ChangeCipherSpec{}
	case protocol.ContentTypeAlert:
		r.Content = &alert.Alert{}
	case protocol.ContentTypeHandshake:
		r.Content = &handshake.Handshake{}
	case protocol.ContentTypeApplicationData:
		r.Content = &protocol.ApplicationData{}
	default:
		return errInvalidContentType
	}

	return r.Content.Unmarshal(data[HeaderSize:])
}

// UnpackDatagram extracts all RecordLayer messages from a single datagram.
// Note that as with TLS, multiple handshake messages may be placed in
// the same DTLS record, provided that there is room and that they are
// part of the same flight.  Thus, there are two acceptable ways to pack
// two DTLS messages into the same datagram: in the same record or in
// separate records.
// https://tools.ietf.org/html/rfc6347#section-4.2.3
func UnpackDatagram(buf []byte) ([][]byte, error) {
	out := [][]byte{}

	for offset := 0; len(buf) != offset; {
		if len(buf)-offset <= HeaderSize {
			return nil, errInvalidPacketLength
		}

		pktLen := (HeaderSize + int(binary.BigEndian.Uint16(buf[offset+11:])))
		if offset+pktLen > len(buf) {
			return nil, errInvalidPacketLength
		}

		out = append(out, buf[offset:offset+pktLen])
		offset += pktLen
	}

	return out, nil
}
