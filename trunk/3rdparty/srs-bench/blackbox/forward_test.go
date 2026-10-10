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
	"net/http/httptest"
	"os"
	"path"
	"strings"
	"sync"
	"testing"
	"time"

	"github.com/ossrs/go-oryx-lib/errors"
	"github.com/ossrs/go-oryx-lib/logger"
)

// forwardHookRequest is the body SRS posts to a forward backend (action on_forward) or to an on_publish hook.
type forwardHookRequest struct {
	Action   string `json:"action"`
	ClientID string `json:"client_id"`
	IP       string `json:"ip"`
	Vhost    string `json:"vhost"`
	App      string `json:"app"`
	TcUrl    string `json:"tcUrl"`
	Stream   string `json:"stream"`
	Param    string `json:"param"`
}

// forwardBackend is the forward backend of a case, an HTTP server inside the test. It answers on_forward at
// /api/v1/forward by onForward, accepts on_publish at /api/v1/publish, and keeps every request it got.
type forwardBackend struct {
	server *httptest.Server

	// Answer an on_forward request with the HTTP status and body.
	onForward func(r *forwardHookRequest) (int, string)

	lock      sync.Mutex
	forwards  []forwardHookRequest
	publishes []forwardHookRequest
}

func newForwardBackend(ctx context.Context, onForward func(r *forwardHookRequest) (int, string)) *forwardBackend {
	v := &forwardBackend{onForward: onForward}

	readRequest := func(r *http.Request) (*forwardHookRequest, string, error) {
		b, err := ioutil.ReadAll(r.Body)
		if err != nil {
			return nil, "", err
		}
		req := &forwardHookRequest{}
		if err := json.Unmarshal(b, req); err != nil {
			return nil, string(b), err
		}
		return req, string(b), nil
	}

	mux := http.NewServeMux()
	mux.HandleFunc("/api/v1/forward", func(w http.ResponseWriter, r *http.Request) {
		req, body, err := readRequest(r)
		if err != nil {
			logger.Wf(ctx, "Backend: bad on_forward %v, err %v", body, err)
			w.WriteHeader(http.StatusBadRequest)
			return
		}

		v.lock.Lock()
		v.forwards = append(v.forwards, *req)
		v.lock.Unlock()

		status, answer := v.onForward(req)
		logger.Tf(ctx, "Backend: on_forward %v, answer %v %v", body, status, answer)
		w.Header().Set("Content-Type", "application/json")
		w.WriteHeader(status)
		w.Write([]byte(answer))
	})
	mux.HandleFunc("/api/v1/publish", func(w http.ResponseWriter, r *http.Request) {
		req, body, err := readRequest(r)
		if err != nil {
			logger.Wf(ctx, "Backend: bad on_publish %v, err %v", body, err)
			w.WriteHeader(http.StatusBadRequest)
			return
		}

		v.lock.Lock()
		v.publishes = append(v.publishes, *req)
		v.lock.Unlock()

		logger.Tf(ctx, "Backend: on_publish %v", body)
		w.Header().Set("Content-Type", "application/json")
		w.Write([]byte(`{"code":0}`))
	})

	v.server = httptest.NewServer(mux)
	return v
}

func (v *forwardBackend) Close() {
	v.server.Close()
}

// ForwardURL is the value of forward.backend.
func (v *forwardBackend) ForwardURL() string {
	return fmt.Sprintf("%v/api/v1/forward", v.server.URL)
}

// PublishURL is the value of http_hooks.on_publish.
func (v *forwardBackend) PublishURL() string {
	return fmt.Sprintf("%v/api/v1/publish", v.server.URL)
}

func (v *forwardBackend) Forwards() []forwardHookRequest {
	v.lock.Lock()
	defer v.lock.Unlock()
	return append([]forwardHookRequest{}, v.forwards...)
}

func (v *forwardBackend) Publishes() []forwardHookRequest {
	v.lock.Lock()
	defer v.lock.Unlock()
	return append([]forwardHookRequest{}, v.publishes...)
}

// forwardBackendAnswer is the on_forward answer of a backend that forwards to the urls.
func forwardBackendAnswer(urls ...string) string {
	if urls == nil {
		urls = []string{}
	}
	b, _ := json.Marshal(map[string]interface{}{
		"code": 0,
		"data": map[string]interface{}{"urls": urls},
	})
	return string(b)
}

