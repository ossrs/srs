// The MIT License (MIT)
//
// # Copyright (c) 2026 Winlin
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
	"encoding/json"
	"fmt"
	"io/ioutil"
	"math/rand"
	"net/http"
	"os"
	"path"
	"sync"
	"testing"
	"time"

	"github.com/ossrs/go-oryx-lib/errors"
	"github.com/ossrs/go-oryx-lib/logger"
)

func TestFast_SrtPublish_SrtPlay_Basic(t *testing.T) {
	// This case is run in parallel.
	t.Parallel()

	// Setup the max timeout for this case.
	ctx, cancel := context.WithTimeout(logger.WithContext(context.Background()), time.Duration(*srsTimeout)*time.Millisecond)
	defer cancel()

	// Check a set of errors.
	var r0, r1, r2, r3, r4, r5, r6, r7 error
	defer func(ctx context.Context) {
		if err := filterTestError(ctx.Err(), r0, r1, r2, r3, r4, r5, r6, r7); err != nil {
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
			"SRS_SRT_SERVER_ENABLED=on",
			"SRS_VHOST_SRT_ENABLED=on",
		}
	})
	wg.Add(1)
	go func() {
		defer wg.Done()
		r0 = svr.Run(ctx, cancel)
	}()

	// Start FFmpeg to publish stream.
	streamID := fmt.Sprintf("stream-%v-%v", os.Getpid(), rand.Int())
	streamURL := fmt.Sprintf("srt://localhost:%v?streamid=#!::r=live/%v,m=publish", svr.SRTPort(), streamID)
	ffmpeg := NewFFmpeg(func(v *ffmpegClient) {
		v.args = []string{
			"-stream_loop", "-1", "-re", "-i", *srsPublishAvatar, "-c", "copy",
			"-pes_payload_size", "0", "-f", "mpegts", streamURL,
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
		v.dvrFile = path.Join(svr.WorkDir(), "objs", fmt.Sprintf("srs-ffprobe-%v.ts", streamID))
		v.streamURL = fmt.Sprintf("srt://localhost:%v?streamid=#!::r=live/%v,m=request", svr.SRTPort(), streamID)
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

		// Note that SRT score is low, so we only check duration.
		if dv := m.Duration(); dv < duration {
			r5 = errors.Errorf("short duration=%v < %v, %v, %v", dv, duration, m.String(), str)
		}
	}
}

// srtClient is a client of /api/v1/clients/, with its SRT stats, which is null if not SRT.
type srtClient struct {
	ID   string `json:"id"`
	Name string `json:"name"`
	Type string `json:"type"`
	Srt  *struct {
		RttMs         float64 `json:"rtt_ms"`
		RecvLatencyMs int     `json:"recv_latency_ms"`
		RecvPackets   int64   `json:"recv_packets"`
		RecvLoss      int64   `json:"recv_loss"`
		RecvRetrans   int64   `json:"recv_retrans"`
		RecvDrop      int64   `json:"recv_drop"`
	} `json:"srt"`
}

// srtClients returns the clients of an SRS, from its HTTP API.
func srtClients(apiPort int) ([]srtClient, error) {
	api := fmt.Sprintf("http://localhost:%v/api/v1/clients/?count=100", apiPort)
	res, err := http.Get(api)
	if err != nil {
		return nil, errors.Wrapf(err, "get %v", api)
	}
	defer res.Body.Close()

	b, err := ioutil.ReadAll(res.Body)
	if err != nil {
		return nil, errors.Wrapf(err, "read %v", api)
	}

	answer := struct {
		Code    int         `json:"code"`
		Clients []srtClient `json:"clients"`
	}{}
	if err := json.Unmarshal(b, &answer); err != nil {
		return nil, errors.Wrapf(err, "parse %v of %v", string(b), api)
	}
	if answer.Code != 0 {
		return nil, errors.Errorf("code=%v of %v, %v", answer.Code, api, string(b))
	}
	return answer.Clients, nil
}

// An SRT publisher and an RTMP publisher: GET /api/v1/clients/ shows the SRT stats of the SRT publisher, with the
// packets it received and the latency it negotiated, and a null srt for the RTMP publisher.
func TestFast_SrtPublish_ClientSrtStats(t *testing.T) {
	// This case is run in parallel.
	t.Parallel()

	// Setup the max timeout for this case.
	ctx, cancel := context.WithTimeout(logger.WithContext(context.Background()), time.Duration(*srsTimeout)*time.Millisecond)
	defer cancel()

	// Check a set of errors.
	var r0, r1, r2, r3 error
	defer func(ctx context.Context) {
		if err := filterTestError(ctx.Err(), r0, r1, r2, r3); err != nil {
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
			"SRS_SRT_SERVER_ENABLED=on",
			"SRS_VHOST_SRT_ENABLED=on",
		}
	})
	wg.Add(1)
	go func() {
		defer wg.Done()
		r0 = svr.Run(ctx, cancel)
	}()

	// Start FFmpeg to publish a stream by SRT, and another by RTMP.
	srtStream := fmt.Sprintf("stream-srt-%v-%v", os.Getpid(), rand.Int())
	rtmpStream := fmt.Sprintf("stream-rtmp-%v-%v", os.Getpid(), rand.Int())
	srtPublisher := NewFFmpeg(func(v *ffmpegClient) {
		v.args = []string{
			"-stream_loop", "-1", "-re", "-i", *srsPublishAvatar, "-c", "copy", "-pes_payload_size", "0", "-f", "mpegts",
			fmt.Sprintf("srt://localhost:%v?streamid=#!::r=live/%v,m=publish&latency=200000", svr.SRTPort(), srtStream),
		}
	})
	rtmpPublisher := NewFFmpeg(func(v *ffmpegClient) {
		v.args = []string{
			"-stream_loop", "-1", "-re", "-i", *srsPublishAvatar, "-c", "copy", "-f", "flv",
			fmt.Sprintf("rtmp://localhost:%v/live/%v", svr.RTMPPort(), rtmpStream),
		}
	})
	wg.Add(2)
	go func() {
		defer wg.Done()
		<-svr.ReadyCtx().Done()
		r1 = srtPublisher.Run(ctx, cancel)
	}()
	go func() {
		defer wg.Done()
		<-svr.ReadyCtx().Done()
		r2 = rtmpPublisher.Run(ctx, cancel)
	}()

	// Wait for both publishers, until the SRT publisher has received packets.
	r3 = func() error {
		<-svr.ReadyCtx().Done()

		var last []srtClient
		for ctx.Err() == nil {
			select {
			case <-ctx.Done():
			case <-time.After(300 * time.Millisecond):
			}

			clients, err := srtClients(svr.APIPort())
			if err != nil {
				return err
			}
			last = clients

			var srt, rtmp *srtClient
			for i := range clients {
				if c := &clients[i]; c.Name == srtStream {
					srt = c
				} else if c.Name == rtmpStream {
					rtmp = c
				}
			}
			if srt == nil || rtmp == nil || srt.Srt == nil || srt.Srt.RecvPackets == 0 {
				continue
			}

			if srt.Type != "srt-publish" {
				return errors.Errorf("srt client type=%v, %+v", srt.Type, clients)
			}
			if rtmp.Srt != nil {
				return errors.Errorf("rtmp client srt=%+v, want null", *rtmp.Srt)
			}
			if s := srt.Srt; s.RecvLatencyMs != 200 || s.RttMs < 0 || s.RecvLoss < 0 || s.RecvRetrans < 0 || s.RecvDrop < 0 {
				return errors.Errorf("invalid srt stats %+v", *s)
			}

			logger.Tf(ctx, "srt stats %+v", *srt.Srt)
			return nil
		}
		return errors.Errorf("no srt stats, clients=%+v", last)
	}()

	// Done, stop the publishers and SRS.
	cancel()
}
