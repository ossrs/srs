package dtls

import (
	"bytes"
	"context"
	"crypto"
	"crypto/x509"

	"github.com/pion/dtls/v2/pkg/crypto/prf"
	"github.com/pion/dtls/v2/pkg/crypto/signaturehash"
	"github.com/pion/dtls/v2/pkg/protocol"
	"github.com/pion/dtls/v2/pkg/protocol/alert"
	"github.com/pion/dtls/v2/pkg/protocol/handshake"
	"github.com/pion/dtls/v2/pkg/protocol/recordlayer"
)

func flight5Parse(ctx context.Context, c flightConn, state *State, cache *handshakeCache, cfg *handshakeConfig) (flightVal, *alert.Alert, error) {
	_, msgs, ok := cache.fullPullMap(state.handshakeRecvSequence,
		handshakeCachePullRule{handshake.TypeFinished, cfg.initialEpoch + 1, false, false},
	)
	if !ok {
		// No valid message received. Keep reading
		return 0, nil, nil
	}

	var finished *handshake.MessageFinished
	if finished, ok = msgs[handshake.TypeFinished].(*handshake.MessageFinished); !ok {
		return 0, &alert.Alert{Level: alert.Fatal, Description: alert.InternalError}, nil
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
		handshakeCachePullRule{handshake.TypeCertificateVerify, cfg.initialEpoch, true, false},
		handshakeCachePullRule{handshake.TypeFinished, cfg.initialEpoch + 1, true, false},
	)

	expectedVerifyData, err := prf.VerifyDataServer(state.masterSecret, plainText, state.cipherSuite.HashFunc())
	if err != nil {
		return 0, &alert.Alert{Level: alert.Fatal, Description: alert.InternalError}, err
	}
	if !bytes.Equal(expectedVerifyData, finished.VerifyData) {
		return 0, &alert.Alert{Level: alert.Fatal, Description: alert.HandshakeFailure}, errVerifyDataMismatch
	}

	return flight5, nil, nil
}

