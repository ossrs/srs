package handshake

import (
	"errors"

	"github.com/pion/dtls/v2/pkg/protocol"
)

// Typed errors
var (
	errUnableToMarshalFragmented = &protocol.InternalError{Err: errors.New("unable to marshal fragmented handshakes")}                               //nolint:goerr113
	errHandshakeMessageUnset     = &protocol.InternalError{Err: errors.New("handshake message unset, unable to marshal")}                            //nolint:goerr113
	errBufferTooSmall            = &protocol.TemporaryError{Err: errors.New("buffer is too small")}                                                  //nolint:goerr113
	errLengthMismatch            = &protocol.InternalError{Err: errors.New("data length and declared length do not match")}                          //nolint:goerr113
	errInvalidClientKeyExchange  = &protocol.FatalError{Err: errors.New("unable to determine if ClientKeyExchange is a public key or PSK Identity")} //nolint:goerr113
	errInvalidHashAlgorithm      = &protocol.FatalError{Err: errors.New("invalid hash algorithm")}                                                   //nolint:goerr113
	errInvalidSignatureAlgorithm = &protocol.FatalError{Err: errors.New("invalid signature algorithm")}                                              //nolint:goerr113
	errCookieTooLong             = &protocol.FatalError{Err: errors.New("cookie must not be longer then 255 bytes")}                                 //nolint:goerr113
	errInvalidEllipticCurveType  = &protocol.FatalError{Err: errors.New("invalid or unknown elliptic curve type")}                                   //nolint:goerr113
	errInvalidNamedCurve         = &protocol.FatalError{Err: errors.New("invalid named curve")}                                                      //nolint:goerr113
	errCipherSuiteUnset          = &protocol.FatalError{Err: errors.New("server hello can not be created without a cipher suite")}                   //nolint:goerr113
	errCompressionMethodUnset    = &protocol.FatalError{Err: errors.New("server hello can not be created without a compression method")}             //nolint:goerr113
	errInvalidCompressionMethod  = &protocol.FatalError{Err: errors.New("invalid or unknown compression method")}                                    //nolint:goerr113
	errNotImplemented            = &protocol.InternalError{Err: errors.New("feature has not been implemented yet")}                                  //nolint:goerr113
)
