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
package srs

import (
	"io/ioutil"
	"math/rand"
	"os"
	"testing"
	"time"

	"github.com/ossrs/go-oryx-lib/logger"
)

func TestMain(m *testing.M) {
	if err := prepareTest(); err != nil {
		logger.Ef(nil, "Prepare test fail, err %+v", err)
		os.Exit(-1)
	}

	// Disable the logger during all tests.
	if *srsLog == false {
		olw := logger.Switch(ioutil.Discard)
		defer func() {
			logger.Switch(olw)
		}()
	}

	// Init rand seed.
	rand.Seed(time.Now().UnixNano())

	os.Exit(m.Run())
}

// The port flags move each protocol off its default port, so the regression SRS
// can run on its own ports while another SRS holds the defaults.
func TestServerHostFlags(t *testing.T) {
	oServer, oRtmp, oApi, oRtsp := *srsServer, *srsRtmpPort, *srsApiPort, *srsRtspPort
	defer func() {
		*srsServer, *srsRtmpPort, *srsApiPort, *srsRtspPort = oServer, oRtmp, oApi, oRtsp
	}()

	*srsServer, *srsRtmpPort, *srsApiPort, *srsRtspPort = "127.0.0.1", 1935, 1985, 8554
	if v := srsRtmpHost(); v != "127.0.0.1:1935" {
		t.Errorf("default rtmp host %v", v)
	}
	if v := srsApiHost(); v != "127.0.0.1:1985" {
		t.Errorf("default api host %v", v)
	}
	if v := srsRtspHost(); v != "127.0.0.1:8554" {
		t.Errorf("default rtsp host %v", v)
	}

	*srsServer, *srsRtmpPort, *srsApiPort, *srsRtspPort = "10.0.0.1", 25935, 25985, 25554
	if v := srsRtmpHost(); v != "10.0.0.1:25935" {
		t.Errorf("rtmp host %v", v)
	}
	if v := srsApiHost(); v != "10.0.0.1:25985" {
		t.Errorf("api host %v", v)
	}
	if v := srsRtspHost(); v != "10.0.0.1:25554" {
		t.Errorf("rtsp host %v", v)
	}
}
