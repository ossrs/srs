// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package webrtc

import (
	"math"

	"github.com/pion/dtls/v3"
)

const (
	// default as the standard ethernet MTU
	// can be overwritten with SettingEngine.SetReceiveMTU().
	receiveMTU = 1500

	// simulcastProbeCount is the amount of RTP Packets
	// that handleUndeclaredSSRC will read and try to dispatch from
	// mid and rid values.
	simulcastProbeCount = 10

	// simulcastMaxProbeRoutines is how many active routines can be used to probe
	// If the total amount of incoming SSRCes exceeds this new requests will be ignored.
	simulcastMaxProbeRoutines = 25

	// Default Max SCTP Message Size is the largest single DataChannel
	// message we can send or accept. This default was chosen to match FireFox.
	defaultMaxSCTPMessageSize = 1073741823

	// If a DataChannel Max Message Size isn't declared by the Remote(max-message-size)
	// this is the value we default to. This value was chosen because it was the behavior
	// of Pion before max-message-size was implemented.
	sctpMaxMessageSizeUnsetValue = math.MaxUint16

	mediaSectionApplication = "application"

	sdpAttributeRid = "rid"

	sdpAttributeSimulcast = "simulcast"

	outboundMTU = 1200

	rtpPayloadTypeBitmask = 0x7F

	incomingUnhandledRTPSsrc = "Incoming unhandled RTP ssrc(%d), OnTrack will not be fired. %v"

	generatedCertificateOrigin = "WebRTC"

	// AttributeRtxPayloadType is the interceptor attribute added when Read()
	// returns an RTX packet containing the RTX stream payload type.
	AttributeRtxPayloadType = "rtx_payload_type"
	// AttributeRtxSsrc is the interceptor attribute added when Read()
	// returns an RTX packet containing the RTX stream SSRC.
	AttributeRtxSsrc = "rtx_ssrc"
	// AttributeRtxSequenceNumber is the interceptor attribute added when
	// Read() returns an RTX packet containing the RTX stream sequence number.
	AttributeRtxSequenceNumber = "rtx_sequence_number"
)

func defaultSrtpProtectionProfiles() []dtls.SRTPProtectionProfile {
	return []dtls.SRTPProtectionProfile{
		dtls.SRTP_AEAD_AES_256_GCM,
		dtls.SRTP_AEAD_AES_128_GCM,
		dtls.SRTP_AES128_CM_HMAC_SHA1_80,
	}
}