// forwardCheckOnForward checks the backend got exactly one on_forward, for the published stream.
func forwardCheckOnForward(backend *forwardBackend, streamID string) error {
	reqs := backend.Forwards()
	if len(reqs) != 1 {
		return errors.Errorf("on_forward count=%v, expect=1, %+v", len(reqs), reqs)
	}

	r := reqs[0]
	if r.Action != "on_forward" || r.Vhost != "__defaultVhost__" || r.App != "live" || r.Stream != streamID {
		return errors.Errorf("on_forward action=%v, vhost=%v, app=%v, stream=%v, expect on_forward, __defaultVhost__, live, %v",
			r.Action, r.Vhost, r.App, r.Stream, streamID)
	}
	if !strings.HasSuffix(r.TcUrl, "/live") || r.ClientID == "" || r.IP == "" {
		return errors.Errorf("on_forward tcUrl=%v, client_id=%v, ip=%v", r.TcUrl, r.ClientID, r.IP)
	}
	return nil
}

// forwardCheckProbe checks a stream probed by ffprobe has audio and video of the expected duration.
func forwardCheckProbe(ffprobe FFprobeClient, duration time.Duration) error {
	str, m := ffprobe.Result()
	if len(m.Streams) != 2 || m.Video() == nil || m.Audio() == nil {
		return errors.Errorf("invalid streams=%v, %v, %v", len(m.Streams), m.String(), str)
	}
	if ts := 90; m.Format.ProbeScore < ts {
		return errors.Errorf("low score=%v < %v, %v, %v", m.Format.ProbeScore, ts, m.String(), str)
	}
	if dv := m.Duration(); dv < duration {
		return errors.Errorf("short duration=%v < %v, %v, %v", dv, duration, m.String(), str)
	}
	return nil
}

// forwardStream is a stream of /api/v1/streams/.
type forwardStream struct {
	ID      string `json:"id"`
	Name    string `json:"name"`
	App     string `json:"app"`
	Publish struct {
		Active bool   `json:"active"`
		Cid    string `json:"cid"`
	} `json:"publish"`
}

// forwardActiveStreams returns the publishing streams of an SRS, from its HTTP API.
func forwardActiveStreams(apiPort int) ([]forwardStream, error) {
	api := fmt.Sprintf("http://localhost:%v/api/v1/streams/?count=100", apiPort)
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
		Code    int             `json:"code"`
		Streams []forwardStream `json:"streams"`
	}{}
	if err := json.Unmarshal(b, &answer); err != nil {
		return nil, errors.Wrapf(err, "parse %v of %v", string(b), api)
	}
	if answer.Code != 0 {
		return nil, errors.Errorf("code=%v of %v, %v", answer.Code, api, string(b))
	}

	var active []forwardStream
	for _, s := range answer.Streams {
		if s.Publish.Active {
			active = append(active, s)
		}
	}
	return active, nil
}

