// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package sdp

import (
	"strconv"
)

// Information describes the "i=" field which provides textual information
// about the session.
type Information string

func (i Information) String() string {
	return stringFromMarshal(i.marshalInto, i.marshalSize)
}

func (i Information) marshalInto(b []byte) []byte {
	return append(b, i...)
}

func (i Information) marshalSize() (size int) {
	return len(i)
}

// ConnectionInformation defines the representation for the "c=" field
// containing connection data.
type ConnectionInformation struct {
	NetworkType string
	AddressType string
	Address     *Address
}

func (c ConnectionInformation) String() string {
	return stringFromMarshal(c.marshalInto, c.marshalSize)
}

func (c ConnectionInformation) marshalInto(b []byte) []byte {
	b = append(append(b, c.NetworkType...), ' ')
	b = append(b, c.AddressType...)

	if c.Address != nil {
		b = append(b, ' ')
		b = c.Address.marshalInto(b)
	}

	return b
}

func (c ConnectionInformation) marshalSize() (size int) {
	size = len(c.NetworkType)
	size += 1 + len(c.AddressType)
	if c.Address != nil {
		size += 1 + c.Address.marshalSize()
	}

	return
}

// Address desribes a structured address token from within the "c=" field.
type Address struct {
	Address string
	TTL     *int
	Range   *int
}

func (c *Address) String() string {
	return stringFromMarshal(c.marshalInto, c.marshalSize)
}

func (c *Address) marshalInto(b []byte) []byte {
	b = append(b, c.Address...)
	if c.TTL != nil {
		b = append(b, '/')
		b = strconv.AppendInt(b, int64(*c.TTL), 10)
	}
	if c.Range != nil {
		b = append(b, '/')
		b = strconv.AppendInt(b, int64(*c.Range), 10)
	}

	return b
}

func (c Address) marshalSize() (size int) {
	size = len(c.Address)
	if c.TTL != nil {
		size += 1 + lenUint(uint64(*c.TTL)) //nolint:gosec // G115
	}
	if c.Range != nil {
		size += 1 + lenUint(uint64(*c.Range)) //nolint:gosec // G115
	}

	return
}

// Bandwidth describes an optional field which denotes the proposed bandwidth
// to be used by the session or media.
type Bandwidth struct {
	Experimental bool
	Type         string
	Bandwidth    uint64
}

func (b Bandwidth) String() string {
	return stringFromMarshal(b.marshalInto, b.marshalSize)
}

func (b Bandwidth) marshalInto(d []byte) []byte {
	if b.Experimental {
		d = append(d, "X-"...)
	}
	d = append(append(d, b.Type...), ':')

	return strconv.AppendUint(d, b.Bandwidth, 10)
}

func (b Bandwidth) marshalSize() (size int) {
	if b.Experimental {
		size += 2
	}

	size += len(b.Type) + 1 + lenUint(b.Bandwidth)

	return
}

// EncryptionKey describes the "k=" which conveys encryption key information.
type EncryptionKey string

func (e EncryptionKey) String() string {
	return stringFromMarshal(e.marshalInto, e.marshalSize)
}

func (e EncryptionKey) marshalInto(b []byte) []byte {
	return append(b, e...)
}

func (e EncryptionKey) marshalSize() (size int) {
	return len(e)
}

// Attribute describes the "a=" field which represents the primary means for
// extending SDP.
type Attribute struct {
	Key   string
	Value string
}

// NewPropertyAttribute constructs a new attribute.
func NewPropertyAttribute(key string) Attribute {
	return Attribute{
		Key: key,
	}
}

// NewAttribute constructs a new attribute.
func NewAttribute(key, value string) Attribute {
	return Attribute{
		Key:   key,
		Value: value,
	}
}

func (a Attribute) String() string {
	return stringFromMarshal(a.marshalInto, a.marshalSize)
}

func (a Attribute) marshalInto(b []byte) []byte {
	b = append(b, a.Key...)
	if len(a.Value) > 0 {
		b = append(append(b, ':'), a.Value...)
	}

	return b
}

func (a Attribute) marshalSize() (size int) {
	size = len(a.Key)
	if len(a.Value) > 0 {
		size += 1 + len(a.Value)
	}

	return size
}

// IsICECandidate returns true if the attribute key equals "candidate".
func (a Attribute) IsICECandidate() bool {
	return a.Key == "candidate"
}
