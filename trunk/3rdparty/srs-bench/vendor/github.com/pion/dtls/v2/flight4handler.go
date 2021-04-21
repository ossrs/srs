package dtls

import (
	"context"
	"crypto/x509"
)

func flight4Parse(ctx context.Context, c flightConn, state *State, cache *handshakeCache, cfg *handshakeConfig) (flightVal, *alert, error) { //nolint:gocognit
	seq, msgs, ok := cache.fullPullMap(state.handshakeRecvSequence,
		handshakeCachePullRule{handshakeTypeCertificate, cfg.initialEpoch, true, true},
		handshakeCachePullRule{handshakeTypeClientKeyExchange, cfg.initialEpoch, true, false},
		handshakeCachePullRule{handshakeTypeCertificateVerify, cfg.initialEpoch, true, true},
	)
	if !ok {
		// No valid message received. Keep reading
		return 0, nil, nil
	}

	// Validate type
	var clientKeyExchange *handshakeMessageClientKeyExchange
	if clientKeyExchange, ok = msgs[handshakeTypeClientKeyExchange].(*handshakeMessageClientKeyExchange); !ok {
		return 0, &alert{alertLevelFatal, alertInternalError}, nil
	}

	if h, hasCert := msgs[handshakeTypeCertificate].(*handshakeMessageCertificate); hasCert {
		state.PeerCertificates = h.certificate
	}

	if h, hasCertVerify := msgs[handshakeTypeCertificateVerify].(*handshakeMessageCertificateVerify); hasCertVerify {
		if state.PeerCertificates == nil {
			return 0, &alert{alertLevelFatal, alertNoCertificate}, errCertificateVerifyNoCertificate
		}

		plainText := cache.pullAndMerge(
			handshakeCachePullRule{handshakeTypeClientHello, cfg.initialEpoch, true, false},
			handshakeCachePullRule{handshakeTypeServerHello, cfg.initialEpoch, false, false},
			handshakeCachePullRule{handshakeTypeCertificate, cfg.initialEpoch, false, false},
			handshakeCachePullRule{handshakeTypeServerKeyExchange, cfg.initialEpoch, false, false},
			handshakeCachePullRule{handshakeTypeCertificateRequest, cfg.initialEpoch, false, false},
			handshakeCachePullRule{handshakeTypeServerHelloDone, cfg.initialEpoch, false, false},
			handshakeCachePullRule{handshakeTypeCertificate, cfg.initialEpoch, true, false},
			handshakeCachePullRule{handshakeTypeClientKeyExchange, cfg.initialEpoch, true, false},
		)

		// Verify that the pair of hash algorithm and signiture is listed.
		var validSignatureScheme bool
		for _, ss := range cfg.localSignatureSchemes {
			if ss.hash == h.hashAlgorithm && ss.signature == h.signatureAlgorithm {
				validSignatureScheme = true
				break
			}
		}
		if !validSignatureScheme {
			return 0, &alert{alertLevelFatal, alertInsufficientSecurity}, errNoAvailableSignatureSchemes
		}

		if err := verifyCertificateVerify(plainText, h.hashAlgorithm, h.signature, state.PeerCertificates); err != nil {
			return 0, &alert{alertLevelFatal, alertBadCertificate}, err
		}
		var chains [][]*x509.Certificate
		var err error
		var verified bool
		if cfg.clientAuth >= VerifyClientCertIfGiven {
			if chains, err = verifyClientCert(state.PeerCertificates, cfg.clientCAs); err != nil {
				return 0, &alert{alertLevelFatal, alertBadCertificate}, err
			}
			verified = true
		}
		if cfg.verifyPeerCertificate != nil {
			if err := cfg.verifyPeerCertificate(state.PeerCertificates, chains); err != nil {
				return 0, &alert{alertLevelFatal, alertBadCertificate}, err
			}
		}
		state.peerCertificatesVerified = verified
	}

	if !state.cipherSuite.isInitialized() {
		serverRandom := state.localRandom.marshalFixed()
		clientRandom := state.remoteRandom.marshalFixed()

		var err error
		var preMasterSecret []byte
		if cfg.localPSKCallback != nil {
			var psk []byte
			if psk, err = cfg.localPSKCallback(clientKeyExchange.identityHint); err != nil {
				return 0, &alert{alertLevelFatal, alertInternalError}, err
			}

			preMasterSecret = prfPSKPreMasterSecret(psk)
		} else {
			preMasterSecret, err = prfPreMasterSecret(clientKeyExchange.publicKey, state.localKeypair.privateKey, state.localKeypair.curve)
			if err != nil {
				return 0, &alert{alertLevelFatal, alertIllegalParameter}, err
			}
		}

		if state.extendedMasterSecret {
			var sessionHash []byte
			sessionHash, err = cache.sessionHash(state.cipherSuite.hashFunc(), cfg.initialEpoch)
			if err != nil {
				return 0, &alert{alertLevelFatal, alertInternalError}, err
			}

			state.masterSecret, err = prfExtendedMasterSecret(preMasterSecret, sessionHash, state.cipherSuite.hashFunc())
			if err != nil {
				return 0, &alert{alertLevelFatal, alertInternalError}, err
			}
		} else {
			state.masterSecret, err = prfMasterSecret(preMasterSecret, clientRandom[:], serverRandom[:], state.cipherSuite.hashFunc())
			if err != nil {
				return 0, &alert{alertLevelFatal, alertInternalError}, err
			}
		}

		if err := state.cipherSuite.init(state.masterSecret, clientRandom[:], serverRandom[:], false); err != nil {
			return 0, &alert{alertLevelFatal, alertInternalError}, err
		}
	}

	// Now, encrypted packets can be handled
	if err := c.handleQueuedPackets(ctx); err != nil {
		return 0, &alert{alertLevelFatal, alertInternalError}, err
	}

	seq, msgs, ok = cache.fullPullMap(seq,
		handshakeCachePullRule{handshakeTypeFinished, cfg.initialEpoch + 1, true, false},
	)
	if !ok {
		// No valid message received. Keep reading
		return 0, nil, nil
	}
	state.handshakeRecvSequence = seq

	if _, ok = msgs[handshakeTypeFinished].(*handshakeMessageFinished); !ok {
		return 0, &alert{alertLevelFatal, alertInternalError}, nil
	}

	switch cfg.clientAuth {
	case RequireAnyClientCert:
		if state.PeerCertificates == nil {
			return 0, &alert{alertLevelFatal, alertNoCertificate}, errClientCertificateRequired
		}
	case VerifyClientCertIfGiven:
		if state.PeerCertificates != nil && !state.peerCertificatesVerified {
			return 0, &alert{alertLevelFatal, alertBadCertificate}, errClientCertificateNotVerified
		}
	case RequireAndVerifyClientCert:
		if state.PeerCertificates == nil {
			return 0, &alert{alertLevelFatal, alertNoCertificate}, errClientCertificateRequired
		}
		if !state.peerCertificatesVerified {
			return 0, &alert{alertLevelFatal, alertBadCertificate}, errClientCertificateNotVerified
		}
	case NoClientCert, RequestClientCert:
		return flight6, nil, nil
	}

	return flight6, nil, nil
}

