// Forked from github.com/StefanKopieczek/gossip by @StefanKopieczek
package timing

import (
	"sync"
	"time"
)

// Controls whether library calls should be mocked, or whether we should use the standard Go time library.
// If we're in Mock Mode, then time does not pass as normal, but only progresses when Elapse is called.
// False by default, indicating that we just call through to standard Go functions.
var MockMode = false
var currentTimeMock = time.Unix(0, 0)
var mockTimers = make([]*mockTimer, 0)
var mockTimerMu = new(sync.Mutex)

// Interface over Golang's built-in Timers, allowing them to be swapped out for mocked timers.
type Timer interface {
	// Returns a channel which sends the current time immediately when the timer expires.
	// Equivalent to time.Timer.C; however, we have to use a method here instead of a member since this is an interface.
	C() <-chan time.Time

	// Resets the timer such that it will expire in duration 'd' after the current time.
	// Returns true if the timer had been active, and false if it had expired or been stopped.
	Reset(d time.Duration) bool

	// Stops the timer, preventing it from firing.
	// Returns true if the timer had been active, and false if it had expired or been stopped.
	Stop() bool
}

// Implementation of Timer that just wraps time.Timer.
type realTimer struct {
	*time.Timer
}

func (t *realTimer) C() <-chan time.Time {
	return t.Timer.C
}

func (t *realTimer) Reset(d time.Duration) bool {
	t.Stop()
	return t.Timer.Reset(d)
}

func (t *realTimer) Stop() bool {
	// return t.Timer.Stop()
	if !t.Timer.Stop() {
		select {
		case <-t.Timer.C:
			return true
		default:
			return false
		}
	}
	return true
}

// Implementation of Timer that mocks time.Timer, firing when the total elapsed time (as controlled by Elapse)
// exceeds the duration specified when the timer was constructed.
type mockTimer struct {
	EndTime time.Time
	Chan    chan time.Time
	fired   bool
	toRun   func()
}

func (t *mockTimer) C() <-chan time.Time {
	return t.Chan
}

func (t *mockTimer) Reset(d time.Duration) bool {
	wasActive := removeMockTimer(t)

	t.EndTime = currentTimeMock.Add(d)
	if d > 0 {
		mockTimerMu.Lock()
		mockTimers = append(mockTimers, t)
		mockTimerMu.Unlock()
	} else {
		// The new timer has an expiry time of 0.
		// Fire it right away, and don't bother tracking it.
		t.Chan <- currentTimeMock
	}

	return wasActive
}

func (t *mockTimer) Stop() bool {
	if !removeMockTimer(t) {
		select {
		case <-t.Chan:
			return true
		default:
			return false
		}
	}
	return true
}

// Creates a new Timer; either a wrapper around a standard Go time.Timer, or a mocked-out Timer,
// depending on whether MockMode is set.
func NewTimer(d time.Duration) Timer {
	if MockMode {
		t := mockTimer{currentTimeMock.Add(d), make(chan time.Time, 1), false, nil}
		if d == 0 {
			t.Chan <- currentTimeMock
		} else {
			mockTimerMu.Lock()
			mockTimers = append(mockTimers, &t)
			mockTimerMu.Unlock()
		}
		return &t
	} else {
		return &realTimer{time.NewTimer(d)}
	}
}

// See built-in time.After() function.
func After(d time.Duration) <-chan time.Time {
	return NewTimer(d).C()
}

// See built-in time.AfterFunc() function.
func AfterFunc(d time.Duration, f func()) Timer {
	if MockMode {
		mockTimerMu.Lock()
		t := mockTimer{currentTimeMock.Add(d), make(chan time.Time, 1), false, f}
		mockTimerMu.Unlock()
		if d == 0 {
			go f()
			t.Chan <- currentTimeMock
		} else {
			mockTimerMu.Lock()
			mockTimers = append(mockTimers, &t)
			mockTimerMu.Unlock()
		}
		return &t
	} else {
		return &realTimer{time.AfterFunc(d, f)}
	}
}

// See built-in time.Sleep() function.
func Sleep(d time.Duration) {
	<-After(d)
}

// Increment the current time by the given Duration.
// This function can only be called in Mock Mode, otherwise we will panic.
func Elapse(d time.Duration) {
	requireMockMode()
	mockTimerMu.Lock()
	currentTimeMock = currentTimeMock.Add(d)
	mockTimerMu.Unlock()

	// Fire any timers whose time has come up.
	mockTimerMu.Lock()
	for _, t := range mockTimers {
		t.fired = false
		if !t.EndTime.After(currentTimeMock) {
			if t.toRun != nil {
				go t.toRun()
			}

			// Clear the channel if something is already in it.
			select {
			case <-t.Chan:
			default:
			}

			t.Chan <- currentTimeMock
			t.fired = true
		}
	}
	mockTimerMu.Unlock()

	// Stop tracking any fired timers.
	remainingTimers := make([]*mockTimer, 0)
	mockTimerMu.Lock()
	for _, t := range mockTimers {
		if !t.fired {
			remainingTimers = append(remainingTimers, t)
		}
	}

	mockTimers = remainingTimers
	mockTimerMu.Unlock()
}

// Returns the current time.
// If Mock Mode is set, this will be the sum of all Durations passed into Elapse calls;
// otherwise it will be the true system time.
func Now() time.Time {
	if MockMode {
		return currentTimeMock
	} else {
		return time.Now()
	}
}

// Shortcut method to enforce that Mock Mode is enabled.
func requireMockMode() {
	if !MockMode {
		panic("This method requires MockMode to be enabled")
	}
}

// Utility method to remove a mockTimer from the list of outstanding timers.
func removeMockTimer(t *mockTimer) bool {
	// First, find the index of the timer in our list.
	found := false
	var idx int
	var elt *mockTimer
	mockTimerMu.Lock()
	for idx, elt = range mockTimers {
		if elt == t {
			found = true
			break
		}
	}
	mockTimerMu.Unlock()

	if found {
		mockTimerMu.Lock()
		// We found the given timer. Remove it.
		mockTimers = append(mockTimers[:idx], mockTimers[idx+1:]...)
		mockTimerMu.Unlock()
		return true
	} else {
		// The timer was not present, indicating that it was already expired.
		return false
	}
}
