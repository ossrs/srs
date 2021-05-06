// The MIT License (MIT)
//
// Copyright (c) 2013-2017 Oryx(ossrs)
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

// The oryx http package, the response parse service.
package http

import (
	"encoding/json"
	"fmt"
	"io/ioutil"
	"net/http"
)

// Read http api by HTTP GET and parse the code/data.
func ApiRequest(url string) (code int, body []byte, err error) {
	if body, err = apiGet(url); err != nil {
		return
	}

	if code, _, err = apiParse(url, body); err != nil {
		return
	}

	return
}

// Read http api by HTTP GET.
func apiGet(url string) (body []byte, err error) {
	var resp *http.Response
	if resp, err = http.Get(url); err != nil {
		err = fmt.Errorf("api get failed, url=%v, err is %v", url, err)
		return
	}
	defer resp.Body.Close()

	if body, err = ioutil.ReadAll(resp.Body); err != nil {
		err = fmt.Errorf("api read failed, url=%v, err is %v", url, err)
		return
	}

	return
}

// Parse the standard response {code:int,data:object}.
func apiParse(url string, body []byte) (code int, data interface{}, err error) {
	obj := make(map[string]interface{})
	if err = json.Unmarshal(body, &obj); err != nil {
		err = fmt.Errorf("api parse failed, url=%v, body=%v, err is %v", url, string(body), err)
		return
	}

	if value, ok := obj["code"]; !ok {
		err = fmt.Errorf("api no code, url=%v, body=%v", url, string(body))
		return
	} else if value, ok := value.(float64); !ok {
		err = fmt.Errorf("api code not number, code=%v, url=%v, body=%v", value, url, string(body))
		return
	} else {
		code = int(value)
	}

	data, _ = obj["data"]
	if code != 0 {
		err = fmt.Errorf("api error, code=%v, url=%v, body=%v, data=%v", code, url, string(body), data)
		return
	}

	return
}
