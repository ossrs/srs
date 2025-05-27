// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package srtp

import (
	"bytes"
	"fmt"

	"github.com/pion/transport/v3/replaydetector"
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

	profile ProtectionProfile

	sendMKI []byte                // Master Key Identifier used for encrypting RTP/RTCP packets. Set to nil if MKI is not enabled.
	mkis    map[string]srtpCipher // Master Key Identifier to cipher mapping. Used for decrypting packets. Empty if MKI is not enabled.

	encryptSRTP  bool
	encryptSRTCP bool
}

// CreateContext creates a new SRTP Context.
//
// CreateContext receives variable number of ContextOption-s.
// Passing multiple options which set the same parameter let the last one valid.
// Following example create SRTP Context with replay protection with window size of 256.
//
//	decCtx, err := srtp.CreateContext(key, salt, profile, srtp.SRTPReplayProtection(256))
func CreateContext(masterKey, masterSalt []byte, profile ProtectionProfile, opts ...ContextOption) (c *Context, err error) {
	c = &Context{
		srtpSSRCStates:  map[uint32]*srtpSSRCState{},
		srtcpSSRCStates: map[uint32]*srtcpSSRCState{},
		profile:         profile,
		mkis:            map[string]srtpCipher{},
	}

	for _, o := range append(
		[]ContextOption{ // Default options
			SRTPNoReplayProtection(),
			SRTCPNoReplayProtection(),
			SRTPEncryption(),
			SRTCPEncryption(),
		},
		opts..., // User specified options
	) {
		if errOpt := o(c); errOpt != nil {
			return nil, errOpt
		}
	}

	c.cipher, err = c.createCipher(c.sendMKI, masterKey, masterSalt, c.encryptSRTP, c.encryptSRTCP)
	if err != nil {
		return nil, err
	}
	if len(c.sendMKI) != 0 {
		c.mkis[string(c.sendMKI)] = c.cipher
	}

	return c, nil
}

// AddCipherForMKI adds new MKI with associated masker key and salt. Context must be created with MasterKeyIndicator option
// to enable MKI support. MKI must be unique and have the same length as the one used for creating Context.
// Operation is not thread-safe, you need to provide synchronization with decrypting packets.
func (c *Context) AddCipherForMKI(mki, masterKey, masterSalt []byte) error {
	if len(c.mkis) == 0 {
		return errMKIIsNotEnabled
	}
	if len(mki) == 0 || len(mki) != len(c.sendMKI) {
		return errInvalidMKILength
	}
	if _, ok := c.mkis[string(mki)]; ok {
		return errMKIAlreadyInUse
	}

	cipher, err := c.createCipher(mki, masterKey, masterSalt, c.encryptSRTP, c.encryptSRTCP)
	if err != nil {
		return err
	}
	c.mkis[string(mki)] = cipher
	return nil
}

func (c *Context) createCipher(mki, masterKey, masterSalt []byte, encryptSRTP, encryptSRTCP bool) (srtpCipher, error) {
	keyLen, err := c.profile.KeyLen()
	if err != nil {
		return nil, err
	}

	saltLen, err := c.profile.SaltLen()
	if err != nil {
		return nil, err
	}

	if masterKeyLen := len(masterKey); masterKeyLen != keyLen {
		return nil, fmt.Errorf("%w expected(%d) actual(%d)", errShortSrtpMasterKey, masterKey, keyLen)
	} else if masterSaltLen := len(masterSalt); masterSaltLen != saltLen {
		return nil, fmt.Errorf("%w expected(%d) actual(%d)", errShortSrtpMasterSalt, saltLen, masterSaltLen)
	}

	switch c.profile {
	case ProtectionProfileAeadAes128Gcm, ProtectionProfileAeadAes256Gcm:
		return newSrtpCipherAeadAesGcm(c.profile, masterKey, masterSalt, mki, encryptSRTP, encryptSRTCP)
	case ProtectionProfileAes128CmHmacSha1_32, ProtectionProfileAes128CmHmacSha1_80, ProtectionProfileAes256CmHmacSha1_32, ProtectionProfileAes256CmHmacSha1_80:
		return newSrtpCipherAesCmHmacSha1(c.profile, masterKey, masterSalt, mki, encryptSRTP, encryptSRTCP)
	case ProtectionProfileNullHmacSha1_32, ProtectionProfileNullHmacSha1_80:
		return newSrtpCipherAesCmHmacSha1(c.profile, masterKey, masterSalt, mki, false, false)
	default:
		return nil, fmt.Errorf("%w: %#v", errNoSuchSRTPProfile, c.profile)
	}
}

// RemoveMKI removes one of MKIs. You cannot remove last MKI and one used for encrypting RTP/RTCP packets.
// Operation is not thread-safe, you need to provide synchronization with decrypting packets.
func (c *Context) RemoveMKI(mki []byte) error {
	if _, ok := c.mkis[string(mki)]; !ok {
		return ErrMKINotFound
	}
	if bytes.Equal(mki, c.sendMKI) {
		return errMKIAlreadyInUse
	}
	delete(c.mkis, string(mki))
	return nil
}

// SetSendMKI switches MKI and cipher used for encrypting RTP/RTCP packets.
// Operation is not thread-safe, you need to provide synchronization with encrypting packets.
func (c *Context) SetSendMKI(mki []byte) error {
	cipher, ok := c.mkis[string(mki)]
	if !ok {
		return ErrMKINotFound
	}
	c.sendMKI = mki
	c.cipher = cipher
	return nil
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
