// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package vnet

import (
	"errors"
	"fmt"
	"net"
	"sync"
	"time"

	"github.com/pion/logging"
)

var (
	errNATRequriesMapping            = errors.New("1:1 NAT requires more than one mapping")
	errMismatchLengthIP              = errors.New("length mismtach between mappedIPs and localIPs")
	errNonUDPTranslationNotSupported = errors.New("non-udp translation is not supported yet")
	errNoAssociatedLocalAddress      = errors.New("no associated local address")
	errNoNATBindingFound             = errors.New("no NAT binding found")
	errHasNoPermission               = errors.New("has no permission")
)

// EndpointDependencyType defines a type of behavioral dependendency on the
// remote endpoint's IP address or port number. This is used for the two
// kinds of behaviors:
//   - Port mapping behavior
//   - Filtering behavior
//
// See: https://tools.ietf.org/html/rfc4787
type EndpointDependencyType uint8

const (
	// EndpointIndependent means the behavior is independent of the endpoint's address or port
	EndpointIndependent EndpointDependencyType = iota
	// EndpointAddrDependent means the behavior is dependent on the endpoint's address
	EndpointAddrDependent
	// EndpointAddrPortDependent means the behavior is dependent on the endpoint's address and port
	EndpointAddrPortDependent
)

// NATMode defines basic behavior of the NAT
type NATMode uint8

const (
	// NATModeNormal means the NAT behaves as a standard NAPT (RFC 2663).
	NATModeNormal NATMode = iota
	// NATModeNAT1To1 exhibits 1:1 DNAT where the external IP address is statically mapped to
	// a specific local IP address with port number is preserved always between them.
	// When this mode is selected, MappingBehavior, FilteringBehavior, PortPreservation and
	// MappingLifeTime of NATType are ignored.
	NATModeNAT1To1
)

const (
	defaultNATMappingLifeTime = 30 * time.Second
)

// NATType has a set of parameters that define the behavior of NAT.
type NATType struct {
	Mode              NATMode
	MappingBehavior   EndpointDependencyType
	FilteringBehavior EndpointDependencyType
	Hairpinning       bool // Not implemented yet
	PortPreservation  bool // Not implemented yet
	MappingLifeTime   time.Duration
}

type natConfig struct {
	name          string
	natType       NATType
	mappedIPs     []net.IP // mapped IPv4
	localIPs      []net.IP // local IPv4, required only when the mode is NATModeNAT1To1
	loggerFactory logging.LoggerFactory
}

type mapping struct {
	proto   string              // "udp" or "tcp"
	local   string              // "<local-ip>:<local-port>"
	mapped  string              // "<mapped-ip>:<mapped-port>"
	bound   string              // key: "[<remote-ip>[:<remote-port>]]"
	filters map[string]struct{} // key: "[<remote-ip>[:<remote-port>]]"
	expires time.Time           // time to expire
}

type networkAddressTranslator struct {
	name           string
	natType        NATType
	mappedIPs      []net.IP            // mapped IPv4
	localIPs       []net.IP            // local IPv4, required only when the mode is NATModeNAT1To1
	outboundMap    map[string]*mapping // key: "<proto>:<local-ip>:<local-port>[:remote-ip[:remote-port]]
	inboundMap     map[string]*mapping // key: "<proto>:<mapped-ip>:<mapped-port>"
	udpPortCounter int
	mutex          sync.RWMutex
	log            logging.LeveledLogger
}

