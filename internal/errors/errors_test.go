// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT
package errors

import (
	stderrors "errors"
	"fmt"
	"strings"
	"testing"
)

func TestNew_MessageAndStack(t *testing.T) {
	err := New("boom")
	if err == nil {
		t.Fatal("New returned nil")
	}
	if err.Error() != "boom" {
		t.Fatalf("Error() = %q, want %q", err.Error(), "boom")
	}
	ws, ok := err.(*withStack)
	if !ok {
		t.Fatalf("New did not return *withStack, got %T", err)
	}
	if len(ws.StackTrace()) == 0 {
		t.Fatal("StackTrace is empty")
	}
}

func TestErrorf_FormatsMessage(t *testing.T) {
	err := Errorf("code=%d reason=%s", 42, "oops")
	if err.Error() != "code=42 reason=oops" {
		t.Fatalf("Error() = %q", err.Error())
	}
}

func TestErrorf_SupportsWrapVerb(t *testing.T) {
	root := stderrors.New("root")
	err := Errorf("ctx: %w", root)
	if !stderrors.Is(err, root) {
		t.Fatal("errors.Is did not find root through Errorf(%w)")
	}
}

func TestWithStack_NilReturnsNil(t *testing.T) {
	if got := WithStack(nil); got != nil {
		t.Fatalf("WithStack(nil) = %v, want nil", got)
	}
}

func TestWithStack_PreservesMessage(t *testing.T) {
	inner := stderrors.New("plain")
	err := WithStack(inner)
	if err.Error() != "plain" {
		t.Fatalf("Error() = %q, want %q", err.Error(), "plain")
	}
	if !stderrors.Is(err, inner) {
		t.Fatal("errors.Is did not find inner through WithStack")
	}
}

func TestWithMessage_NilReturnsNil(t *testing.T) {
	if got := WithMessage(nil, "ignored"); got != nil {
		t.Fatalf("WithMessage(nil) = %v, want nil", got)
	}
}

func TestWithMessage_PrependsAndWraps(t *testing.T) {
	inner := stderrors.New("root")
	err := WithMessage(inner, "ctx")
	if err.Error() != "ctx: root" {
		t.Fatalf("Error() = %q", err.Error())
	}
	if !stderrors.Is(err, inner) {
		t.Fatal("errors.Is did not traverse WithMessage")
	}
	// WithMessage must not capture a stack — verify the result is not a *withStack.
	if _, ok := err.(*withStack); ok {
		t.Fatal("WithMessage should not attach a stack")
	}
}

func TestWrap_NilReturnsNil(t *testing.T) {
	if got := Wrap(nil, "ignored"); got != nil {
		t.Fatalf("Wrap(nil) = %v, want nil", got)
	}
}

func TestWrap_MessageAndStackAndChain(t *testing.T) {
	inner := stderrors.New("root")
	err := Wrap(inner, "ctx")
	if err.Error() != "ctx: root" {
		t.Fatalf("Error() = %q", err.Error())
	}
	ws, ok := err.(*withStack)
	if !ok {
		t.Fatalf("Wrap did not return *withStack, got %T", err)
	}
	if len(ws.StackTrace()) == 0 {
		t.Fatal("StackTrace is empty")
	}
	if !stderrors.Is(err, inner) {
		t.Fatal("errors.Is did not traverse Wrap")
	}
}

func TestWrapf_NilReturnsNil(t *testing.T) {
	if got := Wrapf(nil, "ignored %d", 1); got != nil {
		t.Fatalf("Wrapf(nil) = %v, want nil", got)
	}
}

func TestWrapf_FormatsAndChains(t *testing.T) {
	inner := stderrors.New("root")
	err := Wrapf(inner, "ctx=%d op=%s", 7, "read")
	if err.Error() != "ctx=7 op=read: root" {
		t.Fatalf("Error() = %q", err.Error())
	}
	if !stderrors.Is(err, inner) {
		t.Fatal("errors.Is did not traverse Wrapf")
	}
}

