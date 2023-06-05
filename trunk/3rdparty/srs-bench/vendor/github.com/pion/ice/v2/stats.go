// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package ice

import (
	"time"
)

// CandidatePairStats contains ICE candidate pair statistics
type CandidatePairStats struct {
	// Timestamp is the timestamp associated with this object.
	Timestamp time.Time

	// LocalCandidateID is the ID of the local candidate
	LocalCandidateID string

	// RemoteCandidateID is the ID of the remote candidate
	RemoteCandidateID string

	// State represents the state of the checklist for the local and remote
	// candidates in a pair.
	State CandidatePairState

	// Nominated is true when this valid pair that should be used for media
	// if it is the highest-priority one amongst those whose nominated flag is set
	Nominated bool

	// PacketsSent represents the total number of packets sent on this candidate pair.
	PacketsSent uint32

	// PacketsReceived represents the total number of packets received on this candidate pair.
	PacketsReceived uint32

	// BytesSent represents the total number of payload bytes sent on this candidate pair
	// not including headers or padding.
	BytesSent uint64

	// BytesReceived represents the total number of payload bytes received on this candidate pair
	// not including headers or padding.
	BytesReceived uint64

	// LastPacketSentTimestamp represents the timestamp at which the last packet was
	// sent on this particular candidate pair, excluding STUN packets.
	LastPacketSentTimestamp time.Time

	// LastPacketReceivedTimestamp represents the timestamp at which the last packet
	// was received on this particular candidate pair, excluding STUN packets.
	LastPacketReceivedTimestamp time.Time

	// FirstRequestTimestamp represents the timestamp at which the first STUN request
	// was sent on this particular candidate pair.
	FirstRequestTimestamp time.Time

	// LastRequestTimestamp represents the timestamp at which the last STUN request
	// was sent on this particular candidate pair. The average interval between two
	// consecutive connectivity checks sent can be calculated with
	// (LastRequestTimestamp - FirstRequestTimestamp) / RequestsSent.
	LastRequestTimestamp time.Time

	// LastResponseTimestamp represents the timestamp at which the last STUN response
	// was received on this particular candidate pair.
	LastResponseTimestamp time.Time

	// TotalRoundTripTime represents the sum of all round trip time measurements
	// in seconds since the beginning of the session, based on STUN connectivity
	// check responses (ResponsesReceived), including those that reply to requests
	// that are sent in order to verify consent. The average round trip time can
	// be computed from TotalRoundTripTime by dividing it by ResponsesReceived.
	TotalRoundTripTime float64

	// CurrentRoundTripTime represents the latest round trip time measured in seconds,
	// computed from both STUN connectivity checks, including those that are sent
	// for consent verification.
	CurrentRoundTripTime float64

	// AvailableOutgoingBitrate is calculated by the underlying congestion control
	// by combining the available bitrate for all the outgoing RTP streams using
	// this candidate pair. The bitrate measurement does not count the size of the
	// IP or other transport layers like TCP or UDP. It is similar to the TIAS defined
	// in RFC 3890, i.e., it is measured in bits per second and the bitrate is calculated
	// over a 1 second window.
	AvailableOutgoingBitrate float64

	// AvailableIncomingBitrate is calculated by the underlying congestion control
	// by combining the available bitrate for all the incoming RTP streams using
	// this candidate pair. The bitrate measurement does not count the size of the
	// IP or other transport layers like TCP or UDP. It is similar to the TIAS defined
	// in  RFC 3890, i.e., it is measured in bits per second and the bitrate is
	// calculated over a 1 second window.
	AvailableIncomingBitrate float64

	// CircuitBreakerTriggerCount represents the number of times the circuit breaker
	// is triggered for this particular 5-tuple, ceasing transmission.
	CircuitBreakerTriggerCount uint32

	// RequestsReceived represents the total number of connectivity check requests
	// received (including retransmissions). It is impossible for the receiver to
	// tell whether the request was sent in order to check connectivity or check
	// consent, so all connectivity checks requests are counted here.
	RequestsReceived uint64

	// RequestsSent represents the total number of connectivity check requests
	// sent (not including retransmissions).
	RequestsSent uint64

	// ResponsesReceived represents the total number of connectivity check responses received.
	ResponsesReceived uint64

	// ResponsesSent represents the total number of connectivity check responses sent.
	// Since we cannot distinguish connectivity check requests and consent requests,
	// all responses are counted.
	ResponsesSent uint64

	// RetransmissionsReceived represents the total number of connectivity check
	// request retransmissions received.
	RetransmissionsReceived uint64

	// RetransmissionsSent represents the total number of connectivity check
	// request retransmissions sent.
	RetransmissionsSent uint64

	// ConsentRequestsSent represents the total number of consent requests sent.
	ConsentRequestsSent uint64

	// ConsentExpiredTimestamp represents the timestamp at which the latest valid
	// STUN binding response expired.
	ConsentExpiredTimestamp time.Time
}

// CandidateStats contains ICE candidate statistics related to the ICETransport objects.
type CandidateStats struct {
	// Timestamp is the timestamp associated with this object.
	Timestamp time.Time

	// ID is the candidate ID
	ID string

	// NetworkType represents the type of network interface used by the base of a
	// local candidate (the address the ICE agent sends from). Only present for
	// local candidates; it's not possible to know what type of network interface
	// a remote candidate is using.
	//
	// Note:
	// This stat only tells you about the network interface used by the first "hop";
	// it's possible that a connection will be bottlenecked by another type of network.
	// For example, when using Wi-Fi tethering, the networkType of the relevant candidate
	// would be "wifi", even when the next hop is over a cellular connection.
	NetworkType NetworkType

	// IP is the IP address of the candidate, allowing for IPv4 addresses and
	// IPv6 addresses, but fully qualified domain names (FQDNs) are not allowed.
	IP string

	// Port is the port number of the candidate.
	Port int

	// CandidateType is the "Type" field of the ICECandidate.
	CandidateType CandidateType

	// Priority is the "Priority" field of the ICECandidate.
	Priority uint32

	// URL is the URL of the TURN or STUN server indicated in the that translated
	// this IP address. It is the URL address surfaced in an PeerConnectionICEEvent.
	URL string

	// RelayProtocol is the protocol used by the endpoint to communicate with the
	// TURN server. This is only present for local candidates. Valid values for
	// the TURN URL protocol is one of UDP, TCP, or TLS.
	RelayProtocol string

	// Deleted is true if the candidate has been deleted/freed. For host candidates,
	// this means that any network resources (typically a socket) associated with the
	// candidate have been released. For TURN candidates, this means the TURN allocation
	// is no longer active.
	//
	// Only defined for local candidates. For remote candidates, this property is not applicable.
	Deleted bool
}
