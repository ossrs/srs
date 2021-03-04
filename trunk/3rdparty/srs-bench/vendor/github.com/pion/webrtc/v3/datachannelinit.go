package webrtc

// DataChannelInit can be used to configure properties of the underlying
// channel such as data reliability.
type DataChannelInit struct {
	// Ordered indicates if data is allowed to be delivered out of order. The
	// default value of true, guarantees that data will be delivered in order.
	Ordered *bool

	// MaxPacketLifeTime limits the time (in milliseconds) during which the
	// channel will transmit or retransmit data if not acknowledged. This value
	// may be clamped if it exceeds the maximum value supported.
	MaxPacketLifeTime *uint16

	// MaxRetransmits limits the number of times a channel will retransmit data
	// if not successfully delivered. This value may be clamped if it exceeds
	// the maximum value supported.
	MaxRetransmits *uint16

	// Protocol describes the subprotocol name used for this channel.
	Protocol *string

	// Negotiated describes if the data channel is created by the local peer or
	// the remote peer. The default value of false tells the user agent to
	// announce the channel in-band and instruct the other peer to dispatch a
	// corresponding DataChannel. If set to true, it is up to the application
	// to negotiate the channel and create an DataChannel with the same id
	// at the other peer.
	Negotiated *bool

	// ID overrides the default selection of ID for this channel.
	ID *uint16
}
