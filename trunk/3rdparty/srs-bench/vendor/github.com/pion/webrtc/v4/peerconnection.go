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

	"github.com/pion/ice/v4"
	"github.com/pion/interceptor"
	"github.com/pion/logging"
	"github.com/pion/rtcp"
	"github.com/pion/sdp/v3"
	"github.com/pion/srtp/v3"
	"github.com/pion/webrtc/v4/internal/util"
	"github.com/pion/webrtc/v4/pkg/rtcerr"
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

	isClosed                                *atomic.Bool
	isGracefullyClosingOrClosed             bool
	isCloseDone                             chan struct{}
	isGracefulCloseDone                     chan struct{}
	isNegotiationNeeded                     *atomic.Bool
	updateNegotiationNeededFlagOnEmptyChain *atomic.Bool

	lastOffer  string
	lastAnswer string

	// a value containing the last known greater mid value
	// we internally generate mids as numbers. Needed since JSEP
	// requires that when reusing a media section a new unique mid
	// should be defined (see JSEP 3.4.1).
	greaterMid int

	rtpTransceivers        []*RTPTransceiver
	nonMediaBandwidthProbe atomic.Value // RTPReceiver

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

// NewPeerConnection creates a PeerConnection with the default codecs and interceptors.
//
// If you wish to customize the set of available codecs and/or the set of active interceptors,
// create an API with a custom MediaEngine and/or interceptor.Registry,
// then call [(*API).NewPeerConnection] instead of this function.
func NewPeerConnection(configuration Configuration) (*PeerConnection, error) {
	api := NewAPI()

	return api.NewPeerConnection(configuration)
}

