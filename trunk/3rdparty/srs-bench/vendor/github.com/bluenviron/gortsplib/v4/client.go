/*
Package gortsplib is a RTSP 1.0 library for the Go programming language.

Examples are available at https://github.com/bluenviron/gortsplib/tree/main/examples
*/
package gortsplib

import (
	"context"
	"crypto/tls"
	"fmt"
	"log"
	"net"
	"strconv"
	"strings"
	"sync"
	"sync/atomic"
	"time"

	"github.com/pion/rtcp"
	"github.com/pion/rtp"

	"github.com/bluenviron/gortsplib/v4/pkg/auth"
	"github.com/bluenviron/gortsplib/v4/pkg/base"
	"github.com/bluenviron/gortsplib/v4/pkg/bytecounter"
	"github.com/bluenviron/gortsplib/v4/pkg/conn"
	"github.com/bluenviron/gortsplib/v4/pkg/description"
	"github.com/bluenviron/gortsplib/v4/pkg/format"
	"github.com/bluenviron/gortsplib/v4/pkg/headers"
	"github.com/bluenviron/gortsplib/v4/pkg/liberrors"
	"github.com/bluenviron/gortsplib/v4/pkg/rtcpreceiver"
	"github.com/bluenviron/gortsplib/v4/pkg/rtcpsender"
	"github.com/bluenviron/gortsplib/v4/pkg/rtptime"
	"github.com/bluenviron/gortsplib/v4/pkg/sdp"
)

const (
	clientUserAgent = "gortsplib"
)

// avoid an int64 overflow and preserve resolution by splitting division into two parts:
// first add the integer part, then the decimal part.
func multiplyAndDivide(v, m, d time.Duration) time.Duration {
	secs := v / d
	dec := v % d
	return (secs*m + dec*m/d)
}

// convert an URL into an address, in particular:
// * add default port
// * handle IPv6 with or without square brackets.
// Adapted from net/http:
// https://cs.opensource.google/go/go/+/refs/tags/go1.20.5:src/net/http/transport.go;l=2747
func canonicalAddr(u *base.URL) string {
	addr := u.Hostname()

	port := u.Port()
	if port == "" {
		if u.Scheme == "rtsp" {
			port = "554"
		} else { // rtsps
			port = "322"
		}
	}

	return net.JoinHostPort(addr, port)
}

func isAnyPort(p int) bool {
	return p == 0 || p == 1
}

func findBaseURL(sd *sdp.SessionDescription, res *base.Response, u *base.URL) (*base.URL, error) {
	// use global control attribute
	if control, ok := sd.Attribute("control"); ok && control != "*" {
		ret, err := base.ParseURL(control)
		if err != nil {
			return nil, fmt.Errorf("invalid control attribute: '%v'", control)
		}

		// add credentials
		ret.User = u.User

		return ret, nil
	}

	// use Content-Base
	if cb, ok := res.Header["Content-Base"]; ok {
		if len(cb) != 1 {
			return nil, fmt.Errorf("invalid Content-Base: '%v'", cb)
		}

		if strings.HasPrefix(cb[0], "/") {
			// parse as a relative path
			ret, err := base.ParseURL(u.Scheme + "://" + u.Host + cb[0])
			if err != nil {
				return nil, fmt.Errorf("invalid Content-Base: '%v'", cb)
			}

			// add credentials
			ret.User = u.User

			return ret, nil
		}

		ret, err := base.ParseURL(cb[0])
		if err != nil {
			return nil, fmt.Errorf("invalid Content-Base: '%v'", cb)
		}

		// add credentials
		ret.User = u.User

		return ret, nil
	}

	// use URL of request
	return u, nil
}

func prepareForAnnounce(desc *description.Session) {
	for i, media := range desc.Medias {
		media.Control = "trackID=" + strconv.FormatInt(int64(i), 10)
	}
}

func supportsGetParameter(header base.Header) bool {
	pub, ok := header["Public"]
	if !ok || len(pub) != 1 {
		return false
	}

	for _, m := range strings.Split(pub[0], ",") {
		if base.Method(strings.Trim(m, " ")) == base.GetParameter {
			return true
		}
	}
	return false
}

type clientState int

const (
	clientStateInitial clientState = iota
	clientStatePrePlay
	clientStatePlay
	clientStatePreRecord
	clientStateRecord
)

func (s clientState) String() string {
	switch s {
	case clientStateInitial:
		return "initial"
	case clientStatePrePlay:
		return "prePlay"
	case clientStatePlay:
		return "play"
	case clientStatePreRecord:
		return "preRecord"
	case clientStateRecord:
		return "record"
	}
	return "unknown"
}

type optionsReq struct {
	url *base.URL
	res chan clientRes
}

type describeReq struct {
	url *base.URL
	res chan clientRes
}

type announceReq struct {
	url  *base.URL
	desc *description.Session
	res  chan clientRes
}

type setupReq struct {
	baseURL  *base.URL
	media    *description.Media
	rtpPort  int
	rtcpPort int
	res      chan clientRes
}

type playReq struct {
	ra  *headers.Range
	res chan clientRes
}

type recordReq struct {
	res chan clientRes
}

type pauseReq struct {
	res chan clientRes
}

type clientRes struct {
	sd  *description.Session // describe only
	res *base.Response
	err error
}

// ClientOnRequestFunc is the prototype of Client.OnRequest.
type ClientOnRequestFunc func(*base.Request)

// ClientOnResponseFunc is the prototype of Client.OnResponse.
type ClientOnResponseFunc func(*base.Response)

// ClientOnTransportSwitchFunc is the prototype of Client.OnTransportSwitch.
type ClientOnTransportSwitchFunc func(err error)

// ClientOnPacketLostFunc is the prototype of Client.OnPacketLost.
//
// Deprecated: replaced by ClientOnPacketsLostFunc
type ClientOnPacketLostFunc func(err error)

// ClientOnPacketsLostFunc is the prototype of Client.OnPacketsLost.
type ClientOnPacketsLostFunc func(lost uint64)

// ClientOnDecodeErrorFunc is the prototype of Client.OnDecodeError.
type ClientOnDecodeErrorFunc func(err error)

// OnPacketRTPFunc is the prototype of the callback passed to OnPacketRTP().
type OnPacketRTPFunc func(*rtp.Packet)

// OnPacketRTPAnyFunc is the prototype of the callback passed to OnPacketRTP(Any).
type OnPacketRTPAnyFunc func(*description.Media, format.Format, *rtp.Packet)

// OnPacketRTCPFunc is the prototype of the callback passed to OnPacketRTCP().
type OnPacketRTCPFunc func(rtcp.Packet)

// OnPacketRTCPAnyFunc is the prototype of the callback passed to OnPacketRTCPAny().
type OnPacketRTCPAnyFunc func(*description.Media, rtcp.Packet)

