// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package ice

// OnConnectionStateChange sets a handler that is fired when the connection state changes
func (a *Agent) OnConnectionStateChange(f func(ConnectionState)) error {
	a.onConnectionStateChangeHdlr.Store(f)
	return nil
}

// OnSelectedCandidatePairChange sets a handler that is fired when the final candidate
// pair is selected
func (a *Agent) OnSelectedCandidatePairChange(f func(Candidate, Candidate)) error {
	a.onSelectedCandidatePairChangeHdlr.Store(f)
	return nil
}

// OnCandidate sets a handler that is fired when new candidates gathered. When
// the gathering process complete the last candidate is nil.
func (a *Agent) OnCandidate(f func(Candidate)) error {
	a.onCandidateHdlr.Store(f)
	return nil
}

func (a *Agent) onSelectedCandidatePairChange(p *CandidatePair) {
	if h, ok := a.onSelectedCandidatePairChangeHdlr.Load().(func(Candidate, Candidate)); ok {
		h(p.Local, p.Remote)
	}
}

func (a *Agent) onCandidate(c Candidate) {
	if onCandidateHdlr, ok := a.onCandidateHdlr.Load().(func(Candidate)); ok {
		onCandidateHdlr(c)
	}
}

func (a *Agent) onConnectionStateChange(s ConnectionState) {
	if hdlr, ok := a.onConnectionStateChangeHdlr.Load().(func(ConnectionState)); ok {
		hdlr(s)
	}
}

func (a *Agent) candidatePairRoutine() {
	for p := range a.chanCandidatePair {
		a.onSelectedCandidatePairChange(p)
	}
}

func (a *Agent) connectionStateRoutine() {
	for s := range a.chanState {
		go a.onConnectionStateChange(s)
	}
}

func (a *Agent) candidateRoutine() {
	for c := range a.chanCandidate {
		a.onCandidate(c)
	}
}
