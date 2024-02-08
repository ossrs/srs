// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package srtp

import (
	"net"
	"time"

	"github.com/pion/logging"
	"github.com/pion/rtcp"
)

const defaultSessionSRTCPReplayProtectionWindow = 64

// SessionSRTCP implements io.ReadWriteCloser and provides a bi-directional SRTCP session
// SRTCP itself does not have a design like this, but it is common in most applications
// for local/remote to each have their own keying material. This provides those patterns
// instead of making everyone re-implement
type SessionSRTCP struct {
	session
	writeStream *WriteStreamSRTCP
}

// NewSessionSRTCP creates a SRTCP session using conn as the underlying transport.
func NewSessionSRTCP(conn net.Conn, config *Config) (*SessionSRTCP, error) { //nolint:dupl
	if config == nil {
		return nil, errNoConfig
	} else if conn == nil {
		return nil, errNoConn
	}

	loggerFactory := config.LoggerFactory
	if loggerFactory == nil {
		loggerFactory = logging.NewDefaultLoggerFactory()
	}

	localOpts := append(
		[]ContextOption{},
		config.LocalOptions...,
	)
	remoteOpts := append(
		[]ContextOption{
			// Default options
			SRTCPReplayProtection(defaultSessionSRTCPReplayProtectionWindow),
		},
		config.RemoteOptions...,
	)

	s := &SessionSRTCP{
		session: session{
			nextConn:            conn,
			localOptions:        localOpts,
			remoteOptions:       remoteOpts,
			readStreams:         map[uint32]readStream{},
			newStream:           make(chan readStream),
			acceptStreamTimeout: config.AcceptStreamTimeout,
			started:             make(chan interface{}),
			closed:              make(chan interface{}),
			bufferFactory:       config.BufferFactory,
			log:                 loggerFactory.NewLogger("srtp"),
		},
	}
	s.writeStream = &WriteStreamSRTCP{s}

	err := s.session.start(
		config.Keys.LocalMasterKey, config.Keys.LocalMasterSalt,
		config.Keys.RemoteMasterKey, config.Keys.RemoteMasterSalt,
		config.Profile,
		s,
	)
	if err != nil {
		return nil, err
	}
	return s, nil
}

// OpenWriteStream returns the global write stream for the Session
func (s *SessionSRTCP) OpenWriteStream() (*WriteStreamSRTCP, error) {
	return s.writeStream, nil
}

// OpenReadStream opens a read stream for the given SSRC, it can be used
// if you want a certain SSRC, but don't want to wait for AcceptStream
func (s *SessionSRTCP) OpenReadStream(ssrc uint32) (*ReadStreamSRTCP, error) {
	r, _ := s.session.getOrCreateReadStream(ssrc, s, newReadStreamSRTCP)

	if readStream, ok := r.(*ReadStreamSRTCP); ok {
		return readStream, nil
	}
	return nil, errFailedTypeAssertion
}

// AcceptStream returns a stream to handle RTCP for a single SSRC
func (s *SessionSRTCP) AcceptStream() (*ReadStreamSRTCP, uint32, error) {
	stream, ok := <-s.newStream
	if !ok {
		return nil, 0, errStreamAlreadyClosed
	}

	readStream, ok := stream.(*ReadStreamSRTCP)
	if !ok {
		return nil, 0, errFailedTypeAssertion
	}

	return readStream, stream.GetSSRC(), nil
}

// Close ends the session
func (s *SessionSRTCP) Close() error {
	return s.session.close()
}

// Private

func (s *SessionSRTCP) write(buf []byte) (int, error) {
	if _, ok := <-s.session.started; ok {
		return 0, errStartedChannelUsedIncorrectly
	}

	ibuf := bufferpool.Get()
	defer bufferpool.Put(ibuf)

	s.session.localContextMutex.Lock()
	encrypted, err := s.localContext.EncryptRTCP(ibuf.([]byte), buf, nil)
	s.session.localContextMutex.Unlock()

	if err != nil {
		return 0, err
	}
	return s.session.nextConn.Write(encrypted)
}

func (s *SessionSRTCP) setWriteDeadline(t time.Time) error {
	return s.session.nextConn.SetWriteDeadline(t)
}

// create a list of Destination SSRCs
// that's a superset of all Destinations in the slice.
func destinationSSRC(pkts []rtcp.Packet) []uint32 {
	ssrcSet := make(map[uint32]struct{})
	for _, p := range pkts {
		for _, ssrc := range p.DestinationSSRC() {
			ssrcSet[ssrc] = struct{}{}
		}
	}

	out := make([]uint32, 0, len(ssrcSet))
	for ssrc := range ssrcSet {
		out = append(out, ssrc)
	}

	return out
}

func (s *SessionSRTCP) decrypt(buf []byte) error {
	decrypted, err := s.remoteContext.DecryptRTCP(buf, buf, nil)
	if err != nil {
		return err
	}

	pkt, err := rtcp.Unmarshal(decrypted)
	if err != nil {
		return err
	}

	for _, ssrc := range destinationSSRC(pkt) {
		r, isNew := s.session.getOrCreateReadStream(ssrc, s, newReadStreamSRTCP)
		if r == nil {
			return nil // Session has been closed
		} else if isNew {
			if !s.session.acceptStreamTimeout.IsZero() {
				_ = s.session.nextConn.SetReadDeadline(time.Time{})
			}
			s.session.newStream <- r // Notify AcceptStream
		}

		readStream, ok := r.(*ReadStreamSRTCP)
		if !ok {
			return errFailedTypeAssertion
		}

		_, err = readStream.write(decrypted)
		if err != nil {
			return err
		}
	}

	return nil
}
