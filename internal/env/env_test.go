// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT
package env

import (
	"context"
	"errors"
	"io"
	"os"
	"strings"
	"testing"

	srserrors "srsx/internal/errors"
)

// fakeEnv is an in-memory replacement for process environment variables.
// Tests install it via withFakeEnv so no real os.Setenv/os.Getenv call is
// ever made, which keeps tests hermetic and free of global side effects.
type fakeEnv struct {
	store map[string]string
}

func (f *fakeEnv) get(k string) string   { return f.store[k] }
func (f *fakeEnv) set(k, v string) error { f.store[k] = v; return nil }
func (f *fakeEnv) lookup(k string) (string, bool) {
	v, ok := f.store[k]
	return v, ok
}

// withFakeEnv swaps getEnv/setEnv/lookupEnv to an in-memory map for the
// duration of the test and restores the originals on cleanup.
func withFakeEnv(t *testing.T) *fakeEnv {
	t.Helper()
	fe := &fakeEnv{store: map[string]string{}}
	origGet, origSet, origLookup := getEnv, setEnv, lookupEnv
	getEnv, setEnv, lookupEnv = fe.get, fe.set, fe.lookup
	t.Cleanup(func() {
		getEnv, setEnv, lookupEnv = origGet, origSet, origLookup
	})
	return fe
}

// withFakeOpen swaps openFile to return either content or err, and
// restores the original on cleanup. If err is non-nil, content is ignored.
func withFakeOpen(t *testing.T, content string, err error) {
	t.Helper()
	orig := openFile
	openFile = func(string) (io.ReadCloser, error) {
		if err != nil {
			return nil, err
		}
		return io.NopCloser(strings.NewReader(content)), nil
	}
	t.Cleanup(func() { openFile = orig })
}

func TestParseEnvReader_BasicKeyValue(t *testing.T) {
	m, err := parseEnvReader(strings.NewReader("FOO=bar\nHELLO=world\n"))
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if m["FOO"] != "bar" {
		t.Errorf("FOO = %q, want %q", m["FOO"], "bar")
	}
	if m["HELLO"] != "world" {
		t.Errorf("HELLO = %q, want %q", m["HELLO"], "world")
	}
}

func TestParseEnvReader_SkipCommentsAndBlankLines(t *testing.T) {
	m, err := parseEnvReader(strings.NewReader("# this is a comment\n\nKEY=value\n\n# another comment\n"))
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if len(m) != 1 {
		t.Errorf("got %d entries, want 1", len(m))
	}
	if m["KEY"] != "value" {
		t.Errorf("KEY = %q, want %q", m["KEY"], "value")
	}
}

func TestParseEnvReader_ExportPrefix(t *testing.T) {
	m, err := parseEnvReader(strings.NewReader("export PORT=8080\nexport HOST=localhost\n"))
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if m["PORT"] != "8080" {
		t.Errorf("PORT = %q, want %q", m["PORT"], "8080")
	}
	if m["HOST"] != "localhost" {
		t.Errorf("HOST = %q, want %q", m["HOST"], "localhost")
	}
}

func TestParseEnvReader_SingleQuoted(t *testing.T) {
	m, err := parseEnvReader(strings.NewReader("KEY='hello world'\nRAW='no\\nescaping'\n"))
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if m["KEY"] != "hello world" {
		t.Errorf("KEY = %q, want %q", m["KEY"], "hello world")
	}
	if m["RAW"] != `no\nescaping` {
		t.Errorf("RAW = %q, want %q", m["RAW"], `no\nescaping`)
	}
}

func TestParseEnvReader_DoubleQuoted(t *testing.T) {
	m, err := parseEnvReader(strings.NewReader(`KEY="hello world"` + "\n" + `MSG="line1\nline2"` + "\n"))
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if m["KEY"] != "hello world" {
		t.Errorf("KEY = %q, want %q", m["KEY"], "hello world")
	}
	if m["MSG"] != "line1\nline2" {
		t.Errorf("MSG = %q, want %q", m["MSG"], "line1\nline2")
	}
}

func TestParseEnvReader_DoubleQuotedEscapes(t *testing.T) {
	m, err := parseEnvReader(strings.NewReader(`KEY="say \"hi\""` + "\n" + `BS="back\\slash"` + "\n" + `CR="a\rb"` + "\n"))
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if m["KEY"] != `say "hi"` {
		t.Errorf("KEY = %q, want %q", m["KEY"], `say "hi"`)
	}
	if m["BS"] != `back\slash` {
		t.Errorf("BS = %q, want %q", m["BS"], `back\slash`)
	}
	if m["CR"] != "a\rb" {
		t.Errorf("CR = %q, want %q", m["CR"], "a\rb")
	}
}

