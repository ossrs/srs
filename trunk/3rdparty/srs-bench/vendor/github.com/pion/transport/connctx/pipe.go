package connctx

import (
	"net"
)

// Pipe creates piped pair of ConnCtx.
func Pipe() (ConnCtx, ConnCtx) {
	ca, cb := net.Pipe()
	return New(ca), New(cb)
}
