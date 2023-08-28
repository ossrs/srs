package proto

import (
	"github.com/pion/stun"
)

// DontFragmentAttr is a deprecated alias for DontFragment
// Deprecated: Please use DontFragment
type DontFragmentAttr = DontFragment

// DontFragment represents DONT-FRAGMENT attribute.
//
// This attribute is used by the client to request that the server set
// the DF (Don't Fragment) bit in the IP header when relaying the
// application data onward to the peer.  This attribute has no value
// part and thus the attribute length field is 0.
//
// RFC 5766 Section 14.8
type DontFragment struct{}

const dontFragmentSize = 0

// AddTo adds DONT-FRAGMENT attribute to message.
func (DontFragment) AddTo(m *stun.Message) error {
	m.Add(stun.AttrDontFragment, nil)
	return nil
}

// GetFrom decodes DONT-FRAGMENT from message.
func (d *DontFragment) GetFrom(m *stun.Message) error {
	v, err := m.Get(stun.AttrDontFragment)
	if err != nil {
		return err
	}
	if err = stun.CheckSize(stun.AttrDontFragment, len(v), dontFragmentSize); err != nil {
		return err
	}
	return nil
}

// IsSet returns true if DONT-FRAGMENT attribute is set.
func (DontFragment) IsSet(m *stun.Message) bool {
	_, err := m.Get(stun.AttrDontFragment)
	return err == nil
}
