// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package srt

import (
	"bytes"
	"testing"

	"srs-integ/internal/ts"
)

// TestDroppedMessages has a libsrt listener at loss drop a few whole messages,
// which passes as the known drop, and deliver what was sent in any other
// wrong way, which does not.
func TestDroppedMessages(t *testing.T) {
	var msgs [][]byte
	for i := range 6 {
		msgs = append(msgs, bytes.Repeat([]byte{byte(i + 1)}, MessageSize))
	}
	join := func(idx ...int) []byte {
		var b []byte
		for _, i := range idx {
			b = append(b, msgs[i]...)
		}
		return b
	}
	want := join(0, 1, 2, 3, 4, 5)
	null := nullMessage()
	corrupt := join(0, 1, 2, 3, 4, 5)
	corrupt[MessageSize+5] ^= 0xff

	for _, c := range []struct {
		name    string
		got     []byte
		dropped int
		ok      bool
	}{
		{"AllThere", want, 0, true},
		{"AllThenNulls", append(join(0, 1, 2, 3, 4, 5), null...), 0, true},
		{"OneDropped", append(join(0, 1, 3, 4, 5), null...), 1, true},
		{"TwoApart", join(0, 2, 3, 5), 2, true},
		{"FirstDropped", join(1, 2, 3, 4, 5), 1, true},
		{"NullCutByRead", append(join(0, 2, 3, 4, 5), null[:ts.PacketSize+10]...), 1, true},
		{"Duplicated", join(0, 1, 1, 2, 3, 4, 5), 0, false},
		{"Reordered", join(0, 2, 1, 3, 4, 5), 0, false},
		{"Corrupt", corrupt, 0, false},
		{"LastMissing", join(0, 1, 2, 3, 4), 0, false},
		{"NothingThenNulls", null, 0, false},
		{"OtherAfter", append(join(0, 1, 2, 3, 4, 5), msgs[2]...), 0, false},
	} {
		t.Run(c.name, func(t *testing.T) {
			dropped, ok := droppedMessages(c.got, want)
			if ok != c.ok || (ok && dropped != c.dropped) {
				t.Errorf("got %v dropped, ok %v; want %v, ok %v", dropped, ok, c.dropped, c.ok)
			}
		})
	}
}
