// The MIT License (MIT)
//
// # Copyright (c) 2021 Winlin
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
package janus

import (
	"bytes"
	"context"
	"encoding/json"
	"flag"
	"fmt"
	"io"
	"io/ioutil"
	"net"
	"net/http"
	"net/url"
	"os"
	"path"
	"strconv"
	"strings"
	"sync"
	"time"

	"github.com/ossrs/go-oryx-lib/errors"
	"github.com/ossrs/go-oryx-lib/logger"
	vnet_proxy "github.com/ossrs/srs-bench/vnet"
	"github.com/pion/interceptor"
	"github.com/pion/logging"
	"github.com/pion/rtcp"
	"github.com/pion/transport/v2/vnet"
	"github.com/pion/webrtc/v3"
	"github.com/pion/webrtc/v3/pkg/media/h264reader"
)

var srsHttps *bool
var srsLog *bool

var srsTimeout *int
var srsPlayPLI *int
var srsPlayOKPackets *int
var srsPublishOKPackets *int
var srsPublishVideoFps *int
var srsDTLSDropPackets *int

var srsSchema string
var srsServer *string
var srsStream *string
var srsPublishAudio *string
var srsPublishVideo *string
var srsVnetClientIP *string

func prepareTest() error {
	var err error

	srsHttps = flag.Bool("srs-https", false, "Whther connect to HTTPS-API")
	srsServer = flag.String("srs-server", "127.0.0.1", "The RTC server to connect to")
	srsStream = flag.String("srs-stream", "/rtc/regression", "The RTC stream to play")
	srsLog = flag.Bool("srs-log", false, "Whether enable the detail log")
	srsTimeout = flag.Int("srs-timeout", 5000, "For each case, the timeout in ms")
	srsPlayPLI = flag.Int("srs-play-pli", 5000, "The PLI interval in seconds for player.")
	srsPlayOKPackets = flag.Int("srs-play-ok-packets", 10, "If recv N RTP packets, it's ok, or fail")
	srsPublishOKPackets = flag.Int("srs-publish-ok-packets", 3, "If send N RTP, recv N RTCP packets, it's ok, or fail")
	srsPublishAudio = flag.String("srs-publish-audio", "avatar.ogg", "The audio file for publisher.")
	srsPublishVideo = flag.String("srs-publish-video", "avatar.h264", "The video file for publisher.")
	srsPublishVideoFps = flag.Int("srs-publish-video-fps", 25, "The video fps for publisher.")
	srsVnetClientIP = flag.String("srs-vnet-client-ip", "192.168.168.168", "The client ip in pion/vnet.")
	srsDTLSDropPackets = flag.Int("srs-dtls-drop-packets", 5, "If dropped N packets, it's ok, or fail")

	// Should parse it first.
	flag.Parse()

	// The stream should starts with /, for example, /rtc/regression
	if !strings.HasPrefix(*srsStream, "/") {
		*srsStream = "/" + *srsStream
	}

	// Generate srs protocol from whether use HTTPS.
	srsSchema = "http"
	if *srsHttps {
		srsSchema = "https"
	}

	// Check file.
	tryOpenFile := func(filename string) (string, error) {
		if filename == "" {
			return filename, nil
		}

		f, err := os.Open(filename)
		if err != nil {
			nfilename := path.Join("../", filename)
			f2, err := os.Open(nfilename)
			if err != nil {
				return filename, errors.Wrapf(err, "No video file at %v or %v", filename, nfilename)
			}
			defer f2.Close()

			return nfilename, nil
		}
		defer f.Close()

		return filename, nil
	}

	if *srsPublishVideo, err = tryOpenFile(*srsPublishVideo); err != nil {
		return err
	}

	if *srsPublishAudio, err = tryOpenFile(*srsPublishAudio); err != nil {
		return err
	}

	return nil
}

