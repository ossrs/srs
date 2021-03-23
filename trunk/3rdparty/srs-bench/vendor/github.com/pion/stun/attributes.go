package stun

import (
	"errors"
	"fmt"
)

// Attributes is list of message attributes.
type Attributes []RawAttribute

// Get returns first attribute from list by the type.
// If attribute is present the RawAttribute is returned and the
// boolean is true. Otherwise the returned RawAttribute will be
// empty and boolean will be false.
func (a Attributes) Get(t AttrType) (RawAttribute, bool) {
	for _, candidate := range a {
		if candidate.Type == t {
			return candidate, true
		}
	}
	return RawAttribute{}, false
}

// AttrType is attribute type.
type AttrType uint16

// Required returns true if type is from comprehension-required range (0x0000-0x7FFF).
func (t AttrType) Required() bool {
	return t <= 0x7FFF
}

// Optional returns true if type is from comprehension-optional range (0x8000-0xFFFF).
func (t AttrType) Optional() bool {
	return t >= 0x8000
}

// Attributes from comprehension-required range (0x0000-0x7FFF).
const (
	AttrMappedAddress     AttrType = 0x0001 // MAPPED-ADDRESS
	AttrUsername          AttrType = 0x0006 // USERNAME
	AttrMessageIntegrity  AttrType = 0x0008 // MESSAGE-INTEGRITY
	AttrErrorCode         AttrType = 0x0009 // ERROR-CODE
	AttrUnknownAttributes AttrType = 0x000A // UNKNOWN-ATTRIBUTES
	AttrRealm             AttrType = 0x0014 // REALM
	AttrNonce             AttrType = 0x0015 // NONCE
	AttrXORMappedAddress  AttrType = 0x0020 // XOR-MAPPED-ADDRESS
)

// Attributes from comprehension-optional range (0x8000-0xFFFF).
const (
	AttrSoftware        AttrType = 0x8022 // SOFTWARE
	AttrAlternateServer AttrType = 0x8023 // ALTERNATE-SERVER
	AttrFingerprint     AttrType = 0x8028 // FINGERPRINT
)

// Attributes from RFC 5245 ICE.
const (
	AttrPriority       AttrType = 0x0024 // PRIORITY
	AttrUseCandidate   AttrType = 0x0025 // USE-CANDIDATE
	AttrICEControlled  AttrType = 0x8029 // ICE-CONTROLLED
	AttrICEControlling AttrType = 0x802A // ICE-CONTROLLING
)

// Attributes from RFC 5766 TURN.
const (
	AttrChannelNumber      AttrType = 0x000C // CHANNEL-NUMBER
	AttrLifetime           AttrType = 0x000D // LIFETIME
	AttrXORPeerAddress     AttrType = 0x0012 // XOR-PEER-ADDRESS
	AttrData               AttrType = 0x0013 // DATA
	AttrXORRelayedAddress  AttrType = 0x0016 // XOR-RELAYED-ADDRESS
	AttrEvenPort           AttrType = 0x0018 // EVEN-PORT
	AttrRequestedTransport AttrType = 0x0019 // REQUESTED-TRANSPORT
	AttrDontFragment       AttrType = 0x001A // DONT-FRAGMENT
	AttrReservationToken   AttrType = 0x0022 // RESERVATION-TOKEN
)

// Attributes from RFC 5780 NAT Behavior Discovery
const (
	AttrOtherAddress  AttrType = 0x802C // OTHER-ADDRESS
	AttrChangeRequest AttrType = 0x0003 // CHANGE-REQUEST
)

// Attributes from RFC 6062 TURN Extensions for TCP Allocations.
const (
	AttrConnectionID AttrType = 0x002a // CONNECTION-ID
)

// Attributes from RFC 6156 TURN IPv6.
const (
	AttrRequestedAddressFamily AttrType = 0x0017 // REQUESTED-ADDRESS-FAMILY
)

// Attributes from An Origin Attribute for the STUN Protocol.
const (
	AttrOrigin AttrType = 0x802F
)

// Value returns uint16 representation of attribute type.
func (t AttrType) Value() uint16 {
	return uint16(t)
}

