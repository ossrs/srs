// The MIT License (MIT)
//
// # Copyright (c) 2025 Winlin
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
package blackbox

import (
	"context"
	"fmt"
	"math/rand"
	"os"
	"path"
	"sync"
	"testing"
	"time"

	"github.com/ossrs/go-oryx-lib/errors"
	"github.com/ossrs/go-oryx-lib/logger"
)

func TestFast_RtmpPublish_RtspPlay_Basic(t *testing.T) {
	// This case is run in parallel.
	t.Parallel()

	// Setup the max timeout for this case.
	ctx, cancel := context.WithTimeout(logger.WithContext(context.Background()), time.Duration(*srsTimeout)*time.Millisecond)
	defer cancel()

	// Check a set of errors.
	var r0, r1, r2, r3, r4, r5 error
	defer func(ctx context.Context) {
		if err := filterTestError(ctx.Err(), r0, r1, r2, r3, r4, r5); err != nil {
			t.Errorf("Fail for err %+v", err)
		} else {
			logger.Tf(ctx, "test done with err %+v", err)
		}
	}(ctx)

	var wg sync.WaitGroup
	defer wg.Wait()

	// Start SRS server and wait for it to be ready.
	svr := NewSRSServer(func(v *srsServer) {
		v.envs = []string{
			"SRS_RTSP_SERVER_ENABLED=on",
			"SRS_VHOST_RTSP_ENABLED=on",
			"SRS_VHOST_RTSP_RTMP_TO_RTSP=on",
		}
	})
	wg.Add(1)
	go func() {
		defer wg.Done()
		r0 = svr.Run(ctx, cancel)
	}()

	// Start FFmpeg to publish stream.
	streamID := fmt.Sprintf("stream-%v-%v", os.Getpid(), rand.Int())
	streamURL := fmt.Sprintf("rtmp://localhost:%v/live/%v", svr.RTMPPort(), streamID)
	ffmpeg := NewFFmpeg(func(v *ffmpegClient) {
		v.args = []string{
			"-stream_loop", "-1", "-re", "-i", *srsPublishAvatar, "-c", "copy", "-f", "flv", streamURL,
		}
	})
	wg.Add(1)
	go func() {
		defer wg.Done()
		<-svr.ReadyCtx().Done()
		r1 = ffmpeg.Run(ctx, cancel)
	}()

	// Start FFprobe to detect and verify stream.
	duration := time.Duration(*srsFFprobeDuration) * time.Millisecond
	ffprobe := NewFFprobe(func(v *ffprobeClient) {
		v.dvrFile = path.Join(svr.WorkDir(), "objs", fmt.Sprintf("srs-ffprobe-%v.mp4", streamID))
		v.streamURL = fmt.Sprintf("rtsp://localhost:%v/live/%v", svr.RTSPPort(), streamID)
		v.duration, v.timeout = duration, time.Duration(*srsFFprobeTimeout)*time.Millisecond
	})
	wg.Add(1)
	go func() {
		defer wg.Done()
		<-svr.ReadyCtx().Done()
		r2 = ffprobe.Run(ctx, cancel)
	}()

	// Fast quit for probe done.
	select {
	case <-ctx.Done():
	case <-ffprobe.ProbeDoneCtx().Done():
		defer cancel()

		str, m := ffprobe.Result()
		if len(m.Streams) != 2 {
			r3 = errors.Errorf("invalid streams=%v, %v, %v", len(m.Streams), m.String(), str)
		}

		// Note that RTSP score might be lower than RTMP, so we use a lower threshold
		if ts := 80; m.Format.ProbeScore < ts {
			r4 = errors.Errorf("low score=%v < %v, %v, %v", m.Format.ProbeScore, ts, m.String(), str)
		}
		if dv := m.Duration(); dv < duration/2 {
			r5 = errors.Errorf("short duration=%v < %v, %v, %v", dv, duration, m.String(), str)
		}
	}
}

