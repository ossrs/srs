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
package srs

import (
	"context"
	"fmt"
	"math/rand"
	"os"
	"sync"
	"testing"
	"time"

	"github.com/ossrs/go-oryx-lib/logger"
	"github.com/ossrs/go-oryx-lib/rtmp"
)

func TestRtmpPublishPlay(t *testing.T) {
	var r0, r1 error
	err := func() error {
		publisher := NewRTMPPublisher()
		defer publisher.Close()

		player := NewRTMPPlayer()
		defer player.Close()

		// Connect to RTMP URL.
		ctx, cancel := context.WithTimeout(logger.WithContext(context.Background()), time.Duration(*srsTimeout)*time.Millisecond)
		streamSuffix := fmt.Sprintf("rtmp-regression-%v-%v", os.Getpid(), rand.Int())
		rtmpUrl := fmt.Sprintf("rtmp://%v/live/%v", *srsServer, streamSuffix)

		if err := publisher.Publish(ctx, rtmpUrl); err != nil {
			return err
		}

		if err := player.Play(ctx, rtmpUrl); err != nil {
			return err
		}

		// Check packets.
		var wg sync.WaitGroup
		defer wg.Wait()

		wg.Add(1)
		go func() {
			defer wg.Done()
			var nnPackets int
			player.onRecvPacket = func(m *rtmp.Message) error {
				logger.Tf(ctx, "got %v packet, %v %vms %vB",
					nnPackets, m.MessageType, m.Timestamp, len(m.Payload))
				if nnPackets += 1; nnPackets > 50 {
					cancel()
				}
				return nil
			}
			if r1 = player.Consume(ctx); r1 != nil {
				cancel()
			}
		}()

		wg.Add(1)
		go func() {
			defer wg.Done()
			publisher.onSendPacket = func(m *rtmp.Message) error {
				time.Sleep(1 * time.Millisecond)
				return nil
			}
			if r0 = publisher.Ingest(ctx, *srsPublishAvatar); r0 != nil {
				cancel()
			}
		}()

		return nil
	}()
	if err := filterTestError(err, r0, r1); err != nil {
		t.Errorf("err %+v", err)
	}
}
