// Package errors provides error handling primitives with stack traces.
//
// It is a thin layer over the standard library's errors package, adding a
// stack trace at the point an error is created or wrapped. The wrapping
// chain is fully compatible with errors.Is, errors.As, and errors.Unwrap.
//
// # Adding context to an error
//
//	_, err := io.ReadAll(r)
//	if err != nil {
//	        return errors.Wrap(err, "read failed")
//	}
//
// # Formatted printing of errors
//
//	%s    the error message (full wrap chain)
//	%v    same as %s
//	%+v   the error message followed by the captured stack trace
//	%q    the error message, quoted
//
// # Retrieving the stack trace
//
// Errors returned by this package satisfy the following interface:
//
//	type stackTracer interface {
//	        StackTrace() []uintptr
//	}
package errors

import (
	"errors"
	"fmt"
	"runtime"
)

// Re-exported stdlib primitives so callers can use a single import.
var (
	Is     = errors.Is
	As     = errors.As
	Unwrap = errors.Unwrap
	Join   = errors.Join
)

// withStack wraps an error with a captured stack trace.
type withStack struct {
	err error
	pcs []uintptr
}

func (e *withStack) Error() string {
	return e.err.Error()
}

func (e *withStack) Unwrap() error {
	return e.err
}

func (e *withStack) StackTrace() []uintptr {
	return e.pcs
}

func (e *withStack) Format(s fmt.State, verb rune) {
	switch verb {
	case 'v':
		if s.Flag('+') {
			fmt.Fprint(s, e.err.Error())
			frames := runtime.CallersFrames(e.pcs)
			for {
				f, more := frames.Next()
				fmt.Fprintf(s, "\n%s\n\t%s:%d", f.Function, f.File, f.Line)
				if !more {
					break
				}
			}
			return
		}
		fallthrough
	case 's':
		fmt.Fprint(s, e.err.Error())
	case 'q':
		fmt.Fprintf(s, "%q", e.err.Error())
	}
}

func callers() []uintptr {
	var pcs [32]uintptr
	n := runtime.Callers(3, pcs[:])
	return pcs[:n]
}

func attach(err error) error {
	return &withStack{err: err, pcs: callers()}
}

// New returns an error with the supplied message and a captured stack trace.
func New(message string) error {
	return attach(errors.New(message))
}

// Errorf formats according to a format specifier and returns a new error with
// a captured stack trace. It supports %w for wrapping an existing error.
func Errorf(format string, args ...any) error {
	return attach(fmt.Errorf(format, args...))
}

// WithStack annotates err with a stack trace at the point WithStack was called.
// If err is nil, WithStack returns nil.
func WithStack(err error) error {
	if err == nil {
		return nil
	}
	return attach(err)
}

// WithMessage annotates err with a new message, without capturing a stack.
// If err is nil, WithMessage returns nil.
func WithMessage(err error, message string) error {
	if err == nil {
		return nil
	}
	return fmt.Errorf("%s: %w", message, err)
}

// Wrap returns an error annotating err with a message and a captured stack.
// If err is nil, Wrap returns nil.
func Wrap(err error, message string) error {
	if err == nil {
		return nil
	}
	return attach(fmt.Errorf("%s: %w", message, err))
}

// Wrapf is the formatting variant of Wrap.
// If err is nil, Wrapf returns nil.
func Wrapf(err error, format string, args ...any) error {
	if err == nil {
		return nil
	}
	return attach(fmt.Errorf(fmt.Sprintf(format, args...)+": %w", err))
}

// Cause walks the error's Unwrap chain and returns the root error.
// New code should prefer errors.Is or errors.As.
func Cause(err error) error {
	for err != nil {
		u := errors.Unwrap(err)
		if u == nil {
			return err
		}
		err = u
	}
	return nil
}
