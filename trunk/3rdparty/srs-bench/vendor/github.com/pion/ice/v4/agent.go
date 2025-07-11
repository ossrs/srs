// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

// Package ice implements the Interactive Connectivity Establishment (ICE)
// protocol defined in rfc5245.
package ice

import (
	"context"
	"fmt"
	"math"
	"net"
	"net/netip"
	"strings"
	"sync"
	"sync/atomic"
	"time"

	stunx "github.com/pion/ice/v4/internal/stun"
	"github.com/pion/ice/v4/internal/taskloop"
	"github.com/pion/logging"
	"github.com/pion/mdns/v2"
	"github.com/pion/stun/v3"
	"github.com/pion/transport/v3"
	"github.com/pion/transport/v3/packetio"
	"github.com/pion/transport/v3/stdnet"
	"github.com/pion/transport/v3/vnet"
	"golang.org/x/net/proxy"
)

type bindingRequest struct {
	timestamp      time.Time
	transactionID  [stun.TransactionIDSize]byte
	destination    net.Addr
	isUseCandidate bool
}

// Agent represents the ICE agent.
type Agent struct {
	loop *taskloop.Loop

	onConnectionStateChangeHdlr       atomic.Value // func(ConnectionState)
	onSelectedCandidatePairChangeHdlr atomic.Value // func(Candidate, Candidate)
	onCandidateHdlr                   atomic.Value // func(Candidate)

	onConnected     chan struct{}
	onConnectedOnce sync.Once

	// Force candidate to be contacted immediately (instead of waiting for task ticker)
	forceCandidateContact chan bool

	tieBreaker uint64
	lite       bool

	connectionState ConnectionState
	gatheringState  GatheringState

	mDNSMode MulticastDNSMode
	mDNSName string
	mDNSConn *mdns.Conn

	muHaveStarted sync.Mutex
	startedCh     <-chan struct{}
	startedFn     func()
	isControlling bool

	maxBindingRequests uint16

	hostAcceptanceMinWait  time.Duration
	srflxAcceptanceMinWait time.Duration
	prflxAcceptanceMinWait time.Duration
	relayAcceptanceMinWait time.Duration
	stunGatherTimeout      time.Duration

	tcpPriorityOffset uint16
	disableActiveTCP  bool

	portMin uint16
	portMax uint16

	candidateTypes []CandidateType

	// How long connectivity checks can fail before the ICE Agent
	// goes to disconnected
	disconnectedTimeout time.Duration

	// How long connectivity checks can fail before the ICE Agent
	// goes to failed
	failedTimeout time.Duration

	// How often should we send keepalive packets?
	// 0 means never
	keepaliveInterval time.Duration

	// How often should we run our internal taskLoop to check for state changes when connecting
	checkInterval time.Duration

	localUfrag      string
	localPwd        string
	localCandidates map[NetworkType][]Candidate

	remoteUfrag      string
	remotePwd        string
	remoteCandidates map[NetworkType][]Candidate

	checklist []*CandidatePair
	selector  pairCandidateSelector

	selectedPair atomic.Value // *CandidatePair

	urls         []*stun.URI
	networkTypes []NetworkType

	buf *packetio.Buffer

	// LRU of outbound Binding request Transaction IDs
	pendingBindingRequests []bindingRequest

	// 1:1 D-NAT IP address mapping
	extIPMapper *externalIPMapper

	// Callback that allows user to implement custom behavior
	// for STUN Binding Requests
	userBindingRequestHandler func(m *stun.Message, local, remote Candidate, pair *CandidatePair) bool

	gatherCandidateCancel func()
	gatherCandidateDone   chan struct{}

	connectionStateNotifier       *handlerNotifier
	candidateNotifier             *handlerNotifier
	selectedCandidatePairNotifier *handlerNotifier

	loggerFactory logging.LoggerFactory
	log           logging.LeveledLogger

	net         transport.Net
	tcpMux      TCPMux
	udpMux      UDPMux
	udpMuxSrflx UniversalUDPMux

	interfaceFilter func(string) (keep bool)
	ipFilter        func(net.IP) (keep bool)
	includeLoopback bool

	insecureSkipVerify bool

	proxyDialer proxy.Dialer

	enableUseCandidateCheckPriority bool
}

