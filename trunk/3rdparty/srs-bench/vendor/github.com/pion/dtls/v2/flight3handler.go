package dtls

import (
	"context"

	"github.com/pion/dtls/v2/pkg/crypto/elliptic"
	"github.com/pion/dtls/v2/pkg/crypto/prf"
	"github.com/pion/dtls/v2/pkg/protocol"
	"github.com/pion/dtls/v2/pkg/protocol/alert"
	"github.com/pion/dtls/v2/pkg/protocol/extension"
	"github.com/pion/dtls/v2/pkg/protocol/handshake"
	"github.com/pion/dtls/v2/pkg/protocol/recordlayer"
)

func flight3Parse(ctx context.Context, c flightConn, state *State, cache *handshakeCache, cfg *handshakeConfig) (flightVal, *alert.Alert, error) { //nolint:gocognit
	// Clients may receive multiple HelloVerifyRequest messages with different cookies.
	// Clients SHOULD handle this by sending a new ClientHello with a cookie in response
	// to the new HelloVerifyRequest. RFC 6347 Section 4.2.1
	seq, msgs, ok := cache.fullPullMap(state.handshakeRecvSequence,
		handshakeCachePullRule{handshake.TypeHelloVerifyRequest, cfg.initialEpoch, false, true},
	)
	if ok {
		if h, msgOk := msgs[handshake.TypeHelloVerifyRequest].(*handshake.MessageHelloVerifyRequest); msgOk {
			// DTLS 1.2 clients must not assume that the server will use the protocol version
			// specified in HelloVerifyRequest message. RFC 6347 Section 4.2.1
			if !h.Version.Equal(protocol.Version1_0) && !h.Version.Equal(protocol.Version1_2) {
				return 0, &alert.Alert{Level: alert.Fatal, Description: alert.ProtocolVersion}, errUnsupportedProtocolVersion
			}
			state.cookie = append([]byte{}, h.Cookie...)
			state.handshakeRecvSequence = seq
			return flight3, nil, nil
		}
	}

	if cfg.localPSKCallback != nil {
		seq, msgs, ok = cache.fullPullMap(state.handshakeRecvSequence,
			handshakeCachePullRule{handshake.TypeServerHello, cfg.initialEpoch, false, false},
			handshakeCachePullRule{handshake.TypeServerKeyExchange, cfg.initialEpoch, false, true},
			handshakeCachePullRule{handshake.TypeServerHelloDone, cfg.initialEpoch, false, false},
		)
	} else {
		seq, msgs, ok = cache.fullPullMap(state.handshakeRecvSequence,
			handshakeCachePullRule{handshake.TypeServerHello, cfg.initialEpoch, false, false},
			handshakeCachePullRule{handshake.TypeCertificate, cfg.initialEpoch, false, true},
			handshakeCachePullRule{handshake.TypeServerKeyExchange, cfg.initialEpoch, false, false},
			handshakeCachePullRule{handshake.TypeCertificateRequest, cfg.initialEpoch, false, true},
			handshakeCachePullRule{handshake.TypeServerHelloDone, cfg.initialEpoch, false, false},
		)
	}
	if !ok {
		// Don't have enough messages. Keep reading
		return 0, nil, nil
	}
	state.handshakeRecvSequence = seq

	if h, ok := msgs[handshake.TypeServerHello].(*handshake.MessageServerHello); ok {
		if !h.Version.Equal(protocol.Version1_2) {
			return 0, &alert.Alert{Level: alert.Fatal, Description: alert.ProtocolVersion}, errUnsupportedProtocolVersion
		}
		for _, v := range h.Extensions {
			switch e := v.(type) {
			case *extension.UseSRTP:
				profile, ok := findMatchingSRTPProfile(e.ProtectionProfiles, cfg.localSRTPProtectionProfiles)
				if !ok {
					return 0, &alert.Alert{Level: alert.Fatal, Description: alert.IllegalParameter}, errClientNoMatchingSRTPProfile
				}
				state.srtpProtectionProfile = profile
			case *extension.UseExtendedMasterSecret:
				if cfg.extendedMasterSecret != DisableExtendedMasterSecret {
					state.extendedMasterSecret = true
				}
			}
		}
		if cfg.extendedMasterSecret == RequireExtendedMasterSecret && !state.extendedMasterSecret {
			return 0, &alert.Alert{Level: alert.Fatal, Description: alert.InsufficientSecurity}, errClientRequiredButNoServerEMS
		}
		if len(cfg.localSRTPProtectionProfiles) > 0 && state.srtpProtectionProfile == 0 {
			return 0, &alert.Alert{Level: alert.Fatal, Description: alert.InsufficientSecurity}, errRequestedButNoSRTPExtension
		}

		remoteCipherSuite := cipherSuiteForID(CipherSuiteID(*h.CipherSuiteID), cfg.customCipherSuites)
		if remoteCipherSuite == nil {
			return 0, &alert.Alert{Level: alert.Fatal, Description: alert.InsufficientSecurity}, errCipherSuiteNoIntersection
		}

		selectedCipherSuite, ok := findMatchingCipherSuite([]CipherSuite{remoteCipherSuite}, cfg.localCipherSuites)
		if !ok {
			return 0, &alert.Alert{Level: alert.Fatal, Description: alert.InsufficientSecurity}, errInvalidCipherSuite
		}

		state.cipherSuite = selectedCipherSuite
		state.remoteRandom = h.Random
		cfg.log.Tracef("[handshake] use cipher suite: %s", selectedCipherSuite.String())
	}

	if h, ok := msgs[handshake.TypeCertificate].(*handshake.MessageCertificate); ok {
		state.PeerCertificates = h.Certificate
	} else if state.cipherSuite.AuthenticationType() == CipherSuiteAuthenticationTypeCertificate {
		return 0, &alert.Alert{Level: alert.Fatal, Description: alert.NoCertificate}, errInvalidCertificate
	}

	if h, ok := msgs[handshake.TypeServerKeyExchange].(*handshake.MessageServerKeyExchange); ok {
		alertPtr, err := handleServerKeyExchange(c, state, cfg, h)
		if err != nil {
			return 0, alertPtr, err
		}
	}

	if _, ok := msgs[handshake.TypeCertificateRequest].(*handshake.MessageCertificateRequest); ok {
		state.remoteRequestedCertificate = true
	}

	return flight5, nil, nil
}

