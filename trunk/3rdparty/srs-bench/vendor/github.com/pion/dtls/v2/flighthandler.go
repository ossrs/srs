package dtls

import (
	"context"

	"github.com/pion/dtls/v2/pkg/protocol/alert"
)

// Parse received handshakes and return next flightVal
type flightParser func(context.Context, flightConn, *State, *handshakeCache, *handshakeConfig) (flightVal, *alert.Alert, error)

// Generate flights
type flightGenerator func(flightConn, *State, *handshakeCache, *handshakeConfig) ([]*packet, *alert.Alert, error)

func (f flightVal) getFlightParser() (flightParser, error) {
	switch f {
	case flight0:
		return flight0Parse, nil
	case flight1:
		return flight1Parse, nil
	case flight2:
		return flight2Parse, nil
	case flight3:
		return flight3Parse, nil
	case flight4:
		return flight4Parse, nil
	case flight5:
		return flight5Parse, nil
	case flight6:
		return flight6Parse, nil
	default:
		return nil, errInvalidFlight
	}
}

func (f flightVal) getFlightGenerator() (gen flightGenerator, retransmit bool, err error) {
	switch f {
	case flight0:
		return flight0Generate, true, nil
	case flight1:
		return flight1Generate, true, nil
	case flight2:
		// https://tools.ietf.org/html/rfc6347#section-3.2.1
		// HelloVerifyRequests must not be retransmitted.
		return flight2Generate, false, nil
	case flight3:
		return flight3Generate, true, nil
	case flight4:
		return flight4Generate, true, nil
	case flight5:
		return flight5Generate, true, nil
	case flight6:
		return flight6Generate, true, nil
	default:
		return nil, false, errInvalidFlight
	}
}
