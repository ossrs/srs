// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

//go:build !js
// +build !js

package webrtc

import (
	"context"
	"fmt"
	"sync"
	"sync/atomic"
	"time"

	"github.com/pion/ice/v4"
	"github.com/pion/logging"
	"github.com/pion/webrtc/v4/internal/mux"
	"github.com/pion/webrtc/v4/internal/util"
)

// ICETransport allows an application access to information about the ICE
// transport over which packets are sent and received.
type ICETransport struct {
	lock sync.RWMutex

	role ICERole

	onConnectionStateChangeHandler         atomic.Value // func(ICETransportState)
	internalOnConnectionStateChangeHandler atomic.Value // func(ICETransportState)
	onSelectedCandidatePairChangeHandler   atomic.Value // func(*ICECandidatePair)

	state atomic.Value // ICETransportState

	gatherer *ICEGatherer
	conn     *ice.Conn
	mux      *mux.Mux

	ctxCancel func()

	loggerFactory logging.LoggerFactory

	log logging.LeveledLogger
}

// GetSelectedCandidatePair returns the selected candidate pair on which packets are sent
// if there is no selected pair nil is returned.
func (t *ICETransport) GetSelectedCandidatePair() (*ICECandidatePair, error) {
	agent := t.gatherer.getAgent()
	if agent == nil {
		return nil, nil //nolint:nilnil
	}

	icePair, err := agent.GetSelectedCandidatePair()
	if icePair == nil || err != nil {
		return nil, err
	}

	local, err := newICECandidateFromICE(icePair.Local, "", 0)
	if err != nil {
		return nil, err
	}

	remote, err := newICECandidateFromICE(icePair.Remote, "", 0)
	if err != nil {
		return nil, err
	}

	return NewICECandidatePair(&local, &remote), nil
}

// GetSelectedCandidatePairStats returns the selected candidate pair stats on which packets are sent
// if there is no selected pair empty stats, false is returned to indicate stats not available.
func (t *ICETransport) GetSelectedCandidatePairStats() (ICECandidatePairStats, bool) {
	return t.gatherer.getSelectedCandidatePairStats()
}

// NewICETransport creates a new NewICETransport.
func NewICETransport(gatherer *ICEGatherer, loggerFactory logging.LoggerFactory) *ICETransport {
	iceTransport := &ICETransport{
		gatherer:      gatherer,
		loggerFactory: loggerFactory,
		log:           loggerFactory.NewLogger("ortc"),
	}
	iceTransport.setState(ICETransportStateNew)

	return iceTransport
}

// Start incoming connectivity checks based on its configured role.
func (t *ICETransport) Start(gatherer *ICEGatherer, params ICEParameters, role *ICERole) error { //nolint:cyclop
	t.lock.Lock()
	defer t.lock.Unlock()

	if t.State() != ICETransportStateNew {
		return errICETransportNotInNew
	}

	if gatherer != nil {
		t.gatherer = gatherer
	}

	if err := t.ensureGatherer(); err != nil {
		return err
	}

	agent := t.gatherer.getAgent()
	if agent == nil {
		return fmt.Errorf("%w: unable to start ICETransport", errICEAgentNotExist)
	}

	if err := agent.OnConnectionStateChange(func(iceState ice.ConnectionState) {
		state := newICETransportStateFromICE(iceState)

		t.setState(state)
		t.onConnectionStateChange(state)
	}); err != nil {
		return err
	}
	if err := agent.OnSelectedCandidatePairChange(func(local, remote ice.Candidate) {
		candidates, err := newICECandidatesFromICE([]ice.Candidate{local, remote}, "", 0)
		if err != nil {
			t.log.Warnf("%w: %s", errICECandiatesCoversionFailed, err)

			return
		}
		t.onSelectedCandidatePairChange(NewICECandidatePair(&candidates[0], &candidates[1]))
	}); err != nil {
		return err
	}

	if role == nil {
		controlled := ICERoleControlled
		role = &controlled
	}
	t.role = *role

	ctx, ctxCancel := context.WithCancel(context.Background())
	t.ctxCancel = ctxCancel

	// Drop the lock here to allow ICE candidates to be
	// added so that the agent can complete a connection
	t.lock.Unlock()

	var iceConn *ice.Conn
	var err error
	switch *role {
	case ICERoleControlling:
		iceConn, err = agent.Dial(ctx,
			params.UsernameFragment,
			params.Password)

	case ICERoleControlled:
		iceConn, err = agent.Accept(ctx,
			params.UsernameFragment,
			params.Password)

	default:
		err = errICERoleUnknown
	}

	// Reacquire the lock to set the connection/mux
	t.lock.Lock()
	if err != nil {
		return err
	}

	if t.State() == ICETransportStateClosed {
		return errICETransportClosed
	}

	t.conn = iceConn

	config := mux.Config{
		Conn:          t.conn,
		BufferSize:    int(t.gatherer.api.settingEngine.getReceiveMTU()), //nolint:gosec // G115
		LoggerFactory: t.loggerFactory,
	}
	t.mux = mux.NewMux(config)

	return nil
}

