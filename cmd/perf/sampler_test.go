package main

import (
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"testing"
	"time"
)

func writeProcFixture(t *testing.T, root string, pid int, stat, status string) {
	t.Helper()
	dir := filepath.Join(root, "proc", strconv.Itoa(pid))
	if err := os.MkdirAll(dir, 0755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(dir, "stat"), []byte(stat), 0644); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(dir, "status"), []byte(status), 0644); err != nil {
		t.Fatal(err)
	}
}

func TestProcSampler(t *testing.T) {
	root := t.TempDir()
	writeProcFixture(t, root, 123, "123 (srs (media)) S 1 1 1 0 0 0 0 0 0 0 100 50 0 0 0 0 0 0 0 0\n", "Name:\tsrs\nVmRSS:\t1024 kB\n")
	writeProcFixture(t, root, 456, "456 (worker) S 1 1 1 0 0 0 0 0 0 0 20 10 0 0 0 0 0 0 0 0\n", "Name:\tworker\nVmRSS:\t512 kB\n")
	s := newProcSampler(filepath.Join(root, "proc"), 123, []int{456}, 100)
	start := time.Unix(100, 0)
	first, err := s.sampleAt(start)
	if err != nil {
		t.Fatal(err)
	}
	if first.srsCPUPercent != 0 || first.srsRSSBytes != 1024*1024 || len(first.workerCPUPercent) != 1 || first.workerCPUPercent[0] != 0 {
		t.Fatalf("first sample = %+v", first)
	}
	writeProcFixture(t, root, 123, "123 (srs (media)) S 1 1 1 0 0 0 0 0 0 0 160 90 0 0 0 0 0 0 0 0\n", "Name:\tsrs\nVmRSS:\t2048 kB\n")
	writeProcFixture(t, root, 456, "456 (worker) S 1 1 1 0 0 0 0 0 0 0 40 15 0 0 0 0 0 0 0 0\n", "Name:\tworker\nVmRSS:\t512 kB\n")
	second, err := s.sampleAt(start.Add(time.Second))
	if err != nil {
		t.Fatal(err)
	}
	if second.srsCPUPercent != 100 || second.srsRSSBytes != 2048*1024 || len(second.workerCPUPercent) != 1 || second.workerCPUPercent[0] != 25 {
		t.Fatalf("second sample = %+v", second)
	}
}

func TestProcSamplerBadStat(t *testing.T) {
	root := t.TempDir()
	writeProcFixture(t, root, 123, "123 (srs) S 1\n", "VmRSS:\t1024 kB\n")
	s := newProcSampler(filepath.Join(root, "proc"), 123, nil, 100)
	if _, err := s.sampleAt(time.Unix(100, 0)); err == nil || !strings.Contains(err.Error(), "stat") {
		t.Fatalf("error = %v, want stat error", err)
	}
}

func TestProcSamplerMissingRSS(t *testing.T) {
	root := t.TempDir()
	writeProcFixture(t, root, 123, "123 (srs) S 1 1 1 0 0 0 0 0 0 0 100 50 0 0 0 0 0 0 0 0\n", "Name:\tsrs\n")
	s := newProcSampler(filepath.Join(root, "proc"), 123, nil, 100)
	if _, err := s.sampleAt(time.Unix(100, 0)); err == nil || !strings.Contains(err.Error(), "VmRSS") {
		t.Fatalf("error = %v, want VmRSS error", err)
	}
}
