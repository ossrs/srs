// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package client

import (
	"errors"
)

var (
	errFake                                = errors.New("fake error")
	errTryAgain                            = errors.New("try again")
	errClosed                              = errors.New("use of closed network connection")
	errTCPAddrCast                         = errors.New("addr is not a TCP address")
	errUDPAddrCast                         = errors.New("addr is not a UDP address")
	errAlreadyClosed                       = errors.New("already closed")
	errDoubleLock                          = errors.New("try-lock is already locked")
	errTransactionClosed                   = errors.New("transaction closed")
	errWaitForResultOnNonResultTransaction = errors.New("WaitForResult called on non-result transaction")
	errFailedToBuildRefreshRequest         = errors.New("failed to build refresh request")
	errFailedToRefreshAllocation           = errors.New("failed to refresh allocation")
	errFailedToGetLifetime                 = errors.New("failed to get lifetime from refresh response")
	errInvalidTURNAddress                  = errors.New("invalid TURN server address")
	errUnexpectedSTUNRequestMessage        = errors.New("unexpected STUN request message")
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
