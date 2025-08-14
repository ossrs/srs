package base

import (
	"bufio"
	"fmt"
	"strconv"
)

// StatusCode is the status code of a RTSP response.
type StatusCode int

// status codes.
const (
	StatusContinue                           StatusCode = 100
	StatusOK                                 StatusCode = 200
	StatusMovedPermanently                   StatusCode = 301
	StatusFound                              StatusCode = 302
	StatusSeeOther                           StatusCode = 303
	StatusNotModified                        StatusCode = 304
	StatusUseProxy                           StatusCode = 305
	StatusBadRequest                         StatusCode = 400
	StatusUnauthorized                       StatusCode = 401
	StatusPaymentRequired                    StatusCode = 402
	StatusForbidden                          StatusCode = 403
	StatusNotFound                           StatusCode = 404
	StatusMethodNotAllowed                   StatusCode = 405
	StatusNotAcceptable                      StatusCode = 406
	StatusProxyAuthRequired                  StatusCode = 407
	StatusRequestTimeout                     StatusCode = 408
	StatusGone                               StatusCode = 410
	StatusPreconditionFailed                 StatusCode = 412
	StatusRequestEntityTooLarge              StatusCode = 413
	StatusRequestURITooLong                  StatusCode = 414
	StatusUnsupportedMediaType               StatusCode = 415
	StatusParameterNotUnderstood             StatusCode = 451
	StatusNotEnoughBandwidth                 StatusCode = 453
	StatusSessionNotFound                    StatusCode = 454
	StatusMethodNotValidInThisState          StatusCode = 455
	StatusHeaderFieldNotValidForResource     StatusCode = 456
	StatusInvalidRange                       StatusCode = 457
	StatusParameterIsReadOnly                StatusCode = 458
	StatusAggregateOperationNotAllowed       StatusCode = 459
	StatusOnlyAggregateOperationAllowed      StatusCode = 460
	StatusUnsupportedTransport               StatusCode = 461
	StatusDestinationUnreachable             StatusCode = 462
	StatusDestinationProhibited              StatusCode = 463
	StatusDataTransportNotReadyYet           StatusCode = 464
	StatusNotificationReasonUnknown          StatusCode = 465
	StatusKeyManagementError                 StatusCode = 466
	StatusConnectionAuthorizationRequired    StatusCode = 470
	StatusConnectionCredentialsNotAccepted   StatusCode = 471
	StatusFailureToEstablishSecureConnection StatusCode = 472
	StatusInternalServerError                StatusCode = 500
	StatusNotImplemented                     StatusCode = 501
	StatusBadGateway                         StatusCode = 502
	StatusServiceUnavailable                 StatusCode = 503
	StatusGatewayTimeout                     StatusCode = 504
	StatusRTSPVersionNotSupported            StatusCode = 505
	StatusOptionNotSupported                 StatusCode = 551
	StatusProxyUnavailable                   StatusCode = 553
)

// StatusMessages contains the status messages associated with each status code.
var StatusMessages = statusMessages

var statusMessages = map[StatusCode]string{
	StatusContinue: "Continue",

	StatusOK: "OK",

	StatusMovedPermanently: "Moved Permanently",
	StatusFound:            "Found",
	StatusSeeOther:         "See Other",
	StatusNotModified:      "Not Modified",
	StatusUseProxy:         "Use Proxy",

	StatusBadRequest:                         "Bad Request",
	StatusUnauthorized:                       "Unauthorized",
	StatusPaymentRequired:                    "Payment Required",
	StatusForbidden:                          "Forbidden",
	StatusNotFound:                           "Not Found",
	StatusMethodNotAllowed:                   "Method Not Allowed",
	StatusNotAcceptable:                      "Not Acceptable",
	StatusProxyAuthRequired:                  "Proxy Auth Required",
	StatusRequestTimeout:                     "Request Timeout",
	StatusGone:                               "Gone",
	StatusPreconditionFailed:                 "Precondition Failed",
	StatusRequestEntityTooLarge:              "Request Entity Too Large",
	StatusRequestURITooLong:                  "Request URI Too Long",
	StatusUnsupportedMediaType:               "Unsupported Media Type",
	StatusParameterNotUnderstood:             "Parameter Not Understood",
	StatusNotEnoughBandwidth:                 "Not Enough Bandwidth",
	StatusSessionNotFound:                    "Session Not Found",
	StatusMethodNotValidInThisState:          "Method Not Valid In This State",
	StatusHeaderFieldNotValidForResource:     "Header Field Not Valid for Resource",
	StatusInvalidRange:                       "Invalid Range",
	StatusParameterIsReadOnly:                "Parameter Is Read-Only",
	StatusAggregateOperationNotAllowed:       "Aggregate Operation Not Allowed",
	StatusOnlyAggregateOperationAllowed:      "Only Aggregate Operation Allowed",
	StatusUnsupportedTransport:               "Unsupported Transport",
	StatusDestinationUnreachable:             "Destination Unreachable",
	StatusDestinationProhibited:              "Destination Prohibited",
	StatusDataTransportNotReadyYet:           "Data Transport Not Ready Yet",
	StatusNotificationReasonUnknown:          "Notification Reason Unknown",
	StatusKeyManagementError:                 "Key Management Error",
	StatusConnectionAuthorizationRequired:    "Connection Authorization Required",
	StatusConnectionCredentialsNotAccepted:   "Connection Credentials Not Accepted",
	StatusFailureToEstablishSecureConnection: "Failure to Establish Secure Connection",

	StatusInternalServerError:     "Internal Server Error",
	StatusNotImplemented:          "Not Implemented",
	StatusBadGateway:              "Bad Gateway",
	StatusServiceUnavailable:      "Service Unavailable",
	StatusGatewayTimeout:          "Gateway Timeout",
	StatusRTSPVersionNotSupported: "RTSP Version Not Supported",
	StatusOptionNotSupported:      "Option Not Supported",
	StatusProxyUnavailable:        "Proxy Unavailable",
}

