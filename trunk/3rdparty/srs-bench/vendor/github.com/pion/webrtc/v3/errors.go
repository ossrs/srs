package webrtc

import (
	"errors"
)

var (
	// ErrUnknownType indicates an error with Unknown info.
	ErrUnknownType = errors.New("unknown")

	// ErrConnectionClosed indicates an operation executed after connection
	// has already been closed.
	ErrConnectionClosed = errors.New("connection closed")

	// ErrDataChannelNotOpen indicates an operation executed when the data
	// channel is not (yet) open.
	ErrDataChannelNotOpen = errors.New("data channel not open")

	// ErrCertificateExpired indicates that an x509 certificate has expired.
	ErrCertificateExpired = errors.New("x509Cert expired")

	// ErrNoTurnCredentials indicates that a TURN server URL was provided
	// without required credentials.
	ErrNoTurnCredentials = errors.New("turn server credentials required")

	// ErrTurnCredentials indicates that provided TURN credentials are partial
	// or malformed.
	ErrTurnCredentials = errors.New("invalid turn server credentials")

	// ErrExistingTrack indicates that a track already exists.
	ErrExistingTrack = errors.New("track already exists")

	// ErrPrivateKeyType indicates that a particular private key encryption
	// chosen to generate a certificate is not supported.
	ErrPrivateKeyType = errors.New("private key type not supported")

	// ErrModifyingPeerIdentity indicates that an attempt to modify
	// PeerIdentity was made after PeerConnection has been initialized.
	ErrModifyingPeerIdentity = errors.New("peerIdentity cannot be modified")

	// ErrModifyingCertificates indicates that an attempt to modify
	// Certificates was made after PeerConnection has been initialized.
	ErrModifyingCertificates = errors.New("certificates cannot be modified")

	// ErrModifyingBundlePolicy indicates that an attempt to modify
	// BundlePolicy was made after PeerConnection has been initialized.
	ErrModifyingBundlePolicy = errors.New("bundle policy cannot be modified")

	// ErrModifyingRTCPMuxPolicy indicates that an attempt to modify
	// RTCPMuxPolicy was made after PeerConnection has been initialized.
	ErrModifyingRTCPMuxPolicy = errors.New("rtcp mux policy cannot be modified")

	// ErrModifyingICECandidatePoolSize indicates that an attempt to modify
	// ICECandidatePoolSize was made after PeerConnection has been initialized.
	ErrModifyingICECandidatePoolSize = errors.New("ice candidate pool size cannot be modified")

	// ErrStringSizeLimit indicates that the character size limit of string is
	// exceeded. The limit is hardcoded to 65535 according to specifications.
	ErrStringSizeLimit = errors.New("data channel label exceeds size limit")

	// ErrMaxDataChannelID indicates that the maximum number ID that could be
	// specified for a data channel has been exceeded.
	ErrMaxDataChannelID = errors.New("maximum number ID for datachannel specified")

	// ErrNegotiatedWithoutID indicates that an attempt to create a data channel
	// was made while setting the negotiated option to true without providing
	// the negotiated channel ID.
	ErrNegotiatedWithoutID = errors.New("negotiated set without channel id")

	// ErrRetransmitsOrPacketLifeTime indicates that an attempt to create a data
	// channel was made with both options MaxPacketLifeTime and MaxRetransmits
	// set together. Such configuration is not supported by the specification
	// and is mutually exclusive.
	ErrRetransmitsOrPacketLifeTime = errors.New("both MaxPacketLifeTime and MaxRetransmits was set")

	// ErrCodecNotFound is returned when a codec search to the Media Engine fails
	ErrCodecNotFound = errors.New("codec not found")

	// ErrNoRemoteDescription indicates that an operation was rejected because
	// the remote description is not set
	ErrNoRemoteDescription = errors.New("remote description is not set")

	// ErrIncorrectSDPSemantics indicates that the PeerConnection was configured to
	// generate SDP Answers with different SDP Semantics than the received Offer
	ErrIncorrectSDPSemantics = errors.New("offer SDP semantics does not match configuration")

	// ErrIncorrectSignalingState indicates that the signaling state of PeerConnection is not correct
	ErrIncorrectSignalingState = errors.New("operation can not be run in current signaling state")

	// ErrProtocolTooLarge indicates that value given for a DataChannelInit protocol is
	// longer then 65535 bytes
	ErrProtocolTooLarge = errors.New("protocol is larger then 65535 bytes")

	// ErrSenderNotCreatedByConnection indicates RemoveTrack was called with a RtpSender not created
	// by this PeerConnection
	ErrSenderNotCreatedByConnection = errors.New("RtpSender not created by this PeerConnection")

	// ErrSessionDescriptionNoFingerprint indicates SetRemoteDescription was called with a SessionDescription that has no
	// fingerprint
	ErrSessionDescriptionNoFingerprint = errors.New("SetRemoteDescription called with no fingerprint")

	// ErrSessionDescriptionInvalidFingerprint indicates SetRemoteDescription was called with a SessionDescription that
	// has an invalid fingerprint
	ErrSessionDescriptionInvalidFingerprint = errors.New("SetRemoteDescription called with an invalid fingerprint")

	// ErrSessionDescriptionConflictingFingerprints indicates SetRemoteDescription was called with a SessionDescription that
	// has an conflicting fingerprints
	ErrSessionDescriptionConflictingFingerprints = errors.New("SetRemoteDescription called with multiple conflicting fingerprint")

	// ErrSessionDescriptionMissingIceUfrag indicates SetRemoteDescription was called with a SessionDescription that
	// is missing an ice-ufrag value
	ErrSessionDescriptionMissingIceUfrag = errors.New("SetRemoteDescription called with no ice-ufrag")

	// ErrSessionDescriptionMissingIcePwd indicates SetRemoteDescription was called with a SessionDescription that
	// is missing an ice-pwd value
	ErrSessionDescriptionMissingIcePwd = errors.New("SetRemoteDescription called with no ice-pwd")

	// ErrSessionDescriptionConflictingIceUfrag  indicates SetRemoteDescription was called with a SessionDescription that
	// contains multiple conflicting ice-ufrag values
	ErrSessionDescriptionConflictingIceUfrag = errors.New("SetRemoteDescription called with multiple conflicting ice-ufrag values")

	// ErrSessionDescriptionConflictingIcePwd indicates SetRemoteDescription was called with a SessionDescription that
	// contains multiple conflicting ice-pwd values
	ErrSessionDescriptionConflictingIcePwd = errors.New("SetRemoteDescription called with multiple conflicting ice-pwd values")

	// ErrNoSRTPProtectionProfile indicates that the DTLS handshake completed and no SRTP Protection Profile was chosen
	ErrNoSRTPProtectionProfile = errors.New("DTLS Handshake completed and no SRTP Protection Profile was chosen")

	// ErrFailedToGenerateCertificateFingerprint indicates that we failed to generate the fingerprint used for comparing certificates
	ErrFailedToGenerateCertificateFingerprint = errors.New("failed to generate certificate fingerprint")

	// ErrNoCodecsAvailable indicates that operation isn't possible because the MediaEngine has no codecs available
	ErrNoCodecsAvailable = errors.New("operation failed no codecs are available")

	// ErrUnsupportedCodec indicates the remote peer doesn't support the requested codec
	ErrUnsupportedCodec = errors.New("unable to start track, codec is not supported by remote")

	// ErrUnbindFailed indicates that a TrackLocal was not able to be unbind
	ErrUnbindFailed = errors.New("failed to unbind TrackLocal from PeerConnection")

	// ErrNoPayloaderForCodec indicates that the requested codec does not have a payloader
	ErrNoPayloaderForCodec = errors.New("the requested codec does not have a payloader")

	// ErrRegisterHeaderExtensionInvalidDirection indicates that a extension was registered with a direction besides `sendonly` or `recvonly`
	ErrRegisterHeaderExtensionInvalidDirection = errors.New("a header extension must be registered as 'recvonly', 'sendonly' or both")

	// ErrSimulcastProbeOverflow indicates that too many Simulcast probe streams are in flight and the requested SSRC was ignored
	ErrSimulcastProbeOverflow = errors.New("simulcast probe limit has been reached, new SSRC has been discarded")

	errDetachNotEnabled                 = errors.New("enable detaching by calling webrtc.DetachDataChannels()")
	errDetachBeforeOpened               = errors.New("datachannel not opened yet, try calling Detach from OnOpen")
	errDtlsTransportNotStarted          = errors.New("the DTLS transport has not started yet")
	errDtlsKeyExtractionFailed          = errors.New("failed extracting keys from DTLS for SRTP")
	errFailedToStartSRTP                = errors.New("failed to start SRTP")
	errFailedToStartSRTCP               = errors.New("failed to start SRTCP")
	errInvalidDTLSStart                 = errors.New("attempted to start DTLSTransport that is not in new state")
	errNoRemoteCertificate              = errors.New("peer didn't provide certificate via DTLS")
	errIdentityProviderNotImplemented   = errors.New("identity provider is not implemented")
	errNoMatchingCertificateFingerprint = errors.New("remote certificate does not match any fingerprint")

	errICEConnectionNotStarted        = errors.New("ICE connection not started")
	errICECandidateTypeUnknown        = errors.New("unknown candidate type")
	errICEInvalidConvertCandidateType = errors.New("cannot convert ice.CandidateType into webrtc.ICECandidateType, invalid type")
	errICEAgentNotExist               = errors.New("ICEAgent does not exist")
	errICECandiatesCoversionFailed    = errors.New("unable to convert ICE candidates to ICECandidates")
	errICERoleUnknown                 = errors.New("unknown ICE Role")
	errICEProtocolUnknown             = errors.New("unknown protocol")
	errICEGathererNotStarted          = errors.New("gatherer not started")

	errNetworkTypeUnknown = errors.New("unknown network type")

	errSDPDoesNotMatchOffer                           = errors.New("new sdp does not match previous offer")
	errSDPDoesNotMatchAnswer                          = errors.New("new sdp does not match previous answer")
	errPeerConnSDPTypeInvalidValue                    = errors.New("provided value is not a valid enum value of type SDPType")
	errPeerConnStateChangeInvalid                     = errors.New("invalid state change op")
	errPeerConnStateChangeUnhandled                   = errors.New("unhandled state change op")
	errPeerConnSDPTypeInvalidValueSetLocalDescription = errors.New("invalid SDP type supplied to SetLocalDescription()")
	errPeerConnRemoteDescriptionWithoutMidValue       = errors.New("remoteDescription contained media section without mid value")
	errPeerConnRemoteDescriptionNil                   = errors.New("remoteDescription has not been set yet")
	errPeerConnSingleMediaSectionHasExplicitSSRC      = errors.New("single media section has an explicit SSRC")
	errPeerConnRemoteSSRCAddTransceiver               = errors.New("could not add transceiver for remote SSRC")
	errPeerConnSimulcastMidRTPExtensionRequired       = errors.New("mid RTP Extensions required for Simulcast")
	errPeerConnSimulcastStreamIDRTPExtensionRequired  = errors.New("stream id RTP Extensions required for Simulcast")
	errPeerConnSimulcastIncomingSSRCFailed            = errors.New("incoming SSRC failed Simulcast probing")
	errPeerConnAddTransceiverFromKindOnlyAcceptsOne   = errors.New("AddTransceiverFromKind only accepts one RtpTransceiverInit")
	errPeerConnAddTransceiverFromTrackOnlyAcceptsOne  = errors.New("AddTransceiverFromTrack only accepts one RtpTransceiverInit")
	errPeerConnAddTransceiverFromKindSupport          = errors.New("AddTransceiverFromKind currently only supports recvonly")
	errPeerConnAddTransceiverFromTrackSupport         = errors.New("AddTransceiverFromTrack currently only supports sendonly and sendrecv")
	errPeerConnSetIdentityProviderNotImplemented      = errors.New("TODO SetIdentityProvider")
	errPeerConnWriteRTCPOpenWriteStream               = errors.New("WriteRTCP failed to open WriteStream")
	errPeerConnTranscieverMidNil                      = errors.New("cannot find transceiver with mid")

	errRTPReceiverDTLSTransportNil            = errors.New("DTLSTransport must not be nil")
	errRTPReceiverReceiveAlreadyCalled        = errors.New("Receive has already been called")
	errRTPReceiverWithSSRCTrackStreamNotFound = errors.New("unable to find stream for Track with SSRC")
	errRTPReceiverForSSRCTrackStreamNotFound  = errors.New("no trackStreams found for SSRC")
	errRTPReceiverForRIDTrackStreamNotFound   = errors.New("no trackStreams found for RID")

	errRTPSenderTrackNil          = errors.New("Track must not be nil")
	errRTPSenderDTLSTransportNil  = errors.New("DTLSTransport must not be nil")
	errRTPSenderSendAlreadyCalled = errors.New("Send has already been called")

	errRTPTransceiverCannotChangeMid        = errors.New("errRTPSenderTrackNil")
	errRTPTransceiverSetSendingInvalidState = errors.New("invalid state change in RTPTransceiver.setSending")

	errSCTPTransportDTLS = errors.New("DTLS not established")

	errSDPZeroTransceivers                 = errors.New("addTransceiverSDP() called with 0 transceivers")
	errSDPMediaSectionMediaDataChanInvalid = errors.New("invalid Media Section. Media + DataChannel both enabled")
	errSDPMediaSectionMultipleTrackInvalid = errors.New("invalid Media Section. Can not have multiple tracks in one MediaSection in UnifiedPlan")

	errSettingEngineSetAnsweringDTLSRole = errors.New("SetAnsweringDTLSRole must DTLSRoleClient or DTLSRoleServer")

	errSignalingStateCannotRollback            = errors.New("can't rollback from stable state")
	errSignalingStateProposedTransitionInvalid = errors.New("invalid proposed signaling state transition")

	errStatsICECandidateStateInvalid = errors.New("cannot convert to StatsICECandidatePairStateSucceeded invalid ice candidate state")

	errICETransportNotInNew = errors.New("ICETransport can only be called in ICETransportStateNew")
)