// The backend returns one url with another app and stream and a token: the destination gets the stream there, with
// the token in its on_publish param.
func TestFast_RtmpPublish_Forward_Backend_Basic(t *testing.T) {
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

	streamID := fmt.Sprintf("stream-%v-%v", os.Getpid(), rand.Int())
	dstStream := fmt.Sprintf("%v-dst", streamID)

	// The backend forwards to the destination, with another app and stream and a token.
	var dst SRSServer
	backend := newForwardBackend(ctx, func(r *forwardHookRequest) (int, string) {
		return http.StatusOK, forwardBackendAnswer(
			fmt.Sprintf("rtmp://127.0.0.1:%v/fwd/%v?token=abc", dst.RTMPPort(), dstStream),
		)
	})
	defer backend.Close()

	// The destination SRS, which reports its publishers to the backend.
	dst = NewSRSServer(func(v *srsServer) {
		v.envs = []string{
			"SRS_VHOST_HTTP_HOOKS_ENABLED=on",
			fmt.Sprintf("SRS_VHOST_HTTP_HOOKS_ON_PUBLISH=%v", backend.PublishURL()),
		}
	})

	var wg sync.WaitGroup
	defer wg.Wait()

	wg.Add(1)
	go func() {
		defer wg.Done()
		r0 = dst.Run(ctx, cancel)
	}()

	// The origin SRS, which asks the backend where to forward.
	origin := NewSRSServer(func(v *srsServer) {
		v.envs = []string{
			"SRS_VHOST_FORWARD_ENABLED=on",
			fmt.Sprintf("SRS_VHOST_FORWARD_BACKEND=%v", backend.ForwardURL()),
		}
	})
	wg.Add(1)
	go func() {
		defer wg.Done()
		r1 = origin.Run(ctx, cancel)
	}()

	// Start FFmpeg to publish stream to the origin.
	streamURL := fmt.Sprintf("rtmp://localhost:%v/live/%v", origin.RTMPPort(), streamID)
	ffmpeg := NewFFmpeg(func(v *ffmpegClient) {
		v.args = []string{
			"-stream_loop", "-1", "-re", "-i", *srsPublishAvatar, "-c", "copy", "-f", "flv", streamURL,
		}
	})
	wg.Add(1)
	go func() {
		defer wg.Done()
		<-dst.ReadyCtx().Done()
		<-origin.ReadyCtx().Done()
		r2 = ffmpeg.Run(ctx, cancel)
	}()

	// Probe the forwarded stream on the destination.
	duration := time.Duration(*srsFFprobeDuration) * time.Millisecond
	ffprobe := NewFFprobe(func(v *ffprobeClient) {
		v.dvrFile = path.Join(dst.WorkDir(), "objs", fmt.Sprintf("srs-ffprobe-%v.flv", dstStream))
		v.streamURL = fmt.Sprintf("rtmp://localhost:%v/fwd/%v", dst.RTMPPort(), dstStream)
		v.duration, v.timeout = duration, time.Duration(*srsFFprobeTimeout)*time.Millisecond
	})
	wg.Add(1)
	go func() {
		defer wg.Done()
		<-dst.ReadyCtx().Done()
		<-origin.ReadyCtx().Done()
		r3 = ffprobe.Run(ctx, cancel)
	}()

	select {
	case <-ctx.Done():
	case <-ffprobe.ProbeDoneCtx().Done():
		defer cancel()

		r4 = forwardCheckProbe(ffprobe, duration)
		r5 = forwardCheckOnForward(backend, streamID)

		// The forwarder publishes the backend url as is: its app, stream and token reach the destination.
		pubs := backend.Publishes()
		if len(pubs) != 1 {
			r6 = errors.Errorf("on_publish count=%v, expect=1, %+v", len(pubs), pubs)
		} else if p := pubs[0]; p.App != "fwd" || p.Stream != dstStream || p.Param != "?token=abc" {
			r6 = errors.Errorf("on_publish app=%v, stream=%v, param=%v, expect fwd, %v, ?token=abc",
				p.App, p.Stream, p.Param, dstStream)
		}
	}
}

