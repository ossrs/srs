package client

import (
	"errors"
)

var (
	errFakeErr                             = errors.New("fake error")
	errTryAgain                            = errors.New("try again")
	errClosed                              = errors.New("use of closed network connection")
	errUDPAddrCast                         = errors.New("addr is not a net.UDPAddr")
	errAlreadyClosed                       = errors.New("already closed")
	errDoubleLock                          = errors.New("try-lock is already locked")
	errTransactionClosed                   = errors.New("transaction closed")
	errWaitForResultOnNonResultTransaction = errors.New("WaitForResult called on non-result transaction")
	errFailedToBuildRefreshRequest         = errors.New("failed to build refresh request")
	errFailedToRefreshAllocation           = errors.New("failed to refresh allocation")
	errFailedToGetLifetime                 = errors.New("failed to get lifetime from refresh response")
)

type timeoutError struct {
	msg string
}

func newTimeoutError(msg string) error {
	return &timeoutError{
		msg: msg,
	}
}

func (e *timeoutError) Error() string {
	return e.msg
}

func (e *timeoutError) Timeout() bool {
	return true
}
