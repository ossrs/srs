// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package ice

import (
	"net"
	"time"

	"github.com/pion/logging"
	"github.com/pion/stun/v3"
)

type pairCandidateSelector interface {
	Start()
	ContactCandidates()
	PingCandidate(local, remote Candidate)
	HandleSuccessResponse(m *stun.Message, local, remote Candidate, remoteAddr net.Addr)
	HandleBindingRequest(m *stun.Message, local, remote Candidate)
}

type controllingSelector struct {
	startTime     time.Time
	agent         *Agent
	nominatedPair *CandidatePair
	log           logging.LeveledLogger
}

func (s *controllingSelector) Start() {
	s.startTime = time.Now()
	s.nominatedPair = nil
}

func (s *controllingSelector) isNominatable(c Candidate) bool {
	switch {
	case c.Type() == CandidateTypeHost:
		return time.Since(s.startTime).Nanoseconds() > s.agent.hostAcceptanceMinWait.Nanoseconds()
	case c.Type() == CandidateTypeServerReflexive:
		return time.Since(s.startTime).Nanoseconds() > s.agent.srflxAcceptanceMinWait.Nanoseconds()
	case c.Type() == CandidateTypePeerReflexive:
		return time.Since(s.startTime).Nanoseconds() > s.agent.prflxAcceptanceMinWait.Nanoseconds()
	case c.Type() == CandidateTypeRelay:
		return time.Since(s.startTime).Nanoseconds() > s.agent.relayAcceptanceMinWait.Nanoseconds()
	}

	s.log.Errorf("Invalid candidate type: %s", c.Type())

	return false
}

func (s *controllingSelector) ContactCandidates() {
	switch {
	case s.agent.getSelectedPair() != nil:
		if s.agent.validateSelectedPair() {
			s.log.Trace("Checking keepalive")
			s.agent.checkKeepalive()
		}
	case s.nominatedPair != nil:
		s.nominatePair(s.nominatedPair)
	default:
		p := s.agent.getBestValidCandidatePair()
		if p != nil && s.isNominatable(p.Local) && s.isNominatable(p.Remote) {
			s.log.Tracef("Nominatable pair found, nominating (%s, %s)", p.Local, p.Remote)
			p.nominated = true
			s.nominatedPair = p
			s.nominatePair(p)

			return
		}
		s.agent.pingAllCandidates()
	}
}

func (s *controllingSelector) nominatePair(pair *CandidatePair) {
	// The controlling agent MUST include the USE-CANDIDATE attribute in
	// order to nominate a candidate pair (Section 8.1.1).  The controlled
	// agent MUST NOT include the USE-CANDIDATE attribute in a Binding
	// request.
	msg, err := stun.Build(stun.BindingRequest, stun.TransactionID,
		stun.NewUsername(s.agent.remoteUfrag+":"+s.agent.localUfrag),
		UseCandidate(),
		AttrControlling(s.agent.tieBreaker),
		PriorityAttr(pair.Local.Priority()),
		stun.NewShortTermIntegrity(s.agent.remotePwd),
		stun.Fingerprint,
	)
	if err != nil {
		s.log.Error(err.Error())

		return
	}

	s.log.Tracef("Ping STUN (nominate candidate pair) from %s to %s", pair.Local, pair.Remote)
	s.agent.sendBindingRequest(msg, pair.Local, pair.Remote)
}

func (s *controllingSelector) HandleBindingRequest(message *stun.Message, local, remote Candidate) { //nolint:cyclop
	s.agent.sendBindingSuccess(message, local, remote)

	pair := s.agent.findPair(local, remote)

	if pair == nil {
		pair = s.agent.addPair(local, remote)
		pair.UpdateRequestReceived()

		return
	}
	pair.UpdateRequestReceived()

	if pair.state == CandidatePairStateSucceeded && s.nominatedPair == nil && s.agent.getSelectedPair() == nil {
		bestPair := s.agent.getBestAvailableCandidatePair()
		if bestPair == nil {
			s.log.Tracef("No best pair available")
		} else if bestPair.equal(pair) && s.isNominatable(pair.Local) && s.isNominatable(pair.Remote) {
			s.log.Tracef(
				"The candidate (%s, %s) is the best candidate available, marking it as nominated",
				pair.Local,
				pair.Remote,
			)
			s.nominatedPair = pair
			s.nominatePair(pair)
		}
	}

	if s.agent.userBindingRequestHandler != nil {
		if shouldSwitch := s.agent.userBindingRequestHandler(message, local, remote, pair); shouldSwitch {
			s.agent.setSelectedPair(pair)
		}
	}
}

