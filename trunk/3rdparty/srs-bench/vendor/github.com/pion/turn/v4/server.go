// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

// Package turn contains the public API for pion/turn, a toolkit for building TURN clients and servers
package turn

import (
	"errors"
	"fmt"
	"net"
	"time"

	"github.com/pion/logging"
	"github.com/pion/turn/v4/internal/allocation"
	"github.com/pion/turn/v4/internal/proto"
	"github.com/pion/turn/v4/internal/server"
)

const (
	defaultInboundMTU = 1600
)

// Server is an instance of the Pion TURN Server
type Server struct {
	log                logging.LeveledLogger
	authHandler        AuthHandler
	realm              string
	channelBindTimeout time.Duration
	nonceHash          *server.NonceHash

	packetConnConfigs  []PacketConnConfig
	listenerConfigs    []ListenerConfig
	allocationManagers []*allocation.Manager
	inboundMTU         int
}

// NewServer creates the Pion TURN server
//
//nolint:gocognit
func NewServer(config ServerConfig) (*Server, error) {
	if err := config.validate(); err != nil {
		return nil, err
	}

	loggerFactory := config.LoggerFactory
	if loggerFactory == nil {
		loggerFactory = logging.NewDefaultLoggerFactory()
	}

	mtu := defaultInboundMTU
	if config.InboundMTU != 0 {
		mtu = config.InboundMTU
	}

	nonceHash, err := server.NewNonceHash()
	if err != nil {
		return nil, err
	}

	s := &Server{
		log:                loggerFactory.NewLogger("turn"),
		authHandler:        config.AuthHandler,
		realm:              config.Realm,
		channelBindTimeout: config.ChannelBindTimeout,
		packetConnConfigs:  config.PacketConnConfigs,
		listenerConfigs:    config.ListenerConfigs,
		nonceHash:          nonceHash,
		inboundMTU:         mtu,
	}

	if s.channelBindTimeout == 0 {
		s.channelBindTimeout = proto.DefaultLifetime
	}

	for _, cfg := range s.packetConnConfigs {
		am, err := s.createAllocationManager(cfg.RelayAddressGenerator, cfg.PermissionHandler)
		if err != nil {
			return nil, fmt.Errorf("failed to create AllocationManager: %w", err)
		}

		go func(cfg PacketConnConfig, am *allocation.Manager) {
			s.readLoop(cfg.PacketConn, am)

			if err := am.Close(); err != nil {
				s.log.Errorf("Failed to close AllocationManager: %s", err)
			}
		}(cfg, am)
	}

	for _, cfg := range s.listenerConfigs {
		am, err := s.createAllocationManager(cfg.RelayAddressGenerator, cfg.PermissionHandler)
		if err != nil {
			return nil, fmt.Errorf("failed to create AllocationManager: %w", err)
		}

		go func(cfg ListenerConfig, am *allocation.Manager) {
			s.readListener(cfg.Listener, am)

			if err := am.Close(); err != nil {
				s.log.Errorf("Failed to close AllocationManager: %s", err)
			}
		}(cfg, am)
	}

	return s, nil
}

// AllocationCount returns the number of active allocations. It can be used to drain the server before closing
func (s *Server) AllocationCount() int {
	allocs := 0
	for _, am := range s.allocationManagers {
		allocs += am.AllocationCount()
	}
	return allocs
}

// Close stops the TURN Server. It cleans up any associated state and closes all connections it is managing
func (s *Server) Close() error {
	var errors []error

	for _, cfg := range s.packetConnConfigs {
		if err := cfg.PacketConn.Close(); err != nil {
			errors = append(errors, err)
		}
	}

	for _, cfg := range s.listenerConfigs {
		if err := cfg.Listener.Close(); err != nil {
			errors = append(errors, err)
		}
	}

	if len(errors) == 0 {
		return nil
	}

	err := errFailedToClose
	for _, e := range errors {
		err = fmt.Errorf("%s; close error (%w) ", err, e) //nolint:errorlint
	}

	return err
}

func (s *Server) readListener(l net.Listener, am *allocation.Manager) {
	for {
		conn, err := l.Accept()
		if err != nil {
			s.log.Debugf("Failed to accept: %s", err)
			return
		}

		go func() {
			s.readLoop(NewSTUNConn(conn), am)

			// Delete allocation
			am.DeleteAllocation(&allocation.FiveTuple{
				Protocol: allocation.UDP, // fixed UDP
				SrcAddr:  conn.RemoteAddr(),
				DstAddr:  conn.LocalAddr(),
			})

			if err := conn.Close(); err != nil && !errors.Is(err, net.ErrClosed) {
				s.log.Errorf("Failed to close conn: %s", err)
			}
		}()
	}
}

type nilAddressGenerator struct{}

func (n *nilAddressGenerator) Validate() error { return errRelayAddressGeneratorNil }

func (n *nilAddressGenerator) AllocatePacketConn(string, int) (net.PacketConn, net.Addr, error) {
	return nil, nil, errRelayAddressGeneratorNil
}

func (n *nilAddressGenerator) AllocateConn(string, int) (net.Conn, net.Addr, error) {
	return nil, nil, errRelayAddressGeneratorNil
}

func (s *Server) createAllocationManager(addrGenerator RelayAddressGenerator, handler PermissionHandler) (*allocation.Manager, error) {
	if handler == nil {
		handler = DefaultPermissionHandler
	}
	if addrGenerator == nil {
		addrGenerator = &nilAddressGenerator{}
	}

	am, err := allocation.NewManager(allocation.ManagerConfig{
		AllocatePacketConn: addrGenerator.AllocatePacketConn,
		AllocateConn:       addrGenerator.AllocateConn,
		PermissionHandler:  handler,
		LeveledLogger:      s.log,
	})
	if err != nil {
		return am, err
	}

	s.allocationManagers = append(s.allocationManagers, am)

	return am, err
}

func (s *Server) readLoop(p net.PacketConn, allocationManager *allocation.Manager) {
	buf := make([]byte, s.inboundMTU)
	for {
		n, addr, err := p.ReadFrom(buf)
		switch {
		case err != nil:
			s.log.Debugf("Exit read loop on error: %s", err)
			return
		case n >= s.inboundMTU:
			s.log.Debugf("Read bytes exceeded MTU, packet is possibly truncated")
			continue
		}

		if err := server.HandleRequest(server.Request{
			Conn:               p,
			SrcAddr:            addr,
			Buff:               buf[:n],
			Log:                s.log,
			AuthHandler:        s.authHandler,
			Realm:              s.realm,
			AllocationManager:  allocationManager,
			ChannelBindTimeout: s.channelBindTimeout,
			NonceHash:          s.nonceHash,
		}); err != nil {
			s.log.Errorf("Failed to handle datagram: %v", err)
		}
	}
}
