package allocation

import (
	"net"
	"time"

	"github.com/pion/logging"
	"github.com/pion/turn/v2/internal/proto"
)

// ChannelBind represents a TURN Channel
// https://tools.ietf.org/html/rfc5766#section-2.5
type ChannelBind struct {
	Peer   net.Addr
	Number proto.ChannelNumber

	allocation    *Allocation
	lifetimeTimer *time.Timer
	log           logging.LeveledLogger
}

// NewChannelBind creates a new ChannelBind
func NewChannelBind(number proto.ChannelNumber, peer net.Addr, log logging.LeveledLogger) *ChannelBind {
	return &ChannelBind{
		Number: number,
		Peer:   peer,
		log:    log,
	}
}

func (c *ChannelBind) start(lifetime time.Duration) {
	c.lifetimeTimer = time.AfterFunc(lifetime, func() {
		if !c.allocation.RemoveChannelBind(c.Number) {
			c.log.Errorf("Failed to remove ChannelBind for %v %x %v", c.Number, c.Peer, c.allocation.fiveTuple)
		}
	})
}

func (c *ChannelBind) refresh(lifetime time.Duration) {
	if !c.lifetimeTimer.Reset(lifetime) {
		c.log.Errorf("Failed to reset ChannelBind timer for %v %x %v", c.Number, c.Peer, c.allocation.fiveTuple)
	}
}