// NewAgent creates a new Agent.
func NewAgent(config *AgentConfig) (*Agent, error) { //nolint:gocognit,cyclop
	var err error
	if config.PortMax < config.PortMin {
		return nil, ErrPort
	}

	mDNSName := config.MulticastDNSHostName
	if mDNSName == "" {
		if mDNSName, err = generateMulticastDNSName(); err != nil {
			return nil, err
		}
	}

	if !strings.HasSuffix(mDNSName, ".local") || len(strings.Split(mDNSName, ".")) != 2 {
		return nil, ErrInvalidMulticastDNSHostName
	}

	mDNSMode := config.MulticastDNSMode
	if mDNSMode == 0 {
		mDNSMode = MulticastDNSModeQueryOnly
	}

	loggerFactory := config.LoggerFactory
	if loggerFactory == nil {
		loggerFactory = logging.NewDefaultLoggerFactory()
	}
	log := loggerFactory.NewLogger("ice")

	startedCtx, startedFn := context.WithCancel(context.Background())

	agent := &Agent{
		tieBreaker:       globalMathRandomGenerator.Uint64(),
		lite:             config.Lite,
		gatheringState:   GatheringStateNew,
		connectionState:  ConnectionStateNew,
		localCandidates:  make(map[NetworkType][]Candidate),
		remoteCandidates: make(map[NetworkType][]Candidate),
		urls:             config.Urls,
		networkTypes:     config.NetworkTypes,
		onConnected:      make(chan struct{}),
		buf:              packetio.NewBuffer(),
		startedCh:        startedCtx.Done(),
		startedFn:        startedFn,
		portMin:          config.PortMin,
		portMax:          config.PortMax,
		loggerFactory:    loggerFactory,
		log:              log,
		net:              config.Net,
		proxyDialer:      config.ProxyDialer,
		tcpMux:           config.TCPMux,
		udpMux:           config.UDPMux,
		udpMuxSrflx:      config.UDPMuxSrflx,

		mDNSMode: mDNSMode,
		mDNSName: mDNSName,

		gatherCandidateCancel: func() {},

		forceCandidateContact: make(chan bool, 1),

		interfaceFilter: config.InterfaceFilter,

		ipFilter: config.IPFilter,

		insecureSkipVerify: config.InsecureSkipVerify,

		includeLoopback: config.IncludeLoopback,

		disableActiveTCP: config.DisableActiveTCP,

		userBindingRequestHandler: config.BindingRequestHandler,

		enableUseCandidateCheckPriority: config.EnableUseCandidateCheckPriority,
	}
	agent.connectionStateNotifier = &handlerNotifier{
		connectionStateFunc: agent.onConnectionStateChange,
		done:                make(chan struct{}),
	}
	agent.candidateNotifier = &handlerNotifier{candidateFunc: agent.onCandidate, done: make(chan struct{})}
	agent.selectedCandidatePairNotifier = &handlerNotifier{
		candidatePairFunc: agent.onSelectedCandidatePairChange,
		done:              make(chan struct{}),
	}

	if agent.net == nil {
		agent.net, err = stdnet.NewNet()
		if err != nil {
			return nil, fmt.Errorf("failed to create network: %w", err)
		}
	} else if _, isVirtual := agent.net.(*vnet.Net); isVirtual {
		agent.log.Warn("Virtual network is enabled")
		if agent.mDNSMode != MulticastDNSModeDisabled {
			agent.log.Warn("Virtual network does not support mDNS yet")
		}
	}

	localIfcs, _, err := localInterfaces(
		agent.net,
		agent.interfaceFilter,
		agent.ipFilter,
		agent.networkTypes,
		agent.includeLoopback,
	)
	if err != nil {
		return nil, fmt.Errorf("error getting local interfaces: %w", err)
	}

	// Opportunistic mDNS: If we can't open the connection, that's ok: we
	// can continue without it.
	if agent.mDNSConn, agent.mDNSMode, err = createMulticastDNS(
		agent.net,
		agent.networkTypes,
		localIfcs,
		agent.includeLoopback,
		mDNSMode,
		mDNSName,
		log,
		loggerFactory,
	); err != nil {
		log.Warnf("Failed to initialize mDNS %s: %v", mDNSName, err)
	}

	config.initWithDefaults(agent)

	// Make sure the buffer doesn't grow indefinitely.
	// NOTE: We actually won't get anywhere close to this limit.
	// SRTP will constantly read from the endpoint and drop packets if it's full.
	agent.buf.SetLimitSize(maxBufferSize)

	if agent.lite && (len(agent.candidateTypes) != 1 || agent.candidateTypes[0] != CandidateTypeHost) {
		agent.closeMulticastConn()

		return nil, ErrLiteUsingNonHostCandidates
	}

	if len(config.Urls) > 0 &&
		!containsCandidateType(CandidateTypeServerReflexive, agent.candidateTypes) &&
		!containsCandidateType(CandidateTypeRelay, agent.candidateTypes) {
		agent.closeMulticastConn()

		return nil, ErrUselessUrlsProvided
	}

	if err = config.initExtIPMapping(agent); err != nil {
		agent.closeMulticastConn()

		return nil, err
	}

	agent.loop = taskloop.New(func() {
		agent.removeUfragFromMux()
		agent.deleteAllCandidates()
		agent.startedFn()

		if err := agent.buf.Close(); err != nil {
			agent.log.Warnf("Failed to close buffer: %v", err)
		}

		agent.closeMulticastConn()
		agent.updateConnectionState(ConnectionStateClosed)

		agent.gatherCandidateCancel()
		if agent.gatherCandidateDone != nil {
			<-agent.gatherCandidateDone
		}
	})

	// Restart is also used to initialize the agent for the first time
	if err := agent.Restart(config.LocalUfrag, config.LocalPwd); err != nil {
		agent.closeMulticastConn()
		_ = agent.Close()

		return nil, err
	}

	return agent, nil
}

