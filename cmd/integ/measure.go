// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT
package main

import (
	"context"
	"errors"
	"flag"
	"fmt"
	"io"
	"net/http"
	"os"
	"os/signal"
	"slices"
	"strconv"
	"strings"
	"sync"
	"syscall"
	"time"

	"srs-integ/internal/rtc"
	"srs-integ/internal/rtmp"
)

const measureUsage = `Usage: srs-integ measure [flags] <api-url>

Measure how long WebRTC sessions take to set up, with and without SPED
(draft-hancke-webrtc-sped), against an SRS with vhost.rtc.sped on, at its
HTTP API, such as http://127.0.0.1:1985. It publishes -file by WHIP, then,
for each point of the -delays, -losses and -sped matrix, connects -runs
players by WHEP, -parallel at once. Each player goes over a path with that
one-way delay, its signaling too, that loss of its STUN and DTLS each way,
and offers sped or not. It is timed from its offer to the end of its DTLS
handshake and to its first RTP packet. One row per point gives the p50 and
p95 of both, in ms, and how many runs failed.

`

// A measurePoint is a point of the matrix: the one-way delay, the loss of
// STUN and DTLS each way, from 0 to 1, and whether the player offers sped.
type measurePoint struct {
	delay time.Duration
	loss  float64
	sped  bool
}

// A measureSample is how long one run took, from its offer, to its DTLS
// handshake done and to its first RTP packet.
type measureSample struct {
	dtls, media time.Duration
}

// A measureResult is the runs of a point: the samples of those that
// succeeded, how many failed, and the first failure.
type measureResult struct {
	point   measurePoint
	samples []measureSample
	fails   int
	err     error
}

// measureHeader names the columns of measureResult.row.
var measureHeader = fmt.Sprintf(measureColumns, "delay_ms", "loss_%", "sped", "runs", "fails", "dtls_p50", "dtls_p95", "media_p50", "media_p95")

const measureColumns = "%8s %6s %4s %4s %5s %8s %8s %9s %9s"

// measure runs the measure command and returns its exit code.
func measure(args []string, stdout, stderr io.Writer) int {
	fs := flag.NewFlagSet("measure", flag.ContinueOnError)
	fs.SetOutput(io.Discard)
	file := fs.String("file", "", "the MP4 file with H.264 and Opus to publish, such as cmd/integ/testdata/media/h264-baseline-opus.mp4")
	stream := fs.String("stream", "sped-measure", "the stream to publish and play, in the app live")
	delays := fs.String("delays", "0ms,20ms,60ms,150ms", "the one-way delays, comma-separated")
	losses := fs.String("losses", "0,10,25", "the losses of STUN and DTLS each way, in percent, comma-separated")
	modes := fs.String("sped", "off,on", "whether the players offer sped: off, on, or both")
	runs := fs.Int("runs", 30, "the runs of each point")
	parallel := fs.Int("parallel", 5, "the runs of a point at once")
	timeout := fs.Duration("timeout", 30*time.Second, "the longest a run may take before it fails")
	usage := func(w io.Writer) {
		fmt.Fprint(w, measureUsage)
		fs.SetOutput(w)
		fs.PrintDefaults()
	}
	refuse := func(err error) int {
		fmt.Fprintln(stderr, err)
		usage(stderr)
		return 2
	}
	if err := fs.Parse(args); err != nil {
		if err == flag.ErrHelp {
			usage(stdout)
			return 0
		}
		return refuse(err)
	}
	if fs.NArg() != 1 {
		usage(stderr)
		return 2
	}
	if *file == "" {
		return refuse(errors.New("-file is required"))
	}
	points, err := measureMatrix(*delays, *losses, *modes)
	if err != nil {
		return refuse(err)
	}
	if *runs < 1 || *parallel < 1 {
		return refuse(fmt.Errorf("-runs %v and -parallel %v must be at least 1", *runs, *parallel))
	}
	samples, err := rtc.ReadMP4(*file)
	if err != nil {
		return fail(stderr, fmt.Errorf("measure: %w", err))
	}
	if len(samples) == 0 {
		return fail(stderr, fmt.Errorf("measure: %v has no samples", *file))
	}

	ctx, stop := signal.NotifyContext(context.Background(), os.Interrupt, syscall.SIGTERM)
	defer stop()
	api := strings.TrimSuffix(fs.Arg(0), "/")
	whip := fmt.Sprintf("%v/rtc/v1/whip/?app=live&stream=%v", api, *stream)
	whep := fmt.Sprintf("%v/rtc/v1/whep/?app=live&stream=%v", api, *stream)

	pctx, cancel := context.WithTimeout(ctx, *timeout)
	pub, err := rtc.RawPublish(pctx, whip, &rtc.Options{SRTP: true}, true)
	cancel()
	if err != nil {
		return fail(stderr, fmt.Errorf("measure: publish %v: %w", whip, err))
	}
	defer pub.Close()
	sending, stopSending := context.WithCancel(ctx)
	sent := make(chan error, 1)
	go func() { sent <- sendLooped(sending, pub, samples) }()
	defer func() {
		stopSending()
		<-sent
	}()

	fmt.Fprintf(stdout, "measure %v: %v points, %v runs each, %v at once\n", api, len(points), *runs, *parallel)
	fmt.Fprintln(stdout, measureHeader)
	for _, p := range points {
		r := measurePointRuns(ctx, p, *runs, *parallel, func(ctx context.Context, p measurePoint) (measureSample, error) {
			ctx, cancel := context.WithTimeout(ctx, *timeout)
			defer cancel()
			return measureRun(ctx, whep, p)
		})
		fmt.Fprintln(stdout, r.row())
		if r.err != nil {
			fmt.Fprintf(stderr, "measure: %v: first failure: %v\n", r.point, r.err)
		}
		select {
		case err := <-sent:
			return fail(stderr, fmt.Errorf("measure: the publisher stopped: %w", err))
		default:
		}
		if ctx.Err() != nil {
			return fail(stderr, fmt.Errorf("measure: %w", ctx.Err()))
		}
	}
	return 0
}

