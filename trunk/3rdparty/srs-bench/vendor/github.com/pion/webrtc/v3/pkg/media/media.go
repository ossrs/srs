// Package media provides media writer and filters
package media

import (
	"time"

	"github.com/pion/rtp"
)

// A Sample contains encoded media and timing information
type Sample struct {
	Data      []byte
	Timestamp time.Time
	Duration  time.Duration
}

// Writer defines an interface to handle
// the creation of media files
type Writer interface {
	// Add the content of an RTP packet to the media
	WriteRTP(packet *rtp.Packet) error
	// Close the media
	// Note: Close implementation must be idempotent
	Close() error
}
