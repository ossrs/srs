package ice

import (
	"net"
)

// CandidateRelay ...
type CandidateRelay struct {
	candidateBase

	onClose func() error
}

// CandidateRelayConfig is the config required to create a new CandidateRelay
type CandidateRelayConfig struct {
	CandidateID string
	Network     string
	Address     string
	Port        int
	Component   uint16
	Priority    uint32
	Foundation  string
	RelAddr     string
	RelPort     int
	OnClose     func() error
}

// NewCandidateRelay creates a new relay candidate
func NewCandidateRelay(config *CandidateRelayConfig) (*CandidateRelay, error) {
	candidateID := config.CandidateID

	if candidateID == "" {
		candidateID = globalCandidateIDGenerator.Generate()
	}

	ip := net.ParseIP(config.Address)
	if ip == nil {
		return nil, ErrAddressParseFailed
	}

	networkType, err := determineNetworkType(config.Network, ip)
	if err != nil {
		return nil, err
	}

	return &CandidateRelay{
		candidateBase: candidateBase{
			id:                 candidateID,
			networkType:        networkType,
			candidateType:      CandidateTypeRelay,
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
		},
		onClose: config.OnClose,
	}, nil
}

func (c *CandidateRelay) close() error {
	err := c.candidateBase.close()
	if c.onClose != nil {
		err = c.onClose()
		c.onClose = nil
	}
	return err
}
