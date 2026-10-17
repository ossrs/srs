// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT
package media

import (
	"fmt"
	"os"
	"path/filepath"
	"runtime"
	"sync"
	"testing"
	"time"
)

// prepareHint is how to make missing or stale media.
const prepareHint = "run make integ-prepare"

// Dir is where prepare writes the test media, testdata/media of the module.
func Dir() string {
	return filepath.Join(moduleRoot(), "testdata", "media")
}

// SourcePath is the source of the test media, doc/source.flv of the
// repository.
func SourcePath() string {
	return filepath.Join(moduleRoot(), "..", "..", "doc", "source.flv")
}

// moduleRoot returns the directory of the srs-integ module.
func moduleRoot() string {
	_, file, _, _ := runtime.Caller(0)
	return filepath.Dir(filepath.Dir(filepath.Dir(file)))
}

// Path returns the path of a test media file. It fails the case with "run
// make integ-prepare" when the file is missing or stale, and skips it when
// the ffmpeg that prepared the media lacks the encoder of its recipe.
func Path(tb testing.TB, name string) string {
	tb.Helper()
	return pathIn(tb, Dir(), SourcePath(), name)
}

func pathIn(tb testing.TB, dir, source, name string) string {
	tb.Helper()
	r, ok := RecipeOf(name)
	if !ok {
		tb.Fatalf("no recipe makes %v", name)
	}
	src, err := sourceHash(source)
	if err != nil {
		tb.Fatalf("%v", err)
	}
	m, err := ReadManifest(dir)
	if err != nil {
		tb.Fatalf("no test media in %v (%v), %v", dir, err, prepareHint)
	}
	e, ok := m.Files[name]
	if !ok || e.Spec != r.Spec() || e.Source != src.SHA256 {
		tb.Fatalf("test media %v is missing or stale, %v", name, prepareHint)
	}
	if e.Unavailable != "" {
		tb.Skipf("ffmpeg lacks %v", e.Unavailable)
	}
	path := filepath.Join(dir, name)
	if st, err := os.Stat(path); err != nil || st.Size() != e.Size {
		tb.Fatalf("test media %v is missing or changed, %v", name, prepareHint)
	}
	return path
}

// The hash of each source, read once per process while the file's size and
// modification time stay the same.
var sources sync.Map // map[string]sourceKey

type sourceKey struct {
	size  int64
	mtime time.Time
	src   Source
}

func sourceHash(path string) (Source, error) {
	st, err := os.Stat(path)
	if err != nil {
		return Source{}, fmt.Errorf("the test media source: %w", err)
	}
	if v, ok := sources.Load(path); ok {
		if k := v.(sourceKey); k.size == st.Size() && k.mtime.Equal(st.ModTime()) {
			return k.src, nil
		}
	}
	src, err := HashSource(path)
	if err != nil {
		return Source{}, err
	}
	sources.Store(path, sourceKey{st.Size(), st.ModTime(), src})
	return src, nil
}
