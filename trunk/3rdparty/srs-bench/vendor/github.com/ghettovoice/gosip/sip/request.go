package sip

import (
	"bytes"
	"fmt"
	"strconv"
	"strings"

	"github.com/ghettovoice/gosip/log"
)

// Request RFC 3261 - 7.1.
type Request interface {
	Message
	Method() RequestMethod
	SetMethod(method RequestMethod)
	Recipient() Uri
	SetRecipient(recipient Uri)
	/* Common Helpers */
	IsInvite() bool
}

type request struct {
	message
	method    RequestMethod
	recipient Uri
}

func NewRequest(
	messID MessageID,
	method RequestMethod,
	recipient Uri,
	sipVersion string,
	hdrs []Header,
	body string,
	fields log.Fields,
) Request {
	req := new(request)
	if messID == "" {
		req.messID = NextMessageID()
	} else {
		req.messID = messID
	}
	req.startLine = req.StartLine
	req.sipVersion = sipVersion
	req.headers = newHeaders(hdrs)
	req.method = method
	req.recipient = recipient
	req.body = body
	req.fields = fields.WithFields(log.Fields{
		"request_id": req.messID,
	})

	return req
}

func (req *request) Short() string {
	if req == nil {
		return "<nil>"
	}

	fields := log.Fields{
		"method":      req.Method(),
		"recipient":   req.Recipient(),
		"transport":   req.Transport(),
		"source":      req.Source(),
		"destination": req.Destination(),
	}
	if cseq, ok := req.CSeq(); ok {
		fields["sequence"] = cseq.SeqNo
	}
	fields = req.Fields().WithFields(fields)

	return fmt.Sprintf("sip.Request<%s>", fields)
}

func (req *request) Method() RequestMethod {
	req.mu.RLock()
	defer req.mu.RUnlock()
	return req.method
}
func (req *request) SetMethod(method RequestMethod) {
	req.mu.Lock()
	req.method = method
	req.mu.Unlock()
}

func (req *request) Recipient() Uri {
	req.mu.RLock()
	defer req.mu.RUnlock()
	return req.recipient
}
func (req *request) SetRecipient(recipient Uri) {
	req.mu.Lock()
	req.recipient = recipient
	req.mu.Unlock()
}

// StartLine returns Request Line - RFC 2361 7.1.
func (req *request) StartLine() string {
	var buffer bytes.Buffer

	// Every SIP request starts with a Request Line - RFC 2361 7.1.
	buffer.WriteString(
		fmt.Sprintf(
			"%s %s %s",
			string(req.Method()),
			req.Recipient(),
			req.SipVersion(),
		),
	)

	return buffer.String()
}

func (req *request) Clone() Message {
	return cloneRequest(req, "", nil)
}

func (req *request) Fields() log.Fields {
	return req.fields.WithFields(log.Fields{
		"transport":   req.Transport(),
		"source":      req.Source(),
		"destination": req.Destination(),
	})
}

func (req *request) WithFields(fields log.Fields) Message {
	req.mu.Lock()
	req.fields = req.fields.WithFields(fields)
	req.mu.Unlock()

	return req
}

func (req *request) IsInvite() bool {
	return req.Method() == INVITE
}

func (req *request) IsAck() bool {
	return req.Method() == ACK
}

func (req *request) IsCancel() bool {
	return req.Method() == CANCEL
}

func (req *request) Transport() string {
	if tp := req.message.Transport(); tp != "" {
		return strings.ToUpper(tp)
	}

	var tp string
	if viaHop, ok := req.ViaHop(); ok && viaHop.Transport != "" {
		tp = viaHop.Transport
	} else {
		tp = DefaultProtocol
	}

	uri := req.Recipient()
	if hdrs := req.GetHeaders("Route"); len(hdrs) > 0 {
		routeHeader, ok := hdrs[0].(*RouteHeader)
		if ok && len(routeHeader.Addresses) > 0 {
			uri = routeHeader.Addresses[0]
		}
	}

	if uri != nil {
		if uri.UriParams() != nil {
			if val, ok := uri.UriParams().Get("transport"); ok && !val.Equals("") {
				tp = strings.ToUpper(val.String())
			}
		}

		if uri.IsEncrypted() {
			if tp == "TCP" {
				tp = "TLS"
			} else if tp == "WS" {
				tp = "WSS"
			}
		}
	}

	if tp == "UDP" && len(req.String()) > int(MTU)-200 {
		tp = "TCP"
	}

	return tp
}

