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
	"flag"
	"fmt"
	"net"
	"net/http"
	"os"
	"strings"
	"sync"
	"time"

	"github.com/ossrs/go-oryx-lib/errors"
	"github.com/ossrs/go-oryx-lib/logger"
)

var closeAfterPublished bool

var pr string

var streams, delay int

var statListen string

func Parse(ctx context.Context) {
	fl := flag.NewFlagSet(os.Args[0], flag.ContinueOnError)

	var sfu string
	fl.StringVar(&sfu, "sfu", "srs", "The SFU server, srs or gb28181 or janus")

	fl.BoolVar(&closeAfterPublished, "cap", false, "")

	fl.StringVar(&pr, "pr", "", "")

	fl.IntVar(&streams, "sn", 1, "")
	fl.IntVar(&delay, "delay", 10, "")

	fl.StringVar(&statListen, "stat", "", "")

	fl.Usage = func() {
		fmt.Println(fmt.Sprintf("Usage: %v [Options]", os.Args[0]))
		fmt.Println(fmt.Sprintf("Options:"))
		fmt.Println(fmt.Sprintf("   -sfu    The target server that can be rtc, live, janus, or gb28181. Default: rtc"))
		fmt.Println(fmt.Sprintf("               rtc/srs: SRS WebRTC SFU server, for WebRTC/WHIP/WHEP."))
		fmt.Println(fmt.Sprintf("               live: SRS live streaming server, for RTMP/HTTP-FLV/HLS."))
		fmt.Println(fmt.Sprintf("               janus: Janus WebRTC SFU server, for janus private protocol."))
		fmt.Println(fmt.Sprintf("   -sn     The number of streams to simulate. Variable: %%d. Default: 1"))
		fmt.Println(fmt.Sprintf("   -delay  The start delay in ms for each client or stream to simulate. Default: 50"))
		fmt.Println(fmt.Sprintf("   -stat   [Optional] The stat server API listen port."))
		fmt.Println(fmt.Sprintf("Publisher:"))
		fmt.Println(fmt.Sprintf("   -pr     The url to publish. If sn exceed 1, auto append variable %%d."))
		fmt.Println(fmt.Sprintf("   -cap    Whether to close connection after publish. Default: false"))
		fmt.Println(fmt.Sprintf("\n例如，1个推流，无媒体传输:"))
		fmt.Println(fmt.Sprintf("   %v -pr=rtmp://localhost/live/livestream -cap=true", os.Args[0]))
		fmt.Println(fmt.Sprintf("\n例如，2个推流，无媒体传输："))
		fmt.Println(fmt.Sprintf("   %v -pr=rtmp://localhost/live/livestream_%%d -sn=2 -cap=true", os.Args[0]))
		fmt.Println()
	}
	_ = fl.Parse(os.Args[1:])

	showHelp := streams <= 0
	if pr == "" {
		showHelp = true
	}
	if showHelp {
		fl.Usage()
		os.Exit(-1)
	}

	if statListen != "" && !strings.Contains(statListen, ":") {
		statListen = ":" + statListen
	}

	summaryDesc := fmt.Sprintf("streams=%v", streams)
	if pr != "" {
		summaryDesc = fmt.Sprintf("%v, publish=(url=%v,cap=%v)",
			summaryDesc, pr, closeAfterPublished)
	}
	logger.Tf(ctx, "Run benchmark with %v", summaryDesc)
}

func Run(ctx context.Context) error {
	ctx, cancel := context.WithCancel(ctx)
	defer cancel()

	// Run tasks.
	var wg sync.WaitGroup
	defer wg.Wait()

	// Run STAT API server.
	wg.Add(1)
	go func() {
		defer wg.Done()

		if statListen == "" {
			return
		}

		var lc net.ListenConfig
		ln, err := lc.Listen(ctx, "tcp", statListen)
		if err != nil {
			logger.Ef(ctx, "stat listen err+%v", err)
			cancel()
			return
		}

		mux := http.NewServeMux()
		handleStat(ctx, mux, statListen)

		srv := &http.Server{
			Handler: mux,
			BaseContext: func(listener net.Listener) context.Context {
				return ctx
			},
		}

		go func() {
			<-ctx.Done()
			srv.Shutdown(ctx)
		}()

		logger.Tf(ctx, "Stat listen at %v", statListen)
		if err := srv.Serve(ln); err != nil {
			if ctx.Err() == nil {
				logger.Ef(ctx, "stat serve err+%v", err)
				cancel()
			}
			return
		}
	}()

	// Run all publishers.
	publisherStartedCtx, publisherStartedCancel := context.WithCancel(ctx)
	defer publisherStartedCancel()
	for i := 0; pr != "" && i < streams && ctx.Err() == nil; i++ {
		r_auto := pr
		if streams > 1 && !strings.Contains(r_auto, "%") {
			r_auto += "%d"
		}

		r2 := r_auto
		if strings.Contains(r2, "%") {
			r2 = fmt.Sprintf(r2, i)
		}

		gStatLive.Publishers.Expect++
		gStatLive.Publishers.Alive++

		wg.Add(1)
		go func(pr string) {
			defer wg.Done()
			defer func() {
				gStatLive.Publishers.Alive--
				logger.Tf(ctx, "Publisher %v done, alive=%v", pr, gStatLive.Publishers.Alive)

				<- publisherStartedCtx.Done()
				if gStatLive.Publishers.Alive == 0 {
					cancel()
				}
			}()

			if err := startPublish(ctx, pr, closeAfterPublished); err != nil {
				if errors.Cause(err) != context.Canceled {
					logger.Wf(ctx, "Run err %+v", err)
				}
			}
		}(r2)

		if delay > 0 {
			time.Sleep(time.Duration(delay) * time.Millisecond)
		}
	}
	return nil
}
