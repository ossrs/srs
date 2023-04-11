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
	"github.com/ghettovoice/gosip/sip"
	"github.com/ossrs/go-oryx-lib/errors"
	"github.com/ossrs/go-oryx-lib/logger"
	"github.com/pion/webrtc/v3/pkg/media/h264reader"
	"github.com/yapingcat/gomedia/mpeg2"
	"io"
	"os"
	"path"
	"strconv"
	"strings"
	"sync"
	"time"
)

type GBSessionConfig struct {
	regTimeout    time.Duration
	inviteTimeout time.Duration
}

type GBSessionOutput struct {
	ssrc        int64
	mediaPort   int64
	clockRate   uint64
	payloadType uint8
}

type GBSession struct {
	// GB config.
	conf *GBSessionConfig
	// The output of session.
	out *GBSessionOutput
	// The SIP session object.
	sip *SIPSession
	// Callback when REGISTER done.
	onRegisterDone func(req, res sip.Message) error
	// Callback when got INVITE request.
	onInviteRequest func(req sip.Message) error
	// Callback when got INVITE 200 OK ACK request.
	onInviteOkAck func(req, res sip.Message) error
	// Callback when got MESSAGE response.
	onMessageHeartbeat func(req, res sip.Message) error
	// For heartbeat coroutines.
	heartbeatInterval time.Duration
	heartbeatCtx      context.Context
	cancel            context.CancelFunc
	// WaitGroup for coroutines.
	wg sync.WaitGroup
}

func NewGBSession(c *GBSessionConfig, sc *SIPConfig) *GBSession {
	return &GBSession{
		sip:  NewSIPSession(sc),
		conf: c,
		out: &GBSessionOutput{
			clockRate:   uint64(90000),
			payloadType: uint8(96),
		},
		heartbeatInterval: 1 * time.Second,
	}
}

func (v *GBSession) Close() error {
	if v.cancel != nil {
		v.cancel()
	}
	v.sip.Close()
	v.wg.Wait()
	return nil
}

func (v *GBSession) Connect(ctx context.Context) error {
	client := v.sip

	if err := client.Connect(ctx); err != nil {
		return errors.Wrap(err, "connect")
	}

	return ctx.Err()
}

func (v *GBSession) Register(ctx context.Context) error {
	client := v.sip

	for ctx.Err() == nil {
		ctx, regCancel := context.WithTimeout(ctx, v.conf.regTimeout)
		defer regCancel()

		regReq, regRes, err := client.Register(ctx)
		if err != nil {
			return errors.Wrap(err, "register")
		}
		logger.Tf(ctx, "Register id=%v, response=%v", regReq.MessageID(), regRes.MessageID())

		if v.onRegisterDone != nil {
			if err = v.onRegisterDone(regReq, regRes); err != nil {
				return errors.Wrap(err, "callback")
			}
		}

		break
	}

	return ctx.Err()
}

