// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package mdns

import (
	"net"
	"time"

	"github.com/pion/logging"
)

const (
	// DefaultAddressIPv4 is the default used by mDNS
	// and in most cases should be the address that the
	// ipv4.PacketConn passed to Server is bound to
	DefaultAddressIPv4 = "224.0.0.0:5353"

	// DefaultAddressIPv6 is the default IPv6 address used
	// by mDNS and in most cases should be the address that
	// the ipv6.PacketConn passed to Server is bound to
	DefaultAddressIPv6 = "[FF02::]:5353"
)

// Config is used to configure a mDNS client or server.
type Config struct {
	// Name is the name of the client/server used for logging purposes.
	Name string

	// QueryInterval controls how often we sends Queries until we
	// get a response for the requested name
	QueryInterval time.Duration

	// LocalNames are the names that we will generate answers for
	// when we get questions
	LocalNames []string

	// LocalAddress will override the published address with the given IP
	// when set. Otherwise, the automatically determined address will be used.
	LocalAddress net.IP

	LoggerFactory logging.LoggerFactory

	// IncludeLoopback will include loopback interfaces to be eligble for queries and answers.
	IncludeLoopback bool

	// Interfaces will override the interfaces used for queries and answers.
	Interfaces []net.Interface
}
