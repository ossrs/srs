package sctp

import (
	"encoding/binary"
	"fmt"

	"github.com/pkg/errors"
)

// errorCauseCode is a cause code that appears in either a ERROR or ABORT chunk
type errorCauseCode uint16

type errorCause interface {
	unmarshal([]byte) error
	marshal() ([]byte, error)
	length() uint16
	String() string

	errorCauseCode() errorCauseCode
}

// buildErrorCause delegates the building of a error cause from raw bytes to the correct structure
func buildErrorCause(raw []byte) (errorCause, error) {
	var e errorCause

	c := errorCauseCode(binary.BigEndian.Uint16(raw[0:]))
	switch c {
	case invalidMandatoryParameter:
		e = &errorCauseInvalidMandatoryParameter{}
	case unrecognizedChunkType:
		e = &errorCauseUnrecognizedChunkType{}
	case protocolViolation:
		e = &errorCauseProtocolViolation{}
	default:
		return nil, errors.Errorf("BuildErrorCause does not handle %s", c.String())
	}

	if err := e.unmarshal(raw); err != nil {
		return nil, err
	}
	return e, nil
}

const (
	invalidStreamIdentifier                errorCauseCode = 1
	missingMandatoryParameter              errorCauseCode = 2
	staleCookieError                       errorCauseCode = 3
	outOfResource                          errorCauseCode = 4
	unresolvableAddress                    errorCauseCode = 5
	unrecognizedChunkType                  errorCauseCode = 6
	invalidMandatoryParameter              errorCauseCode = 7
	unrecognizedParameters                 errorCauseCode = 8
	noUserData                             errorCauseCode = 9
	cookieReceivedWhileShuttingDown        errorCauseCode = 10
	restartOfAnAssociationWithNewAddresses errorCauseCode = 11
	userInitiatedAbort                     errorCauseCode = 12
	protocolViolation                      errorCauseCode = 13
)

func (e errorCauseCode) String() string {
	switch e {
	case invalidStreamIdentifier:
		return "Invalid Stream Identifier"
	case missingMandatoryParameter:
		return "Missing Mandatory Parameter"
	case staleCookieError:
		return "Stale Cookie Error"
	case outOfResource:
		return "Out Of Resource"
	case unresolvableAddress:
		return "Unresolvable IP"
	case unrecognizedChunkType:
		return "Unrecognized Chunk Type"
	case invalidMandatoryParameter:
		return "Invalid Mandatory Parameter"
	case unrecognizedParameters:
		return "Unrecognized Parameters"
	case noUserData:
		return "No User Data"
	case cookieReceivedWhileShuttingDown:
		return "Cookie Received While Shutting Down"
	case restartOfAnAssociationWithNewAddresses:
		return "Restart Of An Association With New Addresses"
	case userInitiatedAbort:
		return "User Initiated Abort"
	case protocolViolation:
		return "Protocol Violation"
	default:
		return fmt.Sprintf("Unknown CauseCode: %d", e)
	}
}
