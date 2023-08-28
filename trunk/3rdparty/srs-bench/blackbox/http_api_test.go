// The MIT License (MIT)
//
// # Copyright (c) 2023 Winlin
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
	"fmt"
	"github.com/ossrs/go-oryx-lib/errors"
	"github.com/ossrs/go-oryx-lib/logger"
	"net/http"
	"sync"
	"testing"
	"time"
)

func TestFast_Http_Api_Basic_Auth(t *testing.T) {
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

	var wg sync.WaitGroup
	defer wg.Wait()

	// Start SRS server and wait for it to be ready.
	svr := NewSRSServer(func(v *srsServer) {
		v.envs = []string{
			"SRS_HTTP_API_AUTH_ENABLED=on",
			"SRS_HTTP_API_AUTH_USERNAME=admin",
			"SRS_HTTP_API_AUTH_PASSWORD=admin",
		}
	})
	wg.Add(1)
	go func() {
		defer wg.Done()
		r0 = svr.Run(ctx, cancel)
	}()

	<-svr.ReadyCtx().Done()

	if true {
		defer cancel()

		var res *http.Response
		url := fmt.Sprintf("http://admin:admin@localhost:%v/api/v1/versions", svr.APIPort())
		res, r1 = http.Get(url)
		if r1 == nil && res.StatusCode != 200 {
			r2 = errors.Errorf("get status code=%v, expect=200", res.StatusCode)
		}

		url = fmt.Sprintf("http://admin:123456@localhost:%v/api/v1/versions", svr.APIPort())
		res, r3 = http.Get(url)
		if r3 == nil && res.StatusCode != 401 {
			r4 = errors.Errorf("get status code=%v, expect=401", res.StatusCode)
		}

		url = fmt.Sprintf("http://localhost:%v/api/v1/versions", svr.APIPort())
		res, r5 = http.Get(url)
		if r5 == nil && res.StatusCode != 401 {
			r6 = errors.Errorf("get status code=%v, expect=401", res.StatusCode)
		}
	}
}
