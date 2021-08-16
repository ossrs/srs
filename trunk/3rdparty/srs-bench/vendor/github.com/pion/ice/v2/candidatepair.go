package ice

import (
	"fmt"

	"github.com/pion/stun"
)

func newCandidatePair(local, remote Candidate, controlling bool) *candidatePair {
	return &candidatePair{
		iceRoleControlling: controlling,
		remote:             remote,
		local:              local,
		state:              CandidatePairStateWaiting,
	}
}

// candidatePair represents a combination of a local and remote candidate
type candidatePair struct {
	iceRoleControlling  bool
	remote              Candidate
	local               Candidate
	bindingRequestCount uint16
	state               CandidatePairState
	nominated           bool
}

func (p *candidatePair) String() string {
	if p == nil {
		return ""
	}

	return fmt.Sprintf("prio %d (local, prio %d) %s <-> %s (remote, prio %d)",
		p.Priority(), p.local.Priority(), p.local, p.remote, p.remote.Priority())
}

func (p *candidatePair) Equal(other *candidatePair) bool {
	if p == nil && other == nil {
		return true
	}
	if p == nil || other == nil {
		return false
	}
	return p.local.Equal(other.local) && p.remote.Equal(other.remote)
}

// RFC 5245 - 5.7.2.  Computing Pair Priority and Ordering Pairs
// Let G be the priority for the candidate provided by the controlling
// agent.  Let D be the priority for the candidate provided by the
// controlled agent.
// pair priority = 2^32*MIN(G,D) + 2*MAX(G,D) + (G>D?1:0)
func (p *candidatePair) Priority() uint64 {
	var g uint32
	var d uint32
	if p.iceRoleControlling {
		g = p.local.Priority()
		d = p.remote.Priority()
	} else {
		g = p.remote.Priority()
		d = p.local.Priority()
	}

	// Just implement these here rather
	// than fooling around with the math package
	min := func(x, y uint32) uint64 {
		if x < y {
			return uint64(x)
		}
		return uint64(y)
	}
	max := func(x, y uint32) uint64 {
		if x > y {
			return uint64(x)
		}
		return uint64(y)
	}
	cmp := func(x, y uint32) uint64 {
		if x > y {
			return uint64(1)
		}
		return uint64(0)
	}

	// 1<<32 overflows uint32; and if both g && d are
	// maxUint32, this result would overflow uint64
	return (1<<32-1)*min(g, d) + 2*max(g, d) + cmp(g, d)
}

func (p *candidatePair) Write(b []byte) (int, error) {
	return p.local.writeTo(b, p.remote)
}

func (a *Agent) sendSTUN(msg *stun.Message, local, remote Candidate) {
	_, err := local.writeTo(msg.Raw, remote)
	if err != nil {
		a.log.Tracef("failed to send STUN message: %s", err)
	}
}