func flight5Generate(c flightConn, state *State, cache *handshakeCache, cfg *handshakeConfig) ([]*packet, *alert.Alert, error) { //nolint:gocognit
	var certBytes [][]byte
	var privateKey crypto.PrivateKey
	if len(cfg.localCertificates) > 0 {
		certificate, err := cfg.getCertificate(cfg.serverName)
		if err != nil {
			return nil, &alert.Alert{Level: alert.Fatal, Description: alert.HandshakeFailure}, err
		}
		certBytes = certificate.Certificate
		privateKey = certificate.PrivateKey
	}

	var pkts []*packet

	if state.remoteRequestedCertificate {
		pkts = append(pkts,
			&packet{
				record: &recordlayer.RecordLayer{
					Header: recordlayer.Header{
						Version: protocol.Version1_2,
					},
					Content: &handshake.Handshake{
						Message: &handshake.MessageCertificate{
							Certificate: certBytes,
						},
					},
				},
			})
	}

	clientKeyExchange := &handshake.MessageClientKeyExchange{}
	if cfg.localPSKCallback == nil {
		clientKeyExchange.PublicKey = state.localKeypair.PublicKey
	} else {
		clientKeyExchange.IdentityHint = cfg.localPSKIdentityHint
	}

	pkts = append(pkts,
		&packet{
			record: &recordlayer.RecordLayer{
				Header: recordlayer.Header{
					Version: protocol.Version1_2,
				},
				Content: &handshake.Handshake{
					Message: clientKeyExchange,
				},
			},
		})

	serverKeyExchangeData := cache.pullAndMerge(
		handshakeCachePullRule{handshake.TypeServerKeyExchange, cfg.initialEpoch, false, false},
	)

	serverKeyExchange := &handshake.MessageServerKeyExchange{}

	// handshakeMessageServerKeyExchange is optional for PSK
	if len(serverKeyExchangeData) == 0 {
		alertPtr, err := handleServerKeyExchange(c, state, cfg, &handshake.MessageServerKeyExchange{})
		if err != nil {
			return nil, alertPtr, err
		}
	} else {
		rawHandshake := &handshake.Handshake{}
		err := rawHandshake.Unmarshal(serverKeyExchangeData)
		if err != nil {
			return nil, &alert.Alert{Level: alert.Fatal, Description: alert.UnexpectedMessage}, err
		}

		switch h := rawHandshake.Message.(type) {
		case *handshake.MessageServerKeyExchange:
			serverKeyExchange = h
		default:
			return nil, &alert.Alert{Level: alert.Fatal, Description: alert.UnexpectedMessage}, errInvalidContentType
		}
	}

	// Append not-yet-sent packets
	merged := []byte{}
	seqPred := uint16(state.handshakeSendSequence)
	for _, p := range pkts {
		h, ok := p.record.Content.(*handshake.Handshake)
		if !ok {
			return nil, &alert.Alert{Level: alert.Fatal, Description: alert.InternalError}, errInvalidContentType
		}
		h.Header.MessageSequence = seqPred
		seqPred++
		raw, err := h.Marshal()
		if err != nil {
			return nil, &alert.Alert{Level: alert.Fatal, Description: alert.InternalError}, err
		}
		merged = append(merged, raw...)
	}

	if alertPtr, err := initalizeCipherSuite(state, cache, cfg, serverKeyExchange, merged); err != nil {
		return nil, alertPtr, err
	}

	// If the client has sent a certificate with signing ability, a digitally-signed
	// CertificateVerify message is sent to explicitly verify possession of the
	// private key in the certificate.
	if state.remoteRequestedCertificate && len(cfg.localCertificates) > 0 {
		plainText := append(cache.pullAndMerge(
			handshakeCachePullRule{handshake.TypeClientHello, cfg.initialEpoch, true, false},
			handshakeCachePullRule{handshake.TypeServerHello, cfg.initialEpoch, false, false},
			handshakeCachePullRule{handshake.TypeCertificate, cfg.initialEpoch, false, false},
			handshakeCachePullRule{handshake.TypeServerKeyExchange, cfg.initialEpoch, false, false},
			handshakeCachePullRule{handshake.TypeCertificateRequest, cfg.initialEpoch, false, false},
			handshakeCachePullRule{handshake.TypeServerHelloDone, cfg.initialEpoch, false, false},
			handshakeCachePullRule{handshake.TypeCertificate, cfg.initialEpoch, true, false},
			handshakeCachePullRule{handshake.TypeClientKeyExchange, cfg.initialEpoch, true, false},
		), merged...)

		// Find compatible signature scheme
		signatureHashAlgo, err := signaturehash.SelectSignatureScheme(cfg.localSignatureSchemes, privateKey)
		if err != nil {
			return nil, &alert.Alert{Level: alert.Fatal, Description: alert.InsufficientSecurity}, err
		}

		certVerify, err := generateCertificateVerify(plainText, privateKey, signatureHashAlgo.Hash)
		if err != nil {
			return nil, &alert.Alert{Level: alert.Fatal, Description: alert.InternalError}, err
		}
		state.localCertificatesVerify = certVerify

		p := &packet{
			record: &recordlayer.RecordLayer{
				Header: recordlayer.Header{
					Version: protocol.Version1_2,
				},
				Content: &handshake.Handshake{
					Message: &handshake.MessageCertificateVerify{
						HashAlgorithm:      signatureHashAlgo.Hash,
						SignatureAlgorithm: signatureHashAlgo.Signature,
						Signature:          state.localCertificatesVerify,
					},
				},
			},
		}
		pkts = append(pkts, p)

		h, ok := p.record.Content.(*handshake.Handshake)
		if !ok {
			return nil, &alert.Alert{Level: alert.Fatal, Description: alert.InternalError}, errInvalidContentType
		}
		h.Header.MessageSequence = seqPred
		// seqPred++ // this is the last use of seqPred
		raw, err := h.Marshal()
		if err != nil {
			return nil, &alert.Alert{Level: alert.Fatal, Description: alert.InternalError}, err
		}
		merged = append(merged, raw...)
	}

	pkts = append(pkts,
		&packet{
			record: &recordlayer.RecordLayer{
				Header: recordlayer.Header{
					Version: protocol.Version1_2,
				},
				Content: &protocol.ChangeCipherSpec{},
			},
		})

	if len(state.localVerifyData) == 0 {
		plainText := cache.pullAndMerge(
			handshakeCachePullRule{handshake.TypeClientHello, cfg.initialEpoch, true, false},
			handshakeCachePullRule{handshake.TypeServerHello, cfg.initialEpoch, false, false},
			handshakeCachePullRule{handshake.TypeCertificate, cfg.initialEpoch, false, false},
			handshakeCachePullRule{handshake.TypeServerKeyExchange, cfg.initialEpoch, false, false},
			handshakeCachePullRule{handshake.TypeCertificateRequest, cfg.initialEpoch, false, false},
			handshakeCachePullRule{handshake.TypeServerHelloDone, cfg.initialEpoch, false, false},
			handshakeCachePullRule{handshake.TypeCertificate, cfg.initialEpoch, true, false},
			handshakeCachePullRule{handshake.TypeClientKeyExchange, cfg.initialEpoch, true, false},
			handshakeCachePullRule{handshake.TypeCertificateVerify, cfg.initialEpoch, true, false},
			handshakeCachePullRule{handshake.TypeFinished, cfg.initialEpoch + 1, true, false},
		)

		var err error
		state.localVerifyData, err = prf.VerifyDataClient(state.masterSecret, append(plainText, merged...), state.cipherSuite.HashFunc())
		if err != nil {
			return nil, &alert.Alert{Level: alert.Fatal, Description: alert.InternalError}, err
		}
	}

	pkts = append(pkts,
		&packet{
			record: &recordlayer.RecordLayer{
				Header: recordlayer.Header{
					Version: protocol.Version1_2,
					Epoch:   1,
				},
				Content: &handshake.Handshake{
					Message: &handshake.MessageFinished{
						VerifyData: state.localVerifyData,
					},
				},
			},
			shouldEncrypt:            true,
			resetLocalSequenceNumber: true,
		})

	return pkts, nil, nil
}

