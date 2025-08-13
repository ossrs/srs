// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package ice

import (
	"net"
	"time"

	"github.com/pion/logging"
	"github.com/pion/stun/v3"
	"github.com/pion/transport/v3"
	"golang.org/x/net/proxy"
)

const (
	// defaultCheckInterval is the interval at which the agent performs candidate checks in the connecting phase.
	defaultCheckInterval = 200 * time.Millisecond

	// keepaliveInterval used to keep candidates alive.
	defaultKeepaliveInterval = 2 * time.Second

	// defaultDisconnectedTimeout is the default time till an Agent transitions disconnected.
	defaultDisconnectedTimeout = 5 * time.Second

	// defaultFailedTimeout is the default time till an Agent transitions to failed after disconnected.
	defaultFailedTimeout = 25 * time.Second

	// defaultHostAcceptanceMinWait is the wait time before nominating a host candidate.
	defaultHostAcceptanceMinWait = 0

	// defaultSrflxAcceptanceMinWait is the wait time before nominating a srflx candidate.
	defaultSrflxAcceptanceMinWait = 500 * time.Millisecond

	// defaultPrflxAcceptanceMinWait is the wait time before nominating a prflx candidate.
	defaultPrflxAcceptanceMinWait = 1000 * time.Millisecond

	// defaultRelayAcceptanceMinWait is the wait time before nominating a relay candidate.
	defaultRelayAcceptanceMinWait = 2000 * time.Millisecond

	// defaultSTUNGatherTimeout is the wait time for STUN responses.
	defaultSTUNGatherTimeout = 5 * time.Second

	// defaultMaxBindingRequests is the maximum number of binding requests before considering a pair failed.
	defaultMaxBindingRequests = 7

	// TCPPriorityOffset is a number which is subtracted from the default (UDP) candidate type preference
	// for host, srflx and prfx candidate types.
	defaultTCPPriorityOffset = 27

	// maxBufferSize is the number of bytes that can be buffered before we start to error.
	maxBufferSize = 1000 * 1000 // 1MB

	// maxBindingRequestTimeout is the wait time before binding requests can be deleted.
	maxBindingRequestTimeout = 4000 * time.Millisecond
)

func defaultCandidateTypes() []CandidateType {
	return []CandidateType{CandidateTypeHost, CandidateTypeServerReflexive, CandidateTypeRelay}
}

