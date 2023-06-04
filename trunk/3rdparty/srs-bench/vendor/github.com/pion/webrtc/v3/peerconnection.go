// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

//go:build !js
// +build !js

package webrtc

import (
	"crypto/ecdsa"
	"crypto/elliptic"
	"crypto/rand"
	"errors"
	"fmt"
	"io"
	"strconv"
	"strings"
	"sync"
	"sync/atomic"
	"time"

	"github.com/pion/ice/v2"
	"github.com/pion/interceptor"
	"github.com/pion/logging"
	"github.com/pion/rtcp"
	"github.com/pion/sdp/v3"
	"github.com/pion/srtp/v2"
	"github.com/pion/webrtc/v3/internal/util"
	"github.com/pion/webrtc/v3/pkg/rtcerr"
)

// PeerConnection represents a WebRTC connection that establishes a
// peer-to-peer communications with another PeerConnection instance in a
// browser, or to another endpoint implementing the required protocols.
type PeerConnection struct {
	statsID string
	mu      sync.RWMutex

	sdpOrigin sdp.Origin

	// ops is an operations queue which will ensure the enqueued actions are
	// executed in order. It is used for asynchronously, but serially processing
	// remote and local descriptions
	ops *operations

	configuration Configuration

	currentLocalDescription  *SessionDescription
	pendingLocalDescription  *SessionDescription
	currentRemoteDescription *SessionDescription
	pendingRemoteDescription *SessionDescription
	signalingState           SignalingState
	iceConnectionState       atomic.Value // ICEConnectionState
	connectionState          atomic.Value // PeerConnectionState

	idpLoginURL *string

	isClosed               *atomicBool
	isNegotiationNeeded    *atomicBool
	negotiationNeededState negotiationNeededState

	lastOffer  string
	lastAnswer string

	// a value containing the last known greater mid value
	// we internally generate mids as numbers. Needed since JSEP
	// requires that when reusing a media section a new unique mid
	// should be defined (see JSEP 3.4.1).
	greaterMid int

	rtpTransceivers []*RTPTransceiver

	onSignalingStateChangeHandler     func(SignalingState)
	onICEConnectionStateChangeHandler atomic.Value // func(ICEConnectionState)
	onConnectionStateChangeHandler    atomic.Value // func(PeerConnectionState)
	onTrackHandler                    func(*TrackRemote, *RTPReceiver)
	onDataChannelHandler              func(*DataChannel)
	onNegotiationNeededHandler        atomic.Value // func()

	iceGatherer   *ICEGatherer
	iceTransport  *ICETransport
	dtlsTransport *DTLSTransport
	sctpTransport *SCTPTransport

	// A reference to the associated API state used by this connection
	api *API
	log logging.LeveledLogger

	interceptorRTCPWriter interceptor.RTCPWriter
}

// NewPeerConnection creates a PeerConnection with the default codecs and
// interceptors.  See RegisterDefaultCodecs and RegisterDefaultInterceptors.
//
// If you wish to customize the set of available codecs or the set of
// active interceptors, create a MediaEngine and call api.NewPeerConnection
// instead of this function.
func NewPeerConnection(configuration Configuration) (*PeerConnection, error) {
	m := &MediaEngine{}
	if err := m.RegisterDefaultCodecs(); err != nil {
		return nil, err
	}

	i := &interceptor.Registry{}
	if err := RegisterDefaultInterceptors(m, i); err != nil {
		return nil, err
	}

	api := NewAPI(WithMediaEngine(m), WithInterceptorRegistry(i))
	return api.NewPeerConnection(configuration)
}

// NewPeerConnection creates a new PeerConnection with the provided configuration against the received API object
func (api *API) NewPeerConnection(configuration Configuration) (*PeerConnection, error) {
	// https://w3c.github.io/webrtc-pc/#constructor (Step #2)
	// Some variables defined explicitly despite their implicit zero values to
	// allow better readability to understand what is happening.
	pc := &PeerConnection{
		statsID: fmt.Sprintf("PeerConnection-%d", time.Now().UnixNano()),
		configuration: Configuration{
			ICEServers:           []ICEServer{},
			ICETransportPolicy:   ICETransportPolicyAll,
			BundlePolicy:         BundlePolicyBalanced,
			RTCPMuxPolicy:        RTCPMuxPolicyRequire,
			Certificates:         []Certificate{},
			ICECandidatePoolSize: 0,
		},
		ops:                    newOperations(),
		isClosed:               &atomicBool{},
		isNegotiationNeeded:    &atomicBool{},
		negotiationNeededState: negotiationNeededStateEmpty,
		lastOffer:              "",
		lastAnswer:             "",
		greaterMid:             -1,
		signalingState:         SignalingStateStable,

		api: api,
		log: api.settingEngine.LoggerFactory.NewLogger("pc"),
	}
	pc.iceConnectionState.Store(ICEConnectionStateNew)
	pc.connectionState.Store(PeerConnectionStateNew)

	i, err := api.interceptorRegistry.Build("")
	if err != nil {
		return nil, err
	}

	pc.api = &API{
		settingEngine: api.settingEngine,
		interceptor:   i,
	}

	if api.settingEngine.disableMediaEngineCopy {
		pc.api.mediaEngine = api.mediaEngine
	} else {
		pc.api.mediaEngine = api.mediaEngine.copy()
	}

	if err = pc.initConfiguration(configuration); err != nil {
		return nil, err
	}

	pc.iceGatherer, err = pc.createICEGatherer()
	if err != nil {
		return nil, err
	}

	// Create the ice transport
	iceTransport := pc.createICETransport()
	pc.iceTransport = iceTransport

	// Create the DTLS transport
	dtlsTransport, err := pc.api.NewDTLSTransport(pc.iceTransport, pc.configuration.Certificates)
	if err != nil {
		return nil, err
	}
	pc.dtlsTransport = dtlsTransport

	// Create the SCTP transport
	pc.sctpTransport = pc.api.NewSCTPTransport(pc.dtlsTransport)

	// Wire up the on datachannel handler
	pc.sctpTransport.OnDataChannel(func(d *DataChannel) {
		pc.mu.RLock()
		handler := pc.onDataChannelHandler
		pc.mu.RUnlock()
		if handler != nil {
			handler(d)
		}
	})

	pc.interceptorRTCPWriter = pc.api.interceptor.BindRTCPWriter(interceptor.RTCPWriterFunc(pc.writeRTCP))

	return pc, nil
}

// initConfiguration defines validation of the specified Configuration and
// its assignment to the internal configuration variable. This function differs
// from its SetConfiguration counterpart because most of the checks do not
// include verification statements related to the existing state. Thus the
// function describes only minor verification of some the struct variables.
func (pc *PeerConnection) initConfiguration(configuration Configuration) error {
	if configuration.PeerIdentity != "" {
		pc.configuration.PeerIdentity = configuration.PeerIdentity
	}

	// https://www.w3.org/TR/webrtc/#constructor (step #3)
	if len(configuration.Certificates) > 0 {
		now := time.Now()
		for _, x509Cert := range configuration.Certificates {
			if !x509Cert.Expires().IsZero() && now.After(x509Cert.Expires()) {
				return &rtcerr.InvalidAccessError{Err: ErrCertificateExpired}
			}
			pc.configuration.Certificates = append(pc.configuration.Certificates, x509Cert)
		}
	} else {
		sk, err := ecdsa.GenerateKey(elliptic.P256(), rand.Reader)
		if err != nil {
			return &rtcerr.UnknownError{Err: err}
		}
		certificate, err := GenerateCertificate(sk)
		if err != nil {
			return err
		}
		pc.configuration.Certificates = []Certificate{*certificate}
	}

	if configuration.BundlePolicy != BundlePolicy(Unknown) {
		pc.configuration.BundlePolicy = configuration.BundlePolicy
	}

	if configuration.RTCPMuxPolicy != RTCPMuxPolicy(Unknown) {
		pc.configuration.RTCPMuxPolicy = configuration.RTCPMuxPolicy
	}

	if configuration.ICECandidatePoolSize != 0 {
		pc.configuration.ICECandidatePoolSize = configuration.ICECandidatePoolSize
	}

	if configuration.ICETransportPolicy != ICETransportPolicy(Unknown) {
		pc.configuration.ICETransportPolicy = configuration.ICETransportPolicy
	}

	if configuration.SDPSemantics != SDPSemantics(Unknown) {
		pc.configuration.SDPSemantics = configuration.SDPSemantics
	}

	sanitizedICEServers := configuration.getICEServers()
	if len(sanitizedICEServers) > 0 {
		for _, server := range sanitizedICEServers {
			if err := server.validate(); err != nil {
				return err
			}
		}
		pc.configuration.ICEServers = sanitizedICEServers
	}

	return nil
}

// OnSignalingStateChange sets an event handler which is invoked when the
// peer connection's signaling state changes
func (pc *PeerConnection) OnSignalingStateChange(f func(SignalingState)) {
	pc.mu.Lock()
	defer pc.mu.Unlock()
	pc.onSignalingStateChangeHandler = f
}

func (pc *PeerConnection) onSignalingStateChange(newState SignalingState) {
	pc.mu.RLock()
	handler := pc.onSignalingStateChangeHandler
	pc.mu.RUnlock()

	pc.log.Infof("signaling state changed to %s", newState)
	if handler != nil {
		go handler(newState)
	}
}

// OnDataChannel sets an event handler which is invoked when a data
// channel message arrives from a remote peer.
func (pc *PeerConnection) OnDataChannel(f func(*DataChannel)) {
	pc.mu.Lock()
	defer pc.mu.Unlock()
	pc.onDataChannelHandler = f
}

// OnNegotiationNeeded sets an event handler which is invoked when
// a change has occurred which requires session negotiation
func (pc *PeerConnection) OnNegotiationNeeded(f func()) {
	pc.onNegotiationNeededHandler.Store(f)
}

// onNegotiationNeeded enqueues negotiationNeededOp if necessary
// caller of this method should hold `pc.mu` lock
func (pc *PeerConnection) onNegotiationNeeded() {
	// https://w3c.github.io/webrtc-pc/#updating-the-negotiation-needed-flag
	// non-canon step 1
	if pc.negotiationNeededState == negotiationNeededStateRun {
		pc.negotiationNeededState = negotiationNeededStateQueue
		return
	} else if pc.negotiationNeededState == negotiationNeededStateQueue {
		return
	}
	pc.negotiationNeededState = negotiationNeededStateRun
	pc.ops.Enqueue(pc.negotiationNeededOp)
}

func (pc *PeerConnection) negotiationNeededOp() {
	// Don't run NegotiatedNeeded checks if OnNegotiationNeeded is not set
	if handler, ok := pc.onNegotiationNeededHandler.Load().(func()); !ok || handler == nil {
		return
	}

	// https://www.w3.org/TR/webrtc/#updating-the-negotiation-needed-flag
	// Step 2.1
	if pc.isClosed.get() {
		return
	}
	// non-canon step 2.2
	if !pc.ops.IsEmpty() {
		pc.ops.Enqueue(pc.negotiationNeededOp)
		return
	}

	// non-canon, run again if there was a request
	defer func() {
		pc.mu.Lock()
		defer pc.mu.Unlock()
		if pc.negotiationNeededState == negotiationNeededStateQueue {
			defer pc.onNegotiationNeeded()
		}
		pc.negotiationNeededState = negotiationNeededStateEmpty
	}()

	// Step 2.3
	if pc.SignalingState() != SignalingStateStable {
		return
	}

	// Step 2.4
	if !pc.checkNegotiationNeeded() {
		pc.isNegotiationNeeded.set(false)
		return
	}

	// Step 2.5
	if pc.isNegotiationNeeded.get() {
		return
	}

	// Step 2.6
	pc.isNegotiationNeeded.set(true)

	// Step 2.7
	if handler, ok := pc.onNegotiationNeededHandler.Load().(func()); ok && handler != nil {
		handler()
	}
}

