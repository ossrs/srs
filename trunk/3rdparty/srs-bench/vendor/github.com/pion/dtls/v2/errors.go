package dtls

import (
	"context"
	"errors"
	"fmt"
	"io"
	"net"
	"os"

	"github.com/pion/dtls/v2/pkg/protocol"
	"github.com/pion/dtls/v2/pkg/protocol/alert"
	"golang.org/x/xerrors"
)

// Typed errors
var (
	ErrConnClosed = &FatalError{Err: errors.New("conn is closed")} //nolint:goerr113

	errDeadlineExceeded   = &TimeoutError{Err: xerrors.Errorf("read/write timeout: %w", context.DeadlineExceeded)}
	errInvalidContentType = &TemporaryError{Err: errors.New("invalid content type")} //nolint:goerr113

	errBufferTooSmall               = &TemporaryError{Err: errors.New("buffer is too small")}                                        //nolint:goerr113
	errContextUnsupported           = &TemporaryError{Err: errors.New("context is not supported for ExportKeyingMaterial")}          //nolint:goerr113
	errHandshakeInProgress          = &TemporaryError{Err: errors.New("handshake is in progress")}                                   //nolint:goerr113
	errReservedExportKeyingMaterial = &TemporaryError{Err: errors.New("ExportKeyingMaterial can not be used with a reserved label")} //nolint:goerr113
	errApplicationDataEpochZero     = &TemporaryError{Err: errors.New("ApplicationData with epoch of 0")}                            //nolint:goerr113
	errUnhandledContextType         = &TemporaryError{Err: errors.New("unhandled contentType")}                                      //nolint:goerr113

	errCertificateVerifyNoCertificate    = &FatalError{Err: errors.New("client sent certificate verify but we have no certificate to verify")}                      //nolint:goerr113
	errCipherSuiteNoIntersection         = &FatalError{Err: errors.New("client+server do not support any shared cipher suites")}                                    //nolint:goerr113
	errClientCertificateNotVerified      = &FatalError{Err: errors.New("client sent certificate but did not verify it")}                                            //nolint:goerr113
	errClientCertificateRequired         = &FatalError{Err: errors.New("server required client verification, but got none")}                                        //nolint:goerr113
	errClientNoMatchingSRTPProfile       = &FatalError{Err: errors.New("server responded with SRTP Profile we do not support")}                                     //nolint:goerr113
	errClientRequiredButNoServerEMS      = &FatalError{Err: errors.New("client required Extended Master Secret extension, but server does not support it")}         //nolint:goerr113
	errCookieMismatch                    = &FatalError{Err: errors.New("client+server cookie does not match")}                                                      //nolint:goerr113
	errIdentityNoPSK                     = &FatalError{Err: errors.New("PSK Identity Hint provided but PSK is nil")}                                                //nolint:goerr113
	errInvalidCertificate                = &FatalError{Err: errors.New("no certificate provided")}                                                                  //nolint:goerr113
	errInvalidCipherSuite                = &FatalError{Err: errors.New("invalid or unknown cipher suite")}                                                          //nolint:goerr113
	errInvalidECDSASignature             = &FatalError{Err: errors.New("ECDSA signature contained zero or negative values")}                                        //nolint:goerr113
	errInvalidPrivateKey                 = &FatalError{Err: errors.New("invalid private key type")}                                                                 //nolint:goerr113
	errInvalidSignatureAlgorithm         = &FatalError{Err: errors.New("invalid signature algorithm")}                                                              //nolint:goerr113
	errKeySignatureMismatch              = &FatalError{Err: errors.New("expected and actual key signature do not match")}                                           //nolint:goerr113
	errNilNextConn                       = &FatalError{Err: errors.New("Conn can not be created with a nil nextConn")}                                              //nolint:goerr113
	errNoAvailableCipherSuites           = &FatalError{Err: errors.New("connection can not be created, no CipherSuites satisfy this Config")}                       //nolint:goerr113
	errNoAvailablePSKCipherSuite         = &FatalError{Err: errors.New("connection can not be created, pre-shared key present but no compatible CipherSuite")}      //nolint:goerr113
	errNoAvailableCertificateCipherSuite = &FatalError{Err: errors.New("connection can not be created, certificate present but no compatible CipherSuite")}         //nolint:goerr113
	errNoAvailableSignatureSchemes       = &FatalError{Err: errors.New("connection can not be created, no SignatureScheme satisfy this Config")}                    //nolint:goerr113
	errNoCertificates                    = &FatalError{Err: errors.New("no certificates configured")}                                                               //nolint:goerr113
	errNoConfigProvided                  = &FatalError{Err: errors.New("no config provided")}                                                                       //nolint:goerr113
	errNoSupportedEllipticCurves         = &FatalError{Err: errors.New("client requested zero or more elliptic curves that are not supported by the server")}       //nolint:goerr113
	errUnsupportedProtocolVersion        = &FatalError{Err: errors.New("unsupported protocol version")}                                                             //nolint:goerr113
	errPSKAndIdentityMustBeSetForClient  = &FatalError{Err: errors.New("PSK and PSK Identity Hint must both be set for client")}                                    //nolint:goerr113
	errRequestedButNoSRTPExtension       = &FatalError{Err: errors.New("SRTP support was requested but server did not respond with use_srtp extension")}            //nolint:goerr113
	errServerNoMatchingSRTPProfile       = &FatalError{Err: errors.New("client requested SRTP but we have no matching profiles")}                                   //nolint:goerr113
	errServerRequiredButNoClientEMS      = &FatalError{Err: errors.New("server requires the Extended Master Secret extension, but the client does not support it")} //nolint:goerr113
	errVerifyDataMismatch                = &FatalError{Err: errors.New("expected and actual verify data does not match")}                                           //nolint:goerr113

	errInvalidFlight                     = &InternalError{Err: errors.New("invalid flight number")}                           //nolint:goerr113
	errKeySignatureGenerateUnimplemented = &InternalError{Err: errors.New("unable to generate key signature, unimplemented")} //nolint:goerr113
	errKeySignatureVerifyUnimplemented   = &InternalError{Err: errors.New("unable to verify key signature, unimplemented")}   //nolint:goerr113
	errLengthMismatch                    = &InternalError{Err: errors.New("data length and declared length do not match")}    //nolint:goerr113
	errSequenceNumberOverflow            = &InternalError{Err: errors.New("sequence number overflow")}                        //nolint:goerr113
	errInvalidFSMTransition              = &InternalError{Err: errors.New("invalid state machine transition")}                //nolint:goerr113
)

