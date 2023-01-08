// The MIT License (MIT)
//
// # Copyright (c) 2023 Winlin
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
	"github.com/ossrs/go-oryx-lib/errors"
	"github.com/ossrs/go-oryx-lib/logger"
	"math/rand"
	"os"
	"path"
	"sync"
	"testing"
	"time"
)

func TestFast_RtmpPublish_DvrFlv_Basic(t *testing.T) {
	// This case is run in parallel.
	t.Parallel()

	// Setup the max timeout for this case.
	ctx, cancel := context.WithTimeout(logger.WithContext(context.Background()), time.Duration(*srsTimeout)*time.Millisecond)
	defer cancel()

	// Check a set of errors.
	var r0, r1, r2, r3, r4, r5, r6 error
	defer func(ctx context.Context) {
		if err := filterTestError(ctx.Err(), r0, r1, r2, r3, r4, r5, r6); err != nil {
			t.Errorf("Fail for err %+v", err)
		} else {
			logger.Tf(ctx, "test done with err %+v", err)
		}
	}(ctx)

	var wg sync.WaitGroup
	defer wg.Wait()

	// Start hooks service.
	hooks := NewHooksService()
	wg.Add(1)
	go func() {
		defer wg.Done()
		r6 = hooks.Run(ctx, cancel)
	}()

	// Start SRS server and wait for it to be ready.
	svr := NewSRSServer(func(v *srsServer) {
		v.envs = []string{
			"SRS_VHOST_DVR_ENABLED=on",
			"SRS_VHOST_DVR_DVR_PLAN=session",
			"SRS_VHOST_DVR_DVR_PATH=./objs/nginx/html/[app]/[stream].[timestamp].flv",
			fmt.Sprintf("SRS_VHOST_DVR_DVR_DURATION=%v", *srsFFprobeDuration),
			"SRS_VHOST_HTTP_HOOKS_ENABLED=on",
			fmt.Sprintf("SRS_VHOST_HTTP_HOOKS_ON_DVR=http://localhost:%v/api/v1/dvrs", hooks.HooksAPI()),
		}
	})
	wg.Add(1)
	go func() {
		defer wg.Done()
		<-hooks.ReadyCtx().Done()
		r0 = svr.Run(ctx, cancel)
	}()

	// Start FFmpeg to publish stream.
	duration := time.Duration(*srsFFprobeDuration) * time.Millisecond
	streamID := fmt.Sprintf("stream-%v-%v", os.Getpid(), rand.Int())
	streamURL := fmt.Sprintf("rtmp://localhost:%v/live/%v", svr.RTMPPort(), streamID)
	ffmpeg := NewFFmpeg(func(v *ffmpegClient) {
		// When process quit, still keep case to run.
		v.cancelCaseWhenQuit, v.ffmpegDuration = false, duration
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
	ffprobe := NewFFprobe(func(v *ffprobeClient) {
		v.dvrByFFmpeg, v.streamURL = false, streamURL
		v.duration, v.timeout = duration, time.Duration(*srsFFprobeTimeout)*time.Millisecond

		wg.Add(1)
		go func() {
			defer wg.Done()
			for evt := range hooks.HooksEvents() {
				if onDvrEvt, ok := evt.(*HooksEventOnDvr); ok {
					fp := path.Join(svr.WorkDir(), onDvrEvt.File)
					logger.Tf(ctx, "FFprobe: Set the dvrFile=%v from callback", fp)
					v.dvrFile = fp
				}
			}
		}()
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

		if ts := 90; m.Format.ProbeScore < ts {
			r4 = errors.Errorf("low score=%v < %v, %v, %v", m.Format.ProbeScore, ts, m.String(), str)
		}
		if dv := m.Duration(); dv < duration/2 {
			r5 = errors.Errorf("short duration=%v < %v, %v, %v", dv, duration/2, m.String(), str)
		}
	}
}

func TestFast_RtmpPublish_DvrMp4_Basic(t *testing.T) {
	// This case is run in parallel.
	t.Parallel()

	// Setup the max timeout for this case.
	ctx, cancel := context.WithTimeout(logger.WithContext(context.Background()), time.Duration(*srsTimeout)*time.Millisecond)
	defer cancel()

	// Check a set of errors.
	var r0, r1, r2, r3, r4, r5, r6 error
	defer func(ctx context.Context) {
		if err := filterTestError(ctx.Err(), r0, r1, r2, r3, r4, r5, r6); err != nil {
			t.Errorf("Fail for err %+v", err)
		} else {
			logger.Tf(ctx, "test done with err %+v", err)
		}
	}(ctx)

	var wg sync.WaitGroup
	defer wg.Wait()

	// Start hooks service.
	hooks := NewHooksService()
	wg.Add(1)
	go func() {
		defer wg.Done()
		r6 = hooks.Run(ctx, cancel)
	}()

	// Start SRS server and wait for it to be ready.
	svr := NewSRSServer(func(v *srsServer) {
		v.envs = []string{
			"SRS_VHOST_DVR_ENABLED=on",
			"SRS_VHOST_DVR_DVR_PLAN=session",
			"SRS_VHOST_DVR_DVR_PATH=./objs/nginx/html/[app]/[stream].[timestamp].mp4",
			fmt.Sprintf("SRS_VHOST_DVR_DVR_DURATION=%v", *srsFFprobeDuration),
			"SRS_VHOST_HTTP_HOOKS_ENABLED=on",
			fmt.Sprintf("SRS_VHOST_HTTP_HOOKS_ON_DVR=http://localhost:%v/api/v1/dvrs", hooks.HooksAPI()),
		}
	})
	wg.Add(1)
	go func() {
		defer wg.Done()
		<-hooks.ReadyCtx().Done()
		r0 = svr.Run(ctx, cancel)
	}()

	// Start FFmpeg to publish stream.
	duration := time.Duration(*srsFFprobeDuration) * time.Millisecond
	streamID := fmt.Sprintf("stream-%v-%v", os.Getpid(), rand.Int())
	streamURL := fmt.Sprintf("rtmp://localhost:%v/live/%v", svr.RTMPPort(), streamID)
	ffmpeg := NewFFmpeg(func(v *ffmpegClient) {
		// When process quit, still keep case to run.
		v.cancelCaseWhenQuit, v.ffmpegDuration = false, duration
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
	ffprobe := NewFFprobe(func(v *ffprobeClient) {
		v.dvrByFFmpeg, v.streamURL = false, streamURL
		v.duration, v.timeout = duration, time.Duration(*srsFFprobeTimeout)*time.Millisecond

		wg.Add(1)
		go func() {
			defer wg.Done()
			for evt := range hooks.HooksEvents() {
				if onDvrEvt, ok := evt.(*HooksEventOnDvr); ok {
					fp := path.Join(svr.WorkDir(), onDvrEvt.File)
					logger.Tf(ctx, "FFprobe: Set the dvrFile=%v from callback", fp)
					v.dvrFile = fp
				}
			}
		}()
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

		if ts := 90; m.Format.ProbeScore < ts {
			r4 = errors.Errorf("low score=%v < %v, %v, %v", m.Format.ProbeScore, ts, m.String(), str)
		}
		if dv := m.Duration(); dv < duration/2 {
			r5 = errors.Errorf("short duration=%v < %v, %v, %v", dv, duration/2, m.String(), str)
		}
	}
}