func apiRtcRequest(ctx context.Context, apiPath, r, offer string) (string, error) {
	u, err := url.Parse(r)
	if err != nil {
		return "", errors.Wrapf(err, "Parse url %v", r)
	}

	// Build api url.
	host := u.Host
	if !strings.Contains(host, ":") {
		host += ":1985"
	}

	api := fmt.Sprintf("http://%v", host)
	if !strings.HasPrefix(apiPath, "/") {
		api += "/"
	}
	api += apiPath

	if !strings.HasSuffix(apiPath, "/") {
		api += "/"
	}
	if u.RawQuery != "" {
		api += "?" + u.RawQuery
	}

	// Build JSON body.
	reqBody := struct {
		Api       string `json:"api"`
		ClientIP  string `json:"clientip"`
		SDP       string `json:"sdp"`
		StreamURL string `json:"streamurl"`
	}{
		api, "", offer, r,
	}

	b, err := json.Marshal(reqBody)
	if err != nil {
		return "", errors.Wrapf(err, "Marshal body %v", reqBody)
	}
	logger.If(ctx, "Request url api=%v with %v", api, string(b))
	logger.Tf(ctx, "Request url api=%v with %v bytes", api, len(b))

	req, err := http.NewRequest("POST", api, strings.NewReader(string(b)))
	if err != nil {
		return "", errors.Wrapf(err, "HTTP request %v", string(b))
	}

	res, err := http.DefaultClient.Do(req.WithContext(ctx))
	if err != nil {
		return "", errors.Wrapf(err, "Do HTTP request %v", string(b))
	}

	b2, err := ioutil.ReadAll(res.Body)
	if err != nil {
		return "", errors.Wrapf(err, "Read response for %v", string(b))
	}
	logger.If(ctx, "Response from %v is %v", api, string(b2))
	logger.Tf(ctx, "Response from %v is %v bytes", api, len(b2))

	resBody := struct {
		Code    int    `json:"code"`
		Session string `json:"sessionid"`
		SDP     string `json:"sdp"`
	}{}
	if err := json.Unmarshal(b2, &resBody); err != nil {
		return "", errors.Wrapf(err, "Marshal %v", string(b2))
	}

	if resBody.Code != 0 {
		return "", errors.Errorf("Server fail code=%v %v", resBody.Code, string(b2))
	}
	logger.If(ctx, "Parse response to code=%v, session=%v, sdp=%v",
		resBody.Code, resBody.Session, escapeSDP(resBody.SDP))
	logger.Tf(ctx, "Parse response to code=%v, session=%v, sdp=%v bytes",
		resBody.Code, resBody.Session, len(resBody.SDP))

	return resBody.SDP, nil
}

func escapeSDP(sdp string) string {
	return strings.ReplaceAll(strings.ReplaceAll(sdp, "\r", "\\r"), "\n", "\\n")
}

func packageAsSTAPA(frames ...*h264reader.NAL) *h264reader.NAL {
	first := frames[0]

	buf := bytes.Buffer{}
	buf.WriteByte(
		first.RefIdc<<5&0x60 | byte(24), // STAP-A
	)

	for _, frame := range frames {
		buf.WriteByte(byte(len(frame.Data) >> 8))
		buf.WriteByte(byte(len(frame.Data)))
		buf.Write(frame.Data)
	}

	return &h264reader.NAL{
		PictureOrderCount: first.PictureOrderCount,
		ForbiddenZeroBit:  false,
		RefIdc:            first.RefIdc,
		UnitType:          h264reader.NalUnitType(24), // STAP-A
		Data:              buf.Bytes(),
	}
}

type wallClock struct {
	start    time.Time
	duration time.Duration
}

func newWallClock() *wallClock {
	return &wallClock{start: time.Now()}
}

func (v *wallClock) Tick(d time.Duration) time.Duration {
	v.duration += d

	wc := time.Now().Sub(v.start)
	re := v.duration - wc
	if re > 30*time.Millisecond {
		return re
	}
	return 0
}

// Set to active, as DTLS client, to start ClientHello.
func testUtilSetupActive(s *webrtc.SessionDescription) error {
	if strings.Contains(s.SDP, "setup:passive") {
		return errors.New("set to active")
	}

	s.SDP = strings.ReplaceAll(s.SDP, "setup:actpass", "setup:active")
	return nil
}

// Set to passive, as DTLS client, to start ClientHello.
func testUtilSetupPassive(s *webrtc.SessionDescription) error {
	if strings.Contains(s.SDP, "setup:active") {
		return errors.New("set to passive")
	}

	s.SDP = strings.ReplaceAll(s.SDP, "setup:actpass", "setup:passive")
	return nil
}

// Parse address from SDP.
// candidate:0 1 udp 2130706431 192.168.3.8 8000 typ host generation 0
func parseAddressOfCandidate(answerSDP string) (*net.UDPAddr, error) {
	answer := webrtc.SessionDescription{Type: webrtc.SDPTypeAnswer, SDP: answerSDP}
	answerObject, err := answer.Unmarshal()
	if err != nil {
		return nil, errors.Wrapf(err, "unmarshal answer %v", answerSDP)
	}

	if len(answerObject.MediaDescriptions) == 0 {
		return nil, errors.New("no media")
	}

	candidate, ok := answerObject.MediaDescriptions[0].Attribute("candidate")
	if !ok {
		return nil, errors.New("no candidate")
	}

	// candidate:0 1 udp 2130706431 192.168.3.8 8000 typ host generation 0
	attrs := strings.Split(candidate, " ")
	if len(attrs) <= 6 {
		return nil, errors.Errorf("no address in %v", candidate)
	}

	// Parse ip and port from answer.
	ip := attrs[4]
	port, err := strconv.Atoi(attrs[5])
	if err != nil {
		return nil, errors.Wrapf(err, "invalid port %v", candidate)
	}

	address := fmt.Sprintf("%v:%v", ip, port)
	addr, err := net.ResolveUDPAddr("udp4", address)
	if err != nil {
		return nil, errors.Wrapf(err, "parse %v", address)
	}

	return addr, nil
}

