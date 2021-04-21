package webrtc

import (
	"net"

	"github.com/pion/ice/v2"
	"github.com/pion/logging"
)

// NewICETCPMux creates a new instance of ice.TCPMuxDefault. It enables use of
// passive ICE TCP candidates.
func NewICETCPMux(logger logging.LeveledLogger, listener net.Listener, readBufferSize int) ice.TCPMux {
	return ice.NewTCPMuxDefault(ice.TCPMuxParams{
		Listener:       listener,
		Logger:         logger,
		ReadBufferSize: readBufferSize,
	})
}
