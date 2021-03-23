// +build js,wasm

// Package webrtc implements the WebRTC 1.0 as defined in W3C WebRTC specification document.
package webrtc

import (
	"syscall/js"

	"github.com/pion/ice/v2"
	"github.com/pion/webrtc/v3/pkg/rtcerr"
)

// PeerConnection represents a WebRTC connection that establishes a
// peer-to-peer communications with another PeerConnection instance in a
// browser, or to another endpoint implementing the required protocols.
type PeerConnection struct {
	// Pointer to the underlying JavaScript RTCPeerConnection object.
	underlying js.Value

	// Keep track of handlers/callbacks so we can call Release as required by the
	// syscall/js API. Initially nil.
	onSignalingStateChangeHandler     *js.Func
	onDataChannelHandler              *js.Func
	onNegotiationNeededHandler        *js.Func
	onConnectionStateChangeHandler    *js.Func
	onICEConnectionStateChangeHandler *js.Func
	onICECandidateHandler             *js.Func
	onICEGatheringStateChangeHandler  *js.Func

	// Used by GatheringCompletePromise
	onGatherCompleteHandler func()

	// A reference to the associated API state used by this connection
	api *API
}

// NewPeerConnection creates a peerconnection.
func NewPeerConnection(configuration Configuration) (*PeerConnection, error) {
	api := NewAPI()
	return api.NewPeerConnection(configuration)
}

// NewPeerConnection creates a new PeerConnection with the provided configuration against the received API object
func (api *API) NewPeerConnection(configuration Configuration) (_ *PeerConnection, err error) {
	defer func() {
		if e := recover(); e != nil {
			err = recoveryToError(e)
		}
	}()
	configMap := configurationToValue(configuration)
	underlying := js.Global().Get("window").Get("RTCPeerConnection").New(configMap)
	return &PeerConnection{
		underlying: underlying,
		api:        api,
	}, nil
}

func (pc *PeerConnection) JSValue() js.Value {
	return pc.underlying
}

// OnSignalingStateChange sets an event handler which is invoked when the
// peer connection's signaling state changes
func (pc *PeerConnection) OnSignalingStateChange(f func(SignalingState)) {
	if pc.onSignalingStateChangeHandler != nil {
		oldHandler := pc.onSignalingStateChangeHandler
		defer oldHandler.Release()
	}
	onSignalingStateChangeHandler := js.FuncOf(func(this js.Value, args []js.Value) interface{} {
		state := newSignalingState(args[0].String())
		go f(state)
		return js.Undefined()
	})
	pc.onSignalingStateChangeHandler = &onSignalingStateChangeHandler
	pc.underlying.Set("onsignalingstatechange", onSignalingStateChangeHandler)
}

// OnDataChannel sets an event handler which is invoked when a data
// channel message arrives from a remote peer.
func (pc *PeerConnection) OnDataChannel(f func(*DataChannel)) {
	if pc.onDataChannelHandler != nil {
		oldHandler := pc.onDataChannelHandler
		defer oldHandler.Release()
	}
	onDataChannelHandler := js.FuncOf(func(this js.Value, args []js.Value) interface{} {
		// pion/webrtc/projects/15
		// This reference to the underlying DataChannel doesn't know
		// about any other references to the same DataChannel. This might result in
		// memory leaks where we don't clean up handler functions. Could possibly fix
		// by keeping a mutex-protected list of all DataChannel references as a
		// property of this PeerConnection, but at the cost of additional overhead.
		dataChannel := &DataChannel{
			underlying: args[0].Get("channel"),
			api:        pc.api,
		}
		go f(dataChannel)
		return js.Undefined()
	})
	pc.onDataChannelHandler = &onDataChannelHandler
	pc.underlying.Set("ondatachannel", onDataChannelHandler)
}

