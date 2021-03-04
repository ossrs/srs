package sdp

import (
	"fmt"
	"net/url"
	"strconv"
	"time"
)

// Constants for SDP attributes used in JSEP
const (
	AttrKeyCandidate       = "candidate"
	AttrKeyEndOfCandidates = "end-of-candidates"
	AttrKeyIdentity        = "identity"
	AttrKeyGroup           = "group"
	AttrKeySSRC            = "ssrc"
	AttrKeySSRCGroup       = "ssrc-group"
	AttrKeyMsid            = "msid"
	AttrKeyMsidSemantic    = "msid-semantic"
	AttrKeyConnectionSetup = "setup"
	AttrKeyMID             = "mid"
	AttrKeyICELite         = "ice-lite"
	AttrKeyRTCPMux         = "rtcp-mux"
	AttrKeyRTCPRsize       = "rtcp-rsize"
	AttrKeyInactive        = "inactive"
	AttrKeyRecvOnly        = "recvonly"
	AttrKeySendOnly        = "sendonly"
	AttrKeySendRecv        = "sendrecv"
	AttrKeyExtMap          = "extmap"
)

// Constants for semantic tokens used in JSEP
const (
	SemanticTokenLipSynchronization     = "LS"
	SemanticTokenFlowIdentification     = "FID"
	SemanticTokenForwardErrorCorrection = "FEC"
	SemanticTokenWebRTCMediaStreams     = "WMS"
)

// Constants for extmap key
const (
	ExtMapValueTransportCC = 3
)

func extMapURI() map[int]string {
	return map[int]string{
		ExtMapValueTransportCC: "http://www.ietf.org/id/draft-holmer-rmcat-transport-wide-cc-extensions-01",
	}
}

// API to match draft-ietf-rtcweb-jsep
// Move to webrtc or its own package?

// NewJSEPSessionDescription creates a new SessionDescription with
// some settings that are required by the JSEP spec.
//
// Note: Since v2.4.0, session ID has been fixed to use crypto random according to
//       JSEP spec, so that NewJSEPSessionDescription now returns error as a second
//       return value.
func NewJSEPSessionDescription(identity bool) (*SessionDescription, error) {
	sid, err := newSessionID()
	if err != nil {
		return nil, err
	}
	d := &SessionDescription{
		Version: 0,
		Origin: Origin{
			Username:       "-",
			SessionID:      sid,
			SessionVersion: uint64(time.Now().Unix()),
			NetworkType:    "IN",
			AddressType:    "IP4",
			UnicastAddress: "0.0.0.0",
		},
		SessionName: "-",
		TimeDescriptions: []TimeDescription{
			{
				Timing: Timing{
					StartTime: 0,
					StopTime:  0,
				},
				RepeatTimes: nil,
			},
		},
		Attributes: []Attribute{
			// 	"Attribute(ice-options:trickle)", // TODO: implement trickle ICE
		},
	}

	if identity {
		d.WithPropertyAttribute(AttrKeyIdentity)
	}

	return d, nil
}

// WithPropertyAttribute adds a property attribute 'a=key' to the session description
func (s *SessionDescription) WithPropertyAttribute(key string) *SessionDescription {
	s.Attributes = append(s.Attributes, NewPropertyAttribute(key))
	return s
}

// WithValueAttribute adds a value attribute 'a=key:value' to the session description
func (s *SessionDescription) WithValueAttribute(key, value string) *SessionDescription {
	s.Attributes = append(s.Attributes, NewAttribute(key, value))
	return s
}

// WithFingerprint adds a fingerprint to the session description
func (s *SessionDescription) WithFingerprint(algorithm, value string) *SessionDescription {
	return s.WithValueAttribute("fingerprint", algorithm+" "+value)
}

// WithMedia adds a media description to the session description
func (s *SessionDescription) WithMedia(md *MediaDescription) *SessionDescription {
	s.MediaDescriptions = append(s.MediaDescriptions, md)
	return s
}