var attrNames = map[AttrType]string{
	AttrMappedAddress:          "MAPPED-ADDRESS",
	AttrUsername:               "USERNAME",
	AttrErrorCode:              "ERROR-CODE",
	AttrMessageIntegrity:       "MESSAGE-INTEGRITY",
	AttrUnknownAttributes:      "UNKNOWN-ATTRIBUTES",
	AttrRealm:                  "REALM",
	AttrNonce:                  "NONCE",
	AttrXORMappedAddress:       "XOR-MAPPED-ADDRESS",
	AttrSoftware:               "SOFTWARE",
	AttrAlternateServer:        "ALTERNATE-SERVER",
	AttrOtherAddress:           "OTHER-ADDRESS",
	AttrChangeRequest:          "CHANGE-REQUEST",
	AttrFingerprint:            "FINGERPRINT",
	AttrPriority:               "PRIORITY",
	AttrUseCandidate:           "USE-CANDIDATE",
	AttrICEControlled:          "ICE-CONTROLLED",
	AttrICEControlling:         "ICE-CONTROLLING",
	AttrChannelNumber:          "CHANNEL-NUMBER",
	AttrLifetime:               "LIFETIME",
	AttrXORPeerAddress:         "XOR-PEER-ADDRESS",
	AttrData:                   "DATA",
	AttrXORRelayedAddress:      "XOR-RELAYED-ADDRESS",
	AttrEvenPort:               "EVEN-PORT",
	AttrRequestedTransport:     "REQUESTED-TRANSPORT",
	AttrDontFragment:           "DONT-FRAGMENT",
	AttrReservationToken:       "RESERVATION-TOKEN",
	AttrConnectionID:           "CONNECTION-ID",
	AttrRequestedAddressFamily: "REQUESTED-ADDRESS-FAMILY",
	AttrOrigin:                 "ORIGIN",
}

func (t AttrType) String() string {
	s, ok := attrNames[t]
	if !ok {
		// Just return hex representation of unknown attribute type.
		return fmt.Sprintf("0x%x", uint16(t))
	}
	return s
}

// RawAttribute is a Type-Length-Value (TLV) object that
// can be added to a STUN message. Attributes are divided into two
// types: comprehension-required and comprehension-optional.  STUN
// agents can safely ignore comprehension-optional attributes they
// don't understand, but cannot successfully process a message if it
// contains comprehension-required attributes that are not
// understood.
type RawAttribute struct {
	Type   AttrType
	Length uint16 // ignored while encoding
	Value  []byte
}

// AddTo implements Setter, adding attribute as a.Type with a.Value and ignoring
// the Length field.
func (a RawAttribute) AddTo(m *Message) error {
	m.Add(a.Type, a.Value)
	return nil
}

// Equal returns true if a == b.
func (a RawAttribute) Equal(b RawAttribute) bool {
	if a.Type != b.Type {
		return false
	}
	if a.Length != b.Length {
		return false
	}
	if len(b.Value) != len(a.Value) {
		return false
	}
	for i, v := range a.Value {
		if b.Value[i] != v {
			return false
		}
	}
	return true
}

func (a RawAttribute) String() string {
	return fmt.Sprintf("%s: 0x%x", a.Type, a.Value)
}

// ErrAttributeNotFound means that attribute with provided attribute
// type does not exist in message.
var ErrAttributeNotFound = errors.New("attribute not found")

// Get returns byte slice that represents attribute value,
// if there is no attribute with such type,
// ErrAttributeNotFound is returned.
func (m *Message) Get(t AttrType) ([]byte, error) {
	v, ok := m.Attributes.Get(t)
	if !ok {
		return nil, ErrAttributeNotFound
	}
	return v.Value, nil
}

// STUN aligns attributes on 32-bit boundaries, attributes whose content
// is not a multiple of 4 bytes are padded with 1, 2, or 3 bytes of
// padding so that its value contains a multiple of 4 bytes.  The
// padding bits are ignored, and may be any value.
//
// https://tools.ietf.org/html/rfc5389#section-15
const padding = 4

func nearestPaddedValueLength(l int) int {
	n := padding * (l / padding)
	if n < l {
		n += padding
	}
	return n
}

// This method converts uint16 vlue to AttrType. If it finds an old attribute
// type value, it also translates it to the new value to enable backward
// compatibility. (See: https://github.com/pion/stun/issues/21)
func compatAttrType(val uint16) AttrType {
	if val == 0x8020 {
		return AttrXORMappedAddress // new: 0x0020
	}
	return AttrType(val)
}
