package sdp

import (
	"errors"
	"fmt"
	"io"
	"sort"
	"strconv"
	"strings"

	"github.com/pion/randutil"
)

const (
	attributeKey = "a="
)

var (
	errExtractCodecRtpmap  = errors.New("could not extract codec from rtpmap")
	errExtractCodecFmtp    = errors.New("could not extract codec from fmtp")
	errExtractCodecRtcpFb  = errors.New("could not extract codec from rtcp-fb")
	errPayloadTypeNotFound = errors.New("payload type not found")
	errCodecNotFound       = errors.New("codec not found")
	errSyntaxError         = errors.New("SyntaxError")
)

// ConnectionRole indicates which of the end points should initiate the connection establishment
type ConnectionRole int

const (
	// ConnectionRoleActive indicates the endpoint will initiate an outgoing connection.
	ConnectionRoleActive ConnectionRole = iota + 1

	// ConnectionRolePassive indicates the endpoint will accept an incoming connection.
	ConnectionRolePassive

	// ConnectionRoleActpass indicates the endpoint is willing to accept an incoming connection or to initiate an outgoing connection.
	ConnectionRoleActpass

	// ConnectionRoleHoldconn indicates the endpoint does not want the connection to be established for the time being.
	ConnectionRoleHoldconn
)

func (t ConnectionRole) String() string {
	switch t {
	case ConnectionRoleActive:
		return "active"
	case ConnectionRolePassive:
		return "passive"
	case ConnectionRoleActpass:
		return "actpass"
	case ConnectionRoleHoldconn:
		return "holdconn"
	default:
		return "Unknown"
	}
}

func newSessionID() (uint64, error) {
	// https://tools.ietf.org/html/draft-ietf-rtcweb-jsep-26#section-5.2.1
	// Session ID is recommended to be constructed by generating a 64-bit
	// quantity with the highest bit set to zero and the remaining 63-bits
	// being cryptographically random.
	id, err := randutil.CryptoUint64()
	return id & (^(uint64(1) << 63)), err
}

// Codec represents a codec
type Codec struct {
	PayloadType        uint8
	Name               string
	ClockRate          uint32
	EncodingParameters string
	Fmtp               string
	RTCPFeedback       []string
}

const (
	unknown = iota
)

func (c Codec) String() string {
	return fmt.Sprintf("%d %s/%d/%s (%s) [%s]", c.PayloadType, c.Name, c.ClockRate, c.EncodingParameters, c.Fmtp, strings.Join(c.RTCPFeedback, ", "))
}

func parseRtpmap(rtpmap string) (Codec, error) {
	var codec Codec
	parsingFailed := errExtractCodecRtpmap

	// a=rtpmap:<payload type> <encoding name>/<clock rate>[/<encoding parameters>]
	split := strings.Split(rtpmap, " ")
	if len(split) != 2 {
		return codec, parsingFailed
	}

	ptSplit := strings.Split(split[0], ":")
	if len(ptSplit) != 2 {
		return codec, parsingFailed
	}

	ptInt, err := strconv.ParseUint(ptSplit[1], 10, 8)
	if err != nil {
		return codec, parsingFailed
	}

	codec.PayloadType = uint8(ptInt)

	split = strings.Split(split[1], "/")
	codec.Name = split[0]
	parts := len(split)
	if parts > 1 {
		rate, err := strconv.ParseUint(split[1], 10, 32)
		if err != nil {
			return codec, parsingFailed
		}
		codec.ClockRate = uint32(rate)
	}
	if parts > 2 {
		codec.EncodingParameters = split[2]
	}

	return codec, nil
}

func parseFmtp(fmtp string) (Codec, error) {
	var codec Codec
	parsingFailed := errExtractCodecFmtp

	// a=fmtp:<format> <format specific parameters>
	split := strings.Split(fmtp, " ")
	if len(split) != 2 {
		return codec, parsingFailed
	}

	formatParams := split[1]

	split = strings.Split(split[0], ":")
	if len(split) != 2 {
		return codec, parsingFailed
	}

	ptInt, err := strconv.ParseUint(split[1], 10, 8)
	if err != nil {
		return codec, parsingFailed
	}

	codec.PayloadType = uint8(ptInt)
	codec.Fmtp = formatParams

	return codec, nil
}

