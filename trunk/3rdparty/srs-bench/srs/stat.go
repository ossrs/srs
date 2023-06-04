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
package srs

import (
	"context"
	"encoding/json"
	"net/http"
	"strings"

	"github.com/ossrs/go-oryx-lib/logger"
)

type statRTC struct {
	Publishers struct {
		Expect int `json:"expect"`
		Alive  int `json:"alive"`
	} `json:"publishers"`
	Subscribers struct {
		Expect int `json:"expect"`
		Alive  int `json:"alive"`
	} `json:"subscribers"`
	PeerConnection interface{} `json:"random-pc"`
}

var gStatRTC statRTC

func handleStat(ctx context.Context, mux *http.ServeMux, l string) {
	if strings.HasPrefix(l, ":") {
		l = "127.0.0.1" + l
	}

	logger.Tf(ctx, "Handle http://%v/api/v1/sb/rtc", l)
	mux.HandleFunc("/api/v1/sb/rtc", func(w http.ResponseWriter, r *http.Request) {
		res := &struct {
			Code int         `json:"code"`
			Data interface{} `json:"data"`
		}{
			0, &gStatRTC,
		}

		b, err := json.Marshal(res)
		if err != nil {
			logger.Wf(ctx, "marshal %v err %+v", res, err)
			return
		}

		w.Write(b)
	})
}
