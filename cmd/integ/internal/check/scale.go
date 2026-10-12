// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package check

import (
	"flag"
	"fmt"
	"strconv"
)

// scale is how many times each case runs, as separate cases.
var scale = 1

func init() {
	flag.Func("integ.scale", "run each case N times as separate cases, for the scale check; timer cases run once", func(v string) error {
		n, err := strconv.Atoi(v)
		if err != nil || n < 1 {
			return fmt.Errorf("want a number of at least 1, got %q", v)
		}
		scale = n
		return nil
	})
}

// copyNames returns the names of the copies of a case: the name itself, then
// name-2 to name-n. A timer case is never copied, because it starts a server
// of its own, and n copies of it would pass the process cap.
func copyNames(name string, class Class, n int) []string {
	if n <= 1 || class == Timer {
		return []string{name}
	}
	names := []string{name}
	for i := 2; i <= n; i++ {
		names = append(names, name+"-"+strconv.Itoa(i))
	}
	return names
}

// CopyNames returns the names of the copies of a case of the class at the
// -integ.scale of the process, the name itself first, for a server that must
// know each copy's stream before it starts.
func CopyNames(name string, class Class) []string {
	return copyNames(name, class, scale)
}
