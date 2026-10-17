// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT
package media

import (
	"bytes"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"sort"
	"strings"
	"sync"
	"time"
)

// ManifestFile is the cache manifest beside the files.
const ManifestFile = "manifest.json"

// A Manifest records how each file was made, so a later prepare regenerates
// only the files whose entry changed.
type Manifest struct {
	Source Source           `json:"source"`
	FFmpeg string           `json:"ffmpeg"`
	Files  map[string]Entry `json:"files"`
}

// Source identifies the source file by its size and SHA-256.
type Source struct {
	Size   int64  `json:"size"`
	SHA256 string `json:"sha256"`
}

// An Entry is one file of the manifest.
type Entry struct {
	// Spec is the recipe's Spec when the file was made.
	Spec string `json:"spec"`
	// Source and FFmpeg are the source's SHA-256 and the ffmpeg version.
	Source string `json:"source"`
	FFmpeg string `json:"ffmpeg"`
	// Args are the ffmpeg arguments, for people to read.
	Args []string `json:"args,omitempty"`
	// Size is the size of the file made.
	Size int64 `json:"size,omitempty"`
	// Unavailable names the encoders the ffmpeg lacks; there is no file.
	Unavailable string `json:"unavailable,omitempty"`
}

// Options of Prepare.
type Options struct {
	Source string
	Dir    string
	FFmpeg *FFmpeg
	// Recipes defaults to the test media set.
	Recipes []Recipe
	// Transcode runs ffmpeg; the default executes FFmpeg.Path.
	Transcode func(ff *FFmpeg, args []string) error
	// Jobs is how many transcodes run at once, 4 by default.
	Jobs int
	Log  io.Writer
}

// Result tells what Prepare did with each file.
type Result struct {
	Generated   []string
	Kept        []string
	Unavailable []string
}

// Prepare makes every file whose manifest entry changed, records the
// recipes the ffmpeg can't make as unavailable, and writes the manifest.
// When a transcode fails, the files that worked are still recorded.
func Prepare(o Options) (Result, error) {
	if o.Recipes == nil {
		o.Recipes = Recipes
	}
	if o.Transcode == nil {
		o.Transcode = transcode
	}
	if o.Jobs <= 0 {
		o.Jobs = 4
	}
	if o.Log == nil {
		o.Log = io.Discard
	}

	src, err := HashSource(o.Source)
	if err != nil {
		return Result{}, err
	}
	if err := os.MkdirAll(o.Dir, 0o755); err != nil {
		return Result{}, err
	}
	old, err := ReadManifest(o.Dir)
	if err != nil {
		old = &Manifest{}
	}
	m := &Manifest{Source: src, FFmpeg: o.FFmpeg.Version, Files: map[string]Entry{}}

	// A file of a recipe that was removed is removed too.
	for name := range old.Files {
		if _, ok := recipeIn(o.Recipes, name); !ok {
			os.Remove(filepath.Join(o.Dir, name))
		}
	}

	var res Result
	var todo []Recipe
	for _, r := range o.Recipes {
		out := filepath.Join(o.Dir, r.Name)
		args, missing := r.Args(o.FFmpeg, o.Source, out+".tmp")
		entry := Entry{Spec: r.Spec(), Source: src.SHA256, FFmpeg: o.FFmpeg.Version, Args: args, Unavailable: missing}

		if missing != "" {
			os.Remove(out)
			m.Files[r.Name] = entry
			res.Unavailable = append(res.Unavailable, r.Name)
			continue
		}
		if prev, ok := old.Files[r.Name]; ok && fresh(prev, entry, out) {
			m.Files[r.Name] = prev
			res.Kept = append(res.Kept, r.Name)
			continue
		}
		todo = append(todo, r)
	}

	var (
		mu   sync.Mutex
		errs []error
		wg   sync.WaitGroup
		jobs = make(chan struct{}, o.Jobs)
	)
	for _, r := range todo {
		wg.Add(1)
		go func() {
			defer wg.Done()
			jobs <- struct{}{}
			defer func() { <-jobs }()

			out := filepath.Join(o.Dir, r.Name)
			args, _ := r.Args(o.FFmpeg, o.Source, out+".tmp")
			start := time.Now()
			size, err := make1(o, args, out)

			mu.Lock()
			defer mu.Unlock()
			if err != nil {
				errs = append(errs, fmt.Errorf("make %v: %w", r.Name, err))
				return
			}
			fmt.Fprintf(o.Log, "made %v in %v\n", r.Name, time.Since(start).Round(time.Millisecond))
			m.Files[r.Name] = Entry{Spec: r.Spec(), Source: src.SHA256, FFmpeg: o.FFmpeg.Version, Args: args, Size: size}
			res.Generated = append(res.Generated, r.Name)
		}()
	}
	wg.Wait()

	sort.Strings(res.Generated)
	if err := writeManifest(o.Dir, m); err != nil {
		errs = append(errs, err)
	}
	return res, errors.Join(errs...)
}

