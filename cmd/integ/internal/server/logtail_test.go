// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package server

import (
	"bytes"
	"reflect"
	"testing"
)

func TestLogTail(t *testing.T) {
	var out bytes.Buffer
	l := newLogTail(&out, 3)

	for _, s := range []string{"one\ntw", "o\nthree\n", "four\nfi", "ve"} {
		if n, err := l.Write([]byte(s)); n != len(s) || err != nil {
			t.Fatalf("Write(%q) = %d, %v", s, n, err)
		}
	}

	if got, want := l.Lines(), []string{"three", "four", "five"}; !reflect.DeepEqual(got, want) {
		t.Errorf("Lines() = %q, want %q", got, want)
	}
	if got, want := out.String(), "one\ntwo\nthree\nfour\nfive"; got != want {
		t.Errorf("copied %q, want %q", got, want)
	}
}

func TestLogTailFewLines(t *testing.T) {
	l := newLogTail(&bytes.Buffer{}, 30)
	if got := l.Lines(); len(got) != 0 {
		t.Errorf("Lines() of nothing = %q", got)
	}
	l.Write([]byte("a\r\nb\n"))
	if got, want := l.Lines(), []string{"a", "b"}; !reflect.DeepEqual(got, want) {
		t.Errorf("Lines() = %q, want %q", got, want)
	}
}