// Client is a RTSP client.
type Client struct {
	//
	// RTSP parameters (all optional)
	//
	// timeout of read operations.
	// It defaults to 10 seconds.
	ReadTimeout time.Duration
	// timeout of write operations.
	// It defaults to 10 seconds.
	WriteTimeout time.Duration
	// a TLS configuration to connect to TLS (RTSPS) servers.
	// It defaults to nil.
	TLSConfig *tls.Config
	// enable communication with servers which don't provide UDP server ports
	// or use different server ports than the announced ones.
	// This can be a security issue.
	// It defaults to false.
	AnyPortEnable bool
	// transport protocol (UDP, Multicast or TCP).
	// If nil, it is chosen automatically (first UDP, then, if it fails, TCP).
	// It defaults to nil.
	Transport *Transport
	// If the client is reading with UDP, it must receive
	// at least a packet within this timeout, otherwise it switches to TCP.
	// It defaults to 3 seconds.
	InitialUDPReadTimeout time.Duration
	// Size of the queue of outgoing packets.
	// It defaults to 256.
	WriteQueueSize int
	// maximum size of outgoing RTP / RTCP packets.
	// This must be less than the UDP MTU (1472 bytes).
	// It defaults to 1472.
	MaxPacketSize int
	// user agent header.
	// It defaults to "gortsplib"
	UserAgent string
	// disable automatic RTCP sender reports.
	DisableRTCPSenderReports bool
	// explicitly request back channels to the server.
	RequestBackChannels bool
	// pointer to a variable that stores received bytes.
	//
	// Deprecated: use Client.Stats()
	BytesReceived *uint64
	// pointer to a variable that stores sent bytes.
	//
	// Deprecated: use Client.Stats()
	BytesSent *uint64

	//
	// system functions (all optional)
	//
	// function used to initialize the TCP client.
	// It defaults to (&net.Dialer{}).DialContext.
	DialContext func(ctx context.Context, network, address string) (net.Conn, error)
	// function used to initialize UDP listeners.
	// It defaults to net.ListenPacket.
	ListenPacket func(network, address string) (net.PacketConn, error)

	//
	// callbacks (all optional)
	//
	// called when sending a request to the server.
	OnRequest ClientOnRequestFunc
	// called when receiving a response from the server.
	OnResponse ClientOnResponseFunc
	// called when receiving a request from the server.
	OnServerRequest ClientOnRequestFunc
	// called when sending a response to the server.
	OnServerResponse ClientOnResponseFunc
	// called when the transport protocol changes.
	OnTransportSwitch ClientOnTransportSwitchFunc
	// called when the client detects lost packets.
	//
	// Deprecated: replaced by OnPacketsLost
	OnPacketLost ClientOnPacketLostFunc
	// called when the client detects lost packets.
	OnPacketsLost ClientOnPacketsLostFunc
	// called when a non-fatal decode error occurs.
	OnDecodeError ClientOnDecodeErrorFunc

	//
	// private
	//

	timeNow              func() time.Time
	senderReportPeriod   time.Duration
	receiverReportPeriod time.Duration
	checkTimeoutPeriod   time.Duration

	connURL              *base.URL
	ctx                  context.Context
	ctxCancel            func()
	state                clientState
	nconn                net.Conn
	conn                 *conn.Conn
	session              string
	sender               *auth.Sender
	cseq                 int
	optionsSent          bool
	useGetParameter      bool
	lastDescribeURL      *base.URL
	baseURL              *base.URL
	effectiveTransport   *Transport
	backChannelSetupped  bool
	stdChannelSetupped   bool
	setuppedMedias       map[*description.Media]*clientMedia
	tcpCallbackByChannel map[int]readFunc
	lastRange            *headers.Range
	checkTimeoutTimer    *time.Timer
	checkTimeoutInitial  bool
	tcpLastFrameTime     *int64
	keepalivePeriod      time.Duration
	keepaliveTimer       *time.Timer
	closeError           error
	writer               *asyncProcessor
	writerMutex          sync.RWMutex
	reader               *clientReader
	timeDecoder          *rtptime.GlobalDecoder2
	mustClose            bool
	tcpFrame             *base.InterleavedFrame
	tcpBuffer            []byte
	bytesReceived        *uint64
	bytesSent            *uint64

	// in
	chOptions  chan optionsReq
	chDescribe chan describeReq
	chAnnounce chan announceReq
	chSetup    chan setupReq
	chPlay     chan playReq
	chRecord   chan recordReq
	chPause    chan pauseReq

	// out
	done chan struct{}
}

// Start initializes the connection to a server.
func (c *Client) Start(scheme string, host string) error {
	// RTSP parameters
	if c.ReadTimeout == 0 {
		c.ReadTimeout = 10 * time.Second
	}
	if c.WriteTimeout == 0 {
		c.WriteTimeout = 10 * time.Second
	}
	if c.InitialUDPReadTimeout == 0 {
		c.InitialUDPReadTimeout = 3 * time.Second
	}
	if c.WriteQueueSize == 0 {
		c.WriteQueueSize = 256
	} else if (c.WriteQueueSize & (c.WriteQueueSize - 1)) != 0 {
		return fmt.Errorf("WriteQueueSize must be a power of two")
	}
	if c.MaxPacketSize == 0 {
		c.MaxPacketSize = udpMaxPayloadSize
	} else if c.MaxPacketSize > udpMaxPayloadSize {
		return fmt.Errorf("MaxPacketSize must be less than %d", udpMaxPayloadSize)
	}
	if c.UserAgent == "" {
		c.UserAgent = clientUserAgent
	}

	// system functions
	if c.DialContext == nil {
		c.DialContext = (&net.Dialer{}).DialContext
	}
	if c.ListenPacket == nil {
		c.ListenPacket = net.ListenPacket
	}

	// callbacks
	if c.OnRequest == nil {
		c.OnRequest = func(*base.Request) {
		}
	}
	if c.OnResponse == nil {
		c.OnResponse = func(*base.Response) {
		}
	}
	if c.OnServerRequest == nil {
		c.OnServerRequest = func(*base.Request) {
		}
	}
	if c.OnServerResponse == nil {
		c.OnServerResponse = func(*base.Response) {
		}
	}
	if c.OnTransportSwitch == nil {
		c.OnTransportSwitch = func(err error) {
			log.Println(err.Error())
		}
	}
	if c.OnPacketLost != nil {
		c.OnPacketsLost = func(lost uint64) {
			c.OnPacketLost(liberrors.ErrClientRTPPacketsLost{Lost: uint(lost)}) //nolint:staticcheck
		}
	}
	if c.OnPacketsLost == nil {
		c.OnPacketsLost = func(lost uint64) {
			log.Printf("%d RTP %s lost",
				lost,
				func() string {
					if lost == 1 {
						return "packet"
					}
					return "packets"
				}())
		}
	}
	if c.OnDecodeError == nil {
		c.OnDecodeError = func(err error) {
			log.Println(err.Error())
		}
	}

	// private
	if c.timeNow == nil {
		c.timeNow = time.Now
	}
	if c.senderReportPeriod == 0 {
		c.senderReportPeriod = 10 * time.Second
	}
	if c.receiverReportPeriod == 0 {
		// some cameras require a maximum of 5secs between keepalives
		c.receiverReportPeriod = 5 * time.Second
	}
	if c.checkTimeoutPeriod == 0 {
		c.checkTimeoutPeriod = 1 * time.Second
	}

	ctx, ctxCancel := context.WithCancel(context.Background())

	c.connURL = &base.URL{
		Scheme: scheme,
		Host:   host,
	}
	c.ctx = ctx
	c.ctxCancel = ctxCancel
	c.checkTimeoutTimer = emptyTimer()
	c.keepalivePeriod = 30 * time.Second
	c.keepaliveTimer = emptyTimer()

	if c.BytesReceived != nil {
		c.bytesReceived = c.BytesReceived
	} else {
		c.bytesReceived = new(uint64)
	}
	if c.BytesSent != nil {
		c.bytesSent = c.BytesSent
	} else {
		c.bytesSent = new(uint64)
	}

	c.chOptions = make(chan optionsReq)
	c.chDescribe = make(chan describeReq)
	c.chAnnounce = make(chan announceReq)
	c.chSetup = make(chan setupReq)
	c.chPlay = make(chan playReq)
	c.chRecord = make(chan recordReq)
	c.chPause = make(chan pauseReq)
	c.done = make(chan struct{})

	go c.run()

	return nil
}

// StartRecording connects to the address and starts publishing given media.
func (c *Client) StartRecording(address string, desc *description.Session) error {
	u, err := base.ParseURL(address)
	if err != nil {
		return err
	}

	err = c.Start(u.Scheme, u.Host)
	if err != nil {
		return err
	}

	_, err = c.Announce(u, desc)
	if err != nil {
		c.Close()
		return err
	}

	err = c.SetupAll(u, desc.Medias)
	if err != nil {
		c.Close()
		return err
	}

	_, err = c.Record()
	if err != nil {
		c.Close()
		return err
	}

	return nil
}

// Close closes all client resources and waits for them to close.
func (c *Client) Close() {
	c.ctxCancel()
	<-c.done
}