// OnNegotiationNeeded sets an event handler which is invoked when
// a change has occurred which requires session negotiation
func (pc *PeerConnection) OnNegotiationNeeded(f func()) {
	if pc.onNegotiationNeededHandler != nil {
		oldHandler := pc.onNegotiationNeededHandler
		defer oldHandler.Release()
	}
	onNegotiationNeededHandler := js.FuncOf(func(this js.Value, args []js.Value) interface{} {
		go f()
		return js.Undefined()
	})
	pc.onNegotiationNeededHandler = &onNegotiationNeededHandler
	pc.underlying.Set("onnegotiationneeded", onNegotiationNeededHandler)
}

// OnICEConnectionStateChange sets an event handler which is called
// when an ICE connection state is changed.
func (pc *PeerConnection) OnICEConnectionStateChange(f func(ICEConnectionState)) {
	if pc.onICEConnectionStateChangeHandler != nil {
		oldHandler := pc.onICEConnectionStateChangeHandler
		defer oldHandler.Release()
	}
	onICEConnectionStateChangeHandler := js.FuncOf(func(this js.Value, args []js.Value) interface{} {
		connectionState := NewICEConnectionState(pc.underlying.Get("iceConnectionState").String())
		go f(connectionState)
		return js.Undefined()
	})
	pc.onICEConnectionStateChangeHandler = &onICEConnectionStateChangeHandler
	pc.underlying.Set("oniceconnectionstatechange", onICEConnectionStateChangeHandler)
}

// OnConnectionStateChange sets an event handler which is called
// when an PeerConnectionState is changed.
func (pc *PeerConnection) OnConnectionStateChange(f func(PeerConnectionState)) {
	if pc.onConnectionStateChangeHandler != nil {
		oldHandler := pc.onConnectionStateChangeHandler
		defer oldHandler.Release()
	}
	onConnectionStateChangeHandler := js.FuncOf(func(this js.Value, args []js.Value) interface{} {
		connectionState := newPeerConnectionState(pc.underlying.Get("connectionState").String())
		go f(connectionState)
		return js.Undefined()
	})
	pc.onConnectionStateChangeHandler = &onConnectionStateChangeHandler
	pc.underlying.Set("onconnectionstatechange", onConnectionStateChangeHandler)
}

func (pc *PeerConnection) checkConfiguration(configuration Configuration) error {
	// https://www.w3.org/TR/webrtc/#dom-rtcpeerconnection-setconfiguration (step #2)
	if pc.ConnectionState() == PeerConnectionStateClosed {
		return &rtcerr.InvalidStateError{Err: ErrConnectionClosed}
	}

	existingConfig := pc.GetConfiguration()
	// https://www.w3.org/TR/webrtc/#set-the-configuration (step #3)
	if configuration.PeerIdentity != "" {
		if configuration.PeerIdentity != existingConfig.PeerIdentity {
			return &rtcerr.InvalidModificationError{Err: ErrModifyingPeerIdentity}
		}
	}

	// https://github.com/pion/webrtc/issues/513
	// https://www.w3.org/TR/webrtc/#set-the-configuration (step #4)
	// if len(configuration.Certificates) > 0 {
	// 	if len(configuration.Certificates) != len(existingConfiguration.Certificates) {
	// 		return &rtcerr.InvalidModificationError{Err: ErrModifyingCertificates}
	// 	}

	// 	for i, certificate := range configuration.Certificates {
	// 		if !pc.configuration.Certificates[i].Equals(certificate) {
	// 			return &rtcerr.InvalidModificationError{Err: ErrModifyingCertificates}
	// 		}
	// 	}
	// 	pc.configuration.Certificates = configuration.Certificates
	// }

	// https://www.w3.org/TR/webrtc/#set-the-configuration (step #5)
	if configuration.BundlePolicy != BundlePolicy(Unknown) {
		if configuration.BundlePolicy != existingConfig.BundlePolicy {
			return &rtcerr.InvalidModificationError{Err: ErrModifyingBundlePolicy}
		}
	}

	// https://www.w3.org/TR/webrtc/#set-the-configuration (step #6)
	if configuration.RTCPMuxPolicy != RTCPMuxPolicy(Unknown) {
		if configuration.RTCPMuxPolicy != existingConfig.RTCPMuxPolicy {
			return &rtcerr.InvalidModificationError{Err: ErrModifyingRTCPMuxPolicy}
		}
	}

	// https://www.w3.org/TR/webrtc/#set-the-configuration (step #7)
	if configuration.ICECandidatePoolSize != 0 {
		if configuration.ICECandidatePoolSize != existingConfig.ICECandidatePoolSize &&
			pc.LocalDescription() != nil {
			return &rtcerr.InvalidModificationError{Err: ErrModifyingICECandidatePoolSize}
		}
	}

	// https://www.w3.org/TR/webrtc/#set-the-configuration (step #11)
	if len(configuration.ICEServers) > 0 {
		// https://www.w3.org/TR/webrtc/#set-the-configuration (step #11.3)
		for _, server := range configuration.ICEServers {
			if _, err := server.validate(); err != nil {
				return err
			}
		}
	}
	return nil
}

