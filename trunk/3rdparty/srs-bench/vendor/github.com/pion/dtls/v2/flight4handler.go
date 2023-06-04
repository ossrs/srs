// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package dtls

import (
	"context"
	"crypto/rand"
	"crypto/x509"

	"github.com/pion/dtls/v2/internal/ciphersuite"
	"github.com/pion/dtls/v2/pkg/crypto/clientcertificate"
	"github.com/pion/dtls/v2/pkg/crypto/elliptic"
	"github.com/pion/dtls/v2/pkg/crypto/prf"
	"github.com/pion/dtls/v2/pkg/crypto/signaturehash"
	"github.com/pion/dtls/v2/pkg/protocol"
	"github.com/pion/dtls/v2/pkg/protocol/alert"
	"github.com/pion/dtls/v2/pkg/protocol/extension"
	"github.com/pion/dtls/v2/pkg/protocol/handshake"
	"github.com/pion/dtls/v2/pkg/protocol/recordlayer"
)

func flight4Parse(ctx context.Context, c flightConn, state *State, cache *handshakeCache, cfg *handshakeConfig) (flightVal, *alert.Alert, error) { //nolint:gocognit
	seq, msgs, ok := cache.fullPullMap(state.handshakeRecvSequence, state.cipherSuite,
		handshakeCachePullRule{handshake.TypeCertificate, cfg.initialEpoch, true, true},
		handshakeCachePullRule{handshake.TypeClientKeyExchange, cfg.initialEpoch, true, false},
		handshakeCachePullRule{handshake.TypeCertificateVerify, cfg.initialEpoch, true, true},
	)
	if !ok {
		// No valid message received. Keep reading
		return 0, nil, nil
	}

	// Validate type
	var clientKeyExchange *handshake.MessageClientKeyExchange
	if clientKeyExchange, ok = msgs[handshake.TypeClientKeyExchange].(*handshake.MessageClientKeyExchange); !ok {
		return 0, &alert.Alert{Level: alert.Fatal, Description: alert.InternalError}, nil
	}

	if h, hasCert := msgs[handshake.TypeCertificate].(*handshake.MessageCertificate); hasCert {
		state.PeerCertificates = h.Certificate
		// If the client offer its certificate, just disable session resumption.
		// Otherwise, we have to store the certificate identitfication and expire time.
		// And we have to check whether this certificate expired, revoked or changed.
		//
		// https://curl.se/docs/CVE-2016-5419.html
		state.SessionID = nil
	}

	if h, hasCertVerify := msgs[handshake.TypeCertificateVerify].(*handshake.MessageCertificateVerify); hasCertVerify {
		if state.PeerCertificates == nil {
			return 0, &alert.Alert{Level: alert.Fatal, Description: alert.NoCertificate}, errCertificateVerifyNoCertificate
		}

		plainText := cache.pullAndMerge(
			handshakeCachePullRule{handshake.TypeClientHello, cfg.initialEpoch, true, false},
			handshakeCachePullRule{handshake.TypeServerHello, cfg.initialEpoch, false, false},
			handshakeCachePullRule{handshake.TypeCertificate, cfg.initialEpoch, false, false},
			handshakeCachePullRule{handshake.TypeServerKeyExchange, cfg.initialEpoch, false, false},
			handshakeCachePullRule{handshake.TypeCertificateRequest, cfg.initialEpoch, false, false},
			handshakeCachePullRule{handshake.TypeServerHelloDone, cfg.initialEpoch, false, false},
			handshakeCachePullRule{handshake.TypeCertificate, cfg.initialEpoch, true, false},
			handshakeCachePullRule{handshake.TypeClientKeyExchange, cfg.initialEpoch, true, false},
		)

		// Verify that the pair of hash algorithm and signiture is listed.
		var validSignatureScheme bool
		for _, ss := range cfg.localSignatureSchemes {
			if ss.Hash == h.HashAlgorithm && ss.Signature == h.SignatureAlgorithm {
				validSignatureScheme = true
				break
			}
		}
		if !validSignatureScheme {
			return 0, &alert.Alert{Level: alert.Fatal, Description: alert.InsufficientSecurity}, errNoAvailableSignatureSchemes
		}

		if err := verifyCertificateVerify(plainText, h.HashAlgorithm, h.Signature, state.PeerCertificates); err != nil {
			return 0, &alert.Alert{Level: alert.Fatal, Description: alert.BadCertificate}, err
		}
		var chains [][]*x509.Certificate
		var err error
		var verified bool
		if cfg.clientAuth >= VerifyClientCertIfGiven {
			if chains, err = verifyClientCert(state.PeerCertificates, cfg.clientCAs); err != nil {
				return 0, &alert.Alert{Level: alert.Fatal, Description: alert.BadCertificate}, err
			}
			verified = true
		}
		if cfg.verifyPeerCertificate != nil {
			if err := cfg.verifyPeerCertificate(state.PeerCertificates, chains); err != nil {
				return 0, &alert.Alert{Level: alert.Fatal, Description: alert.BadCertificate}, err
			}
		}
		state.peerCertificatesVerified = verified
	} else if state.PeerCertificates != nil {
		// A certificate was received, but we haven't seen a CertificateVerify
		// keep reading until we receive one
		return 0, nil, nil
	}

	if !state.cipherSuite.IsInitialized() {
		serverRandom := state.localRandom.MarshalFixed()
		clientRandom := state.remoteRandom.MarshalFixed()

		var err error
		var preMasterSecret []byte
		if state.cipherSuite.AuthenticationType() == CipherSuiteAuthenticationTypePreSharedKey {
			var psk []byte
			if psk, err = cfg.localPSKCallback(clientKeyExchange.IdentityHint); err != nil {
				return 0, &alert.Alert{Level: alert.Fatal, Description: alert.InternalError}, err
			}
			state.IdentityHint = clientKeyExchange.IdentityHint
			switch state.cipherSuite.KeyExchangeAlgorithm() {
			case CipherSuiteKeyExchangeAlgorithmPsk:
				preMasterSecret = prf.PSKPreMasterSecret(psk)
			case (CipherSuiteKeyExchangeAlgorithmPsk | CipherSuiteKeyExchangeAlgorithmEcdhe):
				if preMasterSecret, err = prf.EcdhePSKPreMasterSecret(psk, clientKeyExchange.PublicKey, state.localKeypair.PrivateKey, state.localKeypair.Curve); err != nil {
					return 0, &alert.Alert{Level: alert.Fatal, Description: alert.InternalError}, err
				}
			default:
				return 0, &alert.Alert{Level: alert.Fatal, Description: alert.InternalError}, errInvalidCipherSuite
			}
		} else {
			preMasterSecret, err = prf.PreMasterSecret(clientKeyExchange.PublicKey, state.localKeypair.PrivateKey, state.localKeypair.Curve)
			if err != nil {
				return 0, &alert.Alert{Level: alert.Fatal, Description: alert.IllegalParameter}, err
			}
		}

		if state.extendedMasterSecret {
			var sessionHash []byte
			sessionHash, err = cache.sessionHash(state.cipherSuite.HashFunc(), cfg.initialEpoch)
			if err != nil {
				return 0, &alert.Alert{Level: alert.Fatal, Description: alert.InternalError}, err
			}

			state.masterSecret, err = prf.ExtendedMasterSecret(preMasterSecret, sessionHash, state.cipherSuite.HashFunc())
			if err != nil {
				return 0, &alert.Alert{Level: alert.Fatal, Description: alert.InternalError}, err
			}
		} else {
			state.masterSecret, err = prf.MasterSecret(preMasterSecret, clientRandom[:], serverRandom[:], state.cipherSuite.HashFunc())
			if err != nil {
				return 0, &alert.Alert{Level: alert.Fatal, Description: alert.InternalError}, err
			}
		}

		if err := state.cipherSuite.Init(state.masterSecret, clientRandom[:], serverRandom[:], false); err != nil {
			return 0, &alert.Alert{Level: alert.Fatal, Description: alert.InternalError}, err
		}
		cfg.writeKeyLog(keyLogLabelTLS12, clientRandom[:], state.masterSecret)
	}

	if len(state.SessionID) > 0 {
		s := Session{
			ID:     state.SessionID,
			Secret: state.masterSecret,
		}
		cfg.log.Tracef("[handshake] save new session: %x", s.ID)
		if err := cfg.sessionStore.Set(state.SessionID, s); err != nil {
			return 0, &alert.Alert{Level: alert.Fatal, Description: alert.InternalError}, err
		}
	}

	// Now, encrypted packets can be handled
	if err := c.handleQueuedPackets(ctx); err != nil {
		return 0, &alert.Alert{Level: alert.Fatal, Description: alert.InternalError}, err
	}

	seq, msgs, ok = cache.fullPullMap(seq, state.cipherSuite,
		handshakeCachePullRule{handshake.TypeFinished, cfg.initialEpoch + 1, true, false},
	)
	if !ok {
		// No valid message received. Keep reading
		return 0, nil, nil
	}
	state.handshakeRecvSequence = seq

	if _, ok = msgs[handshake.TypeFinished].(*handshake.MessageFinished); !ok {
		return 0, &alert.Alert{Level: alert.Fatal, Description: alert.InternalError}, nil
	}

	if state.cipherSuite.AuthenticationType() == CipherSuiteAuthenticationTypeAnonymous {
		if cfg.verifyConnection != nil {
			if err := cfg.verifyConnection(state.clone()); err != nil {
				return 0, &alert.Alert{Level: alert.Fatal, Description: alert.BadCertificate}, err
			}
		}
		return flight6, nil, nil
	}

	switch cfg.clientAuth {
	case RequireAnyClientCert:
		if state.PeerCertificates == nil {
			return 0, &alert.Alert{Level: alert.Fatal, Description: alert.NoCertificate}, errClientCertificateRequired
		}
	case VerifyClientCertIfGiven:
		if state.PeerCertificates != nil && !state.peerCertificatesVerified {
			return 0, &alert.Alert{Level: alert.Fatal, Description: alert.BadCertificate}, errClientCertificateNotVerified
		}
	case RequireAndVerifyClientCert:
		if state.PeerCertificates == nil {
			return 0, &alert.Alert{Level: alert.Fatal, Description: alert.NoCertificate}, errClientCertificateRequired
		}
		if !state.peerCertificatesVerified {
			return 0, &alert.Alert{Level: alert.Fatal, Description: alert.BadCertificate}, errClientCertificateNotVerified
		}
	case NoClientCert, RequestClientCert:
		// go to flight6
	}
	if cfg.verifyConnection != nil {
		if err := cfg.verifyConnection(state.clone()); err != nil {
			return 0, &alert.Alert{Level: alert.Fatal, Description: alert.BadCertificate}, err
		}
	}

	return flight6, nil, nil
}