func (pc *PeerConnection) checkNegotiationNeeded() bool { //nolint:gocognit
	// To check if negotiation is needed for connection, perform the following checks:
	// Skip 1, 2 steps
	// Step 3
	pc.mu.Lock()
	defer pc.mu.Unlock()

	localDesc := pc.currentLocalDescription
	remoteDesc := pc.currentRemoteDescription

	if localDesc == nil {
		return true
	}

	pc.sctpTransport.lock.Lock()
	lenDataChannel := len(pc.sctpTransport.dataChannels)
	pc.sctpTransport.lock.Unlock()

	if lenDataChannel != 0 && haveDataChannel(localDesc) == nil {
		return true
	}

	for _, t := range pc.rtpTransceivers {
		// https://www.w3.org/TR/webrtc/#dfn-update-the-negotiation-needed-flag
		// Step 5.1
		// if t.stopping && !t.stopped {
		// 	return true
		// }
		m := getByMid(t.Mid(), localDesc)
		// Step 5.2
		if !t.stopped && m == nil {
			return true
		}
		if !t.stopped && m != nil {
			// Step 5.3.1
			if t.Direction() == RTPTransceiverDirectionSendrecv || t.Direction() == RTPTransceiverDirectionSendonly {
				descMsid, okMsid := m.Attribute(sdp.AttrKeyMsid)
				sender := t.Sender()
				if sender == nil {
					return true
				}
				track := sender.Track()
				if !okMsid || descMsid != track.StreamID()+" "+track.ID() {
					return true
				}
			}
			switch localDesc.Type {
			case SDPTypeOffer:
				// Step 5.3.2
				rm := getByMid(t.Mid(), remoteDesc)
				if rm == nil {
					return true
				}

				if getPeerDirection(m) != t.Direction() && getPeerDirection(rm) != t.Direction().Revers() {
					return true
				}
			case SDPTypeAnswer:
				// Step 5.3.3
				if _, ok := m.Attribute(t.Direction().String()); !ok {
					return true
				}
			default:
			}
		}
		// Step 5.4
		if t.stopped && t.Mid() != "" {
			if getByMid(t.Mid(), localDesc) != nil || getByMid(t.Mid(), remoteDesc) != nil {
				return true
			}
		}
	}
	// Step 6
	return false
}

// OnICECandidate sets an event handler which is invoked when a new ICE
// candidate is found.
// ICE candidate gathering only begins when SetLocalDescription or
// SetRemoteDescription is called.
// Take note that the handler will be called with a nil pointer when
// gathering is finished.
func (pc *PeerConnection) OnICECandidate(f func(*ICECandidate)) {
	pc.iceGatherer.OnLocalCandidate(f)
}

// OnICEGatheringStateChange sets an event handler which is invoked when the
// ICE candidate gathering state has changed.
func (pc *PeerConnection) OnICEGatheringStateChange(f func(ICEGathererState)) {
	pc.iceGatherer.OnStateChange(f)
}

// OnTrack sets an event handler which is called when remote track
// arrives from a remote peer.
func (pc *PeerConnection) OnTrack(f func(*TrackRemote, *RTPReceiver)) {
	pc.mu.Lock()
	defer pc.mu.Unlock()
	pc.onTrackHandler = f
}

func (pc *PeerConnection) onTrack(t *TrackRemote, r *RTPReceiver) {
	pc.mu.RLock()
	handler := pc.onTrackHandler
	pc.mu.RUnlock()

	pc.log.Debugf("got new track: %+v", t)
	if t != nil {
		if handler != nil {
			go handler(t, r)
		} else {
			pc.log.Warnf("OnTrack unset, unable to handle incoming media streams")
		}
	}
}

// OnICEConnectionStateChange sets an event handler which is called
// when an ICE connection state is changed.
func (pc *PeerConnection) OnICEConnectionStateChange(f func(ICEConnectionState)) {
	pc.onICEConnectionStateChangeHandler.Store(f)
}

func (pc *PeerConnection) onICEConnectionStateChange(cs ICEConnectionState) {
	pc.iceConnectionState.Store(cs)
	pc.log.Infof("ICE connection state changed: %s", cs)
	if handler, ok := pc.onICEConnectionStateChangeHandler.Load().(func(ICEConnectionState)); ok && handler != nil {
		handler(cs)
	}
}

// OnConnectionStateChange sets an event handler which is called
// when the PeerConnectionState has changed
func (pc *PeerConnection) OnConnectionStateChange(f func(PeerConnectionState)) {
	pc.onConnectionStateChangeHandler.Store(f)
}

func (pc *PeerConnection) onConnectionStateChange(cs PeerConnectionState) {
	pc.connectionState.Store(cs)
	pc.log.Infof("peer connection state changed: %s", cs)
	if handler, ok := pc.onConnectionStateChangeHandler.Load().(func(PeerConnectionState)); ok && handler != nil {
		go handler(cs)
	}
}

// SetConfiguration updates the configuration of this PeerConnection object.
func (pc *PeerConnection) SetConfiguration(configuration Configuration) error { //nolint:gocognit
	// https://www.w3.org/TR/webrtc/#dom-rtcpeerconnection-setconfiguration (step #2)
	if pc.isClosed.get() {
		return &rtcerr.InvalidStateError{Err: ErrConnectionClosed}
	}

	// https://www.w3.org/TR/webrtc/#set-the-configuration (step #3)
	if configuration.PeerIdentity != "" {
		if configuration.PeerIdentity != pc.configuration.PeerIdentity {
			return &rtcerr.InvalidModificationError{Err: ErrModifyingPeerIdentity}
		}
		pc.configuration.PeerIdentity = configuration.PeerIdentity
	}

	// https://www.w3.org/TR/webrtc/#set-the-configuration (step #4)
	if len(configuration.Certificates) > 0 {
		if len(configuration.Certificates) != len(pc.configuration.Certificates) {
			return &rtcerr.InvalidModificationError{Err: ErrModifyingCertificates}
		}

		for i, certificate := range configuration.Certificates {
			if !pc.configuration.Certificates[i].Equals(certificate) {
				return &rtcerr.InvalidModificationError{Err: ErrModifyingCertificates}
			}
		}
		pc.configuration.Certificates = configuration.Certificates
	}

	// https://www.w3.org/TR/webrtc/#set-the-configuration (step #5)
	if configuration.BundlePolicy != BundlePolicy(Unknown) {
		if configuration.BundlePolicy != pc.configuration.BundlePolicy {
			return &rtcerr.InvalidModificationError{Err: ErrModifyingBundlePolicy}
		}
		pc.configuration.BundlePolicy = configuration.BundlePolicy
	}

	// https://www.w3.org/TR/webrtc/#set-the-configuration (step #6)
	if configuration.RTCPMuxPolicy != RTCPMuxPolicy(Unknown) {
		if configuration.RTCPMuxPolicy != pc.configuration.RTCPMuxPolicy {
			return &rtcerr.InvalidModificationError{Err: ErrModifyingRTCPMuxPolicy}
		}
		pc.configuration.RTCPMuxPolicy = configuration.RTCPMuxPolicy
	}

	// https://www.w3.org/TR/webrtc/#set-the-configuration (step #7)
	if configuration.ICECandidatePoolSize != 0 {
		if pc.configuration.ICECandidatePoolSize != configuration.ICECandidatePoolSize &&
			pc.LocalDescription() != nil {
			return &rtcerr.InvalidModificationError{Err: ErrModifyingICECandidatePoolSize}
		}
		pc.configuration.ICECandidatePoolSize = configuration.ICECandidatePoolSize
	}

	// https://www.w3.org/TR/webrtc/#set-the-configuration (step #8)
	if configuration.ICETransportPolicy != ICETransportPolicy(Unknown) {
		pc.configuration.ICETransportPolicy = configuration.ICETransportPolicy
	}

	// https://www.w3.org/TR/webrtc/#set-the-configuration (step #11)
	if len(configuration.ICEServers) > 0 {
		// https://www.w3.org/TR/webrtc/#set-the-configuration (step #11.3)
		for _, server := range configuration.ICEServers {
			if err := server.validate(); err != nil {
				return err
			}
		}
		pc.configuration.ICEServers = configuration.ICEServers
	}
	return nil
}

// GetConfiguration returns a Configuration object representing the current
// configuration of this PeerConnection object. The returned object is a
// copy and direct mutation on it will not take affect until SetConfiguration
// has been called with Configuration passed as its only argument.
// https://www.w3.org/TR/webrtc/#dom-rtcpeerconnection-getconfiguration
func (pc *PeerConnection) GetConfiguration() Configuration {
	return pc.configuration
}

func (pc *PeerConnection) getStatsID() string {
	pc.mu.RLock()
	defer pc.mu.RUnlock()
	return pc.statsID
}

// hasLocalDescriptionChanged returns whether local media (rtpTransceivers) has changed
// caller of this method should hold `pc.mu` lock
func (pc *PeerConnection) hasLocalDescriptionChanged(desc *SessionDescription) bool {
	for _, t := range pc.rtpTransceivers {
		m := getByMid(t.Mid(), desc)
		if m == nil {
			return true
		}

		if getPeerDirection(m) != t.Direction() {
			return true
		}
	}
	return false
}