// The backend returns two urls: both destinations get the stream.
func TestFast_RtmpPublish_Forward_Backend_TwoUrls(t *testing.T) {
	// This case is run in parallel.
	t.Parallel()

	// Setup the max timeout for this case.
	ctx, cancel := context.WithTimeout(logger.WithContext(context.Background()), time.Duration(*srsTimeout)*time.Millisecond)
	defer cancel()

	// Check a set of errors.
	var r0, r1, r2, r3, r4, r5, r6, r7, r8 error
	defer func(ctx context.Context) {
		if err := filterTestError(ctx.Err(), r0, r1, r2, r3, r4, r5, r6, r7, r8); err != nil {
			t.Errorf("Fail for err %+v", err)
		} else {
			logger.Tf(ctx, "test done with err %+v", err)
		}
	}(ctx)

	streamID := fmt.Sprintf("stream-%v-%v", os.Getpid(), rand.Int())

	// Two destination SRS, and a backend that forwards to both.
	dst1, dst2 := NewSRSServer(), NewSRSServer()
	backend := newForwardBackend(ctx, func(r *forwardHookRequest) (int, string) {
		return http.StatusOK, forwardBackendAnswer(
			fmt.Sprintf("rtmp://127.0.0.1:%v/live/%v", dst1.RTMPPort(), streamID),
			fmt.Sprintf("rtmp://127.0.0.1:%v/live/%v", dst2.RTMPPort(), streamID),
		)
	})
	defer backend.Close()

	var wg sync.WaitGroup
	defer wg.Wait()

	wg.Add(1)
	go func() {
		defer wg.Done()
		r0 = dst1.Run(ctx, cancel)
	}()
	wg.Add(1)
	go func() {
		defer wg.Done()
		r1 = dst2.Run(ctx, cancel)
	}()

	origin := NewSRSServer(func(v *srsServer) {
		v.envs = []string{
			"SRS_VHOST_FORWARD_ENABLED=on",
			fmt.Sprintf("SRS_VHOST_FORWARD_BACKEND=%v", backend.ForwardURL()),
		}
	})
	wg.Add(1)
	go func() {
		defer wg.Done()
		r2 = origin.Run(ctx, cancel)
	}()

	allReady := func() {
		<-dst1.ReadyCtx().Done()
		<-dst2.ReadyCtx().Done()
		<-origin.ReadyCtx().Done()
	}

	// Start FFmpeg to publish stream to the origin.
	streamURL := fmt.Sprintf("rtmp://localhost:%v/live/%v", origin.RTMPPort(), streamID)
	ffmpeg := NewFFmpeg(func(v *ffmpegClient) {
		v.args = []string{
			"-stream_loop", "-1", "-re", "-i", *srsPublishAvatar, "-c", "copy", "-f", "flv", streamURL,
		}
	})
	wg.Add(1)
	go func() {
		defer wg.Done()
		allReady()
		r3 = ffmpeg.Run(ctx, cancel)
	}()

	// Probe the forwarded stream on each destination.
	duration := time.Duration(*srsFFprobeDuration) * time.Millisecond
	newProbe := func(dst SRSServer) FFprobeClient {
		return NewFFprobe(func(v *ffprobeClient) {
			v.dvrFile = path.Join(dst.WorkDir(), "objs", fmt.Sprintf("srs-ffprobe-%v.flv", streamID))
			v.streamURL = fmt.Sprintf("rtmp://localhost:%v/live/%v", dst.RTMPPort(), streamID)
			v.duration, v.timeout = duration, time.Duration(*srsFFprobeTimeout)*time.Millisecond
		})
	}
	ffprobe1, ffprobe2 := newProbe(dst1), newProbe(dst2)
	wg.Add(1)
	go func() {
		defer wg.Done()
		allReady()
		r4 = ffprobe1.Run(ctx, cancel)
	}()
	wg.Add(1)
	go func() {
		defer wg.Done()
		allReady()
		r5 = ffprobe2.Run(ctx, cancel)
	}()

	select {
	case <-ctx.Done():
		return
	case <-ffprobe1.ProbeDoneCtx().Done():
	}
	select {
	case <-ctx.Done():
	case <-ffprobe2.ProbeDoneCtx().Done():
		defer cancel()

		r6 = forwardCheckProbe(ffprobe1, duration)
		r7 = forwardCheckProbe(ffprobe2, duration)
		r8 = forwardCheckOnForward(backend, streamID)
	}
}

// The backend returns no url: the origin stream plays, and nothing is forwarded.
func TestFast_RtmpPublish_Forward_Backend_NoUrls(t *testing.T) {
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

	streamID := fmt.Sprintf("stream-%v-%v", os.Getpid(), rand.Int())

	// A destination SRS that nothing should reach, and a backend that returns no url.
	dst := NewSRSServer()
	backend := newForwardBackend(ctx, func(r *forwardHookRequest) (int, string) {
		return http.StatusOK, forwardBackendAnswer()
	})
	defer backend.Close()

	var wg sync.WaitGroup
	defer wg.Wait()

	wg.Add(1)
	go func() {
		defer wg.Done()
		r0 = dst.Run(ctx, cancel)
	}()

	origin := NewSRSServer(func(v *srsServer) {
		v.envs = []string{
			"SRS_VHOST_FORWARD_ENABLED=on",
			fmt.Sprintf("SRS_VHOST_FORWARD_BACKEND=%v", backend.ForwardURL()),
		}
	})
	wg.Add(1)
	go func() {
		defer wg.Done()
		r1 = origin.Run(ctx, cancel)
	}()

	// Start FFmpeg to publish stream to the origin.
	streamURL := fmt.Sprintf("rtmp://localhost:%v/live/%v", origin.RTMPPort(), streamID)
	ffmpeg := NewFFmpeg(func(v *ffmpegClient) {
		v.args = []string{
			"-stream_loop", "-1", "-re", "-i", *srsPublishAvatar, "-c", "copy", "-f", "flv", streamURL,
		}
	})
	wg.Add(1)
	go func() {
		defer wg.Done()
		<-dst.ReadyCtx().Done()
		<-origin.ReadyCtx().Done()
		r2 = ffmpeg.Run(ctx, cancel)
	}()

	// Probe the stream on the origin.
	duration := time.Duration(*srsFFprobeDuration) * time.Millisecond
	ffprobe := NewFFprobe(func(v *ffprobeClient) {
		v.dvrFile = path.Join(origin.WorkDir(), "objs", fmt.Sprintf("srs-ffprobe-%v.flv", streamID))
		v.streamURL, v.duration, v.timeout = streamURL, duration, time.Duration(*srsFFprobeTimeout)*time.Millisecond
	})
	wg.Add(1)
	go func() {
		defer wg.Done()
		<-dst.ReadyCtx().Done()
		<-origin.ReadyCtx().Done()
		r3 = ffprobe.Run(ctx, cancel)
	}()

	select {
	case <-ctx.Done():
	case <-ffprobe.ProbeDoneCtx().Done():
		defer cancel()

		r4 = forwardCheckProbe(ffprobe, duration)
		r5 = forwardCheckOnForward(backend, streamID)

		// After the whole probe, the destination still has no stream.
		if streams, err := forwardActiveStreams(dst.APIPort()); err != nil {
			r6 = err
		} else if len(streams) != 0 {
			r6 = errors.Errorf("destination has streams %+v, expect none", streams)
		}
	}
}

