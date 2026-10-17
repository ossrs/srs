// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package check

import (
	"flag"
	"fmt"
	"strconv"
	"time"
)

// limitScale multiplies every class limit. The limits hold on a developer's
// machine; CI runners are slower and have fewer cores, so CI runs the same
// cases with a larger scale, and a case that hangs still fails.
var limitScale float64 = 1

func init() {
	flag.Func("integ.limit-scale", "multiply every class limit by this, at least 1, for slower machines such as CI runners", func(v string) error {
		f, err := strconv.ParseFloat(v, 64)
		if err != nil || f < 1 {
			return fmt.Errorf("want a number of at least 1, got %q", v)
		}
		limitScale = f
		return nil
	})
}

// limit is the longest a case of the class may take on this runner.
func (r *runner) limit(class Class) time.Duration {
	if r.limitScale <= 1 {
		return class.Limit()
	}
	return time.Duration(float64(class.Limit()) * r.limitScale)
}

// Scaled is d multiplied by the limit scale, for a case's own wait on an
// answer SRS gives at once on a fast machine.
func Scaled(d time.Duration) time.Duration {
	if limitScale <= 1 {
		return d
	}
	return time.Duration(float64(d) * limitScale)
}
