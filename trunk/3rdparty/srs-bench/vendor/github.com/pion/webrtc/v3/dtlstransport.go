// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

//go:build !js
// +build !js

package webrtc

import (
	"crypto/ecdsa"
	"crypto/elliptic"
	"crypto/rand"
	"crypto/tls"
	"crypto/x509"
	"errors"
	"fmt"
	"strings"
	"sync"
	"sync/atomic"
	"time"

	"github.com/pion/dtls/v2"
	"github.com/pion/dtls/v2/pkg/crypto/fingerprint"
	"github.com/pion/interceptor"
	"github.com/pion/logging"
	"github.com/pion/rtcp"
	"github.com/pion/srtp/v2"
	"github.com/pion/webrtc/v3/internal/mux"
	"github.com/pion/webrtc/v3/internal/util"
	"github.com/pion/webrtc/v3/pkg/rtcerr"
)

// DTLSTransport allows an application access to information about the DTLS
// transport over which RTP and RTCP packets are sent and received by
// RTPSender and RTPReceiver, as well other data such as SCTP packets sent
// and received by data channels.
type DTLSTransport struct {
	lock sync.RWMutex

	iceTransport          *ICETransport
	certificates          []Certificate
	remoteParameters      DTLSParameters
	remoteCertificate     []byte
	state                 DTLSTransportState
	srtpProtectionProfile srtp.ProtectionProfile

	onStateChangeHandler func(DTLSTransportState)

	conn *dtls.Conn

	srtpSession, srtcpSession   atomic.Value
	srtpEndpoint, srtcpEndpoint *mux.Endpoint
	simulcastStreams            []*srtp.ReadStreamSRTP
	srtpReady                   chan struct{}

	dtlsMatcher mux.MatchFunc

	api *API
	log logging.LeveledLogger
}

// NewDTLSTransport creates a new DTLSTransport.
// This constructor is part of the ORTC API. It is not
// meant to be used together with the basic WebRTC API.
func (api *API) NewDTLSTransport(transport *ICETransport, certificates []Certificate) (*DTLSTransport, error) {
	t := &DTLSTransport{
		iceTransport: transport,
		api:          api,
		state:        DTLSTransportStateNew,
		dtlsMatcher:  mux.MatchDTLS,
		srtpReady:    make(chan struct{}),
		log:          api.settingEngine.LoggerFactory.NewLogger("DTLSTransport"),
	}

	if len(certificates) > 0 {
		now := time.Now()
		for _, x509Cert := range certificates {
			if !x509Cert.Expires().IsZero() && now.After(x509Cert.Expires()) {
				return nil, &rtcerr.InvalidAccessError{Err: ErrCertificateExpired}
			}
			t.certificates = append(t.certificates, x509Cert)
		}
	} else {
		sk, err := ecdsa.GenerateKey(elliptic.P256(), rand.Reader)
		if err != nil {
			return nil, &rtcerr.UnknownError{Err: err}
		}
		certificate, err := GenerateCertificate(sk)
		if err != nil {
			return nil, err
		}
		t.certificates = []Certificate{*certificate}
	}

	return t, nil
}

// ICETransport returns the currently-configured *ICETransport or nil
// if one has not been configured
func (t *DTLSTransport) ICETransport() *ICETransport {
	t.lock.RLock()
	defer t.lock.RUnlock()
	return t.iceTransport
}

// onStateChange requires the caller holds the lock
func (t *DTLSTransport) onStateChange(state DTLSTransportState) {
	t.state = state
	handler := t.onStateChangeHandler
	if handler != nil {
		handler(state)
	}
}

// OnStateChange sets a handler that is fired when the DTLS
// connection state changes.
func (t *DTLSTransport) OnStateChange(f func(DTLSTransportState)) {
	t.lock.Lock()
	defer t.lock.Unlock()
	t.onStateChangeHandler = f
}

// State returns the current dtls transport state.
func (t *DTLSTransport) State() DTLSTransportState {
	t.lock.RLock()
	defer t.lock.RUnlock()
	return t.state
}

// WriteRTCP sends a user provided RTCP packet to the connected peer. If no peer is connected the
// packet is discarded.
func (t *DTLSTransport) WriteRTCP(pkts []rtcp.Packet) (int, error) {
	raw, err := rtcp.Marshal(pkts)
	if err != nil {
		return 0, err
	}

	srtcpSession, err := t.getSRTCPSession()
	if err != nil {
		return 0, err
	}

	writeStream, err := srtcpSession.OpenWriteStream()
	if err != nil {
		// nolint
		return 0, fmt.Errorf("%w: %v", errPeerConnWriteRTCPOpenWriteStream, err)
	}

	return writeStream.Write(raw)
}