func (a *Agent) startConnectivityChecks(isControlling bool, remoteUfrag, remotePwd string) error {
	a.muHaveStarted.Lock()
	defer a.muHaveStarted.Unlock()
	select {
	case <-a.startedCh:
		return ErrMultipleStart
	default:
	}
	if err := a.SetRemoteCredentials(remoteUfrag, remotePwd); err != nil { //nolint:contextcheck
		return err
	}

	a.log.Debugf("Started agent: isControlling? %t, remoteUfrag: %q, remotePwd: %q", isControlling, remoteUfrag, remotePwd)

	return a.loop.Run(a.loop, func(_ context.Context) {
		a.isControlling = isControlling
		a.remoteUfrag = remoteUfrag
		a.remotePwd = remotePwd

		if isControlling {
			a.selector = &controllingSelector{agent: a, log: a.log}
		} else {
			a.selector = &controlledSelector{agent: a, log: a.log}
		}

		if a.lite {
			a.selector = &liteSelector{pairCandidateSelector: a.selector}
		}

		a.selector.Start()
		a.startedFn()

		a.updateConnectionState(ConnectionStateChecking)

		a.requestConnectivityCheck()
		go a.connectivityChecks() //nolint:contextcheck
	})
}

func (a *Agent) connectivityChecks() { //nolint:cyclop
	lastConnectionState := ConnectionState(0)
	checkingDuration := time.Time{}

	contact := func() {
		if err := a.loop.Run(a.loop, func(_ context.Context) {
			defer func() {
				lastConnectionState = a.connectionState
			}()

			switch a.connectionState {
			case ConnectionStateFailed:
				// The connection is currently failed so don't send any checks
				// In the future it may be restarted though
				return
			case ConnectionStateChecking:
				// We have just entered checking for the first time so update our checking timer
				if lastConnectionState != a.connectionState {
					checkingDuration = time.Now()
				}

				// We have been in checking longer then Disconnect+Failed timeout, set the connection to Failed
				if time.Since(checkingDuration) > a.disconnectedTimeout+a.failedTimeout {
					a.updateConnectionState(ConnectionStateFailed)

					return
				}
			default:
			}

			a.selector.ContactCandidates()
		}); err != nil {
			a.log.Warnf("Failed to start connectivity checks: %v", err)
		}
	}

	timer := time.NewTimer(math.MaxInt64)
	timer.Stop()

	for {
		interval := defaultKeepaliveInterval

		updateInterval := func(x time.Duration) {
			if x != 0 && (interval == 0 || interval > x) {
				interval = x
			}
		}

		switch lastConnectionState {
		case ConnectionStateNew, ConnectionStateChecking: // While connecting, check candidates more frequently
			updateInterval(a.checkInterval)
		case ConnectionStateConnected, ConnectionStateDisconnected:
			updateInterval(a.keepaliveInterval)
		default:
		}
		// Ensure we run our task loop as quickly as the minimum of our various configured timeouts
		updateInterval(a.disconnectedTimeout)
		updateInterval(a.failedTimeout)

		timer.Reset(interval)

		select {
		case <-a.forceCandidateContact:
			if !timer.Stop() {
				<-timer.C
			}
			contact()
		case <-timer.C:
			contact()
		case <-a.loop.Done():
			timer.Stop()

			return
		}
	}
}

func (a *Agent) updateConnectionState(newState ConnectionState) {
	if a.connectionState != newState {
		// Connection has gone to failed, release all gathered candidates
		if newState == ConnectionStateFailed {
			a.removeUfragFromMux()
			a.checklist = make([]*CandidatePair, 0)
			a.pendingBindingRequests = make([]bindingRequest, 0)
			a.setSelectedPair(nil)
			a.deleteAllCandidates()
		}

		a.log.Infof("Setting new connection state: %s", newState)
		a.connectionState = newState
		a.connectionStateNotifier.EnqueueConnectionState(newState)
	}
}

func (a *Agent) setSelectedPair(pair *CandidatePair) {
	if pair == nil {
		var nilPair *CandidatePair
		a.selectedPair.Store(nilPair)
		a.log.Tracef("Unset selected candidate pair")

		return
	}

	pair.nominated = true
	a.selectedPair.Store(pair)
	a.log.Tracef("Set selected candidate pair: %s", pair)

	a.updateConnectionState(ConnectionStateConnected)

	// Notify when the selected pair changes
	a.selectedCandidatePairNotifier.EnqueueSelectedCandidatePair(pair)

	// Signal connected
	a.onConnectedOnce.Do(func() { close(a.onConnected) })
}

func (a *Agent) pingAllCandidates() {
	a.log.Trace("Pinging all candidates")

	if len(a.checklist) == 0 {
		a.log.Warn("Failed to ping without candidate pairs. Connection is not possible yet.")
	}

	for _, p := range a.checklist {
		if p.state == CandidatePairStateWaiting {
			p.state = CandidatePairStateInProgress
		} else if p.state != CandidatePairStateInProgress {
			continue
		}

		if p.bindingRequestCount > a.maxBindingRequests {
			a.log.Tracef("Maximum requests reached for pair %s, marking it as failed", p)
			p.state = CandidatePairStateFailed
		} else {
			a.selector.PingCandidate(p.Local, p.Remote)
			p.bindingRequestCount++
		}
	}
}