// Filter the test error, ignore context.Canceled
func filterTestError(errs ...error) error {
	var filteredErrors []error

	for _, err := range errs {
		if err == nil || errors.Cause(err) == context.Canceled {
			continue
		}

		// If url error, server maybe error, do not print the detail log.
		if r0 := errors.Cause(err); r0 != nil {
			if r1, ok := r0.(*url.Error); ok {
				err = r1
			}
		}

		filteredErrors = append(filteredErrors, err)
	}

	if len(filteredErrors) == 0 {
		return nil
	}
	if len(filteredErrors) == 1 {
		return filteredErrors[0]
	}

	var descs []string
	for i, err := range filteredErrors[1:] {
		descs = append(descs, fmt.Sprintf("err #%d, %+v", i, err))
	}
	return errors.Wrapf(filteredErrors[0], "with %v", strings.Join(descs, ","))
}

// For STUN packet, 0x00 is binding request, 0x01 is binding success response.
// @see srs_is_stun of https://github.com/ossrs/srs
func srsIsStun(b []byte) bool {
	return len(b) > 0 && (b[0] == 0 || b[0] == 1)
}

// change_cipher_spec(20), alert(21), handshake(22), application_data(23)
// @see https://tools.ietf.org/html/rfc2246#section-6.2.1
// @see srs_is_dtls of https://github.com/ossrs/srs
func srsIsDTLS(b []byte) bool {
	return len(b) >= 13 && (b[0] > 19 && b[0] < 64)
}

// For RTP or RTCP, the V=2 which is in the high 2bits, 0xC0 (1100 0000)
// @see srs_is_rtp_or_rtcp of https://github.com/ossrs/srs
func srsIsRTPOrRTCP(b []byte) bool {
	return len(b) >= 12 && (b[0]&0xC0) == 0x80
}

// For RTCP, PT is [128, 223] (or without marker [0, 95]).
// Literally, RTCP starts from 64 not 0, so PT is [192, 223] (or without marker [64, 95]).
// @note For RTP, the PT is [96, 127], or [224, 255] with marker.
// @see srs_is_rtcp of https://github.com/ossrs/srs
func srsIsRTCP(b []byte) bool {
	return (len(b) >= 12) && (b[0]&0x80) != 0 && (b[1] >= 192 && b[1] <= 223)
}

type chunkType int

const (
	chunkTypeICE chunkType = iota + 1
	chunkTypeDTLS
	chunkTypeRTP
	chunkTypeRTCP
)

func (v chunkType) String() string {
	switch v {
	case chunkTypeICE:
		return "ICE"
	case chunkTypeDTLS:
		return "DTLS"
	case chunkTypeRTP:
		return "RTP"
	case chunkTypeRTCP:
		return "RTCP"
	default:
		return "Unknown"
	}
}

type dtlsContentType int

const (
	dtlsContentTypeHandshake        dtlsContentType = 22
	dtlsContentTypeChangeCipherSpec dtlsContentType = 20
	dtlsContentTypeAlert            dtlsContentType = 21
)

func (v dtlsContentType) String() string {
	switch v {
	case dtlsContentTypeHandshake:
		return "Handshake"
	case dtlsContentTypeChangeCipherSpec:
		return "ChangeCipherSpec"
	default:
		return "Unknown"
	}
}

type dtlsHandshakeType int

const (
	dtlsHandshakeTypeClientHello        dtlsHandshakeType = 1
	dtlsHandshakeTypeServerHello        dtlsHandshakeType = 2
	dtlsHandshakeTypeCertificate        dtlsHandshakeType = 11
	dtlsHandshakeTypeServerKeyExchange  dtlsHandshakeType = 12
	dtlsHandshakeTypeCertificateRequest dtlsHandshakeType = 13
	dtlsHandshakeTypeServerDone         dtlsHandshakeType = 14
	dtlsHandshakeTypeCertificateVerify  dtlsHandshakeType = 15
	dtlsHandshakeTypeClientKeyExchange  dtlsHandshakeType = 16
	dtlsHandshakeTypeFinished           dtlsHandshakeType = 20
)

