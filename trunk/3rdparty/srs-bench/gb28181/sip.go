// The MIT License (MIT)
//
// # Copyright (c) 2022 Winlin
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to
// use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
// the Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
// FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
// COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
// IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
// CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
package gb28181

import (
	"context"
	"fmt"
	"github.com/ghettovoice/gosip/log"
	"github.com/ghettovoice/gosip/sip"
	"github.com/ghettovoice/gosip/transport"
	"github.com/ossrs/go-oryx-lib/errors"
	"github.com/ossrs/go-oryx-lib/logger"
	"math/rand"
	"net/url"
	"strings"
	"sync"
	"time"
)

type SIPConfig struct {
	// The server address, for example: tcp://127.0.0.1:5060k
	addr string
	// The SIP domain, for example: ossrs.io or 3402000000
	domain string
	// The SIP device ID, for example: camera or 34020000001320000001
	user string
	// The N number of random device ID, for example, 10 means 1320000001
	random int
	// The SIP server ID, for example: srs or 34020000002000000001
	server string
	// The cached device id.
	deviceID string
}

// The global cache to avoid conflict of deviceID.
// Note that it's not coroutine safe, but it should be OK for utest.
var deviceIDCache map[string]bool

func init() {
	deviceIDCache = make(map[string]bool)
}

func (v *SIPConfig) DeviceID() string {
	for v.deviceID == "" {
		// Generate a random ID.
		var rid string
		for len(rid) < v.random {
			rid += fmt.Sprintf("%v", rand.Uint64())
		}
		deviceID := fmt.Sprintf("%v%v", v.user, rid[:v.random])

		// Ignore if exists.
		if _, ok := deviceIDCache[deviceID]; !ok {
			v.deviceID = deviceID
			deviceIDCache[deviceID] = true
		}
	}
	return v.deviceID
}

func (v *SIPConfig) String() string {
	sb := []string{}
	if v.addr != "" {
		sb = append(sb, fmt.Sprintf("addr=%v", v.addr))
	}
	if v.domain != "" {
		sb = append(sb, fmt.Sprintf("domain=%v", v.domain))
	}
	if v.user != "" {
		sb = append(sb, fmt.Sprintf("user=%v", v.user))
		sb = append(sb, fmt.Sprintf("deviceID=%v", v.DeviceID()))
	}
	if v.random > 0 {
		sb = append(sb, fmt.Sprintf("random=%v", v.random))
	}
	if v.server != "" {
		sb = append(sb, fmt.Sprintf("server=%v", v.server))
	}
	return strings.Join(sb, ",")
}

type SIPSession struct {
	conf      *SIPConfig
	rb        *sip.RequestBuilder
	requests  chan sip.Request
	responses chan sip.Response
	wg        sync.WaitGroup
	ctx       context.Context
	cancel    context.CancelFunc
	client    *SIPClient
	seq       uint
}

func NewSIPSession(c *SIPConfig) *SIPSession {
	return &SIPSession{
		conf: c, client: NewSIPClient(), rb: sip.NewRequestBuilder(),
		requests: make(chan sip.Request, 1024), responses: make(chan sip.Response, 1024),
		seq: 100,
	}
}

func (v *SIPSession) Close() error {
	if v.cancel != nil {
		v.cancel()
	}
	v.client.Close()
	v.wg.Wait()
	return nil
}

func (v *SIPSession) Connect(ctx context.Context) error {
	if ctx.Err() != nil {
		return ctx.Err()
	}

	ctx, cancel := context.WithCancel(ctx)
	v.ctx, v.cancel = ctx, cancel

	if err := v.client.Connect(ctx, v.conf.addr); err != nil {
		return errors.Wrapf(err, "connect with sipConfig %v", v.conf.String())
	}

	// Dispatch requests and responses.
	go func() {
		v.wg.Add(1)
		defer v.wg.Done()

		for {
			select {
			case <-v.ctx.Done():
				return
			case msg := <-v.client.incoming:
				if req, ok := msg.(sip.Request); ok {
					select {
					case v.requests <- req:
					case <-v.ctx.Done():
						return
					}
				} else if res, ok := msg.(sip.Response); ok {
					select {
					case v.responses <- res:
					case <-v.ctx.Done():
						return
					}
				} else {
					logger.Wf(ctx, "Drop message %v", msg.String())
				}
			}
		}
	}()

	return nil
}

func (v *SIPSession) Register(ctx context.Context) (sip.Message, sip.Message, error) {
	return v.doRegister(ctx, 3600)
}

func (v *SIPSession) UnRegister(ctx context.Context) (sip.Message, sip.Message, error) {
	return v.doRegister(ctx, 0)
}

