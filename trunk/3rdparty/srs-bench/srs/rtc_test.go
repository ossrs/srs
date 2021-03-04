package srs

import (
	"context"
	"encoding/json"
	"flag"
	"fmt"
	"github.com/ossrs/go-oryx-lib/errors"
	"github.com/ossrs/go-oryx-lib/logger"
	"github.com/ossrs/srs-bench/rtc"
	"github.com/pion/rtcp"
	"github.com/pion/webrtc/v3"
	"io"
	"io/ioutil"
	"net/http"
	"os"
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
var srsTimeout = flag.Int("srs-timeout", 3000, "For each case, the timeout in ms")
var srsPlayPLI = flag.Int("srs-play-pli", 5000, "The PLI interval in seconds for player.")
var srsPlayOKPackets = flag.Int("srs-play-ok-packets", 10, "If got N packets, it's ok, or fail")
var srsPublishAudio = flag.String("srs-publish-audio", "avatar.ogg", "The audio file for publisher.")
var srsPublishVideo = flag.String("srs-publish-video", "avatar.h264", "The video file for publisher.")
var srsPublishVideoFps = flag.Int("srs-publish-video-fps", 25, "The video fps for publisher.")

func TestMain(m *testing.M) {
	// Should parse it first.
	flag.Parse()

	// The stream should starts with /, for example, /rtc/regression
	if strings.HasPrefix(*srsStream, "/") {
		*srsStream = "/" + *srsStream
	}

	// Generate srs protocol from whether use HTTPS.
	if *srsHttps {
		srsSchema = "https"
	}

	// Disable the logger during all tests.
	logger.Tf(nil, "sys log %v", *srsLog)

	if *srsLog == false {
		olw := logger.Switch(ioutil.Discard)
		defer func() {
			logger.Switch(olw)
		}()
	}

	// Run tests.
	os.Exit(m.Run())
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

func TestRTCServerPublishPlay(t *testing.T) {
	ctx := logger.WithContext(context.Background())
	ctx, cancel := context.WithCancel(ctx)

	r := fmt.Sprintf("%v://%v%v", srsSchema, *srsServer, *srsStream)
	publishReady, publishReadyCancel := context.WithCancel(context.Background())

	startPlay := func(ctx context.Context) error {
		logger.Tf(ctx, "Start play url=%v", r)

		pc, err := webrtc.NewPeerConnection(webrtc.Configuration{})
		if err != nil {
			return errors.Wrapf(err, "Create PC")
		}
		defer pc.Close()

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
					case <-time.After(time.Duration(*srsPlayPLI) * time.Millisecond):
						_ = pc.WriteRTCP([]rtcp.Packet{&rtcp.PictureLossIndication{
							MediaSSRC: uint32(track.SSRC()),
						}})
					}
				}
			}()

			// Try to read packets of track.
			for i := 0; i < *srsPlayOKPackets && ctx.Err() == nil; i++ {
				_, _, err := track.ReadRTP()
				if err != nil {
					return errors.Wrapf(err, "Read RTP")
				}
			}

			// Completed.
			cancel()

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

	startPublish := func(ctx context.Context) error {
		sourceVideo := *srsPublishVideo
		sourceAudio := *srsPublishAudio
		fps := *srsPublishVideoFps

		logger.Tf(ctx, "Start publish url=%v, audio=%v, video=%v, fps=%v",
			r, sourceAudio, sourceVideo, fps)

		pc, err := webrtc.NewPeerConnection(webrtc.Configuration{})
		if err != nil {
			return errors.Wrapf(err, "Create PC")
		}
		defer pc.Close()

		var sVideoTrack *rtc.TrackLocalStaticSample
		var sVideoSender *webrtc.RTPSender
		if sourceVideo != "" {
			mimeType, trackID := "video/H264", "video"
			if strings.HasSuffix(sourceVideo, ".ivf") {
				mimeType = "video/VP8"
			}

			sVideoTrack, err = rtc.NewTrackLocalStaticSample(
				webrtc.RTPCodecCapability{MimeType: mimeType, ClockRate: 90000}, trackID, "pion",
			)
			if err != nil {
				return errors.Wrapf(err, "Create video track")
			}

			sVideoSender, err = pc.AddTrack(sVideoTrack)
			if err != nil {
				return errors.Wrapf(err, "Add video track")
			}
			sVideoSender.Stop()
		}

		var sAudioTrack *rtc.TrackLocalStaticSample
		var sAudioSender *webrtc.RTPSender
		if sourceAudio != "" {
			mimeType, trackID := "audio/opus", "audio"
			sAudioTrack, err = rtc.NewTrackLocalStaticSample(
				webrtc.RTPCodecCapability{MimeType: mimeType, ClockRate: 48000, Channels: 2}, trackID, "pion",
			)
			if err != nil {
				return errors.Wrapf(err, "Create audio track")
			}

			sAudioSender, err = pc.AddTrack(sAudioTrack)
			if err != nil {
				return errors.Wrapf(err, "Add audio track")
			}
			defer sAudioSender.Stop()
		}

		offer, err := pc.CreateOffer(nil)
		if err != nil {
			return errors.Wrapf(err, "Create Offer")
		}

		if err := pc.SetLocalDescription(offer); err != nil {
			return errors.Wrapf(err, "Set offer %v", offer)
		}

		answer, err := apiRtcRequest(ctx, "/rtc/v1/publish", r, offer.SDP)
		if err != nil {
			return errors.Wrapf(err, "Api request offer=%v", offer.SDP)
		}

		if err := pc.SetRemoteDescription(webrtc.SessionDescription{
			Type: webrtc.SDPTypeAnswer, SDP: answer,
		}); err != nil {
			return errors.Wrapf(err, "Set answer %v", answer)
		}

		logger.Tf(ctx, "State signaling=%v, ice=%v, conn=%v", pc.SignalingState(), pc.ICEConnectionState(), pc.ConnectionState())

		ctx, cancel := context.WithCancel(ctx)
		pcDone, pcDoneCancel := context.WithCancel(context.Background())
		pc.OnConnectionStateChange(func(state webrtc.PeerConnectionState) {
			logger.Tf(ctx, "PC state %v", state)

			if state == webrtc.PeerConnectionStateConnected {
				pcDoneCancel()
				publishReadyCancel()
			}

			if state == webrtc.PeerConnectionStateFailed || state == webrtc.PeerConnectionStateClosed {
				err = errors.Errorf("Close for PC state %v", state)
				cancel()
			}
		})

		// Wait for event from context or tracks.
		var wg sync.WaitGroup

		wg.Add(1)
		go func() {
			defer wg.Done()

			if sAudioSender == nil {
				return
			}

			select {
			case <-ctx.Done():
			case <-pcDone.Done():
			}

			buf := make([]byte, 1500)
			for ctx.Err() == nil {
				if _, _, err := sAudioSender.Read(buf); err != nil {
					return
				}
			}
		}()

		wg.Add(1)
		go func() {
			defer wg.Done()

			if sAudioTrack == nil {
				return
			}

			select {
			case <-ctx.Done():
			case <-pcDone.Done():
			}

			for ctx.Err() == nil {
				if err := readAudioTrackFromDisk(ctx, sourceAudio, sAudioSender, sAudioTrack); err != nil {
					if errors.Cause(err) == io.EOF {
						logger.Tf(ctx, "EOF, restart ingest audio %v", sourceAudio)
						continue
					}
					logger.Wf(ctx, "Ignore audio err %+v", err)
				}
			}
		}()

		wg.Add(1)
		go func() {
			defer wg.Done()

			if sVideoSender == nil {
				return
			}

			select {
			case <-ctx.Done():
			case <-pcDone.Done():
				logger.Tf(ctx, "PC(ICE+DTLS+SRTP) done, start read video packets")
			}

			buf := make([]byte, 1500)
			for ctx.Err() == nil {
				if _, _, err := sVideoSender.Read(buf); err != nil {
					return
				}
			}
		}()

		wg.Add(1)
		go func() {
			defer wg.Done()

			if sVideoTrack == nil {
				return
			}

			select {
			case <-ctx.Done():
			case <-pcDone.Done():
				logger.Tf(ctx, "PC(ICE+DTLS+SRTP) done, start ingest video %v", sourceVideo)
			}

			for ctx.Err() == nil {
				if err := readVideoTrackFromDisk(ctx, sourceVideo, sVideoSender, fps, sVideoTrack); err != nil {
					if errors.Cause(err) == io.EOF {
						logger.Tf(ctx, "EOF, restart ingest video %v", sourceVideo)
						continue
					}
					logger.Wf(ctx, "Ignore video err %+v", err)
				}
			}
		}()

		wg.Wait()
		return err
	}

	var wg sync.WaitGroup
	errs := make(chan error, 0)

	wg.Add(1)
	go func() {
		defer wg.Done()

		// Wait for publisher to start first.
		select {
		case <-ctx.Done():
			return
		case <-publishReady.Done():
		}

		errs <- startPlay(logger.WithContext(ctx))
		cancel()
	}()

	wg.Add(1)
	go func() {
		defer wg.Done()

		errs <- startPublish(logger.WithContext(ctx))
		cancel()
	}()

	wg.Add(1)
	go func() {
		defer wg.Done()

		select {
		case <-ctx.Done():
		case <-time.After(time.Duration(*srsTimeout) * time.Millisecond):
			errs <- errors.Errorf("timeout for %vms", *srsTimeout)
			cancel()
		}
	}()

	testDone, testDoneCancel := context.WithCancel(context.Background())
	go func() {
		wg.Wait()
		testDoneCancel()
	}()

	// Handle errs, the test result.
	for {
		select {
		case <-testDone.Done():
			return
		case err := <-errs:
			if err != nil && err != context.Canceled && !t.Failed() {
				t.Errorf("err %+v", err)
			}
		}
	}
}