func handleServerKeyExchange(_ flightConn, state *State, cfg *handshakeConfig, h *handshake.MessageServerKeyExchange) (*alert.Alert, error) {
	var err error
	if cfg.localPSKCallback != nil {
		var psk []byte
		if psk, err = cfg.localPSKCallback(h.IdentityHint); err != nil {
			return &alert.Alert{Level: alert.Fatal, Description: alert.InternalError}, err
		}
		state.IdentityHint = h.IdentityHint
		state.preMasterSecret = prf.PSKPreMasterSecret(psk)
	} else {
		if state.localKeypair, err = elliptic.GenerateKeypair(h.NamedCurve); err != nil {
			return &alert.Alert{Level: alert.Fatal, Description: alert.InternalError}, err
		}

		if state.preMasterSecret, err = prf.PreMasterSecret(h.PublicKey, state.localKeypair.PrivateKey, state.localKeypair.Curve); err != nil {
			return &alert.Alert{Level: alert.Fatal, Description: alert.InternalError}, err
		}
	}

	return nil, nil
}

func flight3Generate(c flightConn, state *State, cache *handshakeCache, cfg *handshakeConfig) ([]*packet, *alert.Alert, error) {
	extensions := []extension.Extension{
		&extension.SupportedSignatureAlgorithms{
			SignatureHashAlgorithms: cfg.localSignatureSchemes,
		},
		&extension.RenegotiationInfo{
			RenegotiatedConnection: 0,
		},
	}
	if cfg.localPSKCallback == nil {
		extensions = append(extensions, []extension.Extension{
			&extension.SupportedEllipticCurves{
				EllipticCurves: []elliptic.Curve{elliptic.X25519, elliptic.P256, elliptic.P384},
			},
			&extension.SupportedPointFormats{
				PointFormats: []elliptic.CurvePointFormat{elliptic.CurvePointFormatUncompressed},
			},
		}...)
	}

	if len(cfg.localSRTPProtectionProfiles) > 0 {
		extensions = append(extensions, &extension.UseSRTP{
			ProtectionProfiles: cfg.localSRTPProtectionProfiles,
		})
	}

	if cfg.extendedMasterSecret == RequestExtendedMasterSecret ||
		cfg.extendedMasterSecret == RequireExtendedMasterSecret {
		extensions = append(extensions, &extension.UseExtendedMasterSecret{
			Supported: true,
		})
	}

	if len(cfg.serverName) > 0 {
		extensions = append(extensions, &extension.ServerName{ServerName: cfg.serverName})
	}

	return []*packet{
		{
			record: &recordlayer.RecordLayer{
				Header: recordlayer.Header{
					Version: protocol.Version1_2,
				},
				Content: &handshake.Handshake{
					Message: &handshake.MessageClientHello{
						Version:            protocol.Version1_2,
						Cookie:             state.cookie,
						Random:             state.localRandom,
						CipherSuiteIDs:     cipherSuiteIDs(cfg.localCipherSuites),
						CompressionMethods: defaultCompressionMethods(),
						Extensions:         extensions,
					},
				},
			},
		},
	}, nil, nil
}
