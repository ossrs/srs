// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package ice

import "github.com/pion/stun"

type (
	// URL represents a STUN (rfc7064) or TURN (rfc7065) URI
	//
	// Deprecated: Please use pion/stun.URI
	URL = stun.URI

	// ProtoType indicates the transport protocol type that is used in the ice.URL
	// structure.
	//
	// Deprecated: TPlease use pion/stun.ProtoType
	ProtoType = stun.ProtoType

	// SchemeType indicates the type of server used in the ice.URL structure.
	//
	// Deprecated: Please use pion/stun.SchemeType
	SchemeType = stun.SchemeType
)

const (
	// SchemeTypeSTUN indicates the URL represents a STUN server.
	//
	// Deprecated: Please use pion/stun.SchemeTypeSTUN
	SchemeTypeSTUN = stun.SchemeTypeSTUN

	// SchemeTypeSTUNS indicates the URL represents a STUNS (secure) server.
	//
	// Deprecated: Please use pion/stun.SchemeTypeSTUNS
	SchemeTypeSTUNS = stun.SchemeTypeSTUNS

	// SchemeTypeTURN indicates the URL represents a TURN server.
	//
	// Deprecated: Please use pion/stun.SchemeTypeTURN
	SchemeTypeTURN = stun.SchemeTypeTURN

	// SchemeTypeTURNS indicates the URL represents a TURNS (secure) server.
	//
	// Deprecated: Please use pion/stun.SchemeTypeTURNS
	SchemeTypeTURNS = stun.SchemeTypeTURNS
)

const (
	// ProtoTypeUDP indicates the URL uses a UDP transport.
	//
	// Deprecated: Please use pion/stun.ProtoTypeUDP
	ProtoTypeUDP = stun.ProtoTypeUDP

	// ProtoTypeTCP indicates the URL uses a TCP transport.
	//
	// Deprecated: Please use pion/stun.ProtoTypeTCP
	ProtoTypeTCP = stun.ProtoTypeTCP
)

// Unknown represents and unknown ProtoType or SchemeType
//
// Deprecated: Please use pion/stun.SchemeTypeUnknown or pion/stun.ProtoTypeUnknown
const Unknown = 0

// ParseURL parses a STUN or TURN urls following the ABNF syntax described in
// https://tools.ietf.org/html/rfc7064 and https://tools.ietf.org/html/rfc7065
// respectively.
//
// Deprecated: Please use pion/stun.ParseURI
var ParseURL = stun.ParseURI //nolint:gochecknoglobals

// NewSchemeType defines a procedure for creating a new SchemeType from a raw
// string naming the scheme type.
//
// Deprecated: Please use pion/stun.NewSchemeType
var NewSchemeType = stun.NewSchemeType //nolint:gochecknoglobals

// NewProtoType defines a procedure for creating a new ProtoType from a raw
// string naming the transport protocol type.
//
// Deprecated: Please use pion/stun.NewProtoType
var NewProtoType = stun.NewProtoType //nolint:gochecknoglobals
