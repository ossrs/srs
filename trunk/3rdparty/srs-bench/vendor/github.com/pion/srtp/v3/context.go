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
)

// Encrypt/Decrypt state for a single SRTP SSRC.
type srtpSSRCState struct {
	ssrc                 uint32
	rolloverHasProcessed bool
	index                uint64
	replayDetector       replaydetector.ReplayDetector
}

// Encrypt/Decrypt state for a single SRTCP SSRC.
type srtcpSSRCState struct {
	srtcpIndex     uint32
	ssrc           uint32
	replayDetector replaydetector.ReplayDetector
}

// RCCMode is the mode of Roll-over Counter Carrying Transform from RFC 4771.
type RCCMode int

const (
	// RCCModeNone is the default mode.
	RCCModeNone RCCMode = iota
	// RCCMode1 is RCCm1 mode from RFC 4771. In this mode ROC and truncated auth tag is sent every R-th packet,
	// and no auth tag in other ones. This mode is not supported by pion/srtp.
	RCCMode1
	// RCCMode2 is RCCm2 mode from RFC 4771. In this mode ROC and truncated auth tag is sent every R-th packet,
	// and full auth tag in other ones. This mode is supported for AES-CM and NULL profiles only.
	RCCMode2
	// RCCMode3 is RCCm3 mode from RFC 4771. In this mode ROC is sent every R-th packet (without truncated auth tag),
	// and no auth tag in other ones. This mode is supported for AES-GCM profiles only.
	RCCMode3
)

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

	// Master Key Identifier used for encrypting RTP/RTCP packets. Set to nil if MKI is not enabled.
	sendMKI []byte
	// Master Key Identifier to cipher mapping. Used for decrypting packets. Empty if MKI is not enabled.
	mkis map[string]srtpCipher

	encryptSRTP  bool
	encryptSRTCP bool

	rccMode         RCCMode
	rocTransmitRate uint16

	authTagRTPLen *int
}

// CreateContext creates a new SRTP Context.
//
// CreateContext receives variable number of ContextOption-s.
// Passing multiple options which set the same parameter let the last one valid.
// Following example create SRTP Context with replay protection with window size of 256.
//
//	decCtx, err := srtp.CreateContext(key, salt, profile, srtp.SRTPReplayProtection(256))
func CreateContext(
	masterKey, masterSalt []byte,
	profile ProtectionProfile,
	opts ...ContextOption,
) (c *Context, err error) {
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

	if err = c.checkRCCMode(); err != nil {
		return nil, err
	}

	if c.authTagRTPLen != nil {
		var authKeyLen int
		authKeyLen, err = c.profile.AuthKeyLen()
		if err != nil {
			return nil, err
		}
		if *c.authTagRTPLen > authKeyLen {
			return nil, errTooLongSRTPAuthTag
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

// AddCipherForMKI adds new MKI with associated masker key and salt.
// Context must be created with MasterKeyIndicator option
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
		return nil, fmt.Errorf("%w expected(%d) actual(%d)", errShortSrtpMasterKey, keyLen, masterKey)
	} else if masterSaltLen := len(masterSalt); masterSaltLen != saltLen {
		return nil, fmt.Errorf("%w expected(%d) actual(%d)", errShortSrtpMasterSalt, saltLen, masterSaltLen)
	}

	profileWithArgs := protectionProfileWithArgs{
		ProtectionProfile: c.profile,
		authTagRTPLen:     c.authTagRTPLen,
	}

	switch c.profile {
	case ProtectionProfileAeadAes128Gcm, ProtectionProfileAeadAes256Gcm:
		return newSrtpCipherAeadAesGcm(profileWithArgs, masterKey, masterSalt, mki, encryptSRTP, encryptSRTCP)
	case ProtectionProfileAes128CmHmacSha1_32,
		ProtectionProfileAes128CmHmacSha1_80,
		ProtectionProfileAes256CmHmacSha1_32,
		ProtectionProfileAes256CmHmacSha1_80:
		return newSrtpCipherAesCmHmacSha1(profileWithArgs, masterKey, masterSalt, mki, encryptSRTP, encryptSRTCP)
	case ProtectionProfileNullHmacSha1_32, ProtectionProfileNullHmacSha1_80:
		return newSrtpCipherAesCmHmacSha1(profileWithArgs, masterKey, masterSalt, mki, false, false)
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
func (s *srtpSSRCState) nextRolloverCount(sequenceNumber uint16) (roc uint32, diff int64, overflow bool) {
	seq := int32(sequenceNumber)
	localRoc := uint32(s.index >> 16)            //nolint:gosec // G115
	localSeq := int32(s.index & (seqNumMax - 1)) //nolint:gosec // G115

	guessRoc := localRoc
	var difference int32

	if s.rolloverHasProcessed { //nolint:nestif
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

	return guessRoc, int64(difference), (guessRoc == 0 && localRoc == maxROC)
}

func (s *srtpSSRCState) updateRolloverCount(sequenceNumber uint16, difference int64, hasRemoteRoc bool,
	remoteRoc uint32,
) {
	switch {
	case hasRemoteRoc:
		s.index = (uint64(remoteRoc) << 16) | uint64(sequenceNumber)
		s.rolloverHasProcessed = true
	case !s.rolloverHasProcessed:
		s.index |= uint64(sequenceNumber)
		s.rolloverHasProcessed = true
	case difference > 0:
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

	return uint32(s.index >> 16), true //nolint:gosec // G115
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

//nolint:cyclop
func (c *Context) checkRCCMode() error {
	if c.rccMode == RCCModeNone {
		return nil
	}

	if c.rocTransmitRate == 0 {
		return errZeroRocTransmitRate
	}

	switch c.profile {
	case ProtectionProfileAeadAes128Gcm, ProtectionProfileAeadAes256Gcm:
		// AEAD profiles support RCCMode3 only
		if c.rccMode != RCCMode3 {
			return errUnsupportedRccMode
		}

	case ProtectionProfileAes128CmHmacSha1_32,
		ProtectionProfileAes256CmHmacSha1_32,
		ProtectionProfileNullHmacSha1_32:
		if c.authTagRTPLen == nil {
			// ROC completely replaces auth tag for _32 profiles. If you really want to use 4-byte
			// SRTP auth tag with RCC, use SRTPAuthenticationTagLength(4) option.
			return errTooShortSRTPAuthTag
		}

		fallthrough // Checks below are common for _32 and _80 profiles.

	case ProtectionProfileAes128CmHmacSha1_80,
		ProtectionProfileAes256CmHmacSha1_80,
		ProtectionProfileNullHmacSha1_80:
		// AES-CM and NULL profiles support RCCMode2 only
		if c.rccMode != RCCMode2 {
			return errUnsupportedRccMode
		}
		if c.authTagRTPLen != nil && *c.authTagRTPLen < 4 {
			return errTooShortSRTPAuthTag
		}

	default:
		return errUnsupportedRccMode
	}

	return nil
}