func (a *Agent) getBestAvailableCandidatePair() *CandidatePair {
	var best *CandidatePair
	for _, p := range a.checklist {
		if p.state == CandidatePairStateFailed {
			continue
		}

		if best == nil {
			best = p
		} else if best.priority() < p.priority() {
			best = p
		}
	}

	return best
}

func (a *Agent) getBestValidCandidatePair() *CandidatePair {
	var best *CandidatePair
	for _, p := range a.checklist {
		if p.state != CandidatePairStateSucceeded {
			continue
		}

		if best == nil {
			best = p
		} else if best.priority() < p.priority() {
			best = p
		}
	}

	return best
}

func (a *Agent) addPair(local, remote Candidate) *CandidatePair {
	p := newCandidatePair(local, remote, a.isControlling)
	a.checklist = append(a.checklist, p)

	return p
}

func (a *Agent) findPair(local, remote Candidate) *CandidatePair {
	for _, p := range a.checklist {
		if p.Local.Equal(local) && p.Remote.Equal(remote) {
			return p
		}
	}

	return nil
}

// validateSelectedPair checks if the selected pair is (still) valid
// Note: the caller should hold the agent lock.
func (a *Agent) validateSelectedPair() bool {
	selectedPair := a.getSelectedPair()
	if selectedPair == nil {
		return false
	}

	disconnectedTime := time.Since(selectedPair.Remote.LastReceived())

	// Only allow transitions to failed if a.failedTimeout is non-zero
	totalTimeToFailure := a.failedTimeout
	if totalTimeToFailure != 0 {
		totalTimeToFailure += a.disconnectedTimeout
	}

	switch {
	case totalTimeToFailure != 0 && disconnectedTime > totalTimeToFailure:
		a.updateConnectionState(ConnectionStateFailed)
	case a.disconnectedTimeout != 0 && disconnectedTime > a.disconnectedTimeout:
		a.updateConnectionState(ConnectionStateDisconnected)
	default:
		a.updateConnectionState(ConnectionStateConnected)
	}

	return true
}

// checkKeepalive sends STUN Binding Indications to the selected pair
// if no packet has been sent on that pair in the last keepaliveInterval
// Note: the caller should hold the agent lock.
func (a *Agent) checkKeepalive() {
	selectedPair := a.getSelectedPair()
	if selectedPair == nil {
		return
	}

	if a.keepaliveInterval != 0 {
		// We use binding request instead of indication to support refresh consent schemas
		// see https://tools.ietf.org/html/rfc7675
		a.selector.PingCandidate(selectedPair.Local, selectedPair.Remote)
	}
}

// AddRemoteCandidate adds a new remote candidate.
func (a *Agent) AddRemoteCandidate(cand Candidate) error {
	if cand == nil {
		return nil
	}

	// TCP Candidates with TCP type active will probe server passive ones, so
	// no need to do anything with them.
	if cand.TCPType() == TCPTypeActive {
		a.log.Infof("Ignoring remote candidate with tcpType active: %s", cand)

		return nil
	}

	// If we have a mDNS Candidate lets fully resolve it before adding it locally
	if cand.Type() == CandidateTypeHost && strings.HasSuffix(cand.Address(), ".local") {
		if a.mDNSMode == MulticastDNSModeDisabled {
			a.log.Warnf("Remote mDNS candidate added, but mDNS is disabled: (%s)", cand.Address())

			return nil
		}

		hostCandidate, ok := cand.(*CandidateHost)
		if !ok {
			return ErrAddressParseFailed
		}

		go a.resolveAndAddMulticastCandidate(hostCandidate)

		return nil
	}

	go func() {
		if err := a.loop.Run(a.loop, func(_ context.Context) {
			// nolint: contextcheck
			a.addRemoteCandidate(cand)
		}); err != nil {
			a.log.Warnf("Failed to add remote candidate %s: %v", cand.Address(), err)

			return
		}
	}()

	return nil
}

func (a *Agent) resolveAndAddMulticastCandidate(cand *CandidateHost) {
	if a.mDNSConn == nil {
		return
	}

	_, src, err := a.mDNSConn.QueryAddr(cand.context(), cand.Address())
	if err != nil {
		a.log.Warnf("Failed to discover mDNS candidate %s: %v", cand.Address(), err)

		return
	}

	if err = cand.setIPAddr(src); err != nil {
		a.log.Warnf("Failed to discover mDNS candidate %s: %v", cand.Address(), err)

		return
	}

	if err = a.loop.Run(a.loop, func(_ context.Context) {
		// nolint: contextcheck
		a.addRemoteCandidate(cand)
	}); err != nil {
		a.log.Warnf("Failed to add mDNS candidate %s: %v", cand.Address(), err)

		return
	}
}