func newNAT(config *natConfig) (*networkAddressTranslator, error) {
	natType := config.natType

	if natType.Mode == NATModeNAT1To1 {
		// 1:1 NAT behavior
		natType.MappingBehavior = EndpointIndependent
		natType.FilteringBehavior = EndpointIndependent
		natType.PortPreservation = true
		natType.MappingLifeTime = 0

		if len(config.mappedIPs) == 0 {
			return nil, errNATRequriesMapping
		}
		if len(config.mappedIPs) != len(config.localIPs) {
			return nil, errMismatchLengthIP
		}
	} else {
		// Normal (NAPT) behavior
		natType.Mode = NATModeNormal
		if natType.MappingLifeTime == 0 {
			natType.MappingLifeTime = defaultNATMappingLifeTime
		}
	}

	return &networkAddressTranslator{
		name:        config.name,
		natType:     natType,
		mappedIPs:   config.mappedIPs,
		localIPs:    config.localIPs,
		outboundMap: map[string]*mapping{},
		inboundMap:  map[string]*mapping{},
		log:         config.loggerFactory.NewLogger("vnet"),
	}, nil
}

func (n *networkAddressTranslator) getPairedMappedIP(locIP net.IP) net.IP {
	for i, ip := range n.localIPs {
		if ip.Equal(locIP) {
			return n.mappedIPs[i]
		}
	}
	return nil
}

func (n *networkAddressTranslator) getPairedLocalIP(mappedIP net.IP) net.IP {
	for i, ip := range n.mappedIPs {
		if ip.Equal(mappedIP) {
			return n.localIPs[i]
		}
	}
	return nil
}

func (n *networkAddressTranslator) translateOutbound(from Chunk) (Chunk, error) {
	n.mutex.Lock()
	defer n.mutex.Unlock()

	to := from.Clone()

	if from.Network() == udp {
		if n.natType.Mode == NATModeNAT1To1 {
			// 1:1 NAT behavior
			srcAddr := from.SourceAddr().(*net.UDPAddr) //nolint:forcetypeassert
			srcIP := n.getPairedMappedIP(srcAddr.IP)
			if srcIP == nil {
				n.log.Debugf("[%s] drop outbound chunk %s with not route", n.name, from.String())
				return nil, nil // nolint:nilnil
			}
			srcPort := srcAddr.Port
			if err := to.setSourceAddr(fmt.Sprintf("%s:%d", srcIP.String(), srcPort)); err != nil {
				return nil, err
			}
		} else {
			// Normal (NAPT) behavior
			var bound, filterKey string
			switch n.natType.MappingBehavior {
			case EndpointIndependent:
				bound = ""
			case EndpointAddrDependent:
				bound = from.getDestinationIP().String()
			case EndpointAddrPortDependent:
				bound = from.DestinationAddr().String()
			}

			switch n.natType.FilteringBehavior {
			case EndpointIndependent:
				filterKey = ""
			case EndpointAddrDependent:
				filterKey = from.getDestinationIP().String()
			case EndpointAddrPortDependent:
				filterKey = from.DestinationAddr().String()
			}

			oKey := fmt.Sprintf("udp:%s:%s", from.SourceAddr().String(), bound)

			m := n.findOutboundMapping(oKey)
			if m == nil {
				// Create a new mapping
				mappedPort := 0xC000 + n.udpPortCounter
				n.udpPortCounter++

				m = &mapping{
					proto:   from.SourceAddr().Network(),
					local:   from.SourceAddr().String(),
					bound:   bound,
					mapped:  fmt.Sprintf("%s:%d", n.mappedIPs[0].String(), mappedPort),
					filters: map[string]struct{}{},
					expires: time.Now().Add(n.natType.MappingLifeTime),
				}

				n.outboundMap[oKey] = m

				iKey := fmt.Sprintf("udp:%s", m.mapped)

				n.log.Debugf("[%s] created a new NAT binding oKey=%s iKey=%s",
					n.name,
					oKey,
					iKey)

				m.filters[filterKey] = struct{}{}
				n.log.Debugf("[%s] permit access from %s to %s", n.name, filterKey, m.mapped)
				n.inboundMap[iKey] = m
			} else if _, ok := m.filters[filterKey]; !ok {
				n.log.Debugf("[%s] permit access from %s to %s", n.name, filterKey, m.mapped)
				m.filters[filterKey] = struct{}{}
			}

			if err := to.setSourceAddr(m.mapped); err != nil {
				return nil, err
			}
		}

		n.log.Debugf("[%s] translate outbound chunk from %s to %s", n.name, from.String(), to.String())

		return to, nil
	}

	return nil, errNonUDPTranslationNotSupported
}

