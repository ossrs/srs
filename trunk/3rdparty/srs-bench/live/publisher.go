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
package live

import (
	"context"
	"fmt"
	"math/rand"
	"net"
	"net/url"
	"strconv"
	"strings"
	"time"

	"github.com/haivision/srtgo"
	"github.com/ossrs/go-oryx-lib/amf0"
	"github.com/ossrs/go-oryx-lib/errors"
	"github.com/ossrs/go-oryx-lib/logger"
	"github.com/ossrs/go-oryx-lib/rtmp"
)

func startPublish(ctx context.Context, r string, closeAfterPublished bool) error {
	ctx = logger.WithContext(ctx)
	logger.Tf(ctx, "Run publish url=%v, cap=%v", r, closeAfterPublished)

	u, err := url.Parse(r)
	if err != nil {
		return errors.Wrapf(err, "parse %v", r)
	}

	if u.Scheme == "rtmp" {
		return startPublishRTMP(ctx, u, closeAfterPublished)
	} else if u.Scheme == "srt" {
		return startPublishSRT(ctx, u, closeAfterPublished)
	}

	return fmt.Errorf("invalid schema %v of %v", u.Scheme, r)
}

func startPublishSRT(ctx context.Context, u *url.URL, closeAfterPublished bool) (err error) {
	// Parse host and port.
	port := 1935
	if u.Port() != "" {
		if port, err = strconv.Atoi(u.Port()); err != nil {
			return errors.Wrapf(err, "parse port %v", u.Port())
		}
	}

	ips, err := net.LookupIP(u.Hostname())
	if err != nil {
		return errors.Wrapf(err, "lookup %v", u.Hostname())
	}
	if len(ips) == 0 {
		return errors.Errorf("no ips for %v", u.Hostname())
	}
	logger.Tf(ctx, "Parse url %v to host=%v, ip=%v, port=%v",
		u.String(), u.Hostname(), ips[0], port)

	// Setup libsrt.
	client := srtgo.NewSrtSocket(ips[0].To4().String(), uint16(port),
		map[string]string{
			"transtype": "live",
			"tsbpdmode": "false",
			"tlpktdrop": "false",
			"latency":   "0",
			"streamid":  fmt.Sprintf("#%v", u.Fragment),
		},
	)
	defer client.Close()

	if err := client.Connect(); err != nil {
		return errors.Wrapf(err, "SRT connect to %v:%v", u.Hostname(), port)
	}
	logger.Tf(ctx, "Connect to SRT server %v:%v success", u.Hostname(), port)

	// We should wait for a while after connected to SRT server before quit. Because SRT server use timeout
	// to detect UDP connection status, so we should never reconnect very fast.
	select {
	case <-ctx.Done():
	case <-time.After(3 * time.Second):
		logger.Tf(ctx, "SRT publish stream success, stream=%v", u.Fragment)
	}

	if closeAfterPublished {
		logger.Tf(ctx, "Close connection after published")
		return nil
	}

	return nil
}

func startPublishRTMP(ctx context.Context, u *url.URL, closeAfterPublished bool) (err error) {
	parts := strings.Split(u.Path, "/")
	if len(parts) == 0 {
		return errors.Errorf("invalid path %v", u.Path)
	}
	app, stream := strings.Join(parts[:len(parts)-1], "/"), parts[len(parts)-1]

	// Parse host and port.
	port := 1935
	if u.Port() != "" {
		if port, err = strconv.Atoi(u.Port()); err != nil {
			return errors.Wrapf(err, "parse port %v", u.Port())
		}
	}

	ips, err := net.LookupIP(u.Hostname())
	if err != nil {
		return errors.Wrapf(err, "lookup %v", u.Hostname())
	}
	if len(ips) == 0 {
		return errors.Errorf("no ips for %v", u.Hostname())
	}
	logger.Tf(ctx, "Parse url %v to host=%v, ip=%v, port=%v, app=%v, stream=%v",
		u.String(), u.Hostname(), ips[0], port, app, stream)

	// Connect via TCP client.
	c, err := net.DialTCP("tcp", nil, &net.TCPAddr{IP: ips[0], Port: port})
	if err != nil {
		return errors.Wrapf(err, "dial %v %v", u.Hostname(), u.Port())
	}
	defer c.Close()
	logger.Tf(ctx, "Connect to RTMP server %v:%v success", u.Hostname(), port)

	// RTMP Handshake.
	rd := rand.New(rand.NewSource(time.Now().UnixNano()))
	hs := rtmp.NewHandshake(rd)

	if err := hs.WriteC0S0(c); err != nil {
		return errors.Wrap(err, "write c0")
	}
	if err := hs.WriteC1S1(c); err != nil {
		return errors.Wrap(err, "write c1")
	}

	if _, err = hs.ReadC0S0(c); err != nil {
		return errors.Wrap(err, "read s1")
	}
	s1, err := hs.ReadC1S1(c)
	if err != nil {
		return errors.Wrap(err, "read s1")
	}
	if _, err = hs.ReadC2S2(c); err != nil {
		return errors.Wrap(err, "read s2")
	}

	if err := hs.WriteC2S2(c, s1); err != nil {
		return errors.Wrap(err, "write c2")
	}
	logger.Tf(ctx, "RTMP handshake with %v:%v success", ips[0], port)

	// Do connect and publish.
	client := rtmp.NewProtocol(c)

	connectApp := rtmp.NewConnectAppPacket()
	tcURL := fmt.Sprintf("rtmp://%v%v", u.Hostname(), app)
	connectApp.CommandObject.Set("tcUrl", amf0.NewString(tcURL))
	if err = client.WritePacket(connectApp, 1); err != nil {
		return errors.Wrap(err, "write connect app")
	}

	var connectAppRes *rtmp.ConnectAppResPacket
	if _, err = client.ExpectPacket(&connectAppRes); err != nil {
		return errors.Wrap(err, "expect connect app res")
	}
	logger.Tf(ctx, "RTMP connect app success, tcUrl=%v", tcURL)

	createStream := rtmp.NewCreateStreamPacket()
	if err = client.WritePacket(createStream, 1); err != nil {
		return errors.Wrap(err, "write create stream")
	}

	var createStreamRes *rtmp.CreateStreamResPacket
	if _, err = client.ExpectPacket(&createStreamRes); err != nil {
		return errors.Wrap(err, "expect create stream res")
	}
	logger.Tf(ctx, "RTMP create stream success")

	publish := rtmp.NewPublishPacket()
	publish.StreamName = *amf0.NewString(stream)
	if err = client.WritePacket(publish, 1); err != nil {
		return errors.Wrap(err, "write publish")
	}
	logger.Tf(ctx, "RTMP publish stream success, stream=%v", stream)

	if closeAfterPublished {
		logger.Tf(ctx, "Close connection after published")
		return nil
	}

	return nil
}
