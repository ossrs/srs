package dtls

import (
	"context"
)

func flight3Parse(ctx context.Context, c flightConn, state *State, cache *handshakeCache, cfg *handshakeConfig) (flightVal, *alert, error) { //nolint:gocognit
	// Clients may receive multiple HelloVerifyRequest messages with different cookies.
	// Clients SHOULD handle this by sending a new ClientHello with a cookie in response
	// to the new HelloVerifyRequest. RFC 6347 Section 4.2.1
	seq, msgs, ok := cache.fullPullMap(state.handshakeRecvSequence,
		handshakeCachePullRule{handshakeTypeHelloVerifyRequest, cfg.initialEpoch, false, true},
	)
	if ok {
		if h, msgOk := msgs[handshakeTypeHelloVerifyRequest].(*handshakeMessageHelloVerifyRequest); msgOk {
			// DTLS 1.2 clients must not assume that the server will use the protocol version
			// specified in HelloVerifyRequest message. RFC 6347 Section 4.2.1
			if !h.version.Equal(protocolVersion1_0) && !h.version.Equal(protocolVersion1_2) {
				return 0, &alert{alertLevelFatal, alertProtocolVersion}, errUnsupportedProtocolVersion
			}
			state.cookie = append([]byte{}, h.cookie...)
			state.handshakeRecvSequence = seq
			return flight3, nil, nil
		}
	}

	if cfg.localPSKCallback != nil {
		seq, msgs, ok = cache.fullPullMap(state.handshakeRecvSequence,
			handshakeCachePullRule{handshakeTypeServerHello, cfg.initialEpoch, false, false},
			handshakeCachePullRule{handshakeTypeServerKeyExchange, cfg.initialEpoch, false, true},
			handshakeCachePullRule{handshakeTypeServerHelloDone, cfg.initialEpoch, false, false},
		)
	} else {
		seq, msgs, ok = cache.fullPullMap(state.handshakeRecvSequence,
			handshakeCachePullRule{handshakeTypeServerHello, cfg.initialEpoch, false, false},
			handshakeCachePullRule{handshakeTypeCertificate, cfg.initialEpoch, false, false},
			handshakeCachePullRule{handshakeTypeServerKeyExchange, cfg.initialEpoch, false, false},
			handshakeCachePullRule{handshakeTypeCertificateRequest, cfg.initialEpoch, false, true},
			handshakeCachePullRule{handshakeTypeServerHelloDone, cfg.initialEpoch, false, false},
		)
	}
	if !ok {
		// Don't have enough messages. Keep reading
		return 0, nil, nil
	}
	state.handshakeRecvSequence = seq

	if h, ok := msgs[handshakeTypeServerHello].(*handshakeMessageServerHello); ok {
		if !h.version.Equal(protocolVersion1_2) {
			return 0, &alert{alertLevelFatal, alertProtocolVersion}, errUnsupportedProtocolVersion
		}
		for _, extension := range h.extensions {
			switch e := extension.(type) {
			case *extensionUseSRTP:
				profile, ok := findMatchingSRTPProfile(e.protectionProfiles, cfg.localSRTPProtectionProfiles)
				if !ok {
					return 0, &alert{alertLevelFatal, alertIllegalParameter}, errClientNoMatchingSRTPProfile
				}
				state.srtpProtectionProfile = profile
			case *extensionUseExtendedMasterSecret:
				if cfg.extendedMasterSecret != DisableExtendedMasterSecret {
					state.extendedMasterSecret = true
				}
			}
		}
		if cfg.extendedMasterSecret == RequireExtendedMasterSecret && !state.extendedMasterSecret {
			return 0, &alert{alertLevelFatal, alertInsufficientSecurity}, errClientRequiredButNoServerEMS
		}
		if len(cfg.localSRTPProtectionProfiles) > 0 && state.srtpProtectionProfile == 0 {
			return 0, &alert{alertLevelFatal, alertInsufficientSecurity}, errRequestedButNoSRTPExtension
		}
		if _, ok := findMatchingCipherSuite([]cipherSuite{h.cipherSuite}, cfg.localCipherSuites); !ok {
			return 0, &alert{alertLevelFatal, alertInsufficientSecurity}, errCipherSuiteNoIntersection
		}

		state.cipherSuite = h.cipherSuite
		state.remoteRandom = h.random
		cfg.log.Tracef("[handshake] use cipher suite: %s", h.cipherSuite.String())
	}

	if h, ok := msgs[handshakeTypeCertificate].(*handshakeMessageCertificate); ok {
		state.PeerCertificates = h.certificate
	}

	if h, ok := msgs[handshakeTypeServerKeyExchange].(*handshakeMessageServerKeyExchange); ok {
		alertPtr, err := handleServerKeyExchange(c, state, cfg, h)
		if err != nil {
			return 0, alertPtr, err
		}
	}

	if _, ok := msgs[handshakeTypeCertificateRequest].(*handshakeMessageCertificateRequest); ok {
		state.remoteRequestedCertificate = true
	}

	return flight5, nil, nil
}

func handleServerKeyExchange(_ flightConn, state *State, cfg *handshakeConfig, h *handshakeMessageServerKeyExchange) (*alert, error) {
	var err error
	if cfg.localPSKCallback != nil {
		var psk []byte
		if psk, err = cfg.localPSKCallback(h.identityHint); err != nil {
			return &alert{alertLevelFatal, alertInternalError}, err
		}

		state.preMasterSecret = prfPSKPreMasterSecret(psk)
	} else {
		if state.localKeypair, err = generateKeypair(h.namedCurve); err != nil {
			return &alert{alertLevelFatal, alertInternalError}, err
		}

		if state.preMasterSecret, err = prfPreMasterSecret(h.publicKey, state.localKeypair.privateKey, state.localKeypair.curve); err != nil {
			return &alert{alertLevelFatal, alertInternalError}, err
		}
	}

	return nil, nil
}

func flight3Generate(c flightConn, state *State, cache *handshakeCache, cfg *handshakeConfig) ([]*packet, *alert, error) {
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