// CreateOffer starts the PeerConnection and generates the localDescription
// https://w3c.github.io/webrtc-pc/#dom-rtcpeerconnection-createoffer
func (pc *PeerConnection) CreateOffer(options *OfferOptions) (SessionDescription, error) { //nolint:gocognit
	useIdentity := pc.idpLoginURL != nil
	switch {
	case useIdentity:
		return SessionDescription{}, errIdentityProviderNotImplemented
	case pc.isClosed.get():
		return SessionDescription{}, &rtcerr.InvalidStateError{Err: ErrConnectionClosed}
	}

	if options != nil && options.ICERestart {
		if err := pc.iceTransport.restart(); err != nil {
			return SessionDescription{}, err
		}
	}

	var (
		d     *sdp.SessionDescription
		offer SessionDescription
		err   error
	)

	// This may be necessary to recompute if, for example, createOffer was called when only an
	// audio RTCRtpTransceiver was added to connection, but while performing the in-parallel
	// steps to create an offer, a video RTCRtpTransceiver was added, requiring additional
	// inspection of video system resources.
	count := 0
	pc.mu.Lock()
	defer pc.mu.Unlock()
	for {
		// We cache current transceivers to ensure they aren't
		// mutated during offer generation. We later check if they have
		// been mutated and recompute the offer if necessary.
		currentTransceivers := pc.rtpTransceivers

		// in-parallel steps to create an offer
		// https://w3c.github.io/webrtc-pc/#dfn-in-parallel-steps-to-create-an-offer
		isPlanB := pc.configuration.SDPSemantics == SDPSemanticsPlanB
		if pc.currentRemoteDescription != nil && isPlanB {
			isPlanB = descriptionPossiblyPlanB(pc.currentRemoteDescription)
		}

		// include unmatched local transceivers
		if !isPlanB {
			// update the greater mid if the remote description provides a greater one
			if pc.currentRemoteDescription != nil {
				var numericMid int
				for _, media := range pc.currentRemoteDescription.parsed.MediaDescriptions {
					mid := getMidValue(media)
					if mid == "" {
						continue
					}
					numericMid, err = strconv.Atoi(mid)
					if err != nil {
						continue
					}
					if numericMid > pc.greaterMid {
						pc.greaterMid = numericMid
					}
				}
			}
			for _, t := range currentTransceivers {
				if mid := t.Mid(); mid != "" {
					numericMid, errMid := strconv.Atoi(mid)
					if errMid == nil {
						if numericMid > pc.greaterMid {
							pc.greaterMid = numericMid
						}
					}
					continue
				}
				pc.greaterMid++
				err = t.SetMid(strconv.Itoa(pc.greaterMid))
				if err != nil {
					return SessionDescription{}, err
				}
			}
		}

		if pc.currentRemoteDescription == nil {
			d, err = pc.generateUnmatchedSDP(currentTransceivers, useIdentity)
		} else {
			d, err = pc.generateMatchedSDP(currentTransceivers, useIdentity, true /*includeUnmatched */, connectionRoleFromDtlsRole(defaultDtlsRoleOffer))
		}

		if err != nil {
			return SessionDescription{}, err
		}

		updateSDPOrigin(&pc.sdpOrigin, d)
		sdpBytes, err := d.Marshal()
		if err != nil {
			return SessionDescription{}, err
		}

		offer = SessionDescription{
			Type:   SDPTypeOffer,
			SDP:    string(sdpBytes),
			parsed: d,
		}

		// Verify local media hasn't changed during offer
		// generation. Recompute if necessary
		if isPlanB || !pc.hasLocalDescriptionChanged(&offer) {
			break
		}
		count++
		if count >= 128 {
			return SessionDescription{}, errExcessiveRetries
		}
	}

	pc.lastOffer = offer.SDP
	return offer, nil
}

func (pc *PeerConnection) createICEGatherer() (*ICEGatherer, error) {
	g, err := pc.api.NewICEGatherer(ICEGatherOptions{
		ICEServers:      pc.configuration.getICEServers(),
		ICEGatherPolicy: pc.configuration.ICETransportPolicy,
	})
	if err != nil {
		return nil, err
	}

	return g, nil
}

// Update the PeerConnectionState given the state of relevant transports
// https://www.w3.org/TR/webrtc/#rtcpeerconnectionstate-enum
func (pc *PeerConnection) updateConnectionState(iceConnectionState ICEConnectionState, dtlsTransportState DTLSTransportState) {
	connectionState := PeerConnectionStateNew
	switch {
	// The RTCPeerConnection object's [[IsClosed]] slot is true.
	case pc.isClosed.get():
		connectionState = PeerConnectionStateClosed

	// Any of the RTCIceTransports or RTCDtlsTransports are in a "failed" state.
	case iceConnectionState == ICEConnectionStateFailed || dtlsTransportState == DTLSTransportStateFailed:
		connectionState = PeerConnectionStateFailed

	// Any of the RTCIceTransports or RTCDtlsTransports are in the "disconnected"
	// state and none of them are in the "failed" or "connecting" or "checking" state.  */
	case iceConnectionState == ICEConnectionStateDisconnected:
		connectionState = PeerConnectionStateDisconnected

	// None of the previous states apply and all RTCIceTransports are in the "new" or "closed" state,
	// and all RTCDtlsTransports are in the "new" or "closed" state, or there are no transports.
	case (iceConnectionState == ICEConnectionStateNew || iceConnectionState == ICEConnectionStateClosed) &&
		(dtlsTransportState == DTLSTransportStateNew || dtlsTransportState == DTLSTransportStateClosed):
		connectionState = PeerConnectionStateNew

	// None of the previous states apply and any RTCIceTransport is in the "new" or "checking" state or
	// any RTCDtlsTransport is in the "new" or "connecting" state.
	case (iceConnectionState == ICEConnectionStateNew || iceConnectionState == ICEConnectionStateChecking) ||
		(dtlsTransportState == DTLSTransportStateNew || dtlsTransportState == DTLSTransportStateConnecting):
		connectionState = PeerConnectionStateConnecting

	// All RTCIceTransports and RTCDtlsTransports are in the "connected", "completed" or "closed"
	// state and all RTCDtlsTransports are in the "connected" or "closed" state.
	case (iceConnectionState == ICEConnectionStateConnected || iceConnectionState == ICEConnectionStateCompleted || iceConnectionState == ICEConnectionStateClosed) &&
		(dtlsTransportState == DTLSTransportStateConnected || dtlsTransportState == DTLSTransportStateClosed):
		connectionState = PeerConnectionStateConnected
	}

	if pc.connectionState.Load() == connectionState {
		return
	}

	pc.onConnectionStateChange(connectionState)
}

func (pc *PeerConnection) createICETransport() *ICETransport {
	t := pc.api.NewICETransport(pc.iceGatherer)
	t.internalOnConnectionStateChangeHandler.Store(func(state ICETransportState) {
		var cs ICEConnectionState
		switch state {
		case ICETransportStateNew:
			cs = ICEConnectionStateNew
		case ICETransportStateChecking:
			cs = ICEConnectionStateChecking
		case ICETransportStateConnected:
			cs = ICEConnectionStateConnected
		case ICETransportStateCompleted:
			cs = ICEConnectionStateCompleted
		case ICETransportStateFailed:
			cs = ICEConnectionStateFailed
		case ICETransportStateDisconnected:
			cs = ICEConnectionStateDisconnected
		case ICETransportStateClosed:
			cs = ICEConnectionStateClosed
		default:
			pc.log.Warnf("OnConnectionStateChange: unhandled ICE state: %s", state)
			return
		}
		pc.onICEConnectionStateChange(cs)
		pc.updateConnectionState(cs, pc.dtlsTransport.State())
	})

	return t
}

// CreateAnswer starts the PeerConnection and generates the localDescription
func (pc *PeerConnection) CreateAnswer(*AnswerOptions) (SessionDescription, error) {
	useIdentity := pc.idpLoginURL != nil
	remoteDesc := pc.RemoteDescription()
	switch {
	case remoteDesc == nil:
		return SessionDescription{}, &rtcerr.InvalidStateError{Err: ErrNoRemoteDescription}
	case useIdentity:
		return SessionDescription{}, errIdentityProviderNotImplemented
	case pc.isClosed.get():
		return SessionDescription{}, &rtcerr.InvalidStateError{Err: ErrConnectionClosed}
	case pc.signalingState.Get() != SignalingStateHaveRemoteOffer && pc.signalingState.Get() != SignalingStateHaveLocalPranswer:
		return SessionDescription{}, &rtcerr.InvalidStateError{Err: ErrIncorrectSignalingState}
	}

	connectionRole := connectionRoleFromDtlsRole(pc.api.settingEngine.answeringDTLSRole)
	if connectionRole == sdp.ConnectionRole(0) {
		connectionRole = connectionRoleFromDtlsRole(defaultDtlsRoleAnswer)

		// If one of the agents is lite and the other one is not, the lite agent must be the controlling agent.
		// If both or neither agents are lite the offering agent is controlling.
		// RFC 8445 S6.1.1
		if isIceLiteSet(remoteDesc.parsed) && !pc.api.settingEngine.candidates.ICELite {
			connectionRole = connectionRoleFromDtlsRole(DTLSRoleServer)
		}
	}
	pc.mu.Lock()
	defer pc.mu.Unlock()

	d, err := pc.generateMatchedSDP(pc.rtpTransceivers, useIdentity, false /*includeUnmatched */, connectionRole)
	if err != nil {
		return SessionDescription{}, err
	}

	updateSDPOrigin(&pc.sdpOrigin, d)
	sdpBytes, err := d.Marshal()
	if err != nil {
		return SessionDescription{}, err
	}

	desc := SessionDescription{
		Type:   SDPTypeAnswer,
		SDP:    string(sdpBytes),
		parsed: d,
	}
	pc.lastAnswer = desc.SDP
	return desc, nil
}

// 4.4.1.6 Set the SessionDescription
func (pc *PeerConnection) setDescription(sd *SessionDescription, op stateChangeOp) error { //nolint:gocognit
	switch {
	case pc.isClosed.get():
		return &rtcerr.InvalidStateError{Err: ErrConnectionClosed}
	case NewSDPType(sd.Type.String()) == SDPType(Unknown):
		return &rtcerr.TypeError{Err: fmt.Errorf("%w: '%d' is not a valid enum value of type SDPType", errPeerConnSDPTypeInvalidValue, sd.Type)}
	}

	nextState, err := func() (SignalingState, error) {
		pc.mu.Lock()
		defer pc.mu.Unlock()

		cur := pc.SignalingState()
		setLocal := stateChangeOpSetLocal
		setRemote := stateChangeOpSetRemote
		newSDPDoesNotMatchOffer := &rtcerr.InvalidModificationError{Err: errSDPDoesNotMatchOffer}
		newSDPDoesNotMatchAnswer := &rtcerr.InvalidModificationError{Err: errSDPDoesNotMatchAnswer}

		var nextState SignalingState
		var err error
		switch op {
		case setLocal:
			switch sd.Type {
			// stable->SetLocal(offer)->have-local-offer
			case SDPTypeOffer:
				if sd.SDP != pc.lastOffer {
					return nextState, newSDPDoesNotMatchOffer
				}
				nextState, err = checkNextSignalingState(cur, SignalingStateHaveLocalOffer, setLocal, sd.Type)
				if err == nil {
					pc.pendingLocalDescription = sd
				}
			// have-remote-offer->SetLocal(answer)->stable
			// have-local-pranswer->SetLocal(answer)->stable
			case SDPTypeAnswer:
				if sd.SDP != pc.lastAnswer {
					return nextState, newSDPDoesNotMatchAnswer
				}
				nextState, err = checkNextSignalingState(cur, SignalingStateStable, setLocal, sd.Type)
				if err == nil {
					pc.currentLocalDescription = sd
					pc.currentRemoteDescription = pc.pendingRemoteDescription
					pc.pendingRemoteDescription = nil
					pc.pendingLocalDescription = nil
				}
			case SDPTypeRollback:
				nextState, err = checkNextSignalingState(cur, SignalingStateStable, setLocal, sd.Type)
				if err == nil {
					pc.pendingLocalDescription = nil
				}
			// have-remote-offer->SetLocal(pranswer)->have-local-pranswer
			case SDPTypePranswer:
				if sd.SDP != pc.lastAnswer {
					return nextState, newSDPDoesNotMatchAnswer
				}
				nextState, err = checkNextSignalingState(cur, SignalingStateHaveLocalPranswer, setLocal, sd.Type)
				if err == nil {
					pc.pendingLocalDescription = sd
				}
			default:
				return nextState, &rtcerr.OperationError{Err: fmt.Errorf("%w: %s(%s)", errPeerConnStateChangeInvalid, op, sd.Type)}
			}
		case setRemote:
			switch sd.Type {
			// stable->SetRemote(offer)->have-remote-offer
			case SDPTypeOffer:
				nextState, err = checkNextSignalingState(cur, SignalingStateHaveRemoteOffer, setRemote, sd.Type)
				if err == nil {
					pc.pendingRemoteDescription = sd
				}
			// have-local-offer->SetRemote(answer)->stable
			// have-remote-pranswer->SetRemote(answer)->stable
			case SDPTypeAnswer:
				nextState, err = checkNextSignalingState(cur, SignalingStateStable, setRemote, sd.Type)
				if err == nil {
					pc.currentRemoteDescription = sd
					pc.currentLocalDescription = pc.pendingLocalDescription
					pc.pendingRemoteDescription = nil
					pc.pendingLocalDescription = nil
				}
			case SDPTypeRollback:
				nextState, err = checkNextSignalingState(cur, SignalingStateStable, setRemote, sd.Type)
				if err == nil {
					pc.pendingRemoteDescription = nil
				}
			// have-local-offer->SetRemote(pranswer)->have-remote-pranswer
			case SDPTypePranswer:
				nextState, err = checkNextSignalingState(cur, SignalingStateHaveRemotePranswer, setRemote, sd.Type)
				if err == nil {
					pc.pendingRemoteDescription = sd
				}
			default:
				return nextState, &rtcerr.OperationError{Err: fmt.Errorf("%w: %s(%s)", errPeerConnStateChangeInvalid, op, sd.Type)}
			}
		default:
			return nextState, &rtcerr.OperationError{Err: fmt.Errorf("%w: %q", errPeerConnStateChangeUnhandled, op)}
		}

		return nextState, err
	}()

	if err == nil {
		pc.signalingState.Set(nextState)
		if pc.signalingState.Get() == SignalingStateStable {
			pc.isNegotiationNeeded.set(false)
			pc.mu.Lock()
			pc.onNegotiationNeeded()
			pc.mu.Unlock()
		}
		pc.onSignalingStateChange(nextState)
	}
	return err
}

