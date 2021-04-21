package sdp

import (
	"strconv"
	"strings"
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

// Attribute returns the value of an attribute and if it exists
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

// MediaName describes the "m=" field storage structure.
type MediaName struct {
	Media   string
	Port    RangedPort
	Protos  []string
	Formats []string
}

func (m MediaName) String() string {
	return strings.Join([]string{
		m.Media,
		m.Port.String(),
		strings.Join(m.Protos, "/"),
		strings.Join(m.Formats, " "),
	}, " ")
}
