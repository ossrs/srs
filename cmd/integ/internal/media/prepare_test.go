// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT
package media

import (
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"runtime"
	"slices"
	"strings"
	"sync"
	"testing"
)

// fakePrepare prepares into a temporary dir with a fake transcoder that
// writes its arguments to the output file.
type fakePrepare struct {
	t      *testing.T
	dir    string
	source string
	mu     sync.Mutex
	made   []string
	fail   string // a recipe whose transcode fails
}

func newFakePrepare(t *testing.T) *fakePrepare {
	dir := t.TempDir()
	source := filepath.Join(dir, "source.flv")
	if err := os.WriteFile(source, []byte("the source"), 0o644); err != nil {
		t.Fatal(err)
	}
	return &fakePrepare{t: t, dir: filepath.Join(dir, "media"), source: source}
}

func (f *fakePrepare) run(ff *FFmpeg, recipes []Recipe) (Result, error) {
	f.mu.Lock()
	f.made = nil
	f.mu.Unlock()
	return Prepare(Options{
		Source:  f.source,
		Dir:     f.dir,
		FFmpeg:  ff,
		Recipes: recipes,
		Transcode: func(ff *FFmpeg, args []string) error {
			out := args[len(args)-1]
			name := strings.TrimSuffix(filepath.Base(out), ".tmp")
			f.mu.Lock()
			f.made = append(f.made, name)
			f.mu.Unlock()
			if strings.Contains(out, f.fail) && f.fail != "" {
				os.WriteFile(out, []byte("partial"), 0o644)
				return errors.New("exit status 1")
			}
			return os.WriteFile(out, []byte(strings.Join(args, " ")), 0o644)
		},
	})
}

func (f *fakePrepare) mustRun(ff *FFmpeg, recipes []Recipe) Result {
	f.t.Helper()
	res, err := f.run(ff, recipes)
	if err != nil {
		f.t.Fatalf("prepare: %v", err)
	}
	return res
}

func sorted(s []string) []string {
	s = slices.Clone(s)
	slices.Sort(s)
	return s
}

func names(recipes []Recipe) []string {
	var s []string
	for _, r := range recipes {
		s = append(s, r.Name)
	}
	return sorted(s)
}

func TestPrepareGeneratesThenKeeps(t *testing.T) {
	f := newFakePrepare(t)
	ff := fakeFFmpeg("v1", everyEncoder...)

	res := f.mustRun(ff, Recipes)
	if got, want := sorted(res.Generated), names(Recipes); !slices.Equal(got, want) {
		t.Fatalf("generated %v, want %v", got, want)
	}
	for _, r := range Recipes {
		if _, err := os.Stat(filepath.Join(f.dir, r.Name)); err != nil {
			t.Errorf("no %v: %v", r.Name, err)
		}
		if _, err := os.Stat(filepath.Join(f.dir, r.Name+".tmp")); err == nil {
			t.Errorf("%v.tmp left", r.Name)
		}
	}

	res = f.mustRun(ff, Recipes)
	if len(res.Generated) != 0 || len(f.made) != 0 || len(res.Kept) != len(Recipes) {
		t.Fatalf("second prepare generated %v, kept %v", res.Generated, res.Kept)
	}

	m, err := ReadManifest(f.dir)
	if err != nil {
		t.Fatal(err)
	}
	src, _ := HashSource(f.source)
	if m.Source != src || m.FFmpeg != "v1" || len(m.Files) != len(Recipes) {
		t.Errorf("manifest %+v", m)
	}
	for _, r := range Recipes {
		e := m.Files[r.Name]
		if e.Spec != r.Spec() || e.Source != src.SHA256 || e.FFmpeg != "v1" || e.Size == 0 || len(e.Args) == 0 {
			t.Errorf("%v: entry %+v", r.Name, e)
		}
	}
}

func TestPrepareRegeneratesOnlyAChangedRecipe(t *testing.T) {
	f := newFakePrepare(t)
	ff := fakeFFmpeg("v1", everyEncoder...)
	f.mustRun(ff, Recipes)

	changed := slices.Clone(Recipes)
	changed[1].Video = []Encoder{{"libx264", append(slices.Clone(changed[1].Video[0].Args), "-crf", "30")}}
	res := f.mustRun(ff, changed)
	if !slices.Equal(res.Generated, []string{changed[1].Name}) || !slices.Equal(f.made, []string{changed[1].Name}) {
		t.Fatalf("generated %v, transcoded %v, want only %v", res.Generated, f.made, changed[1].Name)
	}
	if len(res.Kept) != len(Recipes)-1 {
		t.Errorf("kept %v", res.Kept)
	}
}