// The backend answers HTTP 500: the origin rejects the publish and closes the connection, so the publisher quits and
// no stream is active.
func TestFast_RtmpPublish_Forward_Backend_Error(t *testing.T) {
	// This case is run in parallel.
	t.Parallel()

	// Setup the max timeout for this case.
	ctx, cancel := context.WithTimeout(logger.WithContext(context.Background()), time.Duration(*srsTimeout)*time.Millisecond)
	defer cancel()

	// Check a set of errors.
	var r0, r1, r2, r3, r4 error
	defer func(ctx context.Context) {
		if err := filterTestError(ctx.Err(), r0, r1, r2, r3, r4); err != nil {
			t.Errorf("Fail for err %+v", err)
		} else {
			logger.Tf(ctx, "test done with err %+v", err)
		}
	}(ctx)

	streamID := fmt.Sprintf("stream-%v-%v", os.Getpid(), rand.Int())

	backend := newForwardBackend(ctx, func(r *forwardHookRequest) (int, string) {
		return http.StatusInternalServerError, `{"code":500}`
	})
	defer backend.Close()

	var wg sync.WaitGroup
	defer wg.Wait()

	origin := NewSRSServer(func(v *srsServer) {
		v.envs = []string{
			"SRS_VHOST_FORWARD_ENABLED=on",
			fmt.Sprintf("SRS_VHOST_FORWARD_BACKEND=%v", backend.ForwardURL()),
		}
	})
	wg.Add(1)
	go func() {
		defer wg.Done()
		r0 = origin.Run(ctx, cancel)
	}()

	// Publish to the origin. If the publish were accepted, the looped input would run until the cap.
	streamURL := fmt.Sprintf("rtmp://localhost:%v/live/%v", origin.RTMPPort(), streamID)
	ffmpeg := NewFFmpeg(func(v *ffmpegClient) {
		v.cancelCaseWhenQuit, v.ffmpegDuration = false, time.Duration(*srsTimeout)*time.Millisecond
		v.args = []string{
			"-stream_loop", "-1", "-re", "-i", *srsPublishAvatar, "-c", "copy", "-f", "flv", streamURL,
		}
	})
	ffmpegDone, ffmpegDoneCancel := context.WithCancel(ctx)
	wg.Add(1)
	go func() {
		defer wg.Done()
		defer ffmpegDoneCancel()
		<-origin.ReadyCtx().Done()
		r1 = ffmpeg.Run(ctx, cancel)
	}()

	// The rejected publisher quits soon.
	quitTimeout := 15 * time.Second
	select {
	case <-ctx.Done():
	case <-time.After(quitTimeout):
		defer cancel()
		r2 = errors.Errorf("publisher still runs after %v, expect rejected", quitTimeout)
	case <-ffmpegDone.Done():
		defer cancel()

		r3 = forwardCheckOnForward(backend, streamID)

		if streams, err := forwardActiveStreams(origin.APIPort()); err != nil {
			r4 = err
		} else if len(streams) != 0 {
			r4 = errors.Errorf("origin has streams %+v, expect none", streams)
		}
	}
}