// Wait waits until all client resources are closed.
// This can happen when a fatal error occurs or when Close() is called.
func (c *Client) Wait() error {
	<-c.done
	return c.closeError
}

func (c *Client) run() {
	defer close(c.done)

	c.closeError = c.runInner()

	c.ctxCancel()

	c.doClose()
}

func (c *Client) runInner() error {
	for {
		chReaderResponse := func() chan *base.Response {
			if c.reader != nil {
				return c.reader.chResponse
			}
			return nil
		}()

		chReaderRequest := func() chan *base.Request {
			if c.reader != nil {
				return c.reader.chRequest
			}
			return nil
		}()

		chReaderError := func() chan error {
			if c.reader != nil {
				return c.reader.chError
			}
			return nil
		}()

		chWriterError := func() chan struct{} {
			if c.writer != nil {
				return c.writer.chStopped
			}
			return nil
		}()

		select {
		case req := <-c.chOptions:
			res, err := c.doOptions(req.url)
			req.res <- clientRes{res: res, err: err}

			if c.mustClose {
				return err
			}

		case req := <-c.chDescribe:
			sd, res, err := c.doDescribe(req.url)
			req.res <- clientRes{sd: sd, res: res, err: err}

			if c.mustClose {
				return err
			}

		case req := <-c.chAnnounce:
			res, err := c.doAnnounce(req.url, req.desc)
			req.res <- clientRes{res: res, err: err}

			if c.mustClose {
				return err
			}

		case req := <-c.chSetup:
			res, err := c.doSetup(req.baseURL, req.media, req.rtpPort, req.rtcpPort)
			req.res <- clientRes{res: res, err: err}

			if c.mustClose {
				return err
			}

		case req := <-c.chPlay:
			res, err := c.doPlay(req.ra)
			req.res <- clientRes{res: res, err: err}

			if c.mustClose {
				return err
			}

		case req := <-c.chRecord:
			res, err := c.doRecord()
			req.res <- clientRes{res: res, err: err}

			if c.mustClose {
				return err
			}

		case req := <-c.chPause:
			res, err := c.doPause()
			req.res <- clientRes{res: res, err: err}

			if c.mustClose {
				return err
			}

		case <-c.checkTimeoutTimer.C:
			err := c.doCheckTimeout()
			if err != nil {
				return err
			}
			c.checkTimeoutTimer = time.NewTimer(c.checkTimeoutPeriod)

		case <-c.keepaliveTimer.C:
			err := c.doKeepAlive()
			if err != nil {
				return err
			}
			c.keepaliveTimer = time.NewTimer(c.keepalivePeriod)

		case <-chWriterError:
			return c.writer.stopError

		case err := <-chReaderError:
			c.reader = nil
			return err

		case res := <-chReaderResponse:
			c.OnResponse(res)
			// these are responses to keepalives, ignore them.

		case req := <-chReaderRequest:
			err := c.handleServerRequest(req)
			if err != nil {
				return err
			}

		case <-c.ctx.Done():
			return liberrors.ErrClientTerminated{}
		}
	}
}

func (c *Client) waitResponse(requestCseqStr string) (*base.Response, error) {
	t := time.NewTimer(c.ReadTimeout)
	defer t.Stop()

	for {
		select {
		case <-t.C:
			return nil, liberrors.ErrClientRequestTimedOut{}

		case err := <-c.reader.chError:
			c.reader = nil
			return nil, err

		case res := <-c.reader.chResponse:
			c.OnResponse(res)

			// accept response if CSeq equals request CSeq, or if CSeq is not present
			if cseq, ok := res.Header["CSeq"]; !ok || len(cseq) != 1 || strings.TrimSpace(cseq[0]) == requestCseqStr {
				return res, nil
			}

		case req := <-c.reader.chRequest:
			err := c.handleServerRequest(req)
			if err != nil {
				return nil, err
			}

		case <-c.ctx.Done():
			return nil, liberrors.ErrClientTerminated{}
		}
	}
}

func (c *Client) handleServerRequest(req *base.Request) error {
	c.OnServerRequest(req)

	if req.Method != base.Options {
		return liberrors.ErrClientUnhandledMethod{Method: req.Method}
	}

	h := base.Header{
		"User-Agent": base.HeaderValue{c.UserAgent},
	}

	if cseq, ok := req.Header["CSeq"]; ok {
		h["CSeq"] = cseq
	}

	res := &base.Response{
		StatusCode: base.StatusOK,
		Header:     h,
	}

	c.OnServerResponse(res)

	c.nconn.SetWriteDeadline(time.Now().Add(c.WriteTimeout))
	return c.conn.WriteResponse(res)
}

func (c *Client) doClose() {
	if c.state == clientStatePlay || c.state == clientStateRecord {
		c.destroyWriter()
		c.stopTransportRoutines()
	}

	if c.nconn != nil && c.baseURL != nil {
		header := base.Header{}

		if c.backChannelSetupped {
			header["Require"] = base.HeaderValue{"www.onvif.org/ver20/backchannel"}
		}

		c.do(&base.Request{ //nolint:errcheck
			Method: base.Teardown,
			URL:    c.baseURL,
			Header: header,
		}, true)
	}

	if c.reader != nil {
		c.nconn.Close()
		c.reader.wait()
		c.reader = nil
		c.nconn = nil
		c.conn = nil
	} else if c.nconn != nil {
		c.nconn.Close()
		c.nconn = nil
		c.conn = nil
	}

	for _, cm := range c.setuppedMedias {
		cm.close()
	}
}

func (c *Client) reset() {
	c.doClose()

	c.state = clientStateInitial
	c.session = ""
	c.sender = nil
	c.cseq = 0
	c.optionsSent = false
	c.useGetParameter = false
	c.baseURL = nil
	c.effectiveTransport = nil
	c.backChannelSetupped = false
	c.stdChannelSetupped = false
	c.setuppedMedias = nil
	c.tcpCallbackByChannel = nil
}

func (c *Client) checkState(allowed map[clientState]struct{}) error {
	if _, ok := allowed[c.state]; ok {
		return nil
	}

	allowedList := make([]fmt.Stringer, len(allowed))
	i := 0
	for a := range allowed {
		allowedList[i] = a
		i++
	}

	return liberrors.ErrClientInvalidState{AllowedList: allowedList, State: c.state}
}

func (c *Client) trySwitchingProtocol() error {
	c.OnTransportSwitch(liberrors.ErrClientSwitchToTCP{})

	prevConnURL := c.connURL
	prevBaseURL := c.baseURL
	prevMedias := c.setuppedMedias

	c.reset()

	v := TransportTCP
	c.effectiveTransport = &v
	c.connURL = prevConnURL

	// some Hikvision cameras require a describe before a setup
	_, _, err := c.doDescribe(c.lastDescribeURL)
	if err != nil {
		return err
	}

	for i, cm := range prevMedias {
		_, err = c.doSetup(prevBaseURL, cm.media, 0, 0)
		if err != nil {
			return err
		}

		c.setuppedMedias[i].onPacketRTCP = cm.onPacketRTCP
		for j, tr := range cm.formats {
			c.setuppedMedias[i].formats[j].onPacketRTP = tr.onPacketRTP
		}
	}

	_, err = c.doPlay(c.lastRange)
	if err != nil {
		return err
	}

	return nil
}

func (c *Client) trySwitchingProtocol2(medi *description.Media, baseURL *base.URL) (*base.Response, error) {
	c.OnTransportSwitch(liberrors.ErrClientSwitchToTCP2{})

	prevConnURL := c.connURL

	c.reset()

	v := TransportTCP
	c.effectiveTransport = &v
	c.connURL = prevConnURL

	// some Hikvision cameras require a describe before a setup
	_, _, err := c.doDescribe(c.lastDescribeURL)
	if err != nil {
		return nil, err
	}

	return c.doSetup(baseURL, medi, 0, 0)
}

