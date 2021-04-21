package turn

import (
	"net"
	"strconv"

	"github.com/pion/transport/vnet"
)

// RelayAddressGeneratorStatic can be used to return static IP address each time a relay is created.
// This can be used when you have a single static IP address that you want to use
type RelayAddressGeneratorStatic struct {
	// RelayAddress is the IP returned to the user when the relay is created
	RelayAddress net.IP

	// Address is passed to Listen/ListenPacket when creating the Relay
	Address string

	Net *vnet.Net
}

// Validate is caled on server startup and confirms the RelayAddressGenerator is properly configured
func (r *RelayAddressGeneratorStatic) Validate() error {
	if r.Net == nil {
		r.Net = vnet.NewNet(nil)
	}

	switch {
	case r.RelayAddress == nil:
		return errRelayAddressInvalid
	case r.Address == "":
		return errListeningAddressInvalid
	default:
		return nil
	}
}

// AllocatePacketConn generates a new PacketConn to receive traffic on and the IP/Port to populate the allocation response with
func (r *RelayAddressGeneratorStatic) AllocatePacketConn(network string, requestedPort int) (net.PacketConn, net.Addr, error) {
	conn, err := r.Net.ListenPacket(network, r.Address+":"+strconv.Itoa(requestedPort))
	if err != nil {
		return nil, nil, err
	}

	// Replace actual listening IP with the user requested one of RelayAddressGeneratorStatic
	relayAddr := conn.LocalAddr().(*net.UDPAddr)
	relayAddr.IP = r.RelayAddress

	return conn, relayAddr, nil
}

// AllocateConn generates a new Conn to receive traffic on and the IP/Port to populate the allocation response with
func (r *RelayAddressGeneratorStatic) AllocateConn(network string, requestedPort int) (net.Conn, net.Addr, error) {
	return nil, nil, errTODO
}