func parseRtcpFb(rtcpFb string) (Codec, error) {
	var codec Codec
	parsingFailed := errExtractCodecRtcpFb

	// a=ftcp-fb:<payload type> <RTCP feedback type> [<RTCP feedback parameter>]
	split := strings.SplitN(rtcpFb, " ", 2)
	if len(split) != 2 {
		return codec, parsingFailed
	}

	ptSplit := strings.Split(split[0], ":")
	if len(ptSplit) != 2 {
		return codec, parsingFailed
	}

	ptInt, err := strconv.ParseUint(ptSplit[1], 10, 8)
	if err != nil {
		return codec, parsingFailed
	}

	codec.PayloadType = uint8(ptInt)
	codec.RTCPFeedback = append(codec.RTCPFeedback, split[1])

	return codec, nil
}

func mergeCodecs(codec Codec, codecs map[uint8]Codec) {
	savedCodec := codecs[codec.PayloadType]

	if savedCodec.PayloadType == 0 {
		savedCodec.PayloadType = codec.PayloadType
	}
	if savedCodec.Name == "" {
		savedCodec.Name = codec.Name
	}
	if savedCodec.ClockRate == 0 {
		savedCodec.ClockRate = codec.ClockRate
	}
	if savedCodec.EncodingParameters == "" {
		savedCodec.EncodingParameters = codec.EncodingParameters
	}
	if savedCodec.Fmtp == "" {
		savedCodec.Fmtp = codec.Fmtp
	}
	savedCodec.RTCPFeedback = append(savedCodec.RTCPFeedback, codec.RTCPFeedback...)

	codecs[savedCodec.PayloadType] = savedCodec
}

func (s *SessionDescription) buildCodecMap() map[uint8]Codec {
	codecs := make(map[uint8]Codec)

	for _, m := range s.MediaDescriptions {
		for _, a := range m.Attributes {
			attr := a.String()
			switch {
			case strings.HasPrefix(attr, "rtpmap:"):
				codec, err := parseRtpmap(attr)
				if err == nil {
					mergeCodecs(codec, codecs)
				}
			case strings.HasPrefix(attr, "fmtp:"):
				codec, err := parseFmtp(attr)
				if err == nil {
					mergeCodecs(codec, codecs)
				}
			case strings.HasPrefix(attr, "rtcp-fb:"):
				codec, err := parseRtcpFb(attr)
				if err == nil {
					mergeCodecs(codec, codecs)
				}
			}
		}
	}

	return codecs
}

func equivalentFmtp(want, got string) bool {
	wantSplit := strings.Split(want, ";")
	gotSplit := strings.Split(got, ";")

	if len(wantSplit) != len(gotSplit) {
		return false
	}

	sort.Strings(wantSplit)
	sort.Strings(gotSplit)

	for i, wantPart := range wantSplit {
		wantPart = strings.TrimSpace(wantPart)
		gotPart := strings.TrimSpace(gotSplit[i])
		if gotPart != wantPart {
			return false
		}
	}

	return true
}

func codecsMatch(wanted, got Codec) bool {
	if wanted.Name != "" && !strings.EqualFold(wanted.Name, got.Name) {
		return false
	}
	if wanted.ClockRate != 0 && wanted.ClockRate != got.ClockRate {
		return false
	}
	if wanted.EncodingParameters != "" && wanted.EncodingParameters != got.EncodingParameters {
		return false
	}
	if wanted.Fmtp != "" && !equivalentFmtp(wanted.Fmtp, got.Fmtp) {
		return false
	}

	return true
}

// GetCodecForPayloadType scans the SessionDescription for the given payload type and returns the codec
func (s *SessionDescription) GetCodecForPayloadType(payloadType uint8) (Codec, error) {
	codecs := s.buildCodecMap()

	codec, ok := codecs[payloadType]
	if ok {
		return codec, nil
	}

	return codec, errPayloadTypeNotFound
}

// GetPayloadTypeForCodec scans the SessionDescription for a codec that matches the provided codec
// as closely as possible and returns its payload type
func (s *SessionDescription) GetPayloadTypeForCodec(wanted Codec) (uint8, error) {
	codecs := s.buildCodecMap()

	for payloadType, codec := range codecs {
		if codecsMatch(wanted, codec) {
			return payloadType, nil
		}
	}

	return 0, errCodecNotFound
}

type stateFn func(*lexer) (stateFn, error)

type lexer struct {
	desc *SessionDescription
	baseLexer
}

type keyToState func(key string) stateFn

func (l *lexer) handleType(fn keyToState) (stateFn, error) {
	key, err := l.readType()
	if errors.Is(err, io.EOF) && key == "" {
		return nil, nil //nolint:nilnil
	} else if err != nil {
		return nil, err
	}

	if res := fn(key); res != nil {
		return res, nil
	}

	return nil, l.syntaxError()
}