func (c *Client) startTransportRoutines() {
	c.timeDecoder = &rtptime.GlobalDecoder2{}
	c.timeDecoder.Initialize()

	for _, cm := range c.setuppedMedias {
		cm.start()
	}

	if *c.effectiveTransport == TransportTCP {
		c.tcpFrame = &base.InterleavedFrame{}
		c.tcpBuffer = make([]byte, c.MaxPacketSize+4)
	}

	if c.state == clientStatePlay && c.stdChannelSetupped {
		c.keepaliveTimer = time.NewTimer(c.keepalivePeriod)

		switch *c.effectiveTransport {
		case TransportUDP:
			c.checkTimeoutTimer = time.NewTimer(c.InitialUDPReadTimeout)
			c.checkTimeoutInitial = true

		case TransportUDPMulticast:
			c.checkTimeoutTimer = time.NewTimer(c.checkTimeoutPeriod)

		default: // TCP
			c.checkTimeoutTimer = time.NewTimer(c.checkTimeoutPeriod)
			v := c.timeNow().Unix()
			c.tcpLastFrameTime = &v
		}
	}

	if *c.effectiveTransport == TransportTCP {
		c.reader.setAllowInterleavedFrames(true)
	}
}

func (c *Client) stopTransportRoutines() {
	if c.reader != nil {
		c.reader.setAllowInterleavedFrames(false)
	}

	c.checkTimeoutTimer = emptyTimer()
	c.keepaliveTimer = emptyTimer()

	for _, cm := range c.setuppedMedias {
		cm.stop()
	}

	c.timeDecoder = nil
}

func (c *Client) createWriter() {
	c.writerMutex.Lock()

	c.writer = &asyncProcessor{
		bufferSize: func() int {
			if c.state == clientStateRecord || c.backChannelSetupped {
				return c.WriteQueueSize
			}

			// when reading, buffer is only used to send RTCP receiver reports,
			// that are much smaller than RTP packets and are sent at a fixed interval.
			// decrease RAM consumption by allocating less buffers.
			return 8
		}(),
	}

	c.writer.initialize()

	c.writerMutex.Unlock()
}

func (c *Client) startWriter() {
	c.writer.start()
}

func (c *Client) destroyWriter() {
	c.writer.close()

	c.writerMutex.Lock()
	c.writer = nil
	c.writerMutex.Unlock()
}

func (c *Client) connOpen() error {
	if c.nconn != nil {
		return nil
	}

	if c.connURL.Scheme != "rtsp" && c.connURL.Scheme != "rtsps" {
		return liberrors.ErrClientUnsupportedScheme{Scheme: c.connURL.Scheme}
	}

	if c.connURL.Scheme == "rtsps" && c.Transport != nil && *c.Transport != TransportTCP {
		return liberrors.ErrClientRTSPSTCP{}
	}

	dialCtx, dialCtxCancel := context.WithTimeout(c.ctx, c.ReadTimeout)
	defer dialCtxCancel()

	nconn, err := c.DialContext(dialCtx, "tcp", canonicalAddr(c.connURL))
	if err != nil {
		return err
	}

	if c.connURL.Scheme == "rtsps" {
		tlsConfig := c.TLSConfig
		if tlsConfig == nil {
			tlsConfig = &tls.Config{}
		}
		tlsConfig.ServerName = c.connURL.Hostname()

		nconn = tls.Client(nconn, tlsConfig)
	}

	c.nconn = nconn
	bc := bytecounter.New(c.nconn, c.bytesReceived, c.bytesSent)
	c.conn = conn.NewConn(bc)
	c.reader = &clientReader{
		c: c,
	}
	c.reader.start()

	return nil
}

func (c *Client) do(req *base.Request, skipResponse bool) (*base.Response, error) {
	if !c.optionsSent && req.Method != base.Options {
		_, err := c.doOptions(req.URL)
		if err != nil {
			return nil, err
		}
	}

	if req.Header == nil {
		req.Header = make(base.Header)
	}

	if c.session != "" {
		req.Header["Session"] = base.HeaderValue{c.session}
	}

	c.cseq++
	cseqStr := strconv.FormatInt(int64(c.cseq), 10)
	req.Header["CSeq"] = base.HeaderValue{cseqStr}

	req.Header["User-Agent"] = base.HeaderValue{c.UserAgent}

	if c.sender != nil {
		c.sender.AddAuthorization(req)
	}

	c.OnRequest(req)

	c.nconn.SetWriteDeadline(time.Now().Add(c.WriteTimeout))
	err := c.conn.WriteRequest(req)
	if err != nil {
		return nil, err
	}

	if skipResponse {
		return nil, nil
	}

	res, err := c.waitResponse(cseqStr)
	if err != nil {
		c.mustClose = true
		return nil, err
	}

	// get session from response
	if v, ok := res.Header["Session"]; ok {
		var sx headers.Session
		err = sx.Unmarshal(v)
		if err != nil {
			return nil, liberrors.ErrClientSessionHeaderInvalid{Err: err}
		}
		c.session = sx.Session

		if sx.Timeout != nil && *sx.Timeout > 0 {
			c.keepalivePeriod = time.Duration(*sx.Timeout) * time.Second * 8 / 10
		}
	}

	// send request again with authentication
	if res.StatusCode == base.StatusUnauthorized && req.URL.User != nil && c.sender == nil {
		pass, _ := req.URL.User.Password()
		user := req.URL.User.Username()

		sender := &auth.Sender{
			WWWAuth: res.Header["WWW-Authenticate"],
			User:    user,
			Pass:    pass,
		}
		err = sender.Initialize()
		if err != nil {
			return nil, liberrors.ErrClientAuthSetup{Err: err}
		}
		c.sender = sender

		return c.do(req, skipResponse)
	}

	return res, nil
}

func (c *Client) atLeastOneUDPPacketHasBeenReceived() bool {
	for _, ct := range c.setuppedMedias {
		lft := atomic.LoadInt64(ct.udpRTPListener.lastPacketTime)
		if lft != 0 {
			return true
		}

		lft = atomic.LoadInt64(ct.udpRTCPListener.lastPacketTime)
		if lft != 0 {
			return true
		}
	}
	return false
}

func (c *Client) isInUDPTimeout() bool {
	now := c.timeNow()
	for _, ct := range c.setuppedMedias {
		lft := time.Unix(atomic.LoadInt64(ct.udpRTPListener.lastPacketTime), 0)
		if now.Sub(lft) < c.ReadTimeout {
			return false
		}

		lft = time.Unix(atomic.LoadInt64(ct.udpRTCPListener.lastPacketTime), 0)
		if now.Sub(lft) < c.ReadTimeout {
			return false
		}
	}
	return true
}

func (c *Client) isInTCPTimeout() bool {
	now := c.timeNow()
	lft := time.Unix(atomic.LoadInt64(c.tcpLastFrameTime), 0)
	return now.Sub(lft) >= c.ReadTimeout
}

func (c *Client) doCheckTimeout() error {
	if *c.effectiveTransport == TransportUDP ||
		*c.effectiveTransport == TransportUDPMulticast {
		if c.checkTimeoutInitial && !c.backChannelSetupped && c.Transport == nil {
			c.checkTimeoutInitial = false

			if !c.atLeastOneUDPPacketHasBeenReceived() {
				err := c.trySwitchingProtocol()
				if err != nil {
					return err
				}
			}
		} else if c.isInUDPTimeout() {
			return liberrors.ErrClientUDPTimeout{}
		}
	} else if c.isInTCPTimeout() {
		return liberrors.ErrClientTCPTimeout{}
	}

	return nil
}

func (c *Client) doKeepAlive() error {
	// some cameras do not reply to keepalives, do not wait for responses.
	_, err := c.do(&base.Request{
		Method: func() base.Method {
			// the VLC integrated rtsp server requires GET_PARAMETER
			if c.useGetParameter {
				return base.GetParameter
			}
			return base.Options
		}(),
		// use the stream base URL, otherwise some cameras do not reply
		URL: c.baseURL,
	}, true)
	return err
}