func TestPrepareRegeneratesADeletedFile(t *testing.T) {
	f := newFakePrepare(t)
	ff := fakeFFmpeg("v1", everyEncoder...)
	f.mustRun(ff, Recipes)

	os.Remove(filepath.Join(f.dir, HEVCAAC))
	if res := f.mustRun(ff, Recipes); !slices.Equal(res.Generated, []string{HEVCAAC}) {
		t.Fatalf("generated %v", res.Generated)
	}

	// A file whose size differs from its entry, such as a truncated one, too.
	os.WriteFile(filepath.Join(f.dir, AACOnly), []byte("x"), 0o644)
	if res := f.mustRun(ff, Recipes); !slices.Equal(res.Generated, []string{AACOnly}) {
		t.Fatalf("generated %v", res.Generated)
	}
}

func TestPrepareRegeneratesAllOnANewSourceOrFFmpeg(t *testing.T) {
	f := newFakePrepare(t)
	f.mustRun(fakeFFmpeg("v1", everyEncoder...), Recipes)

	if res := f.mustRun(fakeFFmpeg("v2", everyEncoder...), Recipes); len(res.Generated) != len(Recipes) {
		t.Fatalf("new ffmpeg: generated %v", res.Generated)
	}

	os.WriteFile(f.source, []byte("another source"), 0o644)
	if res := f.mustRun(fakeFFmpeg("v2", everyEncoder...), Recipes); len(res.Generated) != len(Recipes) {
		t.Fatalf("new source: generated %v", res.Generated)
	}
}

func TestPrepareRecordsUnavailableRecipes(t *testing.T) {
	f := newFakePrepare(t)
	// Like the CI image: native Opus and G.711, no VP9 or AV1 encoder.
	ci := fakeFFmpeg("ci", "libx264", "libx265", "aac", "libmp3lame", "opus", "pcm_mulaw", "pcm_alaw")

	res := f.mustRun(ci, Recipes)
	if got := sorted(res.Unavailable); !slices.Equal(got, []string{AV1Opus, VP9Opus}) {
		t.Fatalf("unavailable %v", got)
	}
	m, _ := ReadManifest(f.dir)
	if e := m.Files[VP9Opus]; e.Unavailable != "libvpx-vp9" || e.Size != 0 {
		t.Errorf("vp9 entry %+v", e)
	}
	if e := m.Files[AV1Opus]; e.Unavailable != "libsvtav1" {
		t.Errorf("av1 entry %+v", e)
	}
	if _, err := os.Stat(filepath.Join(f.dir, VP9Opus)); err == nil {
		t.Errorf("an unavailable recipe has a file")
	}
	if e := m.Files[H264BaselineOpus]; !strings.Contains(strings.Join(e.Args, " "), "-c:a opus -strict -2") {
		t.Errorf("no native Opus fallback: %q", e.Args)
	}

	res = f.mustRun(ci, Recipes)
	if len(res.Generated) != 0 || len(res.Unavailable) != 2 {
		t.Fatalf("second run: generated %v, unavailable %v", res.Generated, res.Unavailable)
	}

	// An ffmpeg with every encoder makes them.
	res = f.mustRun(fakeFFmpeg("full", everyEncoder...), Recipes)
	if len(res.Unavailable) != 0 || !slices.Contains(res.Generated, AV1Opus) {
		t.Fatalf("full ffmpeg: generated %v, unavailable %v", res.Generated, res.Unavailable)
	}
}

func TestPrepareFailureKeepsNoFile(t *testing.T) {
	f := newFakePrepare(t)
	f.fail = HEVCAAC
	_, err := f.run(fakeFFmpeg("v1", everyEncoder...), Recipes)
	if err == nil || !strings.Contains(err.Error(), HEVCAAC) {
		t.Fatalf("err %v", err)
	}
	if _, err := os.Stat(filepath.Join(f.dir, HEVCAAC)); err == nil {
		t.Errorf("a failed transcode left its file")
	}
	if _, err := os.Stat(filepath.Join(f.dir, HEVCAAC+".tmp")); err == nil {
		t.Errorf("a failed transcode left its .tmp")
	}
	m, err := ReadManifest(f.dir)
	if err != nil {
		t.Fatalf("the files that worked are not recorded: %v", err)
	}
	if _, ok := m.Files[HEVCAAC]; ok {
		t.Errorf("the failed file is in the manifest")
	}
	if _, ok := m.Files[H264HighAAC]; !ok {
		t.Errorf("a file that worked is not in the manifest")
	}

	f.fail = ""
	if res := f.mustRun(fakeFFmpeg("v1", everyEncoder...), Recipes); !slices.Equal(res.Generated, []string{HEVCAAC}) {
		t.Fatalf("retry generated %v", res.Generated)
	}
}