func flight4Generate(_ flightConn, state *State, _ *handshakeCache, cfg *handshakeConfig) ([]*packet, *alert.Alert, error) {
	extensions := []extension.Extension{&extension.RenegotiationInfo{
		RenegotiatedConnection: 0,
	}}
	if (cfg.extendedMasterSecret == RequestExtendedMasterSecret ||
		cfg.extendedMasterSecret == RequireExtendedMasterSecret) && state.extendedMasterSecret {
		extensions = append(extensions, &extension.UseExtendedMasterSecret{
			Supported: true,
		})
	}
	if state.srtpProtectionProfile != 0 {
		extensions = append(extensions, &extension.UseSRTP{
			ProtectionProfiles: []SRTPProtectionProfile{state.srtpProtectionProfile},
		})
	}
	if state.cipherSuite.AuthenticationType() == CipherSuiteAuthenticationTypeCertificate {
		extensions = append(extensions, &extension.SupportedPointFormats{
			PointFormats: []elliptic.CurvePointFormat{elliptic.CurvePointFormatUncompressed},
		})
	}

	selectedProto, err := extension.ALPNProtocolSelection(cfg.supportedProtocols, state.peerSupportedProtocols)
	if err != nil {
		return nil, &alert.Alert{Level: alert.Fatal, Description: alert.NoApplicationProtocol}, err
	}
	if selectedProto != "" {
		extensions = append(extensions, &extension.ALPN{
			ProtocolNameList: []string{selectedProto},
		})
		state.NegotiatedProtocol = selectedProto
	}

	var pkts []*packet
	cipherSuiteID := uint16(state.cipherSuite.ID())

	if cfg.sessionStore != nil {
		state.SessionID = make([]byte, sessionLength)
		if _, err := rand.Read(state.SessionID); err != nil {
			return nil, &alert.Alert{Level: alert.Fatal, Description: alert.InternalError}, err
		}
	}

	pkts = append(pkts, &packet{
		record: &recordlayer.RecordLayer{
			Header: recordlayer.Header{
				Version: protocol.Version1_2,
			},
			Content: &handshake.Handshake{
				Message: &handshake.MessageServerHello{
					Version:           protocol.Version1_2,
					Random:            state.localRandom,
					SessionID:         state.SessionID,
					CipherSuiteID:     &cipherSuiteID,
					CompressionMethod: defaultCompressionMethods()[0],
					Extensions:        extensions,
				},
			},
		},
	})

	switch {
	case state.cipherSuite.AuthenticationType() == CipherSuiteAuthenticationTypeCertificate:
		certificate, err := cfg.getCertificate(&ClientHelloInfo{
			ServerName:   state.serverName,
			CipherSuites: []ciphersuite.ID{state.cipherSuite.ID()},
		})
		if err != nil {
			return nil, &alert.Alert{Level: alert.Fatal, Description: alert.HandshakeFailure}, err
		}

		pkts = append(pkts, &packet{
			record: &recordlayer.RecordLayer{
				Header: recordlayer.Header{
					Version: protocol.Version1_2,
				},
				Content: &handshake.Handshake{
					Message: &handshake.MessageCertificate{
						Certificate: certificate.Certificate,
					},
				},
			},
		})

		serverRandom := state.localRandom.MarshalFixed()
		clientRandom := state.remoteRandom.MarshalFixed()

		// Find compatible signature scheme
		signatureHashAlgo, err := signaturehash.SelectSignatureScheme(cfg.localSignatureSchemes, certificate.PrivateKey)
		if err != nil {
			return nil, &alert.Alert{Level: alert.Fatal, Description: alert.InsufficientSecurity}, err
		}

		signature, err := generateKeySignature(clientRandom[:], serverRandom[:], state.localKeypair.PublicKey, state.namedCurve, certificate.PrivateKey, signatureHashAlgo.Hash)
		if err != nil {
			return nil, &alert.Alert{Level: alert.Fatal, Description: alert.InternalError}, err
		}
		state.localKeySignature = signature

		pkts = append(pkts, &packet{
			record: &recordlayer.RecordLayer{
				Header: recordlayer.Header{
					Version: protocol.Version1_2,
				},
				Content: &handshake.Handshake{
					Message: &handshake.MessageServerKeyExchange{
						EllipticCurveType:  elliptic.CurveTypeNamedCurve,
						NamedCurve:         state.namedCurve,
						PublicKey:          state.localKeypair.PublicKey,
						HashAlgorithm:      signatureHashAlgo.Hash,
						SignatureAlgorithm: signatureHashAlgo.Signature,
						Signature:          state.localKeySignature,
					},
				},
			},
		})

		if cfg.clientAuth > NoClientCert {
			// An empty list of certificateAuthorities signals to
			// the client that it may send any certificate in response
			// to our request. When we know the CAs we trust, then
			// we can send them down, so that the client can choose
			// an appropriate certificate to give to us.
			var certificateAuthorities [][]byte
			if cfg.clientCAs != nil {
				// nolint:staticcheck // ignoring tlsCert.RootCAs.Subjects is deprecated ERR because cert does not come from SystemCertPool and it's ok if certificate authorities is empty.
				certificateAuthorities = cfg.clientCAs.Subjects()
			}
			pkts = append(pkts, &packet{
				record: &recordlayer.RecordLayer{
					Header: recordlayer.Header{
						Version: protocol.Version1_2,
					},
					Content: &handshake.Handshake{
						Message: &handshake.MessageCertificateRequest{
							CertificateTypes:            []clientcertificate.Type{clientcertificate.RSASign, clientcertificate.ECDSASign},
							SignatureHashAlgorithms:     cfg.localSignatureSchemes,
							CertificateAuthoritiesNames: certificateAuthorities,
						},
					},
				},
			})
		}
	case cfg.localPSKIdentityHint != nil || state.cipherSuite.KeyExchangeAlgorithm().Has(CipherSuiteKeyExchangeAlgorithmEcdhe):
		// To help the client in selecting which identity to use, the server
		// can provide a "PSK identity hint" in the ServerKeyExchange message.
		// If no hint is provided and cipher suite doesn't use elliptic curve,
		// the ServerKeyExchange message is omitted.
		//
		// https://tools.ietf.org/html/rfc4279#section-2
		srvExchange := &handshake.MessageServerKeyExchange{
			IdentityHint: cfg.localPSKIdentityHint,
		}
		if state.cipherSuite.KeyExchangeAlgorithm().Has(CipherSuiteKeyExchangeAlgorithmEcdhe) {
			srvExchange.EllipticCurveType = elliptic.CurveTypeNamedCurve
			srvExchange.NamedCurve = state.namedCurve
			srvExchange.PublicKey = state.localKeypair.PublicKey
		}
		pkts = append(pkts, &packet{
			record: &recordlayer.RecordLayer{
				Header: recordlayer.Header{
					Version: protocol.Version1_2,
				},
				Content: &handshake.Handshake{
					Message: srvExchange,
				},
			},
		})
	}

	pkts = append(pkts, &packet{
		record: &recordlayer.RecordLayer{
			Header: recordlayer.Header{
				Version: protocol.Version1_2,
			},
			Content: &handshake.Handshake{
				Message: &handshake.MessageServerHelloDone{},
			},
		},
	})

	return pkts, nil, nil
}