// GetLocalParameters returns the DTLS parameters of the local DTLSTransport upon construction.
func (t *DTLSTransport) GetLocalParameters() (DTLSParameters, error) {
	fingerprints := []DTLSFingerprint{}

	for _, c := range t.certificates {
		prints, err := c.GetFingerprints()
		if err != nil {
			return DTLSParameters{}, err
		}

		fingerprints = append(fingerprints, prints...)
	}

	return DTLSParameters{
		Role:         DTLSRoleAuto, // always returns the default role
		Fingerprints: fingerprints,
	}, nil
}

// GetRemoteCertificate returns the certificate chain in use by the remote side
// returns an empty list prior to selection of the remote certificate
func (t *DTLSTransport) GetRemoteCertificate() []byte {
	t.lock.RLock()
	defer t.lock.RUnlock()
	return t.remoteCertificate
}

func (t *DTLSTransport) startSRTP() error {
	srtpConfig := &srtp.Config{
		Profile:       t.srtpProtectionProfile,
		BufferFactory: t.api.settingEngine.BufferFactory,
		LoggerFactory: t.api.settingEngine.LoggerFactory,
	}
	if t.api.settingEngine.replayProtection.SRTP != nil {
		srtpConfig.RemoteOptions = append(
			srtpConfig.RemoteOptions,
			srtp.SRTPReplayProtection(*t.api.settingEngine.replayProtection.SRTP),
		)
	}

	if t.api.settingEngine.disableSRTPReplayProtection {
		srtpConfig.RemoteOptions = append(
			srtpConfig.RemoteOptions,
			srtp.SRTPNoReplayProtection(),
		)
	}

	if t.api.settingEngine.replayProtection.SRTCP != nil {
		srtpConfig.RemoteOptions = append(
			srtpConfig.RemoteOptions,
			srtp.SRTCPReplayProtection(*t.api.settingEngine.replayProtection.SRTCP),
		)
	}

	if t.api.settingEngine.disableSRTCPReplayProtection {
		srtpConfig.RemoteOptions = append(
			srtpConfig.RemoteOptions,
			srtp.SRTCPNoReplayProtection(),
		)
	}

	connState := t.conn.ConnectionState()
	err := srtpConfig.ExtractSessionKeysFromDTLS(&connState, t.role() == DTLSRoleClient)
	if err != nil {
		// nolint
		return fmt.Errorf("%w: %v", errDtlsKeyExtractionFailed, err)
	}

	srtpSession, err := srtp.NewSessionSRTP(t.srtpEndpoint, srtpConfig)
	if err != nil {
		// nolint
		return fmt.Errorf("%w: %v", errFailedToStartSRTP, err)
	}

	srtcpSession, err := srtp.NewSessionSRTCP(t.srtcpEndpoint, srtpConfig)
	if err != nil {
		// nolint
		return fmt.Errorf("%w: %v", errFailedToStartSRTCP, err)
	}

	t.srtpSession.Store(srtpSession)
	t.srtcpSession.Store(srtcpSession)
	close(t.srtpReady)
	return nil
}

func (t *DTLSTransport) getSRTPSession() (*srtp.SessionSRTP, error) {
	if value, ok := t.srtpSession.Load().(*srtp.SessionSRTP); ok {
		return value, nil
	}

	return nil, errDtlsTransportNotStarted
}

func (t *DTLSTransport) getSRTCPSession() (*srtp.SessionSRTCP, error) {
	if value, ok := t.srtcpSession.Load().(*srtp.SessionSRTCP); ok {
		return value, nil
	}

	return nil, errDtlsTransportNotStarted
}

func (t *DTLSTransport) role() DTLSRole {
	// If remote has an explicit role use the inverse
	switch t.remoteParameters.Role {
	case DTLSRoleClient:
		return DTLSRoleServer
	case DTLSRoleServer:
		return DTLSRoleClient
	default:
	}

	// If SettingEngine has an explicit role
	switch t.api.settingEngine.answeringDTLSRole {
	case DTLSRoleServer:
		return DTLSRoleServer
	case DTLSRoleClient:
		return DTLSRoleClient
	default:
	}

	// Remote was auto and no explicit role was configured via SettingEngine
	if t.iceTransport.Role() == ICERoleControlling {
		return DTLSRoleServer
	}
	return defaultDtlsRoleAnswer
}

