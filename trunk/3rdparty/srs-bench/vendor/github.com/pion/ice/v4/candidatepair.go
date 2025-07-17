// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package ice

import (
	"fmt"
	"sync/atomic"
	"time"

	"github.com/pion/stun/v3"
)

func newCandidatePair(local, remote Candidate, controlling bool) *CandidatePair {
	return &CandidatePair{
		iceRoleControlling: controlling,
		Remote:             remote,
		Local:              local,
		state:              CandidatePairStateWaiting,
	}
}

// CandidatePair is a combination of a local and remote candidate.
type CandidatePair struct {
	iceRoleControlling       bool
	Remote                   Candidate
	Local                    Candidate
	bindingRequestCount      uint16
	state                    CandidatePairState
	nominated                bool
	nominateOnBindingSuccess bool

	// stats
	currentRoundTripTime int64 // in ns
	totalRoundTripTime   int64 // in ns

	requestsReceived  uint64
	requestsSent      uint64
	responsesReceived uint64
	responsesSent     uint64

	firstRequestSentAt     atomic.Value // time.Time
	lastRequestSentAt      atomic.Value // time.Time
	firstReponseReceivedAt atomic.Value // time.Time
	lastResponseReceivedAt atomic.Value // time.Time
	firstRequestReceivedAt atomic.Value // time.Time
	lastRequestReceivedAt  atomic.Value // time.Time
}

func (p *CandidatePair) String() string {
	if p == nil {
		return ""
	}

	return fmt.Sprintf(
		"prio %d (local, prio %d) %s <-> %s (remote, prio %d), state: %s, nominated: %v, nominateOnBindingSuccess: %v",
		p.priority(),
		p.Local.Priority(),
		p.Local,
		p.Remote,
		p.Remote.Priority(),
		p.state,
		p.nominated,
		p.nominateOnBindingSuccess,
	)
}

func (p *CandidatePair) equal(other *CandidatePair) bool {
	if p == nil && other == nil {
		return true
	}
	if p == nil || other == nil {
		return false
	}

	return p.Local.Equal(other.Local) && p.Remote.Equal(other.Remote)
}