func (s *controllingSelector) HandleSuccessResponse(m *stun.Message, local, remote Candidate, remoteAddr net.Addr) {
	ok, pendingRequest, rtt := s.agent.handleInboundBindingSuccess(m.TransactionID)
	if !ok {
		s.log.Warnf("Discard message from (%s), unknown TransactionID 0x%x", remote, m.TransactionID)

		return
	}

	transactionAddr := pendingRequest.destination

	// Assert that NAT is not symmetric
	// https://tools.ietf.org/html/rfc8445#section-7.2.5.2.1
	if !addrEqual(transactionAddr, remoteAddr) {
		s.log.Debugf(
			"Discard message: transaction source and destination does not match expected(%s), actual(%s)",
			transactionAddr,
			remote,
		)

		return
	}

	s.log.Tracef("Inbound STUN (SuccessResponse) from %s to %s", remote, local)
	pair := s.agent.findPair(local, remote)

	if pair == nil {
		// This shouldn't happen
		s.log.Error("Success response from invalid candidate pair")

		return
	}

	pair.state = CandidatePairStateSucceeded
	s.log.Tracef("Found valid candidate pair: %s", pair)
	if pendingRequest.isUseCandidate && s.agent.getSelectedPair() == nil {
		s.agent.setSelectedPair(pair)
	}

	pair.UpdateRoundTripTime(rtt)
}

func (s *controllingSelector) PingCandidate(local, remote Candidate) {
	msg, err := stun.Build(stun.BindingRequest, stun.TransactionID,
		stun.NewUsername(s.agent.remoteUfrag+":"+s.agent.localUfrag),
		AttrControlling(s.agent.tieBreaker),
		PriorityAttr(local.Priority()),
		stun.NewShortTermIntegrity(s.agent.remotePwd),
		stun.Fingerprint,
	)
	if err != nil {
		s.log.Error(err.Error())

		return
	}

	s.agent.sendBindingRequest(msg, local, remote)
}

type controlledSelector struct {
	agent *Agent
	log   logging.LeveledLogger
}

func (s *controlledSelector) Start() {
}

func (s *controlledSelector) ContactCandidates() {
	if s.agent.getSelectedPair() != nil {
		if s.agent.validateSelectedPair() {
			s.log.Trace("Checking keepalive")
			s.agent.checkKeepalive()
		}
	} else {
		s.agent.pingAllCandidates()
	}
}

func (s *controlledSelector) PingCandidate(local, remote Candidate) {
	msg, err := stun.Build(stun.BindingRequest, stun.TransactionID,
		stun.NewUsername(s.agent.remoteUfrag+":"+s.agent.localUfrag),
		AttrControlled(s.agent.tieBreaker),
		PriorityAttr(local.Priority()),
		stun.NewShortTermIntegrity(s.agent.remotePwd),
		stun.Fingerprint,
	)
	if err != nil {
		s.log.Error(err.Error())

		return
	}

	s.agent.sendBindingRequest(msg, local, remote)
}