func (n *networkAddressTranslator) translateInbound(from Chunk) (Chunk, error) {
	n.mutex.Lock()
	defer n.mutex.Unlock()

	to := from.Clone()

	if from.Network() == udp {
		if n.natType.Mode == NATModeNAT1To1 {
			// 1:1 NAT behavior
			dstAddr := from.DestinationAddr().(*net.UDPAddr) //nolint:forcetypeassert
			dstIP := n.getPairedLocalIP(dstAddr.IP)
			if dstIP == nil {
				return nil, fmt.Errorf("drop %s as %w", from.String(), errNoAssociatedLocalAddress)
			}
			dstPort := from.DestinationAddr().(*net.UDPAddr).Port //nolint:forcetypeassert
			if err := to.setDestinationAddr(fmt.Sprintf("%s:%d", dstIP, dstPort)); err != nil {
				return nil, err
			}
		} else {
			// Normal (NAPT) behavior
			iKey := fmt.Sprintf("udp:%s", from.DestinationAddr().String())
			m := n.findInboundMapping(iKey)
			if m == nil {
				return nil, fmt.Errorf("drop %s as %w", from.String(), errNoNATBindingFound)
			}

			var filterKey string
			switch n.natType.FilteringBehavior {
			case EndpointIndependent:
				filterKey = ""
			case EndpointAddrDependent:
				filterKey = from.getSourceIP().String()
			case EndpointAddrPortDependent:
				filterKey = from.SourceAddr().String()
			}

			if _, ok := m.filters[filterKey]; !ok {
				return nil, fmt.Errorf("drop %s as the remote %s %w", from.String(), filterKey, errHasNoPermission)
			}

			// See RFC 4847 Section 4.3.  Mapping Refresh
			// a) Inbound refresh may be useful for applications with no outgoing
			//   UDP traffic.  However, allowing inbound refresh may allow an
			//   external attacker or misbehaving application to keep a mapping
			//   alive indefinitely.  This may be a security risk.  Also, if the
			//   process is repeated with different ports, over time, it could
			//   use up all the ports on the NAT.

			if err := to.setDestinationAddr(m.local); err != nil {
				return nil, err
			}
		}

		n.log.Debugf("[%s] translate inbound chunk from %s to %s", n.name, from.String(), to.String())

		return to, nil
	}

	return nil, errNonUDPTranslationNotSupported
}

// caller must hold the mutex
func (n *networkAddressTranslator) findOutboundMapping(oKey string) *mapping {
	now := time.Now()

	m, ok := n.outboundMap[oKey]
	if ok {
		// check if this mapping is expired
		if now.After(m.expires) {
			n.removeMapping(m)
			m = nil // expired
		} else {
			m.expires = time.Now().Add(n.natType.MappingLifeTime)
		}
	}

	return m
}

// caller must hold the mutex
func (n *networkAddressTranslator) findInboundMapping(iKey string) *mapping {
	now := time.Now()
	m, ok := n.inboundMap[iKey]
	if !ok {
		return nil
	}

	// check if this mapping is expired
	if now.After(m.expires) {
		n.removeMapping(m)
		return nil
	}

	return m
}

// caller must hold the mutex
func (n *networkAddressTranslator) removeMapping(m *mapping) {
	oKey := fmt.Sprintf("%s:%s:%s", m.proto, m.local, m.bound)
	iKey := fmt.Sprintf("%s:%s", m.proto, m.mapped)

	delete(n.outboundMap, oKey)
	delete(n.inboundMap, iKey)
}
