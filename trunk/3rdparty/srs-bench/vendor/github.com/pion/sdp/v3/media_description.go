// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package sdp

import (
	"strconv"
)

// MediaDescription represents a media type.
// https://tools.ietf.org/html/rfc4566#section-5.14
type MediaDescription struct {
	// m=<media> <port>/<number of ports> <proto> <fmt> ...
	// https://tools.ietf.org/html/rfc4566#section-5.14
	MediaName MediaName

	// i=<session description>
	// https://tools.ietf.org/html/rfc4566#section-5.4
	MediaTitle *Information

	// c=<nettype> <addrtype> <connection-address>
	// https://tools.ietf.org/html/rfc4566#section-5.7
	ConnectionInformation *ConnectionInformation

	// b=<bwtype>:<bandwidth>
	// https://tools.ietf.org/html/rfc4566#section-5.8
	Bandwidth []Bandwidth

	// k=<method>
	// k=<method>:<encryption key>
	// https://tools.ietf.org/html/rfc4566#section-5.12
	EncryptionKey *EncryptionKey

	// a=<attribute>
	// a=<attribute>:<value>
	// https://tools.ietf.org/html/rfc4566#section-5.13
	Attributes []Attribute
}

// Attribute returns the value of an attribute and if it exists.
func (d *MediaDescription) Attribute(key string) (string, bool) {
	for _, a := range d.Attributes {
		if a.Key == key {
			return a.Value, true
		}
	}

	return "", false
}

// RangedPort supports special format for the media field "m=" port value. If
// it may be necessary to specify multiple transport ports, the protocol allows
// to write it as: <port>/<number of ports> where number of ports is a an
// offsetting range.
type RangedPort struct {
	Value int
	Range *int
}

func (p *RangedPort) String() string {
	output := strconv.Itoa(p.Value)
	if p.Range != nil {
		output += "/" + strconv.Itoa(*p.Range)
	}

	return output
}

func (p RangedPort) marshalInto(b []byte) []byte {
	b = strconv.AppendInt(b, int64(p.Value), 10)
	if p.Range != nil {
		b = append(b, '/')
		b = strconv.AppendInt(b, int64(*p.Range), 10)
	}

	return b
}

func (p RangedPort) marshalSize() (size int) {
	size = lenInt(int64(p.Value))
	if p.Range != nil {
		size += 1 + lenInt(int64(*p.Range))
	}

	return
}

// MediaName describes the "m=" field storage structure.
type MediaName struct {
	Media   string
	Port    RangedPort
	Protos  []string
	Formats []string
}

func (m MediaName) String() string {
	return stringFromMarshal(m.marshalInto, m.marshalSize)
}

func (m MediaName) marshalInto(b []byte) []byte {
	appendList := func(list []string, sep byte) {
		for i, p := range list {
			if i != 0 && i != len(list) {
				b = append(b, sep)
			}
			b = append(b, p...)
		}
	}

	b = append(append(b, m.Media...), ' ')
	b = append(m.Port.marshalInto(b), ' ')
	appendList(m.Protos, '/')
	b = append(b, ' ')
	appendList(m.Formats, ' ')

	return b
}

func (m MediaName) marshalSize() (size int) {
	listSize := func(list []string) {
		for _, p := range list {
			size += 1 + len(p)
		}
	}

	size = len(m.Media)
	size += 1 + m.Port.marshalSize()
	listSize(m.Protos)
	listSize(m.Formats)

	return size
}
