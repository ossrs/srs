// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT
package env

import (
	"os"
	"path/filepath"
	"strings"
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
	// Write a .env file in a temp dir.
	dir := t.TempDir()
	envFile := filepath.Join(dir, ".env")
	if err := os.WriteFile(envFile, []byte("TEST_EXISTING=fromfile\nTEST_NEW=fromfile\n"), 0644); err != nil {
		t.Fatalf("write .env: %v", err)
	}

	// Pre-set one of the keys in the real environment.
	os.Setenv("TEST_EXISTING", "fromshell")
	t.Cleanup(func() {
		os.Unsetenv("TEST_EXISTING")
		os.Unsetenv("TEST_NEW")
	})

	// Parse and apply, mimicking loadEnvFile logic.
	m, err := parseEnvFile(envFile)
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}

	currentEnv := make(map[string]bool)
	for _, entry := range os.Environ() {
		k, _, _ := strings.Cut(entry, "=")
		currentEnv[k] = true
	}
	for k, v := range m {
		if !currentEnv[k] {
			os.Setenv(k, v)
		}
	}

	// Existing key should NOT be overwritten.
	if got := os.Getenv("TEST_EXISTING"); got != "fromshell" {
		t.Errorf("TEST_EXISTING = %q, want %q (should not overwrite)", got, "fromshell")
	}
	// New key should be set.
	if got := os.Getenv("TEST_NEW"); got != "fromfile" {
		t.Errorf("TEST_NEW = %q, want %q", got, "fromfile")
	}
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
