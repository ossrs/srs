package dtls

import (
	"bytes"
	"context"
)

func flight2Parse(ctx context.Context, c flightConn, state *State, cache *handshakeCache, cfg *handshakeConfig) (flightVal, *alert, error) {
	seq, msgs, ok := cache.fullPullMap(state.handshakeRecvSequence,
		handshakeCachePullRule{handshakeTypeClientHello, cfg.initialEpoch, true, false},
	)
	if !ok {
		// Client may retransmit the first ClientHello when HelloVerifyRequest is dropped.
		// Parse as flight 0 in this case.
		return flight0Parse(ctx, c, state, cache, cfg)
	}
	state.handshakeRecvSequence = seq

	var clientHello *handshakeMessageClientHello

	// Validate type
	if clientHello, ok = msgs[handshakeTypeClientHello].(*handshakeMessageClientHello); !ok {
		return 0, &alert{alertLevelFatal, alertInternalError}, nil
	}

	if !clientHello.version.Equal(protocolVersion1_2) {
		return 0, &alert{alertLevelFatal, alertProtocolVersion}, errUnsupportedProtocolVersion
	}

	if len(clientHello.cookie) == 0 {
		return 0, nil, nil
	}
	if !bytes.Equal(state.cookie, clientHello.cookie) {
		return 0, &alert{alertLevelFatal, alertAccessDenied}, errCookieMismatch
	}
	return flight4, nil, nil
}

func flight2Generate(c flightConn, state *State, cache *handshakeCache, cfg *handshakeConfig) ([]*packet, *alert, error) {
	state.handshakeSendSequence = 0
	return []*packet{
		{
			record: &recordLayer{
				recordLayerHeader: recordLayerHeader{
					protocolVersion: protocolVersion1_2,
				},
				content: &handshake{
					handshakeMessage: &handshakeMessageHelloVerifyRequest{
						version: protocolVersion1_2,
						cookie:  state.cookie,
					},
				},
			},
		},
	}, nil, nil
}