func (c *Client) doOptions(u *base.URL) (*base.Response, error) {
	err := c.checkState(map[clientState]struct{}{
		clientStateInitial:   {},
		clientStatePrePlay:   {},
		clientStatePreRecord: {},
	})
	if err != nil {
		return nil, err
	}

	err = c.connOpen()
	if err != nil {
		return nil, err
	}

	res, err := c.do(&base.Request{
		Method: base.Options,
		URL:    u,
	}, false)
	if err != nil {
		return nil, err
	}

	if res.StatusCode != base.StatusOK {
		// since this method is not implemented by every RTSP server,
		// return an error only if status code is not 404
		if res.StatusCode == base.StatusNotFound {
			return res, nil
		}
		return nil, liberrors.ErrClientBadStatusCode{Code: res.StatusCode, Message: res.StatusMessage}
	}

	c.optionsSent = true
	c.useGetParameter = supportsGetParameter(res.Header)

	return res, nil
}

// Options sends an OPTIONS request.
func (c *Client) Options(u *base.URL) (*base.Response, error) {
	cres := make(chan clientRes)
	select {
	case c.chOptions <- optionsReq{url: u, res: cres}:
		res := <-cres
		return res.res, res.err

	case <-c.done:
		return nil, c.closeError
	}
}

func (c *Client) doDescribe(u *base.URL) (*description.Session, *base.Response, error) {
	err := c.checkState(map[clientState]struct{}{
		clientStateInitial:   {},
		clientStatePrePlay:   {},
		clientStatePreRecord: {},
	})
	if err != nil {
		return nil, nil, err
	}

	err = c.connOpen()
	if err != nil {
		return nil, nil, err
	}

	header := base.Header{
		"Accept": base.HeaderValue{"application/sdp"},
	}

	if c.RequestBackChannels {
		header["Require"] = base.HeaderValue{"www.onvif.org/ver20/backchannel"}
	}

	res, err := c.do(&base.Request{
		Method: base.Describe,
		URL:    u,
		Header: header,
	}, false)
	if err != nil {
		return nil, nil, err
	}

	if res.StatusCode != base.StatusOK {
		// redirect
		if res.StatusCode >= base.StatusMovedPermanently &&
			res.StatusCode <= base.StatusUseProxy &&
			len(res.Header["Location"]) == 1 {
			c.reset()

			var ru *base.URL
			ru, err = base.ParseURL(res.Header["Location"][0])
			if err != nil {
				return nil, nil, err
			}

			if u.User != nil {
				ru.User = u.User
			}

			c.connURL = &base.URL{
				Scheme: ru.Scheme,
				Host:   ru.Host,
			}

			return c.doDescribe(ru)
		}

		return nil, res, liberrors.ErrClientBadStatusCode{Code: res.StatusCode, Message: res.StatusMessage}
	}

	ct, ok := res.Header["Content-Type"]
	if !ok || len(ct) != 1 {
		return nil, nil, liberrors.ErrClientContentTypeMissing{}
	}

	// strip encoding information from Content-Type header
	ct = base.HeaderValue{strings.Split(ct[0], ";")[0]}

	if ct[0] != "application/sdp" {
		return nil, nil, liberrors.ErrClientContentTypeUnsupported{CT: ct}
	}

	var ssd sdp.SessionDescription
	err = ssd.Unmarshal(res.Body)
	if err != nil {
		return nil, nil, liberrors.ErrClientSDPInvalid{Err: err}
	}

	var desc description.Session
	err = desc.Unmarshal(&ssd)
	if err != nil {
		return nil, nil, liberrors.ErrClientSDPInvalid{Err: err}
	}

	baseURL, err := findBaseURL(&ssd, res, u)
	if err != nil {
		return nil, nil, err
	}
	desc.BaseURL = baseURL

	c.lastDescribeURL = u

	return &desc, res, nil
}

// Describe sends a DESCRIBE request.
func (c *Client) Describe(u *base.URL) (*description.Session, *base.Response, error) {
	cres := make(chan clientRes)
	select {
	case c.chDescribe <- describeReq{url: u, res: cres}:
		res := <-cres
		return res.sd, res.res, res.err

	case <-c.done:
		return nil, nil, c.closeError
	}
}

func (c *Client) doAnnounce(u *base.URL, desc *description.Session) (*base.Response, error) {
	err := c.checkState(map[clientState]struct{}{
		clientStateInitial: {},
	})
	if err != nil {
		return nil, err
	}

	err = c.connOpen()
	if err != nil {
		return nil, err
	}

	prepareForAnnounce(desc)

	byts, err := desc.Marshal(false)
	if err != nil {
		return nil, err
	}

	res, err := c.do(&base.Request{
		Method: base.Announce,
		URL:    u,
		Header: base.Header{
			"Content-Type": base.HeaderValue{"application/sdp"},
		},
		Body: byts,
	}, false)
	if err != nil {
		return nil, err
	}

	if res.StatusCode != base.StatusOK {
		return nil, liberrors.ErrClientBadStatusCode{
			Code: res.StatusCode, Message: res.StatusMessage,
		}
	}

	c.baseURL = u.Clone()
	c.state = clientStatePreRecord

	return res, nil
}

// Announce sends an ANNOUNCE request.
func (c *Client) Announce(u *base.URL, desc *description.Session) (*base.Response, error) {
	cres := make(chan clientRes)
	select {
	case c.chAnnounce <- announceReq{url: u, desc: desc, res: cres}:
		res := <-cres
		return res.res, res.err

	case <-c.done:
		return nil, c.closeError
	}
}