func (v *GBSession) Invite(ctx context.Context) error {
	client := v.sip

	for ctx.Err() == nil {
		ctx, inviteCancel := context.WithTimeout(ctx, v.conf.inviteTimeout)
		defer inviteCancel()

		inviteReq, err := client.Wait(ctx, sip.INVITE)
		if err != nil {
			return errors.Wrap(err, "wait")
		}
		logger.Tf(ctx, "Got INVITE request, Call-ID=%v", sipGetCallID(inviteReq))

		if v.onInviteRequest != nil {
			if err = v.onInviteRequest(inviteReq); err != nil {
				return errors.Wrap(err, "callback")
			}
		}

		if err = client.Trying(ctx, inviteReq); err != nil {
			return errors.Wrapf(err, "trying invite is %v", inviteReq.String())
		}
		time.Sleep(100 * time.Millisecond)

		inviteRes, err := client.InviteResponse(ctx, inviteReq)
		if err != nil {
			return errors.Wrapf(err, "response invite is %v", inviteReq.String())
		}

		offer := inviteReq.Body()
		ssrcStr := strings.Split(strings.Split(offer, "y=")[1], "\r\n")[0]
		if v.out.ssrc, err = strconv.ParseInt(ssrcStr, 10, 64); err != nil {
			return errors.Wrapf(err, "parse ssrc=%v, sdp %v", ssrcStr, offer)
		}
		mediaPortStr := strings.Split(strings.Split(offer, "m=video")[1], " ")[1]
		if v.out.mediaPort, err = strconv.ParseInt(mediaPortStr, 10, 64); err != nil {
			return errors.Wrapf(err, "parse media port=%v, sdp %v", mediaPortStr, offer)
		}
		logger.Tf(ctx, "Invite id=%v, response=%v, y=%v, ssrc=%v, mediaPort=%v",
			inviteReq.MessageID(), inviteRes.MessageID(), ssrcStr, v.out.ssrc, v.out.mediaPort,
		)

		if v.onInviteOkAck != nil {
			if err = v.onInviteOkAck(inviteReq, inviteRes); err != nil {
				return errors.Wrap(err, "callback")
			}
		}

		break
	}

	// Start goroutine for heartbeat every 1s.
	v.heartbeatCtx, v.cancel = context.WithCancel(ctx)
	go func(ctx context.Context) {
		v.wg.Add(1)
		defer v.wg.Done()

		for ctx.Err() == nil {
			req, res, err := client.Message(ctx)
			if err != nil {
				v.cancel()
				logger.Ef(ctx, "heartbeat err %+v", err)
				return
			}

			if v.onMessageHeartbeat != nil {
				if err = v.onMessageHeartbeat(req, res); err != nil {
					v.cancel()
					logger.Ef(ctx, "callback err %+v", err)
					return
				}
			}

			select {
			case <-ctx.Done():
				return
			case <-time.After(v.heartbeatInterval):
			}
		}
	}(v.heartbeatCtx)

	return ctx.Err()
}

func (v *GBSession) Bye(ctx context.Context) error {
	client := v.sip

	for ctx.Err() == nil {
		ctx, regCancel := context.WithTimeout(ctx, v.conf.regTimeout)
		defer regCancel()

		regReq, regRes, err := client.Bye(ctx)
		if err != nil {
			return errors.Wrap(err, "bye")
		}
		logger.Tf(ctx, "Bye id=%v, response=%v", regReq.MessageID(), regRes.MessageID())

		break
	}

	return ctx.Err()
}

func (v *GBSession) UnRegister(ctx context.Context) error {
	client := v.sip

	for ctx.Err() == nil {
		ctx, regCancel := context.WithTimeout(ctx, v.conf.regTimeout)
		defer regCancel()

		regReq, regRes, err := client.UnRegister(ctx)
		if err != nil {
			return errors.Wrap(err, "UnRegister")
		}
		logger.Tf(ctx, "UnRegister id=%v, response=%v", regReq.MessageID(), regRes.MessageID())

		break
	}

	return ctx.Err()
}

type IngesterConfig struct {
	psConfig    PSConfig
	ssrc        uint32
	serverAddr  string
	clockRate   uint64
	payloadType uint8
}

type PSIngester struct {
	conf         *IngesterConfig
	onSendPacket func(pack *PSPackStream) error
	cancel       context.CancelFunc
}

func NewPSIngester(c *IngesterConfig) *PSIngester {
	return &PSIngester{conf: c}
}

func (v *PSIngester) Close() error {
	if v.cancel != nil {
		v.cancel()
	}
	return nil
}

