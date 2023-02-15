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
	"fmt"
	"github.com/ghettovoice/gosip/sip"
	"github.com/ossrs/go-oryx-lib/errors"
	"github.com/ossrs/go-oryx-lib/logger"
	"testing"
	"time"
)

func TestGbPublishRegularly(t *testing.T) {
	ctx := logger.WithContext(context.Background())
	ctx, cancel := context.WithTimeout(ctx, time.Duration(*srsTimeout)*time.Millisecond)
	defer cancel()

	err := func() error {
		t := NewGBTestPublisher()
		defer t.Close()

		var nnPackets int
		t.ingester.onSendPacket = func(pack *PSPackStream) error {
			if nnPackets += 1; nnPackets > 10 {
				cancel()
			}
			return nil
		}

		t.ingester.conf.psConfig.video = "avatar.h264"
		if err := t.Run(ctx); err != nil {
			return err
		}

		return nil
	}()
	if err := filterTestError(ctx.Err(), err); err != nil {
		t.Errorf("err %+v", err)
	}
}

func TestGbPublishRegularlyH265(t *testing.T) {
	ctx := logger.WithContext(context.Background())
	ctx, cancel := context.WithTimeout(ctx, time.Duration(*srsTimeout)*time.Millisecond)
	defer cancel()

	err := func() error {
		t := NewGBTestPublisher()
		defer t.Close()

		var nnPackets int
		t.ingester.onSendPacket = func(pack *PSPackStream) error {
			if nnPackets += 1; nnPackets > 10 {
				cancel()
			}
			return nil
		}

		t.ingester.conf.psConfig.video = "avatar.h265"
		if err := t.Run(ctx); err != nil {
			return err
		}

		return nil
	}()
	if err := filterTestError(ctx.Err(), err); err != nil {
		t.Errorf("err %+v", err)
	}
}

func TestGbSessionHandshake(t *testing.T) {
	ctx := logger.WithContext(context.Background())
	ctx, cancel := context.WithTimeout(ctx, time.Duration(*srsTimeout)*time.Millisecond)
	defer cancel()

	err := func() error {
		t := NewGBTestSession()
		defer t.Close()

		// Use fast heartbeat for utest.
		t.session.heartbeatInterval = 100 * time.Millisecond

		if err := t.Run(ctx); err != nil {
			return err
		}

		var nn int
		t.session.onMessageHeartbeat = func(req, res sip.Message) error {
			if nn++; nn >= 3 {
				t.session.cancel()
			}
			return nil
		}

		<-t.session.heartbeatCtx.Done()
		return t.session.heartbeatCtx.Err()
	}()
	if err := filterTestError(ctx.Err(), err); err != nil {
		t.Errorf("err %+v", err)
	}
}

func TestGbSessionHandshakeDropRegisterOk(t *testing.T) {
	ctx := logger.WithContext(context.Background())
	ctx, cancel := context.WithTimeout(ctx, time.Duration(*srsTimeout)*time.Millisecond)
	defer cancel()

	var conf *SIPConfig
	r0 := func() error {
		t := NewGBTestSession()
		defer t.Close()

		conf = t.session.sip.conf

		ctx, cancel2 := context.WithCancel(ctx)
		t.session.onRegisterDone = func(req, res sip.Message) error {
			cancel2()
			return nil
		}

		return t.Run(ctx)
	}()

	// Use the same session for SIP.
	r1 := func() error {
		session := NewGBTestSession()
		session.session.sip.conf = conf
		defer session.Close()
		return session.Run(ctx)
	}()
	if err := filterTestError(ctx.Err(), r0, r1); err != nil {
		t.Errorf("err %+v", err)
	}
}

func TestGbSessionHandshakeDropInviteRequest(t *testing.T) {
	ctx := logger.WithContext(context.Background())
	ctx, cancel := context.WithTimeout(ctx, time.Duration(*srsTimeout)*time.Millisecond)
	defer cancel()

	var conf *SIPConfig
	r0 := func() error {
		t := NewGBTestSession()
		defer t.Close()
		conf = t.session.sip.conf

		// Drop the invite request, to simulate the device crash or disconnect when got this message.
		ctx2, cancel2 := context.WithCancel(ctx)
		t.session.onInviteRequest = func(req sip.Message) error {
			cancel2()
			return nil
		}

		return t.Run(ctx2)
	}()

	// When device restart session when inviting, server should re-invite when got register message.
	r1 := func() error {
		t := NewGBTestSession()
		t.session.sip.conf = conf
		defer t.Close()
		return t.Run(ctx)
	}()
	if err := filterTestError(ctx.Err(), r0, r1); err != nil {
		t.Errorf("err %+v", err)
	}
}

