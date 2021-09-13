// Package recordlayer implements the TLS Record Layer https://tools.ietf.org/html/rfc5246#section-6
package recordlayer

import (
	"errors"

	"github.com/pion/dtls/v2/pkg/protocol"
)

var (
	errBufferTooSmall             = &protocol.TemporaryError{Err: errors.New("buffer is too small")}                            //nolint:goerr113
	errInvalidPacketLength        = &protocol.TemporaryError{Err: errors.New("packet length and declared length do not match")} //nolint:goerr113
	errSequenceNumberOverflow     = &protocol.InternalError{Err: errors.New("sequence number overflow")}                        //nolint:goerr113
	errUnsupportedProtocolVersion = &protocol.FatalError{Err: errors.New("unsupported protocol version")}                       //nolint:goerr113
	errInvalidContentType         = &protocol.TemporaryError{Err: errors.New("invalid content type")}                           //nolint:goerr113
)
