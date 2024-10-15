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
package main

import (
	"context"
	"flag"
	"fmt"
	"io/ioutil"
	"os"
	"os/signal"
	"syscall"

	"github.com/ossrs/go-oryx-lib/logger"
	"github.com/ossrs/srs-bench/gb28181"
	"github.com/ossrs/srs-bench/janus"
	"github.com/ossrs/srs-bench/live"
	"github.com/ossrs/srs-bench/srs"
)

func main() {
	var sfu string
	fl := flag.NewFlagSet(os.Args[0], flag.ContinueOnError)
	fl.SetOutput(ioutil.Discard)
	fl.StringVar(&sfu, "sfu", "rtc", "")
	_ = fl.Parse(os.Args[1:])

	ctx := context.Background()
	var conf interface{}
	if sfu == "rtc" || sfu == "srs" {
		srs.Parse(ctx)
	} else if sfu == "live" {
		live.Parse(ctx)
	} else if sfu == "gb28181" {
		conf = gb28181.Parse(ctx)
	} else if sfu == "janus" {
		janus.Parse(ctx)
	} else {
		fmt.Println(fmt.Sprintf("Usage: %v [Options]", os.Args[0]))
		fmt.Println(fmt.Sprintf("Options:"))
		fmt.Println(fmt.Sprintf("   -sfu    The target server that can be rtc, live, janus, or gb28181. Default: rtc"))
		fmt.Println(fmt.Sprintf("               rtc/srs: SRS WebRTC SFU server, for WebRTC/WHIP/WHEP."))
		fmt.Println(fmt.Sprintf("               live: SRS live streaming server, for RTMP/HTTP-FLV/HLS."))
		fmt.Println(fmt.Sprintf("               janus: Janus WebRTC SFU server, for janus private protocol."))
		fmt.Println(fmt.Sprintf("               gb28181: GB media server, for GB protocol."))
		os.Exit(-1)
	}

	ctx, cancel := context.WithCancel(ctx)
	go func() {
		sigs := make(chan os.Signal, 1)
		signal.Notify(sigs, syscall.SIGTERM, syscall.SIGINT)
		for sig := range sigs {
			logger.Wf(ctx, "Quit for signal %v", sig)
			cancel()
		}
	}()

	var err error
	if sfu == "rtc" || sfu == "srs" {
		err = srs.Run(ctx)
	} else if sfu == "live" {
		err = live.Run(ctx)
	} else if sfu == "gb28181" {
		err = gb28181.Run(ctx, conf)
	} else if sfu == "janus" {
		err = janus.Run(ctx)
	}

	if err != nil {
		logger.Wf(ctx, "srs err %+v", err)
		return
	}

	logger.Tf(ctx, "Done")
}