// forwardDestinationEnvs are the origin envs that forward every stream to the destinations, by forward.destination.
func forwardDestinationEnvs(dsts ...SRSServer) []string {
	var eps []string
	for _, dst := range dsts {
		eps = append(eps, fmt.Sprintf("127.0.0.1:%v", dst.RTMPPort()))
	}
	return []string{
		"SRS_VHOST_FORWARD_ENABLED=on",
		fmt.Sprintf("SRS_VHOST_FORWARD_DESTINATION=%v", strings.Join(eps, " ")),
	}
}

// One forward.destination: the destination gets the stream with the same app and stream.
func TestFast_RtmpPublish_Forward_Destination_Basic(t *testing.T) {
	// This case is run in parallel.
	t.Parallel()

	// Setup the max timeout for this case.
	ctx, cancel := context.WithTimeout(logger.WithContext(context.Background()), time.Duration(*srsTimeout)*time.Millisecond)
	defer cancel()

	// Check a set of errors.
	var r0, r1, r2, r3, r4 error
	defer func(ctx context.Context) {
		if err := filterTestError(ctx.Err(), r0, r1, r2, r3, r4); err != nil {
			t.Errorf("Fail for err %+v", err)
		} else {
			logger.Tf(ctx, "test done with err %+v", err)
		}
	}(ctx)

	streamID := fmt.Sprintf("stream-%v-%v", os.Getpid(), rand.Int())

	var wg sync.WaitGroup
	defer wg.Wait()

	dst := NewSRSServer()
	wg.Add(1)
	go func() {
		defer wg.Done()
		r0 = dst.Run(ctx, cancel)
	}()

	origin := NewSRSServer(func(v *srsServer) {
		v.envs = forwardDestinationEnvs(dst)
	})
	wg.Add(1)
	go func() {
		defer wg.Done()
		r1 = origin.Run(ctx, cancel)
	}()

	// Start FFmpeg to publish stream to the origin.
	streamURL := fmt.Sprintf("rtmp://localhost:%v/live/%v", origin.RTMPPort(), streamID)
	ffmpeg := NewFFmpeg(func(v *ffmpegClient) {
		v.args = []string{
			"-stream_loop", "-1", "-re", "-i", *srsPublishAvatar, "-c", "copy", "-f", "flv", streamURL,
		}
	})
	wg.Add(1)
	go func() {
		defer wg.Done()
		<-dst.ReadyCtx().Done()
		<-origin.ReadyCtx().Done()
		r2 = ffmpeg.Run(ctx, cancel)
	}()

	// Probe the forwarded stream on the destination, at the same app and stream.
	duration := time.Duration(*srsFFprobeDuration) * time.Millisecond
	ffprobe := NewFFprobe(func(v *ffprobeClient) {
		v.dvrFile = path.Join(dst.WorkDir(), "objs", fmt.Sprintf("srs-ffprobe-%v.flv", streamID))
		v.streamURL = fmt.Sprintf("rtmp://localhost:%v/live/%v", dst.RTMPPort(), streamID)
		v.duration, v.timeout = duration, time.Duration(*srsFFprobeTimeout)*time.Millisecond
	})
	wg.Add(1)
	go func() {
		defer wg.Done()
		<-dst.ReadyCtx().Done()
		<-origin.ReadyCtx().Done()
		r3 = ffprobe.Run(ctx, cancel)
	}()

	select {
	case <-ctx.Done():
	case <-ffprobe.ProbeDoneCtx().Done():
		defer cancel()
		r4 = forwardCheckProbe(ffprobe, duration)
	}
}

