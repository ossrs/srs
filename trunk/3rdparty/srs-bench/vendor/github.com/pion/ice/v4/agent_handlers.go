// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package ice

import "sync"

// OnConnectionStateChange sets a handler that is fired when the connection state changes.
func (a *Agent) OnConnectionStateChange(f func(ConnectionState)) error {
	a.onConnectionStateChangeHdlr.Store(f)

	return nil
}

// OnSelectedCandidatePairChange sets a handler that is fired when the final candidate.
// pair is selected.
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
	if h, ok := a.onSelectedCandidatePairChangeHdlr.Load().(func(Candidate, Candidate)); ok && h != nil {
		h(p.Local, p.Remote)
	}
}

func (a *Agent) onCandidate(c Candidate) {
	if onCandidateHdlr, ok := a.onCandidateHdlr.Load().(func(Candidate)); ok && onCandidateHdlr != nil {
		onCandidateHdlr(c)
	}
}

func (a *Agent) onConnectionStateChange(s ConnectionState) {
	if hdlr, ok := a.onConnectionStateChangeHdlr.Load().(func(ConnectionState)); ok && hdlr != nil {
		hdlr(s)
	}
}

type handlerNotifier struct {
	sync.Mutex
	running   bool
	notifiers sync.WaitGroup

	connectionStates    []ConnectionState
	connectionStateFunc func(ConnectionState)

	candidates    []Candidate
	candidateFunc func(Candidate)

	selectedCandidatePairs []*CandidatePair
	candidatePairFunc      func(*CandidatePair)

	// State for closing
	done chan struct{}
}

func (h *handlerNotifier) Close(graceful bool) {
	if graceful {
		// if we were closed ungracefully before, we now
		// want ot wait.
		defer h.notifiers.Wait()
	}

	h.Lock()

	select {
	case <-h.done:
		h.Unlock()

		return
	default:
	}
	close(h.done)
	h.Unlock()
}

func (h *handlerNotifier) EnqueueConnectionState(state ConnectionState) {
	h.Lock()
	defer h.Unlock()

	select {
	case <-h.done:
		return
	default:
	}

	notify := func() {
		defer h.notifiers.Done()
		for {
			h.Lock()
			if len(h.connectionStates) == 0 {
				h.running = false
				h.Unlock()

				return
			}
			notification := h.connectionStates[0]
			h.connectionStates = h.connectionStates[1:]
			h.Unlock()
			h.connectionStateFunc(notification)
		}
	}

	h.connectionStates = append(h.connectionStates, state)
	if !h.running {
		h.running = true
		h.notifiers.Add(1)
		go notify()
	}
}

func (h *handlerNotifier) EnqueueCandidate(cand Candidate) {
	h.Lock()
	defer h.Unlock()

	select {
	case <-h.done:
		return
	default:
	}

	notify := func() {
		defer h.notifiers.Done()
		for {
			h.Lock()
			if len(h.candidates) == 0 {
				h.running = false
				h.Unlock()

				return
			}
			notification := h.candidates[0]
			h.candidates = h.candidates[1:]
			h.Unlock()
			h.candidateFunc(notification)
		}
	}

	h.candidates = append(h.candidates, cand)
	if !h.running {
		h.running = true
		h.notifiers.Add(1)
		go notify()
	}
}

func (h *handlerNotifier) EnqueueSelectedCandidatePair(pair *CandidatePair) {
	h.Lock()
	defer h.Unlock()

	select {
	case <-h.done:
		return
	default:
	}

	notify := func() {
		defer h.notifiers.Done()
		for {
			h.Lock()
			if len(h.selectedCandidatePairs) == 0 {
				h.running = false
				h.Unlock()

				return
			}
			notification := h.selectedCandidatePairs[0]
			h.selectedCandidatePairs = h.selectedCandidatePairs[1:]
			h.Unlock()
			h.candidatePairFunc(notification)
		}
	}

	h.selectedCandidatePairs = append(h.selectedCandidatePairs, pair)
	if !h.running {
		h.running = true
		h.notifiers.Add(1)
		go notify()
	}
}
