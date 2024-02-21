package main

import (
	"context"
	"crypto/rand"
	"crypto/sha256"
	"encoding/hex"
	"fmt"
	"net"
	"sync"
	"time"
)

func main() {
	if err := doMain(); err != nil {
		panic(err)
	}
}

func doMain() error {
	hashID := buildHashID()

	listener, err := net.Listen("tcp", ":1935")
	if err != nil {
		return err
	}
	trace(hashID, "Listen at %v", listener.Addr())

	for {
		client, err := listener.Accept()
		if err != nil {
			return err
		}

		backend, err := net.Dial("tcp", "localhost:19350")
		if err != nil {
			return err
		}

		go serve(client, backend)
	}
	return nil
}

func serve(client, backend net.Conn) {
	defer client.Close()
	defer backend.Close()
	hashID := buildHashID()
	if err := doServe(hashID, client, backend); err != nil {
		trace(hashID, "Serve error %v", err)
	}
}

func doServe(hashID string, client, backend net.Conn) error {
	var wg sync.WaitGroup
	var r0 error

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	if c, ok := client.(*net.TCPConn); ok {
		c.SetNoDelay(true)
	}
	if c, ok := backend.(*net.TCPConn); ok {
		c.SetNoDelay(true)
	}

	wg.Add(1)
	go func() {
		defer wg.Done()
		defer cancel()

		for {
			buf := make([]byte, 128*1024)
			nn, err := client.Read(buf)
			if err != nil {
				trace(hashID, "Read from client error %v", err)
				r0 = err
				return
			}
			if nn == 0 {
				trace(hashID, "Read from client EOF")
				return
			}

			_, err = backend.Write(buf[:nn])
			if err != nil {
				trace(hashID, "Write to RTMP backend error %v", err)
				r0 = err
				return
			}

			trace(hashID, "Copy %v bytes to RTMP backend", nn)
		}
	}()

	wg.Add(1)
	go func() {
		defer wg.Done()
		defer cancel()

		for {
			buf := make([]byte, 128*1024)
			nn, err := backend.Read(buf)
			if err != nil {
				trace(hashID, "Read from RTMP backend error %v", err)
				r0 = err
				return
			}
			if nn == 0 {
				trace(hashID, "Read from RTMP backend EOF")
				return
			}

			_, err = client.Write(buf[:nn])
			if err != nil {
				trace(hashID, "Write to client error %v", err)
				r0 = err
				return
			}

			trace(hashID, "Copy %v bytes to RTMP client", nn)
		}
	}()

	wg.Add(1)
	go func() {
		defer wg.Done()

		defer client.Close()
		defer backend.Close()

		<-ctx.Done()
		trace(hashID, "Context is done, close the connections")
	}()

	trace(hashID, "Start proxing client %v over %v to backend %v", client.RemoteAddr(), backend.LocalAddr(), backend.RemoteAddr())
	wg.Wait()
	trace(hashID, "Finish proxing client %v over %v to backend %v", client.RemoteAddr(), backend.LocalAddr(), backend.RemoteAddr())

	return r0
}

func trace(id, msg string, a ...interface{}) {
	fmt.Println(fmt.Sprintf("[%v][%v] %v",
		time.Now().Format("2006-01-02 15:04:05.000"), id,
		fmt.Sprintf(msg, a...),
	))
}

func buildHashID() string {
	randomData := make([]byte, 16)
	if _, err := rand.Read(randomData); err != nil {
		return ""
	}

	hash := sha256.Sum256(randomData)
	return hex.EncodeToString(hash[:])[:6]
}
