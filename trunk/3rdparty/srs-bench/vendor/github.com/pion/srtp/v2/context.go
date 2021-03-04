package srtp

import (
	"fmt"

	"github.com/pion/transport/replaydetector"
)

const (
	labelSRTPEncryption        = 0x00
	labelSRTPAuthenticationTag = 0x01
	labelSRTPSalt              = 0x02

	labelSRTCPEncryption        = 0x03
	labelSRTCPAuthenticationTag = 0x04
	labelSRTCPSalt              = 0x05

	maxROCDisorder    = 100
	maxSequenceNumber = 65535

	srtcpIndexSize = 4
)

// Encrypt/Decrypt state for a single SRTP SSRC
type srtpSSRCState struct {
	ssrc                 uint32
	rolloverCounter      uint32
	rolloverHasProcessed bool
	lastSequenceNumber   uint16
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
//   decCtx, err := srtp.CreateContext(key, salt, profile, srtp.SRTPReplayProtection(256))
//
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
	case ProtectionProfileAeadAes128Gcm:
		c.cipher, err = newSrtpCipherAeadAesGcm(masterKey, masterSalt)
	case ProtectionProfileAes128CmHmacSha1_80:
		c.cipher, err = newSrtpCipherAesCmHmacSha1(masterKey, masterSalt)
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
func (s *srtpSSRCState) nextRolloverCount(sequenceNumber uint16) (uint32, func()) {
	roc := s.rolloverCounter

	switch {
	case !s.rolloverHasProcessed:
	case sequenceNumber == 0: // We exactly hit the rollover count
		// Only update rolloverCounter if lastSequenceNumber is greater then maxROCDisorder
		// otherwise we already incremented for disorder
		if s.lastSequenceNumber > maxROCDisorder {
			roc++
		}
	case s.lastSequenceNumber < maxROCDisorder &&
		sequenceNumber > (maxSequenceNumber-maxROCDisorder):
		// Our last sequence number incremented because we crossed 0, but then our current number was within maxROCDisorder of the max
		// So we fell behind, drop to account for jitter
		roc--
	case sequenceNumber < maxROCDisorder &&
		s.lastSequenceNumber > (maxSequenceNumber-maxROCDisorder):
		// our current is within a maxROCDisorder of 0
		// and our last sequence number was a high sequence number, increment to account for jitter
		roc++
	}
	return roc, func() {
		s.rolloverHasProcessed = true
		s.lastSequenceNumber = sequenceNumber
		s.rolloverCounter = roc
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
	return s.rolloverCounter, true
}

// SetROC sets SRTP rollover counter value of specified SSRC.
func (c *Context) SetROC(ssrc uint32, roc uint32) {
	s := c.getSRTPSSRCState(ssrc)
	s.rolloverCounter = roc
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
