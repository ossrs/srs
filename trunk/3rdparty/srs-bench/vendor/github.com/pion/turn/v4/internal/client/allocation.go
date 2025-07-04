// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package client

import (
	"errors"
	"fmt"
	"net"
	"sync"
	"time"

	"github.com/pion/logging"
	"github.com/pion/stun/v3"
	"github.com/pion/transport/v3"
	"github.com/pion/turn/v4/internal/proto"
)

// AllocationConfig is a set of configuration params use by NewUDPConn and NewTCPAllocation.
type AllocationConfig struct {
	Client      Client
	RelayedAddr net.Addr
	ServerAddr  net.Addr
	Integrity   stun.MessageIntegrity
	Nonce       stun.Nonce
	Username    stun.Username
	Realm       stun.Realm
	Lifetime    time.Duration
	Net         transport.Net
	Log         logging.LeveledLogger
}

type allocation struct {
	client            Client                // Read-only
	relayedAddr       net.Addr              // Read-only
	serverAddr        net.Addr              // Read-only
	permMap           *permissionMap        // Thread-safe
	integrity         stun.MessageIntegrity // Read-only
	username          stun.Username         // Read-only
	realm             stun.Realm            // Read-only
	_nonce            stun.Nonce            // Needs mutex x
	_lifetime         time.Duration         // Needs mutex x
	net               transport.Net         // Thread-safe
	refreshAllocTimer *PeriodicTimer        // Thread-safe
	refreshPermsTimer *PeriodicTimer        // Thread-safe
	readTimer         *time.Timer           // Thread-safe
	mutex             sync.RWMutex          // Thread-safe
	log               logging.LeveledLogger // Read-only
}

func (a *allocation) setNonceFromMsg(msg *stun.Message) {
	// Update nonce
	var nonce stun.Nonce
	if err := nonce.GetFrom(msg); err == nil {
		a.setNonce(nonce)
		a.log.Debug("Refresh allocation: 438, got new nonce.")
	} else {
		a.log.Warn("Refresh allocation: 438 but no nonce.")
	}
}

func (a *allocation) refreshAllocation(lifetime time.Duration, dontWait bool) error {
	msg, err := stun.Build(
		stun.TransactionID,
		stun.NewType(stun.MethodRefresh, stun.ClassRequest),
		proto.Lifetime{Duration: lifetime},
		a.username,
		a.realm,
		a.nonce(),
		a.integrity,
		stun.Fingerprint,
	)
	if err != nil {
		return fmt.Errorf("%w: %s", errFailedToBuildRefreshRequest, err.Error())
	}

	a.log.Debugf("Send refresh request (dontWait=%v)", dontWait)
	trRes, err := a.client.PerformTransaction(msg, a.serverAddr, dontWait)
	if err != nil {
		return fmt.Errorf("%w: %s", errFailedToRefreshAllocation, err.Error())
	}

	if dontWait {
		a.log.Debug("Refresh request sent")

		return nil
	}

	a.log.Debug("Refresh request sent, and waiting response")

	res := trRes.Msg
	if res.Type.Class == stun.ClassErrorResponse {
		var code stun.ErrorCodeAttribute
		if err = code.GetFrom(res); err == nil {
			if code.Code == stun.CodeStaleNonce {
				a.setNonceFromMsg(res)

				return errTryAgain
			}

			return err
		}

		return fmt.Errorf("%s", res.Type) //nolint:goerr113
	}

	// Getting lifetime from response
	var updatedLifetime proto.Lifetime
	if err := updatedLifetime.GetFrom(res); err != nil {
		return fmt.Errorf("%w: %s", errFailedToGetLifetime, err.Error())
	}

	a.setLifetime(updatedLifetime.Duration)
	a.log.Debugf("Updated lifetime: %d seconds", int(a.lifetime().Seconds()))

	return nil
}

func (a *allocation) refreshPermissions() error {
	addrs := a.permMap.addrs()
	if len(addrs) == 0 {
		a.log.Debug("No permission to refresh")

		return nil
	}
	if err := a.CreatePermissions(addrs...); err != nil {
		if errors.Is(err, errTryAgain) {
			return errTryAgain
		}
		a.log.Errorf("Fail to refresh permissions: %s", err)

		return err
	}
	a.log.Debug("Refresh permissions successful")

	return nil
}

func (a *allocation) onRefreshTimers(id int) {
	a.log.Debugf("Refresh timer %d expired", id)
	switch id {
	case timerIDRefreshAlloc:
		var err error
		lifetime := a.lifetime()
		// Limit the max retries on errTryAgain to 3
		// when stale nonce returns, sencond retry should succeed
		for i := 0; i < maxRetryAttempts; i++ {
			err = a.refreshAllocation(lifetime, false)
			if !errors.Is(err, errTryAgain) {
				break
			}
		}
		if err != nil {
			a.log.Warnf("Failed to refresh allocation: %s", err)
		}
	case timerIDRefreshPerms:
		var err error
		for i := 0; i < maxRetryAttempts; i++ {
			err = a.refreshPermissions()
			if !errors.Is(err, errTryAgain) {
				break
			}
		}
		if err != nil {
			a.log.Warnf("Failed to refresh permissions: %s", err)
		}
	}
}

func (a *allocation) nonce() stun.Nonce {
	a.mutex.RLock()
	defer a.mutex.RUnlock()

	return a._nonce
}

func (a *allocation) setNonce(nonce stun.Nonce) {
	a.mutex.Lock()
	defer a.mutex.Unlock()

	a.log.Debugf("Set new nonce with %d bytes", len(nonce))
	a._nonce = nonce
}

func (a *allocation) lifetime() time.Duration {
	a.mutex.RLock()
	defer a.mutex.RUnlock()

	return a._lifetime
}

func (a *allocation) setLifetime(lifetime time.Duration) {
	a.mutex.Lock()
	defer a.mutex.Unlock()

	a._lifetime = lifetime
}
