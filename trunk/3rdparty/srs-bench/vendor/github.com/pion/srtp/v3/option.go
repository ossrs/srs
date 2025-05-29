// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package srtp

import (
	"github.com/pion/transport/v3/replaydetector"
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

// SRTPReplayDetectorFactory sets custom SRTP replay detector.
func SRTPReplayDetectorFactory(fn func() replaydetector.ReplayDetector) ContextOption { // nolint:revive
	return func(c *Context) error {
		c.newSRTPReplayDetector = fn
		return nil
	}
}

// SRTCPReplayDetectorFactory sets custom SRTCP replay detector.
func SRTCPReplayDetectorFactory(fn func() replaydetector.ReplayDetector) ContextOption {
	return func(c *Context) error {
		c.newSRTCPReplayDetector = fn
		return nil
	}
}

type nopReplayDetector struct{}

func (s *nopReplayDetector) Check(uint64) (func() bool, bool) {
	return func() bool { return true }, true
}

// MasterKeyIndicator sets RTP/RTCP MKI for the initial master key. Array passed as an argument will be
// copied as-is to encrypted SRTP/SRTCP packets, so it must be of proper length and in Big Endian format.
// All MKIs added later using Context.AddCipherForMKI must have the same length as the one used here.
func MasterKeyIndicator(mki []byte) ContextOption {
	return func(c *Context) error {
		if len(mki) > 0 {
			c.sendMKI = make([]byte, len(mki))
			copy(c.sendMKI, mki)
		}
		return nil
	}
}

// SRTPEncryption enables SRTP encryption.
func SRTPEncryption() ContextOption { // nolint:revive
	return func(c *Context) error {
		c.encryptSRTP = true
		return nil
	}
}

// SRTPNoEncryption disables SRTP encryption. This option is useful when you want to use NullCipher for SRTP and keep authentication only.
// It simplifies debugging and testing, but it is not recommended for production use.
func SRTPNoEncryption() ContextOption { // nolint:revive
	return func(c *Context) error {
		c.encryptSRTP = false
		return nil
	}
}

// SRTCPEncryption enables SRTCP encryption.
func SRTCPEncryption() ContextOption {
	return func(c *Context) error {
		c.encryptSRTCP = true
		return nil
	}
}

// SRTCPNoEncryption disables SRTCP encryption. This option is useful when you want to use NullCipher for SRTCP and keep authentication only.
// It simplifies debugging and testing, but it is not recommended for production use.
func SRTCPNoEncryption() ContextOption {
	return func(c *Context) error {
		c.encryptSRTCP = false
		return nil
	}
}
