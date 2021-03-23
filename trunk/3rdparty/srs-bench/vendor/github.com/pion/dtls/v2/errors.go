package dtls

import (
	"context"
	"errors"
	"fmt"
	"io"
	"net"
	"os"

	"golang.org/x/xerrors"
)

// Typed errors
var (
	ErrConnClosed = &FatalError{errors.New("conn is closed")} //nolint:goerr113

	errDeadlineExceeded = &TimeoutError{xerrors.Errorf("read/write timeout: %w", context.DeadlineExceeded)}

	errBufferTooSmall               = &TemporaryError{errors.New("buffer is too small")}                                        //nolint:goerr113
	errContextUnsupported           = &TemporaryError{errors.New("context is not supported for ExportKeyingMaterial")}          //nolint:goerr113
	errDTLSPacketInvalidLength      = &TemporaryError{errors.New("packet is too short")}                                        //nolint:goerr113
	errHandshakeInProgress          = &TemporaryError{errors.New("handshake is in progress")}                                   //nolint:goerr113
	errInvalidContentType           = &TemporaryError{errors.New("invalid content type")}                                       //nolint:goerr113
	errInvalidMAC                   = &TemporaryError{errors.New("invalid mac")}                                                //nolint:goerr113
	errInvalidPacketLength          = &TemporaryError{errors.New("packet length and declared length do not match")}             //nolint:goerr113
	errReservedExportKeyingMaterial = &TemporaryError{errors.New("ExportKeyingMaterial can not be used with a reserved label")} //nolint:goerr113

	errCertificateVerifyNoCertificate   = &FatalError{errors.New("client sent certificate verify but we have no certificate to verify")}                      //nolint:goerr113
	errCipherSuiteNoIntersection        = &FatalError{errors.New("client+server do not support any shared cipher suites")}                                    //nolint:goerr113
	errCipherSuiteUnset                 = &FatalError{errors.New("server hello can not be created without a cipher suite")}                                   //nolint:goerr113
	errClientCertificateNotVerified     = &FatalError{errors.New("client sent certificate but did not verify it")}                                            //nolint:goerr113
	errClientCertificateRequired        = &FatalError{errors.New("server required client verification, but got none")}                                        //nolint:goerr113
	errClientNoMatchingSRTPProfile      = &FatalError{errors.New("server responded with SRTP Profile we do not support")}                                     //nolint:goerr113
	errClientRequiredButNoServerEMS     = &FatalError{errors.New("client required Extended Master Secret extension, but server does not support it")}         //nolint:goerr113
	errCompressionMethodUnset           = &FatalError{errors.New("server hello can not be created without a compression method")}                             //nolint:goerr113
	errCookieMismatch                   = &FatalError{errors.New("client+server cookie does not match")}                                                      //nolint:goerr113
	errCookieTooLong                    = &FatalError{errors.New("cookie must not be longer then 255 bytes")}                                                 //nolint:goerr113
	errIdentityNoPSK                    = &FatalError{errors.New("PSK Identity Hint provided but PSK is nil")}                                                //nolint:goerr113
	errInvalidCertificate               = &FatalError{errors.New("no certificate provided")}                                                                  //nolint:goerr113
	errInvalidCipherSpec                = &FatalError{errors.New("cipher spec invalid")}                                                                      //nolint:goerr113
	errInvalidCipherSuite               = &FatalError{errors.New("invalid or unknown cipher suite")}                                                          //nolint:goerr113
	errInvalidClientKeyExchange         = &FatalError{errors.New("unable to determine if ClientKeyExchange is a public key or PSK Identity")}                 //nolint:goerr113
	errInvalidCompressionMethod         = &FatalError{errors.New("invalid or unknown compression method")}                                                    //nolint:goerr113
	errInvalidECDSASignature            = &FatalError{errors.New("ECDSA signature contained zero or negative values")}                                        //nolint:goerr113
	errInvalidEllipticCurveType         = &FatalError{errors.New("invalid or unknown elliptic curve type")}                                                   //nolint:goerr113
	errInvalidExtensionType             = &FatalError{errors.New("invalid extension type")}                                                                   //nolint:goerr113
	errInvalidHashAlgorithm             = &FatalError{errors.New("invalid hash algorithm")}                                                                   //nolint:goerr113
	errInvalidNamedCurve                = &FatalError{errors.New("invalid named curve")}                                                                      //nolint:goerr113
	errInvalidPrivateKey                = &FatalError{errors.New("invalid private key type")}                                                                 //nolint:goerr113
	errInvalidSNIFormat                 = &FatalError{errors.New("invalid server name format")}                                                               //nolint:goerr113
	errInvalidSignatureAlgorithm        = &FatalError{errors.New("invalid signature algorithm")}                                                              //nolint:goerr113
	errKeySignatureMismatch             = &FatalError{errors.New("expected and actual key signature do not match")}                                           //nolint:goerr113
	errNilNextConn                      = &FatalError{errors.New("Conn can not be created with a nil nextConn")}                                              //nolint:goerr113
	errNoAvailableCipherSuites          = &FatalError{errors.New("connection can not be created, no CipherSuites satisfy this Config")}                       //nolint:goerr113
	errNoAvailableSignatureSchemes      = &FatalError{errors.New("connection can not be created, no SignatureScheme satisfy this Config")}                    //nolint:goerr113
	errNoCertificates                   = &FatalError{errors.New("no certificates configured")}                                                               //nolint:goerr113
	errNoConfigProvided                 = &FatalError{errors.New("no config provided")}                                                                       //nolint:goerr113
	errNoSupportedEllipticCurves        = &FatalError{errors.New("client requested zero or more elliptic curves that are not supported by the server")}       //nolint:goerr113
	errUnsupportedProtocolVersion       = &FatalError{errors.New("unsupported protocol version")}                                                             //nolint:goerr113
	errPSKAndCertificate                = &FatalError{errors.New("Certificate and PSK provided")}                                                             //nolint:stylecheck
	errPSKAndIdentityMustBeSetForClient = &FatalError{errors.New("PSK and PSK Identity Hint must both be set for client")}                                    //nolint:goerr113
	errRequestedButNoSRTPExtension      = &FatalError{errors.New("SRTP support was requested but server did not respond with use_srtp extension")}            //nolint:goerr113
	errServerMustHaveCertificate        = &FatalError{errors.New("Certificate is mandatory for server")}                                                      //nolint:stylecheck
	errServerNoMatchingSRTPProfile      = &FatalError{errors.New("client requested SRTP but we have no matching profiles")}                                   //nolint:goerr113
	errServerRequiredButNoClientEMS     = &FatalError{errors.New("server requires the Extended Master Secret extension, but the client does not support it")} //nolint:goerr113
	errVerifyDataMismatch               = &FatalError{errors.New("expected and actual verify data does not match")}                                           //nolint:goerr113

	errHandshakeMessageUnset             = &InternalError{errors.New("handshake message unset, unable to marshal")}      //nolint:goerr113
	errInvalidFlight                     = &InternalError{errors.New("invalid flight number")}                           //nolint:goerr113
	errKeySignatureGenerateUnimplemented = &InternalError{errors.New("unable to generate key signature, unimplemented")} //nolint:goerr113
	errKeySignatureVerifyUnimplemented   = &InternalError{errors.New("unable to verify key signature, unimplemented")}   //nolint:goerr113
	errLengthMismatch                    = &InternalError{errors.New("data length and declared length do not match")}    //nolint:goerr113
	errNotEnoughRoomForNonce             = &InternalError{errors.New("buffer not long enough to contain nonce")}         //nolint:goerr113
	errNotImplemented                    = &InternalError{errors.New("feature has not been implemented yet")}            //nolint:goerr113
	errSequenceNumberOverflow            = &InternalError{errors.New("sequence number overflow")}                        //nolint:goerr113
	errUnableToMarshalFragmented         = &InternalError{errors.New("unable to marshal fragmented handshakes")}         //nolint:goerr113
)

