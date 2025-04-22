// Package conn contains a RTSP connection implementation.
package conn

import (
	"bufio"
	"io"

	"github.com/bluenviron/gortsplib/v4/pkg/base"
)

const (
	readBufferSize = 4096
)

// Conn is a RTSP connection.
type Conn struct {
	w  io.Writer
	br *bufio.Reader

	// reuse interleaved frames. they should never be passed to secondary routines
	fr base.InterleavedFrame
}

// NewConn allocates a Conn.
func NewConn(rw io.ReadWriter) *Conn {
	return &Conn{
		w:  rw,
		br: bufio.NewReaderSize(rw, readBufferSize),
	}
}

// Read reads a Request, a Response or an Interleaved frame.
func (c *Conn) Read() (interface{}, error) {
	for {
		byts, err := c.br.Peek(2)
		if err != nil {
			return nil, err
		}

		if byts[0] == base.InterleavedFrameMagicByte {
			return c.ReadInterleavedFrame()
		}

		if byts[0] == 'R' && byts[1] == 'T' {
			return c.ReadResponse()
		}

		if (byts[0] == 'A' && byts[1] == 'N') ||
			(byts[0] == 'D' && byts[1] == 'E') ||
			(byts[0] == 'G' && byts[1] == 'E') ||
			(byts[0] == 'O' && byts[1] == 'P') ||
			(byts[0] == 'P' && byts[1] == 'A') ||
			(byts[0] == 'P' && byts[1] == 'L') ||
			(byts[0] == 'R' && byts[1] == 'E') ||
			(byts[0] == 'S' && byts[1] == 'E') ||
			(byts[0] == 'T' && byts[1] == 'E') {
			return c.ReadRequest()
		}

		if _, err := c.br.Discard(1); err != nil {
			return nil, err
		}
	}
}

// ReadRequest reads a Request.
func (c *Conn) ReadRequest() (*base.Request, error) {
	var req base.Request
	err := req.Unmarshal(c.br)
	return &req, err
}

// ReadResponse reads a Response.
func (c *Conn) ReadResponse() (*base.Response, error) {
	var res base.Response
	err := res.Unmarshal(c.br)
	return &res, err
}

// ReadInterleavedFrame reads a InterleavedFrame.
func (c *Conn) ReadInterleavedFrame() (*base.InterleavedFrame, error) {
	err := c.fr.Unmarshal(c.br)
	return &c.fr, err
}

// WriteRequest writes a request.
func (c *Conn) WriteRequest(req *base.Request) error {
	buf, _ := req.Marshal()
	_, err := c.w.Write(buf)
	return err
}

// WriteResponse writes a response.
func (c *Conn) WriteResponse(res *base.Response) error {
	buf, _ := res.Marshal()
	_, err := c.w.Write(buf)
	return err
}

// WriteInterleavedFrame writes an interleaved frame.
func (c *Conn) WriteInterleavedFrame(fr *base.InterleavedFrame, buf []byte) error {
	n, _ := fr.MarshalTo(buf)
	_, err := c.w.Write(buf[:n])
	return err
}
