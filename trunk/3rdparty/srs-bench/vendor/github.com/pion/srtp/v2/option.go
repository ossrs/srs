// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package srtp

import (
	"github.com/pion/transport/v2/replaydetector"
)

// ContextOption represents option of Context using the functional options pattern.
type ContextOption func(*Context) error

// SRTPReplayProtection sets SRTP replay protection window size.
func SRTPReplayProtection(windowSize uint) ContextOption { // nolint:revive
	return func(c *Context) error {
		c.newSRTPReplayDetector = func() replaydetector.ReplayDetector {
			return replaydetector.New(windowSize, maxROC<<16|maxSequenceNumber)
		}
		return nil
	}
}

// SRTCPReplayProtection sets SRTCP replay protection window size.
func SRTCPReplayProtection(windowSize uint) ContextOption {
	return func(c *Context) error {
		c.newSRTCPReplayDetector = func() replaydetector.ReplayDetector {
			return replaydetector.New(windowSize, maxSRTCPIndex)
		}
		return nil
	}
}

// SRTPNoReplayProtection disables SRTP replay protection.
func SRTPNoReplayProtection() ContextOption { // nolint:revive
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
