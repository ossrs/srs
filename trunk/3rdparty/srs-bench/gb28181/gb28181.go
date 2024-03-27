// The MIT License (MIT)
//
// # Copyright (c) 2022 Winlin
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
package gb28181

import (
	"context"
	"flag"
	"fmt"
	"io"
	"os"
	"strconv"
	"strings"
	"time"

	"github.com/ossrs/go-oryx-lib/errors"
	"github.com/ossrs/go-oryx-lib/logger"
)

type gbMainConfig struct {
	sipConfig SIPConfig
	psConfig  PSConfig
}

func Parse(ctx context.Context) interface{} {
	fl := flag.NewFlagSet(os.Args[0], flag.ContinueOnError)

	var sfu string
	fl.StringVar(&sfu, "sfu", "srs", "The SFU server, srs or gb28181 or janus")

	c := &gbMainConfig{}
	fl.StringVar(&c.sipConfig.addr, "pr", "", "")
	fl.StringVar(&c.sipConfig.user, "user", "", "")
	fl.StringVar(&c.sipConfig.server, "server", "", "")
	fl.StringVar(&c.sipConfig.domain, "domain", "", "")
	fl.IntVar(&c.sipConfig.random, "random", 0, "")

	fl.StringVar(&c.psConfig.addr, "mr", "", "")
	fl.StringVar(&c.psConfig.ssrc, "ssrc", "", "")
	fl.StringVar(&c.psConfig.video, "sv", "", "")
	fl.StringVar(&c.psConfig.audio, "sa", "", "")
	fl.IntVar(&c.psConfig.fps, "fps", 0, "")

	fl.Usage = func() {
		fmt.Printf("Usage: %v [Options]\n", os.Args[0])
		fmt.Printf("Options:\n")
		fmt.Printf("   -sfu    The target SFU, srs or gb28181 or janus. Default: srs\n")
		fmt.Printf("SIP:\n")
		fmt.Printf("   -user   The SIP username, ID of device.\n")
		fmt.Printf("   -random Append N number to user as random device ID, like 1320000001.\n")
		fmt.Printf("   -server The SIP server ID, ID of server.\n")
		fmt.Printf("   -domain The SIP domain, domain of server and device.\n")
		fmt.Printf("Publisher:\n")
		fmt.Printf("   -pr     The SIP server address, format is tcp://ip:port over TCP.\n")
		fmt.Printf("   -mr     The Meida server address, format is tcp://ip:port over TCP.\n")
		fmt.Printf("   -ssrc   [Optional] The ssrc of rtp packet\n")
		fmt.Printf("   -fps    [Optional] The fps of .h264 source file.\n")
		fmt.Printf("   -sa     [Optional] The file path to read audio, ignore if empty.\n")
		fmt.Printf("   -sv     [Optional] The file path to read video, ignore if empty.\n")
		fmt.Printf("\n例如，1个推流：\n")
		fmt.Printf("   %v -sfu gb28181 -pr tcp://127.0.0.1:5060 -user 34020000001320000001 -server 34020000002000000001 -domain 3402000000\n", os.Args[0])
		fmt.Printf("   %v -sfu gb28181 -pr tcp://127.0.0.1:5060 -user 3402000000 -random 10 -server 34020000002000000001 -domain 3402000000\n", os.Args[0])
		fmt.Printf("   %v -sfu gb28181 -pr tcp://127.0.0.1:5060 -user 3402000000 -random 10 -server 34020000002000000001 -domain 3402000000 -sa avatar.aac -sv avatar.h264 -fps 25\n", os.Args[0])
		fmt.Printf("   %v -sfu gb28181 -pr tcp://127.0.0.1:5060 -user livestream -server srs -domain ossrs.io -sa avatar.aac -sv avatar.h264 -fps 25\n", os.Args[0])
		fmt.Printf("\n例如，仅作为媒体服务器，需要提前调用API(/gb/v1/publish)创建通道：\n")
		fmt.Printf("   %v -sfu gb28181 -mr tcp://127.0.0.1:9000 -ssrc 1234567890 -sa avatar.aac -sv avatar.h264 -fps 25\n", os.Args[0])

		fmt.Println()
	}
	if err := fl.Parse(os.Args[1:]); err == flag.ErrHelp {
		os.Exit(0)
	}

	showHelp := c.sipConfig.String() == "" && c.psConfig.addr == ""
	if showHelp {
		fl.Usage()
		os.Exit(-1)
	}

	summaryDesc := ""
	if c.sipConfig.addr != "" {
		pubString := strings.Join([]string{c.sipConfig.String(), c.psConfig.String()}, ",")
		summaryDesc = fmt.Sprintf("%v, publish(%v)", summaryDesc, pubString)
	}
	logger.Tf(ctx, "Run benchmark with %v", summaryDesc)

	return c
}

func Run(ctx context.Context, r0 interface{}) (err error) {
	conf := r0.(*gbMainConfig)
	ctx, cancel := context.WithCancel(ctx)
	defer cancel()

	var mediaAddr string
	var sessionOut GBSessionOutput
	if conf.sipConfig.addr != "" {
		session := NewGBSession(&GBSessionConfig{
			regTimeout: 3 * time.Hour, inviteTimeout: 3 * time.Hour,
		}, &conf.sipConfig)
		defer session.Close()

		if err := session.Connect(ctx); err != nil {
			return errors.Wrapf(err, "connect %v", conf.sipConfig)
		}

		if err := session.Register(ctx); err != nil {
			return errors.Wrapf(err, "register %v", conf.sipConfig)
		}

		if err := session.Invite(ctx); err != nil {
			return errors.Wrapf(err, "invite %v", conf.sipConfig)
		}

		if mediaAddr, err = utilBuildMediaAddr(session.sip.conf.addr, session.out.mediaPort); err != nil {
			return errors.Wrapf(err, "build media addr, sip=%v, mediaPort=%v", session.sip.conf.addr, session.out.mediaPort)
		}

		sessionOut = *session.out
	} else if conf.psConfig.addr != "" {
		sessionOut.ssrc, err = strconv.ParseInt(conf.psConfig.ssrc, 10, 64)
		if err != nil {
			return errors.Wrapf(err, "parse ssrc=%v", conf.psConfig.ssrc)
		}
		sessionOut.clockRate = 90000
		sessionOut.payloadType = 96
		mediaAddr = conf.psConfig.addr
	}

	if conf.psConfig.video == "" || conf.psConfig.audio == "" {
		return errors.Errorf("video or audio is empty, video=%v, audio=%v", conf.psConfig.video, conf.psConfig.audio)
	}

	ingester := NewPSIngester(&IngesterConfig{
		psConfig:    conf.psConfig,
		ssrc:        uint32(sessionOut.ssrc),
		clockRate:   sessionOut.clockRate,
		payloadType: sessionOut.payloadType,
	})
	defer ingester.Close()

	ingester.conf.serverAddr = mediaAddr
	if err := ingester.Ingest(ctx); err != nil {
		if errors.Cause(err) == io.EOF {
			logger.Tf(ctx, "EOF, video=%v, audio=%v", conf.psConfig.video, conf.psConfig.audio)
			return nil
		}
		return errors.Wrap(err, "ingest")
	}

	return nil
}