func flight4Generate(c flightConn, state *State, cache *handshakeCache, cfg *handshakeConfig) ([]*packet, *alert, error) {
	extensions := []extension{}
	if (cfg.extendedMasterSecret == RequestExtendedMasterSecret ||
		cfg.extendedMasterSecret == RequireExtendedMasterSecret) && state.extendedMasterSecret {
		extensions = append(extensions, &extensionUseExtendedMasterSecret{
			supported: true,
		})
	}
	if state.srtpProtectionProfile != 0 {
		extensions = append(extensions, &extensionUseSRTP{
			protectionProfiles: []SRTPProtectionProfile{state.srtpProtectionProfile},
		})
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

	var pkts []*packet

	pkts = append(pkts, &packet{
		record: &recordLayer{
			recordLayerHeader: recordLayerHeader{
				protocolVersion: protocolVersion1_2,
			},
			content: &handshake{
				handshakeMessage: &handshakeMessageServerHello{
					version:           protocolVersion1_2,
					random:            state.localRandom,
					cipherSuite:       state.cipherSuite,
					compressionMethod: defaultCompressionMethods()[0],
					extensions:        extensions,
				},
			},
		},
	})

	if cfg.localPSKCallback == nil {
		certificate, err := cfg.getCertificate(cfg.serverName)
		if err != nil {
			return nil, &alert{alertLevelFatal, alertHandshakeFailure}, err
		}

		pkts = append(pkts, &packet{
			record: &recordLayer{
				recordLayerHeader: recordLayerHeader{
					protocolVersion: protocolVersion1_2,
				},
				content: &handshake{
					handshakeMessage: &handshakeMessageCertificate{
						certificate: certificate.Certificate,
					},
				},
			},
		})

		serverRandom := state.localRandom.marshalFixed()
		clientRandom := state.remoteRandom.marshalFixed()

		// Find compatible signature scheme
		signatureHashAlgo, err := selectSignatureScheme(cfg.localSignatureSchemes, certificate.PrivateKey)
		if err != nil {
			return nil, &alert{alertLevelFatal, alertInsufficientSecurity}, err
		}

		signature, err := generateKeySignature(clientRandom[:], serverRandom[:], state.localKeypair.publicKey, state.namedCurve, certificate.PrivateKey, signatureHashAlgo.hash)
		if err != nil {
			return nil, &alert{alertLevelFatal, alertInternalError}, err
		}
		state.localKeySignature = signature

		pkts = append(pkts, &packet{
			record: &recordLayer{
				recordLayerHeader: recordLayerHeader{
					protocolVersion: protocolVersion1_2,
				},
				content: &handshake{
					handshakeMessage: &handshakeMessageServerKeyExchange{
						ellipticCurveType:  ellipticCurveTypeNamedCurve,
						namedCurve:         state.namedCurve,
						publicKey:          state.localKeypair.publicKey,
						hashAlgorithm:      signatureHashAlgo.hash,
						signatureAlgorithm: signatureHashAlgo.signature,
						signature:          state.localKeySignature,
					},
				},
			},
		})

		if cfg.clientAuth > NoClientCert {
			pkts = append(pkts, &packet{
				record: &recordLayer{
					recordLayerHeader: recordLayerHeader{
						protocolVersion: protocolVersion1_2,
					},
					content: &handshake{
						handshakeMessage: &handshakeMessageCertificateRequest{
							certificateTypes:        []clientCertificateType{clientCertificateTypeRSASign, clientCertificateTypeECDSASign},
							signatureHashAlgorithms: cfg.localSignatureSchemes,
						},
					},
				},
			})
		}
	} else if cfg.localPSKIdentityHint != nil {
		// To help the client in selecting which identity to use, the server
		// can provide a "PSK identity hint" in the ServerKeyExchange message.
		// If no hint is provided, the ServerKeyExchange message is omitted.
		//
		// https://tools.ietf.org/html/rfc4279#section-2
		pkts = append(pkts, &packet{
			record: &recordLayer{
				recordLayerHeader: recordLayerHeader{
					protocolVersion: protocolVersion1_2,
				},
				content: &handshake{
					handshakeMessage: &handshakeMessageServerKeyExchange{
						identityHint: cfg.localPSKIdentityHint,
					},
				},
			},
		})
	}

	pkts = append(pkts, &packet{
		record: &recordLayer{
			recordLayerHeader: recordLayerHeader{
				protocolVersion: protocolVersion1_2,
			},
			content: &handshake{
				handshakeMessage: &handshakeMessageServerHelloDone{},
			},
		},
	})

	return pkts, nil, nil
}
