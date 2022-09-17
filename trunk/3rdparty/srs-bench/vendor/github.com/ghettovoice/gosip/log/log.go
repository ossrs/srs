package log

import (
	"fmt"
	"strings"
)

// Logger interface used as base logger throughout the library.
type Logger interface {
	Print(args ...interface{})
	Printf(format string, args ...interface{})

	Trace(args ...interface{})
	Tracef(format string, args ...interface{})

	Debug(args ...interface{})
	Debugf(format string, args ...interface{})

	Info(args ...interface{})
	Infof(format string, args ...interface{})

	Warn(args ...interface{})
	Warnf(format string, args ...interface{})

	Error(args ...interface{})
	Errorf(format string, args ...interface{})

	Fatal(args ...interface{})
	Fatalf(format string, args ...interface{})

	Panic(args ...interface{})
	Panicf(format string, args ...interface{})

	WithPrefix(prefix string) Logger
	Prefix() string

	WithFields(fields Fields) Logger
	Fields() Fields

	SetLevel(level Level)
}

type Loggable interface {
	Log() Logger
}

type Fields map[string]interface{}

func (fields Fields) String() string {
	str := make([]string, 0)

	for k, v := range fields {
		str = append(str, fmt.Sprintf("%s=%+v", k, v))
	}

	return strings.Join(str, " ")
}

func (fields Fields) WithFields(newFields Fields) Fields {
	allFields := make(Fields)

	for k, v := range fields {
		allFields[k] = v
	}

	for k, v := range newFields {
		allFields[k] = v
	}

	return allFields
}

func AddFieldsFrom(logger Logger, values ...interface{}) Logger {
	for _, value := range values {
		switch v := value.(type) {
		case Logger:
			logger = logger.WithFields(v.Fields())
		case Loggable:
			logger = logger.WithFields(v.Log().Fields())
		case interface{ Fields() Fields }:
			logger = logger.WithFields(v.Fields())
		}
	}
	return logger
}
