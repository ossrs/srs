package jpeg

// StartOfImage is a SOI marker.
type StartOfImage struct{}

// Marshal encodes the marker.
func (StartOfImage) Marshal(buf []byte) []byte {
	buf = append(buf, []byte{0xFF, MarkerStartOfImage}...)
	return buf
}