func TestGbSessionHandshakeDropInvite200Ack(t *testing.T) {
	ctx := logger.WithContext(context.Background())
	ctx, cancel := context.WithTimeout(ctx, time.Duration(*srsTimeout)*time.Millisecond)
	defer cancel()

	var conf *SIPConfig
	r0 := func() error {
		t := NewGBTestSession()
		defer t.Close()
		conf = t.session.sip.conf

		// Drop the invite ok ACK, to simulate the device crash or disconnect when got this message.
		ctx2, cancel2 := context.WithCancel(ctx)
		t.session.onInviteOkAck = func(req, res sip.Message) error {
			cancel2()
			return nil
		}

		return t.Run(ctx2)
	}()

	// When device restart session when 200 ack of invite, server should be stable state and waiting for media, then
	//there should be a media timeout and re-invite.
	r1 := func() error {
		t := NewGBTestSession()
		t.session.sip.conf = conf
		defer t.Close()
		return t.Run(ctx)
	}()
	if err := filterTestError(ctx.Err(), r0, r1); err != nil {
		t.Errorf("err %+v", err)
	}
}

func TestGbPublishMediaDisconnect(t *testing.T) {
	ctx := logger.WithContext(context.Background())
	ctx, cancel := context.WithTimeout(ctx, time.Duration(*srsTimeout)*time.Millisecond)
	defer cancel()

	var conf *SIPConfig
	r0 := func() error {
		t := NewGBTestPublisher()
		defer t.Close()
		conf = t.session.sip.conf

		var nnPackets int
		ctx2, cancel2 := context.WithCancel(ctx)
		t.ingester.onSendPacket = func(pack *PSPackStream) error {
			if nnPackets += 1; nnPackets > 200 {
				cancel2()
			}
			return nil
		}

		if err := t.Run(ctx2); err != nil {
			return err
		}

		return nil
	}()

	r1 := func() error {
		t := NewGBTestSession()
		t.session.sip.conf = conf
		defer t.Close()
		return t.Run(ctx)
	}()

	if err := filterTestError(ctx.Err(), r0, r1); err != nil {
		t.Errorf("err %+v", err)
	}
}

func TestGbSessionBye(t *testing.T) {
	ctx := logger.WithContext(context.Background())
	ctx, cancel := context.WithTimeout(ctx, time.Duration(*srsTimeout)*time.Millisecond)
	defer cancel()

	err := func() error {
		t := NewGBTestSession()
		defer t.Close()

		// Use fast heartbeat for utest.
		t.session.heartbeatInterval = 100 * time.Millisecond

		if err := t.Run(ctx); err != nil {
			return err
		}

		var nn int
		t.session.onMessageHeartbeat = func(req, res sip.Message) error {
			if nn++; nn == 3 {
				return t.session.Bye(ctx)
			}
			return nil
		}

		reconnectTimeout := time.Duration(*srsMediaTimeout+*srsReinviteTimeout+1000) * time.Millisecond
		ctx2, cancel2 := context.WithTimeout(ctx, reconnectTimeout)
		defer cancel2()

		req, err := t.session.sip.Wait(ctx2, sip.INVITE)
		if req != nil {
			return fmt.Errorf("should not invite after bye")
		}
		if errors.Cause(err) == context.DeadlineExceeded {
			return nil
		}

		return err
	}()
	if err := filterTestError(ctx.Err(), err); err != nil {
		t.Errorf("err %+v", err)
	}
}

func TestGbSessionUnregister(t *testing.T) {
	ctx := logger.WithContext(context.Background())
	ctx, cancel := context.WithTimeout(ctx, time.Duration(*srsTimeout)*time.Millisecond)
	defer cancel()

	err := func() error {
		t := NewGBTestSession()
		defer t.Close()

		// Use fast heartbeat for utest.
		t.session.heartbeatInterval = 100 * time.Millisecond

		if err := t.Run(ctx); err != nil {
			return err
		}

		var nn int
		t.session.onMessageHeartbeat = func(req, res sip.Message) error {
			if nn++; nn == 3 {
				return t.session.UnRegister(ctx)
			}
			return nil
		}

		reconnectTimeout := time.Duration(*srsMediaTimeout+*srsReinviteTimeout+1000) * time.Millisecond
		ctx2, cancel2 := context.WithTimeout(ctx, reconnectTimeout)
		defer cancel2()

		req, err := t.session.sip.Wait(ctx2, sip.INVITE)
		if req != nil {
			return fmt.Errorf("should not invite after bye")
		}
		if errors.Cause(err) == context.DeadlineExceeded {
			return nil
		}

		return err
	}()
	if err := filterTestError(ctx.Err(), err); err != nil {
		t.Errorf("err %+v", err)
	}
}

