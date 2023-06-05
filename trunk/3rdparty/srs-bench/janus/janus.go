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
package janus

import (
	"context"
	"flag"
	"fmt"
	"github.com/ossrs/go-oryx-lib/errors"
	"github.com/ossrs/go-oryx-lib/logger"
	"os"
	"strings"
	"sync"
	"time"
)

var sr string
var pli int

var pr, sourceAudio, sourceVideo string
var fps int

var audioLevel, videoTWCC bool

var clients, streams, delay int

func Parse(ctx context.Context) {
	fl := flag.NewFlagSet(os.Args[0], flag.ContinueOnError)

	var sfu string
	fl.StringVar(&sfu, "sfu", "srs", "The SFU server, srs or gb28181 or janus")

	fl.StringVar(&sr, "sr", "", "")
	fl.IntVar(&pli, "pli", 10, "")

	fl.StringVar(&pr, "pr", "", "")
	fl.StringVar(&sourceAudio, "sa", "", "")
	fl.StringVar(&sourceVideo, "sv", "", "")
	fl.IntVar(&fps, "fps", 0, "")

	fl.BoolVar(&audioLevel, "al", true, "")
	fl.BoolVar(&videoTWCC, "twcc", true, "")

	fl.IntVar(&clients, "nn", 1, "")
	fl.IntVar(&streams, "sn", 1, "")
	fl.IntVar(&delay, "delay", 50, "")

	fl.Usage = func() {
		fmt.Println(fmt.Sprintf("Usage: %v [Options]", os.Args[0]))
		fmt.Println(fmt.Sprintf("Options:"))
		fmt.Println(fmt.Sprintf("   -sfu    The target SFU, srs or gb28181 or janus. Default: srs"))
		fmt.Println(fmt.Sprintf("   -nn     The number of clients to simulate. Default: 1"))
		fmt.Println(fmt.Sprintf("   -sn     The number of streams to simulate. Variable: %%d. Default: 1"))
		fmt.Println(fmt.Sprintf("   -delay  The start delay in ms for each client or stream to simulate. Default: 50"))
		fmt.Println(fmt.Sprintf("   -al     [Optional] Whether enable audio-level. Default: true"))
		fmt.Println(fmt.Sprintf("   -twcc   [Optional] Whether enable vdieo-twcc. Default: true"))
		fmt.Println(fmt.Sprintf("Player or Subscriber:"))
		fmt.Println(fmt.Sprintf("   -sr     The url to play/subscribe. If sn exceed 1, auto append variable %%d."))
		fmt.Println(fmt.Sprintf("   -pli    [Optional] PLI request interval in seconds. Default: 10"))
		fmt.Println(fmt.Sprintf("Publisher:"))
		fmt.Println(fmt.Sprintf("   -pr     The url to publish. If sn exceed 1, auto append variable %%d."))
		fmt.Println(fmt.Sprintf("   -fps    [Optional] The fps of .h264 source file."))
		fmt.Println(fmt.Sprintf("   -sa     [Optional] The file path to read audio, ignore if empty."))
		fmt.Println(fmt.Sprintf("   -sv     [Optional] The file path to read video, ignore if empty."))
		fmt.Println(fmt.Sprintf("\n例如，1个播放，1个推流:"))
		fmt.Println(fmt.Sprintf("   %v -sfu janus -sr webrtc://localhost:8080/2345/livestream", os.Args[0]))
		fmt.Println(fmt.Sprintf("   %v -sfu janus -pr webrtc://localhost:8080/2345/livestream -sa avatar.ogg -sv avatar.h264 -fps 25", os.Args[0]))
		fmt.Println(fmt.Sprintf("\n例如，1个流，3个播放，共3个客户端："))
		fmt.Println(fmt.Sprintf("   %v -sfu janus -sr webrtc://localhost:8080/2345/livestream -nn 3", os.Args[0]))
		fmt.Println(fmt.Sprintf("   %v -sfu janus -pr webrtc://localhost:8080/2345/livestream -sa avatar.ogg -sv avatar.h264 -fps 25", os.Args[0]))
		fmt.Println(fmt.Sprintf("\n例如，2个流，每个流3个播放，共6个客户端："))
		fmt.Println(fmt.Sprintf("   %v -sfu janus -sr webrtc://localhost:8080/2345/livestream_%%d -sn 2 -nn 3", os.Args[0]))
		fmt.Println(fmt.Sprintf("   %v -sfu janus -pr webrtc://localhost:8080/2345/livestream_%%d -sn 2 -sa avatar.ogg -sv avatar.h264 -fps 25", os.Args[0]))
		fmt.Println(fmt.Sprintf("\n例如，2个推流："))
		fmt.Println(fmt.Sprintf("   %v -sfu janus -pr webrtc://localhost:8080/2345/livestream_%%d -sn 2 -sa avatar.ogg -sv avatar.h264 -fps 25", os.Args[0]))
	}
	if err := fl.Parse(os.Args[1:]); err == flag.ErrHelp {
		os.Exit(0)
	}

	showHelp := (clients <= 0 || streams <= 0)
	if sr == "" && pr == "" {
		showHelp = true
	}
	if pr != "" && (sourceAudio == "" && sourceVideo == "") {
		showHelp = true
	}
	if showHelp {
		fl.Usage()
		os.Exit(-1)
	}

	summaryDesc := fmt.Sprintf("delay=%v, al=%v, twcc=%v", delay, audioLevel, videoTWCC)
	if sr != "" {
		summaryDesc = fmt.Sprintf("%v, play(url=%v, pli=%v)", summaryDesc, sr, pli)
	}
	if pr != "" {
		summaryDesc = fmt.Sprintf("%v, publish(url=%v, sa=%v, sv=%v, fps=%v)",
			summaryDesc, pr, sourceAudio, sourceVideo, fps)
	}
	logger.Tf(ctx, "Run benchmark with %v", summaryDesc)

	checkFlags := func() error {
		if sourceVideo != "" && !strings.HasSuffix(sourceVideo, ".h264") {
			return errors.Errorf("Should be .264, actual %v", sourceVideo)
		}

		if sourceVideo != "" && strings.HasSuffix(sourceVideo, ".h264") && fps <= 0 {
			return errors.Errorf("Video fps should >0, actual %v", fps)
		}
		return nil
	}
	if err := checkFlags(); err != nil {
		logger.Ef(ctx, "Check faile err %+v", err)
		os.Exit(-1)
	}
}