// FatalError indicates that the DTLS connection is no longer available.
// It is mainly caused by wrong configuration of server or client.
type FatalError struct {
	Err error
}

// InternalError indicates and internal error caused by the implementation, and the DTLS connection is no longer available.
// It is mainly caused by bugs or tried to use unimplemented features.
type InternalError struct {
	Err error
}

// TemporaryError indicates that the DTLS connection is still available, but the request was failed temporary.
type TemporaryError struct {
	Err error
}

// TimeoutError indicates that the request was timed out.
type TimeoutError struct {
	Err error
}

// HandshakeError indicates that the handshake failed.
type HandshakeError struct {
	Err error
}

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

// Timeout implements net.Error.Timeout()
func (*FatalError) Timeout() bool { return false }

// Temporary implements net.Error.Temporary()
func (*FatalError) Temporary() bool { return false }

// Unwrap implements Go1.13 error unwrapper.
func (e *FatalError) Unwrap() error { return e.Err }

func (e *FatalError) Error() string { return fmt.Sprintf("dtls fatal: %v", e.Err) }

// Timeout implements net.Error.Timeout()
func (*InternalError) Timeout() bool { return false }

// Temporary implements net.Error.Temporary()
func (*InternalError) Temporary() bool { return false }

// Unwrap implements Go1.13 error unwrapper.
func (e *InternalError) Unwrap() error { return e.Err }