// Start DTLS transport negotiation with the parameters of the remote DTLS transport
func (t *DTLSTransport) Start(remoteParameters DTLSParameters) error {
	// Take lock and prepare connection, we must not hold the lock
	// when connecting
	prepareTransport := func() (DTLSRole, *dtls.Config, error) {
		t.lock.Lock()
		defer t.lock.Unlock()

		if err := t.ensureICEConn(); err != nil {
			return DTLSRole(0), nil, err
		}

		if t.state != DTLSTransportStateNew {
			return DTLSRole(0), nil, &rtcerr.InvalidStateError{Err: fmt.Errorf("%w: %s", errInvalidDTLSStart, t.state)}
		}

		t.srtpEndpoint = t.iceTransport.newEndpoint(mux.MatchSRTP)
		t.srtcpEndpoint = t.iceTransport.newEndpoint(mux.MatchSRTCP)
		t.remoteParameters = remoteParameters

		cert := t.certificates[0]
		t.onStateChange(DTLSTransportStateConnecting)

		return t.role(), &dtls.Config{
			Certificates: []tls.Certificate{
				{
					Certificate: [][]byte{cert.x509Cert.Raw},
					PrivateKey:  cert.privateKey,
				},
			},
			SRTPProtectionProfiles: func() []dtls.SRTPProtectionProfile {
				if len(t.api.settingEngine.srtpProtectionProfiles) > 0 {
					return t.api.settingEngine.srtpProtectionProfiles
				}

				return defaultSrtpProtectionProfiles()
			}(),
			ClientAuth:         dtls.RequireAnyClientCert,
			LoggerFactory:      t.api.settingEngine.LoggerFactory,
			InsecureSkipVerify: !t.api.settingEngine.dtls.disableInsecureSkipVerify,
		}, nil
	}

	var dtlsConn *dtls.Conn
	dtlsEndpoint := t.iceTransport.newEndpoint(mux.MatchDTLS)
	role, dtlsConfig, err := prepareTransport()
	if err != nil {
		return err
	}

	if t.api.settingEngine.replayProtection.DTLS != nil {
		dtlsConfig.ReplayProtectionWindow = int(*t.api.settingEngine.replayProtection.DTLS)
	}

	if t.api.settingEngine.dtls.clientAuth != nil {
		dtlsConfig.ClientAuth = *t.api.settingEngine.dtls.clientAuth
	}

	dtlsConfig.FlightInterval = t.api.settingEngine.dtls.retransmissionInterval
	dtlsConfig.InsecureSkipVerifyHello = t.api.settingEngine.dtls.insecureSkipHelloVerify
	dtlsConfig.EllipticCurves = t.api.settingEngine.dtls.ellipticCurves
	dtlsConfig.ConnectContextMaker = t.api.settingEngine.dtls.connectContextMaker
	dtlsConfig.ExtendedMasterSecret = t.api.settingEngine.dtls.extendedMasterSecret
	dtlsConfig.ClientCAs = t.api.settingEngine.dtls.clientCAs
	dtlsConfig.RootCAs = t.api.settingEngine.dtls.rootCAs

	// Connect as DTLS Client/Server, function is blocking and we
	// must not hold the DTLSTransport lock
	if role == DTLSRoleClient {
		dtlsConn, err = dtls.Client(dtlsEndpoint, dtlsConfig)
	} else {
		dtlsConn, err = dtls.Server(dtlsEndpoint, dtlsConfig)
	}

	// Re-take the lock, nothing beyond here is blocking
	t.lock.Lock()
	defer t.lock.Unlock()

	if err != nil {
		t.onStateChange(DTLSTransportStateFailed)
		return err
	}

	srtpProfile, ok := dtlsConn.SelectedSRTPProtectionProfile()
	if !ok {
		t.onStateChange(DTLSTransportStateFailed)
		return ErrNoSRTPProtectionProfile
	}

	switch srtpProfile {
	case dtls.SRTP_AEAD_AES_128_GCM:
		t.srtpProtectionProfile = srtp.ProtectionProfileAeadAes128Gcm
	case dtls.SRTP_AEAD_AES_256_GCM:
		t.srtpProtectionProfile = srtp.ProtectionProfileAeadAes256Gcm
	case dtls.SRTP_AES128_CM_HMAC_SHA1_80:
		t.srtpProtectionProfile = srtp.ProtectionProfileAes128CmHmacSha1_80
	default:
		t.onStateChange(DTLSTransportStateFailed)
		return ErrNoSRTPProtectionProfile
	}

	// Check the fingerprint if a certificate was exchanged
	remoteCerts := dtlsConn.ConnectionState().PeerCertificates
	if len(remoteCerts) == 0 {
		t.onStateChange(DTLSTransportStateFailed)
		return errNoRemoteCertificate
	}
	t.remoteCertificate = remoteCerts[0]

	if !t.api.settingEngine.disableCertificateFingerprintVerification {
		parsedRemoteCert, err := x509.ParseCertificate(t.remoteCertificate)
		if err != nil {
			if closeErr := dtlsConn.Close(); closeErr != nil {
				t.log.Error(err.Error())
			}

			t.onStateChange(DTLSTransportStateFailed)
			return err
		}

		if err = t.validateFingerPrint(parsedRemoteCert); err != nil {
			if closeErr := dtlsConn.Close(); closeErr != nil {
				t.log.Error(err.Error())
			}

			t.onStateChange(DTLSTransportStateFailed)
			return err
		}
	}

	t.conn = dtlsConn
	t.onStateChange(DTLSTransportStateConnected)

	return t.startSRTP()
}

