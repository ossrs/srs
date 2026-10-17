// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package flv

import (
	"context"
	"crypto/tls"
	"io"
	"net/http"
	"time"

	"srs-integ/internal/httpplay"
	"srs-integ/internal/media"
)

// HTTPOptions are how an HTTPPlayer asks for a stream.
type HTTPOptions = httpplay.Options

// A StatusError is an HTTP answer other than 200 OK.
type StatusError = httpplay.StatusError

// An HTTPPlayer plays an HTTP-FLV stream over a connection of its own. It
// sends the request when it dials, and reads the answer, the FLV header and
// the tags as they come, so a case can wait for each with a deadline, also
// before the stream is published. A read in the background keeps the stream,
// so a wait that times out loses nothing and the next one goes on.
type HTTPPlayer struct {
	p *httpplay.Player[*media.FLVTag]

	hdrDone            chan struct{}
	hasAudio, hasVideo bool
}

// DialHTTP connects to an http:// or https:// URL and sends the request. It
// doesn't wait for the answer.
func DialHTTP(ctx context.Context, url string, opts *HTTPOptions) (*HTTPPlayer, error) {
	hp := &HTTPPlayer{hdrDone: make(chan struct{})}
	p, err := httpplay.Dial(ctx, url, opts, func(body io.Reader, emit func(*media.FLVTag) bool) error {
		fr, err := media.NewFLVReader(body)
		if err != nil {
			return err
		}
		hp.hasAudio, hp.hasVideo = fr.HasAudio, fr.HasVideo
		close(hp.hdrDone)
		for {
			tag, err := fr.ReadTag()
			if err != nil {
				return err
			}
			if !emit(tag) {
				return nil
			}
		}
	})
	if err != nil {
		return nil, err
	}
	hp.p = p
	return hp, nil
}

// SetDeadline sets when the waits of Response, Header and ReadTag fail with a
// timeout; the zero time waits forever.
func (p *HTTPPlayer) SetDeadline(t time.Time) error {
	return p.p.SetDeadline(t)
}

// Response waits for the status line and headers of the answer.
func (p *HTTPPlayer) Response() (*http.Response, error) {
	return p.p.Response()
}

// Header waits for the FLV header and returns its audio and video flags. An
// answer other than 200 OK is a *StatusError.
func (p *HTTPPlayer) Header() (hasAudio, hasVideo bool, err error) {
	if err := p.p.Wait(p.hdrDone); err != nil {
		return false, false, err
	}
	return p.hasAudio, p.hasVideo, nil
}

// ReadTag waits for the next FLV tag. It returns io.EOF when the stream ends
// cleanly, and a *StatusError for an answer other than 200 OK.
func (p *HTTPPlayer) ReadTag() (*media.FLVTag, error) {
	return p.p.Read()
}

// TLS returns the state of the TLS connection of an https:// player, and
// nil for http://.
func (p *HTTPPlayer) TLS() *tls.ConnectionState {
	return p.p.TLS()
}

// Close closes the connection.
func (p *HTTPPlayer) Close() error {
	return p.p.Close()
}