func initalizeCipherSuite(state *State, cache *handshakeCache, cfg *handshakeConfig, h *handshake.MessageServerKeyExchange, sendingPlainText []byte) (*alert.Alert, error) { //nolint:gocognit
	if state.cipherSuite.IsInitialized() {
		return nil, nil
	}

	clientRandom := state.localRandom.MarshalFixed()
	serverRandom := state.remoteRandom.MarshalFixed()

	var err error

	if state.extendedMasterSecret {
		var sessionHash []byte
		sessionHash, err = cache.sessionHash(state.cipherSuite.HashFunc(), cfg.initialEpoch, sendingPlainText)
		if err != nil {
			return &alert.Alert{Level: alert.Fatal, Description: alert.InternalError}, err
		}

		state.masterSecret, err = prf.ExtendedMasterSecret(state.preMasterSecret, sessionHash, state.cipherSuite.HashFunc())
		if err != nil {
			return &alert.Alert{Level: alert.Fatal, Description: alert.IllegalParameter}, err
		}
	} else {
		state.masterSecret, err = prf.MasterSecret(state.preMasterSecret, clientRandom[:], serverRandom[:], state.cipherSuite.HashFunc())
		if err != nil {
			return &alert.Alert{Level: alert.Fatal, Description: alert.InternalError}, err
		}
	}

	if state.cipherSuite.AuthenticationType() == CipherSuiteAuthenticationTypeCertificate {
		// Verify that the pair of hash algorithm and signiture is listed.
		var validSignatureScheme bool
		for _, ss := range cfg.localSignatureSchemes {
			if ss.Hash == h.HashAlgorithm && ss.Signature == h.SignatureAlgorithm {
				validSignatureScheme = true
				break
			}
		}
		if !validSignatureScheme {
			return &alert.Alert{Level: alert.Fatal, Description: alert.InsufficientSecurity}, errNoAvailableSignatureSchemes
		}

		expectedMsg := valueKeyMessage(clientRandom[:], serverRandom[:], h.PublicKey, h.NamedCurve)
		if err = verifyKeySignature(expectedMsg, h.Signature, h.HashAlgorithm, state.PeerCertificates); err != nil {
			return &alert.Alert{Level: alert.Fatal, Description: alert.BadCertificate}, err
		}
		var chains [][]*x509.Certificate
		if !cfg.insecureSkipVerify {
			if chains, err = verifyServerCert(state.PeerCertificates, cfg.rootCAs, cfg.serverName); err != nil {
				return &alert.Alert{Level: alert.Fatal, Description: alert.BadCertificate}, err
			}
		}
		if cfg.verifyPeerCertificate != nil {
			if err = cfg.verifyPeerCertificate(state.PeerCertificates, chains); err != nil {
				return &alert.Alert{Level: alert.Fatal, Description: alert.BadCertificate}, err
			}
		}
	}

	if err = state.cipherSuite.Init(state.masterSecret, clientRandom[:], serverRandom[:], true); err != nil {
		return &alert.Alert{Level: alert.Fatal, Description: alert.InternalError}, err
	}

	cfg.writeKeyLog(keyLogLabelTLS12, clientRandom[:], state.masterSecret)

	return nil, nil
}