func (a *Agent) requestConnectivityCheck() {
	select {
	case a.forceCandidateContact <- true:
	default:
	}
}

func (a *Agent) addRemotePassiveTCPCandidate(remoteCandidate Candidate) {
	_, localIPs, err := localInterfaces(
		a.net,
		a.interfaceFilter,
		a.ipFilter,
		[]NetworkType{remoteCandidate.NetworkType()},
		a.includeLoopback,
	)
	if err != nil {
		a.log.Warnf("Failed to iterate local interfaces, host candidates will not be gathered %s", err)

		return
	}

	for i := range localIPs {
		ip, _, _, err := parseAddr(remoteCandidate.addr())
		if err != nil {
			a.log.Warnf("Failed to parse address: %s; error: %s", remoteCandidate.addr(), err)

			continue
		}

		conn := newActiveTCPConn(
			a.loop,
			net.JoinHostPort(localIPs[i].String(), "0"),
			netip.AddrPortFrom(ip, uint16(remoteCandidate.Port())), //nolint:gosec // G115, no overflow, a port
			a.log,
		)

		tcpAddr, ok := conn.LocalAddr().(*net.TCPAddr)
		if !ok {
			closeConnAndLog(conn, a.log, "Failed to create Active ICE-TCP Candidate: %v", errInvalidAddress)

			continue
		}

		localCandidate, err := NewCandidateHost(&CandidateHostConfig{
			Network:   remoteCandidate.NetworkType().String(),
			Address:   localIPs[i].String(),
			Port:      tcpAddr.Port,
			Component: ComponentRTP,
			TCPType:   TCPTypeActive,
		})
		if err != nil {
			closeConnAndLog(conn, a.log, "Failed to create Active ICE-TCP Candidate: %v", err)

			continue
		}

		localCandidate.start(a, conn, a.startedCh)
		a.localCandidates[localCandidate.NetworkType()] = append(
			a.localCandidates[localCandidate.NetworkType()],
			localCandidate,
		)
		a.candidateNotifier.EnqueueCandidate(localCandidate)

		a.addPair(localCandidate, remoteCandidate)
	}
}

// addRemoteCandidate assumes you are holding the lock (must be execute using a.run).
func (a *Agent) addRemoteCandidate(cand Candidate) { //nolint:cyclop
	set := a.remoteCandidates[cand.NetworkType()]

	for _, candidate := range set {
		if candidate.Equal(cand) {
			return
		}
	}

	acceptRemotePassiveTCPCandidate := false
	// Assert that TCP4 or TCP6 is a enabled NetworkType locally
	if !a.disableActiveTCP && cand.TCPType() == TCPTypePassive {
		for _, networkType := range a.networkTypes {
			if cand.NetworkType() == networkType {
				acceptRemotePassiveTCPCandidate = true
			}
		}
	}

	if acceptRemotePassiveTCPCandidate {
		a.addRemotePassiveTCPCandidate(cand)
	}

	set = append(set, cand)
	a.remoteCandidates[cand.NetworkType()] = set

	if cand.TCPType() != TCPTypePassive {
		if localCandidates, ok := a.localCandidates[cand.NetworkType()]; ok {
			for _, localCandidate := range localCandidates {
				a.addPair(localCandidate, cand)
			}
		}
	}

	a.requestConnectivityCheck()
}

func (a *Agent) addCandidate(ctx context.Context, cand Candidate, candidateConn net.PacketConn) error {
	return a.loop.Run(ctx, func(context.Context) {
		set := a.localCandidates[cand.NetworkType()]
		for _, candidate := range set {
			if candidate.Equal(cand) {
				a.log.Debugf("Ignore duplicate candidate: %s", cand)
				if err := cand.close(); err != nil {
					a.log.Warnf("Failed to close duplicate candidate: %v", err)
				}
				if err := candidateConn.Close(); err != nil {
					a.log.Warnf("Failed to close duplicate candidate connection: %v", err)
				}

				return
			}
		}

		a.setCandidateExtensions(cand)
		cand.start(a, candidateConn, a.startedCh)

		set = append(set, cand)
		a.localCandidates[cand.NetworkType()] = set

		if remoteCandidates, ok := a.remoteCandidates[cand.NetworkType()]; ok {
			for _, remoteCandidate := range remoteCandidates {
				a.addPair(cand, remoteCandidate)
			}
		}

		a.requestConnectivityCheck()

		if !cand.filterForLocationTracking() {
			a.candidateNotifier.EnqueueCandidate(cand)
		}
	})
}

func (a *Agent) setCandidateExtensions(cand Candidate) {
	err := cand.AddExtension(CandidateExtension{
		Key:   "ufrag",
		Value: a.localUfrag,
	})
	if err != nil {
		a.log.Errorf("Failed to add ufrag extension to candidate: %v", err)
	}
}

