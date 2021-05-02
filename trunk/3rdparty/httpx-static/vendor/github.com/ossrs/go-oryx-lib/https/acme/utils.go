// fork from https://github.com/rsc/letsencrypt/tree/master/vendor/github.com/xenolf/lego/acme
// fork from https://github.com/xenolf/lego/tree/master/acme
package acme

import (
	"fmt"
	"time"
)

// WaitFor polls the given function 'f', once every 'interval', up to 'timeout'.
func WaitFor(timeout, interval time.Duration, f func() (bool, error)) error {
	var lastErr string
	timeup := time.After(timeout)
	for {
		select {
		case <-timeup:
			return fmt.Errorf("Time limit exceeded. Last error: %s", lastErr)
		default:
		}

		stop, err := f()
		if stop {
			return nil
		}
		if err != nil {
			lastErr = err.Error()
		}

		time.Sleep(interval)
	}
}
