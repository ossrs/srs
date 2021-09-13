// +build gofuzz

package dtls

import "fmt"

func partialHeaderMismatch(a, b recordlayer.Header) bool {
	// Ignoring content length for now.
	a.contentLen = b.contentLen
	return a != b
}

func FuzzRecordLayer(data []byte) int {
	var r recordLayer
	if err := r.Unmarshal(data); err != nil {
		return 0
	}
	buf, err := r.Marshal()
	if err != nil {
		return 1
	}
	if len(buf) == 0 {
		panic("zero buff") // nolint
	}
	var nr recordLayer
	if err = nr.Unmarshal(data); err != nil {
		panic(err) // nolint
	}
	if partialHeaderMismatch(nr.recordlayer.Header, r.recordlayer.Header) {
		panic( // nolint
			fmt.Sprintf("header mismatch: %+v != %+v",
				nr.recordlayer.Header, r.recordlayer.Header,
			),
		)
	}

	return 1
}
