package allocation

import (
	"fmt"
	"net"
	"sync"
	"time"

	"github.com/pion/logging"
)

// ManagerConfig a bag of config params for Manager.
type ManagerConfig struct {
	LeveledLogger      logging.LeveledLogger
	AllocatePacketConn func(network string, requestedPort int) (net.PacketConn, net.Addr, error)
	AllocateConn       func(network string, requestedPort int) (net.Conn, net.Addr, error)
}

type reservation struct {
	token string
	port  int
}

// Manager is used to hold active allocations
type Manager struct {
	lock sync.RWMutex
	log  logging.LeveledLogger

	allocations  map[string]*Allocation
	reservations []*reservation

	allocatePacketConn func(network string, requestedPort int) (net.PacketConn, net.Addr, error)
	allocateConn       func(network string, requestedPort int) (net.Conn, net.Addr, error)
}

// NewManager creates a new instance of Manager.
func NewManager(config ManagerConfig) (*Manager, error) {
	switch {
	case config.AllocatePacketConn == nil:
		return nil, errAllocatePacketConnMustBeSet
	case config.AllocateConn == nil:
		return nil, errAllocateConnMustBeSet
	case config.LeveledLogger == nil:
		return nil, errLeveledLoggerMustBeSet
	}

	return &Manager{
		log:                config.LeveledLogger,
		allocations:        make(map[string]*Allocation, 64),
		allocatePacketConn: config.AllocatePacketConn,
		allocateConn:       config.AllocateConn,
	}, nil
}

// GetAllocation fetches the allocation matching the passed FiveTuple
func (m *Manager) GetAllocation(fiveTuple *FiveTuple) *Allocation {
	m.lock.RLock()
	defer m.lock.RUnlock()
	return m.allocations[fiveTuple.Fingerprint()]
}

// Close closes the manager and closes all allocations it manages
func (m *Manager) Close() error {
	m.lock.Lock()
	defer m.lock.Unlock()

	for _, a := range m.allocations {
		if err := a.Close(); err != nil {
			return err
		}
	}
	return nil
}

// CreateAllocation creates a new allocation and starts relaying
func (m *Manager) CreateAllocation(fiveTuple *FiveTuple, turnSocket net.PacketConn, requestedPort int, lifetime time.Duration) (*Allocation, error) {
	switch {
	case fiveTuple == nil:
		return nil, errNilFiveTuple
	case fiveTuple.SrcAddr == nil:
		return nil, errNilFiveTupleSrcAddr
	case fiveTuple.DstAddr == nil:
		return nil, errNilFiveTupleDstAddr
	case turnSocket == nil:
		return nil, errNilTurnSocket
	case lifetime == 0:
		return nil, errLifetimeZero
	}

	if a := m.GetAllocation(fiveTuple); a != nil {
		return nil, fmt.Errorf("%w: %v", errDupeFiveTuple, fiveTuple)
	}
	a := NewAllocation(turnSocket, fiveTuple, m.log)

	conn, relayAddr, err := m.allocatePacketConn("udp4", requestedPort)
	if err != nil {
		return nil, err
	}

	a.RelaySocket = conn
	a.RelayAddr = relayAddr

	m.log.Debugf("listening on relay addr: %s", a.RelayAddr.String())

	a.lifetimeTimer = time.AfterFunc(lifetime, func() {
		m.DeleteAllocation(a.fiveTuple)
	})

	m.lock.Lock()
	m.allocations[fiveTuple.Fingerprint()] = a
	m.lock.Unlock()

	go a.packetHandler(m)
	return a, nil
}

// DeleteAllocation removes an allocation
func (m *Manager) DeleteAllocation(fiveTuple *FiveTuple) {
	fingerprint := fiveTuple.Fingerprint()

	m.lock.Lock()
	allocation := m.allocations[fingerprint]
	delete(m.allocations, fingerprint)
	m.lock.Unlock()

	if allocation == nil {
		return
	}

	if err := allocation.Close(); err != nil {
		m.log.Errorf("Failed to close allocation: %v", err)
	}
}

// CreateReservation stores the reservation for the token+port
func (m *Manager) CreateReservation(reservationToken string, port int) {
	time.AfterFunc(30*time.Second, func() {
		m.lock.Lock()
		defer m.lock.Unlock()
		for i := len(m.reservations) - 1; i >= 0; i-- {
			if m.reservations[i].token == reservationToken {
				m.reservations = append(m.reservations[:i], m.reservations[i+1:]...)
				return
			}
		}
	})

	m.lock.Lock()
	m.reservations = append(m.reservations, &reservation{
		token: reservationToken,
		port:  port,
	})
	m.lock.Unlock()
}

// GetReservation returns the port for a given reservation if it exists
func (m *Manager) GetReservation(reservationToken string) (int, bool) {
	m.lock.RLock()
	defer m.lock.RUnlock()

	for _, r := range m.reservations {
		if r.token == reservationToken {
			return r.port, true
		}
	}
	return 0, false
}

// GetRandomEvenPort returns a random un-allocated udp4 port
func (m *Manager) GetRandomEvenPort() (int, error) {
	conn, addr, err := m.allocatePacketConn("udp4", 0)
	if err != nil {
		return 0, err
	}

	udpAddr, ok := addr.(*net.UDPAddr)
	if !ok {
		return 0, errFailedToCastUDPAddr
	} else if err := conn.Close(); err != nil {
		return 0, err
	} else if udpAddr.Port%2 == 1 {
		return m.GetRandomEvenPort()
	}

	return udpAddr.Port, nil
}