func (req *request) Source() string {
	if src := req.message.Source(); src != "" {
		return src
	}

	viaHop, ok := req.ViaHop()
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
		port = DefaultPort(req.Transport())
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

func (req *request) Destination() string {
	if dest := req.message.Destination(); dest != "" {
		return dest
	}

	var uri *SipUri
	if hdrs := req.GetHeaders("Route"); len(hdrs) > 0 {
		routeHeader, ok := hdrs[0].(*RouteHeader)
		if ok && len(routeHeader.Addresses) > 0 {
			uri = routeHeader.Addresses[0].(*SipUri)
		}
	}
	if uri == nil {
		if u, ok := req.Recipient().(*SipUri); ok {
			uri = u
		} else {
			return ""
		}
	}

	host := uri.FHost
	var port Port
	if uri.FPort != nil {
		port = *uri.FPort
	} else {
		port = DefaultPort(req.Transport())
	}

	return fmt.Sprintf("%v:%v", host, port)
}

// NewAckRequest creates ACK request for 2xx INVITE
// https://tools.ietf.org/html/rfc3261#section-13.2.2.4
func NewAckRequest(ackID MessageID, inviteRequest Request, inviteResponse Response, body string, fields log.Fields) Request {
	recipient := inviteRequest.Recipient()
	if contact, ok := inviteResponse.Contact(); ok {
		// For ws and wss (like clients in browser), don't use Contact
		if strings.Index(strings.ToLower(recipient.String()), "transport=ws") == -1 {
			recipient = contact.Address
		}
	}
	ackRequest := NewRequest(
		ackID,
		ACK,
		recipient,
		inviteRequest.SipVersion(),
		[]Header{},
		body,
		inviteRequest.Fields().
			WithFields(fields).
			WithFields(log.Fields{
				"invite_request_id":  inviteRequest.MessageID(),
				"invite_response_id": inviteResponse.MessageID(),
			}),
	)

	CopyHeaders("Via", inviteRequest, ackRequest)
	if inviteResponse.IsSuccess() {
		// update branch, 2xx ACK is separate Tx
		viaHop, _ := ackRequest.ViaHop()
		viaHop.Params.Add("branch", String{Str: GenerateBranch()})
	}

	if len(inviteRequest.GetHeaders("Route")) > 0 {
		CopyHeaders("Route", inviteRequest, ackRequest)
	} else {
		hdrs := inviteResponse.GetHeaders("Record-Route")
		for i := len(hdrs) - 1; i >= 0; i-- {
			h := hdrs[i]
			uris := make([]Uri, 0)
			for j := len(h.(*RecordRouteHeader).Addresses) - 1; j >= 0; j-- {
				uris = append(uris, h.(*RecordRouteHeader).Addresses[j].Clone())
			}
			ackRequest.AppendHeader(&RouteHeader{
				Addresses: uris,
			})
		}
	}

	maxForwardsHeader := MaxForwards(70)
	ackRequest.AppendHeader(&maxForwardsHeader)
	CopyHeaders("From", inviteRequest, ackRequest)
	CopyHeaders("To", inviteResponse, ackRequest)
	CopyHeaders("Call-ID", inviteRequest, ackRequest)
	CopyHeaders("CSeq", inviteRequest, ackRequest)
	cseq, _ := ackRequest.CSeq()
	cseq.MethodName = ACK

	ackRequest.SetBody("", true)
	ackRequest.SetTransport(inviteRequest.Transport())
	ackRequest.SetSource(inviteRequest.Source())
	ackRequest.SetDestination(inviteRequest.Destination())

	return ackRequest
}

func NewCancelRequest(cancelID MessageID, requestForCancel Request, fields log.Fields) Request {
	cancelReq := NewRequest(
		cancelID,
		CANCEL,
		requestForCancel.Recipient(),
		requestForCancel.SipVersion(),
		[]Header{},
		"",
		requestForCancel.Fields().
			WithFields(fields).
			WithFields(log.Fields{
				"cancelling_request_id": requestForCancel.MessageID(),
			}),
	)

	viaHop, _ := requestForCancel.ViaHop()
	cancelReq.AppendHeader(ViaHeader{viaHop.Clone()})
	CopyHeaders("Route", requestForCancel, cancelReq)
	maxForwardsHeader := MaxForwards(70)
	cancelReq.AppendHeader(&maxForwardsHeader)
	CopyHeaders("From", requestForCancel, cancelReq)
	CopyHeaders("To", requestForCancel, cancelReq)
	CopyHeaders("Call-ID", requestForCancel, cancelReq)
	CopyHeaders("CSeq", requestForCancel, cancelReq)
	cseq, _ := cancelReq.CSeq()
	cseq.MethodName = CANCEL

	cancelReq.SetBody("", true)
	cancelReq.SetTransport(requestForCancel.Transport())
	cancelReq.SetSource(requestForCancel.Source())
	cancelReq.SetDestination(requestForCancel.Destination())

	return cancelReq
}

func cloneRequest(req Request, id MessageID, fields log.Fields) Request {
	newFields := req.Fields()
	if fields != nil {
		newFields = newFields.WithFields(fields)
	}

	newReq := NewRequest(
		id,
		req.Method(),
		req.Recipient().Clone(),
		req.SipVersion(),
		cloneHeaders(req),
		req.Body(),
		newFields,
	)
	newReq.SetTransport(req.Transport())
	newReq.SetSource(req.Source())
	newReq.SetDestination(req.Destination())

	return newReq
}

func CopyRequest(req Request) Request {
	return cloneRequest(req, req.MessageID(), nil)
}
