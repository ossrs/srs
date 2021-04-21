package sdp

import (
	"fmt"
	"net/url"
	"strconv"
	"strings"
)

// Default ext values
const (
	DefExtMapValueABSSendTime     = 1
	DefExtMapValueTransportCC     = 2
	DefExtMapValueSDESMid         = 3
	DefExtMapValueSDESRTPStreamID = 4

	ABSSendTimeURI     = "http://www.webrtc.org/experiments/rtp-hdrext/abs-send-time"
	TransportCCURI     = "http://www.ietf.org/id/draft-holmer-rmcat-transport-wide-cc-extensions-01"
	SDESMidURI         = "urn:ietf:params:rtp-hdrext:sdes:mid"
	SDESRTPStreamIDURI = "urn:ietf:params:rtp-hdrext:sdes:rtp-stream-id"
	AudioLevelURI      = "urn:ietf:params:rtp-hdrext:ssrc-audio-level"
)

// ExtMap represents the activation of a single RTP header extension
type ExtMap struct {
	Value     int
	Direction Direction
	URI       *url.URL
	ExtAttr   *string
}

// Clone converts this object to an Attribute
func (e *ExtMap) Clone() Attribute {
	return Attribute{Key: "extmap", Value: e.string()}
}

// Unmarshal creates an Extmap from a string
func (e *ExtMap) Unmarshal(raw string) error {
	parts := strings.SplitN(raw, ":", 2)
	if len(parts) != 2 {
		return fmt.Errorf("%w: %v", errSyntaxError, raw)
	}

	fields := strings.Fields(parts[1])
	if len(fields) < 2 {
		return fmt.Errorf("%w: %v", errSyntaxError, raw)
	}

	valdir := strings.Split(fields[0], "/")
	value, err := strconv.ParseInt(valdir[0], 10, 64)
	if (value < 1) || (value > 246) {
		return fmt.Errorf("%w: %v -- extmap key must be in the range 1-256", errSyntaxError, valdir[0])
	}
	if err != nil {
		return fmt.Errorf("%w: %v", errSyntaxError, valdir[0])
	}

	var direction Direction
	if len(valdir) == 2 {
		direction, err = NewDirection(valdir[1])
		if err != nil {
			return err
		}
	}

	uri, err := url.Parse(fields[1])
	if err != nil {
		return err
	}

	if len(fields) == 3 {
		tmp := fields[2]
		e.ExtAttr = &tmp
	}

	e.Value = int(value)
	e.Direction = direction
	e.URI = uri
	return nil
}

// Marshal creates a string from an ExtMap
func (e *ExtMap) Marshal() string {
	return e.Name() + ":" + e.string()
}

func (e *ExtMap) string() string {
	output := fmt.Sprintf("%d", e.Value)
	dirstring := e.Direction.String()
	if dirstring != directionUnknownStr {
		output += "/" + dirstring
	}

	if e.URI != nil {
		output += " " + e.URI.String()
	}

	if e.ExtAttr != nil {
		output += " " + *e.ExtAttr
	}

	return output
}

// Name returns the constant name of this object
func (e *ExtMap) Name() string {
	return "extmap"
}
