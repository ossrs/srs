// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package ice

import (
	"net"
	"net/netip"
)

// CandidateRelay ...
type CandidateRelay struct {
	candidateBase

	relayProtocol string
	onClose       func() error
}

// CandidateRelayConfig is the config required to create a new CandidateRelay.
type CandidateRelayConfig struct {
	CandidateID   string
	Network       string
	Address       string
	Port          int
	Component     uint16
	Priority      uint32
	Foundation    string
	RelAddr       string
	RelPort       int
	RelayProtocol string
	OnClose       func() error
}

// NewCandidateRelay creates a new relay candidate.
func NewCandidateRelay(config *CandidateRelayConfig) (*CandidateRelay, error) {
	candidateID := config.CandidateID

	if candidateID == "" {
		candidateID = globalCandidateIDGenerator.Generate()
	}

	ipAddr, err := netip.ParseAddr(config.Address)
	if err != nil {
		return nil, err
	}

	networkType, err := determineNetworkType(config.Network, ipAddr)
	if err != nil {
		return nil, err
	}

	return &CandidateRelay{
		candidateBase: candidateBase{
			id:            candidateID,
			networkType:   networkType,
			candidateType: CandidateTypeRelay,
			address:       config.Address,
			port:          config.Port,
			resolvedAddr: &net.UDPAddr{
				IP:   ipAddr.AsSlice(),
				Port: config.Port,
				Zone: ipAddr.Zone(),
			},
			component:          config.Component,
			foundationOverride: config.Foundation,
			priorityOverride:   config.Priority,
			relatedAddress: &CandidateRelatedAddress{
				Address: config.RelAddr,
				Port:    config.RelPort,
			},
			remoteCandidateCaches: map[AddrPort]Candidate{},
		},
		relayProtocol: config.RelayProtocol,
		onClose:       config.OnClose,
	}, nil
}

// LocalPreference returns the local preference for this candidate.
func (c *CandidateRelay) LocalPreference() uint16 {
	// These preference values come from libwebrtc
	// https://github.com/mozilla/libwebrtc/blob/1389c76d9c79839a2ca069df1db48aa3f2e6a1ac/p2p/base/turn_port.cc#L61
	var relayPreference uint16
	switch c.relayProtocol {
	case relayProtocolTLS, relayProtocolDTLS:
		relayPreference = 2
	case tcp:
		relayPreference = 1
	default:
		relayPreference = 0
	}

	return c.candidateBase.LocalPreference() + relayPreference
}

// RelayProtocol returns the protocol used between the endpoint and the relay server.
func (c *CandidateRelay) RelayProtocol() string {
	return c.relayProtocol
}

func (c *CandidateRelay) close() error {
	err := c.candidateBase.close()
	if c.onClose != nil {
		err = c.onClose()
		c.onClose = nil
	}

	return err
}

func (c *CandidateRelay) copy() (Candidate, error) {
	cc, err := c.candidateBase.copy()
	if err != nil {
		return nil, err
	}

	if ccr, ok := cc.(*CandidateRelay); ok {
		ccr.relayProtocol = c.relayProtocol
	}

	return cc, nil
}
