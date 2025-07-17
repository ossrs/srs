// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package stun

import (
	"errors"
	"net"
	"net/url"
	"strconv"
)

var (
	// ErrUnknownType indicates an error with Unknown info.
	ErrUnknownType = errors.New("Unknown")

	// ErrSchemeType indicates the scheme type could not be parsed.
	ErrSchemeType = errors.New("unknown scheme type")

	// ErrSTUNQuery indicates query arguments are provided in a STUN URL.
	ErrSTUNQuery = errors.New("queries not supported in stun address")

	// ErrInvalidQuery indicates an malformed query is provided.
	ErrInvalidQuery = errors.New("invalid query")

	// ErrHost indicates malformed hostname is provided.
	ErrHost = errors.New("invalid hostname")

	// ErrPort indicates malformed port is provided.
	ErrPort = errors.New("invalid port")

	// ErrProtoType indicates an unsupported transport type was provided.
	ErrProtoType = errors.New("invalid transport protocol type")
)

// SchemeType indicates the type of server used in the ice.URL structure.
type SchemeType int

const (
	// SchemeTypeUnknown indicates an unknown or unsupported scheme.
	SchemeTypeUnknown SchemeType = iota

	// SchemeTypeSTUN indicates the URL represents a STUN server.
	SchemeTypeSTUN

	// SchemeTypeSTUNS indicates the URL represents a STUNS (secure) server.
	SchemeTypeSTUNS

	// SchemeTypeTURN indicates the URL represents a TURN server.
	SchemeTypeTURN

	// SchemeTypeTURNS indicates the URL represents a TURNS (secure) server.
	SchemeTypeTURNS
)

// NewSchemeType defines a procedure for creating a new SchemeType from a raw
// string naming the scheme type.
func NewSchemeType(raw string) SchemeType {
	switch raw {
	case "stun":
		return SchemeTypeSTUN
	case "stuns":
		return SchemeTypeSTUNS
	case "turn":
		return SchemeTypeTURN
	case "turns":
		return SchemeTypeTURNS
	default:
		return SchemeTypeUnknown
	}
}

func (t SchemeType) String() string {
	switch t {
	case SchemeTypeSTUN:
		return "stun"
	case SchemeTypeSTUNS:
		return "stuns"
	case SchemeTypeTURN:
		return "turn"
	case SchemeTypeTURNS:
		return "turns"
	default:
		return ErrUnknownType.Error()
	}
}

// ProtoType indicates the transport protocol type that is used in the ice.URL
// structure.
type ProtoType int

const (
	// ProtoTypeUnknown indicates an unknown or unsupported protocol.
	ProtoTypeUnknown ProtoType = iota

	// ProtoTypeUDP indicates the URL uses a UDP transport.
	ProtoTypeUDP

	// ProtoTypeTCP indicates the URL uses a TCP transport.
	ProtoTypeTCP
)

// NewProtoType defines a procedure for creating a new ProtoType from a raw
// string naming the transport protocol type.
func NewProtoType(raw string) ProtoType {
	switch raw {
	case "udp":
		return ProtoTypeUDP
	case "tcp":
		return ProtoTypeTCP
	default:
		return ProtoTypeUnknown
	}
}

func (t ProtoType) String() string {
	switch t {
	case ProtoTypeUDP:
		return "udp"
	case ProtoTypeTCP:
		return "tcp"
	default:
		return ErrUnknownType.Error()
	}
}

// URI represents a STUN (rfc7064) or TURN (rfc7065) URI
type URI struct {
	Scheme   SchemeType
	Host     string
	Port     int
	Username string
	Password string
	Proto    ProtoType
}

// ParseURI parses a STUN or TURN urls following the ABNF syntax described in
// https://tools.ietf.org/html/rfc7064 and https://tools.ietf.org/html/rfc7065
// respectively.
func ParseURI(raw string) (*URI, error) { //nolint:gocognit
	rawParts, err := url.Parse(raw)
	if err != nil {
		return nil, err
	}

	var u URI
	u.Scheme = NewSchemeType(rawParts.Scheme)
	if u.Scheme == SchemeTypeUnknown {
		return nil, ErrSchemeType
	}

	var rawPort string
	if u.Host, rawPort, err = net.SplitHostPort(rawParts.Opaque); err != nil {
		var e *net.AddrError
		if errors.As(err, &e) {
			if e.Err == "missing port in address" {
				nextRawURL := u.Scheme.String() + ":" + rawParts.Opaque
				switch {
				case u.Scheme == SchemeTypeSTUN || u.Scheme == SchemeTypeTURN:
					nextRawURL += ":3478"
					if rawParts.RawQuery != "" {
						nextRawURL += "?" + rawParts.RawQuery
					}
					return ParseURI(nextRawURL)
				case u.Scheme == SchemeTypeSTUNS || u.Scheme == SchemeTypeTURNS:
					nextRawURL += ":5349"
					if rawParts.RawQuery != "" {
						nextRawURL += "?" + rawParts.RawQuery
					}
					return ParseURI(nextRawURL)
				}
			}
		}
		return nil, err
	}

	if u.Host == "" {
		return nil, ErrHost
	}

	if u.Port, err = strconv.Atoi(rawPort); err != nil {
		return nil, ErrPort
	}

	switch u.Scheme {
	case SchemeTypeSTUN:
		qArgs, err := url.ParseQuery(rawParts.RawQuery)
		if err != nil || len(qArgs) > 0 {
			return nil, ErrSTUNQuery
		}
		u.Proto = ProtoTypeUDP
	case SchemeTypeSTUNS:
		qArgs, err := url.ParseQuery(rawParts.RawQuery)
		if err != nil || len(qArgs) > 0 {
			return nil, ErrSTUNQuery
		}
		u.Proto = ProtoTypeTCP
	case SchemeTypeTURN:
		proto, err := parseProto(rawParts.RawQuery)
		if err != nil {
			return nil, err
		}

		u.Proto = proto
		if u.Proto == ProtoTypeUnknown {
			u.Proto = ProtoTypeUDP
		}
	case SchemeTypeTURNS:
		proto, err := parseProto(rawParts.RawQuery)
		if err != nil {
			return nil, err
		}

		u.Proto = proto
		if u.Proto == ProtoTypeUnknown {
			u.Proto = ProtoTypeTCP
		}

	case SchemeTypeUnknown:
	}

	return &u, nil
}

func parseProto(raw string) (ProtoType, error) {
	qArgs, err := url.ParseQuery(raw)
	if err != nil || len(qArgs) > 1 {
		return ProtoTypeUnknown, ErrInvalidQuery
	}

	var proto ProtoType
	if rawProto := qArgs.Get("transport"); rawProto != "" {
		if proto = NewProtoType(rawProto); proto == ProtoType(0) {
			return ProtoTypeUnknown, ErrProtoType
		}
		return proto, nil
	}

	if len(qArgs) > 0 {
		return ProtoTypeUnknown, ErrInvalidQuery
	}

	return proto, nil
}

func (u URI) String() string {
	rawURL := u.Scheme.String() + ":" + net.JoinHostPort(u.Host, strconv.Itoa(u.Port))
	if u.Scheme == SchemeTypeTURN || u.Scheme == SchemeTypeTURNS {
		rawURL += "?transport=" + u.Proto.String()
	}
	return rawURL
}

// IsSecure returns whether the this URL's scheme describes secure scheme or not.
func (u URI) IsSecure() bool {
	return u.Scheme == SchemeTypeSTUNS || u.Scheme == SchemeTypeTURNS
}
