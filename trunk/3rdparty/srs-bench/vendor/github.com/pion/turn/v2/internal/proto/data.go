package proto

import "github.com/pion/stun"

// Data represents DATA attribute.
//
// The DATA attribute is present in all Send and Data indications.  The
// value portion of this attribute is variable length and consists of
// the application data (that is, the data that would immediately follow
// the UDP header if the data was been sent directly between the client
// and the peer).
//
// RFC 5766 Section 14.4
type Data []byte

// AddTo adds DATA to message.
func (d Data) AddTo(m *stun.Message) error {
	m.Add(stun.AttrData, d)
	return nil
}

// GetFrom decodes DATA from message.
func (d *Data) GetFrom(m *stun.Message) error {
	v, err := m.Get(stun.AttrData)
	if err != nil {
		return err
	}
	*d = v
	return nil
}