// AgentConfig collects the arguments to ice.Agent construction into
// a single structure, for future-proofness of the interface.
type AgentConfig struct {
	Urls []*stun.URI

	// PortMin and PortMax are optional. Leave them 0 for the default UDP port allocation strategy.
	PortMin uint16
	PortMax uint16

	// LocalUfrag and LocalPwd values used to perform connectivity
	// checks.  The values MUST be unguessable, with at least 128 bits of
	// random number generator output used to generate the password, and
	// at least 24 bits of output to generate the username fragment.
	LocalUfrag string
	LocalPwd   string

	// MulticastDNSMode controls mDNS behavior for the ICE agent
	MulticastDNSMode MulticastDNSMode

	// MulticastDNSHostName controls the hostname for this agent. If none is specified a random one will be generated
	MulticastDNSHostName string

	// DisconnectedTimeout defaults to 5 seconds when this property is nil.
	// If the duration is 0, the ICE Agent will never go to disconnected
	DisconnectedTimeout *time.Duration

	// FailedTimeout defaults to 25 seconds when this property is nil.
	// If the duration is 0, we will never go to failed.
	FailedTimeout *time.Duration

	// KeepaliveInterval determines how often should we send ICE
	// keepalives (should be less then connectiontimeout above)
	// when this is nil, it defaults to 2 seconds.
	// A keepalive interval of 0 means we never send keepalive packets
	KeepaliveInterval *time.Duration

	// CheckInterval controls how often our task loop runs when in the
	// connecting state.
	CheckInterval *time.Duration

	// NetworkTypes is an optional configuration for disabling or enabling
	// support for specific network types.
	NetworkTypes []NetworkType

	// CandidateTypes is an optional configuration for disabling or enabling
	// support for specific candidate types.
	CandidateTypes []CandidateType

	LoggerFactory logging.LoggerFactory

	// MaxBindingRequests is the max amount of binding requests the agent will send
	// over a candidate pair for validation or nomination, if after MaxBindingRequests
	// the candidate is yet to answer a binding request or a nomination we set the pair as failed
	MaxBindingRequests *uint16

	// Lite agents do not perform connectivity check and only provide host candidates.
	Lite bool

	// NAT1To1IPCandidateType is used along with NAT1To1IPs to specify which candidate type
	// the 1:1 NAT IP addresses should be mapped to.
	// If unspecified or CandidateTypeHost, NAT1To1IPs are used to replace host candidate IPs.
	// If CandidateTypeServerReflexive, it will insert a srflx candidate (as if it was derived
	// from a STUN server) with its port number being the one for the actual host candidate.
	// Other values will result in an error.
	NAT1To1IPCandidateType CandidateType

	// NAT1To1IPs contains a list of public IP addresses that are to be used as a host
	// candidate or srflx candidate. This is used typically for servers that are behind
	// 1:1 D-NAT (e.g. AWS EC2 instances) and to eliminate the need of server reflexive
	// candidate gathering.
	NAT1To1IPs []string

	// HostAcceptanceMinWait specify a minimum wait time before selecting host candidates
	HostAcceptanceMinWait *time.Duration
	// SrflxAcceptanceMinWait specify a minimum wait time before selecting srflx candidates
	SrflxAcceptanceMinWait *time.Duration
	// PrflxAcceptanceMinWait specify a minimum wait time before selecting prflx candidates
	PrflxAcceptanceMinWait *time.Duration
	// RelayAcceptanceMinWait specify a minimum wait time before selecting relay candidates
	RelayAcceptanceMinWait *time.Duration
	// STUNGatherTimeout specify a minimum wait time for STUN responses
	STUNGatherTimeout *time.Duration

	// Net is the our abstracted network interface for internal development purpose only
	// (see https://github.com/pion/transport)
	Net transport.Net

	// InterfaceFilter is a function that you can use in order to whitelist or blacklist
	// the interfaces which are used to gather ICE candidates.
	InterfaceFilter func(string) (keep bool)

	// IPFilter is a function that you can use in order to whitelist or blacklist
	// the ips which are used to gather ICE candidates.
	IPFilter func(net.IP) (keep bool)

	// InsecureSkipVerify controls if self-signed certificates are accepted when connecting
	// to TURN servers via TLS or DTLS
	InsecureSkipVerify bool

	// TCPMux will be used for multiplexing incoming TCP connections for ICE TCP.
	// Currently only passive candidates are supported. This functionality is
	// experimental and the API might change in the future.
	TCPMux TCPMux

	// UDPMux is used for multiplexing multiple incoming UDP connections on a single port
	// when this is set, the agent ignores PortMin and PortMax configurations and will
	// defer to UDPMux for incoming connections
	UDPMux UDPMux

	// UDPMuxSrflx is used for multiplexing multiple incoming UDP connections of server reflexive candidates
	// on a single port when this is set, the agent ignores PortMin and PortMax configurations and will
	// defer to UDPMuxSrflx for incoming connections
	// It embeds UDPMux to do the actual connection multiplexing
	UDPMuxSrflx UniversalUDPMux

	// Proxy Dialer is a dialer that should be implemented by the user based on golang.org/x/net/proxy
	// dial interface in order to support corporate proxies
	ProxyDialer proxy.Dialer

	// Deprecated: AcceptAggressiveNomination always enabled.
	AcceptAggressiveNomination bool

	// Include loopback addresses in the candidate list.
	IncludeLoopback bool

	// TCPPriorityOffset is a number which is subtracted from the default (UDP) candidate type preference
	// for host, srflx and prfx candidate types. It helps to configure relative preference of UDP candidates
	// against TCP ones. Relay candidates for TCP and UDP are always 0 and not affected by this setting.
	// When this is nil, defaultTCPPriorityOffset is used.
	TCPPriorityOffset *uint16

	// DisableActiveTCP can be used to disable Active TCP candidates. Otherwise when TCP is enabled
	// Active TCP candidates will be created when a new passive TCP remote candidate is added.
	DisableActiveTCP bool

	// BindingRequestHandler allows applications to perform logic on incoming STUN Binding Requests
	// This was implemented to allow users to
	// * Log incoming Binding Requests for debugging
	// * Implement draft-thatcher-ice-renomination
	// * Implement custom CandidatePair switching logic
	BindingRequestHandler func(m *stun.Message, local, remote Candidate, pair *CandidatePair) bool

	// EnableUseCandidateCheckPriority can be used to enable checking for equal or higher priority to
	// switch selected candidate pair if the peer requests USE-CANDIDATE and agent is a lite agent.
	// This is disabled by default, i. e. when peer requests USE-CANDIDATE, the selected pair will be
	// switched to that irrespective of relative priority between current selected pair
	// and priority of the pair being switched to.
	EnableUseCandidateCheckPriority bool
}

