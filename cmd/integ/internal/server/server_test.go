// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package server

import (
	"os"
	"path/filepath"
	"reflect"
	"testing"
)

func TestMissingFeatures(t *testing.T) {
	all := `{"code":0,"data":{"features":{"ssl":true,"srt":true,"rtsp":true,"gb28181":true,"rtc":true,"simulator":true,"sanitizer":true}}}`
	if got, err := missingFeatures([]byte(all)); err != nil || len(got) != 0 {
		t.Errorf("missingFeatures(all) = %v, %v", got, err)
	}

	// A false field and a field the server does not report are both missing.
	some := `{"code":0,"data":{"features":{"srt":false,"rtsp":true,"gb28181":true,"rtc":true,"sanitizer":true}}}`
	if got, err := missingFeatures([]byte(some)); err != nil || !reflect.DeepEqual(got, []string{"srt", "simulator"}) {
		t.Errorf("missingFeatures(some) = %v, %v; want [srt simulator]", got, err)
	}

	if _, err := missingFeatures([]byte(`not json`)); err == nil {
		t.Error("missingFeatures(not json) has no error")
	}
	if _, err := missingFeatures([]byte(`{"code":1}`)); err == nil {
		t.Error("missingFeatures(code 1) has no error")
	}
}

func TestCleanStaleRuns(t *testing.T) {
	root := t.TempDir()
	for _, name := range []string{"100", "200", "keep", "300"} {
		if err := os.MkdirAll(filepath.Join(root, name, "srs-1"), 0o755); err != nil {
			t.Fatal(err)
		}
	}
	if err := os.WriteFile(filepath.Join(root, "400"), nil, 0o644); err != nil {
		t.Fatal(err)
	}

	cleanStaleRuns(root, func(pid int) bool { return pid == 200 })

	for name, want := range map[string]bool{"100": false, "200": true, "keep": true, "300": false, "400": true} {
		_, err := os.Stat(filepath.Join(root, name))
		if exists := err == nil; exists != want {
			t.Errorf("%s exists=%v, want %v", name, exists, want)
		}
	}
}