// NewPeerConnection creates a new PeerConnection with the provided configuration against the received API object.
// This method will attach a default set of codecs and interceptors to
// the resulting PeerConnection.  If this behavior is not desired,
// set the set of codecs and interceptors explicitly by using
// [WithMediaEngine] and [WithInterceptorRegistry] when calling [NewAPI].
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
		isClosed:                                &atomic.Bool{},
		isCloseDone:                             make(chan struct{}),
		isGracefulCloseDone:                     make(chan struct{}),
		isNegotiationNeeded:                     &atomic.Bool{},
		updateNegotiationNeededFlagOnEmptyChain: &atomic.Bool{},
		lastOffer:                               "",
		lastAnswer:                              "",
		greaterMid:                              -1,
		signalingState:                          SignalingStateStable,

		api: api,
		log: api.settingEngine.LoggerFactory.NewLogger("pc"),
	}
	pc.ops = newOperations(pc.updateNegotiationNeededFlagOnEmptyChain, pc.onNegotiationNeeded)

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
		pc.api.mediaEngine.setMultiCodecNegotiation(!api.settingEngine.disableMediaEngineMultipleCodecs)
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
func (pc *PeerConnection) initConfiguration(configuration Configuration) error { //nolint:cyclop
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

	if configuration.BundlePolicy != BundlePolicyUnknown {
		pc.configuration.BundlePolicy = configuration.BundlePolicy
	}

	if configuration.RTCPMuxPolicy != RTCPMuxPolicyUnknown {
		pc.configuration.RTCPMuxPolicy = configuration.RTCPMuxPolicy
	}

	if configuration.ICECandidatePoolSize != 0 {
		pc.configuration.ICECandidatePoolSize = configuration.ICECandidatePoolSize
	}

	pc.configuration.ICETransportPolicy = configuration.ICETransportPolicy
	pc.configuration.SDPSemantics = configuration.SDPSemantics

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
// peer connection's signaling state changes.
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
// a change has occurred which requires session negotiation.
func (pc *PeerConnection) OnNegotiationNeeded(f func()) {
	pc.onNegotiationNeededHandler.Store(f)
}

// onNegotiationNeeded enqueues negotiationNeededOp if necessary
// caller of this method should hold `pc.mu` lock
// https://www.w3.org/TR/webrtc/#dfn-update-the-negotiation-needed-flag
func (pc *PeerConnection) onNegotiationNeeded() {
	// 4.7.3.1 If the length of connection.[[Operations]] is not 0, then set
	// connection.[[UpdateNegotiationNeededFlagOnEmptyChain]] to true, and abort these steps.
	if !pc.ops.IsEmpty() {
		pc.updateNegotiationNeededFlagOnEmptyChain.Store(true)

		return
	}
	pc.ops.Enqueue(pc.negotiationNeededOp)
}

// https://www.w3.org/TR/webrtc/#dfn-update-the-negotiation-needed-flag
func (pc *PeerConnection) negotiationNeededOp() {
	// 4.7.3.2.1 If connection.[[IsClosed]] is true, abort these steps.
	if pc.isClosed.Load() {
		return
	}

	// 4.7.3.2.2 If the length of connection.[[Operations]] is not 0,
	// then set connection.[[UpdateNegotiationNeededFlagOnEmptyChain]] to
	// true, and abort these steps.
	if !pc.ops.IsEmpty() {
		pc.updateNegotiationNeededFlagOnEmptyChain.Store(true)

		return
	}

	// 4.7.3.2.3 If connection's signaling state is not "stable", abort these steps.
	if pc.SignalingState() != SignalingStateStable {
		return
	}

	// 4.7.3.2.4 If the result of checking if negotiation is needed is false,
	// clear the negotiation-needed flag by setting connection.[[NegotiationNeeded]]
	// to false, and abort these steps.
	if !pc.checkNegotiationNeeded() {
		pc.isNegotiationNeeded.Store(false)

		return
	}

	// 4.7.3.2.5 If connection.[[NegotiationNeeded]] is already true, abort these steps.
	if pc.isNegotiationNeeded.Load() {
		return
	}

	// 4.7.3.2.6 Set connection.[[NegotiationNeeded]] to true.
	pc.isNegotiationNeeded.Store(true)

	// 4.7.3.2.7 Fire an event named negotiationneeded at connection.
	if handler, ok := pc.onNegotiationNeededHandler.Load().(func()); ok && handler != nil {
		handler()
	}
}

func (pc *PeerConnection) checkNegotiationNeeded() bool { //nolint:gocognit,cyclop
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

	for _, transceiver := range pc.rtpTransceivers {
		// https://www.w3.org/TR/webrtc/#dfn-update-the-negotiation-needed-flag
		// Step 5.1
		// if t.stopping && !t.stopped {
		// 	return true
		// }
		mid := getByMid(transceiver.Mid(), localDesc)

		// Step 5.2
		if mid == nil {
			return true
		}

		// Step 5.3.1
		if transceiver.Direction() == RTPTransceiverDirectionSendrecv ||
			transceiver.Direction() == RTPTransceiverDirectionSendonly {
			descMsid, okMsid := mid.Attribute(sdp.AttrKeyMsid)
			sender := transceiver.Sender()
			if sender == nil {
				return true
			}
			track := sender.Track()
			if track == nil {
				// Situation when sender's track is nil could happen when
				// a) replaceTrack(nil) is called
				// b) removeTrack() is called, changing the transceiver's direction to inactive
				// As t.Direction() in this branch is either sendrecv or sendonly, we believe (a) option is the case
				// As calling replaceTrack does not require renegotiation, we skip check for this transceiver
				continue
			}
			if !okMsid || descMsid != track.StreamID()+" "+track.ID() {
				return true
			}
		}
		switch localDesc.Type {
		case SDPTypeOffer:
			// Step 5.3.2
			rm := getByMid(transceiver.Mid(), remoteDesc)
			if rm == nil {
				return true
			}

			if getPeerDirection(mid) != transceiver.Direction() && getPeerDirection(rm) != transceiver.Direction().Revers() {
				return true
			}
		case SDPTypeAnswer:
			// Step 5.3.3
			if _, ok := mid.Attribute(transceiver.Direction().String()); !ok {
				return true
			}
		default:
		}

		// Step 5.4
		// if t.stopped && t.Mid() != "" {
		// 	if getByMid(t.Mid(), localDesc) != nil || getByMid(t.Mid(), remoteDesc) != nil {
		// 		return true
		// 	}
		// }
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
func (pc *PeerConnection) OnICEGatheringStateChange(f func(ICEGatheringState)) {
	pc.iceGatherer.OnStateChange(
		func(gathererState ICEGathererState) {
			switch gathererState {
			case ICEGathererStateGathering:
				f(ICEGatheringStateGathering)
			case ICEGathererStateComplete:
				f(ICEGatheringStateComplete)
			default:
				// Other states ignored
			}
		})
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
// when the PeerConnectionState has changed.
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
func (pc *PeerConnection) SetConfiguration(configuration Configuration) error { //nolint:gocognit,cyclop
	// https://www.w3.org/TR/webrtc/#dom-rtcpeerconnection-setconfiguration (step #2)
	if pc.isClosed.Load() {
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
	if configuration.BundlePolicy != BundlePolicyUnknown {
		if configuration.BundlePolicy != pc.configuration.BundlePolicy {
			return &rtcerr.InvalidModificationError{Err: ErrModifyingBundlePolicy}
		}
		pc.configuration.BundlePolicy = configuration.BundlePolicy
	}

	// https://www.w3.org/TR/webrtc/#set-the-configuration (step #6)
	if configuration.RTCPMuxPolicy != RTCPMuxPolicyUnknown {
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
	pc.configuration.ICETransportPolicy = configuration.ICETransportPolicy

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
// caller of this method should hold `pc.mu` lock.
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
//
//nolint:gocognit,cyclop
func (pc *PeerConnection) CreateOffer(options *OfferOptions) (SessionDescription, error) {
	useIdentity := pc.idpLoginURL != nil
	switch {
	case useIdentity:
		return SessionDescription{}, errIdentityProviderNotImplemented
	case pc.isClosed.Load():
		return SessionDescription{}, &rtcerr.InvalidStateError{Err: ErrConnectionClosed}
	}

	if options != nil && options.ICERestart {
		if err := pc.iceTransport.restart(); err != nil {
			return SessionDescription{}, err
		}
	}

	var (
		descr *sdp.SessionDescription
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
		if !isPlanB { //nolint:nestif
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
			descr, err = pc.generateUnmatchedSDP(currentTransceivers, useIdentity)
		} else {
			descr, err = pc.generateMatchedSDP(
				currentTransceivers,
				useIdentity,
				true, /*includeUnmatched */
				connectionRoleFromDtlsRole(defaultDtlsRoleOffer),
			)
		}

		if err != nil {
			return SessionDescription{}, err
		}

		updateSDPOrigin(&pc.sdpOrigin, descr)
		sdpBytes, err := descr.Marshal()
		if err != nil {
			return SessionDescription{}, err
		}

		offer = SessionDescription{
			Type:   SDPTypeOffer,
			SDP:    string(sdpBytes),
			parsed: descr,
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
//
//nolint:cyclop
func (pc *PeerConnection) updateConnectionState(
	iceConnectionState ICEConnectionState,
	dtlsTransportState DTLSTransportState,
) {
	connectionState := PeerConnectionStateNew
	switch {
	// The RTCPeerConnection object's [[IsClosed]] slot is true.
	case pc.isClosed.Load():
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
	case (iceConnectionState == ICEConnectionStateConnected ||
		iceConnectionState == ICEConnectionStateCompleted || iceConnectionState == ICEConnectionStateClosed) &&
		(dtlsTransportState == DTLSTransportStateConnected || dtlsTransportState == DTLSTransportStateClosed):
		connectionState = PeerConnectionStateConnected
	}

	if pc.connectionState.Load() == connectionState {
		return
	}

	pc.onConnectionStateChange(connectionState)
}

func (pc *PeerConnection) createICETransport() *ICETransport {
	transport := pc.api.NewICETransport(pc.iceGatherer)
	transport.internalOnConnectionStateChangeHandler.Store(func(state ICETransportState) {
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

	return transport
}

// CreateAnswer starts the PeerConnection and generates the localDescription.
//
//nolint:cyclop
func (pc *PeerConnection) CreateAnswer(*AnswerOptions) (SessionDescription, error) {
	useIdentity := pc.idpLoginURL != nil
	remoteDesc := pc.RemoteDescription()
	switch {
	case remoteDesc == nil:
		return SessionDescription{}, &rtcerr.InvalidStateError{Err: ErrNoRemoteDescription}
	case useIdentity:
		return SessionDescription{}, errIdentityProviderNotImplemented
	case pc.isClosed.Load():
		return SessionDescription{}, &rtcerr.InvalidStateError{Err: ErrConnectionClosed}
	case pc.signalingState.Get() != SignalingStateHaveRemoteOffer &&
		pc.signalingState.Get() != SignalingStateHaveLocalPranswer:
		return SessionDescription{}, &rtcerr.InvalidStateError{Err: ErrIncorrectSignalingState}
	}

	connectionRole := connectionRoleFromDtlsRole(pc.api.settingEngine.answeringDTLSRole)
	if connectionRole == sdp.ConnectionRole(0) {
		connectionRole = connectionRoleFromDtlsRole(defaultDtlsRoleAnswer)

		// If one of the agents is lite and the other one is not, the lite agent must be the controlled agent.
		// If both or neither agents are lite the offering agent is controlling.
		// RFC 8445 S6.1.1
		if isIceLiteSet(remoteDesc.parsed) && !pc.api.settingEngine.candidates.ICELite {
			connectionRole = connectionRoleFromDtlsRole(DTLSRoleServer)
		}
	}
	pc.mu.Lock()
	defer pc.mu.Unlock()

	descr, err := pc.generateMatchedSDP(pc.rtpTransceivers, useIdentity, false /*includeUnmatched */, connectionRole)
	if err != nil {
		return SessionDescription{}, err
	}

	updateSDPOrigin(&pc.sdpOrigin, descr)
	sdpBytes, err := descr.Marshal()
	if err != nil {
		return SessionDescription{}, err
	}

	desc := SessionDescription{
		Type:   SDPTypeAnswer,
		SDP:    string(sdpBytes),
		parsed: descr,
	}
	pc.lastAnswer = desc.SDP

	return desc, nil
}

// 4.4.1.6 Set the SessionDescription
//
//nolint:gocognit,cyclop
func (pc *PeerConnection) setDescription(sd *SessionDescription, op stateChangeOp) error {
	switch {
	case pc.isClosed.Load():
		return &rtcerr.InvalidStateError{Err: ErrConnectionClosed}
	case NewSDPType(sd.Type.String()) == SDPTypeUnknown:
		return &rtcerr.TypeError{
			Err: fmt.Errorf("%w: '%d' is not a valid enum value of type SDPType", errPeerConnSDPTypeInvalidValue, sd.Type),
		}
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
			pc.isNegotiationNeeded.Store(false)
			pc.mu.Lock()
			pc.onNegotiationNeeded()
			pc.mu.Unlock()
		}
		pc.onSignalingStateChange(nextState)
	}

	return err
}

// SetLocalDescription sets the SessionDescription of the local peer
//
//nolint:cyclop
func (pc *PeerConnection) SetLocalDescription(desc SessionDescription) error {
	if pc.isClosed.Load() {
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
	if err := desc.parsed.UnmarshalString(desc.SDP); err != nil {
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

	mediaSection, ok := selectCandidateMediaSection(desc.parsed)
	if ok {
		pc.iceGatherer.setMediaStreamIdentification(mediaSection.SDPMid, mediaSection.SDPMLineIndex)
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
//
//nolint:gocognit,gocyclo,cyclop,maintidx
func (pc *PeerConnection) SetRemoteDescription(desc SessionDescription) error {
	if pc.isClosed.Load() {
		return &rtcerr.InvalidStateError{Err: ErrConnectionClosed}
	}

	isRenegotiation := pc.currentRemoteDescription != nil

	if _, err := desc.Unmarshal(); err != nil {
		return err
	}
	if err := pc.setDescription(&desc, stateChangeOpSetRemote); err != nil {
		return err
	}

	if err := pc.api.mediaEngine.updateFromRemoteDescription(*desc.parsed); err != nil {
		return err
	}

	// Disable RTX/FEC on RTPSenders if the remote didn't support it
	for _, sender := range pc.GetSenders() {
		sender.configureRTXAndFEC()
	}

	var transceiver *RTPTransceiver
	localTransceivers := append([]*RTPTransceiver{}, pc.GetTransceivers()...)
	detectedPlanB := descriptionIsPlanB(pc.RemoteDescription(), pc.log)
	if pc.configuration.SDPSemantics != SDPSemanticsUnifiedPlan {
		detectedPlanB = descriptionPossiblyPlanB(pc.RemoteDescription())
	}

	weOffer := desc.Type == SDPTypeAnswer

	if !weOffer && !detectedPlanB { //nolint:nestif
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
			if kind == 0 || direction == RTPTransceiverDirectionUnknown {
				continue
			}

			transceiver, localTransceivers = findByMid(midValue, localTransceivers)
			if transceiver == nil {
				transceiver, localTransceivers = satisfyTypeAndDirection(kind, direction, localTransceivers)
			} else if direction == RTPTransceiverDirectionInactive {
				if err := transceiver.Stop(); err != nil {
					return err
				}
			}

			switch {
			case transceiver == nil:
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

				transceiver = newRTPTransceiver(receiver, nil, localDirection, kind, pc.api)
				pc.mu.Lock()
				pc.addRTPTransceiver(transceiver)
				pc.mu.Unlock()

				// if transceiver is create by remote sdp, set prefer codec same as remote peer
				if codecs, err := codecsFromMediaDescription(media); err == nil {
					filteredCodecs := []RTPCodecParameters{}
					for _, codec := range codecs {
						if c, matchType := codecParametersFuzzySearch(
							codec,
							pc.api.mediaEngine.getCodecsByKind(kind),
						); matchType == codecMatchExact {
							// if codec match exact, use payloadtype register to mediaengine
							codec.PayloadType = c.PayloadType
							filteredCodecs = append(filteredCodecs, codec)
						}
					}
					_ = transceiver.SetCodecPreferences(filteredCodecs)
				}

			case direction == RTPTransceiverDirectionRecvonly:
				if transceiver.Direction() == RTPTransceiverDirectionSendrecv {
					transceiver.setDirection(RTPTransceiverDirectionSendonly)
				} else if transceiver.Direction() == RTPTransceiverDirectionRecvonly {
					transceiver.setDirection(RTPTransceiverDirectionInactive)
				}
			case direction == RTPTransceiverDirectionSendrecv:
				if transceiver.Direction() == RTPTransceiverDirectionSendonly {
					transceiver.setDirection(RTPTransceiverDirectionSendrecv)
				} else if transceiver.Direction() == RTPTransceiverDirectionInactive {
					transceiver.setDirection(RTPTransceiverDirectionRecvonly)
				}
			case direction == RTPTransceiverDirectionSendonly:
				if transceiver.Direction() == RTPTransceiverDirectionInactive {
					transceiver.setDirection(RTPTransceiverDirectionRecvonly)
				}
			}

			if transceiver.Mid() == "" {
				if err := transceiver.SetMid(midValue); err != nil {
					return err
				}
			}
		}
	}

	iceDetails, err := extractICEDetails(desc.parsed, pc.log)
	if err != nil {
		return err
	}

	if isRenegotiation && pc.iceTransport.haveRemoteCredentialsChange(iceDetails.Ufrag, iceDetails.Password) {
		// An ICE Restart only happens implicitly for a SetRemoteDescription of type offer
		if !weOffer {
			if err = pc.iceTransport.restart(); err != nil {
				return err
			}
		}

		if err = pc.iceTransport.setRemoteCredentials(iceDetails.Ufrag, iceDetails.Password); err != nil {
			return err
		}
	}

	for i := range iceDetails.Candidates {
		if err = pc.iceTransport.AddRemoteCandidate(&iceDetails.Candidates[i]); err != nil {
			return err
		}
	}

	currentTransceivers := append([]*RTPTransceiver{}, pc.GetTransceivers()...)

	if isRenegotiation {
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
	// If one of the agents is lite and the other one is not, the lite agent must be the controlled agent.
	// If both or neither agents are lite the offering agent is controlling.
	// RFC 8445 S6.1.1
	if (weOffer && remoteIsLite == pc.api.settingEngine.candidates.ICELite) ||
		(remoteIsLite && !pc.api.settingEngine.candidates.ICELite) {
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
		pc.startTransports(
			iceRole,
			dtlsRoleFromRemoteSDP(desc.parsed),
			iceDetails.Ufrag,
			iceDetails.Password,
			fingerprint,
			fingerprintHash,
		)
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

	for _, track := range receiver.Tracks() {
		if track.SSRC() == 0 || track.RID() != "" {
			return
		}

		if pc.api.settingEngine.fireOnTrackBeforeFirstRTP {
			pc.onTrack(track, receiver)

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
		}(track)
	}
}

//nolint:cyclop
func setRTPTransceiverCurrentDirection(
	answer *SessionDescription,
	currentTransceivers []*RTPTransceiver,
	weOffer bool,
) error {
	currentTransceivers = append([]*RTPTransceiver{}, currentTransceivers...)
	for _, media := range answer.parsed.MediaDescriptions {
		midValue := getMidValue(media)
		if midValue == "" {
			return errPeerConnRemoteDescriptionWithoutMidValue
		}

		if media.MediaName.Media == mediaSectionApplication {
			continue
		}

		var transceiver *RTPTransceiver
		transceiver, currentTransceivers = findByMid(midValue, currentTransceivers)

		if transceiver == nil {
			return fmt.Errorf("%w: %q", errPeerConnTranscieverMidNil, midValue)
		}

		direction := getPeerDirection(media)
		if direction == RTPTransceiverDirectionUnknown {
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
		if !weOffer && direction == RTPTransceiverDirectionSendonly && transceiver.Sender() == nil {
			direction = RTPTransceiverDirectionInactive
		}

		transceiver.setCurrentDirection(direction)
	}

	return nil
}

func runIfNewReceiver(
	incomingTrack trackDetails,
	transceivers []*RTPTransceiver,
	callbackFunc func(incomingTrack trackDetails, receiver *RTPReceiver),
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

		callbackFunc(incomingTrack, receiver)

		return true
	}

	return false
}

// configureRTPReceivers opens knows inbound SRTP streams from the RemoteDescription.
//
//nolint:gocognit,cyclop
func (pc *PeerConnection) configureRTPReceivers(
	isRenegotiation bool,
	remoteDesc *SessionDescription,
	currentTransceivers []*RTPTransceiver,
) {
	incomingTracks := trackDetailsFromSDP(pc.log, remoteDesc.parsed)

	if isRenegotiation { //nolint:nestif
		for _, transceiver := range currentTransceivers {
			receiver := transceiver.Receiver()
			if receiver == nil {
				continue
			}

			tracks := transceiver.Receiver().Tracks()
			if len(tracks) == 0 {
				continue
			}

			mid := transceiver.Mid()
			receiverNeedsStopped := false
			for _, trackRemote := range tracks {
				func(track *TrackRemote) {
					track.mu.Lock()
					defer track.mu.Unlock()

					if track.rid != "" {
						if details := trackDetailsForRID(incomingTracks, mid, track.rid); details != nil {
							track.id = details.id
							track.streamID = details.streamID

							return
						}
					} else if track.ssrc != 0 {
						if details := trackDetailsForSSRC(incomingTracks, track.ssrc); details != nil {
							track.id = details.id
							track.streamID = details.streamID

							return
						}
					}

					receiverNeedsStopped = true
				}(trackRemote)
			}

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
			transceiver.setReceiver(receiver)
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

// startRTPReceivers opens knows inbound SRTP streams from the RemoteDescription.
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

// startRTPSenders starts all outbound RTP streams.
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

// Start SCTP subsystem.
func (pc *PeerConnection) startSCTP(maxMessageSize uint32) {
	// Start sctp
	if err := pc.sctpTransport.Start(SCTPCapabilities{
		MaxMessageSize: maxMessageSize,
	}); err != nil {
		pc.log.Warnf("Failed to start SCTP: %s", err)
		if err = pc.sctpTransport.Stop(); err != nil {
			pc.log.Warnf("Failed to stop SCTPTransport: %s", err)
		}

		return
	}
}

func (pc *PeerConnection) handleUndeclaredSSRC(
	ssrc SSRC,
	mediaSection *sdp.MediaDescription,
) (handled bool, err error) {
	streamID := ""
	id := ""
	hasRidAttribute := false
	hasSSRCAttribute := false

	for _, a := range mediaSection.Attributes {
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
		return false, errMediaSectionHasExplictSSRCAttribute
	}

	incoming := trackDetails{
		ssrcs:    []SSRC{ssrc},
		kind:     RTPCodecTypeVideo,
		streamID: streamID,
		id:       id,
	}
	if mediaSection.MediaName.Media == RTPCodecTypeAudio.String() {
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

// For legacy clients that didn't support urn:ietf:params:rtp-hdrext:sdes:rtp-stream-id
// or urn:ietf:params:rtp-hdrext:sdes:mid extension, and didn't declare a=ssrc lines.
// Assumes that the payload type is unique across the media section.
func (pc *PeerConnection) findMediaSectionByPayloadType(
	payloadType PayloadType,
	remoteDescription *SessionDescription,
) (selectedMediaSection *sdp.MediaDescription, ok bool) {
	for i := range remoteDescription.parsed.MediaDescriptions {
		descr := remoteDescription.parsed.MediaDescriptions[i]
		media := descr.MediaName.Media
		if !strings.EqualFold(media, "video") && !strings.EqualFold(media, "audio") {
			continue
		}

		formats := descr.MediaName.Formats
		for _, payloadStr := range formats {
			payload, err := strconv.ParseUint(payloadStr, 10, 8)
			if err != nil {
				continue
			}

			// Return the first media section that has the payload type.
			// Assuming that the payload type is unique across the media section.
			if PayloadType(payload) == payloadType {
				return remoteDescription.parsed.MediaDescriptions[i], true
			}
		}
	}

	return nil, false
}

// Chrome sends probing traffic on SSRC 0. This reads the packets to ensure that we properly
// generate TWCC reports for it. Since this isn't actually media we don't pass this to the user.
func (pc *PeerConnection) handleNonMediaBandwidthProbe() {
	nonMediaBandwidthProbe, err := pc.api.NewRTPReceiver(RTPCodecTypeVideo, pc.dtlsTransport)
	if err != nil {
		pc.log.Errorf("handleNonMediaBandwidthProbe failed to create RTPReceiver: %v", err)

		return
	}

	if err = nonMediaBandwidthProbe.Receive(RTPReceiveParameters{
		Encodings: []RTPDecodingParameters{{RTPCodingParameters: RTPCodingParameters{}}},
	}); err != nil {
		pc.log.Errorf("handleNonMediaBandwidthProbe failed to start RTPReceiver: %v", err)

		return
	}

	pc.nonMediaBandwidthProbe.Store(nonMediaBandwidthProbe)
	b := make([]byte, pc.api.settingEngine.getReceiveMTU())
	for {
		if _, _, err = nonMediaBandwidthProbe.readRTP(b, nonMediaBandwidthProbe.Track()); err != nil {
			pc.log.Tracef("handleNonMediaBandwidthProbe read exiting: %v", err)

			return
		}
	}
}

func (pc *PeerConnection) handleIncomingSSRC(rtpStream io.Reader, ssrc SSRC) error { //nolint:gocyclo,gocognit,cyclop
	remoteDescription := pc.RemoteDescription()
	if remoteDescription == nil {
		return errPeerConnRemoteDescriptionNil
	}

	// If a SSRC already exists in the RemoteDescription don't perform heuristics upon it
	for _, track := range trackDetailsFromSDP(pc.log, remoteDescription.parsed) {
		if track.rtxSsrc != nil && ssrc == *track.rtxSsrc {
			return nil
		}
		if track.fecSsrc != nil && ssrc == *track.fecSsrc {
			return nil
		}
		for _, trackSsrc := range track.ssrcs {
			if ssrc == trackSsrc {
				return nil
			}
		}
	}

	// if the SSRC is not declared in the SDP and there is only one media section,
	// we attempt to resolve it using this single section
	// This applies even if the client supports RTP extensions:
	// (urn:ietf:params:rtp-hdrext:sdes:rtp-stream-id and urn:ietf:params:rtp-hdrext:sdes:mid)
	// and even if the RTP stream contains an incorrect MID or RID.
	// while this can be incorrect, this is done to maintain compatibility with older behavior.
	if len(remoteDescription.parsed.MediaDescriptions) == 1 {
		mediaSection := remoteDescription.parsed.MediaDescriptions[0]
		if handled, err := pc.handleUndeclaredSSRC(ssrc, mediaSection); handled || err != nil {
			return err
		}
	}

	// We read the RTP packet to determine the payload type
	b := make([]byte, pc.api.settingEngine.getReceiveMTU())

	i, err := rtpStream.Read(b)
	if err != nil {
		return err
	}

	if i < 4 {
		return errRTPTooShort
	}

	payloadType := PayloadType(b[1] & 0x7f)
	params, err := pc.api.mediaEngine.getRTPParametersByPayloadType(payloadType)
	if err != nil {
		return err
	}

	midExtensionID, audioSupported, videoSupported := pc.api.mediaEngine.getHeaderExtensionID(
		RTPHeaderExtensionCapability{sdp.SDESMidURI},
	)
	if !audioSupported && !videoSupported {
		// try to find media section by payload type as a last resort for legacy clients.
		mediaSection, ok := pc.findMediaSectionByPayloadType(payloadType, remoteDescription)
		if ok {
			if ok, err = pc.handleUndeclaredSSRC(ssrc, mediaSection); ok || err != nil {
				return err
			}
		}

		return errPeerConnSimulcastMidRTPExtensionRequired
	}

	streamIDExtensionID, audioSupported, videoSupported := pc.api.mediaEngine.getHeaderExtensionID(
		RTPHeaderExtensionCapability{sdp.SDESRTPStreamIDURI},
	)
	if !audioSupported && !videoSupported {
		return errPeerConnSimulcastStreamIDRTPExtensionRequired
	}

	repairStreamIDExtensionID, _, _ := pc.api.mediaEngine.getHeaderExtensionID(
		RTPHeaderExtensionCapability{sdp.SDESRepairRTPStreamIDURI},
	)

	streamInfo := createStreamInfo(
		"",
		ssrc,
		0, 0,
		params.Codecs[0].PayloadType,
		0, 0,
		params.Codecs[0].RTPCodecCapability,
		params.HeaderExtensions,
	)
	readStream, interceptor, rtcpReadStream, rtcpInterceptor, err := pc.dtlsTransport.streamsForSSRC(ssrc, *streamInfo)
	if err != nil {
		return err
	}

	var mid, rid, rsid string
	var paddingOnly bool
	for readCount := 0; readCount <= simulcastProbeCount; readCount++ {
		if mid == "" || (rid == "" && rsid == "") {
			// skip padding only packets for probing
			if paddingOnly {
				readCount--
			}

			i, _, err := interceptor.Read(b, nil)
			if err != nil {
				return err
			}

			if _, paddingOnly, err = handleUnknownRTPPacket(
				b[:i], uint8(midExtensionID), //nolint:gosec // G115
				uint8(streamIDExtensionID),       //nolint:gosec // G115
				uint8(repairStreamIDExtensionID), //nolint:gosec // G115
				&mid,
				&rid,
				&rsid,
			); err != nil {
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

			track, err := receiver.receiveForRid(
				rid,
				params,
				streamInfo,
				readStream,
				interceptor,
				rtcpReadStream,
				rtcpInterceptor,
			)
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

// undeclaredMediaProcessor handles RTP/RTCP packets that don't match any a:ssrc lines.
func (pc *PeerConnection) undeclaredMediaProcessor() {
	go pc.undeclaredRTPMediaProcessor()
	go pc.undeclaredRTCPMediaProcessor()
}

func (pc *PeerConnection) undeclaredRTPMediaProcessor() { //nolint:cyclop
	var simulcastRoutineCount uint64
	for {
		srtpSession, err := pc.dtlsTransport.getSRTPSession()
		if err != nil {
			pc.log.Warnf("undeclaredMediaProcessor failed to open SrtpSession: %v", err)

			return
		}

		srtcpSession, err := pc.dtlsTransport.getSRTCPSession()
		if err != nil {
			pc.log.Warnf("undeclaredMediaProcessor failed to open SrtcpSession: %v", err)

			return
		}

		srtpReadStream, ssrc, err := srtpSession.AcceptStream()
		if err != nil {
			pc.log.Warnf("Failed to accept RTP %v", err)

			return
		}

		// open accompanying srtcp stream
		srtcpReadStream, err := srtcpSession.OpenReadStream(ssrc)
		if err != nil {
			pc.log.Warnf("Failed to open RTCP stream for %d: %v", ssrc, err)

			return
		}

		if pc.isClosed.Load() {
			if err = srtpReadStream.Close(); err != nil {
				pc.log.Warnf("Failed to close RTP stream %v", err)
			}
			if err = srtcpReadStream.Close(); err != nil {
				pc.log.Warnf("Failed to close RTCP stream %v", err)
			}

			continue
		}

		pc.dtlsTransport.storeSimulcastStream(srtpReadStream, srtcpReadStream)

		if ssrc == 0 {
			go pc.handleNonMediaBandwidthProbe()

			continue
		}

		if atomic.AddUint64(&simulcastRoutineCount, 1) >= simulcastMaxProbeRoutines {
			atomic.AddUint64(&simulcastRoutineCount, ^uint64(0))
			pc.log.Warn(ErrSimulcastProbeOverflow.Error())

			continue
		}

		go func(rtpStream io.Reader, ssrc SSRC) {
			if err := pc.handleIncomingSSRC(rtpStream, ssrc); err != nil {
				pc.log.Errorf(incomingUnhandledRTPSsrc, ssrc, err)
			}
			atomic.AddUint64(&simulcastRoutineCount, ^uint64(0))
		}(srtpReadStream, SSRC(ssrc))
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
	remoteDesc := pc.RemoteDescription()
	if remoteDesc == nil {
		return &rtcerr.InvalidStateError{Err: ErrNoRemoteDescription}
	}

	candidateValue := strings.TrimPrefix(candidate.Candidate, "candidate:")

	if candidateValue == "" {
		return pc.iceTransport.AddRemoteCandidate(nil)
	}

	cand, err := ice.UnmarshalCandidate(candidateValue)
	if err != nil {
		if errors.Is(err, ice.ErrUnknownCandidateTyp) || errors.Is(err, ice.ErrDetermineNetworkType) {
			pc.log.Warnf("Discarding remote candidate: %s", err)

			return nil
		}

		return err
	}

	// Reject candidates from old generations.
	// If candidate.usernameFragment is not null,
	// and is not equal to any username fragment present in the corresponding media
	//  description of an applied remote description,
	// return a promise rejected with a newly created OperationError.
	// https://w3c.github.io/webrtc-pc/#dom-peerconnection-addicecandidate
	if ufrag, ok := cand.GetExtension("ufrag"); ok {
		if !pc.descriptionContainsUfrag(remoteDesc.parsed, ufrag.Value) {
			pc.log.Errorf("dropping candidate with ufrag %s because it doesn't match the current ufrags", ufrag.Value)

			return nil
		}
	}

	c, err := newICECandidateFromICE(cand, "", 0)
	if err != nil {
		return err
	}

	return pc.iceTransport.AddRemoteCandidate(&c)
}

// Return true if the sdp contains a specific ufrag.
func (pc *PeerConnection) descriptionContainsUfrag(sdp *sdp.SessionDescription, matchUfrag string) bool {
	ufrag, ok := sdp.Attribute("ice-ufrag")
	if ok && ufrag == matchUfrag {
		return true
	}

	for _, media := range sdp.MediaDescriptions {
		ufrag, ok := media.Attribute("ice-ufrag")
		if ok && ufrag == matchUfrag {
			return true
		}
	}

	return false
}

// ICEConnectionState returns the ICE connection state of the
// PeerConnection instance.
func (pc *PeerConnection) ICEConnectionState() ICEConnectionState {
	if state, ok := pc.iceConnectionState.Load().(ICEConnectionState); ok {
		return state
	}

	return ICEConnectionState(0)
}

// GetSenders returns the RTPSender that are currently attached to this PeerConnection.
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

// GetReceivers returns the RTPReceivers that are currently attached to this PeerConnection.
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

// GetTransceivers returns the RtpTransceiver that are currently attached to this PeerConnection.
func (pc *PeerConnection) GetTransceivers() []*RTPTransceiver {
	pc.mu.Lock()
	defer pc.mu.Unlock()

	return pc.rtpTransceivers
}

// AddTrack adds a Track to the PeerConnection.
//
//nolint:cyclop
func (pc *PeerConnection) AddTrack(track TrackLocal) (*RTPSender, error) {
	if pc.isClosed.Load() {
		return nil, &rtcerr.InvalidStateError{Err: ErrConnectionClosed}
	}

	pc.mu.Lock()
	defer pc.mu.Unlock()
	for _, transceiver := range pc.rtpTransceivers {
		currentDirection := transceiver.getCurrentDirection()
		// According to https://www.w3.org/TR/webrtc/#dom-rtcpeerconnection-addtrack, if the
		// transceiver can be reused only if it's currentDirection never be sendrecv or sendonly.
		// But that will cause sdp inflate. So we only check currentDirection's current value,
		// that's worked for all browsers.
		if transceiver.kind == track.Kind() && transceiver.Sender() == nil &&
			!(currentDirection == RTPTransceiverDirectionSendrecv || currentDirection == RTPTransceiverDirectionSendonly) {
			sender, err := pc.api.NewRTPSender(track, pc.dtlsTransport)
			if err == nil {
				err = transceiver.SetSender(sender, track)
				if err != nil {
					_ = sender.Stop()
					transceiver.setSender(nil)
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

// RemoveTrack removes a Track from the PeerConnection.
func (pc *PeerConnection) RemoveTrack(sender *RTPSender) (err error) {
	if pc.isClosed.Load() {
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

//nolint:cyclop
func (pc *PeerConnection) newTransceiverFromTrack(
	direction RTPTransceiverDirection,
	track TrackLocal,
	init ...RTPTransceiverInit,
) (t *RTPTransceiver, err error) {
	var (
		receiver *RTPReceiver
		sender   *RTPSender
	)
	switch direction {
	case RTPTransceiverDirectionSendrecv:
		receiver, err = pc.api.NewRTPReceiver(track.Kind(), pc.dtlsTransport)
		if err != nil {
			return t, err
		}
		sender, err = pc.api.NewRTPSender(track, pc.dtlsTransport)
	case RTPTransceiverDirectionSendonly:
		sender, err = pc.api.NewRTPSender(track, pc.dtlsTransport)
	default:
		err = errPeerConnAddTransceiverFromTrackSupport
	}
	if err != nil {
		return t, err
	}

	// Allow RTPTransceiverInit to override SSRC
	if sender != nil && len(sender.trackEncodings) == 1 &&
		len(init) == 1 && len(init[0].SendEncodings) == 1 && init[0].SendEncodings[0].SSRC != 0 {
		sender.trackEncodings[0].ssrc = init[0].SendEncodings[0].SSRC
	}

	return newRTPTransceiver(receiver, sender, direction, track.Kind(), pc.api), nil
}

// AddTransceiverFromKind Create a new RtpTransceiver and adds it to the set of transceivers.
//
//nolint:cyclop
func (pc *PeerConnection) AddTransceiverFromKind(
	kind RTPCodecType,
	init ...RTPTransceiverInit,
) (t *RTPTransceiver, err error) {
	if pc.isClosed.Load() {
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
		t, err = pc.newTransceiverFromTrack(direction, track, init...)
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
func (pc *PeerConnection) AddTransceiverFromTrack(
	track TrackLocal,
	init ...RTPTransceiverInit,
) (t *RTPTransceiver, err error) {
	if pc.isClosed.Load() {
		return nil, &rtcerr.InvalidStateError{Err: ErrConnectionClosed}
	}

	direction := RTPTransceiverDirectionSendrecv
	if len(init) > 1 {
		return nil, errPeerConnAddTransceiverFromTrackOnlyAcceptsOne
	} else if len(init) == 1 {
		direction = init[0].Direction
	}

	t, err = pc.newTransceiverFromTrack(direction, track, init...)
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
//
//nolint:cyclop
func (pc *PeerConnection) CreateDataChannel(label string, options *DataChannelInit) (*DataChannel, error) {
	// https://w3c.github.io/webrtc-pc/#peer-to-peer-data-api (Step #2)
	if pc.isClosed.Load() {
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

	if options != nil { //nolint:nestif
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

	dataChannel, err := pc.api.newDataChannel(params, nil, pc.log)
	if err != nil {
		return nil, err
	}

	// https://w3c.github.io/webrtc-pc/#peer-to-peer-data-api (Step #16)
	if dataChannel.maxPacketLifeTime != nil && dataChannel.maxRetransmits != nil {
		return nil, &rtcerr.TypeError{Err: ErrRetransmitsOrPacketLifeTime}
	}

	pc.sctpTransport.lock.Lock()
	pc.sctpTransport.dataChannels = append(pc.sctpTransport.dataChannels, dataChannel)
	if dataChannel.ID() != nil {
		pc.sctpTransport.dataChannelIDsUsed[*dataChannel.ID()] = struct{}{}
	}
	pc.sctpTransport.dataChannelsRequested++
	pc.sctpTransport.lock.Unlock()

	// If SCTP already connected open all the channels
	if pc.sctpTransport.State() == SCTPTransportStateConnected {
		if err = dataChannel.open(pc.sctpTransport); err != nil {
			return nil, err
		}
	}

	pc.mu.Lock()
	pc.onNegotiationNeeded()
	pc.mu.Unlock()

	return dataChannel, nil
}

// SetIdentityProvider is used to configure an identity provider to generate identity assertions.
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

// Close ends the PeerConnection.
func (pc *PeerConnection) Close() error {
	return pc.close(false /* shouldGracefullyClose */)
}

// GracefulClose ends the PeerConnection. It also waits
// for any goroutines it started to complete. This is only safe to call outside of
// PeerConnection callbacks or if in a callback, in its own goroutine.
func (pc *PeerConnection) GracefulClose() error {
	return pc.close(true /* shouldGracefullyClose */)
}

func (pc *PeerConnection) close(shouldGracefullyClose bool) error { //nolint:cyclop
	// https://www.w3.org/TR/webrtc/#dom-rtcpeerconnection-close (step #1)
	// https://www.w3.org/TR/webrtc/#dom-rtcpeerconnection-close (step #2)

	pc.mu.Lock()
	// A lock in this critical section is needed because pc.isClosed and
	// pc.isGracefullyClosingOrClosed are related to each other in that we
	// want to make graceful and normal closure one time operations in order
	// to avoid any double closure errors from cropping up. However, there are
	// some overlapping close cases when both normal and graceful close are used
	// that should be idempotent, but be cautioned when writing new close behavior
	// to preserve this property.
	isAlreadyClosingOrClosed := pc.isClosed.Swap(true)
	isAlreadyGracefullyClosingOrClosed := pc.isGracefullyClosingOrClosed
	if shouldGracefullyClose && !isAlreadyGracefullyClosingOrClosed {
		pc.isGracefullyClosingOrClosed = true
	}
	pc.mu.Unlock()

	if isAlreadyClosingOrClosed {
		if !shouldGracefullyClose {
			return nil
		}
		// Even if we're already closing, it may not be graceful:
		// If we are not the ones doing the closing, we just wait for the graceful close
		// to happen and then return.
		if isAlreadyGracefullyClosingOrClosed {
			<-pc.isGracefulCloseDone

			return nil
		}
		// Otherwise we need to go through the graceful closure flow once the
		// normal closure is done since there are extra steps to take with a
		// graceful close.
		<-pc.isCloseDone
	} else {
		defer close(pc.isCloseDone)
	}

	if shouldGracefullyClose {
		defer close(pc.isGracefulCloseDone)
	}

	// Try closing everything and collect the errors
	// Shutdown strategy:
	// 1. All Conn close by closing their underlying Conn.
	// 2. A Mux stops this chain. It won't close the underlying
	//    Conn if one of the endpoints is closed down. To
	//    continue the chain the Mux has to be closed.
	closeErrs := make([]error, 4)

	doGracefulCloseOps := func() []error {
		if !shouldGracefullyClose {
			return nil
		}

		// these are all non-canon steps
		var gracefulCloseErrors []error
		if pc.iceTransport != nil {
			gracefulCloseErrors = append(gracefulCloseErrors, pc.iceTransport.GracefulStop())
		}

		pc.ops.GracefulClose()

		pc.sctpTransport.lock.Lock()
		for _, d := range pc.sctpTransport.dataChannels {
			gracefulCloseErrors = append(gracefulCloseErrors, d.GracefulClose())
		}
		pc.sctpTransport.lock.Unlock()

		return gracefulCloseErrors
	}

	if isAlreadyClosingOrClosed {
		return util.FlattenErrs(doGracefulCloseOps())
	}

	// https://www.w3.org/TR/webrtc/#dom-rtcpeerconnection-close (step #3)
	pc.signalingState.Set(SignalingStateClosed)

	// https://www.w3.org/TR/webrtc/#dom-rtcpeerconnection-close (step #4)
	pc.mu.Lock()
	for _, t := range pc.rtpTransceivers {
		closeErrs = append(closeErrs, t.Stop()) //nolint:makezero // todo fix
	}
	if nonMediaBandwidthProbe, ok := pc.nonMediaBandwidthProbe.Load().(*RTPReceiver); ok {
		closeErrs = append(closeErrs, nonMediaBandwidthProbe.Stop()) //nolint:makezero // todo fix
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
		closeErrs = append(closeErrs, pc.sctpTransport.Stop()) //nolint:makezero // todo fix
	}

	// https://www.w3.org/TR/webrtc/#dom-rtcpeerconnection-close (step #7)
	closeErrs = append(closeErrs, pc.dtlsTransport.Stop()) //nolint:makezero // todo fix

	// https://www.w3.org/TR/webrtc/#dom-rtcpeerconnection-close (step #8, #9, #10)
	if pc.iceTransport != nil && !shouldGracefullyClose {
		// we will stop gracefully in doGracefulCloseOps
		closeErrs = append(closeErrs, pc.iceTransport.Stop()) //nolint:makezero // todo fix
	}

	// https://www.w3.org/TR/webrtc/#dom-rtcpeerconnection-close (step #11)
	pc.updateConnectionState(pc.ICEConnectionState(), pc.dtlsTransport.State())

	closeErrs = append(closeErrs, doGracefulCloseOps()...) //nolint:makezero // todo fix

	// Interceptor closes at the end to prevent Bind from being called after interceptor is closed
	closeErrs = append(closeErrs, pc.api.interceptor.Close()) //nolint:makezero // todo fix

	return util.FlattenErrs(closeErrs)
}

// addRTPTransceiver appends t into rtpTransceivers
// and fires onNegotiationNeeded;
// caller of this method should hold `pc.mu` lock.
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
	defer pc.mu.Unlock()

	localDescription := pc.currentLocalDescription
	iceGather := pc.iceGatherer
	iceGatheringState := pc.ICEGatheringState()

	return populateLocalCandidates(localDescription, iceGather, iceGatheringState)
}

// PendingLocalDescription represents a local description that is in the
// process of being negotiated plus any local candidates that have been
// generated by the ICEAgent since the offer or answer was created. If the
// PeerConnection is in the stable state, the value is null.
func (pc *PeerConnection) PendingLocalDescription() *SessionDescription {
	pc.mu.Lock()
	defer pc.mu.Unlock()

	localDescription := pc.pendingLocalDescription
	iceGather := pc.iceGatherer
	iceGatheringState := pc.ICEGatheringState()

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

// GetStats return data providing statistics about the overall connection.
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

// Start all transports. PeerConnection now has enough state.
func (pc *PeerConnection) startTransports(
	iceRole ICERole,
	dtlsRole DTLSRole,
	remoteUfrag, remotePwd, fingerprint, fingerprintHash string,
) {
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

	pc.dtlsTransport.internalOnCloseHandler = func() {
		if pc.isClosed.Load() || pc.api.settingEngine.disableCloseByDTLS {
			return
		}

		pc.log.Info("Closing PeerConnection from DTLS CloseNotify")
		go func() {
			if pcClosErr := pc.Close(); pcClosErr != nil {
				pc.log.Warnf("Failed to close PeerConnection from DTLS CloseNotify: %s", pcClosErr)
			}
		}()
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
func (pc *PeerConnection) startRTP(
	isRenegotiation bool,
	remoteDesc *SessionDescription,
	currentTransceivers []*RTPTransceiver,
) {
	if !isRenegotiation {
		pc.undeclaredMediaProcessor()
	}

	pc.startRTPReceivers(remoteDesc, currentTransceivers)
	if d := haveDataChannel(remoteDesc); d != nil {
		pc.startSCTP(getMaxMessageSize(d))
	}
}

// generateUnmatchedSDP generates an SDP that doesn't take remote state into account
// This is used for the initial call for CreateOffer.
//
//nolint:cyclop
func (pc *PeerConnection) generateUnmatchedSDP(
	transceivers []*RTPTransceiver,
	useIdentity bool,
) (*sdp.SessionDescription, error) {
	desc, err := sdp.NewJSEPSessionDescription(useIdentity)
	if err != nil {
		return nil, err
	}
	desc.Attributes = append(desc.Attributes, sdp.Attribute{Key: sdp.AttrKeyMsidSemantic, Value: "WMS *"})

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

	if isPlanB { //nolint:nestif
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

	return populateSDP(
		desc,
		isPlanB,
		dtlsFingerprints,
		pc.api.settingEngine.sdpMediaLevelFingerprints,
		pc.api.settingEngine.candidates.ICELite,
		true,
		pc.api.mediaEngine,
		connectionRoleFromDtlsRole(defaultDtlsRoleOffer),
		candidates,
		iceParams,
		mediaSections,
		pc.ICEGatheringState(),
		nil,
		pc.api.settingEngine.getSCTPMaxMessageSize(),
	)
}

// generateMatchedSDP generates a SDP and takes the remote state into account
// this is used everytime we have a RemoteDescription
//
//nolint:gocognit,gocyclo,cyclop
func (pc *PeerConnection) generateMatchedSDP(
	transceivers []*RTPTransceiver,
	useIdentity, includeUnmatched bool,
	connectionRole sdp.ConnectionRole,
) (*sdp.SessionDescription, error) {
	desc, err := sdp.NewJSEPSessionDescription(useIdentity)
	if err != nil {
		return nil, err
	}
	desc.Attributes = append(desc.Attributes, sdp.Attribute{Key: sdp.AttrKeyMsidSemantic, Value: "WMS *"})

	iceParams, err := pc.iceGatherer.GetLocalParameters()
	if err != nil {
		return nil, err
	}

	candidates, err := pc.iceGatherer.GetLocalCandidates()
	if err != nil {
		return nil, err
	}

	var transceiver *RTPTransceiver
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
		if kind == 0 || direction == RTPTransceiverDirectionUnknown {
			continue
		}

		sdpSemantics := pc.configuration.SDPSemantics

		switch {
		case sdpSemantics == SDPSemanticsPlanB || sdpSemantics == SDPSemanticsUnifiedPlanWithFallback && detectedPlanB:
			if !detectedPlanB {
				return nil, &rtcerr.TypeError{
					Err: fmt.Errorf("%w: Expected PlanB, but RemoteDescription is UnifiedPlan", ErrIncorrectSDPSemantics),
				}
			}
			// If we're responding to a plan-b offer, then we should try to fill up this
			// media entry with all matching local transceivers
			mediaTransceivers := []*RTPTransceiver{}
			for {
				// keep going until we can't get any more
				transceiver, localTransceivers = satisfyTypeAndDirection(kind, direction, localTransceivers)
				if transceiver == nil {
					if len(mediaTransceivers) == 0 {
						transceiver = &RTPTransceiver{kind: kind, api: pc.api, codecs: pc.api.mediaEngine.getCodecsByKind(kind)}
						transceiver.setDirection(RTPTransceiverDirectionInactive)
						mediaTransceivers = append(mediaTransceivers, transceiver)
					}

					break
				}
				if sender := transceiver.Sender(); sender != nil {
					sender.setNegotiated()
				}
				mediaTransceivers = append(mediaTransceivers, transceiver)
			}
			mediaSections = append(mediaSections, mediaSection{id: midValue, transceivers: mediaTransceivers})
		case sdpSemantics == SDPSemanticsUnifiedPlan || sdpSemantics == SDPSemanticsUnifiedPlanWithFallback:
			if detectedPlanB {
				return nil, &rtcerr.TypeError{
					Err: fmt.Errorf(
						"%w: Expected UnifiedPlan, but RemoteDescription is PlanB",
						ErrIncorrectSDPSemantics,
					),
				}
			}
			transceiver, localTransceivers = findByMid(midValue, localTransceivers)
			if transceiver == nil {
				return nil, fmt.Errorf("%w: %q", errPeerConnTranscieverMidNil, midValue)
			}
			if sender := transceiver.Sender(); sender != nil {
				sender.setNegotiated()
			}
			mediaTransceivers := []*RTPTransceiver{transceiver}

			extensions, _ := rtpExtensionsFromMediaDescription(media)
			mediaSections = append(
				mediaSections,
				mediaSection{id: midValue, transceivers: mediaTransceivers, matchExtensions: extensions, rids: getRids(media)},
			)
		}
	}

	pc.sctpTransport.lock.Lock()
	defer pc.sctpTransport.lock.Unlock()

	var bundleGroup *string
	// If we are offering also include unmatched local transceivers
	if includeUnmatched { //nolint:nestif
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
	} else if remoteDescription != nil {
		groupValue, _ := remoteDescription.parsed.Attribute(sdp.AttrKeyGroup)
		groupValue = strings.TrimLeft(groupValue, "BUNDLE")
		bundleGroup = &groupValue
	}

	if pc.configuration.SDPSemantics == SDPSemanticsUnifiedPlanWithFallback && detectedPlanB {
		pc.log.Info("Plan-B Offer detected; responding with Plan-B Answer")
	}

	dtlsFingerprints, err := pc.configuration.Certificates[0].GetFingerprints()
	if err != nil {
		return nil, err
	}

	return populateSDP(
		desc,
		detectedPlanB,
		dtlsFingerprints,
		pc.api.settingEngine.sdpMediaLevelFingerprints,
		pc.api.settingEngine.candidates.ICELite,
		isExtmapAllowMixed,
		pc.api.mediaEngine,
		connectionRole,
		candidates,
		iceParams,
		mediaSections,
		pc.ICEGatheringState(),
		bundleGroup,
		pc.api.settingEngine.getSCTPMaxMessageSize(),
	)
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
