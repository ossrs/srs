// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package check

import (
	"flag"
	"reflect"
	"sync/atomic"
	"testing"
)

func TestCopyNames(t *testing.T) {
	for _, tc := range []struct {
		class Class
		n     int
		want  []string
	}{
		{Normal, 1, []string{"Play"}},
		{Normal, 0, []string{"Play"}},
		{Normal, 3, []string{"Play", "Play-2", "Play-3"}},
		{Fast, 2, []string{"Play", "Play-2"}},
		// A timer case starts its own server, so it is never copied.
		{Timer, 3, []string{"Play"}},
	} {
		if got := copyNames("Play", tc.class, tc.n); !reflect.DeepEqual(got, tc.want) {
			t.Errorf("copyNames(Play, %v, %d) = %q, want %q", tc.class, tc.n, got, tc.want)
		}
	}
}

func TestScaleFlag(t *testing.T) {
	defer func(n int) { scale = n }(scale)

	f := flag.Lookup("integ.scale")
	if f == nil {
		t.Fatal("no -integ.scale flag")
	}
	if err := f.Value.Set("4"); err != nil || scale != 4 {
		t.Errorf("set 4: scale %d, err %v", scale, err)
	}
	for _, bad := range []string{"0", "-1", "x"} {
		if err := f.Value.Set(bad); err == nil {
			t.Errorf("set %q has no error", bad)
		}
	}
}

// TestCaseRunsCopies runs a case with a scale of 3 through a real *testing.T:
// three cases, each recorded with its own name.
func TestCaseRunsCopies(t *testing.T) {
	defer func(n int) { scale = n }(scale)
	scale = 3

	before := len(results.list())
	var ran atomic.Int32
	t.Run("Group", func(t *testing.T) {
		Case(t, "Pass", Fast, func(c *C) { ran.Add(1) })
	})
	got := results.list()[before:]

	var names []string
	for _, r := range got {
		names = append(names, r.Name)
	}
	want := map[string]bool{
		"TestCaseRunsCopies/Group/Pass":   true,
		"TestCaseRunsCopies/Group/Pass-2": true,
		"TestCaseRunsCopies/Group/Pass-3": true,
	}
	if ran.Load() != 3 || len(names) != 3 {
		t.Fatalf("ran %d, results %q, want 3", ran.Load(), names)
	}
	for _, n := range names {
		if !want[n] {
			t.Errorf("result %q, want one of %v", n, want)
		}
	}
}

// TestCopyNamesOfScale gives the names of a case's copies at the -integ.scale
// of the process, for a server that must know them before it starts.
func TestCopyNamesOfScale(t *testing.T) {
	defer func(n int) { scale = n }(scale)
	scale = 2
	if got, want := CopyNames("Play", Normal), []string{"Play", "Play-2"}; !reflect.DeepEqual(got, want) {
		t.Errorf("CopyNames(Play, Normal) = %q, want %q", got, want)
	}
	if got, want := CopyNames("Play", Timer), []string{"Play"}; !reflect.DeepEqual(got, want) {
		t.Errorf("CopyNames(Play, Timer) = %q, want %q", got, want)
	}
}