func (v *PSIngester) Ingest(ctx context.Context) error {
	ctx, v.cancel = context.WithCancel(ctx)

	ps := NewPSClient(uint32(v.conf.ssrc), v.conf.serverAddr)
	if err := ps.Connect(ctx); err != nil {
		return errors.Wrapf(err, "connect media=%v", v.conf.serverAddr)
	}
	defer ps.Close()

	videoFile, err := os.Open(v.conf.psConfig.video)
	if err != nil {
		return errors.Wrapf(err, "Open file %v", v.conf.psConfig.video)
	}
	defer videoFile.Close()

	f, err := os.Open(v.conf.psConfig.audio)
	if err != nil {
		return errors.Wrapf(err, "Open file %v", v.conf.psConfig.audio)
	}
	defer f.Close()

	fileSuffix := path.Ext(v.conf.psConfig.video)
	var h264 *h264reader.H264Reader
	var h265 *H265Reader
	if fileSuffix == ".h265" {
		h265, err = NewReader(videoFile)
	} else {
		h264, err = h264reader.NewReader(videoFile)
	}
	if err != nil {
		return errors.Wrapf(err, "Open %v", v.conf.psConfig.video)
	}

	audio, err := NewAACReader(f)
	if err != nil {
		return errors.Wrapf(err, "Open ogg %v", v.conf.psConfig.audio)
	}

	// Scale the video samples to 1024 according to AAC, that is 1 video frame means 1024 samples.
	audioSampleRate := audio.codec.ASC().SampleRate.ToHz()
	videoSampleRate := 1024 * 1000 / v.conf.psConfig.fps
	logger.Tf(ctx, "PS: Media stream, tbn=%v, ssrc=%v, pt=%v, Video(%v, fps=%v, rate=%v), Audio(%v, rate=%v, channels=%v)",
		v.conf.clockRate, v.conf.ssrc, v.conf.payloadType, v.conf.psConfig.video, v.conf.psConfig.fps, videoSampleRate,
		v.conf.psConfig.audio, audioSampleRate, audio.codec.ASC().Channels)

	lastPrint := time.Now()
	var aacSamples, avcSamples uint64
	var audioDTS, videoDTS uint64
	defer func() {
		logger.Tf(ctx, "Consume Video(samples=%v, dts=%v, ts=%.2f) and Audio(samples=%v, dts=%v, ts=%.2f)",
			avcSamples, videoDTS, float64(videoDTS)/90.0, aacSamples, audioDTS, float64(audioDTS)/90.0,
		)
	}()

	clock := newWallClock()
	var pack *PSPackStream
	for ctx.Err() == nil {
		if pack == nil {
			pack = NewPSPackStream(v.conf.payloadType)
		}

		// One pack should only contains one video frame.
		if !pack.hasVideo {
			if fileSuffix == ".h265" {
				err = v.writeH265(ctx, pack, h265, videoSampleRate, &avcSamples, &videoDTS)
			} else {
				err = v.writeH264(ctx, pack, h264, videoSampleRate, &avcSamples, &videoDTS)
			}
			if err != nil {
				return errors.Wrap(err, "WriteVideo")
			}
		}

		// Always read and consume one audio frame each time.
		if true {
			audioFrame, err := audio.NextADTSFrame()
			if err != nil {
				return errors.Wrap(err, "Read AAC")
			}

			// Each AAC frame contains 1024 samples, DTS = total-samples / sample-rate
			aacSamples += 1024
			audioDTS = uint64(v.conf.clockRate*aacSamples) / uint64(audioSampleRate)
			if time.Now().Sub(lastPrint) > 3*time.Second {
				lastPrint = time.Now()
				logger.Tf(ctx, "Consume Video(samples=%v, dts=%v, ts=%.2f) and Audio(samples=%v, dts=%v, ts=%.2f)",
					avcSamples, videoDTS, float64(videoDTS)/90.0, aacSamples, audioDTS, float64(audioDTS)/90.0,
				)
			}

			if err = pack.WriteAudio(audioFrame, audioDTS); err != nil {
				return errors.Wrapf(err, "write audio %v", len(audioFrame))
			}
		}

		// Send pack when got video and enough audio frames.
		if pack.hasVideo && videoDTS < audioDTS {
			if err := ps.WritePacksOverRTP(pack.packets); err != nil {
				return errors.Wrap(err, "write")
			}
			if v.onSendPacket != nil {
				if err := v.onSendPacket(pack); err != nil {
					return errors.Wrap(err, "callback")
				}
			}
			pack = nil // Reset pack.
		}

		// One audio frame(1024 samples), the duration is 1024/audioSampleRate in seconds.
		sampleDuration := time.Duration(uint64(time.Second) * 1024 / uint64(audioSampleRate))
		if d := clock.Tick(sampleDuration); d > 0 {
			time.Sleep(d)
		}
	}

	return nil
}