// GetRemoteCandidates returns the remote candidates.
func (a *Agent) GetRemoteCandidates() ([]Candidate, error) {
	var res []Candidate

	err := a.loop.Run(a.loop, func(_ context.Context) {
		var candidates []Candidate
		for _, set := range a.remoteCandidates {
			candidates = append(candidates, set...)
		}
		res = candidates
	})
	if err != nil {
		return nil, err
	}

	return res, nil
}

// GetLocalCandidates returns the local candidates.
func (a *Agent) GetLocalCandidates() ([]Candidate, error) {
	var res []Candidate

	err := a.loop.Run(a.loop, func(_ context.Context) {
		var candidates []Candidate
		for _, set := range a.localCandidates {
			for _, c := range set {
				if c.filterForLocationTracking() {
					continue
				}
				candidates = append(candidates, c)
			}
		}
		res = candidates
	})
	if err != nil {
		return nil, err
	}

	return res, nil
}

// GetLocalUserCredentials returns the local user credentials.
func (a *Agent) GetLocalUserCredentials() (frag string, pwd string, err error) {
	valSet := make(chan struct{})
	err = a.loop.Run(a.loop, func(_ context.Context) {
		frag = a.localUfrag
		pwd = a.localPwd
		close(valSet)
	})

	if err == nil {
		<-valSet
	}

	return
}

// GetRemoteUserCredentials returns the remote user credentials.
func (a *Agent) GetRemoteUserCredentials() (frag string, pwd string, err error) {
	valSet := make(chan struct{})
	err = a.loop.Run(a.loop, func(_ context.Context) {
		frag = a.remoteUfrag
		pwd = a.remotePwd
		close(valSet)
	})

	if err == nil {
		<-valSet
	}

	return
}

func (a *Agent) removeUfragFromMux() {
	if a.tcpMux != nil {
		a.tcpMux.RemoveConnByUfrag(a.localUfrag)
	}
	if a.udpMux != nil {
		a.udpMux.RemoveConnByUfrag(a.localUfrag)
	}
	if a.udpMuxSrflx != nil {
		a.udpMuxSrflx.RemoveConnByUfrag(a.localUfrag)
	}
}

// Close cleans up the Agent.
func (a *Agent) Close() error {
	return a.close(false)
}

// GracefulClose cleans up the Agent and waits for any goroutines it started
// to complete. This is only safe to call outside of Agent callbacks or if in a callback,
// in its own goroutine.
func (a *Agent) GracefulClose() error {
	return a.close(true)
}

func (a *Agent) close(graceful bool) error {
	// the loop is safe to wait on no matter what
	a.loop.Close()

	// but we are in less control of the notifiers, so we will
	// pass through `graceful`.
	a.connectionStateNotifier.Close(graceful)
	a.candidateNotifier.Close(graceful)
	a.selectedCandidatePairNotifier.Close(graceful)

	return nil
}

// Remove all candidates. This closes any listening sockets
// and removes both the local and remote candidate lists.
//
// This is used for restarts, failures and on close.
func (a *Agent) deleteAllCandidates() {
	for net, cs := range a.localCandidates {
		for _, c := range cs {
			if err := c.close(); err != nil {
				a.log.Warnf("Failed to close candidate %s: %v", c, err)
			}
		}
		delete(a.localCandidates, net)
	}
	for net, cs := range a.remoteCandidates {
		for _, c := range cs {
			if err := c.close(); err != nil {
				a.log.Warnf("Failed to close candidate %s: %v", c, err)
			}
		}
		delete(a.remoteCandidates, net)
	}
}

func (a *Agent) findRemoteCandidate(networkType NetworkType, addr net.Addr) Candidate {
	ip, port, _, err := parseAddr(addr)
	if err != nil {
		a.log.Warnf("Failed to parse address: %s; error: %s", addr, err)

		return nil
	}

	set := a.remoteCandidates[networkType]
	for _, c := range set {
		if c.Address() == ip.String() && c.Port() == port {
			return c
		}
	}

	return nil
}

func (a *Agent) sendBindingRequest(msg *stun.Message, local, remote Candidate) {
	a.log.Tracef("Ping STUN from %s to %s", local, remote)

	a.invalidatePendingBindingRequests(time.Now())
	a.pendingBindingRequests = append(a.pendingBindingRequests, bindingRequest{
		timestamp:      time.Now(),
		transactionID:  msg.TransactionID,
		destination:    remote.addr(),
		isUseCandidate: msg.Contains(stun.AttrUseCandidate),
	})

	if pair := a.findPair(local, remote); pair != nil {
		pair.UpdateRequestSent()
	} else {
		a.log.Warnf("Failed to find pair for add binding request from %s to %s", local, remote)
	}
	a.sendSTUN(msg, local, remote)
}

