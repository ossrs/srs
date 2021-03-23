package proto

import (
	"errors"

	"github.com/pion/stun"
)

// RequestedAddressFamily represents the REQUESTED-ADDRESS-FAMILY Attribute as
// defined in RFC 6156 Section 4.1.1.
type RequestedAddressFamily byte

const requestedFamilySize = 4

var errInvalidRequestedFamilyValue = errors.New("invalid value for requested family attribute")

// GetFrom decodes REQUESTED-ADDRESS-FAMILY from message.
func (f *RequestedAddressFamily) GetFrom(m *stun.Message) error {
	v, err := m.Get(stun.AttrRequestedAddressFamily)
	if err != nil {
		return err
	}
	if err = stun.CheckSize(stun.AttrRequestedAddressFamily, len(v), requestedFamilySize); err != nil {
		return err
	}
	switch v[0] {
	case byte(RequestedFamilyIPv4), byte(RequestedFamilyIPv6):
		*f = RequestedAddressFamily(v[0])
	default:
		return errInvalidRequestedFamilyValue
	}
	return nil
}

func (f RequestedAddressFamily) String() string {
	switch f {
	case RequestedFamilyIPv4:
		return "IPv4"
	case RequestedFamilyIPv6:
		return "IPv6"
	default:
		return "unknown"
	}
}

// AddTo adds REQUESTED-ADDRESS-FAMILY to message.
func (f RequestedAddressFamily) AddTo(m *stun.Message) error {
	v := make([]byte, requestedFamilySize)
	v[0] = byte(f)
	// b[1:4] is RFFU = 0.
	// The RFFU field MUST be set to zero on transmission and MUST be
	// ignored on reception. It is reserved for future uses.
	m.Add(stun.AttrRequestedAddressFamily, v)
	return nil
}

// Values for RequestedAddressFamily as defined in RFC 6156 Section 4.1.1.
const (
	RequestedFamilyIPv4 RequestedAddressFamily = 0x01
	RequestedFamilyIPv6 RequestedAddressFamily = 0x02
)
