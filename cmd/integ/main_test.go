// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT
package main

import (
	"bytes"
	"strings"
	"testing"
)

func TestRun(t *testing.T) {
	cases := []struct {
		name   string
		args   []string
		code   int
		stdout string
		stderr string
	}{
		{"NoArgs", nil, 2, "", "Usage: srs-integ"},
		{"Help", []string{"help"}, 0, "Usage: srs-integ", ""},
		{"HelpFlag", []string{"-h"}, 0, "Usage: srs-integ", ""},
		{"UnknownCommand", []string{"play"}, 2, "", "unknown command: play"},
		{"ProbeNoURL", []string{"probe"}, 2, "", "Usage: srs-integ probe <url>"},
		{"ProbeExtraArgs", []string{"probe", "rtmp://a/b", "c"}, 2, "", "Usage: srs-integ probe <url>"},
		{"ProbeNotImplemented", []string{"probe", "rtmp://127.0.0.1/live/a"}, 1, "", "probe: not implemented yet"},
		{"PublishNoArgs", []string{"publish"}, 2, "", "Usage: srs-integ publish <file> <url>"},
		{"PublishNoURL", []string{"publish", "a.flv"}, 2, "", "Usage: srs-integ publish <file> <url>"},
		{"PublishBadURL", []string{"publish", "a.flv", "http://127.0.0.1/live/a"}, 1, "", "publish: unsupported URL"},
		{"PublishNoFile", []string{"publish", "/nothing/a.flv", "rtmp://127.0.0.1:1/live/a"}, 1, "", "/nothing/a.flv"},
		{"PrepareExtraArgs", []string{"prepare", "x"}, 2, "", "Usage: srs-integ prepare"},
		{"PrepareBadFlag", []string{"prepare", "-nothing"}, 2, "", "Usage: srs-integ prepare"},
		{"PrepareNoSource", []string{"prepare", "-source", "/nothing/source.flv", "-dir", "/tmp/srs-integ-no-dir"}, 1, "", "/nothing/source.flv"},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			var stdout, stderr bytes.Buffer
			code := run(tc.args, &stdout, &stderr)
			if code != tc.code {
				t.Errorf("exit code %v, want %v; stderr %q", code, tc.code, stderr.String())
			}
			checkOutput(t, "stdout", stdout.String(), tc.stdout)
			checkOutput(t, "stderr", stderr.String(), tc.stderr)
		})
	}
}

// checkOutput requires got to contain want, or to be empty when want is empty.
func checkOutput(t *testing.T, name, got, want string) {
	t.Helper()
	if want == "" {
		if got != "" {
			t.Errorf("%v %q, want empty", name, got)
		}
		return
	}
	if !strings.Contains(got, want) {
		t.Errorf("%v %q, want it to contain %q", name, got, want)
	}
}
