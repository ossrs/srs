package dtls

import (
	"bytes"
	"encoding/gob"
	"sync/atomic"

	"github.com/pion/dtls/v2/pkg/crypto/elliptic"
	"github.com/pion/dtls/v2/pkg/crypto/prf"
	"github.com/pion/dtls/v2/pkg/protocol/handshake"
	"github.com/pion/transport/replaydetector"
)

// State holds the dtls connection state and implements both encoding.BinaryMarshaler and encoding.BinaryUnmarshaler
type State struct {
	localEpoch, remoteEpoch   atomic.Value
	localSequenceNumber       []uint64 // uint48
	localRandom, remoteRandom handshake.Random
	masterSecret              []byte
	cipherSuite               CipherSuite // nil if a cipherSuite hasn't been chosen

	srtpProtectionProfile SRTPProtectionProfile // Negotiated SRTPProtectionProfile
	PeerCertificates      [][]byte
	IdentityHint          []byte

	isClient bool

	preMasterSecret      []byte
	extendedMasterSecret bool

	namedCurve                 elliptic.Curve
	localKeypair               *elliptic.Keypair
	cookie                     []byte
	handshakeSendSequence      int
	handshakeRecvSequence      int
	serverName                 string
	remoteRequestedCertificate bool   // Did we get a CertificateRequest
	localCertificatesVerify    []byte // cache CertificateVerify
	localVerifyData            []byte // cached VerifyData
	localKeySignature          []byte // cached keySignature
	peerCertificatesVerified   bool

	replayDetector []replaydetector.ReplayDetector
}

type serializedState struct {
	LocalEpoch            uint16
	RemoteEpoch           uint16
	LocalRandom           [handshake.RandomLength]byte
	RemoteRandom          [handshake.RandomLength]byte
	CipherSuiteID         uint16
	MasterSecret          []byte
	SequenceNumber        uint64
	SRTPProtectionProfile uint16
	PeerCertificates      [][]byte
	IdentityHint          []byte
	IsClient              bool
}

func (s *State) clone() *State {
	serialized := s.serialize()
	state := &State{}
	state.deserialize(*serialized)

	return state
}

func (s *State) serialize() *serializedState {
	// Marshal random values
	localRnd := s.localRandom.MarshalFixed()
	remoteRnd := s.remoteRandom.MarshalFixed()

	epoch := s.localEpoch.Load().(uint16)
	return &serializedState{
		LocalEpoch:            epoch,
		RemoteEpoch:           s.remoteEpoch.Load().(uint16),
		CipherSuiteID:         uint16(s.cipherSuite.ID()),
		MasterSecret:          s.masterSecret,
		SequenceNumber:        atomic.LoadUint64(&s.localSequenceNumber[epoch]),
		LocalRandom:           localRnd,
		RemoteRandom:          remoteRnd,
		SRTPProtectionProfile: uint16(s.srtpProtectionProfile),
		PeerCertificates:      s.PeerCertificates,
		IdentityHint:          s.IdentityHint,
		IsClient:              s.isClient,
	}
}

func (s *State) deserialize(serialized serializedState) {
	// Set epoch values
	epoch := serialized.LocalEpoch
	s.localEpoch.Store(serialized.LocalEpoch)
	s.remoteEpoch.Store(serialized.RemoteEpoch)

	for len(s.localSequenceNumber) <= int(epoch) {
		s.localSequenceNumber = append(s.localSequenceNumber, uint64(0))
	}

	// Set random values
	localRandom := &handshake.Random{}
	localRandom.UnmarshalFixed(serialized.LocalRandom)
	s.localRandom = *localRandom

	remoteRandom := &handshake.Random{}
	remoteRandom.UnmarshalFixed(serialized.RemoteRandom)
	s.remoteRandom = *remoteRandom

	s.isClient = serialized.IsClient

	// Set master secret
	s.masterSecret = serialized.MasterSecret

	// Set cipher suite
	s.cipherSuite = cipherSuiteForID(CipherSuiteID(serialized.CipherSuiteID), nil)

	atomic.StoreUint64(&s.localSequenceNumber[epoch], serialized.SequenceNumber)
	s.srtpProtectionProfile = SRTPProtectionProfile(serialized.SRTPProtectionProfile)

	// Set remote certificate
	s.PeerCertificates = serialized.PeerCertificates
	s.IdentityHint = serialized.IdentityHint
}

func (s *State) initCipherSuite() error {
	if s.cipherSuite.IsInitialized() {
		return nil
	}

	localRandom := s.localRandom.MarshalFixed()
	remoteRandom := s.remoteRandom.MarshalFixed()

	var err error
	if s.isClient {
		err = s.cipherSuite.Init(s.masterSecret, localRandom[:], remoteRandom[:], true)
	} else {
		err = s.cipherSuite.Init(s.masterSecret, remoteRandom[:], localRandom[:], false)
	}
	if err != nil {
		return err
	}
	return nil
}

// MarshalBinary is a binary.BinaryMarshaler.MarshalBinary implementation
func (s *State) MarshalBinary() ([]byte, error) {
	serialized := s.serialize()

	var buf bytes.Buffer
	enc := gob.NewEncoder(&buf)
	if err := enc.Encode(*serialized); err != nil {
		return nil, err
	}
	return buf.Bytes(), nil
}

// UnmarshalBinary is a binary.BinaryUnmarshaler.UnmarshalBinary implementation
func (s *State) UnmarshalBinary(data []byte) error {
	enc := gob.NewDecoder(bytes.NewBuffer(data))
	var serialized serializedState
	if err := enc.Decode(&serialized); err != nil {
		return err
	}

	s.deserialize(serialized)
	if err := s.initCipherSuite(); err != nil {
		return err
	}
	return nil
}

// ExportKeyingMaterial returns length bytes of exported key material in a new
// slice as defined in RFC 5705.
// This allows protocols to use DTLS for key establishment, but
// then use some of the keying material for their own purposes
func (s *State) ExportKeyingMaterial(label string, context []byte, length int) ([]byte, error) {
	if s.localEpoch.Load().(uint16) == 0 {
		return nil, errHandshakeInProgress
	} else if len(context) != 0 {
		return nil, errContextUnsupported
	} else if _, ok := invalidKeyingLabels()[label]; ok {
		return nil, errReservedExportKeyingMaterial
	}

	localRandom := s.localRandom.MarshalFixed()
	remoteRandom := s.remoteRandom.MarshalFixed()

	seed := []byte(label)
	if s.isClient {
		seed = append(append(seed, localRandom[:]...), remoteRandom[:]...)
	} else {
		seed = append(append(seed, remoteRandom[:]...), localRandom[:]...)
	}
	return prf.PHash(s.masterSecret, seed, length, s.cipherSuite.HashFunc())
}