func (s *controlledSelector) HandleSuccessResponse(m *stun.Message, local, remote Candidate, remoteAddr net.Addr) {
	//nolint:godox
	// TODO according to the standard we should specifically answer a failed nomination:
	// https://tools.ietf.org/html/rfc8445#section-7.3.1.5
	// If the controlled agent does not accept the request from the
	// controlling agent, the controlled agent MUST reject the nomination
	// request with an appropriate error code response (e.g., 400)
	// [RFC5389].

	ok, pendingRequest, rtt := s.agent.handleInboundBindingSuccess(m.TransactionID)
	if !ok {
		s.log.Warnf("Discard message from (%s), unknown TransactionID 0x%x", remote, m.TransactionID)

		return
	}

	transactionAddr := pendingRequest.destination

	// Assert that NAT is not symmetric
	// https://tools.ietf.org/html/rfc8445#section-7.2.5.2.1
	if !addrEqual(transactionAddr, remoteAddr) {
		s.log.Debugf(
			"Discard message: transaction source and destination does not match expected(%s), actual(%s)",
			transactionAddr,
			remote,
		)

		return
	}

	s.log.Tracef("Inbound STUN (SuccessResponse) from %s to %s", remote, local)

	pair := s.agent.findPair(local, remote)
	if pair == nil {
		// This shouldn't happen
		s.log.Error("Success response from invalid candidate pair")

		return
	}

	pair.state = CandidatePairStateSucceeded
	s.log.Tracef("Found valid candidate pair: %s", pair)
	if pair.nominateOnBindingSuccess {
		if selectedPair := s.agent.getSelectedPair(); selectedPair == nil ||
			(selectedPair != pair &&
				(!s.agent.needsToCheckPriorityOnNominated() || selectedPair.priority() <= pair.priority())) {
			s.agent.setSelectedPair(pair)
		} else if selectedPair != pair {
			s.log.Tracef("Ignore nominate new pair %s, already nominated pair %s", pair, selectedPair)
		}
	}

	pair.UpdateRoundTripTime(rtt)
}

func (s *controlledSelector) HandleBindingRequest(message *stun.Message, local, remote Candidate) { //nolint:cyclop
	pair := s.agent.findPair(local, remote)
	if pair == nil {
		pair = s.agent.addPair(local, remote)
	}
	pair.UpdateRequestReceived()

	if message.Contains(stun.AttrUseCandidate) { //nolint:nestif
		// https://tools.ietf.org/html/rfc8445#section-7.3.1.5

		if pair.state == CandidatePairStateSucceeded {
			// If the state of this pair is Succeeded, it means that the check
			// previously sent by this pair produced a successful response and
			// generated a valid pair (Section 7.2.5.3.2).  The agent sets the
			// nominated flag value of the valid pair to true.
			selectedPair := s.agent.getSelectedPair()
			if selectedPair == nil ||
				(selectedPair != pair &&
					(!s.agent.needsToCheckPriorityOnNominated() ||
						selectedPair.priority() <= pair.priority())) {
				s.agent.setSelectedPair(pair)
			} else if selectedPair != pair {
				s.log.Tracef("Ignore nominate new pair %s, already nominated pair %s", pair, selectedPair)
			}
		} else {
			// If the received Binding request triggered a new check to be
			// enqueued in the triggered-check queue (Section 7.3.1.4), once the
			// check is sent and if it generates a successful response, and
			// generates a valid pair, the agent sets the nominated flag of the
			// pair to true.  If the request fails (Section 7.2.5.2), the agent
			// MUST remove the candidate pair from the valid list, set the
			// candidate pair state to Failed, and set the checklist state to
			// Failed.
			pair.nominateOnBindingSuccess = true
		}
	}

	s.agent.sendBindingSuccess(message, local, remote)
	s.PingCandidate(local, remote)

	if s.agent.userBindingRequestHandler != nil {
		if shouldSwitch := s.agent.userBindingRequestHandler(message, local, remote, pair); shouldSwitch {
			s.agent.setSelectedPair(pair)
		}
	}
}

type liteSelector struct {
	pairCandidateSelector
}

// A lite selector should not contact candidates.
func (s *liteSelector) ContactCandidates() {
	if _, ok := s.pairCandidateSelector.(*controllingSelector); ok {
		//nolint:godox
		// https://github.com/pion/ice/issues/96
		// TODO: implement lite controlling agent. For now falling back to full agent.
		// This only happens if both peers are lite. See RFC 8445 S6.1.1 and S6.2
		s.pairCandidateSelector.ContactCandidates()
	} else if v, ok := s.pairCandidateSelector.(*controlledSelector); ok {
		v.agent.validateSelectedPair()
	}
}