// SetConfiguration updates the configuration of this PeerConnection object.
func (pc *PeerConnection) SetConfiguration(configuration Configuration) (err error) {
	defer func() {
		if e := recover(); e != nil {
			err = recoveryToError(e)
		}
	}()
	if err := pc.checkConfiguration(configuration); err != nil {
		return err
	}
	configMap := configurationToValue(configuration)
	pc.underlying.Call("setConfiguration", configMap)
	return nil
}

// GetConfiguration returns a Configuration object representing the current
// configuration of this PeerConnection object. The returned object is a
// copy and direct mutation on it will not take affect until SetConfiguration
// has been called with Configuration passed as its only argument.
// https://www.w3.org/TR/webrtc/#dom-rtcpeerconnection-getconfiguration
func (pc *PeerConnection) GetConfiguration() Configuration {
	return valueToConfiguration(pc.underlying.Call("getConfiguration"))
}

// CreateOffer starts the PeerConnection and generates the localDescription
func (pc *PeerConnection) CreateOffer(options *OfferOptions) (_ SessionDescription, err error) {
	defer func() {
		if e := recover(); e != nil {
			err = recoveryToError(e)
		}
	}()
	promise := pc.underlying.Call("createOffer", offerOptionsToValue(options))
	desc, err := awaitPromise(promise)
	if err != nil {
		return SessionDescription{}, err
	}
	return *valueToSessionDescription(desc), nil
}

// CreateAnswer starts the PeerConnection and generates the localDescription
func (pc *PeerConnection) CreateAnswer(options *AnswerOptions) (_ SessionDescription, err error) {
	defer func() {
		if e := recover(); e != nil {
			err = recoveryToError(e)
		}
	}()
	promise := pc.underlying.Call("createAnswer", answerOptionsToValue(options))
	desc, err := awaitPromise(promise)
	if err != nil {
		return SessionDescription{}, err
	}
	return *valueToSessionDescription(desc), nil
}

// SetLocalDescription sets the SessionDescription of the local peer
func (pc *PeerConnection) SetLocalDescription(desc SessionDescription) (err error) {
	defer func() {
		if e := recover(); e != nil {
			err = recoveryToError(e)
		}
	}()
	promise := pc.underlying.Call("setLocalDescription", sessionDescriptionToValue(&desc))
	_, err = awaitPromise(promise)
	return err
}

// LocalDescription returns PendingLocalDescription if it is not null and
// otherwise it returns CurrentLocalDescription. This property is used to
// determine if setLocalDescription has already been called.
// https://www.w3.org/TR/webrtc/#dom-rtcpeerconnection-localdescription
func (pc *PeerConnection) LocalDescription() *SessionDescription {
	return valueToSessionDescription(pc.underlying.Get("localDescription"))
}

