// +build gofuzz

package sdp

// Fuzz implements a randomized fuzz test of the sdp
// parser using go-fuzz.
//
// To run the fuzzer, first download go-fuzz:
// `go get github.com/dvyukov/go-fuzz/...`
//
// Then build the testing package:
// `go-fuzz-build`
//
// And run the fuzzer on the corpus:
// `go-fuzz`
func Fuzz(data []byte) int {
	// Check that unmarshalling any byte slice does not panic.
	var sd SessionDescription
	if err := sd.Unmarshal(data); err != nil {
		return 0
	}
	// Check that we can marshal anything we unmarshalled.
	_, err := sd.Marshal()
	if err != nil {
		panic("failed to marshal") // nolint
	}
	// It'd be nice to check that if we round trip Marshal then Unmarshal,
	// we get the original back. Right now, though, we frequently don't,
	// and we'd need to fix that first.
	return 1
}