// SetLocalDescription sets the SessionDescription of the local peer
func (pc *PeerConnection) SetLocalDescription(desc SessionDescription) error {
	if pc.isClosed.get() {
		return &rtcerr.InvalidStateError{Err: ErrConnectionClosed}
	}

	haveLocalDescription := pc.currentLocalDescription != nil

	// JSEP 5.4
	if desc.SDP == "" {
		switch desc.Type {
		case SDPTypeAnswer, SDPTypePranswer:
			desc.SDP = pc.lastAnswer
		case SDPTypeOffer:
			desc.SDP = pc.lastOffer
		default:
			return &rtcerr.InvalidModificationError{
				Err: fmt.Errorf("%w: %s", errPeerConnSDPTypeInvalidValueSetLocalDescription, desc.Type),
			}
		}
	}

	desc.parsed = &sdp.SessionDescription{}
	if err := desc.parsed.Unmarshal([]byte(desc.SDP)); err != nil {
		return err
	}
	if err := pc.setDescription(&desc, stateChangeOpSetLocal); err != nil {
		return err
	}

	currentTransceivers := append([]*RTPTransceiver{}, pc.GetTransceivers()...)

	weAnswer := desc.Type == SDPTypeAnswer
	remoteDesc := pc.RemoteDescription()
	if weAnswer && remoteDesc != nil {
		_ = setRTPTransceiverCurrentDirection(&desc, currentTransceivers, false)
		if err := pc.startRTPSenders(currentTransceivers); err != nil {
			return err
		}
		pc.configureRTPReceivers(haveLocalDescription, remoteDesc, currentTransceivers)
		pc.ops.Enqueue(func() {
			pc.startRTP(haveLocalDescription, remoteDesc, currentTransceivers)
		})
	}

	if pc.iceGatherer.State() == ICEGathererStateNew {
		return pc.iceGatherer.Gather()
	}
	return nil
}

// LocalDescription returns PendingLocalDescription if it is not null and
// otherwise it returns CurrentLocalDescription. This property is used to
// determine if SetLocalDescription has already been called.
// https://www.w3.org/TR/webrtc/#dom-rtcpeerconnection-localdescription
func (pc *PeerConnection) LocalDescription() *SessionDescription {
	if pendingLocalDescription := pc.PendingLocalDescription(); pendingLocalDescription != nil {
		return pendingLocalDescription
	}
	return pc.CurrentLocalDescription()
}

// SetRemoteDescription sets the SessionDescription of the remote peer
func (pc *PeerConnection) SetRemoteDescription(desc SessionDescription) error { //nolint:gocognit,gocyclo
	if pc.isClosed.get() {
		return &rtcerr.InvalidStateError{Err: ErrConnectionClosed}
	}

	isRenegotation := pc.currentRemoteDescription != nil

	if _, err := desc.Unmarshal(); err != nil {
		return err
	}
	if err := pc.setDescription(&desc, stateChangeOpSetRemote); err != nil {
		return err
	}

	if err := pc.api.mediaEngine.updateFromRemoteDescription(*desc.parsed); err != nil {
		return err
	}

	var t *RTPTransceiver
	localTransceivers := append([]*RTPTransceiver{}, pc.GetTransceivers()...)
	detectedPlanB := descriptionIsPlanB(pc.RemoteDescription(), pc.log)
	if pc.configuration.SDPSemantics != SDPSemanticsUnifiedPlan {
		detectedPlanB = descriptionPossiblyPlanB(pc.RemoteDescription())
	}

	weOffer := desc.Type == SDPTypeAnswer

	if !weOffer && !detectedPlanB {
		for _, media := range pc.RemoteDescription().parsed.MediaDescriptions {
			midValue := getMidValue(media)
			if midValue == "" {
				return errPeerConnRemoteDescriptionWithoutMidValue
			}

			if media.MediaName.Media == mediaSectionApplication {
				continue
			}

			kind := NewRTPCodecType(media.MediaName.Media)
			direction := getPeerDirection(media)
			if kind == 0 || direction == RTPTransceiverDirection(Unknown) {
				continue
			}

			t, localTransceivers = findByMid(midValue, localTransceivers)
			if t == nil {
				t, localTransceivers = satisfyTypeAndDirection(kind, direction, localTransceivers)
			} else if direction == RTPTransceiverDirectionInactive {
				if err := t.Stop(); err != nil {
					return err
				}
			}

			switch {
			case t == nil:
				receiver, err := pc.api.NewRTPReceiver(kind, pc.dtlsTransport)
				if err != nil {
					return err
				}

				localDirection := RTPTransceiverDirectionRecvonly
				if direction == RTPTransceiverDirectionRecvonly {
					localDirection = RTPTransceiverDirectionSendonly
				} else if direction == RTPTransceiverDirectionInactive {
					localDirection = RTPTransceiverDirectionInactive
				}

				t = newRTPTransceiver(receiver, nil, localDirection, kind, pc.api)
				pc.mu.Lock()
				pc.addRTPTransceiver(t)
				pc.mu.Unlock()

				// if transceiver is create by remote sdp, set prefer codec same as remote peer
				if codecs, err := codecsFromMediaDescription(media); err == nil {
					filteredCodecs := []RTPCodecParameters{}
					for _, codec := range codecs {
						if c, matchType := codecParametersFuzzySearch(codec, pc.api.mediaEngine.getCodecsByKind(kind)); matchType == codecMatchExact {
							// if codec match exact, use payloadtype register to mediaengine
							codec.PayloadType = c.PayloadType
							filteredCodecs = append(filteredCodecs, codec)
						}
					}
					_ = t.SetCodecPreferences(filteredCodecs)
				}

			case direction == RTPTransceiverDirectionRecvonly:
				if t.Direction() == RTPTransceiverDirectionSendrecv {
					t.setDirection(RTPTransceiverDirectionSendonly)
				}
			case direction == RTPTransceiverDirectionSendrecv:
				if t.Direction() == RTPTransceiverDirectionSendonly {
					t.setDirection(RTPTransceiverDirectionSendrecv)
				}
			}

			if t.Mid() == "" {
				if err := t.SetMid(midValue); err != nil {
					return err
				}
			}
		}
	}

	remoteUfrag, remotePwd, candidates, err := extractICEDetails(desc.parsed, pc.log)
	if err != nil {
		return err
	}

	if isRenegotation && pc.iceTransport.haveRemoteCredentialsChange(remoteUfrag, remotePwd) {
		// An ICE Restart only happens implicitly for a SetRemoteDescription of type offer
		if !weOffer {
			if err = pc.iceTransport.restart(); err != nil {
				return err
			}
		}

		if err = pc.iceTransport.setRemoteCredentials(remoteUfrag, remotePwd); err != nil {
			return err
		}
	}

	for i := range candidates {
		if err = pc.iceTransport.AddRemoteCandidate(&candidates[i]); err != nil {
			return err
		}
	}

	currentTransceivers := append([]*RTPTransceiver{}, pc.GetTransceivers()...)

	if isRenegotation {
		if weOffer {
			_ = setRTPTransceiverCurrentDirection(&desc, currentTransceivers, true)
			if err = pc.startRTPSenders(currentTransceivers); err != nil {
				return err
			}
			pc.configureRTPReceivers(true, &desc, currentTransceivers)
			pc.ops.Enqueue(func() {
				pc.startRTP(true, &desc, currentTransceivers)
			})
		}
		return nil
	}

	remoteIsLite := isIceLiteSet(desc.parsed)

	fingerprint, fingerprintHash, err := extractFingerprint(desc.parsed)
	if err != nil {
		return err
	}

	iceRole := ICERoleControlled
	// If one of the agents is lite and the other one is not, the lite agent must be the controlling agent.
	// If both or neither agents are lite the offering agent is controlling.
	// RFC 8445 S6.1.1
	if (weOffer && remoteIsLite == pc.api.settingEngine.candidates.ICELite) || (remoteIsLite && !pc.api.settingEngine.candidates.ICELite) {
		iceRole = ICERoleControlling
	}

	// Start the networking in a new routine since it will block until
	// the connection is actually established.
	if weOffer {
		_ = setRTPTransceiverCurrentDirection(&desc, currentTransceivers, true)
		if err := pc.startRTPSenders(currentTransceivers); err != nil {
			return err
		}

		pc.configureRTPReceivers(false, &desc, currentTransceivers)
	}

	pc.ops.Enqueue(func() {
		pc.startTransports(iceRole, dtlsRoleFromRemoteSDP(desc.parsed), remoteUfrag, remotePwd, fingerprint, fingerprintHash)
		if weOffer {
			pc.startRTP(false, &desc, currentTransceivers)
		}
	})
	return nil
}

func (pc *PeerConnection) configureReceiver(incoming trackDetails, receiver *RTPReceiver) {
	receiver.configureReceive(trackDetailsToRTPReceiveParameters(&incoming))

	// set track id and label early so they can be set as new track information
	// is received from the SDP.
	for i := range receiver.tracks {
		receiver.tracks[i].track.mu.Lock()
		receiver.tracks[i].track.id = incoming.id
		receiver.tracks[i].track.streamID = incoming.streamID
		receiver.tracks[i].track.mu.Unlock()
	}
}

func (pc *PeerConnection) startReceiver(incoming trackDetails, receiver *RTPReceiver) {
	if err := receiver.startReceive(trackDetailsToRTPReceiveParameters(&incoming)); err != nil {
		pc.log.Warnf("RTPReceiver Receive failed %s", err)
		return
	}

	for _, t := range receiver.Tracks() {
		if t.SSRC() == 0 || t.RID() != "" {
			return
		}

		go func(track *TrackRemote) {
			b := make([]byte, pc.api.settingEngine.getReceiveMTU())
			n, _, err := track.peek(b)
			if err != nil {
				pc.log.Warnf("Could not determine PayloadType for SSRC %d (%s)", track.SSRC(), err)
				return
			}

			if err = track.checkAndUpdateTrack(b[:n]); err != nil {
				pc.log.Warnf("Failed to set codec settings for track SSRC %d (%s)", track.SSRC(), err)
				return
			}

			pc.onTrack(track, receiver)
		}(t)
	}
}

