// The MIT License (MIT)
//
// Copyright (c) 2021 srs-bench(ossrs)
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
	"context"
	"encoding/json"
	"flag"
	"fmt"
	"github.com/ossrs/go-oryx-lib/errors"
	"github.com/ossrs/go-oryx-lib/logger"
	vnet_proxy "github.com/ossrs/srs-bench/vnet"
	"github.com/pion/interceptor"
	"github.com/pion/logging"
	"github.com/pion/rtcp"
	"github.com/pion/transport/vnet"
	"github.com/pion/webrtc/v3"
	"io"
	"io/ioutil"
	"net/http"
	"os"
	"path"
	"strings"
	"sync"
	"testing"
	"time"
)

var srsSchema = "http"
var srsHttps = flag.Bool("srs-https", false, "Whther connect to HTTPS-API")
var srsServer = flag.String("srs-server", "127.0.0.1", "The RTC server to connect to")
var srsStream = flag.String("srs-stream", "/rtc/regression", "The RTC stream to play")
var srsLog = flag.Bool("srs-log", false, "Whether enable the detail log")
var srsTimeout = flag.Int("srs-timeout", 5000, "For each case, the timeout in ms")
var srsPlayPLI = flag.Int("srs-play-pli", 5000, "The PLI interval in seconds for player.")
var srsPlayOKPackets = flag.Int("srs-play-ok-packets", 10, "If got N packets, it's ok, or fail")
var srsPublishOKPackets = flag.Int("srs-publish-ok-packets", 10, "If send N packets, it's ok, or fail")
var srsPublishAudio = flag.String("srs-publish-audio", "avatar.ogg", "The audio file for publisher.")
var srsPublishVideo = flag.String("srs-publish-video", "avatar.h264", "The video file for publisher.")
var srsPublishVideoFps = flag.Int("srs-publish-video-fps", 25, "The video fps for publisher.")
var srsVnetClientIP = flag.String("srs-vnet-client-ip", "192.168.168.168", "The client ip in pion/vnet.")
var srsDTLSDropPackets = flag.Int("srs-dtls-drop-packets", 5, "If dropped N packets, it's ok, or fail")

