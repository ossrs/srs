package sctp

import (
	"github.com/pkg/errors"
)

/*
chunkHeartbeatAck represents an SCTP Chunk of type HEARTBEAT ACK

An endpoint should send this chunk to its peer endpoint as a response
to a HEARTBEAT chunk (see Section 8.3).  A HEARTBEAT ACK is always
sent to the source IP address of the IP datagram containing the
HEARTBEAT chunk to which this ack is responding.

The parameter field contains a variable-length opaque data structure.

 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|   Type = 5    | Chunk  Flags  |    Heartbeat Ack Length       |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                                                               |
|            Heartbeat Information TLV (Variable-Length)        |
|                                                               |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+


Defined as a variable-length parameter using the format described
in Section 3.2.1, i.e.:

Variable Parameters                  Status     Type Value
-------------------------------------------------------------
Heartbeat Info                       Mandatory   1

*/
type chunkHeartbeatAck struct {
	chunkHeader
	params []param
}

func (h *chunkHeartbeatAck) unmarshal(raw []byte) error {
	return errors.Errorf("Unimplemented")
}

func (h *chunkHeartbeatAck) marshal() ([]byte, error) {
	if len(h.params) != 1 {
		return nil, errors.Errorf("Heartbeat Ack must have one param")
	}

	switch h.params[0].(type) {
	case *paramHeartbeatInfo:
		// ParamHeartbeatInfo is valid
	default:
		return nil, errors.Errorf("Heartbeat Ack must have one param, and it should be a HeartbeatInfo")
	}

	out := make([]byte, 0)
	for idx, p := range h.params {
		pp, err := p.marshal()
		if err != nil {
			return nil, errors.Wrap(err, "Unable to marshal parameter for Heartbeat Ack")
		}

		out = append(out, pp...)

		// Chunks (including Type, Length, and Value fields) are padded out
		// by the sender with all zero bytes to be a multiple of 4 bytes
		// long.  This padding MUST NOT be more than 3 bytes in total.  The
		// Chunk Length value does not include terminating padding of the
		// chunk.  *However, it does include padding of any variable-length
		// parameter except the last parameter in the chunk.*  The receiver
		// MUST ignore the padding.
		if idx != len(h.params)-1 {
			out = padByte(out, getPadding(len(pp)))
		}
	}

	h.chunkHeader.typ = ctHeartbeatAck
	h.chunkHeader.raw = out

	return h.chunkHeader.marshal()
}

func (h *chunkHeartbeatAck) check() (abort bool, err error) {
	return false, nil
}