func setRTPTransceiverCurrentDirection(answer *SessionDescription, currentTransceivers []*RTPTransceiver, weOffer bool) error {
	currentTransceivers = append([]*RTPTransceiver{}, currentTransceivers...)
	for _, media := range answer.parsed.MediaDescriptions {
		midValue := getMidValue(media)
		if midValue == "" {
			return errPeerConnRemoteDescriptionWithoutMidValue
		}

		if media.MediaName.Media == mediaSectionApplication {
			continue
		}

		var t *RTPTransceiver
		t, currentTransceivers = findByMid(midValue, currentTransceivers)

		if t == nil {
			return fmt.Errorf("%w: %q", errPeerConnTranscieverMidNil, midValue)
		}

		direction := getPeerDirection(media)
		if direction == RTPTransceiverDirection(Unknown) {
			continue
		}

		// reverse direction if it was a remote answer
		if weOffer {
			switch direction {
			case RTPTransceiverDirectionSendonly:
				direction = RTPTransceiverDirectionRecvonly
			case RTPTransceiverDirectionRecvonly:
				direction = RTPTransceiverDirectionSendonly
			default:
			}
		}

		// If a transceiver is created by applying a remote description that has recvonly transceiver,
		// it will have no sender. In this case, the transceiver's current direction is set to inactive so
		// that the transceiver can be reused by next AddTrack.
		if direction == RTPTransceiverDirectionSendonly && t.Sender() == nil {
			direction = RTPTransceiverDirectionInactive
		}

		t.setCurrentDirection(direction)
	}
	return nil
}

func runIfNewReceiver(
	incomingTrack trackDetails,
	transceivers []*RTPTransceiver,
	f func(incomingTrack trackDetails, receiver *RTPReceiver),
) bool {
	for _, t := range transceivers {
		if t.Mid() != incomingTrack.mid {
			continue
		}

		receiver := t.Receiver()
		if (incomingTrack.kind != t.Kind()) ||
			(t.Direction() != RTPTransceiverDirectionRecvonly && t.Direction() != RTPTransceiverDirectionSendrecv) ||
			receiver == nil ||
			(receiver.haveReceived()) {
			continue
		}

		f(incomingTrack, receiver)
		return true
	}

	return false
}

// configurepRTPReceivers opens knows inbound SRTP streams from the RemoteDescription
func (pc *PeerConnection) configureRTPReceivers(isRenegotiation bool, remoteDesc *SessionDescription, currentTransceivers []*RTPTransceiver) { //nolint:gocognit
	incomingTracks := trackDetailsFromSDP(pc.log, remoteDesc.parsed)

	if isRenegotiation {
		for _, t := range currentTransceivers {
			receiver := t.Receiver()
			if receiver == nil {
				continue
			}

			tracks := t.Receiver().Tracks()
			if len(tracks) == 0 {
				continue
			}

			mid := t.Mid()
			receiverNeedsStopped := false
			func() {
				for _, t := range tracks {
					t.mu.Lock()
					defer t.mu.Unlock()

					if t.rid != "" {
						if details := trackDetailsForRID(incomingTracks, mid, t.rid); details != nil {
							t.id = details.id
							t.streamID = details.streamID
							continue
						}
					} else if t.ssrc != 0 {
						if details := trackDetailsForSSRC(incomingTracks, t.ssrc); details != nil {
							t.id = details.id
							t.streamID = details.streamID
							continue
						}
					}

					receiverNeedsStopped = true
				}
			}()

			if !receiverNeedsStopped {
				continue
			}

			if err := receiver.Stop(); err != nil {
				pc.log.Warnf("Failed to stop RtpReceiver: %s", err)
				continue
			}

			receiver, err := pc.api.NewRTPReceiver(receiver.kind, pc.dtlsTransport)
			if err != nil {
				pc.log.Warnf("Failed to create new RtpReceiver: %s", err)
				continue
			}
			t.setReceiver(receiver)
		}
	}

	localTransceivers := append([]*RTPTransceiver{}, currentTransceivers...)

	// Ensure we haven't already started a transceiver for this ssrc
	filteredTracks := append([]trackDetails{}, incomingTracks...)
	for _, incomingTrack := range incomingTracks {
		// If we already have a TrackRemote for a given SSRC don't handle it again
		for _, t := range localTransceivers {
			if receiver := t.Receiver(); receiver != nil {
				for _, track := range receiver.Tracks() {
					for _, ssrc := range incomingTrack.ssrcs {
						if ssrc == track.SSRC() {
							filteredTracks = filterTrackWithSSRC(filteredTracks, track.SSRC())
						}
					}
				}
			}
		}
	}

	for _, incomingTrack := range filteredTracks {
		_ = runIfNewReceiver(incomingTrack, localTransceivers, pc.configureReceiver)
	}
}

// startRTPReceivers opens knows inbound SRTP streams from the RemoteDescription
func (pc *PeerConnection) startRTPReceivers(remoteDesc *SessionDescription, currentTransceivers []*RTPTransceiver) {
	incomingTracks := trackDetailsFromSDP(pc.log, remoteDesc.parsed)
	if len(incomingTracks) == 0 {
		return
	}

	localTransceivers := append([]*RTPTransceiver{}, currentTransceivers...)

	unhandledTracks := incomingTracks[:0]
	for _, incomingTrack := range incomingTracks {
		trackHandled := runIfNewReceiver(incomingTrack, localTransceivers, pc.startReceiver)
		if !trackHandled {
			unhandledTracks = append(unhandledTracks, incomingTrack)
		}
	}

	remoteIsPlanB := false
	switch pc.configuration.SDPSemantics {
	case SDPSemanticsPlanB:
		remoteIsPlanB = true
	case SDPSemanticsUnifiedPlanWithFallback:
		remoteIsPlanB = descriptionPossiblyPlanB(pc.RemoteDescription())
	default:
		// none
	}

	if remoteIsPlanB {
		for _, incomingTrack := range unhandledTracks {
			t, err := pc.AddTransceiverFromKind(incomingTrack.kind, RTPTransceiverInit{
				Direction: RTPTransceiverDirectionSendrecv,
			})
			if err != nil {
				pc.log.Warnf("Could not add transceiver for remote SSRC %d: %s", incomingTrack.ssrcs[0], err)
				continue
			}
			pc.configureReceiver(incomingTrack, t.Receiver())
			pc.startReceiver(incomingTrack, t.Receiver())
		}
	}
}

// startRTPSenders starts all outbound RTP streams
func (pc *PeerConnection) startRTPSenders(currentTransceivers []*RTPTransceiver) error {
	for _, transceiver := range currentTransceivers {
		if sender := transceiver.Sender(); sender != nil && sender.isNegotiated() && !sender.hasSent() {
			err := sender.Send(sender.GetParameters())
			if err != nil {
				return err
			}
		}
	}

	return nil
}

// Start SCTP subsystem
func (pc *PeerConnection) startSCTP() {
	// Start sctp
	if err := pc.sctpTransport.Start(SCTPCapabilities{
		MaxMessageSize: 0,
	}); err != nil {
		pc.log.Warnf("Failed to start SCTP: %s", err)
		if err = pc.sctpTransport.Stop(); err != nil {
			pc.log.Warnf("Failed to stop SCTPTransport: %s", err)
		}

		return
	}
}

func (pc *PeerConnection) handleUndeclaredSSRC(ssrc SSRC, remoteDescription *SessionDescription) (handled bool, err error) {
	if len(remoteDescription.parsed.MediaDescriptions) != 1 {
		return false, nil
	}

	onlyMediaSection := remoteDescription.parsed.MediaDescriptions[0]
	streamID := ""
	id := ""
	hasRidAttribute := false
	hasSSRCAttribute := false

	for _, a := range onlyMediaSection.Attributes {
		switch a.Key {
		case sdp.AttrKeyMsid:
			if split := strings.Split(a.Value, " "); len(split) == 2 {
				streamID = split[0]
				id = split[1]
			}
		case sdp.AttrKeySSRC:
			hasSSRCAttribute = true
		case sdpAttributeRid:
			hasRidAttribute = true
		}
	}

	if hasRidAttribute {
		return false, nil
	} else if hasSSRCAttribute {
		return false, errPeerConnSingleMediaSectionHasExplicitSSRC
	}

	incoming := trackDetails{
		ssrcs:    []SSRC{ssrc},
		kind:     RTPCodecTypeVideo,
		streamID: streamID,
		id:       id,
	}
	if onlyMediaSection.MediaName.Media == RTPCodecTypeAudio.String() {
		incoming.kind = RTPCodecTypeAudio
	}

	t, err := pc.AddTransceiverFromKind(incoming.kind, RTPTransceiverInit{
		Direction: RTPTransceiverDirectionSendrecv,
	})
	if err != nil {
		// nolint
		return false, fmt.Errorf("%w: %d: %s", errPeerConnRemoteSSRCAddTransceiver, ssrc, err)
	}

	pc.configureReceiver(incoming, t.Receiver())
	pc.startReceiver(incoming, t.Receiver())
	return true, nil
}

func (pc *PeerConnection) handleIncomingSSRC(rtpStream io.Reader, ssrc SSRC) error { //nolint:gocognit
	remoteDescription := pc.RemoteDescription()
	if remoteDescription == nil {
		return errPeerConnRemoteDescriptionNil
	}

	// If a SSRC already exists in the RemoteDescription don't perform heuristics upon it
	for _, track := range trackDetailsFromSDP(pc.log, remoteDescription.parsed) {
		if track.repairSsrc != nil && ssrc == *track.repairSsrc {
			return nil
		}
		for _, trackSsrc := range track.ssrcs {
			if ssrc == trackSsrc {
				return nil
			}
		}
	}

	// If the remote SDP was only one media section the ssrc doesn't have to be explicitly declared
	if handled, err := pc.handleUndeclaredSSRC(ssrc, remoteDescription); handled || err != nil {
		return err
	}

	midExtensionID, audioSupported, videoSupported := pc.api.mediaEngine.getHeaderExtensionID(RTPHeaderExtensionCapability{sdp.SDESMidURI})
	if !audioSupported && !videoSupported {
		return errPeerConnSimulcastMidRTPExtensionRequired
	}

	streamIDExtensionID, audioSupported, videoSupported := pc.api.mediaEngine.getHeaderExtensionID(RTPHeaderExtensionCapability{sdp.SDESRTPStreamIDURI})
	if !audioSupported && !videoSupported {
		return errPeerConnSimulcastStreamIDRTPExtensionRequired
	}

	repairStreamIDExtensionID, _, _ := pc.api.mediaEngine.getHeaderExtensionID(RTPHeaderExtensionCapability{sdesRepairRTPStreamIDURI})

	b := make([]byte, pc.api.settingEngine.getReceiveMTU())

	i, err := rtpStream.Read(b)
	if err != nil {
		return err
	}

	var mid, rid, rsid string
	payloadType, err := handleUnknownRTPPacket(b[:i], uint8(midExtensionID), uint8(streamIDExtensionID), uint8(repairStreamIDExtensionID), &mid, &rid, &rsid)
	if err != nil {
		return err
	}

	params, err := pc.api.mediaEngine.getRTPParametersByPayloadType(payloadType)
	if err != nil {
		return err
	}

	streamInfo := createStreamInfo("", ssrc, params.Codecs[0].PayloadType, params.Codecs[0].RTPCodecCapability, params.HeaderExtensions)
	readStream, interceptor, rtcpReadStream, rtcpInterceptor, err := pc.dtlsTransport.streamsForSSRC(ssrc, *streamInfo)
	if err != nil {
		return err
	}

	for readCount := 0; readCount <= simulcastProbeCount; readCount++ {
		if mid == "" || (rid == "" && rsid == "") {
			i, _, err := interceptor.Read(b, nil)
			if err != nil {
				return err
			}

			if _, err = handleUnknownRTPPacket(b[:i], uint8(midExtensionID), uint8(streamIDExtensionID), uint8(repairStreamIDExtensionID), &mid, &rid, &rsid); err != nil {
				return err
			}

			continue
		}

		for _, t := range pc.GetTransceivers() {
			receiver := t.Receiver()
			if t.Mid() != mid || receiver == nil {
				continue
			}

			if rsid != "" {
				receiver.mu.Lock()
				defer receiver.mu.Unlock()
				return receiver.receiveForRtx(SSRC(0), rsid, streamInfo, readStream, interceptor, rtcpReadStream, rtcpInterceptor)
			}

			track, err := receiver.receiveForRid(rid, params, streamInfo, readStream, interceptor, rtcpReadStream, rtcpInterceptor)
			if err != nil {
				return err
			}
			pc.onTrack(track, receiver)
			return nil
		}
	}

	pc.api.interceptor.UnbindRemoteStream(streamInfo)
	return errPeerConnSimulcastIncomingSSRCFailed
}