func TestParseEnvReader_InlineComment(t *testing.T) {
	m, err := parseEnvReader(strings.NewReader("KEY=value # this is a comment\nNUM=42 # the answer\n"))
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if m["KEY"] != "value" {
		t.Errorf("KEY = %q, want %q", m["KEY"], "value")
	}
	if m["NUM"] != "42" {
		t.Errorf("NUM = %q, want %q", m["NUM"], "42")
	}
}

func TestParseEnvReader_NoEqualsSign(t *testing.T) {
	m, err := parseEnvReader(strings.NewReader("NOEQUALS\nKEY=value\n"))
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if len(m) != 1 {
		t.Errorf("got %d entries, want 1", len(m))
	}
	if m["KEY"] != "value" {
		t.Errorf("KEY = %q, want %q", m["KEY"], "value")
	}
}

func TestParseEnvReader_EmptyValue(t *testing.T) {
	m, err := parseEnvReader(strings.NewReader("KEY=\n"))
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if v, ok := m["KEY"]; !ok || v != "" {
		t.Errorf("KEY = %q (ok=%v), want empty string", v, ok)
	}
}

func TestParseEnvReader_ValueWithEquals(t *testing.T) {
	m, err := parseEnvReader(strings.NewReader("URL=postgres://host:5432/db?opt=val\n"))
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if m["URL"] != "postgres://host:5432/db?opt=val" {
		t.Errorf("URL = %q, want %q", m["URL"], "postgres://host:5432/db?opt=val")
	}
}

func TestParseEnvReader_WhitespaceAroundKeyValue(t *testing.T) {
	m, err := parseEnvReader(strings.NewReader("  KEY  =  value  \n"))
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if m["KEY"] != "value" {
		t.Errorf("KEY = %q, want %q", m["KEY"], "value")
	}
}

func TestParseEnvReader_ShortValue(t *testing.T) {
	// Single-character value exercises the len(value) < 2 short-value branch.
	m, err := parseEnvReader(strings.NewReader("A=x\nB=y\n"))
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if m["A"] != "x" {
		t.Errorf("A = %q, want %q", m["A"], "x")
	}
	if m["B"] != "y" {
		t.Errorf("B = %q, want %q", m["B"], "y")
	}
}

func TestParseEnvFile_FileNotFound(t *testing.T) {
	withFakeOpen(t, "", os.ErrNotExist)
	_, err := parseEnvFile(".env")
	if !errors.Is(err, os.ErrNotExist) {
		t.Errorf("expected os.ErrNotExist, got: %v", err)
	}
}

func TestParseEnvFile_OpenError(t *testing.T) {
	// A non-NotExist open error should bubble up as-is.
	sentinel := errors.New("boom")
	withFakeOpen(t, "", sentinel)
	_, err := parseEnvFile(".env")
	if !errors.Is(err, sentinel) {
		t.Errorf("expected sentinel error, got: %v", err)
	}
}

func TestParseEnvFile_DelegatesToReader(t *testing.T) {
	withFakeOpen(t, "FOO=bar\n", nil)
	m, err := parseEnvFile(".env")
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if m["FOO"] != "bar" {
		t.Errorf("FOO = %q, want %q", m["FOO"], "bar")
	}
}

func TestLoadEnvFile_DoesNotOverwriteExisting(t *testing.T) {
	fe := withFakeEnv(t)
	fe.store["TEST_EXISTING"] = "fromshell"
	// TEST_NEW is absent from the store, so it should be loaded from the file.
	withFakeOpen(t, "TEST_EXISTING=fromfile\nTEST_NEW=fromfile\n", nil)

	if err := loadEnvFile(context.Background()); err != nil {
		t.Fatalf("loadEnvFile: %v", err)
	}
	if got := fe.store["TEST_EXISTING"]; got != "fromshell" {
		t.Errorf("TEST_EXISTING = %q, want %q (should not overwrite)", got, "fromshell")
	}
	if got := fe.store["TEST_NEW"]; got != "fromfile" {
		t.Errorf("TEST_NEW = %q, want %q", got, "fromfile")
	}
}

func TestLoadEnvFile_NoFileIsNoError(t *testing.T) {
	withFakeEnv(t)
	withFakeOpen(t, "", os.ErrNotExist)
	if err := loadEnvFile(context.Background()); err != nil {
		t.Errorf("unexpected error: %v", err)
	}
}

func TestLoadEnvFile_OpenErrorIsWrapped(t *testing.T) {
	withFakeEnv(t)
	sentinel := errors.New("disk gone")
	withFakeOpen(t, "", sentinel)
	err := loadEnvFile(context.Background())
	if err == nil {
		t.Fatal("expected error, got nil")
	}
	if srserrors.Cause(err) != sentinel {
		t.Errorf("expected wrapped sentinel, got: %v", err)
	}
}

func TestLoadEnvFile_AppliesFromFile(t *testing.T) {
	fe := withFakeEnv(t)
	withFakeOpen(t, "TEST_LOAD_ENV_FILE_APPLIES=loaded\n", nil)

	if err := loadEnvFile(context.Background()); err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if got := fe.store["TEST_LOAD_ENV_FILE_APPLIES"]; got != "loaded" {
		t.Errorf("got %q, want %q", got, "loaded")
	}
}