func (e *InternalError) Error() string { return fmt.Sprintf("dtls internal: %v", e.Err) }

// Timeout implements net.Error.Timeout()
func (*TemporaryError) Timeout() bool { return false }

// Temporary implements net.Error.Temporary()
func (*TemporaryError) Temporary() bool { return true }

// Unwrap implements Go1.13 error unwrapper.
func (e *TemporaryError) Unwrap() error { return e.Err }

func (e *TemporaryError) Error() string { return fmt.Sprintf("dtls temporary: %v", e.Err) }

// Timeout implements net.Error.Timeout()
func (*TimeoutError) Timeout() bool { return true }

// Temporary implements net.Error.Temporary()
func (*TimeoutError) Temporary() bool { return true }

// Unwrap implements Go1.13 error unwrapper.
func (e *TimeoutError) Unwrap() error { return e.Err }

func (e *TimeoutError) Error() string { return fmt.Sprintf("dtls timeout: %v", e.Err) }

// Timeout implements net.Error.Timeout()
func (e *HandshakeError) Timeout() bool {
	if netErr, ok := e.Err.(net.Error); ok {
		return netErr.Timeout()
	}
	return false
}

// Temporary implements net.Error.Temporary()
func (e *HandshakeError) Temporary() bool {
	if netErr, ok := e.Err.(net.Error); ok {
		return netErr.Temporary()
	}
	return false
}

// Unwrap implements Go1.13 error unwrapper.
func (e *HandshakeError) Unwrap() error { return e.Err }

func (e *HandshakeError) Error() string { return fmt.Sprintf("handshake error: %v", e.Err) }

// errAlert wraps DTLS alert notification as an error
type errAlert struct {
	*alert
}

func (e *errAlert) Error() string {
	return fmt.Sprintf("alert: %s", e.alert.String())
}

func (e *errAlert) IsFatalOrCloseNotify() bool {
	return e.alertLevel == alertLevelFatal || e.alertDescription == alertCloseNotify
}

func (e *errAlert) Is(err error) bool {
	if other, ok := err.(*errAlert); ok {
		return e.alertLevel == other.alertLevel && e.alertDescription == other.alertDescription
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
				return &TimeoutError{err}
			}
			if isOpErrorTemporary(se) {
				return &TemporaryError{err}
			}
		}
	case (net.Error):
		return err
	}
	return &FatalError{err}
}