// undeclaredMediaProcessor handles RTP/RTCP packets that don't match any a:ssrc lines
func (pc *PeerConnection) undeclaredMediaProcessor() {
	go pc.undeclaredRTPMediaProcessor()
	go pc.undeclaredRTCPMediaProcessor()
}

func (pc *PeerConnection) undeclaredRTPMediaProcessor() {
	var simulcastRoutineCount uint64
	for {
		srtpSession, err := pc.dtlsTransport.getSRTPSession()
		if err != nil {
			pc.log.Warnf("undeclaredMediaProcessor failed to open SrtpSession: %v", err)
			return
		}

		stream, ssrc, err := srtpSession.AcceptStream()
		if err != nil {
			pc.log.Warnf("Failed to accept RTP %v", err)
			return
		}

		if pc.isClosed.get() {
			if err = stream.Close(); err != nil {
				pc.log.Warnf("Failed to close RTP stream %v", err)
			}
			continue
		}

		if atomic.AddUint64(&simulcastRoutineCount, 1) >= simulcastMaxProbeRoutines {
			atomic.AddUint64(&simulcastRoutineCount, ^uint64(0))
			pc.log.Warn(ErrSimulcastProbeOverflow.Error())
			pc.dtlsTransport.storeSimulcastStream(stream)
			continue
		}

		go func(rtpStream io.Reader, ssrc SSRC) {
			if err := pc.handleIncomingSSRC(rtpStream, ssrc); err != nil {
				pc.log.Errorf(incomingUnhandledRTPSsrc, ssrc, err)
				pc.dtlsTransport.storeSimulcastStream(stream)
			}
			atomic.AddUint64(&simulcastRoutineCount, ^uint64(0))
		}(stream, SSRC(ssrc))
	}
}

func (pc *PeerConnection) undeclaredRTCPMediaProcessor() {
	var unhandledStreams []*srtp.ReadStreamSRTCP
	defer func() {
		for _, s := range unhandledStreams {
			_ = s.Close()
		}
	}()
	for {
		srtcpSession, err := pc.dtlsTransport.getSRTCPSession()
		if err != nil {
			pc.log.Warnf("undeclaredMediaProcessor failed to open SrtcpSession: %v", err)
			return
		}

		stream, ssrc, err := srtcpSession.AcceptStream()
		if err != nil {
			pc.log.Warnf("Failed to accept RTCP %v", err)
			return
		}
		pc.log.Warnf("Incoming unhandled RTCP ssrc(%d), OnTrack will not be fired", ssrc)
		unhandledStreams = append(unhandledStreams, stream)
	}
}

// RemoteDescription returns pendingRemoteDescription if it is not null and
// otherwise it returns currentRemoteDescription. This property is used to
// determine if setRemoteDescription has already been called.
// https://www.w3.org/TR/webrtc/#dom-rtcpeerconnection-remotedescription
func (pc *PeerConnection) RemoteDescription() *SessionDescription {
	pc.mu.RLock()
	defer pc.mu.RUnlock()

	if pc.pendingRemoteDescription != nil {
		return pc.pendingRemoteDescription
	}
	return pc.currentRemoteDescription
}

// AddICECandidate accepts an ICE candidate string and adds it
// to the existing set of candidates.
func (pc *PeerConnection) AddICECandidate(candidate ICECandidateInit) error {
	if pc.RemoteDescription() == nil {
		return &rtcerr.InvalidStateError{Err: ErrNoRemoteDescription}
	}

	candidateValue := strings.TrimPrefix(candidate.Candidate, "candidate:")

	var iceCandidate *ICECandidate
	if candidateValue != "" {
		candidate, err := ice.UnmarshalCandidate(candidateValue)
		if err != nil {
			if errors.Is(err, ice.ErrUnknownCandidateTyp) || errors.Is(err, ice.ErrDetermineNetworkType) {
				pc.log.Warnf("Discarding remote candidate: %s", err)
				return nil
			}
			return err
		}

		c, err := newICECandidateFromICE(candidate)
		if err != nil {
			return err
		}
		iceCandidate = &c
	}

	return pc.iceTransport.AddRemoteCandidate(iceCandidate)
}

// ICEConnectionState returns the ICE connection state of the
// PeerConnection instance.
func (pc *PeerConnection) ICEConnectionState() ICEConnectionState {
	if state, ok := pc.iceConnectionState.Load().(ICEConnectionState); ok {
		return state
	}
	return ICEConnectionState(0)
}

// GetSenders returns the RTPSender that are currently attached to this PeerConnection
func (pc *PeerConnection) GetSenders() (result []*RTPSender) {
	pc.mu.Lock()
	defer pc.mu.Unlock()

	for _, transceiver := range pc.rtpTransceivers {
		if sender := transceiver.Sender(); sender != nil {
			result = append(result, sender)
		}
	}
	return result
}

// GetReceivers returns the RTPReceivers that are currently attached to this PeerConnection
func (pc *PeerConnection) GetReceivers() (receivers []*RTPReceiver) {
	pc.mu.Lock()
	defer pc.mu.Unlock()

	for _, transceiver := range pc.rtpTransceivers {
		if receiver := transceiver.Receiver(); receiver != nil {
			receivers = append(receivers, receiver)
		}
	}
	return
}

// GetTransceivers returns the RtpTransceiver that are currently attached to this PeerConnection
func (pc *PeerConnection) GetTransceivers() []*RTPTransceiver {
	pc.mu.Lock()
	defer pc.mu.Unlock()

	return pc.rtpTransceivers
}

// AddTrack adds a Track to the PeerConnection
func (pc *PeerConnection) AddTrack(track TrackLocal) (*RTPSender, error) {
	if pc.isClosed.get() {
		return nil, &rtcerr.InvalidStateError{Err: ErrConnectionClosed}
	}

	pc.mu.Lock()
	defer pc.mu.Unlock()
	for _, t := range pc.rtpTransceivers {
		currentDirection := t.getCurrentDirection()
		// According to https://www.w3.org/TR/webrtc/#dom-rtcpeerconnection-addtrack, if the
		// transceiver can be reused only if it's currentDirection never be sendrecv or sendonly.
		// But that will cause sdp inflate. So we only check currentDirection's current value,
		// that's worked for all browsers.
		if !t.stopped && t.kind == track.Kind() && t.Sender() == nil &&
			!(currentDirection == RTPTransceiverDirectionSendrecv || currentDirection == RTPTransceiverDirectionSendonly) {
			sender, err := pc.api.NewRTPSender(track, pc.dtlsTransport)
			if err == nil {
				err = t.SetSender(sender, track)
				if err != nil {
					_ = sender.Stop()
					t.setSender(nil)
				}
			}
			if err != nil {
				return nil, err
			}
			pc.onNegotiationNeeded()
			return sender, nil
		}
	}

	transceiver, err := pc.newTransceiverFromTrack(RTPTransceiverDirectionSendrecv, track)
	if err != nil {
		return nil, err
	}
	pc.addRTPTransceiver(transceiver)
	return transceiver.Sender(), nil
}

// RemoveTrack removes a Track from the PeerConnection
func (pc *PeerConnection) RemoveTrack(sender *RTPSender) (err error) {
	if pc.isClosed.get() {
		return &rtcerr.InvalidStateError{Err: ErrConnectionClosed}
	}

	var transceiver *RTPTransceiver
	pc.mu.Lock()
	defer pc.mu.Unlock()
	for _, t := range pc.rtpTransceivers {
		if t.Sender() == sender {
			transceiver = t
			break
		}
	}
	if transceiver == nil {
		return &rtcerr.InvalidAccessError{Err: ErrSenderNotCreatedByConnection}
	} else if err = sender.Stop(); err == nil {
		err = transceiver.setSendingTrack(nil)
		if err == nil {
			pc.onNegotiationNeeded()
		}
	}
	return
}

func (pc *PeerConnection) newTransceiverFromTrack(direction RTPTransceiverDirection, track TrackLocal) (t *RTPTransceiver, err error) {
	var (
		r *RTPReceiver
		s *RTPSender
	)
	switch direction {
	case RTPTransceiverDirectionSendrecv:
		r, err = pc.api.NewRTPReceiver(track.Kind(), pc.dtlsTransport)
		if err != nil {
			return
		}
		s, err = pc.api.NewRTPSender(track, pc.dtlsTransport)
	case RTPTransceiverDirectionSendonly:
		s, err = pc.api.NewRTPSender(track, pc.dtlsTransport)
	default:
		err = errPeerConnAddTransceiverFromTrackSupport
	}
	if err != nil {
		return
	}
	return newRTPTransceiver(r, s, direction, track.Kind(), pc.api), nil
}

// AddTransceiverFromKind Create a new RtpTransceiver and adds it to the set of transceivers.
func (pc *PeerConnection) AddTransceiverFromKind(kind RTPCodecType, init ...RTPTransceiverInit) (t *RTPTransceiver, err error) {
	if pc.isClosed.get() {
		return nil, &rtcerr.InvalidStateError{Err: ErrConnectionClosed}
	}

	direction := RTPTransceiverDirectionSendrecv
	if len(init) > 1 {
		return nil, errPeerConnAddTransceiverFromKindOnlyAcceptsOne
	} else if len(init) == 1 {
		direction = init[0].Direction
	}
	switch direction {
	case RTPTransceiverDirectionSendonly, RTPTransceiverDirectionSendrecv:
		codecs := pc.api.mediaEngine.getCodecsByKind(kind)
		if len(codecs) == 0 {
			return nil, ErrNoCodecsAvailable
		}
		track, err := NewTrackLocalStaticSample(codecs[0].RTPCodecCapability, util.MathRandAlpha(16), util.MathRandAlpha(16))
		if err != nil {
			return nil, err
		}
		t, err = pc.newTransceiverFromTrack(direction, track)
		if err != nil {
			return nil, err
		}
	case RTPTransceiverDirectionRecvonly:
		receiver, err := pc.api.NewRTPReceiver(kind, pc.dtlsTransport)
		if err != nil {
			return nil, err
		}
		t = newRTPTransceiver(receiver, nil, RTPTransceiverDirectionRecvonly, kind, pc.api)
	default:
		return nil, errPeerConnAddTransceiverFromKindSupport
	}
	pc.mu.Lock()
	pc.addRTPTransceiver(t)
	pc.mu.Unlock()
	return t, nil
}

