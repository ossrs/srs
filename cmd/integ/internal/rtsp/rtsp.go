// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

// Package rtsp plays RTSP streams with gortsplib and analyzes them into the
// stream report, and sends raw RTSP requests for the error cases.
package rtsp

import (
	"context"
	"errors"
	"fmt"
	"sync"
	"time"

	"github.com/bluenviron/gortsplib/v5"
	"github.com/bluenviron/gortsplib/v5/pkg/base"
	"github.com/bluenviron/gortsplib/v5/pkg/description"
	"github.com/bluenviron/gortsplib/v5/pkg/liberrors"
	"github.com/pion/rtp"

	"srs-integ/internal/report"
)

// Options are a player's settings; nil is the defaults.
type Options struct {
	// TryUDP lets the player ask for UDP first and fall back to TCP when the
	// server refuses it, as most clients do; by default it asks for TCP only.
	TryUDP bool
	// Timeout bounds each request and each wait for a packet, 1s by default.
	Timeout time.Duration
}

// A StatusError is an answer with a status other than 200 to a request.
type StatusError struct {
	Method string
	Code   int
	// Message is the status text.
	Message string
}

func (e *StatusError) Error() string {
	return fmt.Sprintf("%v: status %v %v", e.Method, e.Code, e.Message)
}

// statusError returns err as a StatusError of the method when it is a bad
// status, and err otherwise.
func statusError(method string, err error) error {
	var bad liberrors.ErrClientBadStatusCode
	if errors.As(err, &bad) {
		return &StatusError{Method: method, Code: int(bad.Code), Message: bad.Message}
	}
	return fmt.Errorf("%v: %w", method, err)
}

// A Player plays a stream over RTSP and analyzes what it gets.
type Player struct {
	// Desc is the session the DESCRIBE answer described.
	Desc *description.Session
	// Describe is the DESCRIBE answer.
	Describe *base.Response
	// Tracks are the tracks the player set up, one for each media.
	Tracks []*Track

	client *gortsplib.Client
	// done is closed when the connection ends, with doneErr.
	done    chan struct{}
	doneErr error

	mu       sync.Mutex
	analyzer Analyzer
	err      error
	changed  chan struct{}
	packets  int
}

// Play sends DESCRIBE, SETUP for every media and PLAY to url, then analyzes
// the packets until Close. Each media must have a codec the analyzer reads.
func Play(ctx context.Context, url string, opts *Options) (*Player, error) {
	if opts == nil {
		opts = &Options{}
	}
	timeout := opts.Timeout
	if timeout == 0 {
		timeout = time.Second
	}
	u, err := base.ParseURL(url)
	if err != nil {
		return nil, err
	}
	p := &Player{changed: make(chan struct{}), done: make(chan struct{})}
	p.client = &gortsplib.Client{
		Scheme:            u.Scheme,
		Host:              u.Host,
		ReadTimeout:       timeout,
		WriteTimeout:      timeout,
		OnTransportSwitch: func(error) {},
		OnPacketsLost:     func(uint64) {},
		OnDecodeError:     func(error) {},
	}
	if !opts.TryUDP {
		tcp := gortsplib.ProtocolTCP
		p.client.Protocol = &tcp
	}
	if err := p.client.Start(); err != nil {
		return nil, err
	}
	// A request waits for its answer up to the timeout; ctx ends it sooner.
	stop := context.AfterFunc(ctx, p.client.Close)
	defer stop()
	if err := p.start(u); err != nil {
		p.client.Close()
		if ctx.Err() != nil {
			return nil, fmt.Errorf("%w: %v", ctx.Err(), err)
		}
		return nil, err
	}
	go func() {
		p.doneErr = p.client.Wait()
		close(p.done)
	}()
	return p, nil
}

func (p *Player) start(u *base.URL) error {
	desc, res, err := p.client.Describe(u)
	if err != nil {
		return statusError("DESCRIBE", err)
	}
	p.Desc, p.Describe = desc, res
	for _, m := range desc.Medias {
		t, err := TrackOf(m, m.Formats[0])
		if err != nil {
			return fmt.Errorf("%v media: %w", m.Type, err)
		}
		p.Tracks = append(p.Tracks, t)
		if _, err := p.client.Setup(desc.BaseURL, m, 0, 0); err != nil {
			return statusError("SETUP", err)
		}
	}
	for _, t := range p.Tracks {
		t := t
		p.client.OnPacketRTP(t.Media, t.Format, func(pkt *rtp.Packet) { p.add(t, pkt) })
	}
	if _, err := p.client.Play(nil); err != nil {
		return statusError("PLAY", err)
	}
	return nil
}

// add analyzes a packet; the first error stops the analysis.
func (p *Player) add(t *Track, pkt *rtp.Packet) {
	p.mu.Lock()
	defer p.mu.Unlock()
	p.packets++
	if p.err == nil {
		p.err = p.analyzer.Add(t, pkt)
	}
	close(p.changed)
	p.changed = make(chan struct{})
}

// TCP is whether the player gets its media over the RTSP connection.
func (p *Player) TCP() bool {
	tr := p.client.Transport()
	return tr.Session != nil && tr.Session.Protocol == gortsplib.ProtocolTCP
}

// Packets is how many RTP packets the player got.
func (p *Player) Packets() int {
	p.mu.Lock()
	defer p.mu.Unlock()
	return p.packets
}

// Report returns a copy of the report so far.
func (p *Player) Report() *report.Report {
	p.mu.Lock()
	defer p.mu.Unlock()
	return p.copyReport()
}

func (p *Player) copyReport() *report.Report {
	r := *p.analyzer.Report()
	if r.Video != nil {
		v := *r.Video
		r.Video = &v
	}
	if r.Audio != nil {
		a := *r.Audio
		r.Audio = &a
	}
	return &r
}

// Until waits until the report meets u, and returns a copy of it, partial
// with an error when ctx ends first, a packet can't be analyzed, or the
// connection fails.
func (p *Player) Until(ctx context.Context, u report.Until) (*report.Report, error) {
	for {
		p.mu.Lock()
		r, err, changed := p.copyReport(), p.err, p.changed
		p.mu.Unlock()
		if err != nil {
			return r, err
		}
		if u.Done(r) {
			return r, nil
		}
		select {
		case <-changed:
		case <-p.done:
			return r, fmt.Errorf("the connection ended before %v: %v: %v", u, p.doneErr, r)
		case <-ctx.Done():
			return r, fmt.Errorf("%w before %v: %v", ctx.Err(), u, r)
		}
	}
}

// Close sends TEARDOWN and closes the connection.
func (p *Player) Close() {
	p.client.Close()
}