func (c *Client) doSetup(
	baseURL *base.URL,
	medi *description.Media,
	rtpPort int,
	rtcpPort int,
) (*base.Response, error) {
	err := c.checkState(map[clientState]struct{}{
		clientStateInitial:   {},
		clientStatePrePlay:   {},
		clientStatePreRecord: {},
	})
	if err != nil {
		return nil, err
	}

	err = c.connOpen()
	if err != nil {
		return nil, err
	}

	if c.baseURL != nil && *baseURL != *c.baseURL {
		return nil, liberrors.ErrClientCannotSetupMediasDifferentURLs{}
	}

	th := headers.Transport{
		Mode: func() *headers.TransportMode {
			if c.state == clientStatePreRecord {
				v := headers.TransportModeRecord
				return &v
			}
			// when playing, omit mode, since it causes errors with some servers.
			return nil
		}(),
	}

	cm := &clientMedia{
		c:     c,
		media: medi,
	}
	cm.initialize()

	if c.effectiveTransport == nil {
		if c.connURL.Scheme == "rtsps" { // always use TCP if encrypted
			v := TransportTCP
			c.effectiveTransport = &v
		} else if c.Transport != nil { // take transport from config
			c.effectiveTransport = c.Transport
		}
	}

	var desiredTransport Transport
	if c.effectiveTransport != nil {
		desiredTransport = *c.effectiveTransport
	} else {
		desiredTransport = TransportUDP
	}

	switch desiredTransport {
	case TransportUDP:
		if (rtpPort == 0 && rtcpPort != 0) ||
			(rtpPort != 0 && rtcpPort == 0) {
			return nil, liberrors.ErrClientUDPPortsZero{}
		}

		if rtpPort != 0 && rtcpPort != (rtpPort+1) {
			return nil, liberrors.ErrClientUDPPortsNotConsecutive{}
		}

		err = cm.createUDPListeners(
			false,
			nil,
			net.JoinHostPort("", strconv.FormatInt(int64(rtpPort), 10)),
			net.JoinHostPort("", strconv.FormatInt(int64(rtcpPort), 10)),
		)
		if err != nil {
			return nil, err
		}

		v1 := headers.TransportDeliveryUnicast
		th.Delivery = &v1
		th.Protocol = headers.TransportProtocolUDP
		th.ClientPorts = &[2]int{cm.udpRTPListener.port(), cm.udpRTCPListener.port()}

	case TransportUDPMulticast:
		v1 := headers.TransportDeliveryMulticast
		th.Delivery = &v1
		th.Protocol = headers.TransportProtocolUDP

	case TransportTCP:
		v1 := headers.TransportDeliveryUnicast
		th.Delivery = &v1
		th.Protocol = headers.TransportProtocolTCP
		ch := c.findFreeChannelPair()
		th.InterleavedIDs = &[2]int{ch, ch + 1}
	}

	mediaURL, err := medi.URL(baseURL)
	if err != nil {
		cm.close()
		return nil, err
	}

	header := base.Header{
		"Transport": th.Marshal(),
	}

	if medi.IsBackChannel {
		header["Require"] = base.HeaderValue{"www.onvif.org/ver20/backchannel"}
	}

	res, err := c.do(&base.Request{
		Method: base.Setup,
		URL:    mediaURL,
		Header: header,
	}, false)
	if err != nil {
		cm.close()
		return nil, err
	}

	if res.StatusCode != base.StatusOK {
		cm.close()

		// switch transport automatically
		if res.StatusCode == base.StatusUnsupportedTransport &&
			c.effectiveTransport == nil {
			c.OnTransportSwitch(liberrors.ErrClientSwitchToTCP2{})
			v := TransportTCP
			c.effectiveTransport = &v
			return c.doSetup(baseURL, medi, 0, 0)
		}

		return nil, liberrors.ErrClientBadStatusCode{Code: res.StatusCode, Message: res.StatusMessage}
	}

	var thRes headers.Transport
	err = thRes.Unmarshal(res.Header["Transport"])
	if err != nil {
		cm.close()
		return nil, liberrors.ErrClientTransportHeaderInvalid{Err: err}
	}

	switch desiredTransport {
	case TransportUDP, TransportUDPMulticast:
		if thRes.Protocol == headers.TransportProtocolTCP {
			cm.close()

			// switch transport automatically
			if c.effectiveTransport == nil &&
				c.Transport == nil {
				c.baseURL = baseURL
				return c.trySwitchingProtocol2(medi, baseURL)
			}

			return nil, liberrors.ErrClientServerRequestedTCP{}
		}
	}

	switch desiredTransport {
	case TransportUDP:
		if thRes.Delivery != nil && *thRes.Delivery != headers.TransportDeliveryUnicast {
			cm.close()
			return nil, liberrors.ErrClientTransportHeaderInvalidDelivery{}
		}

		serverPortsValid := thRes.ServerPorts != nil && !isAnyPort(thRes.ServerPorts[0]) && !isAnyPort(thRes.ServerPorts[1])

		if (c.state == clientStatePreRecord || !c.AnyPortEnable) && !serverPortsValid {
			cm.close()
			return nil, liberrors.ErrClientServerPortsNotProvided{}
		}

		var readIP net.IP
		if thRes.Source != nil {
			readIP = *thRes.Source
		} else {
			readIP = c.nconn.RemoteAddr().(*net.TCPAddr).IP
		}

		if serverPortsValid {
			if !c.AnyPortEnable {
				cm.udpRTPListener.readPort = thRes.ServerPorts[0]
			}
			cm.udpRTPListener.writeAddr = &net.UDPAddr{
				IP:   c.nconn.RemoteAddr().(*net.TCPAddr).IP,
				Zone: c.nconn.RemoteAddr().(*net.TCPAddr).Zone,
				Port: thRes.ServerPorts[0],
			}
		}
		cm.udpRTPListener.readIP = readIP

		if serverPortsValid {
			if !c.AnyPortEnable {
				cm.udpRTCPListener.readPort = thRes.ServerPorts[1]
			}
			cm.udpRTCPListener.writeAddr = &net.UDPAddr{
				IP:   c.nconn.RemoteAddr().(*net.TCPAddr).IP,
				Zone: c.nconn.RemoteAddr().(*net.TCPAddr).Zone,
				Port: thRes.ServerPorts[1],
			}
		}
		cm.udpRTCPListener.readIP = readIP

	case TransportUDPMulticast:
		if thRes.Delivery == nil || *thRes.Delivery != headers.TransportDeliveryMulticast {
			return nil, liberrors.ErrClientTransportHeaderInvalidDelivery{}
		}

		if thRes.Ports == nil {
			return nil, liberrors.ErrClientTransportHeaderNoPorts{}
		}

		if thRes.Destination == nil {
			return nil, liberrors.ErrClientTransportHeaderNoDestination{}
		}

		var readIP net.IP
		if thRes.Source != nil {
			readIP = *thRes.Source
		} else {
			readIP = c.nconn.RemoteAddr().(*net.TCPAddr).IP
		}

		err = cm.createUDPListeners(
			true,
			readIP,
			net.JoinHostPort(thRes.Destination.String(), strconv.FormatInt(int64(thRes.Ports[0]), 10)),
			net.JoinHostPort(thRes.Destination.String(), strconv.FormatInt(int64(thRes.Ports[1]), 10)),
		)
		if err != nil {
			return nil, err
		}

		cm.udpRTPListener.readIP = readIP
		cm.udpRTPListener.readPort = thRes.Ports[0]
		cm.udpRTPListener.writeAddr = &net.UDPAddr{
			IP:   *thRes.Destination,
			Port: thRes.Ports[0],
		}

		cm.udpRTCPListener.readIP = readIP
		cm.udpRTCPListener.readPort = thRes.Ports[1]
		cm.udpRTCPListener.writeAddr = &net.UDPAddr{
			IP:   *thRes.Destination,
			Port: thRes.Ports[1],
		}

	case TransportTCP:
		if thRes.Protocol != headers.TransportProtocolTCP {
			return nil, liberrors.ErrClientServerRequestedUDP{}
		}

		if thRes.Delivery != nil && *thRes.Delivery != headers.TransportDeliveryUnicast {
			return nil, liberrors.ErrClientTransportHeaderInvalidDelivery{}
		}

		if thRes.InterleavedIDs == nil {
			return nil, liberrors.ErrClientTransportHeaderNoInterleavedIDs{}
		}

		if (thRes.InterleavedIDs[0] + 1) != thRes.InterleavedIDs[1] {
			return nil, liberrors.ErrClientTransportHeaderInvalidInterleavedIDs{}
		}

		if c.isChannelPairInUse(thRes.InterleavedIDs[0]) {
			return &base.Response{
				StatusCode: base.StatusBadRequest,
			}, liberrors.ErrClientTransportHeaderInterleavedIDsInUse{}
		}

		cm.tcpChannel = thRes.InterleavedIDs[0]
	}

	if c.setuppedMedias == nil {
		c.setuppedMedias = make(map[*description.Media]*clientMedia)
	}

	c.setuppedMedias[medi] = cm

	c.baseURL = baseURL
	c.effectiveTransport = &desiredTransport

	if medi.IsBackChannel {
		c.backChannelSetupped = true
	} else {
		c.stdChannelSetupped = true
	}

	if c.state == clientStateInitial {
		c.state = clientStatePrePlay
	}

	return res, nil
}

func (c *Client) isChannelPairInUse(channel int) bool {
	for _, cm := range c.setuppedMedias {
		if (cm.tcpChannel+1) == channel || cm.tcpChannel == channel || cm.tcpChannel == (channel+1) {
			return true
		}
	}
	return false
}

func (c *Client) findFreeChannelPair() int {
	for i := 0; ; i += 2 { // prefer even channels
		if !c.isChannelPairInUse(i) {
			return i
		}
	}
}

// Setup sends a SETUP request.
// rtpPort and rtcpPort are used only if transport is UDP.
// if rtpPort and rtcpPort are zero, they are chosen automatically.
func (c *Client) Setup(
	baseURL *base.URL,
	media *description.Media,
	rtpPort int,
	rtcpPort int,
) (*base.Response, error) {
	cres := make(chan clientRes)
	select {
	case c.chSetup <- setupReq{
		baseURL:  baseURL,
		media:    media,
		rtpPort:  rtpPort,
		rtcpPort: rtcpPort,
		res:      cres,
	}:
		res := <-cres
		return res.res, res.err

	case <-c.done:
		return nil, c.closeError
	}
}

