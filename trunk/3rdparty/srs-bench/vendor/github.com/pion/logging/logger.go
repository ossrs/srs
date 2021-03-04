package logging

import (
	"fmt"
	"io"
	"log"
	"os"
	"strings"
	"sync"
)

// Use this abstraction to ensure thread-safe access to the logger's io.Writer
// (which could change at runtime)
type loggerWriter struct {
	sync.RWMutex
	output io.Writer
}

func (lw *loggerWriter) SetOutput(output io.Writer) {
	lw.Lock()
	defer lw.Unlock()
	lw.output = output
}

func (lw *loggerWriter) Write(data []byte) (int, error) {
	lw.RLock()
	defer lw.RUnlock()
	return lw.output.Write(data)
}

// DefaultLeveledLogger encapsulates functionality for providing logging at
// user-defined levels
type DefaultLeveledLogger struct {
	level  LogLevel
	writer *loggerWriter
	trace  *log.Logger
	debug  *log.Logger
	info   *log.Logger
	warn   *log.Logger
	err    *log.Logger
}

// WithTraceLogger is a chainable configuration function which sets the
// Trace-level logger
func (ll *DefaultLeveledLogger) WithTraceLogger(log *log.Logger) *DefaultLeveledLogger {
	ll.trace = log
	return ll
}

// WithDebugLogger is a chainable configuration function which sets the
// Debug-level logger
func (ll *DefaultLeveledLogger) WithDebugLogger(log *log.Logger) *DefaultLeveledLogger {
	ll.debug = log
	return ll
}

// WithInfoLogger is a chainable configuration function which sets the
// Info-level logger
func (ll *DefaultLeveledLogger) WithInfoLogger(log *log.Logger) *DefaultLeveledLogger {
	ll.info = log
	return ll
}

// WithWarnLogger is a chainable configuration function which sets the
// Warn-level logger
func (ll *DefaultLeveledLogger) WithWarnLogger(log *log.Logger) *DefaultLeveledLogger {
	ll.warn = log
	return ll
}

// WithErrorLogger is a chainable configuration function which sets the
// Error-level logger
func (ll *DefaultLeveledLogger) WithErrorLogger(log *log.Logger) *DefaultLeveledLogger {
	ll.err = log
	return ll
}

// WithOutput is a chainable configuration function which sets the logger's
// logging output to the supplied io.Writer
func (ll *DefaultLeveledLogger) WithOutput(output io.Writer) *DefaultLeveledLogger {
	ll.writer.SetOutput(output)
	return ll
}

func (ll *DefaultLeveledLogger) logf(logger *log.Logger, level LogLevel, format string, args ...interface{}) {
	if ll.level.Get() < level {
		return
	}

	callDepth := 3 // this frame + wrapper func + caller
	msg := fmt.Sprintf(format, args...)
	if err := logger.Output(callDepth, msg); err != nil {
		fmt.Fprintf(os.Stderr, "Unable to log: %s", err)
	}
}

// SetLevel sets the logger's logging level
func (ll *DefaultLeveledLogger) SetLevel(newLevel LogLevel) {
	ll.level.Set(newLevel)
}

// Trace emits the preformatted message if the logger is at or below LogLevelTrace
func (ll *DefaultLeveledLogger) Trace(msg string) {
	ll.logf(ll.trace, LogLevelTrace, msg)
}

// Tracef formats and emits a message if the logger is at or below LogLevelTrace
func (ll *DefaultLeveledLogger) Tracef(format string, args ...interface{}) {
	ll.logf(ll.trace, LogLevelTrace, format, args...)
}

// Debug emits the preformatted message if the logger is at or below LogLevelDebug
func (ll *DefaultLeveledLogger) Debug(msg string) {
	ll.logf(ll.debug, LogLevelDebug, msg)
}

// Debugf formats and emits a message if the logger is at or below LogLevelDebug
func (ll *DefaultLeveledLogger) Debugf(format string, args ...interface{}) {
	ll.logf(ll.debug, LogLevelDebug, format, args...)
}

// Info emits the preformatted message if the logger is at or below LogLevelInfo
func (ll *DefaultLeveledLogger) Info(msg string) {
	ll.logf(ll.info, LogLevelInfo, msg)
}

