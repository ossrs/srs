// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package sdp

import (
	"net/url"
	"strconv"
)

// SessionDescription is a a well-defined format for conveying sufficient
// information to discover and participate in a multimedia session.
type SessionDescription struct {
	// v=0
	// https://tools.ietf.org/html/rfc4566#section-5.1
	Version Version

	// o=<username> <sess-id> <sess-version> <nettype> <addrtype> <unicast-address>
	// https://tools.ietf.org/html/rfc4566#section-5.2
	Origin Origin

	// s=<session name>
	// https://tools.ietf.org/html/rfc4566#section-5.3
	SessionName SessionName

	// i=<session description>
	// https://tools.ietf.org/html/rfc4566#section-5.4
	SessionInformation *Information

	// u=<uri>
	// https://tools.ietf.org/html/rfc4566#section-5.5
	URI *url.URL

	// e=<email-address>
	// https://tools.ietf.org/html/rfc4566#section-5.6
	EmailAddress *EmailAddress

	// p=<phone-number>
	// https://tools.ietf.org/html/rfc4566#section-5.6
	PhoneNumber *PhoneNumber

	// c=<nettype> <addrtype> <connection-address>
	// https://tools.ietf.org/html/rfc4566#section-5.7
	ConnectionInformation *ConnectionInformation

	// b=<bwtype>:<bandwidth>
	// https://tools.ietf.org/html/rfc4566#section-5.8
	Bandwidth []Bandwidth

	// https://tools.ietf.org/html/rfc4566#section-5.9
	// https://tools.ietf.org/html/rfc4566#section-5.10
	TimeDescriptions []TimeDescription

	// z=<adjustment time> <offset> <adjustment time> <offset> ...
	// https://tools.ietf.org/html/rfc4566#section-5.11
	TimeZones []TimeZone

	// k=<method>
	// k=<method>:<encryption key>
	// https://tools.ietf.org/html/rfc4566#section-5.12
	EncryptionKey *EncryptionKey

	// a=<attribute>
	// a=<attribute>:<value>
	// https://tools.ietf.org/html/rfc4566#section-5.13
	Attributes []Attribute

	// https://tools.ietf.org/html/rfc4566#section-5.14
	MediaDescriptions []*MediaDescription
}

// Attribute returns the value of an attribute and if it exists.
func (s *SessionDescription) Attribute(key string) (string, bool) {
	for _, a := range s.Attributes {
		if a.Key == key {
			return a.Value, true
		}
	}

	return "", false
}

// Version describes the value provided by the "v=" field which gives
// the version of the Session Description Protocol.
type Version int

func (v Version) String() string {
	return stringFromMarshal(v.marshalInto, v.marshalSize)
}

func (v Version) marshalInto(b []byte) []byte {
	return strconv.AppendInt(b, int64(v), 10)
}

func (v Version) marshalSize() (size int) {
	return lenInt(int64(v))
}

// Origin defines the structure for the "o=" field which provides the
// originator of the session plus a session identifier and version number.
type Origin struct {
	Username       string
	SessionID      uint64
	SessionVersion uint64
	NetworkType    string
	AddressType    string
	UnicastAddress string
}

func (o Origin) String() string {
	return stringFromMarshal(o.marshalInto, o.marshalSize)
}

func (o Origin) marshalInto(b []byte) []byte {
	b = append(append(b, o.Username...), ' ')
	b = append(strconv.AppendUint(b, o.SessionID, 10), ' ')
	b = append(strconv.AppendUint(b, o.SessionVersion, 10), ' ')
	b = append(append(b, o.NetworkType...), ' ')
	b = append(append(b, o.AddressType...), ' ')

	return append(b, o.UnicastAddress...)
}

func (o Origin) marshalSize() (size int) {
	return len(o.Username) +
		lenUint(o.SessionID) +
		lenUint(o.SessionVersion) +
		len(o.NetworkType) +
		len(o.AddressType) +
		len(o.UnicastAddress) +
		5
}

// SessionName describes a structured representations for the "s=" field
// and is the textual session name.
type SessionName string

func (s SessionName) String() string {
	return stringFromMarshal(s.marshalInto, s.marshalSize)
}

func (s SessionName) marshalInto(b []byte) []byte {
	return append(b, s...)
}

func (s SessionName) marshalSize() (size int) {
	return len(s)
}

// EmailAddress describes a structured representations for the "e=" line
// which specifies email contact information for the person responsible for
// the conference.
type EmailAddress string

func (e EmailAddress) String() string {
	return stringFromMarshal(e.marshalInto, e.marshalSize)
}

func (e EmailAddress) marshalInto(b []byte) []byte {
	return append(b, e...)
}

func (e EmailAddress) marshalSize() (size int) {
	return len(e)
}

// PhoneNumber describes a structured representations for the "p=" line
// specify phone contact information for the person responsible for the
// conference.
type PhoneNumber string

func (p PhoneNumber) String() string {
	return stringFromMarshal(p.marshalInto, p.marshalSize)
}

func (p PhoneNumber) marshalInto(b []byte) []byte {
	return append(b, p...)
}

func (p PhoneNumber) marshalSize() (size int) {
	return len(p)
}

// TimeZone defines the structured object for "z=" line which describes
// repeated sessions scheduling.
type TimeZone struct {
	AdjustmentTime uint64
	Offset         int64
}

func (z TimeZone) String() string {
	return stringFromMarshal(z.marshalInto, z.marshalSize)
}

func (z TimeZone) marshalInto(b []byte) []byte {
	b = strconv.AppendUint(b, z.AdjustmentTime, 10)
	b = append(b, ' ')

	return strconv.AppendInt(b, z.Offset, 10)
}

func (z TimeZone) marshalSize() (size int) {
	return lenUint(z.AdjustmentTime) + 1 + lenInt(z.Offset)
}
