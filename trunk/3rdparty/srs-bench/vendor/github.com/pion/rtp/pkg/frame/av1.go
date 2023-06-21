// Package frame provides code to construct complete media frames from packetized media
package frame

import "github.com/pion/rtp/codecs"

// AV1 represents a collection of OBUs given a stream of AV1 Packets.
// Each AV1 RTP Packet is a collection of OBU Elements. Each OBU Element may be a full OBU, or just a fragment of one.
// AV1 provides the tools to construct a collection of OBUs from a collection of OBU Elements. This structure
// contains an internal cache and should be used for the entire RTP Stream.
type AV1 struct {
	// Buffer for fragmented OBU. If ReadFrames is called on a RTP Packet
	// that doesn't contain a fully formed OBU
	obuBuffer []byte
}

func (f *AV1) pushOBUElement(isFirstOBUFragment *bool, obuElement []byte, obuList [][]byte) [][]byte {
	if *isFirstOBUFragment {
		*isFirstOBUFragment = false
		// Discard pushed because we don't have a fragment to combine it with
		if f.obuBuffer == nil {
			return obuList
		}
		obuElement = append(f.obuBuffer, obuElement...)
		f.obuBuffer = nil
	}
	return append(obuList, obuElement)
}

// ReadFrames processes the codecs.AV1Packet and returns fully constructed frames
func (f *AV1) ReadFrames(pkt *codecs.AV1Packet) ([][]byte, error) {
	OBUs := [][]byte{}
	isFirstOBUFragment := pkt.Z

	for i := range pkt.OBUElements {
		OBUs = f.pushOBUElement(&isFirstOBUFragment, pkt.OBUElements[i], OBUs)
	}

	if pkt.Y && len(OBUs) > 0 {
		// Take copy of OBUElement that is being cached
		f.obuBuffer = append(f.obuBuffer, append([]byte{}, OBUs[len(OBUs)-1]...)...)
		OBUs = OBUs[:len(OBUs)-1]
	}
	return OBUs, nil
}