// SetRemoteDescription sets the SessionDescription of the remote peer
func (pc *PeerConnection) SetRemoteDescription(desc SessionDescription) (err error) {
	defer func() {
		if e := recover(); e != nil {
			err = recoveryToError(e)
		}
	}()
	promise := pc.underlying.Call("setRemoteDescription", sessionDescriptionToValue(&desc))
	_, err = awaitPromise(promise)
	return err
}

// RemoteDescription returns PendingRemoteDescription if it is not null and
// otherwise it returns CurrentRemoteDescription. This property is used to
// determine if setRemoteDescription has already been called.
// https://www.w3.org/TR/webrtc/#dom-rtcpeerconnection-remotedescription
func (pc *PeerConnection) RemoteDescription() *SessionDescription {
	return valueToSessionDescription(pc.underlying.Get("remoteDescription"))
}

// AddICECandidate accepts an ICE candidate string and adds it
// to the existing set of candidates
func (pc *PeerConnection) AddICECandidate(candidate ICECandidateInit) (err error) {
	defer func() {
		if e := recover(); e != nil {
			err = recoveryToError(e)
		}
	}()
	promise := pc.underlying.Call("addIceCandidate", iceCandidateInitToValue(candidate))
	_, err = awaitPromise(promise)
	return err
}

// ICEConnectionState returns the ICE connection state of the
// PeerConnection instance.
func (pc *PeerConnection) ICEConnectionState() ICEConnectionState {
	return NewICEConnectionState(pc.underlying.Get("iceConnectionState").String())
}

// OnICECandidate sets an event handler which is invoked when a new ICE
// candidate is found.
func (pc *PeerConnection) OnICECandidate(f func(candidate *ICECandidate)) {
	if pc.onICECandidateHandler != nil {
		oldHandler := pc.onICECandidateHandler
		defer oldHandler.Release()
	}
	onICECandidateHandler := js.FuncOf(func(this js.Value, args []js.Value) interface{} {
		candidate := valueToICECandidate(args[0].Get("candidate"))
		if candidate == nil && pc.onGatherCompleteHandler != nil {
			go pc.onGatherCompleteHandler()
		}

		go f(candidate)
		return js.Undefined()
	})
	pc.onICECandidateHandler = &onICECandidateHandler
	pc.underlying.Set("onicecandidate", onICECandidateHandler)
}

// OnICEGatheringStateChange sets an event handler which is invoked when the
// ICE candidate gathering state has changed.
func (pc *PeerConnection) OnICEGatheringStateChange(f func()) {
	if pc.onICEGatheringStateChangeHandler != nil {
		oldHandler := pc.onICEGatheringStateChangeHandler
		defer oldHandler.Release()
	}
	onICEGatheringStateChangeHandler := js.FuncOf(func(this js.Value, args []js.Value) interface{} {
		go f()
		return js.Undefined()
	})
	pc.onICEGatheringStateChangeHandler = &onICEGatheringStateChangeHandler
	pc.underlying.Set("onicegatheringstatechange", onICEGatheringStateChangeHandler)
}

// CreateDataChannel creates a new DataChannel object with the given label
// and optional DataChannelInit used to configure properties of the
// underlying channel such as data reliability.
func (pc *PeerConnection) CreateDataChannel(label string, options *DataChannelInit) (_ *DataChannel, err error) {
	defer func() {
		if e := recover(); e != nil {
			err = recoveryToError(e)
		}
	}()
	channel := pc.underlying.Call("createDataChannel", label, dataChannelInitToValue(options))
	return &DataChannel{
		underlying: channel,
		api:        pc.api,
	}, nil
}

// SetIdentityProvider is used to configure an identity provider to generate identity assertions
func (pc *PeerConnection) SetIdentityProvider(provider string) (err error) {
	defer func() {
		if e := recover(); e != nil {
			err = recoveryToError(e)
		}
	}()
	pc.underlying.Call("setIdentityProvider", provider)
	return nil
}