// Stop stops and closes the DTLSTransport object.
func (t *DTLSTransport) Stop() error {
	t.lock.Lock()
	defer t.lock.Unlock()

	// Try closing everything and collect the errors
	var closeErrs []error

	if srtpSession, err := t.getSRTPSession(); err == nil && srtpSession != nil {
		closeErrs = append(closeErrs, srtpSession.Close())
	}

	if srtcpSession, err := t.getSRTCPSession(); err == nil && srtcpSession != nil {
		closeErrs = append(closeErrs, srtcpSession.Close())
	}

	for i := range t.simulcastStreams {
		closeErrs = append(closeErrs, t.simulcastStreams[i].Close())
	}

	if t.conn != nil {
		// dtls connection may be closed on sctp close.
		if err := t.conn.Close(); err != nil && !errors.Is(err, dtls.ErrConnClosed) {
			closeErrs = append(closeErrs, err)
		}
	}
	t.onStateChange(DTLSTransportStateClosed)
	return util.FlattenErrs(closeErrs)
}

func (t *DTLSTransport) validateFingerPrint(remoteCert *x509.Certificate) error {
	for _, fp := range t.remoteParameters.Fingerprints {
		hashAlgo, err := fingerprint.HashFromString(fp.Algorithm)
		if err != nil {
			return err
		}

		remoteValue, err := fingerprint.Fingerprint(remoteCert, hashAlgo)
		if err != nil {
			return err
		}

		if strings.EqualFold(remoteValue, fp.Value) {
			return nil
		}
	}

	return errNoMatchingCertificateFingerprint
}

func (t *DTLSTransport) ensureICEConn() error {
	if t.iceTransport == nil {
		return errICEConnectionNotStarted
	}

	return nil
}

func (t *DTLSTransport) storeSimulcastStream(s *srtp.ReadStreamSRTP) {
	t.lock.Lock()
	defer t.lock.Unlock()

	t.simulcastStreams = append(t.simulcastStreams, s)
}

func (t *DTLSTransport) streamsForSSRC(ssrc SSRC, streamInfo interceptor.StreamInfo) (*srtp.ReadStreamSRTP, interceptor.RTPReader, *srtp.ReadStreamSRTCP, interceptor.RTCPReader, error) {
	srtpSession, err := t.getSRTPSession()
	if err != nil {
		return nil, nil, nil, nil, err
	}

	rtpReadStream, err := srtpSession.OpenReadStream(uint32(ssrc))
	if err != nil {
		return nil, nil, nil, nil, err
	}

	rtpInterceptor := t.api.interceptor.BindRemoteStream(&streamInfo, interceptor.RTPReaderFunc(func(in []byte, a interceptor.Attributes) (n int, attributes interceptor.Attributes, err error) {
		n, err = rtpReadStream.Read(in)
		return n, a, err
	}))

	srtcpSession, err := t.getSRTCPSession()
	if err != nil {
		return nil, nil, nil, nil, err
	}

	rtcpReadStream, err := srtcpSession.OpenReadStream(uint32(ssrc))
	if err != nil {
		return nil, nil, nil, nil, err
	}

	rtcpInterceptor := t.api.interceptor.BindRTCPReader(interceptor.RTPReaderFunc(func(in []byte, a interceptor.Attributes) (n int, attributes interceptor.Attributes, err error) {
		n, err = rtcpReadStream.Read(in)
		return n, a, err
	}))

	return rtpReadStream, rtpInterceptor, rtcpReadStream, rtcpInterceptor, nil
}