// SetupAll setups all the given medias.
func (c *Client) SetupAll(baseURL *base.URL, medias []*description.Media) error {
	for _, m := range medias {
		_, err := c.Setup(baseURL, m, 0, 0)
		if err != nil {
			return err
		}
	}
	return nil
}

func (c *Client) doPlay(ra *headers.Range) (*base.Response, error) {
	err := c.checkState(map[clientState]struct{}{
		clientStatePrePlay: {},
	})
	if err != nil {
		return nil, err
	}

	c.state = clientStatePlay
	c.startTransportRoutines()
	c.createWriter()

	// Range is mandatory in Parrot Streaming Server
	if ra == nil {
		ra = &headers.Range{
			Value: &headers.RangeNPT{
				Start: 0,
			},
		}
	}

	header := base.Header{
		"Range": ra.Marshal(),
	}

	if c.backChannelSetupped {
		header["Require"] = base.HeaderValue{"www.onvif.org/ver20/backchannel"}
	}

	res, err := c.do(&base.Request{
		Method: base.Play,
		URL:    c.baseURL,
		Header: header,
	}, false)
	if err != nil {
		c.destroyWriter()
		c.stopTransportRoutines()
		c.state = clientStatePrePlay
		return nil, err
	}

	if res.StatusCode != base.StatusOK {
		c.destroyWriter()
		c.stopTransportRoutines()
		c.state = clientStatePrePlay
		return nil, liberrors.ErrClientBadStatusCode{
			Code: res.StatusCode, Message: res.StatusMessage,
		}
	}

	// open the firewall by sending empty packets to the counterpart.
	// do this before sending the request.
	// don't do this with multicast, otherwise the RTP packet is going to be broadcasted
	// to all listeners, including us, messing up the stream.
	if *c.effectiveTransport == TransportUDP {
		for _, cm := range c.setuppedMedias {
			byts, _ := (&rtp.Packet{Header: rtp.Header{Version: 2}}).Marshal()
			cm.udpRTPListener.write(byts) //nolint:errcheck

			byts, _ = (&rtcp.ReceiverReport{}).Marshal()
			cm.udpRTCPListener.write(byts) //nolint:errcheck
		}
	}

	c.startWriter()

	c.lastRange = ra

	return res, nil
}

// Play sends a PLAY request.
// This can be called only after Setup().
func (c *Client) Play(ra *headers.Range) (*base.Response, error) {
	cres := make(chan clientRes)
	select {
	case c.chPlay <- playReq{ra: ra, res: cres}:
		res := <-cres
		return res.res, res.err

	case <-c.done:
		return nil, c.closeError
	}
}

func (c *Client) doRecord() (*base.Response, error) {
	err := c.checkState(map[clientState]struct{}{
		clientStatePreRecord: {},
	})
	if err != nil {
		return nil, err
	}

	c.state = clientStateRecord
	c.startTransportRoutines()
	c.createWriter()

	res, err := c.do(&base.Request{
		Method: base.Record,
		URL:    c.baseURL,
	}, false)
	if err != nil {
		c.destroyWriter()
		c.stopTransportRoutines()
		c.state = clientStatePreRecord
		return nil, err
	}

	if res.StatusCode != base.StatusOK {
		c.destroyWriter()
		c.stopTransportRoutines()
		c.state = clientStatePreRecord
		return nil, liberrors.ErrClientBadStatusCode{
			Code: res.StatusCode, Message: res.StatusMessage,
		}
	}

	c.startWriter()

	return nil, nil
}

// Record sends a RECORD request.
// This can be called only after Announce() and Setup().
func (c *Client) Record() (*base.Response, error) {
	cres := make(chan clientRes)
	select {
	case c.chRecord <- recordReq{res: cres}:
		res := <-cres
		return res.res, res.err

	case <-c.done:
		return nil, c.closeError
	}
}

func (c *Client) doPause() (*base.Response, error) {
	err := c.checkState(map[clientState]struct{}{
		clientStatePlay:   {},
		clientStateRecord: {},
	})
	if err != nil {
		return nil, err
	}

	c.destroyWriter()

	res, err := c.do(&base.Request{
		Method: base.Pause,
		URL:    c.baseURL,
	}, false)
	if err != nil {
		c.createWriter()
		c.startWriter()
		return nil, err
	}

	if res.StatusCode != base.StatusOK {
		c.createWriter()
		c.startWriter()
		return nil, liberrors.ErrClientBadStatusCode{
			Code: res.StatusCode, Message: res.StatusMessage,
		}
	}

	c.stopTransportRoutines()

	switch c.state {
	case clientStatePlay:
		c.state = clientStatePrePlay
	case clientStateRecord:
		c.state = clientStatePreRecord
	}

	return res, nil
}

// Pause sends a PAUSE request.
// This can be called only after Play() or Record().
func (c *Client) Pause() (*base.Response, error) {
	cres := make(chan clientRes)
	select {
	case c.chPause <- pauseReq{res: cres}:
		res := <-cres
		return res.res, res.err

	case <-c.done:
		return nil, c.closeError
	}
}

// Seek asks the server to re-start the stream from a specific timestamp.
func (c *Client) Seek(ra *headers.Range) (*base.Response, error) {
	_, err := c.Pause()
	if err != nil {
		return nil, err
	}

	return c.Play(ra)
}

// OnPacketRTPAny sets a callback that is called when a RTP packet is read from any setupped media.
func (c *Client) OnPacketRTPAny(cb OnPacketRTPAnyFunc) {
	for _, cm := range c.setuppedMedias {
		cmedia := cm.media
		for _, forma := range cm.media.Formats {
			c.OnPacketRTP(cm.media, forma, func(pkt *rtp.Packet) {
				cb(cmedia, forma, pkt)
			})
		}
	}
}

// OnPacketRTCPAny sets a callback that is called when a RTCP packet is read from any setupped media.
func (c *Client) OnPacketRTCPAny(cb OnPacketRTCPAnyFunc) {
	for _, cm := range c.setuppedMedias {
		cmedia := cm.media
		c.OnPacketRTCP(cm.media, func(pkt rtcp.Packet) {
			cb(cmedia, pkt)
		})
	}
}

// OnPacketRTP sets a callback that is called when a RTP packet is read.
func (c *Client) OnPacketRTP(medi *description.Media, forma format.Format, cb OnPacketRTPFunc) {
	cm := c.setuppedMedias[medi]
	ct := cm.formats[forma.PayloadType()]
	ct.onPacketRTP = cb
}

// OnPacketRTCP sets a callback that is called when a RTCP packet is read.
func (c *Client) OnPacketRTCP(medi *description.Media, cb OnPacketRTCPFunc) {
	cm := c.setuppedMedias[medi]
	cm.onPacketRTCP = cb
}

// WritePacketRTP writes a RTP packet to the server.
func (c *Client) WritePacketRTP(medi *description.Media, pkt *rtp.Packet) error {
	return c.WritePacketRTPWithNTP(medi, pkt, c.timeNow())
}

// WritePacketRTPWithNTP writes a RTP packet to the server.
// ntp is the absolute time of the packet, and is sent with periodic RTCP sender reports.
func (c *Client) WritePacketRTPWithNTP(medi *description.Media, pkt *rtp.Packet, ntp time.Time) error {
	byts := make([]byte, c.MaxPacketSize)
	n, err := pkt.MarshalTo(byts)
	if err != nil {
		return err
	}
	byts = byts[:n]

	select {
	case <-c.done:
		return c.closeError
	default:
	}

	c.writerMutex.RLock()
	defer c.writerMutex.RUnlock()

	if c.writer == nil {
		return nil
	}

	cm := c.setuppedMedias[medi]
	cf := cm.formats[pkt.PayloadType]

	cf.rtcpSender.ProcessPacket(pkt, ntp, cf.format.PTSEqualsDTS(pkt))

	ok := c.writer.push(func() error {
		return cf.writePacketRTPInQueue(byts)
	})
	if !ok {
		return liberrors.ErrClientWriteQueueFull{}
	}

	return nil
}