// Close ends the PeerConnection
func (pc *PeerConnection) Close() (err error) {
	defer func() {
		if e := recover(); e != nil {
			err = recoveryToError(e)
		}
	}()

	pc.underlying.Call("close")

	// Release any handlers as required by the syscall/js API.
	if pc.onSignalingStateChangeHandler != nil {
		pc.onSignalingStateChangeHandler.Release()
	}
	if pc.onDataChannelHandler != nil {
		pc.onDataChannelHandler.Release()
	}
	if pc.onNegotiationNeededHandler != nil {
		pc.onNegotiationNeededHandler.Release()
	}
	if pc.onConnectionStateChangeHandler != nil {
		pc.onConnectionStateChangeHandler.Release()
	}
	if pc.onICEConnectionStateChangeHandler != nil {
		pc.onICEConnectionStateChangeHandler.Release()
	}
	if pc.onICECandidateHandler != nil {
		pc.onICECandidateHandler.Release()
	}
	if pc.onICEGatheringStateChangeHandler != nil {
		pc.onICEGatheringStateChangeHandler.Release()
	}

	return nil
}

// CurrentLocalDescription represents the local description that was
// successfully negotiated the last time the PeerConnection transitioned
// into the stable state plus any local candidates that have been generated
// by the ICEAgent since the offer or answer was created.
func (pc *PeerConnection) CurrentLocalDescription() *SessionDescription {
	desc := pc.underlying.Get("currentLocalDescription")
	return valueToSessionDescription(desc)
}

// PendingLocalDescription represents a local description that is in the
// process of being negotiated plus any local candidates that have been
// generated by the ICEAgent since the offer or answer was created. If the
// PeerConnection is in the stable state, the value is null.
func (pc *PeerConnection) PendingLocalDescription() *SessionDescription {
	desc := pc.underlying.Get("pendingLocalDescription")
	return valueToSessionDescription(desc)
}

// CurrentRemoteDescription represents the last remote description that was
// successfully negotiated the last time the PeerConnection transitioned
// into the stable state plus any remote candidates that have been supplied
// via AddICECandidate() since the offer or answer was created.
func (pc *PeerConnection) CurrentRemoteDescription() *SessionDescription {
	desc := pc.underlying.Get("currentRemoteDescription")
	return valueToSessionDescription(desc)
}

// PendingRemoteDescription represents a remote description that is in the
// process of being negotiated, complete with any remote candidates that
// have been supplied via AddICECandidate() since the offer or answer was
// created. If the PeerConnection is in the stable state, the value is
// null.
func (pc *PeerConnection) PendingRemoteDescription() *SessionDescription {
	desc := pc.underlying.Get("pendingRemoteDescription")
	return valueToSessionDescription(desc)
}

// SignalingState returns the signaling state of the PeerConnection instance.
func (pc *PeerConnection) SignalingState() SignalingState {
	rawState := pc.underlying.Get("signalingState").String()
	return newSignalingState(rawState)
}

// ICEGatheringState attribute the ICE gathering state of the PeerConnection
// instance.
func (pc *PeerConnection) ICEGatheringState() ICEGatheringState {
	rawState := pc.underlying.Get("iceGatheringState").String()
	return NewICEGatheringState(rawState)
}

// ConnectionState attribute the connection state of the PeerConnection
// instance.
func (pc *PeerConnection) ConnectionState() PeerConnectionState {
	rawState := pc.underlying.Get("connectionState").String()
	return newPeerConnectionState(rawState)
}

func (pc *PeerConnection) setGatherCompleteHandler(handler func()) {
	pc.onGatherCompleteHandler = handler

	// If no onIceCandidate handler has been set provide an empty one
	// otherwise our onGatherCompleteHandler will not be executed
	if pc.onICECandidateHandler == nil {
		pc.OnICECandidate(func(i *ICECandidate) {})
	}
}