func Run(ctx context.Context) error {
	// Run tasks.
	var wg sync.WaitGroup

	// Run all subscribers or players.
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
			wg.Add(1)
			go func(sr string) {
				defer wg.Done()

				if err := startPlay(ctx, sr, audioLevel, videoTWCC, pli); err != nil {
					if errors.Cause(err) != context.Canceled {
						logger.Wf(ctx, "Run err %+v", err)
					}
				}
			}(r2)

			time.Sleep(time.Duration(delay) * time.Millisecond)
		}
	}

	// Run all publishers.
	for i := 0; pr != "" && i < streams && ctx.Err() == nil; i++ {
		r_auto := pr
		if streams > 1 && !strings.Contains(r_auto, "%") {
			r_auto += "%d"
		}

		r2 := r_auto
		if strings.Contains(r2, "%") {
			r2 = fmt.Sprintf(r2, i)
		}

		wg.Add(1)
		go func(pr string) {
			defer wg.Done()

			if err := startPublish(ctx, pr, sourceAudio, sourceVideo, fps, audioLevel, videoTWCC); err != nil {
				if errors.Cause(err) != context.Canceled {
					logger.Wf(ctx, "Run err %+v", err)
				}
			}
		}(r2)

		time.Sleep(time.Duration(delay) * time.Millisecond)
	}

	wg.Wait()

	return nil
}
