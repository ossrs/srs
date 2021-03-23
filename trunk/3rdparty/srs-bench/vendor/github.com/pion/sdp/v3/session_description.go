package sdp

import (
	"fmt"
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

// Attribute returns the value of an attribute and if it exists
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
	return strconv.Itoa(int(v))
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
	return fmt.Sprintf(
		"%v %d %d %v %v %v",
		o.Username,
		o.SessionID,
		o.SessionVersion,
		o.NetworkType,
		o.AddressType,
		o.UnicastAddress,
	)
}

// SessionName describes a structured representations for the "s=" field
// and is the textual session name.
type SessionName string

func (s SessionName) String() string {
	return string(s)
}

// EmailAddress describes a structured representations for the "e=" line
// which specifies email contact information for the person responsible for
// the conference.
type EmailAddress string

func (e EmailAddress) String() string {
	return string(e)
}

// PhoneNumber describes a structured representations for the "p=" line
// specify phone contact information for the person responsible for the
// conference.
type PhoneNumber string

func (p PhoneNumber) String() string {
	return string(p)
}

// TimeZone defines the structured object for "z=" line which describes
// repeated sessions scheduling.
type TimeZone struct {
	AdjustmentTime uint64
	Offset         int64
}

func (z TimeZone) String() string {
	return strconv.FormatUint(z.AdjustmentTime, 10) + " " + strconv.FormatInt(z.Offset, 10)
}