// restart is not exposed currently because ORTC has users create a whole new ICETransport
// so for now lets keep it private so we don't cause ORTC users to depend on non-standard APIs.
func (t *ICETransport) restart() error {
	t.lock.Lock()
	defer t.lock.Unlock()

	agent := t.gatherer.getAgent()
	if agent == nil {
		return fmt.Errorf("%w: unable to restart ICETransport", errICEAgentNotExist)
	}

	if err := agent.Restart(
		t.gatherer.api.settingEngine.candidates.UsernameFragment,
		t.gatherer.api.settingEngine.candidates.Password,
	); err != nil {
		return err
	}

	return t.gatherer.Gather()
}

// Stop irreversibly stops the ICETransport.
func (t *ICETransport) Stop() error {
	return t.stop(false /* shouldGracefullyClose */)
}

// GracefulStop irreversibly stops the ICETransport. It also waits
// for any goroutines it started to complete. This is only safe to call outside of
// ICETransport callbacks or if in a callback, in its own goroutine.
func (t *ICETransport) GracefulStop() error {
	return t.stop(true /* shouldGracefullyClose */)
}

func (t *ICETransport) stop(shouldGracefullyClose bool) error {
	t.lock.Lock()
	t.setState(ICETransportStateClosed)

	if t.ctxCancel != nil {
		t.ctxCancel()
	}

	// mux and gatherer can only be set when ICETransport.State != Closed.
	mux := t.mux
	gatherer := t.gatherer
	t.lock.Unlock()

	if mux != nil {
		var closeErrs []error
		if shouldGracefullyClose && gatherer != nil {
			// we can't access icegatherer/icetransport.Close via
			// mux's net.Conn Close so we call it earlier here.
			closeErrs = append(closeErrs, gatherer.GracefulClose())
		}
		closeErrs = append(closeErrs, mux.Close())

		return util.FlattenErrs(closeErrs)
	} else if gatherer != nil {
		if shouldGracefullyClose {
			return gatherer.GracefulClose()
		}

		return gatherer.Close()
	}

	return nil
}

// OnSelectedCandidatePairChange sets a handler that is invoked when a new
// ICE candidate pair is selected.
func (t *ICETransport) OnSelectedCandidatePairChange(f func(*ICECandidatePair)) {
	t.onSelectedCandidatePairChangeHandler.Store(f)
}

func (t *ICETransport) onSelectedCandidatePairChange(pair *ICECandidatePair) {
	if handler, ok := t.onSelectedCandidatePairChangeHandler.Load().(func(*ICECandidatePair)); ok {
		handler(pair)
	}
}

// OnConnectionStateChange sets a handler that is fired when the ICE
// connection state changes.
func (t *ICETransport) OnConnectionStateChange(f func(ICETransportState)) {
	t.onConnectionStateChangeHandler.Store(f)
}

func (t *ICETransport) onConnectionStateChange(state ICETransportState) {
	if handler, ok := t.onConnectionStateChangeHandler.Load().(func(ICETransportState)); ok {
		handler(state)
	}
	if handler, ok := t.internalOnConnectionStateChangeHandler.Load().(func(ICETransportState)); ok {
		handler(state)
	}
}

// Role indicates the current role of the ICE transport.
func (t *ICETransport) Role() ICERole {
	t.lock.RLock()
	defer t.lock.RUnlock()

	return t.role
}

