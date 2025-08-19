package gortsplib

import (
	"errors"
	"fmt"
	"time"

	"github.com/bluenviron/gortsplib/v4/pkg/base"
	"github.com/bluenviron/gortsplib/v4/pkg/liberrors"
)

type switchReadFuncError struct {
	tcp bool
}

func (switchReadFuncError) Error() string {
	return "switching read function"
}

func isSwitchReadFuncError(err error) bool {
	var eerr switchReadFuncError
	return errors.As(err, &eerr)
}

type serverConnReader struct {
	sc *ServerConn

	chRequest chan readReq
	chError   chan error
}

func (cr *serverConnReader) initialize() {
	cr.chRequest = make(chan readReq)
	cr.chError = make(chan error)

	go cr.run()
}

func (cr *serverConnReader) wait() {
	for {
		select {
		case <-cr.chError:
			return

		case req := <-cr.chRequest:
			req.res <- fmt.Errorf("terminated")
		}
	}
}

func (cr *serverConnReader) run() {
	readFunc := cr.readFuncStandard

	for {
		err := readFunc()

		var eerr switchReadFuncError
		if errors.As(err, &eerr) {
			if eerr.tcp {
				readFunc = cr.readFuncTCP
			} else {
				readFunc = cr.readFuncStandard
			}
			continue
		}

		cr.chError <- err
		break
	}
}

func (cr *serverConnReader) readFuncStandard() error {
	// reset deadline
	cr.sc.nconn.SetReadDeadline(time.Time{})

	for {
		what, err := cr.sc.conn.Read()
		if err != nil {
			return err
		}

		switch what := what.(type) {
		case *base.Request:
			cres := make(chan error)
			req := readReq{req: what, res: cres}
			cr.chRequest <- req

			err := <-cres
			if err != nil {
				return err
			}

		case *base.Response:
			return liberrors.ErrServerUnexpectedResponse{}

		case *base.InterleavedFrame:
			return liberrors.ErrServerUnexpectedFrame{}
		}
	}
}

func (cr *serverConnReader) readFuncTCP() error {
	// reset deadline
	cr.sc.nconn.SetReadDeadline(time.Time{})

	cr.sc.session.asyncStartWriter()

	for {
		if cr.sc.session.state == ServerSessionStateRecord {
			cr.sc.nconn.SetReadDeadline(time.Now().Add(cr.sc.s.ReadTimeout))
		}

		what, err := cr.sc.conn.Read()
		if err != nil {
			return err
		}

		switch what := what.(type) {
		case *base.Request:
			cres := make(chan error)
			req := readReq{req: what, res: cres}
			cr.chRequest <- req

			err := <-cres
			if err != nil {
				return err
			}

		case *base.Response:
			return liberrors.ErrServerUnexpectedResponse{}

		case *base.InterleavedFrame:
			if cb, ok := cr.sc.session.tcpCallbackByChannel[what.Channel]; ok {
				cb(what.Payload)
			}
		}
	}
}
