// Forked from github.com/StefanKopieczek/gossip by @StefanKopieczek
package util

import "sync"

// Simple semaphore implementation.
// Any number of calls to Acquire() can be made; these will not block.
// If the semaphore has been acquired more times than it has been released, it is called 'blocked'.
// Otherwise, it is called 'free'.
type Semaphore interface {
	// Take a semaphore lock.
	Acquire()

	// Release an acquired semaphore lock.
	// This should only be called when the semaphore is blocked, otherwise behaviour is undefined
	Release()

	// Block execution until the semaphore is free.
	Wait()

	// Clean up the semaphore object.
	Dispose()
}

func NewSemaphore() Semaphore {
	sem := new(semaphore)
	sem.cond = sync.NewCond(&sync.Mutex{})
	go func(s *semaphore) {
		select {
		case <-s.stop:
			return
		case <-s.acquired:
			s.locks += 1
		case <-s.released:
			s.locks -= 1
			if s.locks == 0 {
				s.cond.Broadcast()
			}
		}
	}(sem)
	return sem
}

// Concrete implementation of Semaphore.
type semaphore struct {
	held     bool
	locks    int
	acquired chan bool
	released chan bool
	stop     chan bool
	cond     *sync.Cond
}

// Implements Semaphore.Acquire()
func (sem *semaphore) Acquire() {
	sem.acquired <- true
}

// Implements Semaphore.Release()
func (sem *semaphore) Release() {
	sem.released <- true
}

// Implements Semaphore.Wait()
func (sem *semaphore) Wait() {
	sem.cond.L.Lock()
	for sem.locks != 0 {
		sem.cond.Wait()
	}
}

// Implements Semaphore.Dispose()
func (sem *semaphore) Dispose() {
	sem.stop <- true
}
