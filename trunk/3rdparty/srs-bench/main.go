// The MIT License (MIT)
//
// Copyright (c) 2021 Winlin
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
	"github.com/ossrs/go-oryx-lib/errors"
	"github.com/ossrs/go-oryx-lib/logger"
	"github.com/ossrs/srs-bench/srs"
	"net"
	"net/http"
	"os"
	"os/signal"
	"strings"
	"sync"
	"syscall"
	"time"
)

func main() {
	var sr, dumpAudio, dumpVideo string
	var pli int
	flag.StringVar(&sr, "sr", "", "")
	flag.StringVar(&dumpAudio, "da", "", "")
	flag.StringVar(&dumpVideo, "dv", "", "")
	flag.IntVar(&pli, "pli", 10, "")

	var pr, sourceAudio, sourceVideo string
	var fps int
	flag.StringVar(&pr, "pr", "", "")
	flag.StringVar(&sourceAudio, "sa", "", "")
	flag.StringVar(&sourceVideo, "sv", "", "")
	flag.IntVar(&fps, "fps", 0, "")

	var audioLevel, videoTWCC bool
	flag.BoolVar(&audioLevel, "al", true, "")
	flag.BoolVar(&videoTWCC, "twcc", true, "")

	var clients, streams, delay int
	flag.IntVar(&clients, "nn", 1, "")
	flag.IntVar(&streams, "sn", 1, "")
	flag.IntVar(&delay, "delay", 50, "")

	var statListen string
	flag.StringVar(&statListen, "stat", "", "")

	flag.Usage = func() {
		fmt.Println(fmt.Sprintf("Usage: %v [Options]", os.Args[0]))
		fmt.Println(fmt.Sprintf("Options:"))
		fmt.Println(fmt.Sprintf("   -nn     The number of clients to simulate. Default: 1"))
		fmt.Println(fmt.Sprintf("   -sn     The number of streams to simulate. Variable: %%d. Default: 1"))
		fmt.Println(fmt.Sprintf("   -delay  The start delay in ms for each client or stream to simulate. Default: 50"))
		fmt.Println(fmt.Sprintf("   -al     [Optional] Whether enable audio-level. Default: true"))
		fmt.Println(fmt.Sprintf("   -twcc   [Optional] Whether enable vdieo-twcc. Default: true"))
		fmt.Println(fmt.Sprintf("   -stat   [Optional] The stat server API listen port."))
		fmt.Println(fmt.Sprintf("Player or Subscriber:"))
		fmt.Println(fmt.Sprintf("   -sr     The url to play/subscribe. If sn exceed 1, auto append variable %%d."))
		fmt.Println(fmt.Sprintf("   -da     [Optional] The file path to dump audio, ignore if empty."))
		fmt.Println(fmt.Sprintf("   -dv     [Optional] The file path to dump video, ignore if empty."))
		fmt.Println(fmt.Sprintf("   -pli    [Optional] PLI request interval in seconds. Default: 10"))
		fmt.Println(fmt.Sprintf("Publisher:"))
		fmt.Println(fmt.Sprintf("   -pr     The url to publish. If sn exceed 1, auto append variable %%d."))
		fmt.Println(fmt.Sprintf("   -fps    The fps of .h264 source file."))
		fmt.Println(fmt.Sprintf("   -sa     [Optional] The file path to read audio, ignore if empty."))
		fmt.Println(fmt.Sprintf("   -sv     [Optional] The file path to read video, ignore if empty."))
		fmt.Println(fmt.Sprintf("\n例如，1个播放，1个推流:"))
		fmt.Println(fmt.Sprintf("   %v -sr webrtc://localhost/live/livestream", os.Args[0]))
		fmt.Println(fmt.Sprintf("   %v -pr webrtc://localhost/live/livestream -sa a.ogg -sv v.h264 -fps 25", os.Args[0]))
		fmt.Println(fmt.Sprintf("\n例如，1个流，3个播放，共3个客户端："))
		fmt.Println(fmt.Sprintf("   %v -sr webrtc://localhost/live/livestream -nn 3", os.Args[0]))
		fmt.Println(fmt.Sprintf("   %v -pr webrtc://localhost/live/livestream -sa a.ogg -sv v.h264 -fps 25", os.Args[0]))
		fmt.Println(fmt.Sprintf("\n例如，2个流，每个流3个播放，共6个客户端："))
		fmt.Println(fmt.Sprintf("   %v -sr webrtc://localhost/live/livestream_%%d -sn 2 -nn 3", os.Args[0]))
		fmt.Println(fmt.Sprintf("   %v -pr webrtc://localhost/live/livestream_%%d -sn 2 -sa a.ogg -sv v.h264 -fps 25", os.Args[0]))
		fmt.Println(fmt.Sprintf("\n例如，2个推流："))
		fmt.Println(fmt.Sprintf("   %v -pr webrtc://localhost/live/livestream_%%d -sn 2 -sa a.ogg -sv v.h264 -fps 25", os.Args[0]))
		fmt.Println(fmt.Sprintf("\n例如，1个录制："))
		fmt.Println(fmt.Sprintf("   %v -sr webrtc://localhost/live/livestream -da a.ogg -dv v.h264", os.Args[0]))
		fmt.Println(fmt.Sprintf("\n例如，1个明文播放："))
		fmt.Println(fmt.Sprintf("   %v -sr webrtc://localhost/live/livestream?encrypt=false", os.Args[0]))
		fmt.Println()
	}
	flag.Parse()

	showHelp := (clients <= 0 || streams <= 0)
	if sr == "" && pr == "" {
		showHelp = true
	}
	if pr != "" && (sourceAudio == "" && sourceVideo == "") {
		showHelp = true
	}
	if showHelp {
		flag.Usage()
		os.Exit(-1)
	}

	if statListen != "" && !strings.Contains(statListen, ":") {
		statListen = ":" + statListen
	}

	ctx := context.Background()
	summaryDesc := fmt.Sprintf("clients=%v, delay=%v, al=%v, twcc=%v, stat=%v", clients, delay, audioLevel, videoTWCC, statListen)
	if sr != "" {
		summaryDesc = fmt.Sprintf("%v, play(url=%v, da=%v, dv=%v, pli=%v)", summaryDesc, sr, dumpAudio, dumpVideo, pli)
	}
	if pr != "" {
		summaryDesc = fmt.Sprintf("%v, publish(url=%v, sa=%v, sv=%v, fps=%v)",
			summaryDesc, pr, sourceAudio, sourceVideo, fps)
	}
	logger.Tf(ctx, "Start benchmark with %v", summaryDesc)

	checkFlag := func() error {
		if dumpVideo != "" && !strings.HasSuffix(dumpVideo, ".h264") && !strings.HasSuffix(dumpVideo, ".ivf") {
			return errors.Errorf("Should be .ivf or .264, actual %v", dumpVideo)
		}

		if sourceVideo != "" && !strings.HasSuffix(sourceVideo, ".h264") {
			return errors.Errorf("Should be .264, actual %v", sourceVideo)
		}

		if sourceVideo != "" && strings.HasSuffix(sourceVideo, ".h264") && fps <= 0 {
			return errors.Errorf("Video fps should >0, actual %v", fps)
		}
		return nil
	}
	if err := checkFlag(); err != nil {
		logger.Ef(ctx, "Check faile err %+v", err)
		os.Exit(-1)
	}

	ctx, cancel := context.WithCancel(ctx)

	// Process all signals.
	go func() {
		sigs := make(chan os.Signal, 1)
		signal.Notify(sigs, syscall.SIGTERM, syscall.SIGINT)
		for sig := range sigs {
			logger.Wf(ctx, "Quit for signal %v", sig)
			cancel()
		}
	}()

	// Run tasks.
	var wg sync.WaitGroup

	// Start STAT API server.
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
		srs.HandleStat(ctx, mux, statListen)

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

	// Start all subscribers or players.
	for i := 0; sr != "" && i < streams && ctx.Err() == nil; i++ {
		r_auto := sr
		if streams > 1 && !strings.Contains(r_auto, "%") {
			r_auto += "%d"
		}

		r2 := r_auto
		if strings.Contains(r2, "%") {
			r2 = fmt.Sprintf(r2, i)
		}

		for j := 0; sr != "" && j < clients && ctx.Err() == nil; j++ {
			// Dump audio or video only for the first client.
			da, dv := dumpAudio, dumpVideo
			if i > 0 {
				da, dv = "", ""
			}

			srs.StatRTC.Subscribers.Expect++
			srs.StatRTC.Subscribers.Alive++

			wg.Add(1)
			go func(sr, da, dv string) {
				defer wg.Done()
				defer func() {
					srs.StatRTC.Subscribers.Alive--
				}()

				if err := srs.StartPlay(ctx, sr, da, dv, audioLevel, videoTWCC, pli); err != nil {
					if errors.Cause(err) != context.Canceled {
						logger.Wf(ctx, "Run err %+v", err)
					}
				}
			}(r2, da, dv)

			time.Sleep(time.Duration(delay) * time.Millisecond)
		}
	}

	// Start all publishers.
	for i := 0; pr != "" && i < streams && ctx.Err() == nil; i++ {
		r_auto := pr
		if streams > 1 && !strings.Contains(r_auto, "%") {
			r_auto += "%d"
		}

		r2 := r_auto
		if strings.Contains(r2, "%") {
			r2 = fmt.Sprintf(r2, i)
		}

		srs.StatRTC.Publishers.Expect++
		srs.StatRTC.Publishers.Alive++

		wg.Add(1)
		go func(pr string) {
			defer wg.Done()
			defer func() {
				srs.StatRTC.Publishers.Alive--
			}()

			if err := srs.StartPublish(ctx, pr, sourceAudio, sourceVideo, fps, audioLevel, videoTWCC); err != nil {
				if errors.Cause(err) != context.Canceled {
					logger.Wf(ctx, "Run err %+v", err)
				}
			}
		}(r2)

		time.Sleep(time.Duration(delay) * time.Millisecond)
	}

	wg.Wait()

	logger.Tf(ctx, "Done")
}