// make1 transcodes into out.tmp, then renames it to out, so a failed or
// interrupted transcode never leaves a file that looks made.
func make1(o Options, args []string, out string) (int64, error) {
	tmp := out + ".tmp"
	os.Remove(out)
	if err := o.Transcode(o.FFmpeg, args); err != nil {
		os.Remove(tmp)
		return 0, err
	}
	st, err := os.Stat(tmp)
	if err != nil {
		return 0, err
	}
	if st.Size() == 0 {
		os.Remove(tmp)
		return 0, errors.New("ffmpeg made an empty file")
	}
	return st.Size(), os.Rename(tmp, out)
}

// fresh tells whether a file made as prev is still what entry would make.
func fresh(prev, entry Entry, path string) bool {
	if prev.Unavailable != "" || prev.Spec != entry.Spec || prev.Source != entry.Source || prev.FFmpeg != entry.FFmpeg {
		return false
	}
	st, err := os.Stat(path)
	return err == nil && st.Size() == prev.Size
}

func recipeIn(recipes []Recipe, name string) (Recipe, bool) {
	for _, r := range recipes {
		if r.Name == name {
			return r, true
		}
	}
	return Recipe{}, false
}

func transcode(ff *FFmpeg, args []string) error {
	var stderr bytes.Buffer
	cmd := exec.Command(ff.Path, args...)
	cmd.Stderr = &stderr
	if err := cmd.Run(); err != nil {
		return fmt.Errorf("%v %v: %w: %v", ff.Path, strings.Join(args, " "), err, strings.TrimSpace(stderr.String()))
	}
	return nil
}

// ReadManifest reads the manifest in dir.
func ReadManifest(dir string) (*Manifest, error) {
	b, err := os.ReadFile(filepath.Join(dir, ManifestFile))
	if err != nil {
		return nil, err
	}
	var m Manifest
	if err := json.Unmarshal(b, &m); err != nil {
		return nil, fmt.Errorf("read %v: %w", ManifestFile, err)
	}
	if m.Files == nil {
		m.Files = map[string]Entry{}
	}
	return &m, nil
}

func writeManifest(dir string, m *Manifest) error {
	b, err := json.MarshalIndent(m, "", "  ")
	if err != nil {
		return err
	}
	tmp := filepath.Join(dir, ManifestFile+".tmp")
	if err := os.WriteFile(tmp, append(b, '\n'), 0o644); err != nil {
		return err
	}
	return os.Rename(tmp, filepath.Join(dir, ManifestFile))
}

// HashSource returns the size and SHA-256 of the source.
func HashSource(path string) (Source, error) {
	f, err := os.Open(path)
	if err != nil {
		return Source{}, fmt.Errorf("the test media source: %w", err)
	}
	defer f.Close()
	h := sha256.New()
	n, err := io.Copy(h, f)
	if err != nil {
		return Source{}, fmt.Errorf("read %v: %w", path, err)
	}
	return Source{Size: n, SHA256: hex.EncodeToString(h.Sum(nil))}, nil
}