func (v dtlsHandshakeType) String() string {
	switch v {
	case dtlsHandshakeTypeClientHello:
		return "ClientHello"
	case dtlsHandshakeTypeServerHello:
		return "ServerHello"
	case dtlsHandshakeTypeCertificate:
		return "Certificate"
	case dtlsHandshakeTypeServerKeyExchange:
		return "ServerKeyExchange"
	case dtlsHandshakeTypeCertificateRequest:
		return "CertificateRequest"
	case dtlsHandshakeTypeServerDone:
		return "ServerDone"
	case dtlsHandshakeTypeCertificateVerify:
		return "CertificateVerify"
	case dtlsHandshakeTypeClientKeyExchange:
		return "ClientKeyExchange"
	case dtlsHandshakeTypeFinished:
		return "Finished"
	default:
		return "Unknown"
	}
}

type chunkMessageType struct {
	chunk     chunkType
	content   dtlsContentType
	handshake dtlsHandshakeType
}

func (v *chunkMessageType) String() string {
	if v.chunk == chunkTypeDTLS {
		if v.content == dtlsContentTypeHandshake {
			return fmt.Sprintf("%v-%v-%v", v.chunk, v.content, v.handshake)
		} else {
			return fmt.Sprintf("%v-%v", v.chunk, v.content)
		}
	}
	return fmt.Sprintf("%v", v.chunk)
}

func newChunkMessageType(c vnet.Chunk) (*chunkMessageType, bool) {
	b := c.UserData()

	if len(b) == 0 {
		return nil, false
	}

	v := &chunkMessageType{}

	if srsIsRTPOrRTCP(b) {
		if srsIsRTCP(b) {
			v.chunk = chunkTypeRTCP
		} else {
			v.chunk = chunkTypeRTP
		}
		return v, true
	}

	if srsIsStun(b) {
		v.chunk = chunkTypeICE
		return v, true
	}

	if !srsIsDTLS(b) {
		return nil, false
	}

	v.chunk, v.content = chunkTypeDTLS, dtlsContentType(b[0])
	if v.content != dtlsContentTypeHandshake {
		return v, true
	}

	if len(b) < 14 {
		return v, false
	}
	v.handshake = dtlsHandshakeType(b[13])
	return v, true
}

func (v *chunkMessageType) IsHandshake() bool {
	return v.chunk == chunkTypeDTLS && v.content == dtlsContentTypeHandshake
}

func (v *chunkMessageType) IsClientHello() bool {
	return v.chunk == chunkTypeDTLS && v.content == dtlsContentTypeHandshake && v.handshake == dtlsHandshakeTypeClientHello
}

func (v *chunkMessageType) IsServerHello() bool {
	return v.chunk == chunkTypeDTLS && v.content == dtlsContentTypeHandshake && v.handshake == dtlsHandshakeTypeServerHello
}

func (v *chunkMessageType) IsCertificate() bool {
	return v.chunk == chunkTypeDTLS && v.content == dtlsContentTypeHandshake && v.handshake == dtlsHandshakeTypeCertificate
}

func (v *chunkMessageType) IsChangeCipherSpec() bool {
	return v.chunk == chunkTypeDTLS && v.content == dtlsContentTypeChangeCipherSpec
}

type dtlsRecord struct {
	ContentType    dtlsContentType
	Version        uint16
	Epoch          uint16
	SequenceNumber uint64
	Length         uint16
	Data           []byte
}

func newDTLSRecord(b []byte) (*dtlsRecord, error) {
	v := &dtlsRecord{}
	return v, v.Unmarshal(b)
}

func (v *dtlsRecord) String() string {
	return fmt.Sprintf("epoch=%v, sequence=%v", v.Epoch, v.SequenceNumber)
}

func (v *dtlsRecord) Equals(p *dtlsRecord) bool {
	return v.Epoch == p.Epoch && v.SequenceNumber == p.SequenceNumber
}

func (v *dtlsRecord) Unmarshal(b []byte) error {
	if len(b) < 13 {
		return errors.Errorf("requires 13B only %v", len(b))
	}

	v.ContentType = dtlsContentType(b[0])
	v.Version = uint16(b[1])<<8 | uint16(b[2])
	v.Epoch = uint16(b[3])<<8 | uint16(b[4])
	v.SequenceNumber = uint64(b[5])<<40 | uint64(b[6])<<32 | uint64(b[7])<<24 | uint64(b[8])<<16 | uint64(b[9])<<8 | uint64(b[10])
	v.Length = uint16(b[11])<<8 | uint16(b[12])
	v.Data = b[13:]
	return nil
}

type testWebRTCAPIOptionFunc func(api *testWebRTCAPI)

