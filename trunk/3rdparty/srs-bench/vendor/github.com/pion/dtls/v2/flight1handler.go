package dtls

import (
	"context"
)

func flight1Parse(ctx context.Context, c flightConn, state *State, cache *handshakeCache, cfg *handshakeConfig) (flightVal, *alert, error) {
	// HelloVerifyRequest can be skipped by the server,
	// so allow ServerHello during flight1 also
	seq, msgs, ok := cache.fullPullMap(state.handshakeRecvSequence,
		handshakeCachePullRule{handshakeTypeHelloVerifyRequest, cfg.initialEpoch, false, true},
		handshakeCachePullRule{handshakeTypeServerHello, cfg.initialEpoch, false, true},
	)
	if !ok {
		// No valid message received. Keep reading
		return 0, nil, nil
	}

	if _, ok := msgs[handshakeTypeServerHello]; ok {
		// Flight1 and flight2 were skipped.
		// Parse as flight3.
		return flight3Parse(ctx, c, state, cache, cfg)
	}

	if h, ok := msgs[handshakeTypeHelloVerifyRequest].(*handshakeMessageHelloVerifyRequest); ok {
		// DTLS 1.2 clients must not assume that the server will use the protocol version
		// specified in HelloVerifyRequest message. RFC 6347 Section 4.2.1
		if !h.version.Equal(protocolVersion1_0) && !h.version.Equal(protocolVersion1_2) {
			return 0, &alert{alertLevelFatal, alertProtocolVersion}, errUnsupportedProtocolVersion
		}
		state.cookie = append([]byte{}, h.cookie...)
		state.handshakeRecvSequence = seq
		return flight3, nil, nil
	}

	return 0, &alert{alertLevelFatal, alertInternalError}, nil
}

func flight1Generate(c flightConn, state *State, cache *handshakeCache, cfg *handshakeConfig) ([]*packet, *alert, error) {
	var zeroEpoch uint16
	state.localEpoch.Store(zeroEpoch)
	state.remoteEpoch.Store(zeroEpoch)
	state.namedCurve = defaultNamedCurve
	state.cookie = nil

	if err := state.localRandom.populate(); err != nil {
		return nil, nil, err
	}

	extensions := []extension{
		&extensionSupportedSignatureAlgorithms{
			signatureHashAlgorithms: cfg.localSignatureSchemes,
		},
		&extensionRenegotiationInfo{
			renegotiatedConnection: 0,
		},
	}
	if cfg.localPSKCallback == nil {
		extensions = append(extensions, []extension{
			&extensionSupportedEllipticCurves{
				ellipticCurves: []namedCurve{namedCurveX25519, namedCurveP256, namedCurveP384},
			},
			&extensionSupportedPointFormats{
				pointFormats: []ellipticCurvePointFormat{ellipticCurvePointFormatUncompressed},
			},
		}...)
	}

	if len(cfg.localSRTPProtectionProfiles) > 0 {
		extensions = append(extensions, &extensionUseSRTP{
			protectionProfiles: cfg.localSRTPProtectionProfiles,
		})
	}

	if cfg.extendedMasterSecret == RequestExtendedMasterSecret ||
		cfg.extendedMasterSecret == RequireExtendedMasterSecret {
		extensions = append(extensions, &extensionUseExtendedMasterSecret{
			supported: true,
		})
	}

	if len(cfg.serverName) > 0 {
		extensions = append(extensions, &extensionServerName{serverName: cfg.serverName})
	}

	return []*packet{
		{
			record: &recordLayer{
				recordLayerHeader: recordLayerHeader{
					protocolVersion: protocolVersion1_2,
				},
				content: &handshake{
					handshakeMessage: &handshakeMessageClientHello{
						version:            protocolVersion1_2,
						cookie:             state.cookie,
						random:             state.localRandom,
						cipherSuites:       cfg.localCipherSuites,
						compressionMethods: defaultCompressionMethods(),
						extensions:         extensions,
					},
				},
			},
		},
	}, nil, nil
}