// Converts a Configuration to js.Value so it can be passed
// through to the JavaScript WebRTC API. Any zero values are converted to
// js.Undefined(), which will result in the default value being used.
func configurationToValue(configuration Configuration) js.Value {
	return js.ValueOf(map[string]interface{}{
		"iceServers":           iceServersToValue(configuration.ICEServers),
		"iceTransportPolicy":   stringEnumToValueOrUndefined(configuration.ICETransportPolicy.String()),
		"bundlePolicy":         stringEnumToValueOrUndefined(configuration.BundlePolicy.String()),
		"rtcpMuxPolicy":        stringEnumToValueOrUndefined(configuration.RTCPMuxPolicy.String()),
		"peerIdentity":         stringToValueOrUndefined(configuration.PeerIdentity),
		"iceCandidatePoolSize": uint8ToValueOrUndefined(configuration.ICECandidatePoolSize),

		// Note: Certificates are not currently supported.
		// "certificates": configuration.Certificates,
	})
}

func iceServersToValue(iceServers []ICEServer) js.Value {
	if len(iceServers) == 0 {
		return js.Undefined()
	}
	maps := make([]interface{}, len(iceServers))
	for i, server := range iceServers {
		maps[i] = iceServerToValue(server)
	}
	return js.ValueOf(maps)
}

func iceServerToValue(server ICEServer) js.Value {
	return js.ValueOf(map[string]interface{}{
		"urls":     stringsToValue(server.URLs), // required
		"username": stringToValueOrUndefined(server.Username),
		// Note: credential and credentialType are not currently supported.
		// "credential":     interfaceToValueOrUndefined(server.Credential),
		// "credentialType": stringEnumToValueOrUndefined(server.CredentialType.String()),
	})
}

func valueToConfiguration(configValue js.Value) Configuration {
	if jsValueIsNull(configValue) || jsValueIsUndefined(configValue) {
		return Configuration{}
	}
	return Configuration{
		ICEServers:           valueToICEServers(configValue.Get("iceServers")),
		ICETransportPolicy:   NewICETransportPolicy(valueToStringOrZero(configValue.Get("iceTransportPolicy"))),
		BundlePolicy:         newBundlePolicy(valueToStringOrZero(configValue.Get("bundlePolicy"))),
		RTCPMuxPolicy:        newRTCPMuxPolicy(valueToStringOrZero(configValue.Get("rtcpMuxPolicy"))),
		PeerIdentity:         valueToStringOrZero(configValue.Get("peerIdentity")),
		ICECandidatePoolSize: valueToUint8OrZero(configValue.Get("iceCandidatePoolSize")),

		// Note: Certificates are not supported.
		// Certificates []Certificate
	}
}

func valueToICEServers(iceServersValue js.Value) []ICEServer {
	if jsValueIsNull(iceServersValue) || jsValueIsUndefined(iceServersValue) {
		return nil
	}
	iceServers := make([]ICEServer, iceServersValue.Length())
	for i := 0; i < iceServersValue.Length(); i++ {
		iceServers[i] = valueToICEServer(iceServersValue.Index(i))
	}
	return iceServers
}

func valueToICEServer(iceServerValue js.Value) ICEServer {
	return ICEServer{
		URLs:     valueToStrings(iceServerValue.Get("urls")), // required
		Username: valueToStringOrZero(iceServerValue.Get("username")),
		// Note: Credential and CredentialType are not currently supported.
		// Credential: iceServerValue.Get("credential"),
		// CredentialType: newICECredentialType(valueToStringOrZero(iceServerValue.Get("credentialType"))),
	}
}

