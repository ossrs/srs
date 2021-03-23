package mdns

import "errors"

var (
	errJoiningMulticastGroup = errors.New("mDNS: failed to join multicast group")
	errConnectionClosed      = errors.New("mDNS: connection is closed")
	errContextElapsed        = errors.New("mDNS: context has elapsed")
	errNilConfig             = errors.New("mDNS: config must not be nil")
)