func (a *Agent) sendBindingSuccess(m *stun.Message, local, remote Candidate) {
	base := remote

	ip, port, _, err := parseAddr(base.addr())
	if err != nil {
		a.log.Warnf("Failed to parse address: %s; error: %s", base.addr(), err)

		return
	}

	if out, err := stun.Build(m, stun.BindingSuccess,
		&stun.XORMappedAddress{
			IP:   ip.AsSlice(),
			Port: port,
		},
		stun.NewShortTermIntegrity(a.localPwd),
		stun.Fingerprint,
	); err != nil {
		a.log.Warnf("Failed to handle inbound ICE from: %s to: %s error: %s", local, remote, err)
	} else {
		if pair := a.findPair(local, remote); pair != nil {
			pair.UpdateResponseSent()
		} else {
			a.log.Warnf("Failed to find pair for add binding response from %s to %s", local, remote)
		}
		a.sendSTUN(out, local, remote)
	}
}

// Removes pending binding requests that are over maxBindingRequestTimeout old
//
// Let HTO be the transaction timeout, which SHOULD be 2*RTT if
// RTT is known or 500 ms otherwise.
// https://tools.ietf.org/html/rfc8445#appendix-B.1
func (a *Agent) invalidatePendingBindingRequests(filterTime time.Time) {
	initialSize := len(a.pendingBindingRequests)

	temp := a.pendingBindingRequests[:0]
	for _, bindingRequest := range a.pendingBindingRequests {
		if filterTime.Sub(bindingRequest.timestamp) < maxBindingRequestTimeout {
			temp = append(temp, bindingRequest)
		}
	}

	a.pendingBindingRequests = temp
	if bindRequestsRemoved := initialSize - len(a.pendingBindingRequests); bindRequestsRemoved > 0 {
		a.log.Tracef("Discarded %d binding requests because they expired", bindRequestsRemoved)
	}
}

// Assert that the passed TransactionID is in our pendingBindingRequests and returns the destination
// If the bindingRequest was valid remove it from our pending cache.
func (a *Agent) handleInboundBindingSuccess(id [stun.TransactionIDSize]byte) (bool, *bindingRequest, time.Duration) {
	a.invalidatePendingBindingRequests(time.Now())
	for i := range a.pendingBindingRequests {
		if a.pendingBindingRequests[i].transactionID == id {
			validBindingRequest := a.pendingBindingRequests[i]
			a.pendingBindingRequests = append(a.pendingBindingRequests[:i], a.pendingBindingRequests[i+1:]...)

			return true, &validBindingRequest, time.Since(validBindingRequest.timestamp)
		}
	}

	return false, nil, 0
}

// handleInbound processes STUN traffic from a remote candidate.
func (a *Agent) handleInbound(msg *stun.Message, local Candidate, remote net.Addr) { //nolint:gocognit,cyclop
	var err error
	if msg == nil || local == nil {
		return
	}

	if msg.Type.Method != stun.MethodBinding ||
		!(msg.Type.Class == stun.ClassSuccessResponse ||
			msg.Type.Class == stun.ClassRequest ||
			msg.Type.Class == stun.ClassIndication) {
		a.log.Tracef("Unhandled STUN from %s to %s class(%s) method(%s)", remote, local, msg.Type.Class, msg.Type.Method)

		return
	}

	if a.isControlling {
		if msg.Contains(stun.AttrICEControlling) {
			a.log.Debug("Inbound STUN message: isControlling && a.isControlling == true")

			return
		} else if msg.Contains(stun.AttrUseCandidate) {
			a.log.Debug("Inbound STUN message: useCandidate && a.isControlling == true")

			return
		}
	} else {
		if msg.Contains(stun.AttrICEControlled) {
			a.log.Debug("Inbound STUN message: isControlled && a.isControlling == false")

			return
		}
	}

	remoteCandidate := a.findRemoteCandidate(local.NetworkType(), remote)
	if msg.Type.Class == stun.ClassSuccessResponse { //nolint:nestif
		if err = stun.MessageIntegrity([]byte(a.remotePwd)).Check(msg); err != nil {
			a.log.Warnf("Discard message from (%s), %v", remote, err)

			return
		}

		if remoteCandidate == nil {
			a.log.Warnf("Discard success message from (%s), no such remote", remote)

			return
		}

		a.selector.HandleSuccessResponse(msg, local, remoteCandidate, remote)
	} else if msg.Type.Class == stun.ClassRequest {
		a.log.Tracef(
			"Inbound STUN (Request) from %s to %s, useCandidate: %v",
			remote,
			local,
			msg.Contains(stun.AttrUseCandidate),
		)

		if err = stunx.AssertUsername(msg, a.localUfrag+":"+a.remoteUfrag); err != nil {
			a.log.Warnf("Discard message from (%s), %v", remote, err)

			return
		} else if err = stun.MessageIntegrity([]byte(a.localPwd)).Check(msg); err != nil {
			a.log.Warnf("Discard message from (%s), %v", remote, err)

			return
		}

		if remoteCandidate == nil {
			ip, port, networkType, err := parseAddr(remote)
			if err != nil {
				a.log.Errorf("Failed to create parse remote net.Addr when creating remote prflx candidate: %s", err)

				return
			}

			prflxCandidateConfig := CandidatePeerReflexiveConfig{
				Network:   networkType.String(),
				Address:   ip.String(),
				Port:      port,
				Component: local.Component(),
				RelAddr:   "",
				RelPort:   0,
			}

			prflxCandidate, err := NewCandidatePeerReflexive(&prflxCandidateConfig)
			if err != nil {
				a.log.Errorf("Failed to create new remote prflx candidate (%s)", err)

				return
			}
			remoteCandidate = prflxCandidate

			a.log.Debugf("Adding a new peer-reflexive candidate: %s ", remote)
			a.addRemoteCandidate(remoteCandidate)
		}

		a.selector.HandleBindingRequest(msg, local, remoteCandidate)
	}

	if remoteCandidate != nil {
		remoteCandidate.seen(false)
	}
}

