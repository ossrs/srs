// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

// Package metrics parses the Prometheus text exposition format, strictly, so a
// case sees whether a scraper can read what the server exports.
package metrics

import (
	"fmt"
	"io"
	"strconv"
	"strings"
)

// Label is a label of a sample.
type Label struct {
	Name, Value string
}

// Sample is a line of a metric's value.
type Sample struct {
	Name   string
	Labels []Label
	Value  float64
}

// Family is a metric: its HELP, its TYPE and its samples, in order.
type Family struct {
	Name, Help, Type string
	Samples          []Sample
}

// The metric types of the format.
var types = map[string]bool{"counter": true, "gauge": true, "histogram": true, "summary": true, "untyped": true}

// Parse reads the exposition text, failing on anything a scraper rejects.
func Parse(r io.Reader) ([]Family, error) {
	b, err := io.ReadAll(r)
	if err != nil {
		return nil, err
	}
	text := string(b)
	if text != "" && !strings.HasSuffix(text, "\n") {
		return nil, fmt.Errorf("the text doesn't end with a newline")
	}

	var fams []Family
	// cur is the family the lines belong to; sampled when it has a sample, so
	// a HELP or TYPE of it must come before.
	var cur *Family
	sampled := false
	family := func(name string) *Family {
		if cur == nil || cur.Name != name {
			fams = append(fams, Family{Name: name})
			cur, sampled = &fams[len(fams)-1], false
		}
		return cur
	}

	for i, line := range strings.Split(strings.TrimSuffix(text, "\n"), "\n") {
		n := i + 1
		if strings.TrimSpace(line) == "" {
			continue
		}
		if strings.HasPrefix(line, "#") {
			fields := strings.Fields(line)
			if len(fields) < 3 || (fields[1] != "HELP" && fields[1] != "TYPE") {
				continue // A comment.
			}
			name := fields[2]
			if !validName(name) {
				return nil, fmt.Errorf("line %d: bad metric name %q", n, name)
			}
			if cur != nil && cur.Name == name && sampled {
				return nil, fmt.Errorf("line %d: %s of %s after its samples", n, fields[1], name)
			}
			f := family(name)
			if fields[1] == "HELP" {
				if f.Help != "" {
					return nil, fmt.Errorf("line %d: a second HELP of %s", n, name)
				}
				_, help, _ := strings.Cut(line, name)
				f.Help = strings.TrimPrefix(help, " ")
				continue
			}
			if f.Type != "" || len(fields) != 4 || !types[fields[3]] {
				return nil, fmt.Errorf("line %d: bad TYPE %q", n, line)
			}
			f.Type = fields[3]
			continue
		}

		s, err := parseSample(line)
		if err != nil {
			return nil, fmt.Errorf("line %d: %w: %q", n, err, line)
		}
		if cur != nil && cur.Name != s.Name && cur.Type != "" && !sampled {
			return nil, fmt.Errorf("line %d: a sample of %s after the TYPE of %s", n, s.Name, cur.Name)
		}
		f := family(s.Name)
		f.Samples = append(f.Samples, s)
		sampled = true
	}

	for i := range fams {
		if fams[i].Type == "" {
			fams[i].Type = "untyped"
		}
	}
	return fams, nil
}

// parseSample parses a sample line: a name, labels in braces, a value and an
// optional timestamp.
func parseSample(line string) (Sample, error) {
	var s Sample
	end := strings.IndexAny(line, "{ ")
	if end < 0 {
		return s, fmt.Errorf("no value")
	}
	s.Name, line = line[:end], line[end:]
	if !validName(s.Name) {
		return s, fmt.Errorf("bad metric name %q", s.Name)
	}

	if strings.HasPrefix(line, "{") {
		line = line[1:]
		seen := map[string]bool{}
		for {
			line = strings.TrimLeft(line, " ")
			if strings.HasPrefix(line, "}") {
				line = line[1:]
				break
			}
			eq := strings.Index(line, "=")
			if eq < 0 {
				return s, fmt.Errorf("unclosed labels")
			}
			name := strings.TrimSpace(line[:eq])
			if !validLabel(name) || seen[name] {
				return s, fmt.Errorf("bad or repeated label name %q", name)
			}
			seen[name] = true
			value, rest, err := quoted(strings.TrimLeft(line[eq+1:], " "))
			if err != nil {
				return s, fmt.Errorf("label %s: %w", name, err)
			}
			s.Labels = append(s.Labels, Label{name, value})
			line = strings.TrimLeft(rest, " ")
			if strings.HasPrefix(line, ",") {
				line = line[1:]
			} else if !strings.HasPrefix(line, "}") {
				return s, fmt.Errorf("unclosed labels")
			}
		}
	}

	fields := strings.Fields(line)
	if len(fields) < 1 || len(fields) > 2 || !strings.HasPrefix(line, " ") {
		return s, fmt.Errorf("want a value and an optional timestamp")
	}
	v, err := strconv.ParseFloat(fields[0], 64)
	if err != nil {
		return s, fmt.Errorf("bad value %q", fields[0])
	}
	s.Value = v
	if len(fields) == 2 {
		if _, err := strconv.ParseInt(fields[1], 10, 64); err != nil {
			return s, fmt.Errorf("bad timestamp %q", fields[1])
		}
	}
	return s, nil
}

// quoted reads a quoted label value with its escapes, \\, \" and \n, and
// returns it and the text after it.
func quoted(text string) (string, string, error) {
	if !strings.HasPrefix(text, `"`) {
		return "", "", fmt.Errorf("value not quoted")
	}
	var b strings.Builder
	for i := 1; i < len(text); i++ {
		switch ch := text[i]; ch {
		case '"':
			return b.String(), text[i+1:], nil
		case '\\':
			if i+1 == len(text) {
				return "", "", fmt.Errorf("unclosed value")
			}
			i++
			switch text[i] {
			case '\\':
				b.WriteByte('\\')
			case '"':
				b.WriteByte('"')
			case 'n':
				b.WriteByte('\n')
			default:
				return "", "", fmt.Errorf("bad escape \\%c", text[i])
			}
		default:
			b.WriteByte(ch)
		}
	}
	return "", "", fmt.Errorf("unclosed value")
}

// validName reports whether a metric name matches [a-zA-Z_:][a-zA-Z0-9_:]*.
func validName(name string) bool {
	return validIdent(name, true)
}

// validLabel reports whether a label name matches [a-zA-Z_][a-zA-Z0-9_]*.
func validLabel(name string) bool {
	return validIdent(name, false)
}

func validIdent(name string, colon bool) bool {
	if name == "" {
		return false
	}
	for i, ch := range name {
		ok := ch == '_' || (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (colon && ch == ':') || (i > 0 && ch >= '0' && ch <= '9')
		if !ok {
			return false
		}
	}
	return true
}

// Find returns the family of a name, or nil.
func Find(fams []Family, name string) *Family {
	for i := range fams {
		if fams[i].Name == name {
			return &fams[i]
		}
	}
	return nil
}

// Label returns the value of a label of the sample.
func (s Sample) Label(name string) (string, bool) {
	for _, l := range s.Labels {
		if l.Name == name {
			return l.Value, true
		}
	}
	return "", false
}

// LabelNames returns the names of the sample's labels, in order.
func (s Sample) LabelNames() []string {
	names := make([]string, 0, len(s.Labels))
	for _, l := range s.Labels {
		names = append(names, l.Name)
	}
	return names
}