// AddTransceiverFromTrack Create a new RtpTransceiver(SendRecv or SendOnly) and add it to the set of transceivers.
func (pc *PeerConnection) AddTransceiverFromTrack(track TrackLocal, init ...RTPTransceiverInit) (t *RTPTransceiver, err error) {
	if pc.isClosed.get() {
		return nil, &rtcerr.InvalidStateError{Err: ErrConnectionClosed}
	}

	direction := RTPTransceiverDirectionSendrecv
	if len(init) > 1 {
		return nil, errPeerConnAddTransceiverFromTrackOnlyAcceptsOne
	} else if len(init) == 1 {
		direction = init[0].Direction
	}

	t, err = pc.newTransceiverFromTrack(direction, track)
	if err == nil {
		pc.mu.Lock()
		pc.addRTPTransceiver(t)
		pc.mu.Unlock()
	}
	return
}

// CreateDataChannel creates a new DataChannel object with the given label
// and optional DataChannelInit used to configure properties of the
// underlying channel such as data reliability.
func (pc *PeerConnection) CreateDataChannel(label string, options *DataChannelInit) (*DataChannel, error) {
	// https://w3c.github.io/webrtc-pc/#peer-to-peer-data-api (Step #2)
	if pc.isClosed.get() {
		return nil, &rtcerr.InvalidStateError{Err: ErrConnectionClosed}
	}

	params := &DataChannelParameters{
		Label:   label,
		Ordered: true,
	}

	// https://w3c.github.io/webrtc-pc/#peer-to-peer-data-api (Step #19)
	if options != nil {
		params.ID = options.ID
	}

	if options != nil {
		// Ordered indicates if data is allowed to be delivered out of order. The
		// default value of true, guarantees that data will be delivered in order.
		// https://w3c.github.io/webrtc-pc/#peer-to-peer-data-api (Step #9)
		if options.Ordered != nil {
			params.Ordered = *options.Ordered
		}

		// https://w3c.github.io/webrtc-pc/#peer-to-peer-data-api (Step #7)
		if options.MaxPacketLifeTime != nil {
			params.MaxPacketLifeTime = options.MaxPacketLifeTime
		}

		// https://w3c.github.io/webrtc-pc/#peer-to-peer-data-api (Step #8)
		if options.MaxRetransmits != nil {
			params.MaxRetransmits = options.MaxRetransmits
		}

		// https://w3c.github.io/webrtc-pc/#peer-to-peer-data-api (Step #10)
		if options.Protocol != nil {
			params.Protocol = *options.Protocol
		}

		// https://w3c.github.io/webrtc-pc/#peer-to-peer-data-api (Step #11)
		if len(params.Protocol) > 65535 {
			return nil, &rtcerr.TypeError{Err: ErrProtocolTooLarge}
		}

		// https://w3c.github.io/webrtc-pc/#peer-to-peer-data-api (Step #12)
		if options.Negotiated != nil {
			params.Negotiated = *options.Negotiated
		}
	}

	d, err := pc.api.newDataChannel(params, nil, pc.log)
	if err != nil {
		return nil, err
	}

	// https://w3c.github.io/webrtc-pc/#peer-to-peer-data-api (Step #16)
	if d.maxPacketLifeTime != nil && d.maxRetransmits != nil {
		return nil, &rtcerr.TypeError{Err: ErrRetransmitsOrPacketLifeTime}
	}

	pc.sctpTransport.lock.Lock()
	pc.sctpTransport.dataChannels = append(pc.sctpTransport.dataChannels, d)
	pc.sctpTransport.dataChannelsRequested++
	pc.sctpTransport.lock.Unlock()

	// If SCTP already connected open all the channels
	if pc.sctpTransport.State() == SCTPTransportStateConnected {
		if err = d.open(pc.sctpTransport); err != nil {
			return nil, err
		}
	}

	pc.mu.Lock()
	pc.onNegotiationNeeded()
	pc.mu.Unlock()

	return d, nil
}

// SetIdentityProvider is used to configure an identity provider to generate identity assertions
func (pc *PeerConnection) SetIdentityProvider(string) error {
	return errPeerConnSetIdentityProviderNotImplemented
}

// WriteRTCP sends a user provided RTCP packet to the connected peer. If no peer is connected the
// packet is discarded. It also runs any configured interceptors.
func (pc *PeerConnection) WriteRTCP(pkts []rtcp.Packet) error {
	_, err := pc.interceptorRTCPWriter.Write(pkts, make(interceptor.Attributes))
	return err
}

func (pc *PeerConnection) writeRTCP(pkts []rtcp.Packet, _ interceptor.Attributes) (int, error) {
	return pc.dtlsTransport.WriteRTCP(pkts)
}

// Close ends the PeerConnection
func (pc *PeerConnection) Close() error {
	// https://www.w3.org/TR/webrtc/#dom-rtcpeerconnection-close (step #1)
	// https://www.w3.org/TR/webrtc/#dom-rtcpeerconnection-close (step #2)
	if pc.isClosed.swap(true) {
		return nil
	}

	// https://www.w3.org/TR/webrtc/#dom-rtcpeerconnection-close (step #3)
	pc.signalingState.Set(SignalingStateClosed)

	// Try closing everything and collect the errors
	// Shutdown strategy:
	// 1. All Conn close by closing their underlying Conn.
	// 2. A Mux stops this chain. It won't close the underlying
	//    Conn if one of the endpoints is closed down. To
	//    continue the chain the Mux has to be closed.
	closeErrs := make([]error, 4)

	closeErrs = append(closeErrs, pc.api.interceptor.Close())

	// https://www.w3.org/TR/webrtc/#dom-rtcpeerconnection-close (step #4)
	pc.mu.Lock()
	for _, t := range pc.rtpTransceivers {
		if !t.stopped {
			closeErrs = append(closeErrs, t.Stop())
		}
	}
	pc.mu.Unlock()

	// https://www.w3.org/TR/webrtc/#dom-rtcpeerconnection-close (step #5)
	pc.sctpTransport.lock.Lock()
	for _, d := range pc.sctpTransport.dataChannels {
		d.setReadyState(DataChannelStateClosed)
	}
	pc.sctpTransport.lock.Unlock()

	// https://www.w3.org/TR/webrtc/#dom-rtcpeerconnection-close (step #6)
	if pc.sctpTransport != nil {
		closeErrs = append(closeErrs, pc.sctpTransport.Stop())
	}

	// https://www.w3.org/TR/webrtc/#dom-rtcpeerconnection-close (step #7)
	closeErrs = append(closeErrs, pc.dtlsTransport.Stop())

	// https://www.w3.org/TR/webrtc/#dom-rtcpeerconnection-close (step #8, #9, #10)
	if pc.iceTransport != nil {
		closeErrs = append(closeErrs, pc.iceTransport.Stop())
	}

	// https://www.w3.org/TR/webrtc/#dom-rtcpeerconnection-close (step #11)
	pc.updateConnectionState(pc.ICEConnectionState(), pc.dtlsTransport.State())

	return util.FlattenErrs(closeErrs)
}

// addRTPTransceiver appends t into rtpTransceivers
// and fires onNegotiationNeeded;
// caller of this method should hold `pc.mu` lock
func (pc *PeerConnection) addRTPTransceiver(t *RTPTransceiver) {
	pc.rtpTransceivers = append(pc.rtpTransceivers, t)
	pc.onNegotiationNeeded()
}

// CurrentLocalDescription represents the local description that was
// successfully negotiated the last time the PeerConnection transitioned
// into the stable state plus any local candidates that have been generated
// by the ICEAgent since the offer or answer was created.
func (pc *PeerConnection) CurrentLocalDescription() *SessionDescription {
	pc.mu.Lock()
	localDescription := pc.currentLocalDescription
	iceGather := pc.iceGatherer
	iceGatheringState := pc.ICEGatheringState()
	pc.mu.Unlock()
	return populateLocalCandidates(localDescription, iceGather, iceGatheringState)
}

// PendingLocalDescription represents a local description that is in the
// process of being negotiated plus any local candidates that have been
// generated by the ICEAgent since the offer or answer was created. If the
// PeerConnection is in the stable state, the value is null.
func (pc *PeerConnection) PendingLocalDescription() *SessionDescription {
	pc.mu.Lock()
	localDescription := pc.pendingLocalDescription
	iceGather := pc.iceGatherer
	iceGatheringState := pc.ICEGatheringState()
	pc.mu.Unlock()
	return populateLocalCandidates(localDescription, iceGather, iceGatheringState)
}

// CurrentRemoteDescription represents the last remote description that was
// successfully negotiated the last time the PeerConnection transitioned
// into the stable state plus any remote candidates that have been supplied
// via AddICECandidate() since the offer or answer was created.
func (pc *PeerConnection) CurrentRemoteDescription() *SessionDescription {
	pc.mu.RLock()
	defer pc.mu.RUnlock()

	return pc.currentRemoteDescription
}

// PendingRemoteDescription represents a remote description that is in the
// process of being negotiated, complete with any remote candidates that
// have been supplied via AddICECandidate() since the offer or answer was
// created. If the PeerConnection is in the stable state, the value is
// null.
func (pc *PeerConnection) PendingRemoteDescription() *SessionDescription {
	pc.mu.RLock()
	defer pc.mu.RUnlock()

	return pc.pendingRemoteDescription
}

// SignalingState attribute returns the signaling state of the
// PeerConnection instance.
func (pc *PeerConnection) SignalingState() SignalingState {
	return pc.signalingState.Get()
}

// ICEGatheringState attribute returns the ICE gathering state of the
// PeerConnection instance.
func (pc *PeerConnection) ICEGatheringState() ICEGatheringState {
	if pc.iceGatherer == nil {
		return ICEGatheringStateNew
	}

	switch pc.iceGatherer.State() {
	case ICEGathererStateNew:
		return ICEGatheringStateNew
	case ICEGathererStateGathering:
		return ICEGatheringStateGathering
	default:
		return ICEGatheringStateComplete
	}
}

// ConnectionState attribute returns the connection state of the
// PeerConnection instance.
func (pc *PeerConnection) ConnectionState() PeerConnectionState {
	if state, ok := pc.connectionState.Load().(PeerConnectionState); ok {
		return state
	}
	return PeerConnectionState(0)
}

