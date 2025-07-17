package gortsplib

import (
	"net"
)

type serverTCPListener struct {
	s *Server

	ln net.Listener
}

func (sl *serverTCPListener) initialize() error {
	var err error
	sl.ln, err = sl.s.Listen(restrictNetwork("tcp", sl.s.RTSPAddress))
	if err != nil {
		return err
	}

	sl.s.wg.Add(1)
	go sl.run()

	return nil
}

func (sl *serverTCPListener) close() {
	sl.ln.Close()
}

func (sl *serverTCPListener) run() {
	defer sl.s.wg.Done()

	for {
		nconn, err := sl.ln.Accept()
		if err != nil {
			sl.s.acceptErr(err)
			return
		}

		sl.s.newConn(nconn)
	}
}