func TestGbPublishReinvite(t *testing.T) {
	ctx := logger.WithContext(context.Background())
	ctx, cancel := context.WithTimeout(ctx, time.Duration(*srsTimeout)*time.Millisecond)
	defer cancel()

	var conf *SIPConfig
	err := func() error {
		t := NewGBTestPublisher()
		defer t.Close()
		conf = t.session.sip.conf

		var nnPackets int
		ctx2, cancel2 := context.WithCancel(ctx)
		t.ingester.onSendPacket = func(pack *PSPackStream) error {
			if nnPackets += 1; nnPackets == 3 {
				cancel2()
			}
			return nil
		}

		if err := t.Run(ctx2); err != nil {
			return err
		}

		return nil
	}()

	r1 := func() error {
		t := NewGBTestSession()
		defer t.Close()
		t.session.sip.conf = conf

		// Only register the device, bind to session.
		if err := t.session.Connect(ctx); err != nil {
			return err
		}
		if err := t.session.Register(ctx); err != nil {
			return err
		}

		// We should get reinvite when reconnect to SRS.
		req, err := t.session.sip.Wait(ctx, sip.INVITE)
		if req == nil {
			return fmt.Errorf("should reinvite after disconnect")
		}

		return err
	}()
	if err := filterTestError(ctx.Err(), err, r1); err != nil {
		t.Errorf("err %+v", err)
	}
}

func TestGbPublishBye(t *testing.T) {
	ctx := logger.WithContext(context.Background())
	ctx, cancel := context.WithTimeout(ctx, time.Duration(*srsTimeout)*time.Millisecond)
	defer cancel()

	var conf *SIPConfig
	err := func() error {
		t := NewGBTestPublisher()
		defer t.Close()
		conf = t.session.sip.conf

		var nnPackets int
		ctx2, cancel2 := context.WithCancel(ctx)
		t.ingester.onSendPacket = func(pack *PSPackStream) error {
			if nnPackets += 1; nnPackets == 10 {
				if err := t.session.Bye(ctx2); err != nil {
					return err
				}
				cancel2()
			}
			return nil
		}

		if err := t.Run(ctx2); err != nil {
			return err
		}

		return nil
	}()

	r1 := func() error {
		t := NewGBTestSession()
		defer t.Close()
		t.session.sip.conf = conf

		// Only register the device, bind to session.
		if err := t.session.Connect(ctx); err != nil {
			return err
		}
		if err := t.session.Register(ctx); err != nil {
			return err
		}

		// We should not get reinvite when reconnect to SRS.
		reconnectTimeout := time.Duration(*srsMediaTimeout+*srsReinviteTimeout+1000) * time.Millisecond
		ctx2, cancel2 := context.WithTimeout(ctx, reconnectTimeout)
		defer cancel2()

		req, err := t.session.sip.Wait(ctx2, sip.INVITE)
		if req != nil {
			return fmt.Errorf("should not invite after bye")
		}
		if errors.Cause(err) == context.DeadlineExceeded {
			return nil
		}

		return err
	}()
	if err := filterTestError(ctx.Err(), err, r1); err != nil {
		t.Errorf("err %+v", err)
	}
}

func TestGbPublishUnregister(t *testing.T) {
	ctx := logger.WithContext(context.Background())
	ctx, cancel := context.WithTimeout(ctx, time.Duration(*srsTimeout)*time.Millisecond)
	defer cancel()

	var conf *SIPConfig
	err := func() error {
		t := NewGBTestPublisher()
		defer t.Close()
		conf = t.session.sip.conf

		var nnPackets int
		ctx2, cancel2 := context.WithCancel(ctx)
		t.ingester.onSendPacket = func(pack *PSPackStream) error {
			if nnPackets += 1; nnPackets == 10 {
				if err := t.session.UnRegister(ctx2); err != nil {
					return err
				}
				cancel2()
			}
			return nil
		}

		if err := t.Run(ctx2); err != nil {
			return err
		}

		return nil
	}()

	r1 := func() error {
		t := NewGBTestSession()
		defer t.Close()
		t.session.sip.conf = conf

		// Only register the device, bind to session.
		if err := t.session.Connect(ctx); err != nil {
			return err
		}
		if err := t.session.Register(ctx); err != nil {
			return err
		}

		// We should not get reinvite when reconnect to SRS.
		reconnectTimeout := time.Duration(*srsMediaTimeout+*srsReinviteTimeout+1000) * time.Millisecond
		ctx2, cancel2 := context.WithTimeout(ctx, reconnectTimeout)
		defer cancel2()

		req, err := t.session.sip.Wait(ctx2, sip.INVITE)
		if req != nil {
			return fmt.Errorf("should not invite after bye")
		}
		if errors.Cause(err) == context.DeadlineExceeded {
			return nil
		}

		return err
	}()
	if err := filterTestError(ctx.Err(), err, r1); err != nil {
		t.Errorf("err %+v", err)
	}
}