func (v *PSIngester) writeH264(ctx context.Context, pack *PSPackStream, h264 *h264reader.H264Reader,
	videoSampleRate int, avcSamples, videoDTS *uint64) error {
	var sps, pps *h264reader.NAL
	var videoFrames []*h264reader.NAL
	for ctx.Err() == nil {
		frame, err := h264.NextNAL()
		if err == io.EOF {
			return io.EOF
		}
		if err != nil {
			return errors.Wrapf(err, "Read h264")
		}

		videoFrames = append(videoFrames, frame)
		logger.If(ctx, "NALU %v PictureOrderCount=%v, ForbiddenZeroBit=%v, RefIdc=%v, %v bytes",
			frame.UnitType.String(), frame.PictureOrderCount, frame.ForbiddenZeroBit, frame.RefIdc, len(frame.Data))

		if frame.UnitType == h264reader.NalUnitTypeSPS {
			sps = frame
		} else if frame.UnitType == h264reader.NalUnitTypePPS {
			pps = frame
		} else {
			break
		}
	}

	// We convert the video sample rate to be based over 1024, that is 1024 samples means one video frame.
	*avcSamples += 1024
	*videoDTS = uint64(v.conf.clockRate*(*avcSamples)) / uint64(videoSampleRate)

	var err error
	if sps != nil || pps != nil {
		err = pack.WriteHeader(mpeg2.PS_STREAM_H264, *videoDTS)
	} else {
		err = pack.WritePackHeader(*videoDTS)
	}
	if err != nil {
		return errors.Wrap(err, "pack header")
	}

	for _, frame := range videoFrames {
		if err = pack.WriteVideo(frame.Data, *videoDTS); err != nil {
			return errors.Wrapf(err, "write video %v", len(frame.Data))
		}
	}
	return nil
}

func (v *PSIngester) writeH265(ctx context.Context, pack *PSPackStream, h265 *H265Reader,
	videoSampleRate int, avcSamples, videoDTS *uint64) error {
	var vps, sps, pps *NAL
	var videoFrames []*NAL
	for ctx.Err() == nil {
		frame, err := h265.NextNAL()
		if err == io.EOF {
			return io.EOF
		}
		if err != nil {
			return errors.Wrapf(err, "Read h265")
		}

		videoFrames = append(videoFrames, frame)
		logger.If(ctx, "NALU %v PictureOrderCount=%v, ForbiddenZeroBit=%v, %v bytes",
			frame.UnitType, frame.PictureOrderCount, frame.ForbiddenZeroBit, len(frame.Data))

		if frame.UnitType == NaluTypeVps {
			vps = frame
		} else if frame.UnitType == NaluTypeSps {
			sps = frame
		} else if frame.UnitType == NaluTypePps {
			pps = frame
		} else {
			break
		}
	}

	// We convert the video sample rate to be based over 1024, that is 1024 samples means one video frame.
	*avcSamples += 1024
	*videoDTS = uint64(v.conf.clockRate*(*avcSamples)) / uint64(videoSampleRate)

	var err error
	if vps != nil || sps != nil || pps != nil {
		err = pack.WriteHeader(mpeg2.PS_STREAM_H265, *videoDTS)
	} else {
		err = pack.WritePackHeader(*videoDTS)
	}
	if err != nil {
		return errors.Wrap(err, "pack header")
	}

	for _, frame := range videoFrames {
		if err = pack.WriteVideo(frame.Data, *videoDTS); err != nil {
			return errors.Wrapf(err, "write video %v", len(frame.Data))
		}
	}
	return nil
}