func TestFast_RtmpPublish_RtspPlay_MultipleClients(t *testing.T) {
	// This case is run in parallel.
	t.Parallel()

	// Setup the max timeout for this case.
	ctx, cancel := context.WithTimeout(logger.WithContext(context.Background()), time.Duration(*srsTimeout)*time.Millisecond)
	defer cancel()

	// Check a set of errors.
	var r0, r1, r2, r3, r4, r5, r6, r7, r8, r9 error
	defer func(ctx context.Context) {
		if err := filterTestError(ctx.Err(), r0, r1, r2, r3, r4, r5, r6, r7, r8, r9); err != nil {
			t.Errorf("Fail for err %+v", err)
		} else {
			logger.Tf(ctx, "test done with err %+v", err)
		}
	}(ctx)

	var wg sync.WaitGroup
	defer wg.Wait()

	// Start SRS server and wait for it to be ready.
	svr := NewSRSServer(func(v *srsServer) {
		v.envs = []string{
			"SRS_RTSP_SERVER_ENABLED=on",
			"SRS_VHOST_RTSP_ENABLED=on",
			"SRS_VHOST_RTSP_RTMP_TO_RTSP=on",
		}
	})
	wg.Add(1)
	go func() {
		defer wg.Done()
		r0 = svr.Run(ctx, cancel)
	}()

	// Start FFmpeg to publish stream.
	streamID := fmt.Sprintf("stream-%v-%v", os.Getpid(), rand.Int())
	streamURL := fmt.Sprintf("rtmp://localhost:%v/live/%v", svr.RTMPPort(), streamID)
	ffmpeg := NewFFmpeg(func(v *ffmpegClient) {
		v.args = []string{
			"-stream_loop", "-1", "-re", "-i", *srsPublishAvatar, "-c", "copy", "-f", "flv", streamURL,
		}
	})
	wg.Add(1)
	go func() {
		defer wg.Done()
		<-svr.ReadyCtx().Done()
		r1 = ffmpeg.Run(ctx, cancel)
	}()

	// Start multiple FFprobe clients to test concurrent RTSP playback.
	duration := time.Duration(*srsFFprobeDuration) * time.Millisecond
	rtspURL := fmt.Sprintf("rtsp://localhost:%v/live/%v", svr.RTSPPort(), streamID)

	// First RTSP client
	ffprobe1 := NewFFprobe(func(v *ffprobeClient) {
		v.dvrFile = path.Join(svr.WorkDir(), "objs", fmt.Sprintf("srs-ffprobe1-%v.mp4", streamID))
		v.streamURL = rtspURL
		v.duration, v.timeout = duration, time.Duration(*srsFFprobeTimeout)*time.Millisecond
	})
	wg.Add(1)
	go func() {
		defer wg.Done()
		<-svr.ReadyCtx().Done()
		r2 = ffprobe1.Run(ctx, cancel)
	}()

	// Second RTSP client
	ffprobe2 := NewFFprobe(func(v *ffprobeClient) {
		v.dvrFile = path.Join(svr.WorkDir(), "objs", fmt.Sprintf("srs-ffprobe2-%v.mp4", streamID))
		v.streamURL = rtspURL
		v.duration, v.timeout = duration, time.Duration(*srsFFprobeTimeout)*time.Millisecond
	})
	wg.Add(1)
	go func() {
		defer wg.Done()
		<-svr.ReadyCtx().Done()
		r3 = ffprobe2.Run(ctx, cancel)
	}()

	// Wait for both probes to complete and verify results.
	var probe1Done, probe2Done bool
	for !probe1Done || !probe2Done {
		select {
		case <-ctx.Done():
			return
		case <-ffprobe1.ProbeDoneCtx().Done():
			if !probe1Done {
				probe1Done = true
				str, m := ffprobe1.Result()
				if len(m.Streams) != 2 {
					r4 = errors.Errorf("client1: invalid streams=%v, %v, %v", len(m.Streams), m.String(), str)
				}
				if ts := 80; m.Format.ProbeScore < ts {
					r5 = errors.Errorf("client1: low score=%v < %v, %v, %v", m.Format.ProbeScore, ts, m.String(), str)
				}
				if dv := m.Duration(); dv < duration/2 {
					r6 = errors.Errorf("client1: short duration=%v < %v, %v, %v", dv, duration, m.String(), str)
				}
			}
		case <-ffprobe2.ProbeDoneCtx().Done():
			if !probe2Done {
				probe2Done = true
				str, m := ffprobe2.Result()
				if len(m.Streams) != 2 {
					r7 = errors.Errorf("client2: invalid streams=%v, %v, %v", len(m.Streams), m.String(), str)
				}
				if ts := 80; m.Format.ProbeScore < ts {
					r8 = errors.Errorf("client2: low score=%v < %v, %v, %v", m.Format.ProbeScore, ts, m.String(), str)
				}
				if dv := m.Duration(); dv < duration {
					r9 = errors.Errorf("client2: short duration=%v < %v, %v, %v", dv, duration, m.String(), str)
				}
			}
		}
	}
	defer cancel()
}