func (v *SIPSession) doRegister(ctx context.Context, expires int) (sip.Message, sip.Message, error) {
	if ctx.Err() != nil {
		return nil, nil, ctx.Err()
	}

	sipPort := sip.Port(5060)
	sipCallID := sip.CallID(fmt.Sprintf("%v", rand.Uint64()))
	sipBranch := fmt.Sprintf("z9hG4bK_%v", rand.Uint32())
	sipTag := fmt.Sprintf("%v", rand.Uint32())
	sipMaxForwards := sip.MaxForwards(70)
	sipExpires := sip.Expires(uint32(expires))
	sipPIP := "192.168.3.99"
	v.seq++

	rb := v.rb
	rb.SetTransport("TCP")
	rb.SetMethod(sip.REGISTER)
	rb.AddVia(&sip.ViaHop{
		ProtocolName: "SIP", ProtocolVersion: "2.0", Transport: "TCP", Host: sipPIP, Port: &sipPort,
		Params: sip.NewParams().Add("branch", sip.String{Str: sipBranch}),
	})
	rb.SetFrom(&sip.Address{
		Uri:    &sip.SipUri{FUser: sip.String{v.conf.DeviceID()}, FHost: v.conf.domain},
		Params: sip.NewParams().Add("tag", sip.String{Str: sipTag}),
	})
	rb.SetTo(&sip.Address{
		Uri: &sip.SipUri{FUser: sip.String{v.conf.DeviceID()}, FHost: v.conf.domain},
	})
	rb.SetCallID(&sipCallID)
	rb.SetSeqNo(v.seq)
	rb.SetRecipient(&sip.SipUri{FUser: sip.String{v.conf.server}, FHost: v.conf.domain})
	rb.SetContact(&sip.Address{
		Uri: &sip.SipUri{FUser: sip.String{v.conf.DeviceID()}, FHost: sipPIP, FPort: &sipPort},
	})
	rb.SetMaxForwards(&sipMaxForwards)
	rb.SetExpires(&sipExpires)
	req, err := rb.Build()
	if err != nil {
		return req, nil, errors.Wrap(err, "build request")
	}

	if err = v.client.Send(req); err != nil {
		return req, nil, errors.Wrapf(err, "send request %v", req.String())
	}

	callID := sipGetCallID(req)
	if callID == "" {
		return req, nil, errors.Errorf("Invalid SIP Call-ID register %v", req.String())
	}
	logger.Tf(ctx, "Send REGISTER request, Call-ID=%v, Expires=%v", callID, expires)

	for {
		select {
		case <-ctx.Done():
			return nil, nil, ctx.Err()
		case <-v.ctx.Done():
			return nil, nil, v.ctx.Err()
		case msg := <-v.responses:
			if tv := sipGetCallID(msg); tv == callID {
				return req, msg, nil
			} else {
				logger.Wf(v.ctx, "Not callID=%v, msg=%v, drop message %v", callID, tv, msg.String())
			}
		}
	}
}

func (v *SIPSession) Trying(ctx context.Context, invite sip.Message) error {
	if ctx.Err() != nil {
		return ctx.Err()
	}

	req, ok := invite.(sip.Request)
	if !ok {
		return errors.Errorf("Invalid SIP request invite %v", invite.String())
	}

	res := sip.NewResponseFromRequest("", req, sip.StatusCode(100), "Trying", "")
	if err := v.client.Send(res); err != nil {
		return errors.Wrapf(err, "send response %v", res.String())
	}

	return nil
}

func (v *SIPSession) InviteResponse(ctx context.Context, invite sip.Message) (sip.Message, error) {
	if ctx.Err() != nil {
		return nil, ctx.Err()
	}

	req, ok := invite.(sip.Request)
	if !ok {
		return nil, errors.Errorf("Invalid SIP request invite %v", invite.String())
	}

	callID := sipGetCallID(invite)
	if callID == "" {
		return nil, errors.Errorf("Invalid SIP Call-ID invite %v", invite.String())
	}

	res := sip.NewResponseFromRequest("", req, sip.StatusCode(200), "OK", "")
	if err := v.client.Send(res); err != nil {
		return nil, errors.Wrapf(err, "send response %v", res.String())
	}
	logger.Tf(ctx, "Send INVITE response, Call-ID=%v", callID)

	for {
		select {
		case <-ctx.Done():
			return nil, ctx.Err()
		case <-v.ctx.Done():
			return nil, v.ctx.Err()
		case msg := <-v.requests:
			// Must be an ACK message.
			if !msg.IsAck() {
				return msg, errors.Errorf("invalid ACK message %v", msg.String())
			}

			// Check CALL-ID of ACK, should be equal to 200 OK.
			if tv := sipGetCallID(msg); tv == callID {
				return msg, nil
			} else {
				logger.Wf(v.ctx, "Not callID=%v, msg=%v, drop message %v", callID, tv, msg.String())
			}
		}
	}
}

