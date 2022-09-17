package sip

import (
	"fmt"

	"github.com/ghettovoice/gosip/util"
)

type RequestBuilder struct {
	protocol        string
	protocolVersion string
	transport       string
	host            string
	method          RequestMethod
	cseq            *CSeq
	recipient       Uri
	body            string
	callID          *CallID
	via             ViaHeader
	from            *FromHeader
	to              *ToHeader
	contact         *ContactHeader
	expires         *Expires
	userAgent       *UserAgentHeader
	maxForwards     *MaxForwards
	supported       *SupportedHeader
	require         *RequireHeader
	allow           AllowHeader
	contentType     *ContentType
	accept          *Accept
	route           *RouteHeader
	generic         map[string]Header
}

func NewRequestBuilder() *RequestBuilder {
	callID := CallID(util.RandString(32))
	maxForwards := MaxForwards(70)
	userAgent := UserAgentHeader("GoSIP")
	rb := &RequestBuilder{
		protocol:        "SIP",
		protocolVersion: "2.0",
		transport:       "UDP",
		host:            "localhost",
		cseq:            &CSeq{SeqNo: 1},
		body:            "",
		via:             make(ViaHeader, 0),
		callID:          &callID,
		userAgent:       &userAgent,
		maxForwards:     &maxForwards,
		generic:         make(map[string]Header),
	}

	return rb
}

func (rb *RequestBuilder) SetTransport(transport string) *RequestBuilder {
	if transport == "" {
		rb.transport = "UDP"
	} else {
		rb.transport = transport
	}

	return rb
}

func (rb *RequestBuilder) SetHost(host string) *RequestBuilder {
	if host == "" {
		rb.host = "localhost"
	} else {
		rb.host = host
	}

	return rb
}

func (rb *RequestBuilder) SetMethod(method RequestMethod) *RequestBuilder {
	rb.method = method
	rb.cseq.MethodName = method

	return rb
}

func (rb *RequestBuilder) SetSeqNo(seqNo uint) *RequestBuilder {
	rb.cseq.SeqNo = uint32(seqNo)

	return rb
}

func (rb *RequestBuilder) SetRecipient(uri Uri) *RequestBuilder {
	rb.recipient = uri.Clone()

	return rb
}

func (rb *RequestBuilder) SetBody(body string) *RequestBuilder {
	rb.body = body

	return rb
}

func (rb *RequestBuilder) SetCallID(callID *CallID) *RequestBuilder {
	if callID != nil {
		rb.callID = callID
	}

	return rb
}

func (rb *RequestBuilder) AddVia(via *ViaHop) *RequestBuilder {
	if via.ProtocolName == "" {
		via.ProtocolName = rb.protocol
	}
	if via.ProtocolVersion == "" {
		via.ProtocolVersion = rb.protocolVersion
	}
	if via.Transport == "" {
		via.Transport = rb.transport
	}
	if via.Host == "" {
		via.Host = rb.host
	}
	if via.Params == nil {
		via.Params = NewParams()
	}

	rb.via = append(rb.via, via)

	return rb
}

func (rb *RequestBuilder) SetFrom(address *Address) *RequestBuilder {
	if address == nil {
		rb.from = nil
	} else {
		address = address.Clone()
		if address.Uri.Host() == "" {
			address.Uri.SetHost(rb.host)
		}
		rb.from = &FromHeader{
			DisplayName: address.DisplayName,
			Address:     address.Uri,
			Params:      address.Params,
		}
	}

	return rb
}

func (rb *RequestBuilder) SetTo(address *Address) *RequestBuilder {
	if address == nil {
		rb.to = nil
	} else {
		address = address.Clone()
		if address.Uri.Host() == "" {
			address.Uri.SetHost(rb.host)
		}
		rb.to = &ToHeader{
			DisplayName: address.DisplayName,
			Address:     address.Uri,
			Params:      address.Params,
		}
	}

	return rb
}