// FatalError indicates that the DTLS connection is no longer available.
// It is mainly caused by wrong configuration of server or client.
type FatalError = protocol.FatalError

// InternalError indicates and internal error caused by the implementation, and the DTLS connection is no longer available.
// It is mainly caused by bugs or tried to use unimplemented features.
type InternalError = protocol.InternalError

// TemporaryError indicates that the DTLS connection is still available, but the request was failed temporary.
type TemporaryError = protocol.TemporaryError

// TimeoutError indicates that the request was timed out.
type TimeoutError = protocol.TimeoutError

// HandshakeError indicates that the handshake failed.
type HandshakeError = protocol.HandshakeError

// invalidCipherSuite indicates an attempt at using an unsupported cipher suite.
type invalidCipherSuite struct {
	id CipherSuiteID
}

func (e *invalidCipherSuite) Error() string {
	return fmt.Sprintf("CipherSuite with id(%d) is not valid", e.id)
}

func (e *invalidCipherSuite) Is(err error) bool {
	if other, ok := err.(*invalidCipherSuite); ok {
		return e.id == other.id
	}
	return false
}

// errAlert wraps DTLS alert notification as an error
type errAlert struct {
	*alert.Alert
}

func (e *errAlert) Error() string {
	return fmt.Sprintf("alert: %s", e.Alert.String())
}

func (e *errAlert) IsFatalOrCloseNotify() bool {
	return e.Level == alert.Fatal || e.Description == alert.CloseNotify
}

func (e *errAlert) Is(err error) bool {
	if other, ok := err.(*errAlert); ok {
		return e.Level == other.Level && e.Description == other.Description
	}
	return false
}

// netError translates an error from underlying Conn to corresponding net.Error.
func netError(err error) error {
	switch err {
	case io.EOF, context.Canceled, context.DeadlineExceeded:
		// Return io.EOF and context errors as is.
		return err
	}
	switch e := err.(type) {
	case (*net.OpError):
		if se, ok := e.Err.(*os.SyscallError); ok {
			if se.Timeout() {
				return &TimeoutError{Err: err}
			}
			if isOpErrorTemporary(se) {
				return &TemporaryError{Err: err}
			}
		}
	case (net.Error):
		return err
	}
	return &FatalError{Err: err}
}