func (p measurePoint) String() string {
	return fmt.Sprintf("delay %v, loss %v%%, sped %v", p.delay, p.loss*100, onOff(p.sped))
}

func onOff(b bool) string {
	if b {
		return "on"
	}
	return "off"
}

// sendLooped sends the samples in real time again and again, each time
// after the last, until ctx ends or a write fails.
func sendLooped(ctx context.Context, pub *rtc.RawPublisher, samples []rtc.Sample) error {
	// The next loop starts one frame after the last sample.
	length := samples[len(samples)-1].DTS + 40*time.Millisecond
	pacer := &rtmp.Pacer{}
	for offset := time.Duration(0); ; offset += length {
		for _, s := range samples {
			s.DTS += offset
			if err := pacer.Wait(ctx, uint32(s.DTS/time.Millisecond)); err != nil {
				return err
			}
			if err := pub.Write(s); err != nil {
				return err
			}
		}
	}
}

// measureRun connects a player for a point and times its setup: the faults
// are on its socket, and its signaling is delayed as much.
func measureRun(ctx context.Context, whep string, p measurePoint) (measureSample, error) {
	opts := &rtc.Options{
		SRTP:   true,
		SPED:   p.sped,
		Faults: &rtc.Faults{Delay: p.delay, Loss: p.loss},
		Client: &http.Client{Transport: &delayTransport{base: http.DefaultTransport, delay: p.delay}},
	}
	player, err := rtc.RawPlay(ctx, whep, opts, true)
	if err != nil {
		return measureSample{}, err
	}
	defer player.Close()
	if st := player.SPED().State; p.sped && (st == rtc.SpedNone || st == rtc.SpedOff) {
		return measureSample{}, fmt.Errorf("SPED is %v, want it on: is vhost.rtc.sped on?", st)
	}
	media, err := player.FirstPacket(ctx)
	if err != nil {
		return measureSample{}, fmt.Errorf("first RTP packet: %w", err)
	}
	return measureSample{dtls: player.Setup().DTLS, media: media}, nil
}