func TestCause_NilReturnsNil(t *testing.T) {
	if got := Cause(nil); got != nil {
		t.Fatalf("Cause(nil) = %v, want nil", got)
	}
}

func TestCause_NoUnwrapReturnsSelf(t *testing.T) {
	root := stderrors.New("root")
	if got := Cause(root); got != root {
		t.Fatalf("Cause(root) = %v, want root", got)
	}
}

func TestCause_WalksToRoot(t *testing.T) {
	root := stderrors.New("root")
	err := Wrap(Wrap(WithMessage(root, "a"), "b"), "c")
	if got := Cause(err); got != root {
		t.Fatalf("Cause = %v, want root", got)
	}
}

func TestUnwrap_ReturnsInner(t *testing.T) {
	inner := stderrors.New("inner")
	err := WithStack(inner)
	if got := stderrors.Unwrap(err); got != inner {
		t.Fatalf("Unwrap = %v, want inner", got)
	}
}

func TestFormat_S(t *testing.T) {
	err := New("msg")
	got := fmt.Sprintf("%s", err)
	if got != "msg" {
		t.Fatalf("%%s = %q, want %q", got, "msg")
	}
}

func TestFormat_VFallsThroughToS(t *testing.T) {
	err := New("msg")
	got := fmt.Sprintf("%v", err)
	if got != "msg" {
		t.Fatalf("%%v = %q, want %q", got, "msg")
	}
}

func TestFormat_VPlusIncludesStack(t *testing.T) {
	err := New("msg")
	got := fmt.Sprintf("%+v", err)
	if !strings.HasPrefix(got, "msg") {
		t.Fatalf("%%+v output does not start with message: %q", got)
	}
	// Must include this test function in the captured stack.
	if !strings.Contains(got, "TestFormat_VPlusIncludesStack") {
		t.Fatalf("%%+v output missing caller frame:\n%s", got)
	}
	// Must include a file:line reference.
	if !strings.Contains(got, "errors_test.go:") {
		t.Fatalf("%%+v output missing file:line:\n%s", got)
	}
}

func TestFormat_Q(t *testing.T) {
	err := New("msg")
	got := fmt.Sprintf("%q", err)
	if got != `"msg"` {
		t.Fatalf("%%q = %q, want %q", got, `"msg"`)
	}
}

func TestIs_ThroughWrapChain(t *testing.T) {
	sentinel := stderrors.New("sentinel")
	err := Wrap(WithMessage(WithStack(sentinel), "mid"), "outer")
	if !stderrors.Is(err, sentinel) {
		t.Fatal("errors.Is failed to traverse Wrap/WithMessage/WithStack chain")
	}
}

type typedErr struct{ code int }

func (t *typedErr) Error() string { return fmt.Sprintf("typed(%d)", t.code) }

func TestAs_ThroughWrapChain(t *testing.T) {
	target := &typedErr{code: 7}
	err := Wrap(WithStack(target), "ctx")
	var got *typedErr
	if !stderrors.As(err, &got) {
		t.Fatal("errors.As failed to find *typedErr in chain")
	}
	if got.code != 7 {
		t.Fatalf("As returned code=%d, want 7", got.code)
	}
}

func TestReExports_AreStdlib(t *testing.T) {
	// Sanity: the re-exports must actually be the stdlib functions.
	a := stderrors.New("a")
	b := stderrors.New("b")
	joined := Join(a, b)
	if !Is(joined, a) || !Is(joined, b) {
		t.Fatal("Join/Is re-exports do not match stdlib behavior")
	}
	if Unwrap(WithStack(a)) != a {
		t.Fatal("Unwrap re-export does not match stdlib behavior")
	}
	var target *typedErr
	te := &typedErr{code: 1}
	if !As(WithStack(te), &target) {
		t.Fatal("As re-export does not match stdlib behavior")
	}
}
