package extension

import (
	"errors"

	"github.com/pion/dtls/v2/pkg/protocol"
)

var (
	errBufferTooSmall       = &protocol.TemporaryError{Err: errors.New("buffer is too small")}                         //nolint:goerr113
	errInvalidExtensionType = &protocol.FatalError{Err: errors.New("invalid extension type")}                          //nolint:goerr113
	errInvalidSNIFormat     = &protocol.FatalError{Err: errors.New("invalid server name format")}                      //nolint:goerr113
	errLengthMismatch       = &protocol.InternalError{Err: errors.New("data length and declared length do not match")} //nolint:goerr113
)
