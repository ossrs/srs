package sctp

import (
	"github.com/pkg/errors"
)

/*
chunkHeartbeat represents an SCTP Chunk of type HEARTBEAT

An endpoint should send this chunk to its peer endpoint to probe the
reachability of a particular destination transport address defined in
the present association.

The parameter field contains the Heartbeat Information, which is a
variable-length opaque data structure understood only by the sender.


 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|   Type = 4    | Chunk  Flags  |      Heartbeat Length         |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                                                               |
|            Heartbeat Information TLV (Variable-Length)        |
|                                                               |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

Defined as a variable-length parameter using the format described
in Section 3.2.1, i.e.:

Variable Parameters                  Status     Type Value
-------------------------------------------------------------
heartbeat Info                       Mandatory   1

*/
type chunkHeartbeat struct {
	chunkHeader
	params []param
}

func (h *chunkHeartbeat) unmarshal(raw []byte) error {
	if err := h.chunkHeader.unmarshal(raw); err != nil {
		return err
	} else if h.typ != ctHeartbeat {
		return errors.Errorf("ChunkType is not of type HEARTBEAT, actually is %s", h.typ.String())
	}

	if len(raw) <= chunkHeaderSize {
		return errors.Errorf("Heartbeat is not long enough to contain Heartbeat Info %d", len(raw))
	}

	pType, err := parseParamType(raw[chunkHeaderSize:])
	if err != nil {
		return errors.Wrap(err, "failed to parse param type")
	}
	if pType != heartbeatInfo {
		return errors.Errorf("Heartbeat should only have HEARTBEAT param, instead have %s", pType.String())
	}

	p, err := buildParam(pType, raw[chunkHeaderSize:])
	if err != nil {
		return errors.Wrap(err, "Failed unmarshalling param in Heartbeat Chunk")
	}
	h.params = append(h.params, p)

	return nil
}

func (h *chunkHeartbeat) Marshal() ([]byte, error) {
	return nil, errors.Errorf("Unimplemented")
}

func (h *chunkHeartbeat) check() (abort bool, err error) {
	return false, nil
}
