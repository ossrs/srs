// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package srtp

import (
	"fmt"

	"github.com/pion/transport/v2/replaydetector"
)

const (
	labelSRTPEncryption        = 0x00
	labelSRTPAuthenticationTag = 0x01
	labelSRTPSalt              = 0x02

	labelSRTCPEncryption        = 0x03
	labelSRTCPAuthenticationTag = 0x04
	labelSRTCPSalt              = 0x05

	maxSequenceNumber = 65535
	maxROC            = (1 << 32) - 1

	seqNumMedian = 1 << 15
	seqNumMax    = 1 << 16

	srtcpIndexSize = 4
)

// Encrypt/Decrypt state for a single SRTP SSRC
type srtpSSRCState struct {
	ssrc                 uint32
	rolloverHasProcessed bool
	index                uint64
	replayDetector       replaydetector.ReplayDetector
}

// Encrypt/Decrypt state for a single SRTCP SSRC
type srtcpSSRCState struct {
	srtcpIndex     uint32
	ssrc           uint32
	replayDetector replaydetector.ReplayDetector
}

// Context represents a SRTP cryptographic context.
// Context can only be used for one-way operations.
// it must either used ONLY for encryption or ONLY for decryption.
// Note that Context does not provide any concurrency protection:
// access to a Context from multiple goroutines requires external
// synchronization.
type Context struct {
	cipher srtpCipher

	srtpSSRCStates  map[uint32]*srtpSSRCState
	srtcpSSRCStates map[uint32]*srtcpSSRCState

	newSRTCPReplayDetector func() replaydetector.ReplayDetector
	newSRTPReplayDetector  func() replaydetector.ReplayDetector
}

// CreateContext creates a new SRTP Context.
//
// CreateContext receives variable number of ContextOption-s.
// Passing multiple options which set the same parameter let the last one valid.
// Following example create SRTP Context with replay protection with window size of 256.
//
//	decCtx, err := srtp.CreateContext(key, salt, profile, srtp.SRTPReplayProtection(256))
func CreateContext(masterKey, masterSalt []byte, profile ProtectionProfile, opts ...ContextOption) (c *Context, err error) {
	keyLen, err := profile.keyLen()
	if err != nil {
		return nil, err
	}

	saltLen, err := profile.saltLen()
	if err != nil {
		return nil, err
	}

	if masterKeyLen := len(masterKey); masterKeyLen != keyLen {
		return c, fmt.Errorf("%w expected(%d) actual(%d)", errShortSrtpMasterKey, masterKey, keyLen)
	} else if masterSaltLen := len(masterSalt); masterSaltLen != saltLen {
		return c, fmt.Errorf("%w expected(%d) actual(%d)", errShortSrtpMasterSalt, saltLen, masterSaltLen)
	}

	c = &Context{
		srtpSSRCStates:  map[uint32]*srtpSSRCState{},
		srtcpSSRCStates: map[uint32]*srtcpSSRCState{},
	}

	switch profile {
	case ProtectionProfileAeadAes128Gcm, ProtectionProfileAeadAes256Gcm:
		c.cipher, err = newSrtpCipherAeadAesGcm(profile, masterKey, masterSalt)
	case ProtectionProfileAes128CmHmacSha1_32, ProtectionProfileAes128CmHmacSha1_80:
		c.cipher, err = newSrtpCipherAesCmHmacSha1(profile, masterKey, masterSalt)
	default:
		return nil, fmt.Errorf("%w: %#v", errNoSuchSRTPProfile, profile)
	}
	if err != nil {
		return nil, err
	}

	for _, o := range append(
		[]ContextOption{ // Default options
			SRTPNoReplayProtection(),
			SRTCPNoReplayProtection(),
		},
		opts..., // User specified options
	) {
		if errOpt := o(c); errOpt != nil {
			return nil, errOpt
		}
	}

	return c, nil
}

// https://tools.ietf.org/html/rfc3550#appendix-A.1
func (s *srtpSSRCState) nextRolloverCount(sequenceNumber uint16) (roc uint32, diff int32, overflow bool) {
	seq := int32(sequenceNumber)
	localRoc := uint32(s.index >> 16)
	localSeq := int32(s.index & (seqNumMax - 1))

	guessRoc := localRoc
	var difference int32

	if s.rolloverHasProcessed {
		// When localROC is equal to 0, and entering seq-localSeq > seqNumMedian
		// judgment, it will cause guessRoc calculation error
		if s.index > seqNumMedian {
			if localSeq < seqNumMedian {
				if seq-localSeq > seqNumMedian {
					guessRoc = localRoc - 1
					difference = seq - localSeq - seqNumMax
				} else {
					guessRoc = localRoc
					difference = seq - localSeq
				}
			} else {
				if localSeq-seqNumMedian > seq {
					guessRoc = localRoc + 1
					difference = seq - localSeq + seqNumMax
				} else {
					guessRoc = localRoc
					difference = seq - localSeq
				}
			}
		} else {
			// localRoc is equal to 0
			difference = seq - localSeq
		}
	}

	return guessRoc, difference, (guessRoc == 0 && localRoc == maxROC)
}

func (s *srtpSSRCState) updateRolloverCount(sequenceNumber uint16, difference int32) {
	if !s.rolloverHasProcessed {
		s.index |= uint64(sequenceNumber)
		s.rolloverHasProcessed = true
		return
	}
	if difference > 0 {
		s.index += uint64(difference)
	}
}

func (c *Context) getSRTPSSRCState(ssrc uint32) *srtpSSRCState {
	s, ok := c.srtpSSRCStates[ssrc]
	if ok {
		return s
	}

	s = &srtpSSRCState{
		ssrc:           ssrc,
		replayDetector: c.newSRTPReplayDetector(),
	}
	c.srtpSSRCStates[ssrc] = s
	return s
}

func (c *Context) getSRTCPSSRCState(ssrc uint32) *srtcpSSRCState {
	s, ok := c.srtcpSSRCStates[ssrc]
	if ok {
		return s
	}

	s = &srtcpSSRCState{
		ssrc:           ssrc,
		replayDetector: c.newSRTCPReplayDetector(),
	}
	c.srtcpSSRCStates[ssrc] = s
	return s
}

// ROC returns SRTP rollover counter value of specified SSRC.
func (c *Context) ROC(ssrc uint32) (uint32, bool) {
	s, ok := c.srtpSSRCStates[ssrc]
	if !ok {
		return 0, false
	}
	return uint32(s.index >> 16), true
}

// SetROC sets SRTP rollover counter value of specified SSRC.
func (c *Context) SetROC(ssrc uint32, roc uint32) {
	s := c.getSRTPSSRCState(ssrc)
	s.index = uint64(roc) << 16
	s.rolloverHasProcessed = false
}

// Index returns SRTCP index value of specified SSRC.
func (c *Context) Index(ssrc uint32) (uint32, bool) {
	s, ok := c.srtcpSSRCStates[ssrc]
	if !ok {
		return 0, false
	}
	return s.srtcpIndex, true
}

// SetIndex sets SRTCP index value of specified SSRC.
func (c *Context) SetIndex(ssrc uint32, index uint32) {
	s := c.getSRTCPSSRCState(ssrc)
	s.srtcpIndex = index % (maxSRTCPIndex + 1)
}