func (rb *RequestBuilder) SetContact(address *Address) *RequestBuilder {
	if address == nil {
		rb.contact = nil
	} else {
		address = address.Clone()
		if address.Uri.Host() == "" {
			address.Uri.SetHost(rb.host)
		}
		rb.contact = &ContactHeader{
			DisplayName: address.DisplayName,
			Address:     address.Uri,
			Params:      address.Params,
		}
	}

	return rb
}

func (rb *RequestBuilder) SetExpires(expires *Expires) *RequestBuilder {
	rb.expires = expires

	return rb
}

func (rb *RequestBuilder) SetUserAgent(userAgent *UserAgentHeader) *RequestBuilder {
	rb.userAgent = userAgent

	return rb
}

func (rb *RequestBuilder) SetMaxForwards(maxForwards *MaxForwards) *RequestBuilder {
	rb.maxForwards = maxForwards

	return rb
}

func (rb *RequestBuilder) SetAllow(methods []RequestMethod) *RequestBuilder {
	rb.allow = methods

	return rb
}

func (rb *RequestBuilder) SetSupported(options []string) *RequestBuilder {
	if len(options) == 0 {
		rb.supported = nil
	} else {
		rb.supported = &SupportedHeader{
			Options: options,
		}
	}

	return rb
}

func (rb *RequestBuilder) SetRequire(options []string) *RequestBuilder {
	if len(options) == 0 {
		rb.require = nil
	} else {
		rb.require = &RequireHeader{
			Options: options,
		}
	}

	return rb
}

func (rb *RequestBuilder) SetContentType(contentType *ContentType) *RequestBuilder {
	rb.contentType = contentType

	return rb
}

func (rb *RequestBuilder) SetAccept(accept *Accept) *RequestBuilder {
	rb.accept = accept

	return rb
}

func (rb *RequestBuilder) SetRoutes(routes []Uri) *RequestBuilder {
	if len(routes) == 0 {
		rb.route = nil
	} else {
		rb.route = &RouteHeader{
			Addresses: routes,
		}
	}

	return rb
}

func (rb *RequestBuilder) AddHeader(header Header) *RequestBuilder {
	rb.generic[header.Name()] = header

	return rb
}

func (rb *RequestBuilder) RemoveHeader(headerName string) *RequestBuilder {
	if _, ok := rb.generic[headerName]; ok {
		delete(rb.generic, headerName)
	}

	return rb
}

func (rb *RequestBuilder) Build() (Request, error) {
	if rb.method == "" {
		return nil, fmt.Errorf("undefined method name")
	}
	if rb.recipient == nil {
		return nil, fmt.Errorf("empty recipient")
	}
	if rb.from == nil {
		return nil, fmt.Errorf("empty 'From' header")
	}
	if rb.to == nil {
		return nil, fmt.Errorf("empty 'From' header")
	}

	hdrs := make([]Header, 0)

	if rb.route != nil {
		hdrs = append(hdrs, rb.route)
	}

	if len(rb.via) != 0 {
		via := make(ViaHeader, 0)
		for _, viaHop := range rb.via {
			via = append(via, viaHop)
		}
		hdrs = append(hdrs, via)
	}

	hdrs = append(hdrs, rb.cseq, rb.from, rb.to, rb.callID)

	if rb.contact != nil {
		hdrs = append(hdrs, rb.contact)
	}
	if rb.maxForwards != nil {
		hdrs = append(hdrs, rb.maxForwards)
	}
	if rb.expires != nil {
		hdrs = append(hdrs, rb.expires)
	}
	if rb.supported != nil {
		hdrs = append(hdrs, rb.supported)
	}
	if rb.allow != nil {
		hdrs = append(hdrs, rb.allow)
	}
	if rb.contentType != nil {
		hdrs = append(hdrs, rb.contentType)
	}
	if rb.accept != nil {
		hdrs = append(hdrs, rb.accept)
	}
	if rb.userAgent != nil {
		hdrs = append(hdrs, rb.userAgent)
	}

	for _, header := range rb.generic {
		hdrs = append(hdrs, header)
	}

	sipVersion := rb.protocol + "/" + rb.protocolVersion
	// basic request
	req := NewRequest("", rb.method, rb.recipient, sipVersion, hdrs, "", nil)
	req.SetBody(rb.body, true)

	return req, nil
}
