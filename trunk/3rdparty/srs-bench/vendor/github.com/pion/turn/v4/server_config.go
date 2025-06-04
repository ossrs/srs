// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package turn

import (
	"crypto/md5" //nolint:gosec,gci
	"fmt"
	"net"
	"strings"
	"time"

	"github.com/pion/logging"
)

// RelayAddressGenerator is used to generate a RelayAddress when creating an allocation.
// You can use one of the provided ones or provide your own.
type RelayAddressGenerator interface {
	// Validate confirms that the RelayAddressGenerator is properly initialized
	Validate() error

	// Allocate a PacketConn (UDP) RelayAddress
	AllocatePacketConn(network string, requestedPort int) (net.PacketConn, net.Addr, error)

	// Allocate a Conn (TCP) RelayAddress
	AllocateConn(network string, requestedPort int) (net.Conn, net.Addr, error)
}

// PermissionHandler is a callback to filter incoming CreatePermission and ChannelBindRequest
// requests based on the client IP address and port and the peer IP address the client intends to
// connect to. If the client is behind a NAT then the filter acts on the server reflexive
// ("mapped") address instead of the real client IP address and port. Note that TURN permissions
// are per-allocation and per-peer-IP-address, to mimic the address-restricted filtering mechanism
// of NATs that comply with [RFC4787], see https://tools.ietf.org/html/rfc5766#section-2.3.
type PermissionHandler func(clientAddr net.Addr, peerIP net.IP) (ok bool)

// DefaultPermissionHandler is convince function that grants permission to all peers
func DefaultPermissionHandler(net.Addr, net.IP) (ok bool) {
	return true
}

// PacketConnConfig is a single net.PacketConn to listen/write on. This will be used for UDP listeners
type PacketConnConfig struct {
	PacketConn net.PacketConn

	// When an allocation is generated the RelayAddressGenerator
	// creates the net.PacketConn and returns the IP/Port it is available at
	RelayAddressGenerator RelayAddressGenerator

	// PermissionHandler is a callback to filter peer addresses. Can be set as nil, in which
	// case the DefaultPermissionHandler is automatically instantiated to admit all peer
	// connections
	PermissionHandler PermissionHandler
}

func (c *PacketConnConfig) validate() error {
	if c.PacketConn == nil {
		return errConnUnset
	}

	if c.RelayAddressGenerator != nil {
		if err := c.RelayAddressGenerator.Validate(); err != nil {
			return err
		}
	}

	return nil
}

// ListenerConfig is a single net.Listener to accept connections on. This will be used for TCP, TLS and DTLS listeners
type ListenerConfig struct {
	Listener net.Listener

	// When an allocation is generated the RelayAddressGenerator
	// creates the net.PacketConn and returns the IP/Port it is available at
	RelayAddressGenerator RelayAddressGenerator

	// PermissionHandler is a callback to filter peer addresses. Can be set as nil, in which
	// case the DefaultPermissionHandler is automatically instantiated to admit all peer
	// connections
	PermissionHandler PermissionHandler
}

func (c *ListenerConfig) validate() error {
	if c.Listener == nil {
		return errListenerUnset
	}

	if c.RelayAddressGenerator == nil {
		return errRelayAddressGeneratorUnset
	}

	return c.RelayAddressGenerator.Validate()
}

// AuthHandler is a callback used to handle incoming auth requests, allowing users to customize Pion TURN with custom behavior
type AuthHandler func(username, realm string, srcAddr net.Addr) (key []byte, ok bool)

// GenerateAuthKey is a convenience function to easily generate keys in the format used by AuthHandler
func GenerateAuthKey(username, realm, password string) []byte {
	// #nosec
	h := md5.New()
	fmt.Fprint(h, strings.Join([]string{username, realm, password}, ":"))
	return h.Sum(nil)
}

// ServerConfig configures the Pion TURN Server
type ServerConfig struct {
	// PacketConnConfigs and ListenerConfigs are a list of all the turn listeners
	// Each listener can have custom behavior around the creation of Relays
	PacketConnConfigs []PacketConnConfig
	ListenerConfigs   []ListenerConfig

	// LoggerFactory must be set for logging from this server.
	LoggerFactory logging.LoggerFactory

	// Realm sets the realm for this server
	Realm string

	// AuthHandler is a callback used to handle incoming auth requests, allowing users to customize Pion TURN with custom behavior
	AuthHandler AuthHandler

	// ChannelBindTimeout sets the lifetime of channel binding. Defaults to 10 minutes.
	ChannelBindTimeout time.Duration

	// Sets the server inbound MTU(Maximum transmition unit). Defaults to 1600 bytes.
	InboundMTU int
}

func (s *ServerConfig) validate() error {
	if len(s.PacketConnConfigs) == 0 && len(s.ListenerConfigs) == 0 {
		return errNoAvailableConns
	}

	for _, s := range s.PacketConnConfigs {
		if err := s.validate(); err != nil {
			return err
		}
	}

	for _, s := range s.ListenerConfigs {
		if err := s.validate(); err != nil {
			return err
		}
	}

	return nil
}
