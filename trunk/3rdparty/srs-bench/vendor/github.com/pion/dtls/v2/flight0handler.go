package dtls

import (
	"context"
	"crypto/rand"
)

func flight0Parse(ctx context.Context, c flightConn, state *State, cache *handshakeCache, cfg *handshakeConfig) (flightVal, *alert, error) {
	seq, msgs, ok := cache.fullPullMap(0,
		handshakeCachePullRule{handshakeTypeClientHello, cfg.initialEpoch, true, false},
	)
	if !ok {
		// No valid message received. Keep reading
		return 0, nil, nil
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

	state.remoteRandom = clientHello.random

	if state.cipherSuite, ok = findMatchingCipherSuite(clientHello.cipherSuites, cfg.localCipherSuites); !ok {
		return 0, &alert{alertLevelFatal, alertInsufficientSecurity}, errCipherSuiteNoIntersection
	}

	for _, extension := range clientHello.extensions {
		switch e := extension.(type) {
		case *extensionSupportedEllipticCurves:
			if len(e.ellipticCurves) == 0 {
				return 0, &alert{alertLevelFatal, alertInsufficientSecurity}, errNoSupportedEllipticCurves
			}
			state.namedCurve = e.ellipticCurves[0]
		case *extensionUseSRTP:
			profile, ok := findMatchingSRTPProfile(e.protectionProfiles, cfg.localSRTPProtectionProfiles)
			if !ok {
				return 0, &alert{alertLevelFatal, alertInsufficientSecurity}, errServerNoMatchingSRTPProfile
			}
			state.srtpProtectionProfile = profile
		case *extensionUseExtendedMasterSecret:
			if cfg.extendedMasterSecret != DisableExtendedMasterSecret {
				state.extendedMasterSecret = true
			}
		case *extensionServerName:
			state.serverName = e.serverName // remote server name
		}
	}

	if cfg.extendedMasterSecret == RequireExtendedMasterSecret && !state.extendedMasterSecret {
		return 0, &alert{alertLevelFatal, alertInsufficientSecurity}, errServerRequiredButNoClientEMS
	}

	if state.localKeypair == nil {
		var err error
		state.localKeypair, err = generateKeypair(state.namedCurve)
		if err != nil {
			return 0, &alert{alertLevelFatal, alertIllegalParameter}, err
		}
	}

	return flight2, nil, nil
}

func flight0Generate(c flightConn, state *State, cache *handshakeCache, cfg *handshakeConfig) ([]*packet, *alert, error) {
	// Initialize
	state.cookie = make([]byte, cookieLength)
	if _, err := rand.Read(state.cookie); err != nil {
		return nil, nil, err
	}

	var zeroEpoch uint16
	state.localEpoch.Store(zeroEpoch)
	state.remoteEpoch.Store(zeroEpoch)
	state.namedCurve = defaultNamedCurve

	if err := state.localRandom.populate(); err != nil {
		return nil, nil, err
	}

	return nil, nil, nil
}
