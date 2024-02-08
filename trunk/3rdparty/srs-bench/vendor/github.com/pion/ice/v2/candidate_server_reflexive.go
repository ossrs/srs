// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package ice

import "net"

// CandidateServerReflexive ...
type CandidateServerReflexive struct {
	candidateBase
}

// CandidateServerReflexiveConfig is the config required to create a new CandidateServerReflexive
type CandidateServerReflexiveConfig struct {
	CandidateID string
	Network     string
	Address     string
	Port        int
	Component   uint16
	Priority    uint32
	Foundation  string
	RelAddr     string
	RelPort     int
}

// NewCandidateServerReflexive creates a new server reflective candidate
func NewCandidateServerReflexive(config *CandidateServerReflexiveConfig) (*CandidateServerReflexive, error) {
	ip := net.ParseIP(config.Address)
	if ip == nil {
		return nil, ErrAddressParseFailed
	}

	networkType, err := determineNetworkType(config.Network, ip)
	if err != nil {
		return nil, err
	}

	candidateID := config.CandidateID
	if candidateID == "" {
		candidateID = globalCandidateIDGenerator.Generate()
	}

	return &CandidateServerReflexive{
		candidateBase: candidateBase{
			id:                 candidateID,
			networkType:        networkType,
			candidateType:      CandidateTypeServerReflexive,
			address:            config.Address,
			port:               config.Port,
			resolvedAddr:       &net.UDPAddr{IP: ip, Port: config.Port},
			component:          config.Component,
			foundationOverride: config.Foundation,
			priorityOverride:   config.Priority,
			relatedAddress: &CandidateRelatedAddress{
				Address: config.RelAddr,
				Port:    config.RelPort,
			},
			remoteCandidateCaches: map[AddrPort]Candidate{},
		},
	}, nil
}
