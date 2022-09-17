package sip

import (
	"bytes"
	"fmt"
	"strconv"
	"strings"

	"github.com/ghettovoice/gosip/log"
)

// Response RFC 3261 - 7.2.
type Response interface {
	Message
	StatusCode() StatusCode
	SetStatusCode(code StatusCode)
	Reason() string
	SetReason(reason string)
	// Previous returns previous provisional responses
	Previous() []Response
	SetPrevious(responses []Response)
	/* Common helpers */
	IsProvisional() bool
	IsSuccess() bool
	IsRedirection() bool
	IsClientError() bool
	IsServerError() bool
	IsGlobalError() bool
}

type response struct {
	message
	status   StatusCode
	reason   string
	previous []Response
}

func NewResponse(
	messID MessageID,
	sipVersion string,
	statusCode StatusCode,
	reason string,
	hdrs []Header,
	body string,
	fields log.Fields,
) Response {
	res := new(response)
	if messID == "" {
		res.messID = NextMessageID()
	} else {
		res.messID = messID
	}
	res.startLine = res.StartLine
	res.sipVersion = sipVersion
	res.headers = newHeaders(hdrs)
	res.status = statusCode
	res.reason = reason
	res.body = body
	res.fields = fields.WithFields(log.Fields{
		"response_id": res.messID,
	})
	res.previous = make([]Response, 0)

	return res
}

func (res *response) Short() string {
	if res == nil {
		return "<nil>"
	}

	fields := log.Fields{
		"status":      res.StatusCode(),
		"reason":      res.Reason(),
		"transport":   res.Transport(),
		"source":      res.Source(),
		"destination": res.Destination(),
	}
	if cseq, ok := res.CSeq(); ok {
		fields["method"] = cseq.MethodName
		fields["sequence"] = cseq.SeqNo
	}
	fields = res.Fields().WithFields(fields)

	return fmt.Sprintf("sip.Response<%s>", fields)
}

func (res *response) StatusCode() StatusCode {
	res.mu.RLock()
	defer res.mu.RUnlock()
	return res.status
}
func (res *response) SetStatusCode(code StatusCode) {
	res.mu.Lock()
	res.status = code
	res.mu.Unlock()
}

func (res *response) Reason() string {
	res.mu.RLock()
	defer res.mu.RUnlock()
	return res.reason
}
func (res *response) SetReason(reason string) {
	res.mu.Lock()
	res.reason = reason
	res.mu.Unlock()
}

func (res *response) Previous() []Response {
	res.mu.RLock()
	defer res.mu.RUnlock()
	return res.previous
}

func (res *response) SetPrevious(responses []Response) {
	res.mu.Lock()
	res.previous = responses
	res.mu.Unlock()
}

// StartLine returns Response Status Line - RFC 2361 7.2.
func (res *response) StartLine() string {
	var buffer bytes.Buffer

	// Every SIP response starts with a Status Line - RFC 2361 7.2.
	buffer.WriteString(
		fmt.Sprintf(
			"%s %d %s",
			res.SipVersion(),
			res.StatusCode(),
			res.Reason(),
		),
	)

	return buffer.String()
}

func (res *response) Clone() Message {
	return cloneResponse(res, "", nil)
}

func (res *response) Fields() log.Fields {
	return res.fields.WithFields(log.Fields{
		"transport":   res.Transport(),
		"source":      res.Source(),
		"destination": res.Destination(),
	})
}

func (res *response) WithFields(fields log.Fields) Message {
	res.mu.Lock()
	res.fields = res.fields.WithFields(fields)
	res.mu.Unlock()

	return res
}

func (res *response) IsProvisional() bool {
	return res.StatusCode() < 200
}

func (res *response) IsSuccess() bool {
	return res.StatusCode() >= 200 && res.StatusCode() < 300
}

func (res *response) IsRedirection() bool {
	return res.StatusCode() >= 300 && res.StatusCode() < 400
}

func (res *response) IsClientError() bool {
	return res.StatusCode() >= 400 && res.StatusCode() < 500
}

func (res *response) IsServerError() bool {
	return res.StatusCode() >= 500 && res.StatusCode() < 600
}

func (res *response) IsGlobalError() bool {
	return res.StatusCode() >= 600
}

func (res *response) IsAck() bool {
	if cseq, ok := res.CSeq(); ok {
		return cseq.MethodName == ACK
	}
	return false
}

func (res *response) IsCancel() bool {
	if cseq, ok := res.CSeq(); ok {
		return cseq.MethodName == CANCEL
	}
	return false
}

func (res *response) Transport() string {
	if tp := res.message.Transport(); tp != "" {
		return strings.ToUpper(tp)
	}

	var tp string
	if viaHop, ok := res.ViaHop(); ok && viaHop.Transport != "" {
		tp = viaHop.Transport
	} else {
		tp = DefaultProtocol
	}

	return tp
}

func (res *response) Destination() string {
	if dest := res.message.Destination(); dest != "" {
		return dest
	}

	viaHop, ok := res.ViaHop()
	if !ok {
		return ""
	}

	var (
		host string
		port Port
	)

	host = viaHop.Host
	if viaHop.Port != nil {
		port = *viaHop.Port
	} else {
		port = DefaultPort(res.Transport())
	}

	if viaHop.Params != nil {
		if received, ok := viaHop.Params.Get("received"); ok && received.String() != "" {
			host = received.String()
		}
		if rport, ok := viaHop.Params.Get("rport"); ok && rport != nil && rport.String() != "" {
			if p, err := strconv.Atoi(rport.String()); err == nil {
				port = Port(uint16(p))
			}
		}
	}

	return fmt.Sprintf("%v:%v", host, port)
}

// RFC 3261 - 8.2.6
func NewResponseFromRequest(
	resID MessageID,
	req Request,
	statusCode StatusCode,
	reason string,
	body string,
) Response {
	res := NewResponse(
		resID,
		req.SipVersion(),
		statusCode,
		reason,
		[]Header{},
		"",
		req.Fields(),
	)
	CopyHeaders("Record-Route", req, res)
	CopyHeaders("Via", req, res)
	CopyHeaders("From", req, res)
	CopyHeaders("To", req, res)
	CopyHeaders("Call-ID", req, res)
	CopyHeaders("CSeq", req, res)

	if statusCode == 100 {
		CopyHeaders("Timestamp", req, res)
	}

	res.SetBody(body, true)

	res.SetTransport(req.Transport())
	res.SetSource(req.Destination())
	res.SetDestination(req.Source())

	return res
}

func cloneResponse(res Response, id MessageID, fields log.Fields) Response {
	newFields := res.Fields()
	if fields != nil {
		newFields = newFields.WithFields(fields)
	}

	newRes := NewResponse(
		id,
		res.SipVersion(),
		res.StatusCode(),
		res.Reason(),
		cloneHeaders(res),
		res.Body(),
		newFields,
	)
	newRes.SetPrevious(res.Previous())
	newRes.SetTransport(res.Transport())
	newRes.SetSource(res.Source())
	newRes.SetDestination(res.Destination())

	return newRes
}

func CopyResponse(res Response) Response {
	return cloneResponse(res, res.MessageID(), nil)
}