type testWebRTCAPI struct {
	// The options to setup the api.
	options []testWebRTCAPIOptionFunc
	// The api and settings.
	api           *webrtc.API
	mediaEngine   *webrtc.MediaEngine
	registry      *interceptor.Registry
	settingEngine *webrtc.SettingEngine
	// The vnet router, can be shared by different apis, but we do not share it.
	router *vnet.Router
	// The network for api.
	network *vnet.Net
	// The vnet UDP proxy bind to the router.
	proxy *vnet_proxy.UDPProxy
}

func newTestWebRTCAPI(options ...testWebRTCAPIOptionFunc) (*testWebRTCAPI, error) {
	v := &testWebRTCAPI{}

	v.mediaEngine = &webrtc.MediaEngine{}
	if err := v.mediaEngine.RegisterDefaultCodecs(); err != nil {
		return nil, err
	}

	v.registry = &interceptor.Registry{}
	if err := webrtc.RegisterDefaultInterceptors(v.mediaEngine, v.registry); err != nil {
		return nil, err
	}

	for _, setup := range options {
		setup(v)
	}

	v.settingEngine = &webrtc.SettingEngine{}

	return v, nil
}

func (v *testWebRTCAPI) Close() error {
	if v.proxy != nil {
		_ = v.proxy.Close()
	}

	if v.router != nil {
		_ = v.router.Stop()
	}

	return nil
}

func (v *testWebRTCAPI) Setup(vnetClientIP string, options ...testWebRTCAPIOptionFunc) error {
	// Setting engine for https://github.com/pion/transport/tree/master/vnet
	setupVnet := func(vnetClientIP string) (err error) {
		// We create a private router for a api, however, it's possible to share the
		// same router between apis.
		if v.router, err = vnet.NewRouter(&vnet.RouterConfig{
			CIDR:          "0.0.0.0/0", // Accept all ip, no sub router.
			LoggerFactory: logging.NewDefaultLoggerFactory(),
		}); err != nil {
			return errors.Wrapf(err, "create router for api")
		}

		// Each api should bind to a network, however, it's possible to share it
		// for different apis.
		v.network, err = vnet.NewNet(&vnet.NetConfig{
			StaticIP: vnetClientIP,
		})
		if err != nil {
			return errors.Wrapf(err, "create network for api")
		}

		if err = v.router.AddNet(v.network); err != nil {
			return errors.Wrapf(err, "create network for api")
		}

		v.settingEngine.SetVNet(v.network)

		// Create a proxy bind to the router.
		if v.proxy, err = vnet_proxy.NewProxy(v.router); err != nil {
			return errors.Wrapf(err, "create proxy for router")
		}

		return v.router.Start()
	}
	if err := setupVnet(vnetClientIP); err != nil {
		return err
	}

	for _, setup := range options {
		setup(v)
	}

	for _, setup := range v.options {
		setup(v)
	}

	v.api = webrtc.NewAPI(
		webrtc.WithMediaEngine(v.mediaEngine),
		webrtc.WithInterceptorRegistry(v.registry),
		webrtc.WithSettingEngine(*v.settingEngine),
	)

	return nil
}

func (v *testWebRTCAPI) NewPeerConnection(configuration webrtc.Configuration) (*webrtc.PeerConnection, error) {
	return v.api.NewPeerConnection(configuration)
}

type testPlayerOptionFunc func(p *testPlayer) error

type testPlayer struct {
	pc        *webrtc.PeerConnection
	receivers []*webrtc.RTPReceiver
	// We should dispose it.
	api *testWebRTCAPI
	// Optional suffix for stream url.
	streamSuffix string
}

func createApiForPlayer(play *testPlayer) error {
	api, err := newTestWebRTCAPI()
	if err != nil {
		return err
	}

	play.api = api
	return nil
}

func newTestPlayer(options ...testPlayerOptionFunc) (*testPlayer, error) {
	v := &testPlayer{}

	for _, opt := range options {
		if err := opt(v); err != nil {
			return nil, err
		}
	}

	return v, nil
}

func (v *testPlayer) Setup(vnetClientIP string, options ...testWebRTCAPIOptionFunc) error {
	return v.api.Setup(vnetClientIP, options...)
}

func (v *testPlayer) Close() error {
	if v.pc != nil {
		_ = v.pc.Close()
	}

	for _, receiver := range v.receivers {
		_ = receiver.Stop()
	}

	if v.api != nil {
		_ = v.api.Close()
	}

	return nil
}

