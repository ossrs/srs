package srtp

import (
	"io"
	"net"
	"sync"

	"github.com/pion/logging"
	"github.com/pion/transport/packetio"
)

type streamSession interface {
	Close() error
	write([]byte) (int, error)
	decrypt([]byte) error
}

type session struct {
	localContextMutex           sync.Mutex
	localContext, remoteContext *Context
	localOptions, remoteOptions []ContextOption

	newStream chan readStream

	started chan interface{}
	closed  chan interface{}

	readStreamsClosed bool
	readStreams       map[uint32]readStream
	readStreamsLock   sync.Mutex

	log           logging.LeveledLogger
	bufferFactory func(packetType packetio.BufferPacketType, ssrc uint32) io.ReadWriteCloser

	nextConn net.Conn
}

// Config is used to configure a session.
// You can provide either a KeyingMaterialExporter to export keys
// or directly pass the keys themselves.
// After a Config is passed to a session it must not be modified.
type Config struct {
	Keys          SessionKeys
	Profile       ProtectionProfile
	BufferFactory func(packetType packetio.BufferPacketType, ssrc uint32) io.ReadWriteCloser
	LoggerFactory logging.LoggerFactory

	// List of local/remote context options.
	// ReplayProtection is enabled on remote context by default.
	// Default replay protection window size is 64.
	LocalOptions, RemoteOptions []ContextOption
}

// SessionKeys bundles the keys required to setup an SRTP session
type SessionKeys struct {
	LocalMasterKey   []byte
	LocalMasterSalt  []byte
	RemoteMasterKey  []byte
	RemoteMasterSalt []byte
}

func (s *session) getOrCreateReadStream(ssrc uint32, child streamSession, proto func() readStream) (readStream, bool) {
	s.readStreamsLock.Lock()
	defer s.readStreamsLock.Unlock()

	if s.readStreamsClosed {
		return nil, false
	}

	r, ok := s.readStreams[ssrc]
	if ok {
		return r, false
	}

	// Create the readStream.
	r = proto()

	if err := r.init(child, ssrc); err != nil {
		return nil, false
	}

	s.readStreams[ssrc] = r
	return r, true
}

func (s *session) removeReadStream(ssrc uint32) {
	s.readStreamsLock.Lock()
	defer s.readStreamsLock.Unlock()

	if s.readStreamsClosed {
		return
	}

	delete(s.readStreams, ssrc)
}

func (s *session) close() error {
	if s.nextConn == nil {
		return nil
	} else if err := s.nextConn.Close(); err != nil {
		return err
	}

	<-s.closed
	return nil
}

func (s *session) start(localMasterKey, localMasterSalt, remoteMasterKey, remoteMasterSalt []byte, profile ProtectionProfile, child streamSession) error {
	var err error
	s.localContext, err = CreateContext(localMasterKey, localMasterSalt, profile, s.localOptions...)
	if err != nil {
		return err
	}

	s.remoteContext, err = CreateContext(remoteMasterKey, remoteMasterSalt, profile, s.remoteOptions...)
	if err != nil {
		return err
	}

	go func() {
		defer func() {
			close(s.newStream)

			s.readStreamsLock.Lock()
			s.readStreamsClosed = true
			s.readStreamsLock.Unlock()
			close(s.closed)
		}()

		b := make([]byte, 8192)
		for {
			var i int
			i, err = s.nextConn.Read(b)
			if err != nil {
				if err != io.EOF {
					s.log.Error(err.Error())
				}
				return
			}

			if err = child.decrypt(b[:i]); err != nil {
				s.log.Info(err.Error())
			}
		}
	}()

	close(s.started)

	return nil
}
