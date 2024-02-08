// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

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

// NewICEUDPMux creates a new instance of ice.UDPMuxDefault. It allows many PeerConnections to be served
// by a single UDP Port.
func NewICEUDPMux(logger logging.LeveledLogger, udpConn net.PacketConn) ice.UDPMux {
	return ice.NewUDPMuxDefault(ice.UDPMuxParams{
		UDPConn: udpConn,
		Logger:  logger,
	})
}