func (v *testPlayer) Run(ctx context.Context, cancel context.CancelFunc) error {
	r := fmt.Sprintf("%v://%v%v", srsSchema, *srsServer, *srsStream)
	if v.streamSuffix != "" {
		r = fmt.Sprintf("%v-%v", r, v.streamSuffix)
	}
	pli := time.Duration(*srsPlayPLI) * time.Millisecond
	logger.Tf(ctx, "Run play url=%v", r)

	pc, err := v.api.NewPeerConnection(webrtc.Configuration{})
	if err != nil {
		return errors.Wrapf(err, "Create PC")
	}
	v.pc = pc

	if _, err := pc.AddTransceiverFromKind(webrtc.RTPCodecTypeAudio, webrtc.RTPTransceiverInit{
		Direction: webrtc.RTPTransceiverDirectionRecvonly,
	}); err != nil {
		return errors.Wrapf(err, "add track")
	}
	if _, err := pc.AddTransceiverFromKind(webrtc.RTPCodecTypeVideo, webrtc.RTPTransceiverInit{
		Direction: webrtc.RTPTransceiverDirectionRecvonly,
	}); err != nil {
		return errors.Wrapf(err, "add track")
	}

	offer, err := pc.CreateOffer(nil)
	if err != nil {
		return errors.Wrapf(err, "Create Offer")
	}

	if err := pc.SetLocalDescription(offer); err != nil {
		return errors.Wrapf(err, "Set offer %v", offer)
	}

	answer, err := apiRtcRequest(ctx, "/rtc/v1/play", r, offer.SDP)
	if err != nil {
		return errors.Wrapf(err, "Api request offer=%v", offer.SDP)
	}

	// Run a proxy for real server and vnet.
	if address, err := parseAddressOfCandidate(answer); err != nil {
		return errors.Wrapf(err, "parse address of %v", answer)
	} else if err := v.api.proxy.Proxy(v.api.network, address); err != nil {
		return errors.Wrapf(err, "proxy %v to %v", v.api.network, address)
	}

	if err := pc.SetRemoteDescription(webrtc.SessionDescription{
		Type: webrtc.SDPTypeAnswer, SDP: answer,
	}); err != nil {
		return errors.Wrapf(err, "Set answer %v", answer)
	}

	handleTrack := func(ctx context.Context, track *webrtc.TrackRemote, receiver *webrtc.RTPReceiver) error {
		// Send a PLI on an interval so that the publisher is pushing a keyframe
		go func() {
			if track.Kind() == webrtc.RTPCodecTypeAudio {
				return
			}

			for {
				select {
				case <-ctx.Done():
					return
				case <-time.After(pli):
					_ = pc.WriteRTCP([]rtcp.Packet{&rtcp.PictureLossIndication{
						MediaSSRC: uint32(track.SSRC()),
					}})
				}
			}
		}()

		v.receivers = append(v.receivers, receiver)

		for ctx.Err() == nil {
			_, _, err := track.ReadRTP()
			if err != nil {
				return errors.Wrapf(err, "Read RTP")
			}
		}

		return nil
	}

	pc.OnTrack(func(track *webrtc.TrackRemote, receiver *webrtc.RTPReceiver) {
		err = handleTrack(ctx, track, receiver)
		if err != nil {
			codec := track.Codec()
			err = errors.Wrapf(err, "Handle  track %v, pt=%v", codec.MimeType, codec.PayloadType)
			cancel()
		}
	})

	pc.OnICEConnectionStateChange(func(state webrtc.ICEConnectionState) {
		if state == webrtc.ICEConnectionStateFailed || state == webrtc.ICEConnectionStateClosed {
			err = errors.Errorf("Close for ICE state %v", state)
			cancel()
		}
	})

	<-ctx.Done()
	return err
}

type testPublisherOptionFunc func(p *testPublisher) error

type testPublisher struct {
	onOffer        func(s *webrtc.SessionDescription) error
	onAnswer       func(s *webrtc.SessionDescription) error
	iceReadyCancel context.CancelFunc
	// internal objects
	aIngester *audioIngester
	vIngester *videoIngester
	pc        *webrtc.PeerConnection
	// We should dispose it.
	api *testWebRTCAPI
	// Optional suffix for stream url.
	streamSuffix string
	// To cancel the publisher, pass by Run.
	cancel context.CancelFunc
}

func createApiForPublisher(pub *testPublisher) error {
	api, err := newTestWebRTCAPI()
	if err != nil {
		return err
	}

	pub.api = api
	return nil
}

