// +build gofuzz

package rtcp

import (
	"bytes"
	"io"
)

// Fuzz implements a randomized fuzz test of the rtcp
// parser using go-fuzz.
//
// To run the fuzzer, first download go-fuzz:
// `go get github.com/dvyukov/go-fuzz/...`
//
// Then build the testing package:
// `go-fuzz-build github.com/pion/webrtc`
//
// And run the fuzzer on the corpus:
// ```
// mkdir workdir
//
// # optionally add a starter corpus of valid rtcp packets.
// # the corpus should be as compact and diverse as possible.
// cp -r ~/my-rtcp-packets workdir/corpus
//
// go-fuzz -bin=ase-fuzz.zip -workdir=workdir
// ````
func Fuzz(data []byte) int {
	r := NewReader(bytes.NewReader(data))
	for {
		_, data, err := r.ReadPacket()
		if err == io.EOF {
			break
		}
		if err != nil {
			return 0
		}

		packet, err := Unmarshal(data)
		if err != nil {
			return 0
		}

		if _, err := packet.Marshal(); err != nil {
			return 0
		}
	}

	return 1
}