// Two forward.destination entries: both destinations get the stream.
func TestFast_RtmpPublish_Forward_Destination_Two(t *testing.T) {
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

	streamID := fmt.Sprintf("stream-%v-%v", os.Getpid(), rand.Int())

	var wg sync.WaitGroup
	defer wg.Wait()

	dst1, dst2 := NewSRSServer(), NewSRSServer()
	wg.Add(1)
	go func() {
		defer wg.Done()
		r0 = dst1.Run(ctx, cancel)
	}()
	wg.Add(1)
	go func() {
		defer wg.Done()
		r1 = dst2.Run(ctx, cancel)
	}()

	origin := NewSRSServer(func(v *srsServer) {
		v.envs = forwardDestinationEnvs(dst1, dst2)
	})
	wg.Add(1)
	go func() {
		defer wg.Done()
		r2 = origin.Run(ctx, cancel)
	}()

	allReady := func() {
		<-dst1.ReadyCtx().Done()
		<-dst2.ReadyCtx().Done()
		<-origin.ReadyCtx().Done()
	}

	// Start FFmpeg to publish stream to the origin.
	streamURL := fmt.Sprintf("rtmp://localhost:%v/live/%v", origin.RTMPPort(), streamID)
	ffmpeg := NewFFmpeg(func(v *ffmpegClient) {
		v.args = []string{
			"-stream_loop", "-1", "-re", "-i", *srsPublishAvatar, "-c", "copy", "-f", "flv", streamURL,
		}
	})
	wg.Add(1)
	go func() {
		defer wg.Done()
		allReady()
		r3 = ffmpeg.Run(ctx, cancel)
	}()

	// Probe the forwarded stream on each destination.
	duration := time.Duration(*srsFFprobeDuration) * time.Millisecond
	newProbe := func(dst SRSServer) FFprobeClient {
		return NewFFprobe(func(v *ffprobeClient) {
			v.dvrFile = path.Join(dst.WorkDir(), "objs", fmt.Sprintf("srs-ffprobe-%v.flv", streamID))
			v.streamURL = fmt.Sprintf("rtmp://localhost:%v/live/%v", dst.RTMPPort(), streamID)
			v.duration, v.timeout = duration, time.Duration(*srsFFprobeTimeout)*time.Millisecond
		})
	}
	ffprobe1, ffprobe2 := newProbe(dst1), newProbe(dst2)
	wg.Add(1)
	go func() {
		defer wg.Done()
		allReady()
		r4 = ffprobe1.Run(ctx, cancel)
	}()
	wg.Add(1)
	go func() {
		defer wg.Done()
		allReady()
		r5 = ffprobe2.Run(ctx, cancel)
	}()

	select {
	case <-ctx.Done():
		return
	case <-ffprobe1.ProbeDoneCtx().Done():
	}
	select {
	case <-ctx.Done():
	case <-ffprobe2.ProbeDoneCtx().Done():
		defer cancel()

		r6 = forwardCheckProbe(ffprobe1, duration)
		r7 = forwardCheckProbe(ffprobe2, duration)
	}
}

// The publisher's query reaches the destination: its on_publish has the same app, stream and token.
func TestFast_RtmpPublish_Forward_Destination_WithQuery(t *testing.T) {
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

	streamID := fmt.Sprintf("stream-%v-%v", os.Getpid(), rand.Int())

	// Only the on_publish hook of the destination is used; nothing asks for a forward.
	hooks := newForwardBackend(ctx, func(r *forwardHookRequest) (int, string) {
		return http.StatusNotFound, `{"code":404}`
	})
	defer hooks.Close()

	var wg sync.WaitGroup
	defer wg.Wait()

	dst := NewSRSServer(func(v *srsServer) {
		v.envs = []string{
			"SRS_VHOST_HTTP_HOOKS_ENABLED=on",
			fmt.Sprintf("SRS_VHOST_HTTP_HOOKS_ON_PUBLISH=%v", hooks.PublishURL()),
		}
	})
	wg.Add(1)
	go func() {
		defer wg.Done()
		r0 = dst.Run(ctx, cancel)
	}()

	origin := NewSRSServer(func(v *srsServer) {
		v.envs = forwardDestinationEnvs(dst)
	})
	wg.Add(1)
	go func() {
		defer wg.Done()
		r1 = origin.Run(ctx, cancel)
	}()

	// Start FFmpeg to publish stream with a token to the origin.
	streamURL := fmt.Sprintf("rtmp://localhost:%v/live/%v?token=abc", origin.RTMPPort(), streamID)
	ffmpeg := NewFFmpeg(func(v *ffmpegClient) {
		v.args = []string{
			"-stream_loop", "-1", "-re", "-i", *srsPublishAvatar, "-c", "copy", "-f", "flv", streamURL,
		}
	})
	wg.Add(1)
	go func() {
		defer wg.Done()
		<-dst.ReadyCtx().Done()
		<-origin.ReadyCtx().Done()
		r2 = ffmpeg.Run(ctx, cancel)
	}()

	// Probe the forwarded stream on the destination.
	duration := time.Duration(*srsFFprobeDuration) * time.Millisecond
	ffprobe := NewFFprobe(func(v *ffprobeClient) {
		v.dvrFile = path.Join(dst.WorkDir(), "objs", fmt.Sprintf("srs-ffprobe-%v.flv", streamID))
		v.streamURL = fmt.Sprintf("rtmp://localhost:%v/live/%v", dst.RTMPPort(), streamID)
		v.duration, v.timeout = duration, time.Duration(*srsFFprobeTimeout)*time.Millisecond
	})
	wg.Add(1)
	go func() {
		defer wg.Done()
		<-dst.ReadyCtx().Done()
		<-origin.ReadyCtx().Done()
		r3 = ffprobe.Run(ctx, cancel)
	}()

	select {
	case <-ctx.Done():
	case <-ffprobe.ProbeDoneCtx().Done():
		defer cancel()

		r4 = forwardCheckProbe(ffprobe, duration)

		// The forwarder publishes with the publisher's app, stream and query.
		pubs := hooks.Publishes()
		if len(pubs) != 1 {
			r5 = errors.Errorf("on_publish count=%v, expect=1, %+v", len(pubs), pubs)
		} else if p := pubs[0]; p.App != "live" || p.Stream != streamID || p.Param != "?token=abc" {
			r5 = errors.Errorf("on_publish app=%v, stream=%v, param=%v, expect live, %v, ?token=abc",
				p.App, p.Stream, p.Param, streamID)
		}
	}
}

