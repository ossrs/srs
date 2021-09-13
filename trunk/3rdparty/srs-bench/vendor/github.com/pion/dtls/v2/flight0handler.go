package dtls

import (
	"context"
	"crypto/rand"

	"github.com/pion/dtls/v2/pkg/crypto/elliptic"
	"github.com/pion/dtls/v2/pkg/protocol"
	"github.com/pion/dtls/v2/pkg/protocol/alert"
	"github.com/pion/dtls/v2/pkg/protocol/extension"
	"github.com/pion/dtls/v2/pkg/protocol/handshake"
)

func flight0Parse(ctx context.Context, c flightConn, state *State, cache *handshakeCache, cfg *handshakeConfig) (flightVal, *alert.Alert, error) {
	seq, msgs, ok := cache.fullPullMap(0,
		handshakeCachePullRule{handshake.TypeClientHello, cfg.initialEpoch, true, false},
	)
	if !ok {
		// No valid message received. Keep reading
		return 0, nil, nil
	}
	state.handshakeRecvSequence = seq

	var clientHello *handshake.MessageClientHello

	// Validate type
	if clientHello, ok = msgs[handshake.TypeClientHello].(*handshake.MessageClientHello); !ok {
		return 0, &alert.Alert{Level: alert.Fatal, Description: alert.InternalError}, nil
	}

	if !clientHello.Version.Equal(protocol.Version1_2) {
		return 0, &alert.Alert{Level: alert.Fatal, Description: alert.ProtocolVersion}, errUnsupportedProtocolVersion
	}

	state.remoteRandom = clientHello.Random

	cipherSuites := []CipherSuite{}
	for _, id := range clientHello.CipherSuiteIDs {
		if c := cipherSuiteForID(CipherSuiteID(id), cfg.customCipherSuites); c != nil {
			cipherSuites = append(cipherSuites, c)
		}
	}

	if state.cipherSuite, ok = findMatchingCipherSuite(cipherSuites, cfg.localCipherSuites); !ok {
		return 0, &alert.Alert{Level: alert.Fatal, Description: alert.InsufficientSecurity}, errCipherSuiteNoIntersection
	}

	for _, val := range clientHello.Extensions {
		switch e := val.(type) {
		case *extension.SupportedEllipticCurves:
			if len(e.EllipticCurves) == 0 {
				return 0, &alert.Alert{Level: alert.Fatal, Description: alert.InsufficientSecurity}, errNoSupportedEllipticCurves
			}
			state.namedCurve = e.EllipticCurves[0]
		case *extension.UseSRTP:
			profile, ok := findMatchingSRTPProfile(e.ProtectionProfiles, cfg.localSRTPProtectionProfiles)
			if !ok {
				return 0, &alert.Alert{Level: alert.Fatal, Description: alert.InsufficientSecurity}, errServerNoMatchingSRTPProfile
			}
			state.srtpProtectionProfile = profile
		case *extension.UseExtendedMasterSecret:
			if cfg.extendedMasterSecret != DisableExtendedMasterSecret {
				state.extendedMasterSecret = true
			}
		case *extension.ServerName:
			state.serverName = e.ServerName // remote server name
		}
	}

	if cfg.extendedMasterSecret == RequireExtendedMasterSecret && !state.extendedMasterSecret {
		return 0, &alert.Alert{Level: alert.Fatal, Description: alert.InsufficientSecurity}, errServerRequiredButNoClientEMS
	}

	if state.localKeypair == nil {
		var err error
		state.localKeypair, err = elliptic.GenerateKeypair(state.namedCurve)
		if err != nil {
			return 0, &alert.Alert{Level: alert.Fatal, Description: alert.IllegalParameter}, err
		}
	}

	return flight2, nil, nil
}

func flight0Generate(c flightConn, state *State, cache *handshakeCache, cfg *handshakeConfig) ([]*packet, *alert.Alert, error) {
	// Initialize
	state.cookie = make([]byte, cookieLength)
	if _, err := rand.Read(state.cookie); err != nil {
		return nil, nil, err
	}

	var zeroEpoch uint16
	state.localEpoch.Store(zeroEpoch)
	state.remoteEpoch.Store(zeroEpoch)
	state.namedCurve = defaultNamedCurve

	if err := state.localRandom.Populate(); err != nil {
		return nil, nil, err
	}

	return nil, nil, nil
}
