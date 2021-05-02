/*
The MIT License (MIT)

Copyright (c) 2019 winlin

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

package main

import "testing"

func TestShouldProxyURL(t *testing.T) {
	vvs := []struct {
		source string
		proxy  string
		expect bool
	}{
		{"talks/v1", "talks/v1", true},
		{"talks/v1/iceconfig", "talks/v1", true},
		{"talks/v1/iceconfig.js", "talks/v1", true},
		{"talks/v1.js", "talks/v1", false},
		{"talks/iceconfig", "talks/v1", false},
		{"talks/v1", "api/v1", false},
		{"talks/v1/iceconfig", "api/v1", false},
	}

	for _, vv := range vvs {
		if v := shouldProxyURL(vv.source, vv.proxy); v != vv.expect {
			t.Errorf("source=%v, proxy=%v, expect=%v", vv.source, vv.proxy, vv.expect)
		}
	}
}
