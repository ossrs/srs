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
	"github.com/ossrs/go-oryx-lib/errors"
	"github.com/ossrs/go-oryx-lib/logger"
	"io"
	"os"
	"strings"
	"time"
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

	fl.StringVar(&c.psConfig.video, "sv", "", "")
	fl.StringVar(&c.psConfig.audio, "sa", "", "")
	fl.IntVar(&c.psConfig.fps, "fps", 0, "")

	fl.Usage = func() {
		fmt.Println(fmt.Sprintf("Usage: %v [Options]", os.Args[0]))
		fmt.Println(fmt.Sprintf("Options:"))
		fmt.Println(fmt.Sprintf("   -sfu    The target SFU, srs or gb28181 or janus. Default: srs"))
		fmt.Println(fmt.Sprintf("SIP:"))
		fmt.Println(fmt.Sprintf("   -user   The SIP username, ID of device."))
		fmt.Println(fmt.Sprintf("   -random Append N number to user as random device ID, like 1320000001."))
		fmt.Println(fmt.Sprintf("   -server The SIP server ID, ID of server."))
		fmt.Println(fmt.Sprintf("   -domain The SIP domain, domain of server and device."))
		fmt.Println(fmt.Sprintf("Publisher:"))
		fmt.Println(fmt.Sprintf("   -pr     The SIP server address, format is tcp://ip:port over TCP."))
		fmt.Println(fmt.Sprintf("   -fps    [Optional] The fps of .h264 source file."))
		fmt.Println(fmt.Sprintf("   -sa     [Optional] The file path to read audio, ignore if empty."))
		fmt.Println(fmt.Sprintf("   -sv     [Optional] The file path to read video, ignore if empty."))
		fmt.Println(fmt.Sprintf("\n例如，1个推流："))
		fmt.Println(fmt.Sprintf("   %v -sfu gb28181 -pr tcp://127.0.0.1:5060 -user 34020000001320000001 -server 34020000002000000001 -domain 3402000000", os.Args[0]))
		fmt.Println(fmt.Sprintf("   %v -sfu gb28181 -pr tcp://127.0.0.1:5060 -user 3402000000 -random 10 -server 34020000002000000001 -domain 3402000000", os.Args[0]))
		fmt.Println(fmt.Sprintf("   %v -sfu gb28181 -pr tcp://127.0.0.1:5060 -user 3402000000 -random 10 -server 34020000002000000001 -domain 3402000000 -sa avatar.aac -sv avatar.h264 -fps 25", os.Args[0]))
		fmt.Println(fmt.Sprintf("   %v -sfu gb28181 -pr tcp://127.0.0.1:5060 -user livestream -server srs -domain ossrs.io -sa avatar.aac -sv avatar.h264 -fps 25", os.Args[0]))
		fmt.Println()
	}
	if err := fl.Parse(os.Args[1:]); err == flag.ErrHelp {
		os.Exit(0)
	}

	showHelp := c.sipConfig.String() == ""
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

	if conf.psConfig.video == "" || conf.psConfig.audio == "" {
		cancel()
		return nil
	}

	ingester := NewPSIngester(&IngesterConfig{
		psConfig:    conf.psConfig,
		ssrc:        uint32(session.out.ssrc),
		clockRate:   session.out.clockRate,
		payloadType: uint8(session.out.payloadType),
	})
	defer ingester.Close()

	if ingester.conf.serverAddr, err = utilBuildMediaAddr(session.sip.conf.addr, session.out.mediaPort); err != nil {
		return err
	}

	if err := ingester.Ingest(ctx); err != nil {
		if errors.Cause(err) == io.EOF {
			logger.Tf(ctx, "EOF, video=%v, audio=%v", conf.psConfig.video, conf.psConfig.audio)
			return nil
		}
		return errors.Wrap(err, "ingest")
	}

	return nil
}