// NewJSEPMediaDescription creates a new MediaName with
// some settings that are required by the JSEP spec.
func NewJSEPMediaDescription(codecType string, codecPrefs []string) *MediaDescription {
	return &MediaDescription{
		MediaName: MediaName{
			Media:  codecType,
			Port:   RangedPort{Value: 9},
			Protos: []string{"UDP", "TLS", "RTP", "SAVPF"},
		},
		ConnectionInformation: &ConnectionInformation{
			NetworkType: "IN",
			AddressType: "IP4",
			Address: &Address{
				Address: "0.0.0.0",
			},
		},
	}
}

// WithPropertyAttribute adds a property attribute 'a=key' to the media description
func (d *MediaDescription) WithPropertyAttribute(key string) *MediaDescription {
	d.Attributes = append(d.Attributes, NewPropertyAttribute(key))
	return d
}

// WithValueAttribute adds a value attribute 'a=key:value' to the media description
func (d *MediaDescription) WithValueAttribute(key, value string) *MediaDescription {
	d.Attributes = append(d.Attributes, NewAttribute(key, value))
	return d
}

// WithFingerprint adds a fingerprint to the media description
func (d *MediaDescription) WithFingerprint(algorithm, value string) *MediaDescription {
	return d.WithValueAttribute("fingerprint", algorithm+" "+value)
}

// WithICECredentials adds ICE credentials to the media description
func (d *MediaDescription) WithICECredentials(username, password string) *MediaDescription {
	return d.
		WithValueAttribute("ice-ufrag", username).
		WithValueAttribute("ice-pwd", password)
}

// WithCodec adds codec information to the media description
func (d *MediaDescription) WithCodec(payloadType uint8, name string, clockrate uint32, channels uint16, fmtp string) *MediaDescription {
	d.MediaName.Formats = append(d.MediaName.Formats, strconv.Itoa(int(payloadType)))
	rtpmap := fmt.Sprintf("%d %s/%d", payloadType, name, clockrate)
	if channels > 0 {
		rtpmap += fmt.Sprintf("/%d", channels)
	}
	d.WithValueAttribute("rtpmap", rtpmap)
	if fmtp != "" {
		d.WithValueAttribute("fmtp", fmt.Sprintf("%d %s", payloadType, fmtp))
	}
	return d
}

// WithMediaSource adds media source information to the media description
func (d *MediaDescription) WithMediaSource(ssrc uint32, cname, streamLabel, label string) *MediaDescription {
	return d.
		WithValueAttribute("ssrc", fmt.Sprintf("%d cname:%s", ssrc, cname)). // Deprecated but not phased out?
		WithValueAttribute("ssrc", fmt.Sprintf("%d msid:%s %s", ssrc, streamLabel, label)).
		WithValueAttribute("ssrc", fmt.Sprintf("%d mslabel:%s", ssrc, streamLabel)). // Deprecated but not phased out?
		WithValueAttribute("ssrc", fmt.Sprintf("%d label:%s", ssrc, label))          // Deprecated but not phased out?
}

// WithCandidate adds an ICE candidate to the media description
// Deprecated: use WithICECandidate instead
func (d *MediaDescription) WithCandidate(value string) *MediaDescription {
	return d.WithValueAttribute("candidate", value)
}

// WithExtMap adds an extmap to the media description
func (d *MediaDescription) WithExtMap(e ExtMap) *MediaDescription {
	return d.WithPropertyAttribute(e.Marshal())
}

// WithTransportCCExtMap adds an extmap to the media description
func (d *MediaDescription) WithTransportCCExtMap() *MediaDescription {
	uri, _ := url.Parse(extMapURI()[ExtMapValueTransportCC])
	e := ExtMap{
		Value: ExtMapValueTransportCC,
		URI:   uri,
	}
	return d.WithExtMap(e)
}
