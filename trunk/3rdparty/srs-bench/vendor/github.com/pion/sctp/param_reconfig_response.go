package sctp

import (
	"encoding/binary"
	"errors"
	"fmt"
)

// This parameter is used by the receiver of a Re-configuration Request
// Parameter to respond to the request.
//
// 0                   1                   2                   3
// 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |     Parameter Type = 16       |      Parameter Length         |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |         Re-configuration Response Sequence Number             |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |                            Result                             |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |                   Sender's Next TSN (optional)                |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |                  Receiver's Next TSN (optional)               |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

type paramReconfigResponse struct {
	paramHeader
	// This value is copied from the request parameter and is used by the
	// receiver of the Re-configuration Response Parameter to tie the
	// response to the request.
	reconfigResponseSequenceNumber uint32
	// This value describes the result of the processing of the request.
	result reconfigResult
}

type reconfigResult uint32

const (
	reconfigResultSuccessNOP                    reconfigResult = 0
	reconfigResultSuccessPerformed              reconfigResult = 1
	reconfigResultDenied                        reconfigResult = 2
	reconfigResultErrorWrongSSN                 reconfigResult = 3
	reconfigResultErrorRequestAlreadyInProgress reconfigResult = 4
	reconfigResultErrorBadSequenceNumber        reconfigResult = 5
	reconfigResultInProgress                    reconfigResult = 6
)

var errReconfigRespParamTooShort = errors.New("reconfig response parameter too short")

func (t reconfigResult) String() string {
	switch t {
	case reconfigResultSuccessNOP:
		return "0: Success - Nothing to do"
	case reconfigResultSuccessPerformed:
		return "1: Success - Performed"
	case reconfigResultDenied:
		return "2: Denied"
	case reconfigResultErrorWrongSSN:
		return "3: Error - Wrong SSN"
	case reconfigResultErrorRequestAlreadyInProgress:
		return "4: Error - Request already in progress"
	case reconfigResultErrorBadSequenceNumber:
		return "5: Error - Bad Sequence Number"
	case reconfigResultInProgress:
		return "6: In progress"
	default:
		return fmt.Sprintf("Unknown reconfigResult: %d", t)
	}
}

func (r *paramReconfigResponse) marshal() ([]byte, error) {
	r.typ = reconfigResp
	r.raw = make([]byte, 8)
	binary.BigEndian.PutUint32(r.raw, r.reconfigResponseSequenceNumber)
	binary.BigEndian.PutUint32(r.raw[4:], uint32(r.result))

	return r.paramHeader.marshal()
}

func (r *paramReconfigResponse) unmarshal(raw []byte) (param, error) {
	err := r.paramHeader.unmarshal(raw)
	if err != nil {
		return nil, err
	}
	if len(r.raw) < 8 {
		return nil, errReconfigRespParamTooShort
	}
	r.reconfigResponseSequenceNumber = binary.BigEndian.Uint32(r.raw)
	r.result = reconfigResult(binary.BigEndian.Uint32(r.raw[4:]))

	return r, nil
}