// Response is a RTSP response.
type Response struct {
	// numeric status code
	StatusCode StatusCode

	// status message
	StatusMessage string

	// map of header values
	Header Header

	// optional body
	Body []byte
}

// Unmarshal reads a response.
func (res *Response) Unmarshal(br *bufio.Reader) error {
	byts, err := readBytesLimited(br, ' ', 255)
	if err != nil {
		return err
	}
	proto := byts[:len(byts)-1]

	if string(proto) != rtspProtocol10 {
		return fmt.Errorf("expected '%s', got %v", rtspProtocol10, proto)
	}

	byts, err = readBytesLimited(br, ' ', 4)
	if err != nil {
		return err
	}
	statusCodeStr := string(byts[:len(byts)-1])

	tmp, err := strconv.ParseUint(statusCodeStr, 10, 31)
	if err != nil {
		return fmt.Errorf("unable to parse status code")
	}
	res.StatusCode = StatusCode(tmp)

	byts, err = readBytesLimited(br, '\r', 255)
	if err != nil {
		return err
	}
	res.StatusMessage = string(byts[:len(byts)-1])

	if len(res.StatusMessage) == 0 {
		return fmt.Errorf("empty status message")
	}

	err = readByteEqual(br, '\n')
	if err != nil {
		return err
	}

	err = res.Header.unmarshal(br)
	if err != nil {
		return err
	}

	err = (*body)(&res.Body).unmarshal(res.Header, br)
	if err != nil {
		return err
	}

	return nil
}

// MarshalSize returns the size of a Response.
func (res Response) MarshalSize() int {
	n := 0

	if res.StatusMessage == "" {
		if status, ok := statusMessages[res.StatusCode]; ok {
			res.StatusMessage = status
		}
	}

	n += len(rtspProtocol10) + 1 + len(strconv.FormatInt(int64(res.StatusCode), 10)) + 1 + len(res.StatusMessage) + 2

	if len(res.Body) != 0 {
		res.Header["Content-Length"] = HeaderValue{strconv.FormatInt(int64(len(res.Body)), 10)}
	}

	n += res.Header.marshalSize()

	n += body(res.Body).marshalSize()

	return n
}

// MarshalTo writes a Response.
func (res Response) MarshalTo(buf []byte) (int, error) {
	if res.StatusMessage == "" {
		if status, ok := statusMessages[res.StatusCode]; ok {
			res.StatusMessage = status
		}
	}

	pos := 0

	pos += copy(buf[pos:], []byte(rtspProtocol10))
	buf[pos] = ' '
	pos++
	pos += copy(buf[pos:], []byte(strconv.FormatInt(int64(res.StatusCode), 10)))
	buf[pos] = ' '
	pos++
	pos += copy(buf[pos:], []byte(res.StatusMessage))
	buf[pos] = '\r'
	pos++
	buf[pos] = '\n'
	pos++

	if len(res.Body) != 0 {
		res.Header["Content-Length"] = HeaderValue{strconv.FormatInt(int64(len(res.Body)), 10)}
	}

	pos += res.Header.marshalTo(buf[pos:])

	pos += body(res.Body).marshalTo(buf[pos:])

	return pos, nil
}

// Marshal writes a Response.
func (res Response) Marshal() ([]byte, error) {
	buf := make([]byte, res.MarshalSize())
	_, err := res.MarshalTo(buf)
	return buf, err
}

// String implements fmt.Stringer.
func (res Response) String() string {
	buf, _ := res.Marshal()
	return string(buf)
}
