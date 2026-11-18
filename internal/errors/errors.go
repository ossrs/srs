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

func New(message string) error {
	return attach(errors.New(message))
}

func Errorf(format string, args ...any) error {
	return attach(fmt.Errorf(format, args...))
}

func WithStack(err error) error {
	if err == nil {
		return nil
	}
	return attach(err)
}

func WithMessage(err error, message string) error {
	if err == nil {
		return nil
	}
	return fmt.Errorf("%s: %w", message, err)
}

func Wrap(err error, message string) error {
	if err == nil {
		return nil
	}
	return attach(fmt.Errorf("%s: %w", message, err))
}

func Wrapf(err error, format string, args ...any) error {
	if err == nil {
		return nil
	}
	return attach(fmt.Errorf(fmt.Sprintf(format, args...)+": %w", err))
}

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
