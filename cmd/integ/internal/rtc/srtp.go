// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package rtc

import (
	"errors"

	"github.com/pion/dtls/v3"
	"github.com/pion/srtp/v3"
)

// An srtpPair protects what a raw session sends with its keys from the DTLS
// handshake, and unprotects what it receives with the peer's, RFC 5764.
type srtpPair struct {
	out, in *srtp.Context
}

// newSRTPPair makes the SRTP of a DTLS connection whose handshake is done,
// as the DTLS client when isClient is set.
func newSRTPPair(c *dtls.Conn, isClient bool) (*srtpPair, error) {
	state, ok := c.ConnectionState()
	if !ok {
		return nil, errors.New("the DTLS handshake is not done")
	}
	cfg := &srtp.Config{Profile: srtp.ProtectionProfileAes128CmHmacSha1_80}
	if err := cfg.ExtractSessionKeysFromDTLS(&state, isClient); err != nil {
		return nil, err
	}
	out, err := srtp.CreateContext(cfg.Keys.LocalMasterKey, cfg.Keys.LocalMasterSalt, cfg.Profile)
	if err != nil {
		return nil, err
	}
	in, err := srtp.CreateContext(cfg.Keys.RemoteMasterKey, cfg.Keys.RemoteMasterSalt, cfg.Profile)
	if err != nil {
		return nil, err
	}
	return &srtpPair{out: out, in: in}, nil
}

// protect returns an RTP or RTCP packet protected. SRTP takes an RTP packet
// only with a header it can parse, and RTCP with a first header of version
// 2; what comes after, the payload or the rest of a compound packet, may be
// anything.
func (p *srtpPair) protect(b []byte) ([]byte, error) {
	if isRTCP(b) {
		return p.out.EncryptRTCP(nil, b, nil)
	}
	return p.out.EncryptRTP(nil, b, nil)
}

// unprotect returns a protected RTP or RTCP packet in clear.
func (p *srtpPair) unprotect(b []byte) ([]byte, error) {
	if isRTCP(b) {
		return p.in.DecryptRTCP(nil, b, nil)
	}
	return p.in.DecryptRTP(nil, b, nil)
}