func TestFast_RtmpPublish_RtspPlay_CustomPort(t *testing.T) {
	// This case is run in parallel.
	t.Parallel()

	// Setup the max timeout for this case.
	ctx, cancel := context.WithTimeout(logger.WithContext(context.Background()), time.Duration(*srsTimeout)*time.Millisecond)
	defer cancel()

	// Check a set of errors.
	var r0, r1, r2, r3, r4, r5 error
	defer func(ctx context.Context) {
		if err := filterTestError(ctx.Err(), r0, r1, r2, r3, r4, r5); err != nil {
			t.Errorf("Fail for err %+v", err)
		} else {
			logger.Tf(ctx, "test done with err %+v", err)
		}
	}(ctx)

	var wg sync.WaitGroup
	defer wg.Wait()

	// Start SRS server with custom RTSP port.
	customRTSPPort := 15540 + rand.Intn(1000)
	svr := NewSRSServer(func(v *srsServer) {
		v.envs = []string{
			"SRS_RTSP_SERVER_ENABLED=on",
			"SRS_VHOST_RTSP_ENABLED=on",
			"SRS_VHOST_RTSP_RTMP_TO_RTSP=on",
			fmt.Sprintf("SRS_RTSP_SERVER_LISTEN=%d", customRTSPPort),
		}
	})
	wg.Add(1)
	go func() {
		defer wg.Done()
		r0 = svr.Run(ctx, cancel)
	}()

	// Start FFmpeg to publish stream.
	streamID := fmt.Sprintf("stream-%v-%v", os.Getpid(), rand.Int())
	streamURL := fmt.Sprintf("rtmp://localhost:%v/live/%v", svr.RTMPPort(), streamID)
	ffmpeg := NewFFmpeg(func(v *ffmpegClient) {
		v.args = []string{
			"-stream_loop", "-1", "-re", "-i", *srsPublishAvatar, "-c", "copy", "-f", "flv", streamURL,
		}
	})
	wg.Add(1)
	go func() {
		defer wg.Done()
		<-svr.ReadyCtx().Done()
		r1 = ffmpeg.Run(ctx, cancel)
	}()

	// Start FFprobe to detect and verify stream on custom port.
	duration := time.Duration(*srsFFprobeDuration) * time.Millisecond
	ffprobe := NewFFprobe(func(v *ffprobeClient) {
		v.dvrFile = path.Join(svr.WorkDir(), "objs", fmt.Sprintf("srs-ffprobe-%v.mp4", streamID))
		v.streamURL = fmt.Sprintf("rtsp://localhost:%v/live/%v", customRTSPPort, streamID)
		v.duration, v.timeout = duration, time.Duration(*srsFFprobeTimeout)*time.Millisecond
	})
	wg.Add(1)
	go func() {
		defer wg.Done()
		<-svr.ReadyCtx().Done()
		r2 = ffprobe.Run(ctx, cancel)
	}()

	// Fast quit for probe done.
	select {
	case <-ctx.Done():
	case <-ffprobe.ProbeDoneCtx().Done():
		defer cancel()

		str, m := ffprobe.Result()
		if len(m.Streams) != 2 {
			r3 = errors.Errorf("invalid streams=%v, %v, %v", len(m.Streams), m.String(), str)
		}

		// Note that RTSP score might be lower than RTMP, so we use a lower threshold
		if ts := 80; m.Format.ProbeScore < ts {
			r4 = errors.Errorf("low score=%v < %v, %v, %v", m.Format.ProbeScore, ts, m.String(), str)
		}
		if dv := m.Duration(); dv < duration/2 {
			r5 = errors.Errorf("short duration=%v < %v, %v, %v", dv, duration, m.String(), str)
		}
	}
}

