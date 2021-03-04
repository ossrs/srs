package ice

import (
	"context"
	"net"
	"time"
)

const (
	receiveMTU             = 8192
	defaultLocalPreference = 65535

	// ComponentRTP indicates that the candidate is used for RTP
	ComponentRTP uint16 = 1
	// ComponentRTCP indicates that the candidate is used for RTCP
	ComponentRTCP
)

// Candidate represents an ICE candidate
type Candidate interface {
	// An arbitrary string used in the freezing algorithm to
	// group similar candidates.  It is the same for two candidates that
	// have the same type, base IP address, protocol (UDP, TCP, etc.),
	// and STUN or TURN server.
	Foundation() string

	// ID is a unique identifier for just this candidate
	// Unlike the foundation this is different for each candidate
	ID() string

	// A component is a piece of a data stream.
	// An example is one for RTP, and one for RTCP
	Component() uint16
	SetComponent(uint16)

	// The last time this candidate received traffic
	LastReceived() time.Time

	// The last time this candidate sent traffic
	LastSent() time.Time

	NetworkType() NetworkType
	Address() string
	Port() int

	Priority() uint32

	// A transport address related to a
	//  candidate, which is useful for diagnostics and other purposes
	RelatedAddress() *CandidateRelatedAddress

	String() string
	Type() CandidateType
	TCPType() TCPType

	Equal(other Candidate) bool

	Marshal() string

	addr() net.Addr
	agent() *Agent
	context() context.Context

	close() error
	seen(outbound bool)
	start(a *Agent, conn net.PacketConn, initializedCh <-chan struct{})
	writeTo(raw []byte, dst Candidate) (int, error)
}
