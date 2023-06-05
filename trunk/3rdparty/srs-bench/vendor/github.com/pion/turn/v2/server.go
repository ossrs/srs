// Package turn contains the public API for pion/turn, a toolkit for building TURN clients and servers
package turn

import (
	"fmt"
	"net"
	"sync"
	"time"

	"github.com/pion/logging"
	"github.com/pion/turn/v2/internal/allocation"
	"github.com/pion/turn/v2/internal/proto"
	"github.com/pion/turn/v2/internal/server"
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
	nonces             *sync.Map

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

	s := &Server{
		log:                loggerFactory.NewLogger("turn"),
		authHandler:        config.AuthHandler,
		realm:              config.Realm,
		channelBindTimeout: config.ChannelBindTimeout,
		packetConnConfigs:  config.PacketConnConfigs,
		listenerConfigs:    config.ListenerConfigs,
		nonces:             &sync.Map{},
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

		go s.readPacketConn(cfg, am)
	}

	for _, cfg := range s.listenerConfigs {
		am, err := s.createAllocationManager(cfg.RelayAddressGenerator, cfg.PermissionHandler)
		if err != nil {
			return nil, fmt.Errorf("failed to create AllocationManager: %w", err)
		}

		go s.readListener(cfg, am)
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
		err = fmt.Errorf("%s; close error (%w) ", err, e)
	}

	return err
}

func (s *Server) readPacketConn(p PacketConnConfig, am *allocation.Manager) {
	s.readLoop(p.PacketConn, am)

	if err := am.Close(); err != nil {
		s.log.Errorf("Failed to close AllocationManager: %s", err)
	}
}

func (s *Server) readListener(l ListenerConfig, am *allocation.Manager) {
	defer func() {
		if err := am.Close(); err != nil {
			s.log.Errorf("Failed to close AllocationManager: %s", err)
		}
	}()

	for {
		conn, err := l.Listener.Accept()
		if err != nil {
			s.log.Debugf("Failed to accept: %s", err)
			return
		}

		go s.readLoop(NewSTUNConn(conn), am)
	}
}

func (s *Server) createAllocationManager(addrGenerator RelayAddressGenerator, handler PermissionHandler) (*allocation.Manager, error) {
	if handler == nil {
		handler = DefaultPermissionHandler
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
			s.log.Debugf("exit read loop on error: %s", err.Error())
			return
		case n >= s.inboundMTU:
			s.log.Debugf("Read bytes exceeded MTU, packet is possibly truncated")
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
			Nonces:             s.nonces,
		}); err != nil {
			s.log.Errorf("error when handling datagram: %v", err)
		}
	}
}