func TestPrepareDropsARemovedRecipe(t *testing.T) {
	f := newFakePrepare(t)
	ff := fakeFFmpeg("v1", everyEncoder...)
	f.mustRun(ff, Recipes)

	f.mustRun(ff, Recipes[1:])
	m, _ := ReadManifest(f.dir)
	if _, ok := m.Files[Recipes[0].Name]; ok {
		t.Errorf("a removed recipe is still in the manifest")
	}
	if _, err := os.Stat(filepath.Join(f.dir, Recipes[0].Name)); err == nil {
		t.Errorf("a removed recipe's file is still there")
	}
}

func TestPrepareMissingSource(t *testing.T) {
	f := newFakePrepare(t)
	f.source = filepath.Join(f.dir, "nothing.flv")
	if _, err := f.run(fakeFFmpeg("v1", everyEncoder...), Recipes); err == nil {
		t.Fatalf("no error for a missing source")
	}
}

// fakeTB records how a helper ended a case.
type fakeTB struct {
	testing.TB
	fatal, skip string
}

func (f *fakeTB) Helper() {}
func (f *fakeTB) Fatalf(format string, args ...any) {
	f.fatal = fmt.Sprintf(format, args...)
	runtime.Goexit()
}
func (f *fakeTB) Skipf(format string, args ...any) {
	f.skip = fmt.Sprintf(format, args...)
	runtime.Goexit()
}

// callPath runs pathIn in its own goroutine, as a case would.
func callPath(f *fakePrepare, name string) (path string, tb *fakeTB) {
	tb = &fakeTB{}
	done := make(chan bool)
	go func() {
		defer close(done)
		path = pathIn(tb, f.dir, f.source, name)
	}()
	<-done
	return path, tb
}

func TestPathFindsAFreshFile(t *testing.T) {
	f := newFakePrepare(t)
	f.mustRun(fakeFFmpeg("ci", "libx264", "libx265", "aac", "libmp3lame", "opus"), Recipes)

	path, tb := callPath(f, H264HighAAC)
	if path != filepath.Join(f.dir, H264HighAAC) || tb.fatal != "" || tb.skip != "" {
		t.Errorf("path %q, fatal %q, skip %q", path, tb.fatal, tb.skip)
	}

	_, tb = callPath(f, AV1Opus)
	if tb.skip != "ffmpeg lacks libsvtav1" {
		t.Errorf("unavailable: skip %q, fatal %q", tb.skip, tb.fatal)
	}

	_, tb = callPath(f, "nothing.flv")
	if !strings.Contains(tb.fatal, "no recipe") {
		t.Errorf("unknown file: fatal %q", tb.fatal)
	}
}

func TestPathFailsWhenMissingOrStale(t *testing.T) {
	const hint = "run make integ-prepare"

	t.Run("NoManifest", func(t *testing.T) {
		f := newFakePrepare(t)
		if _, tb := callPath(f, H264HighAAC); !strings.Contains(tb.fatal, hint) {
			t.Errorf("fatal %q", tb.fatal)
		}
	})
	t.Run("NoFile", func(t *testing.T) {
		f := newFakePrepare(t)
		f.mustRun(fakeFFmpeg("v1", everyEncoder...), Recipes)
		os.Remove(filepath.Join(f.dir, H264HighAAC))
		if _, tb := callPath(f, H264HighAAC); !strings.Contains(tb.fatal, hint) {
			t.Errorf("fatal %q", tb.fatal)
		}
	})
	t.Run("NotInManifest", func(t *testing.T) {
		f := newFakePrepare(t)
		f.mustRun(fakeFFmpeg("v1", everyEncoder...), Recipes[1:])
		if _, tb := callPath(f, Recipes[0].Name); !strings.Contains(tb.fatal, hint) {
			t.Errorf("fatal %q", tb.fatal)
		}
	})
	t.Run("RecipeChanged", func(t *testing.T) {
		f := newFakePrepare(t)
		changed := slices.Clone(Recipes)
		changed[0].Format = "mpegts"
		f.mustRun(fakeFFmpeg("v1", everyEncoder...), changed)
		if _, tb := callPath(f, Recipes[0].Name); !strings.Contains(tb.fatal, hint) {
			t.Errorf("fatal %q", tb.fatal)
		}
	})
	t.Run("SourceChanged", func(t *testing.T) {
		f := newFakePrepare(t)
		f.mustRun(fakeFFmpeg("v1", everyEncoder...), Recipes)
		os.WriteFile(f.source, []byte("a new source"), 0o644)
		if _, tb := callPath(f, H264HighAAC); !strings.Contains(tb.fatal, hint) {
			t.Errorf("fatal %q", tb.fatal)
		}
	})
}
