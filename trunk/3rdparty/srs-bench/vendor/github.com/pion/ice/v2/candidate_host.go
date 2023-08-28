// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package ice

import (
	"net"
	"strings"
)

// CandidateHost is a candidate of type host
type CandidateHost struct {
	candidateBase

	network string
}

// CandidateHostConfig is the config required to create a new CandidateHost
type CandidateHostConfig struct {
	CandidateID string
	Network     string
	Address     string
	Port        int
	Component   uint16
	Priority    uint32
	Foundation  string
	TCPType     TCPType
}

// NewCandidateHost creates a new host candidate
func NewCandidateHost(config *CandidateHostConfig) (*CandidateHost, error) {
	candidateID := config.CandidateID

	if candidateID == "" {
		candidateID = globalCandidateIDGenerator.Generate()
	}

	c := &CandidateHost{
		candidateBase: candidateBase{
			id:                    candidateID,
			address:               config.Address,
			candidateType:         CandidateTypeHost,
			component:             config.Component,
			port:                  config.Port,
			tcpType:               config.TCPType,
			foundationOverride:    config.Foundation,
			priorityOverride:      config.Priority,
			remoteCandidateCaches: map[AddrPort]Candidate{},
		},
		network: config.Network,
	}

	if !strings.HasSuffix(config.Address, ".local") {
		ip := net.ParseIP(config.Address)
		if ip == nil {
			return nil, ErrAddressParseFailed
		}

		if err := c.setIP(ip); err != nil {
			return nil, err
		}
	} else {
		// Until mDNS candidate is resolved assume it is UDPv4
		c.candidateBase.networkType = NetworkTypeUDP4
	}

	return c, nil
}

func (c *CandidateHost) setIP(ip net.IP) error {
	networkType, err := determineNetworkType(c.network, ip)
	if err != nil {
		return err
	}

	c.candidateBase.networkType = networkType
	c.candidateBase.resolvedAddr = createAddr(networkType, ip, c.port)

	return nil
}