func newTestPublisher(options ...testPublisherOptionFunc) (*testPublisher, error) {
	sourceVideo, sourceAudio := *srsPublishVideo, *srsPublishAudio

	v := &testPublisher{}

	for _, opt := range options {
		if err := opt(v); err != nil {
			return nil, err
		}
	}

	// Create ingesters.
	if sourceAudio != "" {
		v.aIngester = newAudioIngester(sourceAudio)
	}
	if sourceVideo != "" {
		v.vIngester = newVideoIngester(sourceVideo)
	}

	// Setup the interceptors for packets.
	api := v.api
	api.options = append(api.options, func(api *testWebRTCAPI) {
		// Filter for RTCP packets.
		rtcpInterceptor := &rtcpInterceptor{}
		rtcpInterceptor.rtcpReader = func(buf []byte, attributes interceptor.Attributes) (int, interceptor.Attributes, error) {
			return rtcpInterceptor.nextRTCPReader.Read(buf, attributes)
		}
		rtcpInterceptor.rtcpWriter = func(pkts []rtcp.Packet, attributes interceptor.Attributes) (int, error) {
			return rtcpInterceptor.nextRTCPWriter.Write(pkts, attributes)
		}
		api.registry.Add(&rtcpInteceptorFactory{rtcpInterceptor})

		// Filter for ingesters.
		if sourceAudio != "" {
			api.registry.Add(&rtpInteceptorFactory{v.aIngester.audioLevelInterceptor})
		}
		if sourceVideo != "" {
			api.registry.Add(&rtpInteceptorFactory{v.vIngester.markerInterceptor})
		}
	})

	return v, nil
}

func (v *testPublisher) Setup(vnetClientIP string, options ...testWebRTCAPIOptionFunc) error {
	return v.api.Setup(vnetClientIP, options...)
}

func (v *testPublisher) Close() error {
	if v.vIngester != nil {
		_ = v.vIngester.Close()
	}

	if v.aIngester != nil {
		_ = v.aIngester.Close()
	}

	if v.pc != nil {
		_ = v.pc.Close()
	}

	if v.api != nil {
		_ = v.api.Close()
	}

	return nil
}

func (v *testPublisher) SetStreamSuffix(suffix string) *testPublisher {
	v.streamSuffix = suffix
	return v
}

