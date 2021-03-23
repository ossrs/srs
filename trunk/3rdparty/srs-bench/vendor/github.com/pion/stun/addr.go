package stun

import (
	"fmt"
	"io"
	"net"
	"strconv"
)

// MappedAddress represents MAPPED-ADDRESS attribute.
//
// This attribute is used only by servers for achieving backwards
// compatibility with RFC 3489 clients.
//
// RFC 5389 Section 15.1
type MappedAddress struct {
	IP   net.IP
	Port int
}

// AlternateServer represents ALTERNATE-SERVER attribute.
//
// RFC 5389 Section 15.11
type AlternateServer struct {
	IP   net.IP
	Port int
}

// OtherAddress represents OTHER-ADDRESS attribute.
//
// RFC 5780 Section 7.4
type OtherAddress struct {
	IP   net.IP
	Port int
}

// AddTo adds ALTERNATE-SERVER attribute to message.
func (s *AlternateServer) AddTo(m *Message) error {
	a := (*MappedAddress)(s)
	return a.addAs(m, AttrAlternateServer)
}

// GetFrom decodes ALTERNATE-SERVER from message.
func (s *AlternateServer) GetFrom(m *Message) error {
	a := (*MappedAddress)(s)
	return a.getAs(m, AttrAlternateServer)
}

func (a MappedAddress) String() string {
	return net.JoinHostPort(a.IP.String(), strconv.Itoa(a.Port))
}

func (a *MappedAddress) getAs(m *Message, t AttrType) error {
	v, err := m.Get(t)
	if err != nil {
		return err
	}
	if len(v) <= 4 {
		return io.ErrUnexpectedEOF
	}
	family := bin.Uint16(v[0:2])
	if family != familyIPv6 && family != familyIPv4 {
		return newDecodeErr("xor-mapped address", "family",
			fmt.Sprintf("bad value %d", family),
		)
	}
	ipLen := net.IPv4len
	if family == familyIPv6 {
		ipLen = net.IPv6len
	}
	// Ensuring len(a.IP) == ipLen and reusing a.IP.
	if len(a.IP) < ipLen {
		a.IP = a.IP[:cap(a.IP)]
		for len(a.IP) < ipLen {
			a.IP = append(a.IP, 0)
		}
	}
	a.IP = a.IP[:ipLen]
	for i := range a.IP {
		a.IP[i] = 0
	}
	a.Port = int(bin.Uint16(v[2:4]))
	copy(a.IP, v[4:])
	return nil
}

func (a *MappedAddress) addAs(m *Message, t AttrType) error {
	var (
		family = familyIPv4
		ip     = a.IP
	)
	if len(a.IP) == net.IPv6len {
		if isIPv4(ip) {
			ip = ip[12:16] // like in ip.To4()
		} else {
			family = familyIPv6
		}
	} else if len(ip) != net.IPv4len {
		return ErrBadIPLength
	}
	value := make([]byte, 128)
	value[0] = 0 // first 8 bits are zeroes
	bin.PutUint16(value[0:2], family)
	bin.PutUint16(value[2:4], uint16(a.Port))
	copy(value[4:], ip)
	m.Add(t, value[:4+len(ip)])
	return nil
}

// AddTo adds MAPPED-ADDRESS to message.
func (a *MappedAddress) AddTo(m *Message) error {
	return a.addAs(m, AttrMappedAddress)
}

// GetFrom decodes MAPPED-ADDRESS from message.
func (a *MappedAddress) GetFrom(m *Message) error {
	return a.getAs(m, AttrMappedAddress)
}

// AddTo adds OTHER-ADDRESS attribute to message.
func (o *OtherAddress) AddTo(m *Message) error {
	a := (*MappedAddress)(o)
	return a.addAs(m, AttrOtherAddress)
}

// GetFrom decodes OTHER-ADDRESS from message.
func (o *OtherAddress) GetFrom(m *Message) error {
	a := (*MappedAddress)(o)
	return a.getAs(m, AttrOtherAddress)
}

func (o OtherAddress) String() string {
	return net.JoinHostPort(o.IP.String(), strconv.Itoa(o.Port))
}
