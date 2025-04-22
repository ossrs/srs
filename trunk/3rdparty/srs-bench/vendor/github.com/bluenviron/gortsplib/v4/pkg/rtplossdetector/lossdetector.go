// Package rtplossdetector implements an algorithm that detects lost packets.
package rtplossdetector

import (
	"github.com/pion/rtp"
)

// LossDetector detects lost packets.
type LossDetector struct {
	initialized    bool
	expectedSeqNum uint16
}

// New allocates a LossDetector.
//
// Deprecated: Useless.
func New() *LossDetector {
	return &LossDetector{}
}

// Process processes a RTP packet.
// It returns the number of lost packets.
func (r *LossDetector) Process(pkt *rtp.Packet) uint {
	if !r.initialized {
		r.initialized = true
		r.expectedSeqNum = pkt.SequenceNumber + 1
		return 0
	}

	if pkt.SequenceNumber != r.expectedSeqNum {
		diff := pkt.SequenceNumber - r.expectedSeqNum
		r.expectedSeqNum = pkt.SequenceNumber + 1
		return uint(diff)
	}

	r.expectedSeqNum = pkt.SequenceNumber + 1
	return 0
}
