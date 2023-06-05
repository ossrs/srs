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
	"fmt"
	"io/ioutil"
	"net/http"
	"strings"

	"github.com/ossrs/go-oryx-lib/errors"
	"github.com/ossrs/go-oryx-lib/logger"
)

// Request SRS API and got response, both in JSON.
// The r is HTTP API to request, like "http://localhost:1985/rtc/v1/play".
// The req is the HTTP request body, will be marshal to JSON object. nil is no body
// The res is the HTTP response body, already unmarshal to JSON object.
func apiRequest(ctx context.Context, r string, req interface{}, res interface{}) error {
	var b []byte
	if req != nil {
		if b0, err := json.Marshal(req); err != nil {
			return errors.Wrapf(err, "Marshal body %v", req)
		} else {
			b = b0
		}
	}
	logger.If(ctx, "Request url api=%v with %v", r, string(b))
	logger.Tf(ctx, "Request url api=%v with %v bytes", r, len(b))

	method := "POST"
	if req == nil {
		method = "GET"
	}
	reqObj, err := http.NewRequest(method, r, strings.NewReader(string(b)))
	if err != nil {
		return errors.Wrapf(err, "HTTP request %v", string(b))
	}

	resObj, err := http.DefaultClient.Do(reqObj.WithContext(ctx))
	if err != nil {
		return errors.Wrapf(err, "Do HTTP request %v", string(b))
	}

	b2, err := ioutil.ReadAll(resObj.Body)
	if err != nil {
		return errors.Wrapf(err, "Read response for %v", string(b))
	}
	logger.If(ctx, "Response from %v is %v", r, string(b2))
	logger.Tf(ctx, "Response from %v is %v bytes", r, len(b2))

	errorCode := struct {
		Code int `json:"code"`
	}{}
	if err := json.Unmarshal(b2, &errorCode); err != nil {
		return errors.Wrapf(err, "Unmarshal %v", string(b2))
	}
	if errorCode.Code != 0 {
		return errors.Errorf("Server fail code=%v %v", errorCode.Code, string(b2))
	}

	if err := json.Unmarshal(b2, res); err != nil {
		return errors.Wrapf(err, "Unmarshal %v", string(b2))
	}
	logger.Tf(ctx, "Parse response to code=%v ok, %v", errorCode.Code, res)

	return nil
}

// The SRS HTTP statistic API.
type statAPI struct {
	ctx     context.Context
	streams []*statStream
	stream  *statStream
}

func newStatAPI(ctx context.Context) *statAPI {
	return &statAPI{ctx: ctx}
}

type statGeneral struct {
	Code   int    `json:"code"`
	Server string `json:"server"`
}

type statPublishInStream struct {
	Cid    string `json:"cid"`
	Active bool   `json:"active"`
}

func (v statPublishInStream) String() string {
	return fmt.Sprintf("id=%v, active=%v", v.Cid, v.Active)
}

type statStream struct {
	ID      string              `json:"id"`
	Vhost   string              `json:"vhost"`
	App     string              `json:"app"`
	Name    string              `json:"name"`
	Clients int                 `json:"clients"`
	Publish statPublishInStream `json:"publish"`
}

func (v statStream) String() string {
	return fmt.Sprintf("id=%v, name=%v, pub=%v", v.ID, v.Name, v.Publish)
}

// Output to v.streams
func (v *statAPI) Streams() *statAPI {
	res := struct {
		statGeneral
		Streams []*statStream `json:"streams"`
	}{}

	ctx := v.ctx
	if err := apiRequest(ctx, "http://localhost:1985/api/v1/streams/", nil, &res); err != nil {
		logger.Tf(ctx, "query streams err %+v", err)
		return v
	}

	v.streams = res.Streams
	return v
}

// Output to v.stream
func (v *statAPI) FilterByStreamSuffix(suffix string) *statAPI {
	for _, stream := range v.streams {
		if strings.HasSuffix(stream.Name, suffix) {
			v.stream = stream
			break
		}
	}
	return v
}