// initWithDefaults populates an agent and falls back to defaults if fields are unset.
func (config *AgentConfig) initWithDefaults(agent *Agent) { //nolint:cyclop
	if config.MaxBindingRequests == nil {
		agent.maxBindingRequests = defaultMaxBindingRequests
	} else {
		agent.maxBindingRequests = *config.MaxBindingRequests
	}

	if config.HostAcceptanceMinWait == nil {
		agent.hostAcceptanceMinWait = defaultHostAcceptanceMinWait
	} else {
		agent.hostAcceptanceMinWait = *config.HostAcceptanceMinWait
	}

	if config.SrflxAcceptanceMinWait == nil {
		agent.srflxAcceptanceMinWait = defaultSrflxAcceptanceMinWait
	} else {
		agent.srflxAcceptanceMinWait = *config.SrflxAcceptanceMinWait
	}

	if config.PrflxAcceptanceMinWait == nil {
		agent.prflxAcceptanceMinWait = defaultPrflxAcceptanceMinWait
	} else {
		agent.prflxAcceptanceMinWait = *config.PrflxAcceptanceMinWait
	}

	if config.RelayAcceptanceMinWait == nil {
		agent.relayAcceptanceMinWait = defaultRelayAcceptanceMinWait
	} else {
		agent.relayAcceptanceMinWait = *config.RelayAcceptanceMinWait
	}

	if config.STUNGatherTimeout == nil {
		agent.stunGatherTimeout = defaultSTUNGatherTimeout
	} else {
		agent.stunGatherTimeout = *config.STUNGatherTimeout
	}

	if config.TCPPriorityOffset == nil {
		agent.tcpPriorityOffset = defaultTCPPriorityOffset
	} else {
		agent.tcpPriorityOffset = *config.TCPPriorityOffset
	}

	if config.DisconnectedTimeout == nil {
		agent.disconnectedTimeout = defaultDisconnectedTimeout
	} else {
		agent.disconnectedTimeout = *config.DisconnectedTimeout
	}

	if config.FailedTimeout == nil {
		agent.failedTimeout = defaultFailedTimeout
	} else {
		agent.failedTimeout = *config.FailedTimeout
	}

	if config.KeepaliveInterval == nil {
		agent.keepaliveInterval = defaultKeepaliveInterval
	} else {
		agent.keepaliveInterval = *config.KeepaliveInterval
	}

	if config.CheckInterval == nil {
		agent.checkInterval = defaultCheckInterval
	} else {
		agent.checkInterval = *config.CheckInterval
	}

	if len(config.CandidateTypes) == 0 {
		agent.candidateTypes = defaultCandidateTypes()
	} else {
		agent.candidateTypes = config.CandidateTypes
	}
}

func (config *AgentConfig) initExtIPMapping(agent *Agent) error { //nolint:cyclop
	var err error
	agent.extIPMapper, err = newExternalIPMapper(config.NAT1To1IPCandidateType, config.NAT1To1IPs)
	if err != nil {
		return err
	}
	if agent.extIPMapper == nil {
		return nil // This may happen when config.NAT1To1IPs is an empty array
	}
	if agent.extIPMapper.candidateType == CandidateTypeHost { //nolint:nestif
		if agent.mDNSMode == MulticastDNSModeQueryAndGather {
			return ErrMulticastDNSWithNAT1To1IPMapping
		}
		candiHostEnabled := false
		for _, candiType := range agent.candidateTypes {
			if candiType == CandidateTypeHost {
				candiHostEnabled = true

				break
			}
		}
		if !candiHostEnabled {
			return ErrIneffectiveNAT1To1IPMappingHost
		}
	} else if agent.extIPMapper.candidateType == CandidateTypeServerReflexive {
		candiSrflxEnabled := false
		for _, candiType := range agent.candidateTypes {
			if candiType == CandidateTypeServerReflexive {
				candiSrflxEnabled = true

				break
			}
		}
		if !candiSrflxEnabled {
			return ErrIneffectiveNAT1To1IPMappingSrflx
		}
	}

	return nil
}