// validateNonSTUNTraffic processes non STUN traffic from a remote candidate,
// and returns true if it is an actual remote candidate.
func (a *Agent) validateNonSTUNTraffic(local Candidate, remote net.Addr) (Candidate, bool) {
	var remoteCandidate Candidate
	if err := a.loop.Run(local.context(), func(context.Context) {
		remoteCandidate = a.findRemoteCandidate(local.NetworkType(), remote)
		if remoteCandidate != nil {
			remoteCandidate.seen(false)
		}
	}); err != nil {
		a.log.Warnf("Failed to validate remote candidate: %v", err)
	}

	return remoteCandidate, remoteCandidate != nil
}

// GetSelectedCandidatePair returns the selected pair or nil if there is none.
func (a *Agent) GetSelectedCandidatePair() (*CandidatePair, error) {
	selectedPair := a.getSelectedPair()
	if selectedPair == nil {
		return nil, nil //nolint:nilnil
	}

	local, err := selectedPair.Local.copy()
	if err != nil {
		return nil, err
	}

	remote, err := selectedPair.Remote.copy()
	if err != nil {
		return nil, err
	}

	return &CandidatePair{Local: local, Remote: remote}, nil
}

func (a *Agent) getSelectedPair() *CandidatePair {
	if selectedPair, ok := a.selectedPair.Load().(*CandidatePair); ok {
		return selectedPair
	}

	return nil
}

func (a *Agent) closeMulticastConn() {
	if a.mDNSConn != nil {
		if err := a.mDNSConn.Close(); err != nil {
			a.log.Warnf("Failed to close mDNS Conn: %v", err)
		}
	}
}

// SetRemoteCredentials sets the credentials of the remote agent.
func (a *Agent) SetRemoteCredentials(remoteUfrag, remotePwd string) error {
	switch {
	case remoteUfrag == "":
		return ErrRemoteUfragEmpty
	case remotePwd == "":
		return ErrRemotePwdEmpty
	}

	return a.loop.Run(a.loop, func(_ context.Context) {
		a.remoteUfrag = remoteUfrag
		a.remotePwd = remotePwd
	})
}

// Restart restarts the ICE Agent with the provided ufrag/pwd
// If no ufrag/pwd is provided the Agent will generate one itself
//
// If there is a gatherer routine currently running, Restart will
// cancel it.
// After a Restart, the user must then call GatherCandidates explicitly
// to start generating new ones.
func (a *Agent) Restart(ufrag, pwd string) error { //nolint:cyclop
	if ufrag == "" {
		var err error
		ufrag, err = generateUFrag()
		if err != nil {
			return err
		}
	}
	if pwd == "" {
		var err error
		pwd, err = generatePwd()
		if err != nil {
			return err
		}
	}

	if len([]rune(ufrag))*8 < 24 {
		return ErrLocalUfragInsufficientBits
	}
	if len([]rune(pwd))*8 < 128 {
		return ErrLocalPwdInsufficientBits
	}

	var err error
	if runErr := a.loop.Run(a.loop, func(_ context.Context) {
		if a.gatheringState == GatheringStateGathering {
			a.gatherCandidateCancel()
		}

		// Clear all agent needed to take back to fresh state
		a.removeUfragFromMux()
		a.localUfrag = ufrag
		a.localPwd = pwd
		a.remoteUfrag = ""
		a.remotePwd = ""
		a.gatheringState = GatheringStateNew
		a.checklist = make([]*CandidatePair, 0)
		a.pendingBindingRequests = make([]bindingRequest, 0)
		a.setSelectedPair(nil)
		a.deleteAllCandidates()
		if a.selector != nil {
			a.selector.Start()
		}

		// Restart is used by NewAgent. Accept/Connect should be used to move to checking
		// for new Agents
		if a.connectionState != ConnectionStateNew {
			a.updateConnectionState(ConnectionStateChecking)
		}
	}); runErr != nil {
		return runErr
	}

	return err
}

func (a *Agent) setGatheringState(newState GatheringState) error {
	done := make(chan struct{})
	if err := a.loop.Run(a.loop, func(context.Context) {
		if a.gatheringState != newState && newState == GatheringStateComplete {
			a.candidateNotifier.EnqueueCandidate(nil)
		}

		a.gatheringState = newState
		close(done)
	}); err != nil {
		return err
	}

	<-done

	return nil
}

func (a *Agent) needsToCheckPriorityOnNominated() bool {
	return !a.lite || a.enableUseCandidateCheckPriority
}