func (v *SIPSession) Message(ctx context.Context) (sip.Message, sip.Message, error) {
	if ctx.Err() != nil {
		return nil, nil, ctx.Err()
	}

	sipPort := sip.Port(5060)
	sipCallID := sip.CallID(fmt.Sprintf("%v", rand.Uint64()))
	sipBranch := fmt.Sprintf("z9hG4bK_%v", rand.Uint32())
	sipTag := fmt.Sprintf("%v", rand.Uint32())
	sipMaxForwards := sip.MaxForwards(70)
	sipExpires := sip.Expires(3600)
	sipPIP := "192.168.3.99"
	v.seq++

	rb := v.rb
	rb.SetTransport("TCP")
	rb.SetMethod(sip.MESSAGE)
	rb.AddVia(&sip.ViaHop{
		ProtocolName: "SIP", ProtocolVersion: "2.0", Transport: "TCP", Host: sipPIP, Port: &sipPort,
		Params: sip.NewParams().Add("branch", sip.String{Str: sipBranch}),
	})
	rb.SetFrom(&sip.Address{
		Uri:    &sip.SipUri{FUser: sip.String{v.conf.DeviceID()}, FHost: v.conf.domain},
		Params: sip.NewParams().Add("tag", sip.String{Str: sipTag}),
	})
	rb.SetTo(&sip.Address{
		Uri: &sip.SipUri{FUser: sip.String{v.conf.server}, FHost: v.conf.domain},
	})
	rb.SetCallID(&sipCallID)
	rb.SetSeqNo(v.seq)
	rb.SetRecipient(&sip.SipUri{FUser: sip.String{v.conf.server}, FHost: v.conf.domain})
	rb.SetContact(&sip.Address{
		Uri: &sip.SipUri{FUser: sip.String{v.conf.DeviceID()}, FHost: sipPIP, FPort: &sipPort},
	})
	rb.SetMaxForwards(&sipMaxForwards)
	rb.SetExpires(&sipExpires)

	v.seq++
	rb.SetBody(strings.Join([]string{
		`<?xml version="1.0" encoding="GB2312"?>`,
		"<Notify>",
		"<CmdType>Keepalive</CmdType>",
		fmt.Sprintf("<SN>%v</SN>", v.seq),
		fmt.Sprintf("<DeviceID>%v</DeviceID>", v.conf.DeviceID()),
		"<Status>OK</Status>",
		"</Notify>\n",
	}, "\n"))

	req, err := rb.Build()
	if err != nil {
		return req, nil, errors.Wrap(err, "build request")
	}

	if err = v.client.Send(req); err != nil {
		return req, nil, errors.Wrapf(err, "send request %v", req.String())
	}

	callID := sipGetCallID(req)
	if callID == "" {
		return req, nil, errors.Errorf("Invalid SIP Call-ID message %v", req.String())
	}
	logger.Tf(ctx, "Send MESSAGE request, Call-ID=%v", callID)

	for {
		select {
		case <-ctx.Done():
			return nil, nil, ctx.Err()
		case <-v.ctx.Done():
			return nil, nil, v.ctx.Err()
		case msg := <-v.responses:
			if tv := sipGetCallID(msg); tv == callID {
				return req, msg, nil
			} else {
				logger.Wf(v.ctx, "Not callID=%v, msg=%v, drop message %v", callID, tv, msg.String())
			}
		}
	}
}