func valueToICECandidate(val js.Value) *ICECandidate {
	if jsValueIsNull(val) || jsValueIsUndefined(val) {
		return nil
	}
	if jsValueIsUndefined(val.Get("protocol")) && !jsValueIsUndefined(val.Get("candidate")) {
		// Missing some fields, assume it's Firefox and parse SDP candidate.
		c, err := ice.UnmarshalCandidate(val.Get("candidate").String())
		if err != nil {
			return nil
		}

		iceCandidate, err := newICECandidateFromICE(c)
		if err != nil {
			return nil
		}

		return &iceCandidate
	}
	protocol, _ := NewICEProtocol(val.Get("protocol").String())
	candidateType, _ := NewICECandidateType(val.Get("type").String())
	return &ICECandidate{
		Foundation:     val.Get("foundation").String(),
		Priority:       valueToUint32OrZero(val.Get("priority")),
		Address:        val.Get("address").String(),
		Protocol:       protocol,
		Port:           valueToUint16OrZero(val.Get("port")),
		Typ:            candidateType,
		Component:      stringToComponentIDOrZero(val.Get("component").String()),
		RelatedAddress: val.Get("relatedAddress").String(),
		RelatedPort:    valueToUint16OrZero(val.Get("relatedPort")),
	}
}

func stringToComponentIDOrZero(val string) uint16 {
	// See: https://developer.mozilla.org/en-US/docs/Web/API/RTCIceComponent
	switch val {
	case "rtp":
		return 1
	case "rtcp":
		return 2
	}
	return 0
}

func sessionDescriptionToValue(desc *SessionDescription) js.Value {
	if desc == nil {
		return js.Undefined()
	}
	return js.ValueOf(map[string]interface{}{
		"type": desc.Type.String(),
		"sdp":  desc.SDP,
	})
}

func valueToSessionDescription(descValue js.Value) *SessionDescription {
	if jsValueIsNull(descValue) || jsValueIsUndefined(descValue) {
		return nil
	}
	return &SessionDescription{
		Type: NewSDPType(descValue.Get("type").String()),
		SDP:  descValue.Get("sdp").String(),
	}
}

func offerOptionsToValue(offerOptions *OfferOptions) js.Value {
	if offerOptions == nil {
		return js.Undefined()
	}
	return js.ValueOf(map[string]interface{}{
		"iceRestart":             offerOptions.ICERestart,
		"voiceActivityDetection": offerOptions.VoiceActivityDetection,
	})
}

func answerOptionsToValue(answerOptions *AnswerOptions) js.Value {
	if answerOptions == nil {
		return js.Undefined()
	}
	return js.ValueOf(map[string]interface{}{
		"voiceActivityDetection": answerOptions.VoiceActivityDetection,
	})
}

func iceCandidateInitToValue(candidate ICECandidateInit) js.Value {
	return js.ValueOf(map[string]interface{}{
		"candidate":        candidate.Candidate,
		"sdpMid":           stringPointerToValue(candidate.SDPMid),
		"sdpMLineIndex":    uint16PointerToValue(candidate.SDPMLineIndex),
		"usernameFragment": stringPointerToValue(candidate.UsernameFragment),
	})
}

func dataChannelInitToValue(options *DataChannelInit) js.Value {
	if options == nil {
		return js.Undefined()
	}

	maxPacketLifeTime := uint16PointerToValue(options.MaxPacketLifeTime)
	return js.ValueOf(map[string]interface{}{
		"ordered":           boolPointerToValue(options.Ordered),
		"maxPacketLifeTime": maxPacketLifeTime,
		// See https://bugs.chromium.org/p/chromium/issues/detail?id=696681
		// Chrome calls this "maxRetransmitTime"
		"maxRetransmitTime": maxPacketLifeTime,
		"maxRetransmits":    uint16PointerToValue(options.MaxRetransmits),
		"protocol":          stringPointerToValue(options.Protocol),
		"negotiated":        boolPointerToValue(options.Negotiated),
		"id":                uint16PointerToValue(options.ID),
	})
}