// WritePacketRTCP writes a RTCP packet to the server.
func (c *Client) WritePacketRTCP(medi *description.Media, pkt rtcp.Packet) error {
	byts, err := pkt.Marshal()
	if err != nil {
		return err
	}

	select {
	case <-c.done:
		return c.closeError
	default:
	}

	c.writerMutex.RLock()
	defer c.writerMutex.RUnlock()

	if c.writer == nil {
		return nil
	}

	cm := c.setuppedMedias[medi]

	ok := c.writer.push(func() error {
		return cm.writePacketRTCPInQueue(byts)
	})
	if !ok {
		return liberrors.ErrClientWriteQueueFull{}
	}

	return nil
}

// PacketPTS returns the PTS of an incoming RTP packet.
// It is computed by decoding the packet timestamp and sychronizing it with other tracks.
//
// Deprecated: replaced by PacketPTS2.
func (c *Client) PacketPTS(medi *description.Media, pkt *rtp.Packet) (time.Duration, bool) {
	cm := c.setuppedMedias[medi]
	ct := cm.formats[pkt.PayloadType]

	v, ok := c.timeDecoder.Decode(ct.format, pkt)
	if !ok {
		return 0, false
	}

	return multiplyAndDivide(time.Duration(v), time.Second, time.Duration(ct.format.ClockRate())), true
}

// PacketPTS2 returns the PTS of an incoming RTP packet.
// It is computed by decoding the packet timestamp and sychronizing it with other tracks.
func (c *Client) PacketPTS2(medi *description.Media, pkt *rtp.Packet) (int64, bool) {
	cm := c.setuppedMedias[medi]
	ct := cm.formats[pkt.PayloadType]
	return c.timeDecoder.Decode(ct.format, pkt)
}

// PacketNTP returns the NTP timestamp of an incoming RTP packet.
// The NTP timestamp is computed from RTCP sender reports.
func (c *Client) PacketNTP(medi *description.Media, pkt *rtp.Packet) (time.Time, bool) {
	cm := c.setuppedMedias[medi]
	ct := cm.formats[pkt.PayloadType]
	return ct.rtcpReceiver.PacketNTP(pkt.Timestamp)
}

// Stats returns client statistics.
func (c *Client) Stats() *ClientStats {
	return &ClientStats{
		Conn: StatsConn{
			BytesReceived: atomic.LoadUint64(c.bytesReceived),
			BytesSent:     atomic.LoadUint64(c.bytesSent),
		},
		Session: StatsSession{
			BytesReceived: func() uint64 {
				v := uint64(0)
				for _, sm := range c.setuppedMedias {
					v += atomic.LoadUint64(sm.bytesReceived)
				}
				return v
			}(),
			BytesSent: func() uint64 {
				v := uint64(0)
				for _, sm := range c.setuppedMedias {
					v += atomic.LoadUint64(sm.bytesSent)
				}
				return v
			}(),
			RTPPacketsReceived: func() uint64 {
				v := uint64(0)
				for _, sm := range c.setuppedMedias {
					for _, f := range sm.formats {
						v += atomic.LoadUint64(f.rtpPacketsReceived)
					}
				}
				return v
			}(),
			RTPPacketsSent: func() uint64 {
				v := uint64(0)
				for _, sm := range c.setuppedMedias {
					for _, f := range sm.formats {
						v += atomic.LoadUint64(f.rtpPacketsSent)
					}
				}
				return v
			}(),
			RTPPacketsLost: func() uint64 {
				v := uint64(0)
				for _, sm := range c.setuppedMedias {
					for _, f := range sm.formats {
						v += atomic.LoadUint64(f.rtpPacketsLost)
					}
				}
				return v
			}(),
			RTPPacketsInError: func() uint64 {
				v := uint64(0)
				for _, sm := range c.setuppedMedias {
					v += atomic.LoadUint64(sm.rtpPacketsInError)
				}
				return v
			}(),
			RTPPacketsJitter: func() float64 {
				v := float64(0)
				n := float64(0)
				for _, sm := range c.setuppedMedias {
					for _, fo := range sm.formats {
						if fo.rtcpReceiver != nil {
							stats := fo.rtcpReceiver.Stats()
							if stats != nil {
								v += stats.Jitter
								n++
							}
						}
					}
				}
				if n != 0 {
					return v / n
				}
				return 0
			}(),
			RTCPPacketsReceived: func() uint64 {
				v := uint64(0)
				for _, sm := range c.setuppedMedias {
					v += atomic.LoadUint64(sm.rtcpPacketsReceived)
				}
				return v
			}(),
			RTCPPacketsSent: func() uint64 {
				v := uint64(0)
				for _, sm := range c.setuppedMedias {
					v += atomic.LoadUint64(sm.rtcpPacketsSent)
				}
				return v
			}(),
			RTCPPacketsInError: func() uint64 {
				v := uint64(0)
				for _, sm := range c.setuppedMedias {
					v += atomic.LoadUint64(sm.rtcpPacketsInError)
				}
				return v
			}(),
			Medias: func() map[*description.Media]StatsSessionMedia { //nolint:dupl
				ret := make(map[*description.Media]StatsSessionMedia, len(c.setuppedMedias))

				for med, sm := range c.setuppedMedias {
					ret[med] = StatsSessionMedia{
						BytesReceived:       atomic.LoadUint64(sm.bytesReceived),
						BytesSent:           atomic.LoadUint64(sm.bytesSent),
						RTPPacketsInError:   atomic.LoadUint64(sm.rtpPacketsInError),
						RTCPPacketsReceived: atomic.LoadUint64(sm.rtcpPacketsReceived),
						RTCPPacketsSent:     atomic.LoadUint64(sm.rtcpPacketsSent),
						RTCPPacketsInError:  atomic.LoadUint64(sm.rtcpPacketsInError),
						Formats: func() map[format.Format]StatsSessionFormat {
							ret := make(map[format.Format]StatsSessionFormat, len(sm.formats))

							for _, fo := range sm.formats {
								recvStats := func() *rtcpreceiver.Stats {
									if fo.rtcpReceiver != nil {
										return fo.rtcpReceiver.Stats()
									}
									return nil
								}()
								sentStats := func() *rtcpsender.Stats {
									if fo.rtcpSender != nil {
										return fo.rtcpSender.Stats()
									}
									return nil
								}()

								ret[fo.format] = StatsSessionFormat{ //nolint:dupl
									RTPPacketsReceived: atomic.LoadUint64(fo.rtpPacketsReceived),
									RTPPacketsSent:     atomic.LoadUint64(fo.rtpPacketsSent),
									RTPPacketsLost:     atomic.LoadUint64(fo.rtpPacketsLost),
									LocalSSRC: func() uint32 {
										if fo.rtcpReceiver != nil {
											return *fo.rtcpReceiver.LocalSSRC
										}
										if sentStats != nil {
											return sentStats.LocalSSRC
										}
										return 0
									}(),
									RemoteSSRC: func() uint32 {
										if recvStats != nil {
											return recvStats.RemoteSSRC
										}
										return 0
									}(),
									RTPPacketsLastSequenceNumber: func() uint16 {
										if recvStats != nil {
											return recvStats.LastSequenceNumber
										}
										if sentStats != nil {
											return sentStats.LastSequenceNumber
										}
										return 0
									}(),
									RTPPacketsLastRTP: func() uint32 {
										if recvStats != nil {
											return recvStats.LastRTP
										}
										if sentStats != nil {
											return sentStats.LastRTP
										}
										return 0
									}(),
									RTPPacketsLastNTP: func() time.Time {
										if recvStats != nil {
											return recvStats.LastNTP
										}
										if sentStats != nil {
											return sentStats.LastNTP
										}
										return time.Time{}
									}(),
									RTPPacketsJitter: func() float64 {
										if recvStats != nil {
											return recvStats.Jitter
										}
										return 0
									}(),
								}
							}

							return ret
						}(),
					}
				}

				return ret
			}(),
		},
	}
}