func (v *testPublisher) Run(ctx context.Context, cancel context.CancelFunc) error {
	// Save the cancel.
	v.cancel = cancel

	r := fmt.Sprintf("%v://%v%v", srsSchema, *srsServer, *srsStream)
	if v.streamSuffix != "" {
		r = fmt.Sprintf("%v-%v", r, v.streamSuffix)
	}
	sourceVideo, sourceAudio, fps := *srsPublishVideo, *srsPublishAudio, *srsPublishVideoFps

	logger.Tf(ctx, "Run publish url=%v, audio=%v, video=%v, fps=%v",
		r, sourceAudio, sourceVideo, fps)

	pc, err := v.api.NewPeerConnection(webrtc.Configuration{})
	if err != nil {
		return errors.Wrapf(err, "Create PC")
	}
	v.pc = pc

	if v.vIngester != nil {
		if err := v.vIngester.AddTrack(pc, fps); err != nil {
			return errors.Wrapf(err, "Add track")
		}
	}

	if v.aIngester != nil {
		if err := v.aIngester.AddTrack(pc); err != nil {
			return errors.Wrapf(err, "Add track")
		}
	}

	offer, err := pc.CreateOffer(nil)
	if err != nil {
		return errors.Wrapf(err, "Create Offer")
	}

	if err := pc.SetLocalDescription(offer); err != nil {
		return errors.Wrapf(err, "Set offer %v", offer)
	}

	if v.onOffer != nil {
		if err := v.onOffer(&offer); err != nil {
			return errors.Wrapf(err, "sdp %v %v", offer.Type, offer.SDP)
		}
	}

	answerSDP, err := apiRtcRequest(ctx, "/rtc/v1/publish", r, offer.SDP)
	if err != nil {
		return errors.Wrapf(err, "Api request offer=%v", offer.SDP)
	}

	// Run a proxy for real server and vnet.
	if address, err := parseAddressOfCandidate(answerSDP); err != nil {
		return errors.Wrapf(err, "parse address of %v", answerSDP)
	} else if err := v.api.proxy.Proxy(v.api.network, address); err != nil {
		return errors.Wrapf(err, "proxy %v to %v", v.api.network, address)
	}

	answer := &webrtc.SessionDescription{
		Type: webrtc.SDPTypeAnswer, SDP: answerSDP,
	}
	if v.onAnswer != nil {
		if err := v.onAnswer(answer); err != nil {
			return errors.Wrapf(err, "on answerSDP")
		}
	}

	if err := pc.SetRemoteDescription(*answer); err != nil {
		return errors.Wrapf(err, "Set answerSDP %v", answerSDP)
	}

	logger.Tf(ctx, "State signaling=%v, ice=%v, conn=%v", pc.SignalingState(), pc.ICEConnectionState(), pc.ConnectionState())

	// ICE state management.
	pc.OnICEGatheringStateChange(func(state webrtc.ICEGathererState) {
		logger.Tf(ctx, "ICE gather state %v", state)
	})
	pc.OnICECandidate(func(candidate *webrtc.ICECandidate) {
		logger.Tf(ctx, "ICE candidate %v %v:%v", candidate.Protocol, candidate.Address, candidate.Port)

	})
	pc.OnICEConnectionStateChange(func(state webrtc.ICEConnectionState) {
		logger.Tf(ctx, "ICE state %v", state)
	})

	pc.OnSignalingStateChange(func(state webrtc.SignalingState) {
		logger.Tf(ctx, "Signaling state %v", state)
	})

	if v.aIngester != nil {
		v.aIngester.sAudioSender.Transport().OnStateChange(func(state webrtc.DTLSTransportState) {
			logger.Tf(ctx, "DTLS state %v", state)
		})
	}

	pcDone, pcDoneCancel := context.WithCancel(context.Background())
	pc.OnConnectionStateChange(func(state webrtc.PeerConnectionState) {
		logger.Tf(ctx, "PC state %v", state)

		if state == webrtc.PeerConnectionStateConnected {
			pcDoneCancel()
			if v.iceReadyCancel != nil {
				v.iceReadyCancel()
			}
		}

		if state == webrtc.PeerConnectionStateFailed || state == webrtc.PeerConnectionStateClosed {
			err = errors.Errorf("Close for PC state %v", state)
			cancel()
		}
	})

	// Wait for event from context or tracks.
	var wg sync.WaitGroup
	var finalErr error

	wg.Add(1)
	go func() {
		defer wg.Done()
		defer logger.Tf(ctx, "ingest notify done")

		<-ctx.Done()

		if v.aIngester != nil && v.aIngester.sAudioSender != nil {
			// We MUST wait for the ingester ready(or closed), because it might crash if sender is disposed.
			<-v.aIngester.ready.Done()

			_ = v.aIngester.Close()
		}

		if v.vIngester != nil && v.vIngester.sVideoSender != nil {
			// We MUST wait for the ingester ready(or closed), because it might crash if sender is disposed.
			<-v.vIngester.ready.Done()

			_ = v.vIngester.Close()
		}
	}()

	wg.Add(1)
	go func() {
		defer wg.Done()
		defer cancel()

		if v.aIngester == nil {
			return
		}
		defer v.aIngester.readyCancel()

		select {
		case <-ctx.Done():
			return
		case <-pcDone.Done():
		}

		wg.Add(1)
		go func() {
			defer wg.Done()
			defer logger.Tf(ctx, "aingester sender read done")

			buf := make([]byte, 1500)
			for ctx.Err() == nil {
				if _, _, err := v.aIngester.sAudioSender.Read(buf); err != nil {
					return
				}
			}
		}()

		for {
			if err := v.aIngester.Ingest(ctx); err != nil {
				if err == io.EOF {
					logger.Tf(ctx, "aingester retry for %v", err)
					continue
				}
				if err != context.Canceled {
					finalErr = errors.Wrapf(err, "audio")
				}

				logger.Tf(ctx, "aingester err=%v, final=%v", err, finalErr)
				return
			}
		}
	}()

	wg.Add(1)
	go func() {
		defer wg.Done()
		defer cancel()

		if v.vIngester == nil {
			return
		}
		defer v.vIngester.readyCancel()

		select {
		case <-ctx.Done():
			return
		case <-pcDone.Done():
			logger.Tf(ctx, "PC(ICE+DTLS+SRTP) done, start ingest video %v", sourceVideo)
		}

		wg.Add(1)
		go func() {
			defer wg.Done()
			defer logger.Tf(ctx, "vingester sender read done")

			buf := make([]byte, 1500)
			for ctx.Err() == nil {
				// The Read() might block in r.rtcpInterceptor.Read(b, a),
				// so that the Stop() can not stop it.
				if _, _, err := v.vIngester.sVideoSender.Read(buf); err != nil {
					return
				}
			}
		}()

		for {
			if err := v.vIngester.Ingest(ctx); err != nil {
				if err == io.EOF {
					logger.Tf(ctx, "vingester retry for %v", err)
					continue
				}
				if err != context.Canceled {
					finalErr = errors.Wrapf(err, "video")
				}

				logger.Tf(ctx, "vingester err=%v, final=%v", err, finalErr)
				return
			}
		}
	}()

	wg.Wait()

	logger.Tf(ctx, "ingester done ctx=%v, final=%v", ctx.Err(), finalErr)
	if finalErr != nil {
		return finalErr
	}
	return ctx.Err()
}
