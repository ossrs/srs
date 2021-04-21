package turn

import (
	"net"
	"strconv"

	"github.com/pion/transport/vnet"
)

// RelayAddressGeneratorNone returns the listener with no modifications
type RelayAddressGeneratorNone struct {
	// Address is passed to Listen/ListenPacket when creating the Relay
	Address string

	Net *vnet.Net
}

// Validate is caled on server startup and confirms the RelayAddressGenerator is properly configured
func (r *RelayAddressGeneratorNone) Validate() error {
	if r.Net == nil {
		r.Net = vnet.NewNet(nil)
	}

	switch {
	case r.Address == "":
		return errListeningAddressInvalid
	default:
		return nil
	}
}

// AllocatePacketConn generates a new PacketConn to receive traffic on and the IP/Port to populate the allocation response with
func (r *RelayAddressGeneratorNone) AllocatePacketConn(network string, requestedPort int) (net.PacketConn, net.Addr, error) {
	conn, err := r.Net.ListenPacket(network, r.Address+":"+strconv.Itoa(requestedPort))
	if err != nil {
		return nil, nil, err
	}

	return conn, conn.LocalAddr(), nil
}

// AllocateConn generates a new Conn to receive traffic on and the IP/Port to populate the allocation response with
func (r *RelayAddressGeneratorNone) AllocateConn(network string, requestedPort int) (net.Conn, net.Addr, error) {
	return nil, nil, errTODO
}