func TestSetEnvDefault_SetsWhenEmpty(t *testing.T) {
	fe := withFakeEnv(t)
	// Key is absent (getEnv returns ""), so the default should apply.
	setEnvDefault("KEY", "defaultVal")
	if got := fe.store["KEY"]; got != "defaultVal" {
		t.Errorf("KEY = %q, want %q", got, "defaultVal")
	}
}

func TestSetEnvDefault_PreservesExisting(t *testing.T) {
	fe := withFakeEnv(t)
	fe.store["KEY"] = "original"
	setEnvDefault("KEY", "shouldNotApply")
	if got := fe.store["KEY"]; got != "original" {
		t.Errorf("KEY = %q, want %q", got, "original")
	}
}

func TestNewProxyEnvironment_AppliesDefaultsAndAccessors(t *testing.T) {
	withFakeEnv(t)
	// No .env file present.
	withFakeOpen(t, "", os.ErrNotExist)

	// PROXY_DEFAULT_BACKEND_HTTP has no default in buildDefaultEnvironmentVariables;
	// pre-set it so the accessor has a value to return.
	setEnv("PROXY_DEFAULT_BACKEND_HTTP", "8080")

	env, err := NewProxyEnvironment(context.Background())
	if err != nil {
		t.Fatalf("NewProxyEnvironment: %v", err)
	}

	cases := []struct {
		name string
		got  string
		want string
	}{
		{"GoPprof", env.GoPprof(), ""},
		{"GraceQuitTimeout", env.GraceQuitTimeout(), "20s"},
		{"ForceQuitTimeout", env.ForceQuitTimeout(), "30s"},
		{"HttpAPI", env.HttpAPI(), "11985"},
		{"HttpServer", env.HttpServer(), "18080"},
		{"RtmpServer", env.RtmpServer(), "11935"},
		{"WebRTCServer", env.WebRTCServer(), "18000"},
		{"SRTServer", env.SRTServer(), "20080"},
		{"SystemAPI", env.SystemAPI(), "12025"},
		{"StaticFiles", env.StaticFiles(), "./trunk/research"},
		{"LoadBalancerType", env.LoadBalancerType(), "memory"},
		{"RedisHost", env.RedisHost(), "127.0.0.1"},
		{"RedisPort", env.RedisPort(), "6379"},
		{"RedisPassword", env.RedisPassword(), ""},
		{"RedisDB", env.RedisDB(), "0"},
		{"RedisKeyPrefix", env.RedisKeyPrefix(), ""},
		{"OriginServerTTL", env.OriginServerTTL(), "300s"},
		{"DefaultBackendEnabled", env.DefaultBackendEnabled(), "off"},
		{"DefaultBackendIP", env.DefaultBackendIP(), "127.0.0.1"},
		{"DefaultBackendRTMP", env.DefaultBackendRTMP(), "1935"},
		{"DefaultBackendHttp", env.DefaultBackendHttp(), "8080"},
		{"DefaultBackendAPI", env.DefaultBackendAPI(), "1985"},
		{"DefaultBackendRTC", env.DefaultBackendRTC(), "8000"},
		{"DefaultBackendSRT", env.DefaultBackendSRT(), "10080"},
	}
	for _, c := range cases {
		if c.got != c.want {
			t.Errorf("%s() = %q, want %q", c.name, c.got, c.want)
		}
	}
}

func TestNewProxyEnvironment_PreservesPreSetValues(t *testing.T) {
	withFakeEnv(t)
	withFakeOpen(t, "", os.ErrNotExist)
	setEnv("PROXY_HTTP_API", "9999")
	setEnv("PROXY_REDIS_KEY_PREFIX", "xxx")
	setEnv("PROXY_ORIGIN_SERVER_TTL", "45s")

	env, err := NewProxyEnvironment(context.Background())
	if err != nil {
		t.Fatalf("NewProxyEnvironment: %v", err)
	}
	if got := env.HttpAPI(); got != "9999" {
		t.Errorf("HttpAPI() = %q, want %q", got, "9999")
	}
	if got := env.RedisKeyPrefix(); got != "xxx" {
		t.Errorf("RedisKeyPrefix() = %q, want %q", got, "xxx")
	}
	if got := env.OriginServerTTL(); got != "45s" {
		t.Errorf("OriginServerTTL() = %q, want %q", got, "45s")
	}
}

func TestNewProxyEnvironment_LoadEnvFailurePropagates(t *testing.T) {
	withFakeEnv(t)
	sentinel := errors.New("open failed")
	withFakeOpen(t, "", sentinel)

	_, err := NewProxyEnvironment(context.Background())
	if srserrors.Cause(err) != sentinel {
		t.Errorf("expected wrapped sentinel, got: %v", err)
	}
}
