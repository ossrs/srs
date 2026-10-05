// The MIT License (MIT)
//
// # Copyright (c) 2026 Winlin
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
	"io/ioutil"
	"math/rand"
	"net"
	"os"
	"testing"
	"time"

	"github.com/ossrs/go-oryx-lib/logger"
)

func TestMain(m *testing.M) {
	if err := prepareTest(); err != nil {
		logger.Ef(nil, "Prepare test fail, err %+v", err)
		os.Exit(-1)
	}

	// Disable the logger during all tests.
	if *srsLog == false {
		olw := logger.Switch(ioutil.Discard)
		defer func() {
			logger.Switch(olw)
		}()
	}

	// Init rand seed.
	rand.Seed(time.Now().UnixNano())

	os.Exit(m.Run())
}

// The allocator must skip a port another process already holds, over TCP or UDP, because
// the fast and slow black-box processes and the other Full-tier lanes run at the same time.
func TestFast_PortAllocator_SkipsBusyPort(t *testing.T) {
	tcp, err := net.Listen("tcp", ":0")
	if err != nil {
		t.Fatalf("listen tcp, err %v", err)
	}
	defer tcp.Close()

	udp, err := net.ListenPacket("udp", ":0")
	if err != nil {
		t.Fatalf("listen udp, err %v", err)
	}
	defer udp.Close()

	busyTCP := tcp.Addr().(*net.TCPAddr).Port
	busyUDP := udp.LocalAddr().(*net.UDPAddr).Port
	for _, busy := range []int{busyTCP, busyUDP} {
		candidates := []int{busy, busy + 1}
		v := &SRSPortAllocator{pick: func() int {
			port := candidates[0]
			if len(candidates) > 1 {
				candidates = candidates[1:]
			}
			return port
		}}

		if port := v.Allocate(); port != busy+1 {
			t.Errorf("allocate got %v, want %v instead of the busy port %v", port, busy+1, busy)
		}
	}
}

// The allocator picks ports only from its range, the black-box range, so it never takes a fixed
// port of a test script, a unit test port or a kernel ephemeral port in the parallel Full tier.
func TestFast_PortAllocator_PicksInBlackboxRange(t *testing.T) {
	v := NewSRSPortAllocator(30000, 30004)

	outside := 0
	for i := 0; i < 1000; i++ {
		if port := v.pick(); port < 30000 || port > 30004 {
			outside++
		}
	}
	if outside != 0 {
		t.Errorf("%v of 1000 picks outside [30000, 30004]", outside)
	}
}

// The SRS_BLACKBOX_PORTS env, like 21000-25999, moves the black-box range without a code change.
func TestFast_PortAllocator_RangeFromEnv(t *testing.T) {
	if min, max, err := blackboxPortRange(""); err != nil || min != 21000 || max != 25999 {
		t.Errorf("default range [%v, %v], err %v", min, max, err)
	}
	if min, max, err := blackboxPortRange("30000-30099"); err != nil || min != 30000 || max != 30099 {
		t.Errorf("env range [%v, %v], err %v", min, max, err)
	}
	for _, v := range []string{"30000", "30099-30000", "0-100", "60000-65536", "a-b", "1-2-3", " 1-2"} {
		if min, max, err := blackboxPortRange(v); err == nil {
			t.Errorf("invalid range %q got [%v, %v]", v, min, max)
		}
	}
}