// measureMatrix is the points of comma-separated one-way delays, such as
// 0ms,20ms, losses in percent, such as 0,10, and sped modes, off and on:
// each loss and mode at each delay, off before on.
func measureMatrix(delays, losses, modes string) ([]measurePoint, error) {
	var ds []time.Duration
	for _, v := range strings.Split(delays, ",") {
		d, err := time.ParseDuration(strings.TrimSpace(v))
		if err != nil || d < 0 {
			return nil, fmt.Errorf("-delays %q: %q is not a delay such as 20ms", delays, v)
		}
		ds = append(ds, d)
	}
	var ls []float64
	for _, v := range strings.Split(losses, ",") {
		l, err := strconv.ParseFloat(strings.TrimSpace(v), 64)
		if err != nil || l < 0 || l > 100 {
			return nil, fmt.Errorf("-losses %q: %q is not a percent from 0 to 100", losses, v)
		}
		ls = append(ls, l/100)
	}
	var on, off bool
	for _, v := range strings.Split(modes, ",") {
		switch strings.TrimSpace(v) {
		case "off":
			if off {
				return nil, fmt.Errorf("-sped %q has off twice", modes)
			}
			off = true
		case "on":
			if on {
				return nil, fmt.Errorf("-sped %q has on twice", modes)
			}
			on = true
		default:
			return nil, fmt.Errorf("-sped %q: %q is neither off nor on", modes, v)
		}
	}
	var points []measurePoint
	for _, d := range ds {
		for _, l := range ls {
			if off {
				points = append(points, measurePoint{d, l, false})
			}
			if on {
				points = append(points, measurePoint{d, l, true})
			}
		}
	}
	return points, nil
}

// measurePointRuns does runs runs of run for a point, parallel at once,
// until they end or ctx ends.
func measurePointRuns(ctx context.Context, p measurePoint, runs, parallel int,
	run func(context.Context, measurePoint) (measureSample, error)) measureResult {
	r := measureResult{point: p}
	var mu sync.Mutex
	var wg sync.WaitGroup
	slots := make(chan struct{}, parallel)
	for range runs {
		slots <- struct{}{}
		wg.Add(1)
		go func() {
			defer wg.Done()
			defer func() { <-slots }()
			var s measureSample
			err := ctx.Err()
			if err == nil {
				s, err = run(ctx, p)
			}
			mu.Lock()
			defer mu.Unlock()
			if err != nil {
				r.fails++
				if r.err == nil {
					r.err = err
				}
				return
			}
			r.samples = append(r.samples, s)
		}()
	}
	wg.Wait()
	return r
}

// percentile is the p-th percentile of d by the nearest rank, 0 when d is
// empty.
func percentile(d []time.Duration, p int) time.Duration {
	if len(d) == 0 {
		return 0
	}
	sorted := slices.Clone(d)
	slices.Sort(sorted)
	rank := (p*len(sorted) + 99) / 100
	return sorted[max(rank, 1)-1]
}

// row is the line of the result, in the columns of measureHeader.
func (r measureResult) row() string {
	var dtls, media []time.Duration
	for _, s := range r.samples {
		dtls = append(dtls, s.dtls)
		media = append(media, s.media)
	}
	ms := func(d []time.Duration, p int) string {
		if len(d) == 0 {
			return "-"
		}
		return strconv.FormatInt(percentile(d, p).Milliseconds(), 10)
	}
	return fmt.Sprintf(measureColumns, strconv.FormatInt(r.point.delay.Milliseconds(), 10),
		strconv.FormatFloat(r.point.loss*100, 'f', -1, 64), onOff(r.point.sped),
		strconv.Itoa(len(r.samples)+r.fails), strconv.Itoa(r.fails),
		ms(dtls, 50), ms(dtls, 95), ms(media, 50), ms(media, 95))
}

// A delayTransport delays each HTTP request by a one-way delay, and its
// response by another.
type delayTransport struct {
	base  http.RoundTripper
	delay time.Duration
}

func (t *delayTransport) RoundTrip(req *http.Request) (*http.Response, error) {
	if err := sleepCtx(req.Context(), t.delay); err != nil {
		return nil, err
	}
	res, err := t.base.RoundTrip(req)
	if err != nil {
		return nil, err
	}
	if err := sleepCtx(req.Context(), t.delay); err != nil {
		res.Body.Close()
		return nil, err
	}
	return res, nil
}

// sleepCtx waits for d, or until ctx ends.
func sleepCtx(ctx context.Context, d time.Duration) error {
	t := time.NewTimer(d)
	defer t.Stop()
	select {
	case <-t.C:
		return nil
	case <-ctx.Done():
		return ctx.Err()
	}
}