// GetStats return data providing statistics about the overall connection
func (pc *PeerConnection) GetStats() StatsReport {
	var (
		dataChannelsAccepted  uint32
		dataChannelsClosed    uint32
		dataChannelsOpened    uint32
		dataChannelsRequested uint32
	)
	statsCollector := newStatsReportCollector()
	statsCollector.Collecting()

	pc.mu.Lock()
	if pc.iceGatherer != nil {
		pc.iceGatherer.collectStats(statsCollector)
	}
	if pc.iceTransport != nil {
		pc.iceTransport.collectStats(statsCollector)
	}

	pc.sctpTransport.lock.Lock()
	dataChannels := append([]*DataChannel{}, pc.sctpTransport.dataChannels...)
	dataChannelsAccepted = pc.sctpTransport.dataChannelsAccepted
	dataChannelsOpened = pc.sctpTransport.dataChannelsOpened
	dataChannelsRequested = pc.sctpTransport.dataChannelsRequested
	pc.sctpTransport.lock.Unlock()

	for _, d := range dataChannels {
		state := d.ReadyState()
		if state != DataChannelStateConnecting && state != DataChannelStateOpen {
			dataChannelsClosed++
		}

		d.collectStats(statsCollector)
	}
	pc.sctpTransport.collectStats(statsCollector)

	stats := PeerConnectionStats{
		Timestamp:             statsTimestampNow(),
		Type:                  StatsTypePeerConnection,
		ID:                    pc.statsID,
		DataChannelsAccepted:  dataChannelsAccepted,
		DataChannelsClosed:    dataChannelsClosed,
		DataChannelsOpened:    dataChannelsOpened,
		DataChannelsRequested: dataChannelsRequested,
	}

	statsCollector.Collect(stats.ID, stats)

	certificates := pc.configuration.Certificates
	for _, certificate := range certificates {
		if err := certificate.collectStats(statsCollector); err != nil {
			continue
		}
	}
	pc.mu.Unlock()

	pc.api.mediaEngine.collectStats(statsCollector)

	return statsCollector.Ready()
}

// Start all transports. PeerConnection now has enough state
func (pc *PeerConnection) startTransports(iceRole ICERole, dtlsRole DTLSRole, remoteUfrag, remotePwd, fingerprint, fingerprintHash string) {
	// Start the ice transport
	err := pc.iceTransport.Start(
		pc.iceGatherer,
		ICEParameters{
			UsernameFragment: remoteUfrag,
			Password:         remotePwd,
			ICELite:          false,
		},
		&iceRole,
	)
	if err != nil {
		pc.log.Warnf("Failed to start manager: %s", err)
		return
	}

	// Start the dtls transport
	err = pc.dtlsTransport.Start(DTLSParameters{
		Role:         dtlsRole,
		Fingerprints: []DTLSFingerprint{{Algorithm: fingerprintHash, Value: fingerprint}},
	})
	pc.updateConnectionState(pc.ICEConnectionState(), pc.dtlsTransport.State())
	if err != nil {
		pc.log.Warnf("Failed to start manager: %s", err)
		return
	}
}

// nolint: gocognit
func (pc *PeerConnection) startRTP(isRenegotiation bool, remoteDesc *SessionDescription, currentTransceivers []*RTPTransceiver) {
	if !isRenegotiation {
		pc.undeclaredMediaProcessor()
	}

	pc.startRTPReceivers(remoteDesc, currentTransceivers)
	if haveApplicationMediaSection(remoteDesc.parsed) {
		pc.startSCTP()
	}
}

// generateUnmatchedSDP generates an SDP that doesn't take remote state into account
// This is used for the initial call for CreateOffer
func (pc *PeerConnection) generateUnmatchedSDP(transceivers []*RTPTransceiver, useIdentity bool) (*sdp.SessionDescription, error) {
	d, err := sdp.NewJSEPSessionDescription(useIdentity)
	if err != nil {
		return nil, err
	}

	iceParams, err := pc.iceGatherer.GetLocalParameters()
	if err != nil {
		return nil, err
	}

	candidates, err := pc.iceGatherer.GetLocalCandidates()
	if err != nil {
		return nil, err
	}

	isPlanB := pc.configuration.SDPSemantics == SDPSemanticsPlanB
	mediaSections := []mediaSection{}

	// Needed for pc.sctpTransport.dataChannelsRequested
	pc.sctpTransport.lock.Lock()
	defer pc.sctpTransport.lock.Unlock()

	if isPlanB {
		video := make([]*RTPTransceiver, 0)
		audio := make([]*RTPTransceiver, 0)

		for _, t := range transceivers {
			if t.kind == RTPCodecTypeVideo {
				video = append(video, t)
			} else if t.kind == RTPCodecTypeAudio {
				audio = append(audio, t)
			}
			if sender := t.Sender(); sender != nil {
				sender.setNegotiated()
			}
		}

		if len(video) > 0 {
			mediaSections = append(mediaSections, mediaSection{id: "video", transceivers: video})
		}
		if len(audio) > 0 {
			mediaSections = append(mediaSections, mediaSection{id: "audio", transceivers: audio})
		}

		if pc.sctpTransport.dataChannelsRequested != 0 {
			mediaSections = append(mediaSections, mediaSection{id: "data", data: true})
		}
	} else {
		for _, t := range transceivers {
			if sender := t.Sender(); sender != nil {
				sender.setNegotiated()
			}
			mediaSections = append(mediaSections, mediaSection{id: t.Mid(), transceivers: []*RTPTransceiver{t}})
		}

		if pc.sctpTransport.dataChannelsRequested != 0 {
			mediaSections = append(mediaSections, mediaSection{id: strconv.Itoa(len(mediaSections)), data: true})
		}
	}

	dtlsFingerprints, err := pc.configuration.Certificates[0].GetFingerprints()
	if err != nil {
		return nil, err
	}

	return populateSDP(d, isPlanB, dtlsFingerprints, pc.api.settingEngine.sdpMediaLevelFingerprints, pc.api.settingEngine.candidates.ICELite, true, pc.api.mediaEngine, connectionRoleFromDtlsRole(defaultDtlsRoleOffer), candidates, iceParams, mediaSections, pc.ICEGatheringState())
}

// generateMatchedSDP generates a SDP and takes the remote state into account
// this is used everytime we have a RemoteDescription
// nolint: gocyclo
func (pc *PeerConnection) generateMatchedSDP(transceivers []*RTPTransceiver, useIdentity bool, includeUnmatched bool, connectionRole sdp.ConnectionRole) (*sdp.SessionDescription, error) { //nolint:gocognit
	d, err := sdp.NewJSEPSessionDescription(useIdentity)
	if err != nil {
		return nil, err
	}

	iceParams, err := pc.iceGatherer.GetLocalParameters()
	if err != nil {
		return nil, err
	}

	candidates, err := pc.iceGatherer.GetLocalCandidates()
	if err != nil {
		return nil, err
	}

	var t *RTPTransceiver
	remoteDescription := pc.currentRemoteDescription
	if pc.pendingRemoteDescription != nil {
		remoteDescription = pc.pendingRemoteDescription
	}
	isExtmapAllowMixed := isExtMapAllowMixedSet(remoteDescription.parsed)
	localTransceivers := append([]*RTPTransceiver{}, transceivers...)

	detectedPlanB := descriptionIsPlanB(remoteDescription, pc.log)
	if pc.configuration.SDPSemantics != SDPSemanticsUnifiedPlan {
		detectedPlanB = descriptionPossiblyPlanB(remoteDescription)
	}

	mediaSections := []mediaSection{}
	alreadyHaveApplicationMediaSection := false
	for _, media := range remoteDescription.parsed.MediaDescriptions {
		midValue := getMidValue(media)
		if midValue == "" {
			return nil, errPeerConnRemoteDescriptionWithoutMidValue
		}

		if media.MediaName.Media == mediaSectionApplication {
			mediaSections = append(mediaSections, mediaSection{id: midValue, data: true})
			alreadyHaveApplicationMediaSection = true
			continue
		}

		kind := NewRTPCodecType(media.MediaName.Media)
		direction := getPeerDirection(media)
		if kind == 0 || direction == RTPTransceiverDirection(Unknown) {
			continue
		}

		sdpSemantics := pc.configuration.SDPSemantics

		switch {
		case sdpSemantics == SDPSemanticsPlanB || sdpSemantics == SDPSemanticsUnifiedPlanWithFallback && detectedPlanB:
			if !detectedPlanB {
				return nil, &rtcerr.TypeError{Err: fmt.Errorf("%w: Expected PlanB, but RemoteDescription is UnifiedPlan", ErrIncorrectSDPSemantics)}
			}
			// If we're responding to a plan-b offer, then we should try to fill up this
			// media entry with all matching local transceivers
			mediaTransceivers := []*RTPTransceiver{}
			for {
				// keep going until we can't get any more
				t, localTransceivers = satisfyTypeAndDirection(kind, direction, localTransceivers)
				if t == nil {
					if len(mediaTransceivers) == 0 {
						t = &RTPTransceiver{kind: kind, api: pc.api, codecs: pc.api.mediaEngine.getCodecsByKind(kind)}
						t.setDirection(RTPTransceiverDirectionInactive)
						mediaTransceivers = append(mediaTransceivers, t)
					}
					break
				}
				if sender := t.Sender(); sender != nil {
					sender.setNegotiated()
				}
				mediaTransceivers = append(mediaTransceivers, t)
			}
			mediaSections = append(mediaSections, mediaSection{id: midValue, transceivers: mediaTransceivers})
		case sdpSemantics == SDPSemanticsUnifiedPlan || sdpSemantics == SDPSemanticsUnifiedPlanWithFallback:
			if detectedPlanB {
				return nil, &rtcerr.TypeError{Err: fmt.Errorf("%w: Expected UnifiedPlan, but RemoteDescription is PlanB", ErrIncorrectSDPSemantics)}
			}
			t, localTransceivers = findByMid(midValue, localTransceivers)
			if t == nil {
				return nil, fmt.Errorf("%w: %q", errPeerConnTranscieverMidNil, midValue)
			}
			if sender := t.Sender(); sender != nil {
				sender.setNegotiated()
			}
			mediaTransceivers := []*RTPTransceiver{t}
			mediaSections = append(mediaSections, mediaSection{id: midValue, transceivers: mediaTransceivers, ridMap: getRids(media)})
		}
	}

	// If we are offering also include unmatched local transceivers
	if includeUnmatched {
		if !detectedPlanB {
			for _, t := range localTransceivers {
				if sender := t.Sender(); sender != nil {
					sender.setNegotiated()
				}
				mediaSections = append(mediaSections, mediaSection{id: t.Mid(), transceivers: []*RTPTransceiver{t}})
			}
		}

		if pc.sctpTransport.dataChannelsRequested != 0 && !alreadyHaveApplicationMediaSection {
			if detectedPlanB {
				mediaSections = append(mediaSections, mediaSection{id: "data", data: true})
			} else {
				mediaSections = append(mediaSections, mediaSection{id: strconv.Itoa(len(mediaSections)), data: true})
			}
		}
	}

	if pc.configuration.SDPSemantics == SDPSemanticsUnifiedPlanWithFallback && detectedPlanB {
		pc.log.Info("Plan-B Offer detected; responding with Plan-B Answer")
	}

	dtlsFingerprints, err := pc.configuration.Certificates[0].GetFingerprints()
	if err != nil {
		return nil, err
	}

	return populateSDP(d, detectedPlanB, dtlsFingerprints, pc.api.settingEngine.sdpMediaLevelFingerprints, pc.api.settingEngine.candidates.ICELite, isExtmapAllowMixed, pc.api.mediaEngine, connectionRole, candidates, iceParams, mediaSections, pc.ICEGatheringState())
}

func (pc *PeerConnection) setGatherCompleteHandler(handler func()) {
	pc.iceGatherer.onGatheringCompleteHandler.Store(handler)
}

// SCTP returns the SCTPTransport for this PeerConnection
//
// The SCTP transport over which SCTP data is sent and received. If SCTP has not been negotiated, the value is nil.
// https://www.w3.org/TR/webrtc/#attributes-15
func (pc *PeerConnection) SCTP() *SCTPTransport {
	return pc.sctpTransport
}