// SetRemoteCandidates sets the sequence of candidates associated with the remote ICETransport.
func (t *ICETransport) SetRemoteCandidates(remoteCandidates []ICECandidate) error {
	t.lock.RLock()
	defer t.lock.RUnlock()

	if err := t.ensureGatherer(); err != nil {
		return err
	}

	agent := t.gatherer.getAgent()
	if agent == nil {
		return fmt.Errorf("%w: unable to set remote candidates", errICEAgentNotExist)
	}

	for _, c := range remoteCandidates {
		i, err := c.ToICE()
		if err != nil {
			return err
		}

		if err = agent.AddRemoteCandidate(i); err != nil {
			return err
		}
	}

	return nil
}

// AddRemoteCandidate adds a candidate associated with the remote ICETransport.
func (t *ICETransport) AddRemoteCandidate(remoteCandidate *ICECandidate) error {
	t.lock.RLock()
	defer t.lock.RUnlock()

	var (
		candidate ice.Candidate
		err       error
	)

	if err = t.ensureGatherer(); err != nil {
		return err
	}

	if remoteCandidate != nil {
		if candidate, err = remoteCandidate.ToICE(); err != nil {
			return err
		}
	}

	agent := t.gatherer.getAgent()
	if agent == nil {
		return fmt.Errorf("%w: unable to add remote candidates", errICEAgentNotExist)
	}

	return agent.AddRemoteCandidate(candidate)
}

// State returns the current ice transport state.
func (t *ICETransport) State() ICETransportState {
	if v, ok := t.state.Load().(ICETransportState); ok {
		return v
	}

	return ICETransportState(0)
}

// GetLocalParameters returns an IceParameters object which provides information
// uniquely identifying the local peer for the duration of the ICE session.
func (t *ICETransport) GetLocalParameters() (ICEParameters, error) {
	if err := t.ensureGatherer(); err != nil {
		return ICEParameters{}, err
	}

	return t.gatherer.GetLocalParameters()
}

// GetRemoteParameters returns an IceParameters object which provides information
// uniquely identifying the remote peer for the duration of the ICE session.
func (t *ICETransport) GetRemoteParameters() (ICEParameters, error) {
	t.lock.Lock()
	defer t.lock.Unlock()

	agent := t.gatherer.getAgent()
	if agent == nil {
		return ICEParameters{}, fmt.Errorf("%w: unable to get remote parameters", errICEAgentNotExist)
	}

	uFrag, uPwd, err := agent.GetRemoteUserCredentials()
	if err != nil {
		return ICEParameters{}, fmt.Errorf("%w: unable to get remote parameters", err)
	}

	return ICEParameters{
		UsernameFragment: uFrag,
		Password:         uPwd,
	}, nil
}

func (t *ICETransport) setState(i ICETransportState) {
	t.state.Store(i)
}

func (t *ICETransport) newEndpoint(f mux.MatchFunc) *mux.Endpoint {
	t.lock.Lock()
	defer t.lock.Unlock()

	return t.mux.NewEndpoint(f)
}

func (t *ICETransport) ensureGatherer() error {
	if t.gatherer == nil {
		return errICEGathererNotStarted
	} else if t.gatherer.getAgent() == nil {
		if err := t.gatherer.createAgent(); err != nil {
			return err
		}
	}

	return nil
}

func (t *ICETransport) collectStats(collector *statsReportCollector) {
	t.lock.Lock()
	conn := t.conn
	t.lock.Unlock()

	collector.Collecting()

	stats := TransportStats{
		Timestamp: statsTimestampFrom(time.Now()),
		Type:      StatsTypeTransport,
		ID:        "iceTransport",
	}

	if conn != nil {
		stats.BytesSent = conn.BytesSent()
		stats.BytesReceived = conn.BytesReceived()
	}

	collector.Collect(stats.ID, stats)
}

func (t *ICETransport) haveRemoteCredentialsChange(newUfrag, newPwd string) bool {
	t.lock.Lock()
	defer t.lock.Unlock()

	agent := t.gatherer.getAgent()
	if agent == nil {
		return false
	}

	uFrag, uPwd, err := agent.GetRemoteUserCredentials()
	if err != nil {
		return false
	}

	return uFrag != newUfrag || uPwd != newPwd
}

func (t *ICETransport) setRemoteCredentials(newUfrag, newPwd string) error {
	t.lock.Lock()
	defer t.lock.Unlock()

	agent := t.gatherer.getAgent()
	if agent == nil {
		return fmt.Errorf("%w: unable to SetRemoteCredentials", errICEAgentNotExist)
	}

	return agent.SetRemoteCredentials(newUfrag, newPwd)
}
