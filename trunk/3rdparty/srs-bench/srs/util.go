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
package srs

import (
	"bytes"
	"context"
	"crypto/ecdsa"
	"crypto/elliptic"
	crand "crypto/rand"
	"crypto/rsa"
	"crypto/x509"
	"crypto/x509/pkix"
	"flag"
	"fmt"
	"github.com/ossrs/go-oryx-lib/amf0"
	"github.com/ossrs/go-oryx-lib/avc"
	"github.com/ossrs/go-oryx-lib/flv"
	"github.com/ossrs/go-oryx-lib/rtmp"
	"github.com/pion/ice/v2"
	"github.com/pion/rtp"
	"github.com/pion/rtp/codecs"
	"io"
	"math/big"
	"math/rand"
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
var srsHttpServer *string
var srsStream *string
var srsLiveStream *string
var srsPublishAudio *string
var srsPublishVideo *string
var srsPublishAvatar *string
var srsPublishBBB *string
var srsVnetClientIP *string

func prepareTest() (err error) {
	srsHttps = flag.Bool("srs-https", false, "Whther connect to HTTPS-API")
	srsServer = flag.String("srs-server", "127.0.0.1", "The RTMP/RTC server to connect to")
	srsHttpServer = flag.String("srs-http-server", "127.0.0.1:8080", "The HTTP server to connect to")
	srsStream = flag.String("srs-stream", "/rtc/regression", "The RTC app/stream to play")
	srsLiveStream = flag.String("srs-live-stream", "/live/livestream", "The LIVE app/stream to play")
	srsLog = flag.Bool("srs-log", false, "Whether enable the detail log")
	srsTimeout = flag.Int("srs-timeout", 5000, "For each case, the timeout in ms")
	srsPlayPLI = flag.Int("srs-play-pli", 5000, "The PLI interval in seconds for player.")
	srsPlayOKPackets = flag.Int("srs-play-ok-packets", 10, "If recv N RTP packets, it's ok, or fail")
	srsPublishOKPackets = flag.Int("srs-publish-ok-packets", 3, "If send N RTP, recv N RTCP packets, it's ok, or fail")
	srsPublishAudio = flag.String("srs-publish-audio", "avatar.ogg", "The audio file for publisher.")
	srsPublishVideo = flag.String("srs-publish-video", "avatar.h264", "The video file for publisher.")
	srsPublishAvatar = flag.String("srs-publish-avatar", "avatar.flv", "The avatar file for publisher.")
	srsPublishBBB = flag.String("srs-publish-bbb", "bbb.flv", "The bbb file for publisher.")
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

	if *srsPublishAvatar, err = tryOpenFile(*srsPublishAvatar); err != nil {
		return err
	}

	if *srsPublishBBB, err = tryOpenFile(*srsPublishBBB); err != nil {
		return err
	}

	if *srsPublishAudio, err = tryOpenFile(*srsPublishAudio); err != nil {
		return err
	}

	return nil
}

// Request SRS RTC API, the apiPath like "/rtc/v1/play", the r is WebRTC url like
// "webrtc://localhost/live/livestream", and the offer is SDP in string.
//
// Return the response of answer SDP in string.
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

	resBody := struct {
		Code    int    `json:"code"`
		Session string `json:"sessionid"`
		SDP     string `json:"sdp"`
	}{}

	if err := apiRequest(ctx, api, reqBody, &resBody); err != nil {
		return "", errors.Wrapf(err, "request api=%v", api)
	}

	if resBody.Code != 0 {
		return "", errors.Errorf("Server fail code=%v", resBody.Code)
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

// Do nothing for SDP.
func testUtilPassBy(s *webrtc.SessionDescription) error {
	return nil
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

func newChunkAll(c vnet.Chunk) ([]byte, *chunkMessageType, bool, *dtlsRecord, error) {
	b := c.UserData()
	chunk, parsed := newChunkMessageType(c)
	record, err := newDTLSRecord(c.UserData())
	return b, chunk, parsed, record, err
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

// The func to setup testWebRTCAPI
type testWebRTCAPIOptionFunc func(api *testWebRTCAPI)

type testWebRTCAPI struct {
	// The options to setup the api.
	options []testWebRTCAPIOptionFunc
	// The api and settings.
	api           *webrtc.API
	registry      *interceptor.Registry
	mediaEngine   *webrtc.MediaEngine
	settingEngine *webrtc.SettingEngine
	// The vnet router, can be shared by different apis, but we do not share it.
	router *vnet.Router
	// The network for api.
	network *vnet.Net
	// The vnet UDP proxy bind to the router.
	proxy *vnet_proxy.UDPProxy
}

// The func to initialize testWebRTCAPI
type testWebRTCAPIInitFunc func(api *testWebRTCAPI) error

// Implements interface testWebRTCAPIInitFunc to init testWebRTCAPI
func registerDefaultCodecs(api *testWebRTCAPI) error {
	v := api

	if err := v.mediaEngine.RegisterDefaultCodecs(); err != nil {
		return err
	}

	if err := webrtc.RegisterDefaultInterceptors(v.mediaEngine, v.registry); err != nil {
		return err
	}

	return nil
}

// Implements interface testWebRTCAPIInitFunc to init testWebRTCAPI
func registerMiniCodecs(api *testWebRTCAPI) error {
	v := api

	if err := v.mediaEngine.RegisterCodec(webrtc.RTPCodecParameters{
		RTPCodecCapability: webrtc.RTPCodecCapability{webrtc.MimeTypeOpus, 48000, 2, "minptime=10;useinbandfec=1", nil},
		PayloadType:        111,
	}, webrtc.RTPCodecTypeAudio); err != nil {
		return err
	}

	videoRTCPFeedback := []webrtc.RTCPFeedback{{"goog-remb", ""}, {"ccm", "fir"}, {"nack", ""}, {"nack", "pli"}}
	if err := v.mediaEngine.RegisterCodec(webrtc.RTPCodecParameters{
		RTPCodecCapability: webrtc.RTPCodecCapability{webrtc.MimeTypeH264, 90000, 0, "level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=42e01f", videoRTCPFeedback},
		PayloadType:        108,
	}, webrtc.RTPCodecTypeVideo); err != nil {
		return err
	}

	// Interceptors for NACK??? @see webrtc.ConfigureNack(v.mediaEngine, v.registry)
	return nil
}

// Implements interface testWebRTCAPIInitFunc to init testWebRTCAPI
func registerMiniCodecsWithoutNack(api *testWebRTCAPI) error {
	v := api

	if err := v.mediaEngine.RegisterCodec(webrtc.RTPCodecParameters{
		RTPCodecCapability: webrtc.RTPCodecCapability{webrtc.MimeTypeOpus, 48000, 2, "minptime=10;useinbandfec=1", nil},
		PayloadType:        111,
	}, webrtc.RTPCodecTypeAudio); err != nil {
		return err
	}

	videoRTCPFeedback := []webrtc.RTCPFeedback{{"goog-remb", ""}, {"ccm", "fir"}}
	if err := v.mediaEngine.RegisterCodec(webrtc.RTPCodecParameters{
		RTPCodecCapability: webrtc.RTPCodecCapability{webrtc.MimeTypeH264, 90000, 0, "level-asymmetry-allowed=1;packetization-mode=0;profile-level-id=42e01f", videoRTCPFeedback},
		PayloadType:        108,
	}, webrtc.RTPCodecTypeVideo); err != nil {
		return err
	}

	// Interceptors for NACK??? @see webrtc.ConfigureNack(v.mediaEngine, v.registry)
	return nil
}

func newTestWebRTCAPI(inits ...testWebRTCAPIInitFunc) (*testWebRTCAPI, error) {
	v := &testWebRTCAPI{}

	v.registry = &interceptor.Registry{}
	v.mediaEngine = &webrtc.MediaEngine{}
	v.settingEngine = &webrtc.SettingEngine{}

	// Disable the mDNS to suppress the error:
	//		Failed to enable mDNS, continuing in mDNS disabled mode
	v.settingEngine.SetICEMulticastDNSMode(ice.MulticastDNSModeDisabled)

	// Apply an initialize filter, such as registering default codecs when creating a publisher/player.
	for _, setup := range inits {
		if setup == nil {
			continue
		}

		if err := setup(v); err != nil {
			return nil, err
		}
	}

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

	// Apply options from params, for example, tester to register vnet filter.
	for _, setup := range options {
		setup(v)
	}

	// Apply options in api, for example, publisher register audio-level interceptor.
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
	onOffer        func(s *webrtc.SessionDescription) error
	onAnswer       func(s *webrtc.SessionDescription) error
	iceReadyCancel context.CancelFunc
	pc             *webrtc.PeerConnection
	receivers      []*webrtc.RTPReceiver
	// We should dispose it.
	api *testWebRTCAPI
	// Optional suffix for stream url.
	streamSuffix string
	// Optional app/stream to play, use srsStream by default.
	defaultStream string
}

// Create test player, the init is used to initialize api which maybe nil,
// and the options is used to setup the player itself.
func newTestPlayer(init testWebRTCAPIInitFunc, options ...testPlayerOptionFunc) (*testPlayer, error) {
	v := &testPlayer{}

	api, err := newTestWebRTCAPI(init)
	if err != nil {
		return nil, err
	}
	v.api = api

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
	if v.defaultStream != "" {
		r = fmt.Sprintf("%v://%v%v", srsSchema, *srsServer, v.defaultStream)
	}
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

	if v.onOffer != nil {
		if err := v.onOffer(&offer); err != nil {
			return errors.Wrapf(err, "sdp %v %v", offer.Type, offer.SDP)
		}
	}

	answerSDP, err := apiRtcRequest(ctx, "/rtc/v1/play", r, offer.SDP)
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
		logger.Tf(ctx, "ICE state %v", state)
	})

	pc.OnConnectionStateChange(func(state webrtc.PeerConnectionState) {
		logger.Tf(ctx, "PC state %v", state)

		if state == webrtc.PeerConnectionStateConnected {
			if v.iceReadyCancel != nil {
				v.iceReadyCancel()
			}
		}

		if state == webrtc.PeerConnectionStateFailed || state == webrtc.PeerConnectionStateClosed {
			err = errors.Errorf("Close for PC state %v", state)
			cancel()
		}
	})

	<-ctx.Done()
	return err
}

type testPublisherOptionFunc func(p *testPublisher) error

func createLargeRsaCertificate(p *testPublisher) error {
	privateKey, err := rsa.GenerateKey(crand.Reader, 4096)
	if err != nil {
		return errors.Wrapf(err, "Generate key")
	}

	template := x509.Certificate{
		SerialNumber: big.NewInt(1),
		Subject: pkix.Name{
			Country:    []string{"CN"},
			CommonName: "Pion WebRTC",
		},
		NotBefore: time.Now().Add(-24 * time.Hour),
		NotAfter:  time.Now().Add(24 * time.Hour),
		KeyUsage:  x509.KeyUsageDigitalSignature,
	}

	certificate, err := webrtc.NewCertificate(privateKey, template)
	if err != nil {
		return errors.Wrapf(err, "New certificate")
	}

	p.pcc.Certificates = []webrtc.Certificate{*certificate}

	return nil
}

func createLargeEcdsaCertificate(p *testPublisher) error {
	privateKey, err := ecdsa.GenerateKey(elliptic.P256(), crand.Reader)
	if err != nil {
		return errors.Wrapf(err, "Generate key")
	}

	template := x509.Certificate{
		SerialNumber: big.NewInt(1),
		Subject: pkix.Name{
			Country:            []string{"CN"},
			Organization:       []string{"Example Majestic Mountain Meadows"},
			OrganizationalUnit: []string{"Example Emerald Enchantment Forest"},
			Locality:           []string{"Sunset Serenity Shores"},
			Province:           []string{"Crystal Cove Lagoon"},
			StreetAddress:      []string{"1234 Market St Whispering Willow Valley"},
			PostalCode:         []string{"100010"},
			CommonName:         "MediaSphere Solutions CreativeWave Media Pion WebRTC",
		},
		NotBefore:             time.Now().Add(-24 * time.Hour),
		NotAfter:              time.Now().Add(24 * time.Hour),
		KeyUsage:              x509.KeyUsageDigitalSignature,
		OCSPServer:            []string{"http://CreativeMediaPro.example.com"},
		IssuingCertificateURL: []string{"http://DigitalVisionary.example.com/ca1.crt"},
		DNSNames:              []string{"PixelStoryteller.example.com", "www.SkylineExplorer.example.com"},
		EmailAddresses:        []string{"HarmonyVisuals@example.com"},
		IPAddresses:           []net.IP{net.ParseIP("192.0.2.1"), net.ParseIP("2001:db8::1")},
		URIs:                  []*url.URL{&url.URL{Scheme: "https", Host: "SunsetDreamer.example.com"}},
		PermittedDNSDomains:   []string{".SerenityFilms.example.com", "EnchantedLens.example.net"},
		CRLDistributionPoints: []string{"http://crl.example.com/ca1.crl"},
	}

	certificate, err := webrtc.NewCertificate(privateKey, template)
	if err != nil {
		return errors.Wrapf(err, "New certificate")
	}

	p.pcc.Certificates = []webrtc.Certificate{*certificate}

	return nil
}

type testPublisher struct {
	// When got offer.
	onOffer func(s *webrtc.SessionDescription) error
	// When got answer.
	onAnswer func(s *webrtc.SessionDescription) error
	// Whether ignore any PC state error, for error scenario test.
	ignorePCStateError bool
	// When PC state change.
	onPeerConnectionStateChange func(state webrtc.PeerConnectionState)
	// Whether ignore any DTLS error, for error scenario test.
	ignoreDTLSStateError bool
	// When DTLS state change.
	onDTLSStateChange func(state webrtc.DTLSTransportState)
	// When ICE is ready.
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
	// The config for peer connection.
	pcc *webrtc.Configuration
}

// Create test publisher, the init is used to initialize api which maybe nil,
// and the options is used to setup the publisher itself.
func newTestPublisher(init testWebRTCAPIInitFunc, options ...testPublisherOptionFunc) (*testPublisher, error) {
	sourceVideo, sourceAudio := *srsPublishVideo, *srsPublishAudio

	v := &testPublisher{
		pcc: &webrtc.Configuration{},
	}

	api, err := newTestWebRTCAPI(init)
	if err != nil {
		return nil, err
	}
	v.api = api

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

	pc, err := v.api.NewPeerConnection(*v.pcc)
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

	var finalErr error
	if v.aIngester != nil {
		v.aIngester.sAudioSender.Transport().OnStateChange(func(state webrtc.DTLSTransportState) {
			if v.onDTLSStateChange != nil {
				v.onDTLSStateChange(state)
			}
			logger.Tf(ctx, "DTLS state %v", state)

			if state == webrtc.DTLSTransportStateFailed {
				if !v.ignoreDTLSStateError {
					finalErr = errors.Errorf("DTLS failed")
				}
				cancel()
			}
		})
	}

	pcDone, pcDoneCancel := context.WithCancel(context.Background())
	pc.OnConnectionStateChange(func(state webrtc.PeerConnectionState) {
		if v.onPeerConnectionStateChange != nil {
			v.onPeerConnectionStateChange(state)
		}
		logger.Tf(ctx, "PC state %v", state)

		if state == webrtc.PeerConnectionStateConnected {
			pcDoneCancel()
			if v.iceReadyCancel != nil {
				v.iceReadyCancel()
			}
		}

		if state == webrtc.PeerConnectionStateFailed || state == webrtc.PeerConnectionStateClosed {
			if finalErr == nil && !v.ignorePCStateError {
				finalErr = errors.Errorf("Close for PC state %v", state)
			}
			cancel()
		}
	})

	// Wait for event from context or tracks.
	var wg sync.WaitGroup

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

type RTMPClient struct {
	rtmpUrl string

	rtmpTcUrl     string
	rtmpStream    string
	rtmpUrlObject *url.URL

	streamID int

	conn  *net.TCPConn
	proto *rtmp.Protocol
}

func (v *RTMPClient) Close() error {
	if v.conn != nil {
		v.conn.Close()
	}
	return nil
}

func (v *RTMPClient) connect(rtmpUrl string) error {
	v.rtmpUrl = rtmpUrl

	if index := strings.LastIndex(rtmpUrl, "/"); index <= 0 {
		return fmt.Errorf("invalid url %v, index=%v", rtmpUrl, index)
	} else {
		v.rtmpTcUrl = rtmpUrl[0:index]
		v.rtmpStream = rtmpUrl[index+1:]
	}

	// Parse RTMP url.
	rtmpUrlObject, err := url.Parse(rtmpUrl)
	if err != nil {
		return err
	}
	v.rtmpUrlObject = rtmpUrlObject

	port := rtmpUrlObject.Port()
	if port == "" {
		port = "1935"
	}

	// Connect to TCP server.
	rtmpAddr, err := net.ResolveTCPAddr("tcp4", fmt.Sprintf("%v:%v", rtmpUrlObject.Hostname(), port))
	if err != nil {
		return err
	}

	c, err := net.DialTCP("tcp4", nil, rtmpAddr)
	if err != nil {
		return err
	}
	v.conn = c

	// RTMP Handshake with server.
	hs := rtmp.NewHandshake(rand.New(rand.NewSource(time.Now().UnixNano())))
	if err := hs.WriteC0S0(c); err != nil {
		return err
	}
	if err := hs.WriteC1S1(c); err != nil {
		return err
	}

	if _, err := hs.ReadC0S0(c); err != nil {
		return err
	}
	s1, err := hs.ReadC1S1(c)
	if err != nil {
		return err
	}
	if _, err := hs.ReadC2S2(c); err != nil {
		return err
	}

	if err := hs.WriteC2S2(c, s1); err != nil {
		return err
	}

	// Connect to RTMP tcUrl.
	p := rtmp.NewProtocol(v.conn)

	pkt := rtmp.NewConnectAppPacket()
	pkt.CommandObject.Set("tcUrl", amf0.NewString(v.rtmpTcUrl))
	if err = p.WritePacket(pkt, 0); err != nil {
		return err
	}

	res := rtmp.NewConnectAppResPacket(pkt.TransactionID)
	if _, err := p.ExpectPacket(&res); err != nil {
		return err
	}
	v.proto = p

	return nil
}

func (v *RTMPClient) Publish(ctx context.Context, rtmpUrl string) error {
	if err := v.connect(rtmpUrl); err != nil {
		return err
	}
	p := v.proto

	// Create RTMP stream.
	if true {
		pkt := rtmp.NewCreateStreamPacket()
		if err := p.WritePacket(pkt, 0); err != nil {
			return err
		}

		res := rtmp.NewCreateStreamResPacket(pkt.TransactionID)
		if _, err := p.ExpectPacket(&res); err != nil {
			return err
		}
		v.streamID = int(res.StreamID)
	}

	// Publish RTMP stream.
	if true {
		pkt := rtmp.NewPublishPacket()
		pkt.StreamName = *amf0.NewString(v.rtmpStream)
		if err := p.WritePacket(pkt, v.streamID); err != nil {
			return err
		}

		res := rtmp.NewCallPacket()
		if _, err := p.ExpectPacket(&res); err != nil {
			return err
		}
	}

	return nil
}

func (v *RTMPClient) Play(ctx context.Context, rtmpUrl string) error {
	if err := v.connect(rtmpUrl); err != nil {
		return err
	}
	p := v.proto

	// Create RTMP stream.
	if true {
		pkt := rtmp.NewCreateStreamPacket()
		if err := p.WritePacket(pkt, 0); err != nil {
			return err
		}

		res := rtmp.NewCreateStreamResPacket(pkt.TransactionID)
		if _, err := p.ExpectPacket(&res); err != nil {
			return err
		}
		v.streamID = int(res.StreamID)
	}

	// Play RTMP stream.
	if true {
		pkt := rtmp.NewPlayPacket()
		pkt.StreamName = *amf0.NewString(v.rtmpStream)
		if err := p.WritePacket(pkt, v.streamID); err != nil {
			return err
		}

		res := rtmp.NewCallPacket()
		if _, err := p.ExpectPacket(&res); err != nil {
			return err
		}
	}

	return nil
}

type RTMPPublisher struct {
	client *RTMPClient
	// Whether auto close transport when ingest done.
	closeTransportWhenIngestDone bool
	// Whether drop audio, set the hasAudio to false.
	hasAudio bool
	// Whether drop video, set the hasVideo to false.
	hasVideo bool

	onSendPacket func(m *rtmp.Message) error
}

func NewRTMPPublisher() *RTMPPublisher {
	v := &RTMPPublisher{
		client: &RTMPClient{},
	}

	// By default, set to on.
	v.closeTransportWhenIngestDone = true
	v.hasAudio, v.hasVideo = true, true

	return v
}

func (v *RTMPPublisher) Close() error {
	return v.client.Close()
}

func (v *RTMPPublisher) Publish(ctx context.Context, rtmpUrl string) error {
	logger.Tf(ctx, "Publish %v", rtmpUrl)
	return v.client.Publish(ctx, rtmpUrl)
}

func (v *RTMPPublisher) Ingest(ctx context.Context, flvInput string) error {
	// If ctx is cancelled, close the RTMP transport.
	var wg sync.WaitGroup
	defer wg.Wait()

	wg.Add(1)
	go func() {
		defer wg.Done()
		<-ctx.Done()
		if v.closeTransportWhenIngestDone {
			v.Close()
		}
	}()

	// Consume all packets.
	logger.Tf(ctx, "Start to ingest %v", flvInput)
	err := v.ingest(ctx, flvInput)
	if err == io.EOF {
		return nil
	}
	if ctx.Err() == context.Canceled {
		return nil
	}
	return err
}

func (v *RTMPPublisher) ingest(ctx context.Context, flvInput string) error {
	p := v.client

	fs, err := os.Open(flvInput)
	if err != nil {
		return err
	}
	defer fs.Close()
	logger.Tf(ctx, "Open input %v", flvInput)

	demuxer, err := flv.NewDemuxer(fs)
	if err != nil {
		return err
	}

	if _, _, _, err = demuxer.ReadHeader(); err != nil {
		return err
	}

	for {
		tagType, tagSize, timestamp, err := demuxer.ReadTagHeader()
		if err != nil {
			return err
		}

		tag, err := demuxer.ReadTag(tagSize)
		if err != nil {
			return err
		}

		if tagType != flv.TagTypeVideo && tagType != flv.TagTypeAudio {
			continue
		}
		if !v.hasAudio && tagType == flv.TagTypeAudio {
			continue
		}
		if !v.hasVideo && tagType == flv.TagTypeVideo {
			continue
		}

		m := rtmp.NewStreamMessage(p.streamID)
		m.MessageType = rtmp.MessageType(tagType)
		m.Timestamp = uint64(timestamp)
		m.Payload = tag
		if err = p.proto.WriteMessage(m); err != nil {
			return err
		}

		if v.onSendPacket != nil {
			if err = v.onSendPacket(m); err != nil {
				return err
			}
		}
	}

	return nil
}

type RTMPPlayer struct {
	// Transport.
	client *RTMPClient
	// FLV packager.
	videoPackager flv.VideoPackager

	onRecvPacket func(m *rtmp.Message, a *flv.AudioFrame, v *flv.VideoFrame) error
}

func NewRTMPPlayer() *RTMPPlayer {
	return &RTMPPlayer{
		client: &RTMPClient{},
	}
}

func (v *RTMPPlayer) Close() error {
	return v.client.Close()
}

func (v *RTMPPlayer) Play(ctx context.Context, rtmpUrl string) error {
	var err error
	if v.videoPackager, err = flv.NewVideoPackager(); err != nil {
		return err
	}

	return v.client.Play(ctx, rtmpUrl)
}

func (v *RTMPPlayer) Consume(ctx context.Context) error {
	// If ctx is cancelled, close the RTMP transport.
	var wg sync.WaitGroup
	defer wg.Wait()

	ctx, cancel := context.WithCancel(ctx)
	defer cancel()

	wg.Add(1)
	go func() {
		defer wg.Done()
		<-ctx.Done()
		v.Close()
	}()

	// Consume all packets.
	err := v.consume()
	if err == io.EOF {
		return nil
	}
	if ctx.Err() == context.Canceled {
		return nil
	}
	return err
}

func (v *RTMPPlayer) consume() error {
	for {
		res, err := v.client.proto.ExpectMessage(rtmp.MessageTypeVideo, rtmp.MessageTypeAudio)
		if err != nil {
			return err
		}

		if v.onRecvPacket != nil {
			var audioFrame *flv.AudioFrame
			var videoFrame *flv.VideoFrame
			if res.MessageType == rtmp.MessageTypeVideo {
				if videoFrame, err = v.videoPackager.Decode(res.Payload); err != nil {
					return err
				}
			}

			if err := v.onRecvPacket(res, audioFrame, videoFrame); err != nil {
				return err
			}
		}
	}
}

type FLVPlayer struct {
	flvUrl string
	client *http.Client
	resp   *http.Response
	f      flv.Demuxer

	onRecvHeader func(hasAudio, hasVideo bool) error
	onRecvTag    func(tp flv.TagType, size, ts uint32, tag []byte) error
}

func NewFLVPlayer() *FLVPlayer {
	return &FLVPlayer{
		client: &http.Client{}, resp: nil, f: nil, onRecvHeader: nil, onRecvTag: nil,
	}
}

func (v *FLVPlayer) Close() error {
	if v.f != nil {
		v.f.Close()
	}
	if v.resp != nil {
		v.resp.Body.Close()
	}
	return nil
}

func (v *FLVPlayer) Play(ctx context.Context, flvUrl string) error {
	v.flvUrl = flvUrl
	return nil
}

func (v *FLVPlayer) Consume(ctx context.Context) error {
	// If ctx is cancelled, close the RTMP transport.
	var wg sync.WaitGroup
	defer wg.Wait()

	ctx, cancel := context.WithCancel(ctx)
	defer cancel()

	wg.Add(1)
	go func() {
		defer wg.Done()
		<-ctx.Done()
		v.Close()
	}()

	// Start to play.
	if err := v.play(ctx, v.flvUrl); err != nil {
		return err
	}

	// Consume all packets.
	err := v.consume(ctx)
	if err == io.EOF {
		return nil
	}
	if ctx.Err() == context.Canceled {
		return nil
	}
	return err
}

func (v *FLVPlayer) play(ctx context.Context, flvUrl string) error {
	logger.Tf(ctx, "Run play flv url=%v", flvUrl)

	req, err := http.NewRequestWithContext(ctx, "GET", flvUrl, nil)
	if err != nil {
		return errors.Wrapf(err, "New request for flv %v failed, err=%v", flvUrl, err)
	}

	resp, err := v.client.Do(req)
	if err != nil {
		return errors.Wrapf(err, "Http get flv %v failed, err=%v", flvUrl, err)
	}
	logger.Tf(ctx, "Connected to %v", flvUrl)

	if v.resp != nil {
		v.resp.Body.Close()
	}
	v.resp = resp

	f, err := flv.NewDemuxer(resp.Body)
	if err != nil {
		return errors.Wrapf(err, "Create flv demuxer for %v failed, err=%v", flvUrl, err)
	}

	if v.f != nil {
		v.f.Close()
	}
	v.f = f

	return nil
}

func (v *FLVPlayer) consume(ctx context.Context) (err error) {
	var hasVideo, hasAudio bool
	if _, hasVideo, hasAudio, err = v.f.ReadHeader(); err != nil {
		return errors.Wrapf(err, "Flv demuxer read header failed, err=%v", err)
	}
	logger.Tf(ctx, "Got audio=%v, video=%v", hasAudio, hasVideo)

	if v.onRecvHeader != nil {
		if err := v.onRecvHeader(hasAudio, hasVideo); err != nil {
			return errors.Wrapf(err, "Callback FLV header audio=%v, video=%v", hasAudio, hasVideo)
		}
	}

	for {
		var tagType flv.TagType
		var tagSize, timestamp uint32
		if tagType, tagSize, timestamp, err = v.f.ReadTagHeader(); err != nil {
			return errors.Wrapf(err, "Flv demuxer read tag header failed, err=%v", err)
		}

		var tag []byte
		if tag, err = v.f.ReadTag(tagSize); err != nil {
			return errors.Wrapf(err, "Flv demuxer read tag failed, err=%v", err)
		}

		if v.onRecvTag != nil {
			if err := v.onRecvTag(tagType, tagSize, timestamp, tag); err != nil {
				return errors.Wrapf(err, "Callback tag type=%v, size=%v, ts=%v, tag=%vB", tagType, tagSize, timestamp, len(tag))
			}
		}
	}
}

func IsAvccrEquals(a, b *avc.AVCDecoderConfigurationRecord) bool {
	if a == nil || b == nil {
		return false
	}

	if a.AVCLevelIndication != b.AVCLevelIndication ||
		a.AVCProfileIndication != b.AVCProfileIndication ||
		a.LengthSizeMinusOne != b.LengthSizeMinusOne ||
		len(a.SequenceParameterSetNALUnits) != len(b.SequenceParameterSetNALUnits) ||
		len(a.PictureParameterSetNALUnits) != len(b.PictureParameterSetNALUnits) {
		return false
	}

	for i := 0; i < len(a.SequenceParameterSetNALUnits); i++ {
		if !IsNALUEquals(a.SequenceParameterSetNALUnits[i], b.SequenceParameterSetNALUnits[i]) {
			return false
		}
	}

	for i := 0; i < len(a.PictureParameterSetNALUnits); i++ {
		if !IsNALUEquals(a.PictureParameterSetNALUnits[i], b.PictureParameterSetNALUnits[i]) {
			return false
		}
	}

	return true
}

func IsNALUEquals(a, b *avc.NALU) bool {
	if a == nil || b == nil {
		return false
	}

	if a.NALUType != b.NALUType || a.NALRefIDC != b.NALRefIDC {
		return false
	}

	return bytes.Equal(a.Data, b.Data)
}

func DemuxRtpSpsPps(payload []byte) ([]byte, []*avc.NALU, error) {
	// Parse RTP packet.
	pkt := rtp.Packet{}
	if err := pkt.Unmarshal(payload); err != nil {
		return nil, nil, err
	}

	// Decode H264 packet.
	h264Packet := codecs.H264Packet{}
	annexb, err := h264Packet.Unmarshal(pkt.Payload)
	if err != nil {
		return annexb, nil, err
	}

	// Ignore if not STAP-A
	if !bytes.HasPrefix(annexb, []byte{0x00, 0x00, 0x00, 0x01}) {
		return annexb, nil, err
	}

	// Parse to NALUs
	rawNalus := bytes.Split(annexb, []byte{0x00, 0x00, 0x00, 0x01})

	nalus := []*avc.NALU{}
	for _, rawNalu := range rawNalus {
		if len(rawNalu) == 0 {
			continue
		}

		nalu := avc.NewNALU()
		if err := nalu.UnmarshalBinary(rawNalu); err != nil {
			return annexb, nil, err
		}

		nalus = append(nalus, nalu)
	}

	return annexb, nalus, nil
}
