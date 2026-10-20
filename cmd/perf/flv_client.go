package main

import (
	"bufio"
	"fmt"
	"io"
	"net"
	"net/http"
	"time"
)

// flvClient is a lean HTTP-FLV player: one GET on its own TCP connection, with
// no HTTP client pool, then the FLV tags of the body, chunked or not.
type flvClient struct {
	conn net.Conn
	addr string
	app  string
}

func dialFLV(addr, app string) (*flvClient, error) {
	conn, err := net.DialTimeout("tcp", addr, 5*time.Second)
	if err != nil {
		return nil, err
	}
	return &flvClient{conn: conn, addr: addr, app: app}, nil
}

// play GETs /<app>/<stream>.flv and reports its audio and video tags until
// stop, which closes the connection and returns nil.
func (c *flvClient) play(stream string, stop <-chan struct{}, report chan<- rtmpMessage) error {
	return playHTTPLive(c.conn, c.addr, fmt.Sprintf("/%s/%s.flv", c.app, stream), "HTTP-FLV", stop, func(body io.Reader) error {
		if err := readFLVHeader(body); err != nil {
			return err
		}
		c.conn.SetDeadline(time.Time{})
		for {
			m, err := readFLVTag(body)
			if err != nil {
				return err
			}
			if m.typeID != 8 && m.typeID != 9 {
				continue
			}
			select {
			case report <- m:
			case <-stop:
				return nil
			}
		}
	})
}

// playHTTPLive GETs path on conn, one request on its own connection, and hands
// a 200 body, de-chunked, to read. The request and the response header must
// come within 10s, a deadline read clears once the stream has started. Stop
// closes the connection, and an error after stop is nil.
func playHTTPLive(conn net.Conn, addr, path, protocol string, stop <-chan struct{}, read func(body io.Reader) error) error {
	defer conn.Close()
	done := make(chan struct{})
	defer close(done)
	go func() {
		select {
		case <-stop:
			conn.Close()
		case <-done:
		}
	}()
	stopped := func(err error) error {
		select {
		case <-stop:
			return nil
		default:
			return err
		}
	}
	conn.SetDeadline(time.Now().Add(10 * time.Second))
	request := fmt.Sprintf("GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: srs-perf/1\r\nAccept: */*\r\n\r\n", path, addr)
	if _, err := io.WriteString(conn, request); err != nil {
		return stopped(err)
	}
	response, err := http.ReadResponse(bufio.NewReader(conn), nil)
	if err != nil {
		return stopped(err)
	}
	defer response.Body.Close()
	if response.StatusCode != http.StatusOK {
		return fmt.Errorf("%s status %s", protocol, response.Status)
	}
	return stopped(read(response.Body))
}
