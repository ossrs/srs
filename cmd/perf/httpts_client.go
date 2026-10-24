package main

import (
	"fmt"
	"io"
	"net"
	"time"
)

// httpTSPort is SRS's HTTP server port in the rtmp-ts scenario.
const httpTSPort = "8080"

// httpTSClient is a lean HTTP-TS player: one GET of SRS's http_remux TS
// stream on its own TCP connection, as the HTTP-FLV player does, with the
// body demuxed by the SRT player's TS demuxer only to rebuild each H.264
// frame for the lag anchor; AAC is counted per PES.
type httpTSClient struct {
	conn net.Conn
	addr string
	app  string
}

func dialHTTPTS(addr, app string) (*httpTSClient, error) {
	conn, err := net.DialTimeout("tcp", addr, 5*time.Second)
	if err != nil {
		return nil, err
	}
	return &httpTSClient{conn: conn, addr: addr, app: app}, nil
}

// play GETs /<app>/<stream>.ts and reports its frames until stop, which
// closes the connection and returns nil. A video frame ends at the next one,
// so it is reported one frame late, as for SRT.
func (c *httpTSClient) play(stream string, stop <-chan struct{}, report chan<- rtmpMessage) error {
	send := func(m rtmpMessage) bool {
		select {
		case report <- m:
			return true
		case <-stop:
			return false
		}
	}
	return playHTTPLive(c.conn, c.addr, fmt.Sprintf("/%s/%s.ts", c.app, stream), "HTTP-TS", stop, func(body io.Reader) error {
		c.conn.SetDeadline(time.Time{})
		demuxer := newStreamTSDemuxer(send)
		b := make([]byte, 16*1024)
		for {
			n, err := body.Read(b)
			if n > 0 {
				if err := demuxer.write(b[:n]); err != nil {
					return err
				}
			}
			if err != nil {
				return err
			}
		}
	})
}
