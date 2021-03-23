package srtp

import (
	"github.com/pion/transport/replaydetector"
)

// ContextOption represents option of Context using the functional options pattern.
type ContextOption func(*Context) error

// SRTPReplayProtection sets SRTP replay protection window size.
func SRTPReplayProtection(windowSize uint) ContextOption { // nolint:golint
	return func(c *Context) error {
		c.newSRTPReplayDetector = func() replaydetector.ReplayDetector {
			return replaydetector.WithWrap(windowSize, maxSequenceNumber)
		}
		return nil
	}
}

// SRTCPReplayProtection sets SRTCP replay protection window size.
func SRTCPReplayProtection(windowSize uint) ContextOption {
	return func(c *Context) error {
		c.newSRTCPReplayDetector = func() replaydetector.ReplayDetector {
			return replaydetector.WithWrap(windowSize, maxSRTCPIndex)
		}
		return nil
	}
}

// SRTPNoReplayProtection disables SRTP replay protection.
func SRTPNoReplayProtection() ContextOption { // nolint:golint
	return func(c *Context) error {
		c.newSRTPReplayDetector = func() replaydetector.ReplayDetector {
			return &nopReplayDetector{}
		}
		return nil
	}
}

// SRTCPNoReplayProtection disables SRTCP replay protection.
func SRTCPNoReplayProtection() ContextOption {
	return func(c *Context) error {
		c.newSRTCPReplayDetector = func() replaydetector.ReplayDetector {
			return &nopReplayDetector{}
		}
		return nil
	}
}

type nopReplayDetector struct{}

func (s *nopReplayDetector) Check(uint64) (func(), bool) {
	return func() {}, true
}