func TestFast_RtmpPublish_RtspPlay_AudioOnly(t *testing.T) {
	// This case is run in parallel.
	t.Parallel()

	// Setup the max timeout for this case.
	ctx, cancel := context.WithTimeout(logger.WithContext(context.Background()), time.Duration(*srsTimeout)*time.Millisecond)
	defer cancel()

	// Check a set of errors.
	var r0, r1, r2, r3, r4, r5 error
	defer func(ctx context.Context) {
		if err := filterTestError(ctx.Err(), r0, r1, r2, r3, r4, r5); err != nil {
			t.Errorf("Fail for err %+v", err)
		} else {
			logger.Tf(ctx, "test done with err %+v", err)
		}
	}(ctx)

	var wg sync.WaitGroup
	defer wg.Wait()

	// Start SRS server and wait for it to be ready.
	svr := NewSRSServer(func(v *srsServer) {
		v.envs = []string{
			"SRS_RTSP_SERVER_ENABLED=on",
			"SRS_VHOST_RTSP_ENABLED=on",
			"SRS_VHOST_RTSP_RTMP_TO_RTSP=on",
		}
	})
	wg.Add(1)
	go func() {
		defer wg.Done()
		r0 = svr.Run(ctx, cancel)
	}()

	// Start FFmpeg to publish audio-only stream.
	streamID := fmt.Sprintf("stream-%v-%v", os.Getpid(), rand.Int())
	streamURL := fmt.Sprintf("rtmp://localhost:%v/live/%v", svr.RTMPPort(), streamID)
	ffmpeg := NewFFmpeg(func(v *ffmpegClient) {
		v.args = []string{
			"-stream_loop", "-1", "-re", "-i", *srsPublishAvatar, "-vn", "-c:a", "copy", "-f", "flv", streamURL,
		}
	})
	wg.Add(1)
	go func() {
		defer wg.Done()
		<-svr.ReadyCtx().Done()
		r1 = ffmpeg.Run(ctx, cancel)
	}()

	// Start FFprobe to detect and verify audio-only stream.
	duration := time.Duration(*srsFFprobeDuration) * time.Millisecond
	ffprobe := NewFFprobe(func(v *ffprobeClient) {
		v.dvrFile = path.Join(svr.WorkDir(), "objs", fmt.Sprintf("srs-ffprobe-%v.mp4", streamID))
		v.streamURL = fmt.Sprintf("rtsp://localhost:%v/live/%v", svr.RTSPPort(), streamID)
		v.duration, v.timeout = duration, time.Duration(*srsFFprobeTimeout)*time.Millisecond
	})
	wg.Add(1)
	go func() {
		defer wg.Done()
		<-svr.ReadyCtx().Done()
		r2 = ffprobe.Run(ctx, cancel)
	}()

	// Fast quit for probe done.
	select {
	case <-ctx.Done():
	case <-ffprobe.ProbeDoneCtx().Done():
		defer cancel()

		str, m := ffprobe.Result()
		// Audio-only stream should have 1 stream
		if len(m.Streams) != 1 {
			r3 = errors.Errorf("invalid streams=%v, expected 1 for audio-only, %v, %v", len(m.Streams), m.String(), str)
		}

		// Check if it's audio stream
		if len(m.Streams) > 0 && m.Streams[0].CodecType != "audio" {
			r4 = errors.Errorf("expected audio stream, got %v, %v, %v", m.Streams[0].CodecType, m.String(), str)
		}

		if dv := m.Duration(); dv < duration/2 {
			r5 = errors.Errorf("short duration=%v < %v, %v, %v", dv, duration, m.String(), str)
		}
	}
}
