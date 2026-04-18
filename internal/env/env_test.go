// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT
package env

import (
	"context"
	"os"
	"path/filepath"
	"testing"
)

func TestParseEnvFile_BasicKeyValue(t *testing.T) {
	f := writeTempEnv(t, "FOO=bar\nHELLO=world\n")
	m, err := parseEnvFile(f)
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

func TestParseEnvFile_SkipCommentsAndBlankLines(t *testing.T) {
	f := writeTempEnv(t, "# this is a comment\n\nKEY=value\n\n# another comment\n")
	m, err := parseEnvFile(f)
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

func TestParseEnvFile_ExportPrefix(t *testing.T) {
	f := writeTempEnv(t, "export PORT=8080\nexport HOST=localhost\n")
	m, err := parseEnvFile(f)
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

func TestParseEnvFile_SingleQuoted(t *testing.T) {
	f := writeTempEnv(t, "KEY='hello world'\nRAW='no\\nescaping'\n")
	m, err := parseEnvFile(f)
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if m["KEY"] != "hello world" {
		t.Errorf("KEY = %q, want %q", m["KEY"], "hello world")
	}
	// Single quotes: backslash-n stays literal.
	if m["RAW"] != `no\nescaping` {
		t.Errorf("RAW = %q, want %q", m["RAW"], `no\nescaping`)
	}
}

func TestParseEnvFile_DoubleQuoted(t *testing.T) {
	f := writeTempEnv(t, `KEY="hello world"`+"\n"+`MSG="line1\nline2"`+"\n")
	m, err := parseEnvFile(f)
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

func TestParseEnvFile_DoubleQuotedEscapes(t *testing.T) {
	f := writeTempEnv(t, `KEY="say \"hi\""`+"\n"+`BS="back\\slash"`+"\n"+`CR="a\rb"`+"\n")
	m, err := parseEnvFile(f)
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

func TestParseEnvFile_InlineComment(t *testing.T) {
	f := writeTempEnv(t, "KEY=value # this is a comment\nNUM=42 # the answer\n")
	m, err := parseEnvFile(f)
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

func TestParseEnvFile_NoEqualsSign(t *testing.T) {
	f := writeTempEnv(t, "NOEQUALS\nKEY=value\n")
	m, err := parseEnvFile(f)
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

func TestParseEnvFile_EmptyValue(t *testing.T) {
	f := writeTempEnv(t, "KEY=\n")
	m, err := parseEnvFile(f)
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if v, ok := m["KEY"]; !ok || v != "" {
		t.Errorf("KEY = %q (ok=%v), want empty string", v, ok)
	}
}

func TestParseEnvFile_ValueWithEquals(t *testing.T) {
	f := writeTempEnv(t, "URL=postgres://host:5432/db?opt=val\n")
	m, err := parseEnvFile(f)
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if m["URL"] != "postgres://host:5432/db?opt=val" {
		t.Errorf("URL = %q, want %q", m["URL"], "postgres://host:5432/db?opt=val")
	}
}

func TestParseEnvFile_FileNotFound(t *testing.T) {
	_, err := parseEnvFile("/nonexistent/.env")
	if err == nil {
		t.Fatal("expected error for missing file")
	}
	if !os.IsNotExist(err) {
		t.Errorf("expected os.IsNotExist, got: %v", err)
	}
}

func TestParseEnvFile_WhitespaceAroundKeyValue(t *testing.T) {
	f := writeTempEnv(t, "  KEY  =  value  \n")
	m, err := parseEnvFile(f)
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if m["KEY"] != "value" {
		t.Errorf("KEY = %q, want %q", m["KEY"], "value")
	}
}

func TestLoadEnvFile_DoesNotOverwriteExisting(t *testing.T) {
	dir := t.TempDir()
	if err := os.WriteFile(filepath.Join(dir, ".env"), []byte("TEST_EXISTING=fromfile\nTEST_NEW=fromfile\n"), 0644); err != nil {
		t.Fatalf("write .env: %v", err)
	}
	t.Chdir(dir)

	t.Setenv("TEST_EXISTING", "fromshell")
	unsetEnv(t, "TEST_NEW")

	if err := loadEnvFile(context.Background()); err != nil {
		t.Fatalf("loadEnvFile: %v", err)
	}

	if got := os.Getenv("TEST_EXISTING"); got != "fromshell" {
		t.Errorf("TEST_EXISTING = %q, want %q (should not overwrite)", got, "fromshell")
	}
	if got := os.Getenv("TEST_NEW"); got != "fromfile" {
		t.Errorf("TEST_NEW = %q, want %q", got, "fromfile")
	}
}

func TestParseEnvFile_ShortValue(t *testing.T) {
	// Single-character value exercises the len(value) < 2 short-value branch.
	f := writeTempEnv(t, "A=x\nB=y\n")
	m, err := parseEnvFile(f)
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

func TestSetEnvDefault_SetsWhenEmpty(t *testing.T) {
	const key = "TEST_SET_ENV_DEFAULT_EMPTY"
	// t.Setenv both guards against t.Parallel and auto-restores the
	// original value on cleanup. setEnvDefault treats "" as unset.
	t.Setenv(key, "")
	setEnvDefault(key, "defaultVal")
	if got := os.Getenv(key); got != "defaultVal" {
		t.Errorf("%s = %q, want %q", key, got, "defaultVal")
	}
}

func TestSetEnvDefault_PreservesExisting(t *testing.T) {
	const key = "TEST_SET_ENV_DEFAULT_EXISTING"
	t.Setenv(key, "original")
	setEnvDefault(key, "shouldNotApply")
	if got := os.Getenv(key); got != "original" {
		t.Errorf("%s = %q, want %q", key, got, "original")
	}
}

func TestLoadEnvFile_NoFileIsNoError(t *testing.T) {
	// Run in a temp dir with no .env present.
	t.Chdir(t.TempDir())
	if err := loadEnvFile(context.Background()); err != nil {
		t.Errorf("unexpected error: %v", err)
	}
}

func TestLoadEnvFile_AppliesFromFile(t *testing.T) {
	const key = "TEST_LOAD_ENV_FILE_APPLIES"
	unsetEnv(t, key)

	dir := t.TempDir()
	if err := os.WriteFile(filepath.Join(dir, ".env"), []byte(key+"=loaded\n"), 0644); err != nil {
		t.Fatalf("write .env: %v", err)
	}
	t.Chdir(dir)

	if err := loadEnvFile(context.Background()); err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if got := os.Getenv(key); got != "loaded" {
		t.Errorf("%s = %q, want %q", key, got, "loaded")
	}
}

func TestNewEnvironment_AppliesDefaultsAndAccessors(t *testing.T) {
	// Work in a clean dir so no stray .env is picked up.
	t.Chdir(t.TempDir())

	// Keys with a default in buildDefaultEnvironmentVariables.
	defaults := map[string]string{
		"GO_PPROF":                      "",
		"PROXY_FORCE_QUIT_TIMEOUT":      "30s",
		"PROXY_GRACE_QUIT_TIMEOUT":      "20s",
		"PROXY_HTTP_API":                "11985",
		"PROXY_HTTP_SERVER":             "18080",
		"PROXY_RTMP_SERVER":             "11935",
		"PROXY_WEBRTC_SERVER":           "18000",
		"PROXY_SRT_SERVER":              "20080",
		"PROXY_SYSTEM_API":              "12025",
		"PROXY_STATIC_FILES":            "./trunk/research",
		"PROXY_LOAD_BALANCER_TYPE":      "memory",
		"PROXY_REDIS_HOST":              "127.0.0.1",
		"PROXY_REDIS_PORT":              "6379",
		"PROXY_REDIS_PASSWORD":          "",
		"PROXY_REDIS_DB":                "0",
		"PROXY_DEFAULT_BACKEND_ENABLED": "off",
		"PROXY_DEFAULT_BACKEND_IP":      "127.0.0.1",
		"PROXY_DEFAULT_BACKEND_RTMP":    "1935",
		"PROXY_DEFAULT_BACKEND_API":     "1985",
		"PROXY_DEFAULT_BACKEND_RTC":     "8000",
		"PROXY_DEFAULT_BACKEND_SRT":     "10080",
	}
	// Clear defaults so setEnvDefault actually applies them.
	for k := range defaults {
		t.Setenv(k, "")
	}
	// PROXY_DEFAULT_BACKEND_HTTP has no default; set it explicitly so the
	// accessor has a value to return.
	t.Setenv("PROXY_DEFAULT_BACKEND_HTTP", "8080")

	env, err := NewEnvironment(context.Background())
	if err != nil {
		t.Fatalf("NewEnvironment: %v", err)
	}

	// Verify every accessor returns the expected value.
	cases := []struct {
		name string
		got  string
		want string
	}{
		{"GoPprof", env.GoPprof(), defaults["GO_PPROF"]},
		{"GraceQuitTimeout", env.GraceQuitTimeout(), defaults["PROXY_GRACE_QUIT_TIMEOUT"]},
		{"ForceQuitTimeout", env.ForceQuitTimeout(), defaults["PROXY_FORCE_QUIT_TIMEOUT"]},
		{"HttpAPI", env.HttpAPI(), defaults["PROXY_HTTP_API"]},
		{"HttpServer", env.HttpServer(), defaults["PROXY_HTTP_SERVER"]},
		{"RtmpServer", env.RtmpServer(), defaults["PROXY_RTMP_SERVER"]},
		{"WebRTCServer", env.WebRTCServer(), defaults["PROXY_WEBRTC_SERVER"]},
		{"SRTServer", env.SRTServer(), defaults["PROXY_SRT_SERVER"]},
		{"SystemAPI", env.SystemAPI(), defaults["PROXY_SYSTEM_API"]},
		{"StaticFiles", env.StaticFiles(), defaults["PROXY_STATIC_FILES"]},
		{"LoadBalancerType", env.LoadBalancerType(), defaults["PROXY_LOAD_BALANCER_TYPE"]},
		{"RedisHost", env.RedisHost(), defaults["PROXY_REDIS_HOST"]},
		{"RedisPort", env.RedisPort(), defaults["PROXY_REDIS_PORT"]},
		{"RedisPassword", env.RedisPassword(), defaults["PROXY_REDIS_PASSWORD"]},
		{"RedisDB", env.RedisDB(), defaults["PROXY_REDIS_DB"]},
		{"DefaultBackendEnabled", env.DefaultBackendEnabled(), defaults["PROXY_DEFAULT_BACKEND_ENABLED"]},
		{"DefaultBackendIP", env.DefaultBackendIP(), defaults["PROXY_DEFAULT_BACKEND_IP"]},
		{"DefaultBackendRTMP", env.DefaultBackendRTMP(), defaults["PROXY_DEFAULT_BACKEND_RTMP"]},
		{"DefaultBackendHttp", env.DefaultBackendHttp(), "8080"},
		{"DefaultBackendAPI", env.DefaultBackendAPI(), defaults["PROXY_DEFAULT_BACKEND_API"]},
		{"DefaultBackendRTC", env.DefaultBackendRTC(), defaults["PROXY_DEFAULT_BACKEND_RTC"]},
		{"DefaultBackendSRT", env.DefaultBackendSRT(), defaults["PROXY_DEFAULT_BACKEND_SRT"]},
	}
	for _, c := range cases {
		if c.got != c.want {
			t.Errorf("%s() = %q, want %q", c.name, c.got, c.want)
		}
	}
}

func TestNewEnvironment_PreservesPreSetValues(t *testing.T) {
	// When a var is already set, setEnvDefault must not overwrite it.
	t.Chdir(t.TempDir())
	t.Setenv("PROXY_HTTP_API", "9999")

	env, err := NewEnvironment(context.Background())
	if err != nil {
		t.Fatalf("NewEnvironment: %v", err)
	}
	if got := env.HttpAPI(); got != "9999" {
		t.Errorf("HttpAPI() = %q, want %q", got, "9999")
	}
}

// unsetEnv removes key for the duration of the test and restores its
// original value (or unset state) on cleanup. It also calls t.Setenv on
// a sentinel so the test will panic if anyone adds t.Parallel() — env
// vars are process-global and cannot be mutated safely in parallel.
func unsetEnv(t *testing.T, key string) {
	t.Helper()
	t.Setenv("_ENV_TEST_NO_PARALLEL", "1")
	prev, had := os.LookupEnv(key)
	os.Unsetenv(key)
	t.Cleanup(func() {
		if had {
			os.Setenv(key, prev)
		} else {
			os.Unsetenv(key)
		}
	})
}

// writeTempEnv writes content to a temp .env file and returns the path.
func writeTempEnv(t *testing.T, content string) string {
	t.Helper()
	dir := t.TempDir()
	f := filepath.Join(dir, ".env")
	if err := os.WriteFile(f, []byte(content), 0644); err != nil {
		t.Fatalf("write temp .env: %v", err)
	}
	return f
}