func prepareTest() error {
	var err error

	// Should parse it first.
	flag.Parse()

	// The stream should starts with /, for example, /rtc/regression
	if !strings.HasPrefix(*srsStream, "/") {
		*srsStream = "/" + *srsStream
	}

	// Generate srs protocol from whether use HTTPS.
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

func TestMain(m *testing.M) {
	if err := prepareTest(); err != nil {
		logger.Ef(nil, "Prepare test fail, err %+v", err)
		os.Exit(-1)
	}

	// Disable the logger during all tests.
	if *srsLog == false {
		olw := logger.Switch(ioutil.Discard)
		defer func() {
			logger.Switch(olw)
		}()
	}

	os.Exit(m.Run())
}

type TestWebRTCAPIOptionFunc func(api *TestWebRTCAPI)

type TestWebRTCAPI struct {
	// The options to setup the api.
	options []TestWebRTCAPIOptionFunc
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

func NewTestWebRTCAPI(options ...TestWebRTCAPIOptionFunc) (*TestWebRTCAPI, error) {
	v := &TestWebRTCAPI{}

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

func (v *TestWebRTCAPI) Close() error {
	if v.proxy != nil {
		v.proxy.Close()
		v.proxy = nil
	}

	if v.router != nil {
		v.router.Stop()
		v.router = nil
	}

	return nil
}

func (v *TestWebRTCAPI) Setup(vnetClientIP string, options ...TestWebRTCAPIOptionFunc) error {
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
		v.network = vnet.NewNet(&vnet.NetConfig{
			StaticIP: vnetClientIP,
		})

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

func (v *TestWebRTCAPI) NewPeerConnection(configuration webrtc.Configuration) (*webrtc.PeerConnection, error) {
	return v.api.NewPeerConnection(configuration)
}

type TestPlayerOptionFunc func(p *TestPlayer)

type TestPlayer struct {
	pc        *webrtc.PeerConnection
	receivers []*webrtc.RTPReceiver
	// root api object
	api *TestWebRTCAPI
	// Optional suffix for stream url.
	streamSuffix string
}

func NewTestPlayer(api *TestWebRTCAPI, options ...TestPlayerOptionFunc) *TestPlayer {
	v := &TestPlayer{api: api}

	for _, opt := range options {
		opt(v)
	}

	return v
}

func (v *TestPlayer) Close() error {
	if v.pc != nil {
		v.pc.Close()
		v.pc = nil
	}

	for _, receiver := range v.receivers {
		receiver.Stop()
	}
	v.receivers = nil

	return nil
}

func (v *TestPlayer) Run(ctx context.Context, cancel context.CancelFunc) error {
	r := fmt.Sprintf("%v://%v%v", srsSchema, *srsServer, *srsStream)
	if v.streamSuffix != "" {
		r = fmt.Sprintf("%v-%v", r, v.streamSuffix)
	}
	pli := time.Duration(*srsPlayPLI) * time.Millisecond
	logger.Tf(ctx, "Start play url=%v", r)

	pc, err := v.api.NewPeerConnection(webrtc.Configuration{})
	if err != nil {
		return errors.Wrapf(err, "Create PC")
	}
	v.pc = pc

	pc.AddTransceiverFromKind(webrtc.RTPCodecTypeAudio, webrtc.RTPTransceiverInit{
		Direction: webrtc.RTPTransceiverDirectionRecvonly,
	})
	pc.AddTransceiverFromKind(webrtc.RTPCodecTypeVideo, webrtc.RTPTransceiverInit{
		Direction: webrtc.RTPTransceiverDirectionRecvonly,
	})

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

	// Start a proxy for real server and vnet.
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

type TestPublisherOptionFunc func(p *TestPublisher)

type TestPublisher struct {
	onOffer        func(s *webrtc.SessionDescription) error
	onAnswer       func(s *webrtc.SessionDescription) error
	iceReadyCancel context.CancelFunc
	// internal objects
	aIngester *audioIngester
	vIngester *videoIngester
	pc        *webrtc.PeerConnection
	// root api object
	api *TestWebRTCAPI
	// Optional suffix for stream url.
	streamSuffix string
}

func NewTestPublisher(api *TestWebRTCAPI, options ...TestPublisherOptionFunc) *TestPublisher {
	sourceVideo, sourceAudio := *srsPublishVideo, *srsPublishAudio

	v := &TestPublisher{api: api}

	for _, opt := range options {
		opt(v)
	}

	// Create ingesters.
	if sourceAudio != "" {
		v.aIngester = NewAudioIngester(sourceAudio)
	}
	if sourceVideo != "" {
		v.vIngester = NewVideoIngester(sourceVideo)
	}

	// Setup the interceptors for packets.
	api.options = append(api.options, func(api *TestWebRTCAPI) {
		// Filter for RTCP packets.
		rtcpInterceptor := &RTCPInterceptor{}
		rtcpInterceptor.rtcpReader = func(buf []byte, attributes interceptor.Attributes) (int, interceptor.Attributes, error) {
			return rtcpInterceptor.nextRTCPReader.Read(buf, attributes)
		}
		rtcpInterceptor.rtcpWriter = func(pkts []rtcp.Packet, attributes interceptor.Attributes) (int, error) {
			return rtcpInterceptor.nextRTCPWriter.Write(pkts, attributes)
		}
		api.registry.Add(rtcpInterceptor)

		// Filter for ingesters.
		if sourceAudio != "" {
			api.registry.Add(v.aIngester.audioLevelInterceptor)
		}
		if sourceVideo != "" {
			api.registry.Add(v.vIngester.markerInterceptor)
		}
	})

	return v
}

func (v *TestPublisher) Close() error {
	if v.vIngester != nil {
		v.vIngester.Close()
	}

	if v.aIngester != nil {
		v.aIngester.Close()
	}

	if v.pc != nil {
		v.pc.Close()
	}

	return nil
}

func (v *TestPublisher) SetStreamSuffix(suffix string) *TestPublisher {
	v.streamSuffix = suffix
	return v
}

func (v *TestPublisher) Run(ctx context.Context, cancel context.CancelFunc) error {
	r := fmt.Sprintf("%v://%v%v", srsSchema, *srsServer, *srsStream)
	if v.streamSuffix != "" {
		r = fmt.Sprintf("%v-%v", r, v.streamSuffix)
	}
	sourceVideo, sourceAudio, fps := *srsPublishVideo, *srsPublishAudio, *srsPublishVideoFps

	logger.Tf(ctx, "Start publish url=%v, audio=%v, video=%v, fps=%v",
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
		defer v.vIngester.Close()
	}

	if v.aIngester != nil {
		if err := v.aIngester.AddTrack(pc); err != nil {
			return errors.Wrapf(err, "Add track")
		}
		defer v.aIngester.Close()
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

	// Start a proxy for real server and vnet.
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
			v.aIngester.sAudioSender.Stop()
		}

		if v.vIngester != nil && v.vIngester.sVideoSender != nil {
			v.vIngester.sVideoSender.Stop()
		}
	}()

	wg.Add(1)
	go func() {
		defer wg.Done()
		defer cancel()

		if v.aIngester == nil {
			return
		}

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

func TestRTCServerVersion(t *testing.T) {
	api := fmt.Sprintf("http://%v:1985/api/v1/versions", *srsServer)
	req, err := http.NewRequest("POST", api, nil)
	if err != nil {
		t.Errorf("Request %v", api)
		return
	}

	res, err := http.DefaultClient.Do(req)
	if err != nil {
		t.Errorf("Do request %v", api)
		return
	}

	b, err := ioutil.ReadAll(res.Body)
	if err != nil {
		t.Errorf("Read body of %v", api)
		return
	}

	obj := struct {
		Code   int    `json:"code"`
		Server string `json:"server"`
		Data   struct {
			Major    int    `json:"major"`
			Minor    int    `json:"minor"`
			Revision int    `json:"revision"`
			Version  string `json:"version"`
		} `json:"data"`
	}{}
	if err := json.Unmarshal(b, &obj); err != nil {
		t.Errorf("Parse %v", string(b))
		return
	}
	if obj.Code != 0 {
		t.Errorf("Server err code=%v, server=%v", obj.Code, obj.Server)
		return
	}
	if obj.Data.Major == 0 && obj.Data.Minor == 0 {
		t.Errorf("Invalid version %v", obj.Data)
		return
	}
}