// RFC 5245 - 5.7.2.  Computing Pair Priority and Ordering Pairs
// Let G be the priority for the candidate provided by the controlling
// agent.  Let D be the priority for the candidate provided by the
// controlled agent.
// pair priority = 2^32*MIN(G,D) + 2*MAX(G,D) + (G>D?1:0).
func (p *CandidatePair) priority() uint64 {
	var g, d uint32 //nolint:varnamelen // clearer to use g and d here
	if p.iceRoleControlling {
		g = p.Local.Priority()
		d = p.Remote.Priority()
	} else {
		g = p.Remote.Priority()
		d = p.Local.Priority()
	}

	// Just implement these here rather
	// than fooling around with the math package
	localMin := func(x, y uint32) uint64 {
		if x < y {
			return uint64(x)
		}

		return uint64(y)
	}
	localMax := func(x, y uint32) uint64 {
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
	return (1<<32-1)*localMin(g, d) + 2*localMax(g, d) + cmp(g, d)
}

func (p *CandidatePair) Write(b []byte) (int, error) {
	return p.Local.writeTo(b, p.Remote)
}

func (a *Agent) sendSTUN(msg *stun.Message, local, remote Candidate) {
	_, err := local.writeTo(msg.Raw, remote)
	if err != nil {
		a.log.Tracef("Failed to send STUN message: %s", err)
	}
}

// UpdateRoundTripTime sets the current round time of this pair and
// accumulates total round trip time and responses received.
func (p *CandidatePair) UpdateRoundTripTime(rtt time.Duration) {
	rttNs := rtt.Nanoseconds()
	atomic.StoreInt64(&p.currentRoundTripTime, rttNs)
	atomic.AddInt64(&p.totalRoundTripTime, rttNs)
	atomic.AddUint64(&p.responsesReceived, 1)

	now := time.Now()
	p.firstReponseReceivedAt.CompareAndSwap(nil, now)
	p.lastResponseReceivedAt.Store(now)
}

// CurrentRoundTripTime returns the current round trip time in seconds
// https://www.w3.org/TR/webrtc-stats/#dom-rtcicecandidatepairstats-currentroundtriptime
func (p *CandidatePair) CurrentRoundTripTime() float64 {
	return time.Duration(atomic.LoadInt64(&p.currentRoundTripTime)).Seconds()
}

// TotalRoundTripTime returns the current round trip time in seconds
// https://www.w3.org/TR/webrtc-stats/#dom-rtcicecandidatepairstats-totalroundtriptime
func (p *CandidatePair) TotalRoundTripTime() float64 {
	return time.Duration(atomic.LoadInt64(&p.totalRoundTripTime)).Seconds()
}

// RequestsReceived returns the total number of connectivity checks received
// https://www.w3.org/TR/webrtc-stats/#dom-rtcicecandidatepairstats-requestsreceived
func (p *CandidatePair) RequestsReceived() uint64 {
	return atomic.LoadUint64(&p.requestsReceived)
}

// RequestsSent returns the total number of connectivity checks sent
// https://www.w3.org/TR/webrtc-stats/#dom-rtcicecandidatepairstats-requestssent
func (p *CandidatePair) RequestsSent() uint64 {
	return atomic.LoadUint64(&p.requestsSent)
}

// ResponsesReceived returns the total number of connectivity responses received
// https://www.w3.org/TR/webrtc-stats/#dom-rtcicecandidatepairstats-responsesreceived
func (p *CandidatePair) ResponsesReceived() uint64 {
	return atomic.LoadUint64(&p.responsesReceived)
}

// ResponsesSent returns the total number of connectivity responses sent
// https://www.w3.org/TR/webrtc-stats/#dom-rtcicecandidatepairstats-responsessent
func (p *CandidatePair) ResponsesSent() uint64 {
	return atomic.LoadUint64(&p.responsesSent)
}

// FirstRequestSentAt returns the timestamp of the first connectivity check sent.
func (p *CandidatePair) FirstRequestSentAt() time.Time {
	if v, ok := p.firstRequestSentAt.Load().(time.Time); ok {
		return v
	}

	return time.Time{}
}

// LastRequestSentAt returns the timestamp of the last connectivity check sent.
func (p *CandidatePair) LastRequestSentAt() time.Time {
	if v, ok := p.lastRequestSentAt.Load().(time.Time); ok {
		return v
	}

	return time.Time{}
}

// FirstReponseReceivedAt returns the timestamp of the first connectivity response received.
func (p *CandidatePair) FirstReponseReceivedAt() time.Time {
	if v, ok := p.firstReponseReceivedAt.Load().(time.Time); ok {
		return v
	}

	return time.Time{}
}

// LastResponseReceivedAt returns the timestamp of the last connectivity response received.
func (p *CandidatePair) LastResponseReceivedAt() time.Time {
	if v, ok := p.lastResponseReceivedAt.Load().(time.Time); ok {
		return v
	}

	return time.Time{}
}

// FirstRequestReceivedAt returns the timestamp of the first connectivity check received.
func (p *CandidatePair) FirstRequestReceivedAt() time.Time {
	if v, ok := p.firstRequestReceivedAt.Load().(time.Time); ok {
		return v
	}

	return time.Time{}
}

// LastRequestReceivedAt returns the timestamp of the last connectivity check received.
func (p *CandidatePair) LastRequestReceivedAt() time.Time {
	if v, ok := p.lastRequestReceivedAt.Load().(time.Time); ok {
		return v
	}

	return time.Time{}
}

// UpdateRequestSent increments the number of requests sent and updates the timestamp.
func (p *CandidatePair) UpdateRequestSent() {
	atomic.AddUint64(&p.requestsSent, 1)
	now := time.Now()
	p.firstRequestSentAt.CompareAndSwap(nil, now)
	p.lastRequestSentAt.Store(now)
}

// UpdateResponseSent increments the number of responses sent.
func (p *CandidatePair) UpdateResponseSent() {
	atomic.AddUint64(&p.responsesSent, 1)
}

// UpdateRequestReceived increments the number of requests received and updates the timestamp.
func (p *CandidatePair) UpdateRequestReceived() {
	atomic.AddUint64(&p.requestsReceived, 1)
	now := time.Now()
	p.firstRequestReceivedAt.CompareAndSwap(nil, now)
	p.lastRequestReceivedAt.Store(now)
}