// Infof formats and emits a message if the logger is at or below LogLevelInfo
func (ll *DefaultLeveledLogger) Infof(format string, args ...interface{}) {
	ll.logf(ll.info, LogLevelInfo, format, args...)
}

// Warn emits the preformatted message if the logger is at or below LogLevelWarn
func (ll *DefaultLeveledLogger) Warn(msg string) {
	ll.logf(ll.warn, LogLevelWarn, msg)
}

// Warnf formats and emits a message if the logger is at or below LogLevelWarn
func (ll *DefaultLeveledLogger) Warnf(format string, args ...interface{}) {
	ll.logf(ll.warn, LogLevelWarn, format, args...)
}

// Error emits the preformatted message if the logger is at or below LogLevelError
func (ll *DefaultLeveledLogger) Error(msg string) {
	ll.logf(ll.err, LogLevelError, msg)
}

// Errorf formats and emits a message if the logger is at or below LogLevelError
func (ll *DefaultLeveledLogger) Errorf(format string, args ...interface{}) {
	ll.logf(ll.err, LogLevelError, format, args...)
}

// NewDefaultLeveledLoggerForScope returns a configured LeveledLogger
func NewDefaultLeveledLoggerForScope(scope string, level LogLevel, writer io.Writer) *DefaultLeveledLogger {
	if writer == nil {
		writer = os.Stdout
	}
	logger := &DefaultLeveledLogger{
		writer: &loggerWriter{output: writer},
		level:  level,
	}
	return logger.
		WithTraceLogger(log.New(logger.writer, fmt.Sprintf("%s TRACE: ", scope), log.Lmicroseconds|log.Lshortfile)).
		WithDebugLogger(log.New(logger.writer, fmt.Sprintf("%s DEBUG: ", scope), log.Lmicroseconds|log.Lshortfile)).
		WithInfoLogger(log.New(logger.writer, fmt.Sprintf("%s INFO: ", scope), log.LstdFlags)).
		WithWarnLogger(log.New(logger.writer, fmt.Sprintf("%s WARNING: ", scope), log.LstdFlags)).
		WithErrorLogger(log.New(logger.writer, fmt.Sprintf("%s ERROR: ", scope), log.LstdFlags))
}

// DefaultLoggerFactory define levels by scopes and creates new DefaultLeveledLogger
type DefaultLoggerFactory struct {
	Writer          io.Writer
	DefaultLogLevel LogLevel
	ScopeLevels     map[string]LogLevel
}

// NewDefaultLoggerFactory creates a new DefaultLoggerFactory
func NewDefaultLoggerFactory() *DefaultLoggerFactory {
	factory := DefaultLoggerFactory{}
	factory.DefaultLogLevel = LogLevelError
	factory.ScopeLevels = make(map[string]LogLevel)
	factory.Writer = os.Stdout

	logLevels := map[string]LogLevel{
		"DISABLE": LogLevelDisabled,
		"ERROR":   LogLevelError,
		"WARN":    LogLevelWarn,
		"INFO":    LogLevelInfo,
		"DEBUG":   LogLevelDebug,
		"TRACE":   LogLevelTrace,
	}

	for name, level := range logLevels {
		env := os.Getenv(fmt.Sprintf("PION_LOG_%s", name))

		if env == "" {
			env = os.Getenv(fmt.Sprintf("PIONS_LOG_%s", name))
		}

		if env == "" {
			continue
		}

		if strings.ToLower(env) == "all" {
			factory.DefaultLogLevel = level
			continue
		}

		scopes := strings.Split(strings.ToLower(env), ",")
		for _, scope := range scopes {
			factory.ScopeLevels[scope] = level
		}
	}

	return &factory
}

// NewLogger returns a configured LeveledLogger for the given , argsscope
func (f *DefaultLoggerFactory) NewLogger(scope string) LeveledLogger {
	logLevel := f.DefaultLogLevel
	if f.ScopeLevels != nil {
		scopeLevel, found := f.ScopeLevels[scope]

		if found {
			logLevel = scopeLevel
		}
	}
	return NewDefaultLeveledLoggerForScope(scope, logLevel, f.Writer)
}
