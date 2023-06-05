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
	"context"
	"io"
	"sync"
	"time"

	"github.com/ossrs/go-oryx-lib/errors"
	"github.com/ossrs/go-oryx-lib/logger"
	"github.com/pion/interceptor"
	"github.com/pion/sdp/v3"
	"github.com/pion/webrtc/v3"
)

// @see https://github.com/pion/webrtc/blob/master/examples/play-from-disk/main.go
func startPublish(ctx context.Context, r, sourceAudio, sourceVideo string, fps int, enableAudioLevel, enableTWCC bool) error {
	ctx = logger.WithContext(ctx)

	logger.Tf(ctx, "Run publish url=%v, audio=%v, video=%v, fps=%v, audio-level=%v, twcc=%v",
		r, sourceAudio, sourceVideo, fps, enableAudioLevel, enableTWCC)

	// Filter for SPS/PPS marker.
	var aIngester *audioIngester
	var vIngester *videoIngester

	// For audio-level and sps/pps marker.
	// TODO: FIXME: Should share with player.
	webrtcNewPeerConnection := func(configuration webrtc.Configuration) (*webrtc.PeerConnection, error) {
		m := &webrtc.MediaEngine{}
		if err := m.RegisterDefaultCodecs(); err != nil {
			return nil, err
		}

		for _, extension := range []string{sdp.SDESMidURI, sdp.SDESRTPStreamIDURI, sdp.TransportCCURI} {
			if extension == sdp.TransportCCURI && !enableTWCC {
				continue
			}
			if err := m.RegisterHeaderExtension(webrtc.RTPHeaderExtensionCapability{URI: extension}, webrtc.RTPCodecTypeVideo); err != nil {
				return nil, err
			}
		}

		// https://github.com/pion/ion/issues/130
		// https://github.com/pion/ion-sfu/pull/373/files#diff-6f42c5ac6f8192dd03e5a17e9d109e90cb76b1a4a7973be6ce44a89ffd1b5d18R73
		for _, extension := range []string{sdp.SDESMidURI, sdp.SDESRTPStreamIDURI, sdp.AudioLevelURI} {
			if extension == sdp.AudioLevelURI && !enableAudioLevel {
				continue
			}
			if err := m.RegisterHeaderExtension(webrtc.RTPHeaderExtensionCapability{URI: extension}, webrtc.RTPCodecTypeAudio); err != nil {
				return nil, err
			}
		}

		registry := &interceptor.Registry{}
		if err := webrtc.RegisterDefaultInterceptors(m, registry); err != nil {
			return nil, err
		}

		if sourceAudio != "" {
			aIngester = newAudioIngester(sourceAudio)
			registry.Add(&rtpInteceptorFactory{aIngester.audioLevelInterceptor})
		}
		if sourceVideo != "" {
			vIngester = newVideoIngester(sourceVideo)
			registry.Add(&rtpInteceptorFactory{vIngester.markerInterceptor})
		}

		api := webrtc.NewAPI(webrtc.WithMediaEngine(m), webrtc.WithInterceptorRegistry(registry))
		return api.NewPeerConnection(configuration)
	}

	pc, err := webrtcNewPeerConnection(webrtc.Configuration{})
	if err != nil {
		return errors.Wrapf(err, "Create PC")
	}

	doClose := func() {
		if pc != nil {
			pc.Close()
		}
		if vIngester != nil {
			vIngester.Close()
		}
		if aIngester != nil {
			aIngester.Close()
		}
	}
	defer doClose()

	if vIngester != nil {
		if err := vIngester.AddTrack(pc, fps); err != nil {
			return errors.Wrapf(err, "Add track")
		}
	}

	if aIngester != nil {
		if err := aIngester.AddTrack(pc); err != nil {
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

	// ICE state management.
	pc.OnICEConnectionStateChange(func(state webrtc.ICEConnectionState) {
		logger.Tf(ctx, "ICE state %v", state)
	})

	pc.OnSignalingStateChange(func(state webrtc.SignalingState) {
		logger.Tf(ctx, "Signaling state %v", state)
	})

	if aIngester != nil {
		aIngester.sAudioSender.Transport().OnStateChange(func(state webrtc.DTLSTransportState) {
			logger.Tf(ctx, "DTLS state %v", state)
		})
	}

	ctx, cancel := context.WithCancel(ctx)
	pcDoneCtx, pcDoneCancel := context.WithCancel(context.Background())
	pc.OnConnectionStateChange(func(state webrtc.PeerConnectionState) {
		logger.Tf(ctx, "PC state %v", state)

		if state == webrtc.PeerConnectionStateConnected {
			pcDoneCancel()
		}

		if state == webrtc.PeerConnectionStateFailed || state == webrtc.PeerConnectionStateClosed {
			if ctx.Err() != nil {
				return
			}

			logger.Wf(ctx, "Close for PC state %v", state)
			cancel()
		}
	})

	// Wait for event from context or tracks.
	var wg sync.WaitGroup

	wg.Add(1)
	go func() {
		defer wg.Done()
		<-ctx.Done()
		doClose() // Interrupt the RTCP read.
	}()

	wg.Add(1)
	go func() {
		defer wg.Done()

		if aIngester == nil {
			return
		}

		select {
		case <-ctx.Done():
		case <-pcDoneCtx.Done():
			logger.Tf(ctx, "PC(ICE+DTLS+SRTP) done, start read audio packets")
		}

		buf := make([]byte, 1500)
		for ctx.Err() == nil {
			if _, _, err := aIngester.sAudioSender.Read(buf); err != nil {
				return
			}
		}
	}()

	wg.Add(1)
	go func() {
		defer wg.Done()

		if aIngester == nil {
			return
		}

		select {
		case <-ctx.Done():
		case <-pcDoneCtx.Done():
			logger.Tf(ctx, "PC(ICE+DTLS+SRTP) done, start ingest audio %v", sourceAudio)
		}

		// Read audio and send out.
		for ctx.Err() == nil {
			if err := aIngester.Ingest(ctx); err != nil {
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

		if vIngester == nil {
			return
		}

		select {
		case <-ctx.Done():
		case <-pcDoneCtx.Done():
			logger.Tf(ctx, "PC(ICE+DTLS+SRTP) done, start read video packets")
		}

		buf := make([]byte, 1500)
		for ctx.Err() == nil {
			if _, _, err := vIngester.sVideoSender.Read(buf); err != nil {
				return
			}
		}
	}()

	wg.Add(1)
	go func() {
		defer wg.Done()

		if vIngester == nil {
			return
		}

		select {
		case <-ctx.Done():
		case <-pcDoneCtx.Done():
			logger.Tf(ctx, "PC(ICE+DTLS+SRTP) done, start ingest video %v", sourceVideo)
		}

		for ctx.Err() == nil {
			if err := vIngester.Ingest(ctx); err != nil {
				if errors.Cause(err) == io.EOF {
					logger.Tf(ctx, "EOF, restart ingest video %v", sourceVideo)
					continue
				}
				logger.Wf(ctx, "Ignore video err %+v", err)
			}
		}
	}()

	wg.Add(1)
	go func() {
		defer wg.Done()

		for {
			select {
			case <-ctx.Done():
				return
			case <-time.After(5 * time.Second):
				gStatRTC.PeerConnection = pc.GetStats()
			}
		}
	}()

	wg.Wait()
	return nil
}