// forward.enabled off with a destination set: the origin stream plays, and nothing is forwarded.
func TestFast_RtmpPublish_Forward_Disabled(t *testing.T) {
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

	streamID := fmt.Sprintf("stream-%v-%v", os.Getpid(), rand.Int())

	var wg sync.WaitGroup
	defer wg.Wait()

	// A destination SRS that nothing should reach.
	dst := NewSRSServer()
	wg.Add(1)
	go func() {
		defer wg.Done()
		r0 = dst.Run(ctx, cancel)
	}()

	origin := NewSRSServer(func(v *srsServer) {
		v.envs = []string{
			"SRS_VHOST_FORWARD_ENABLED=off",
			fmt.Sprintf("SRS_VHOST_FORWARD_DESTINATION=127.0.0.1:%v", dst.RTMPPort()),
		}
	})
	wg.Add(1)
	go func() {
		defer wg.Done()
		r1 = origin.Run(ctx, cancel)
	}()

	// Start FFmpeg to publish stream to the origin.
	streamURL := fmt.Sprintf("rtmp://localhost:%v/live/%v", origin.RTMPPort(), streamID)
	ffmpeg := NewFFmpeg(func(v *ffmpegClient) {
		v.args = []string{
			"-stream_loop", "-1", "-re", "-i", *srsPublishAvatar, "-c", "copy", "-f", "flv", streamURL,
		}
	})
	wg.Add(1)
	go func() {
		defer wg.Done()
		<-dst.ReadyCtx().Done()
		<-origin.ReadyCtx().Done()
		r2 = ffmpeg.Run(ctx, cancel)
	}()

	// Probe the stream on the origin.
	duration := time.Duration(*srsFFprobeDuration) * time.Millisecond
	ffprobe := NewFFprobe(func(v *ffprobeClient) {
		v.dvrFile = path.Join(origin.WorkDir(), "objs", fmt.Sprintf("srs-ffprobe-%v.flv", streamID))
		v.streamURL, v.duration, v.timeout = streamURL, duration, time.Duration(*srsFFprobeTimeout)*time.Millisecond
	})
	wg.Add(1)
	go func() {
		defer wg.Done()
		<-dst.ReadyCtx().Done()
		<-origin.ReadyCtx().Done()
		r3 = ffprobe.Run(ctx, cancel)
	}()

	select {
	case <-ctx.Done():
	case <-ffprobe.ProbeDoneCtx().Done():
		defer cancel()

		r4 = forwardCheckProbe(ffprobe, duration)

		// After the whole probe, the destination still has no stream.
		if streams, err := forwardActiveStreams(dst.APIPort()); err != nil {
			r5 = err
		} else if len(streams) != 0 {
			r5 = errors.Errorf("destination has streams %+v, expect none", streams)
		}
	}
}
