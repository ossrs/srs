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
	"context"
	"fmt"
	"github.com/ossrs/go-oryx-lib/errors"
	"github.com/ossrs/go-oryx-lib/logger"
	"github.com/pion/interceptor"
	"github.com/pion/sdp/v3"
	"github.com/pion/webrtc/v3"
	"io"
	"net/url"
	"strconv"
	"strings"
	"sync"
)

func startPublish(ctx context.Context, r, sourceAudio, sourceVideo string, fps int, enableAudioLevel, enableTWCC bool) error {
	ctx = logger.WithContext(ctx)

	u, err := url.Parse(r)
	if err != nil {
		return errors.Wrapf(err, "Parse url %v", r)
	}

	var room int
	var display string
	if us := strings.SplitN(u.Path, "/", 3); len(us) >= 3 {
		if iv, err := strconv.Atoi(us[1]); err != nil {
			return errors.Wrapf(err, "parse %v", us[1])
		} else {
			room = iv
		}

		display = strings.Join(us[2:], "-")
	}

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

	// Signaling API
	api := newJanusAPI(fmt.Sprintf("http://%v/janus", u.Host))

	webrtcUpCtx, webrtcUpCancel := context.WithCancel(ctx)
	api.onWebrtcUp = func(sender, sessionID uint64) {
		logger.Tf(ctx, "Event webrtcup: DTLS/SRTP done, from=(sender:%v,session:%v)", sender, sessionID)
		webrtcUpCancel()
	}
	api.onMedia = func(sender, sessionID uint64, mtype string, receiving bool) {
		logger.Tf(ctx, "Event media: %v receiving=%v, from=(sender:%v,session:%v)", mtype, receiving, sender, sessionID)
	}
	api.onSlowLink = func(sender, sessionID uint64, media string, lost uint64, uplink bool) {
		logger.Tf(ctx, "Event slowlink: %v lost=%v, uplink=%v, from=(sender:%v,session:%v)", media, lost, uplink, sender, sessionID)
	}
	api.onPublisher = func(sender, sessionID uint64, publishers []publisherInfo) {
		logger.Tf(ctx, "Event publisher: %v, from=(sender:%v,session:%v)", publishers, sender, sessionID)
	}
	api.onUnPublished = func(sender, sessionID, id uint64) {
		logger.Tf(ctx, "Event unpublish: %v, from=(sender:%v,session:%v)", id, sender, sessionID)
	}
	api.onLeave = func(sender, sessionID, id uint64) {
		logger.Tf(ctx, "Event leave: %v, from=(sender:%v,session:%v)", id, sender, sessionID)
	}

	if err := api.Create(ctx); err != nil {
		return errors.Wrapf(err, "create")
	}
	defer api.Close()

	publishHandleID, err := api.AttachPlugin(ctx)
	if err != nil {
		return errors.Wrapf(err, "attach plugin")
	}
	defer api.DetachPlugin(ctx, publishHandleID)

	if err := api.JoinAsPublisher(ctx, publishHandleID, room, display); err != nil {
		return errors.Wrapf(err, "join as publisher")
	}

	answer, err := api.Publish(ctx, publishHandleID, offer.SDP)
	if err != nil {
		return errors.Wrapf(err, "join as publisher")
	}
	defer api.UnPublish(ctx, publishHandleID)

	// Setup the offer-answer
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

	// OK, DTLS/SRTP ok.
	select {
	case <-ctx.Done():
		return nil
	case <-webrtcUpCtx.Done():
	}

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

	wg.Wait()
	return nil
}