func (v *SIPSession) Bye(ctx context.Context) (sip.Message, sip.Message, error) {
	if ctx.Err() != nil {
		return nil, nil, ctx.Err()
	}

	sipPort := sip.Port(5060)
	sipCallID := sip.CallID(fmt.Sprintf("%v", rand.Uint64()))
	sipBranch := fmt.Sprintf("z9hG4bK_%v", rand.Uint32())
	sipTag := fmt.Sprintf("%v", rand.Uint32())
	sipMaxForwards := sip.MaxForwards(70)
	sipExpires := sip.Expires(3600)
	sipPIP := "192.168.3.99"
	v.seq++

	rb := v.rb
	rb.SetTransport("TCP")
	rb.SetMethod(sip.BYE)
	rb.AddVia(&sip.ViaHop{
		ProtocolName: "SIP", ProtocolVersion: "2.0", Transport: "TCP", Host: sipPIP, Port: &sipPort,
		Params: sip.NewParams().Add("branch", sip.String{Str: sipBranch}),
	})
	rb.SetFrom(&sip.Address{
		Uri:    &sip.SipUri{FUser: sip.String{v.conf.DeviceID()}, FHost: v.conf.domain},
		Params: sip.NewParams().Add("tag", sip.String{Str: sipTag}),
	})
	rb.SetTo(&sip.Address{
		Uri: &sip.SipUri{FUser: sip.String{v.conf.server}, FHost: v.conf.domain},
	})
	rb.SetCallID(&sipCallID)
	rb.SetSeqNo(v.seq)
	rb.SetRecipient(&sip.SipUri{FUser: sip.String{v.conf.server}, FHost: v.conf.domain})
	rb.SetContact(&sip.Address{
		Uri: &sip.SipUri{FUser: sip.String{v.conf.DeviceID()}, FHost: sipPIP, FPort: &sipPort},
	})
	rb.SetMaxForwards(&sipMaxForwards)
	rb.SetExpires(&sipExpires)

	req, err := rb.Build()
	if err != nil {
		return req, nil, errors.Wrap(err, "build request")
	}

	if err = v.client.Send(req); err != nil {
		return req, nil, errors.Wrapf(err, "send request %v", req.String())
	}

	callID := sipGetCallID(req)
	if callID == "" {
		return req, nil, errors.Errorf("Invalid SIP Call-ID bye %v", req.String())
	}
	logger.Tf(ctx, "Send BYE request, Call-ID=%v", callID)

	for {
		select {
		case <-ctx.Done():
			return nil, nil, ctx.Err()
		case <-v.ctx.Done():
			return nil, nil, v.ctx.Err()
		case msg := <-v.responses:
			if tv := sipGetCallID(msg); tv == callID {
				return req, msg, nil
			} else {
				logger.Wf(v.ctx, "Not callID=%v, msg=%v, drop message %v", callID, tv, msg.String())
			}
		}
	}
}

func (v *SIPSession) Wait(ctx context.Context, method sip.RequestMethod) (sip.Message, error) {
	if ctx.Err() != nil {
		return nil, ctx.Err()
	}

	for {
		select {
		case <-ctx.Done():
			return nil, ctx.Err()
		case <-v.ctx.Done():
			return nil, v.ctx.Err()
		case msg := <-v.requests:
			if r, ok := msg.(sip.Request); ok && r.Method() == method {
				return msg, nil
			} else {
				logger.Wf(v.ctx, "Not method=%v, drop message %v", method, msg.String())
			}
		}
	}
}

type SIPClient struct {
	ctx            context.Context
	cancel         context.CancelFunc
	incoming       chan sip.Message
	target         *transport.Target
	protocol       transport.Protocol
	cleanupTimeout time.Duration
}

func NewSIPClient() *SIPClient {
	return &SIPClient{
		cleanupTimeout: 5 * time.Second,
	}
}

func (v *SIPClient) Close() error {
	if v.cancel != nil {
		v.cancel()
	}

	// Wait for protocol stack to cleanup.
	if v.protocol != nil {
		select {
		case <-time.After(v.cleanupTimeout):
			logger.E(v.ctx, "Wait for protocol cleanup timeout")
		case <-v.protocol.Done():
			logger.T(v.ctx, "SIP protocol stack done")
		}
	}

	return nil
}

func (v *SIPClient) Connect(ctx context.Context, addr string) error {
	prURL, err := url.Parse(addr)
	if err != nil {
		return errors.Wrapf(err, "parse addr=%v", addr)
	}

	if prURL.Scheme != "tcp" && prURL.Scheme != "tcp4" {
		return errors.Errorf("invalid scheme=%v of addr=%v", prURL.Scheme, addr)
	}

	target, err := transport.NewTargetFromAddr(prURL.Host)
	if err != nil {
		return errors.Wrapf(err, "create target to %v", prURL.Host)
	}
	v.target = target

	incoming := make(chan sip.Message, 1024)
	errs := make(chan error, 1)
	cancels := make(chan struct{}, 1)
	protocol := transport.NewTcpProtocol(incoming, errs, cancels, nil, log.NewDefaultLogrusLogger())
	v.protocol = protocol
	v.incoming = incoming

	// Convert protocol stack errs to context signal.
	ctx, cancel := context.WithCancel(ctx)
	v.cancel = cancel
	v.ctx = ctx

	go func() {
		select {
		case <-ctx.Done():
			return
		case r0 := <-errs:
			logger.Ef(ctx, "SIP stack err %+v", r0)
			cancel()
		}
	}()

	// Covert context signal to cancels for protocol stack.
	go func() {
		<-ctx.Done()
		close(cancels)
		logger.Tf(ctx, "Notify SIP stack to cancel")
	}()

	return nil
}

func (v *SIPClient) Send(msg sip.Message) error {
	logger.Tf(v.ctx, "Send msg %v", msg.String())
	return v.protocol.Send(v.target, msg)
}
