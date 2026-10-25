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

func writeCgroupFixture(t *testing.T, root string, pid int, path string, usageUsec int) {
	t.Helper()
	procDir := filepath.Join(root, "proc", strconv.Itoa(pid))
	if err := os.MkdirAll(procDir, 0755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(procDir, "cgroup"), []byte("0::"+path+"\n"), 0644); err != nil {
		t.Fatal(err)
	}
	dir := filepath.Join(root, "cgroup", path)
	if err := os.MkdirAll(dir, 0755); err != nil {
		t.Fatal(err)
	}
	stat := "usage_usec " + strconv.Itoa(usageUsec) + "\nuser_usec 1\nsystem_usec 1\n"
	if err := os.WriteFile(filepath.Join(dir, "cpu.stat"), []byte(stat), 0644); err != nil {
		t.Fatal(err)
	}
}

// SRS CPU comes from its container's cgroup usage_usec, not from the
// process's own ticks; RSS and the workers' CPU still come from /proc.
func TestProcSampler(t *testing.T) {
	root := t.TempDir()
	writeProcFixture(t, root, 123, "123 (srs (media)) S 1 1 1 0 0 0 0 0 0 0 100 50 0 0 0 0 0 0 0 0\n", "Name:\tsrs\nVmRSS:\t1024 kB\n")
	writeProcFixture(t, root, 456, "456 (worker) S 1 1 1 0 0 0 0 0 0 0 20 10 0 0 0 0 0 0 0 0\n", "Name:\tworker\nVmRSS:\t512 kB\n")
	writeCgroupFixture(t, root, 123, "/docker/abc", 5000000)
	s := newProcSampler(filepath.Join(root, "proc"), filepath.Join(root, "cgroup"), 123, []int{456}, 100)
	start := time.Unix(100, 0)
	first, err := s.sampleAt(start)
	if err != nil {
		t.Fatal(err)
	}
	if first.srsCPUPercent != 0 || first.srsCPUUsageUsec != 5000000 || first.srsRSSBytes != 1024*1024 || len(first.workerCPUPercent) != 1 || first.workerCPUPercent[0] != 0 {
		t.Fatalf("first sample = %+v", first)
	}
	// The process ticks jump by 150 (1.5s of CPU), but the cgroup moved 0.6s:
	// SRS CPU must follow the cgroup.
	writeProcFixture(t, root, 123, "123 (srs (media)) S 1 1 1 0 0 0 0 0 0 0 200 100 0 0 0 0 0 0 0 0\n", "Name:\tsrs\nVmRSS:\t2048 kB\n")
	writeProcFixture(t, root, 456, "456 (worker) S 1 1 1 0 0 0 0 0 0 0 40 15 0 0 0 0 0 0 0 0\n", "Name:\tworker\nVmRSS:\t512 kB\n")
	writeCgroupFixture(t, root, 123, "/docker/abc", 5600000)
	second, err := s.sampleAt(start.Add(time.Second))
	if err != nil {
		t.Fatal(err)
	}
	if second.srsCPUPercent != 60 || second.srsCPUUsageUsec != 5600000 || second.srsRSSBytes != 2048*1024 || len(second.workerCPUPercent) != 1 || second.workerCPUPercent[0] != 25 {
		t.Fatalf("second sample = %+v", second)
	}
	writeCgroupFixture(t, root, 123, "/docker/abc", 6500000)
	third, err := s.sampleAt(start.Add(2500 * time.Millisecond))
	if err != nil {
		t.Fatal(err)
	}
	if third.srsCPUPercent != 60 {
		t.Fatalf("third sample over 1.5s = %+v, want 60%%", third)
	}
}

func TestCgroupPath(t *testing.T) {
	cases := []struct {
		name, content, want, err string
	}{
		{"v2", "0::/docker/abc\n", "/docker/abc", ""},
		{"v2 after v1 lines", "12:pids:/docker/abc\n0::/docker/abc\n", "/docker/abc", ""},
		{"private namespace", "0::/../abc\n", "", "cgroupns host"},
		{"v1 only", "12:pids:/docker/abc\n", "", "cgroup v2"},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			root := t.TempDir()
			dir := filepath.Join(root, "7")
			if err := os.MkdirAll(dir, 0755); err != nil {
				t.Fatal(err)
			}
			if err := os.WriteFile(filepath.Join(dir, "cgroup"), []byte(tc.content), 0644); err != nil {
				t.Fatal(err)
			}
			got, err := readCgroupPath(root, 7)
			if tc.err != "" {
				if err == nil || !strings.Contains(err.Error(), tc.err) {
					t.Fatalf("err = %v, want %q", err, tc.err)
				}
				return
			}
			if err != nil || got != tc.want {
				t.Fatalf("path = %q, err = %v", got, err)
			}
		})
	}
}

func TestCgroupUsageErrors(t *testing.T) {
	root := t.TempDir()
	dir := filepath.Join(root, "docker", "abc")
	if err := os.MkdirAll(dir, 0755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(dir, "cpu.stat"), []byte("user_usec 1\n"), 0644); err != nil {
		t.Fatal(err)
	}
	if _, err := readCgroupUsage(root, "/docker/abc"); err == nil || !strings.Contains(err.Error(), "usage_usec") {
		t.Fatalf("err = %v, want usage_usec error", err)
	}
	if _, err := readCgroupUsage(root, "/docker/missing"); err == nil || !strings.Contains(err.Error(), "cpu.stat") {
		t.Fatalf("err = %v, want cpu.stat error", err)
	}
}

func TestCgroupUsageBackwards(t *testing.T) {
	root := t.TempDir()
	writeProcFixture(t, root, 123, "123 (srs) S 1 1 1 0 0 0 0 0 0 0 100 50 0 0 0 0 0 0 0 0\n", "VmRSS:\t1024 kB\n")
	writeCgroupFixture(t, root, 123, "/docker/abc", 5000000)
	s := newProcSampler(filepath.Join(root, "proc"), filepath.Join(root, "cgroup"), 123, nil, 100)
	if _, err := s.sampleAt(time.Unix(100, 0)); err != nil {
		t.Fatal(err)
	}
	writeCgroupFixture(t, root, 123, "/docker/abc", 4000000)
	if _, err := s.sampleAt(time.Unix(101, 0)); err == nil || !strings.Contains(err.Error(), "backwards") {
		t.Fatalf("err = %v, want backwards error", err)
	}
}

func TestProcSamplerBadStat(t *testing.T) {
	root := t.TempDir()
	writeProcFixture(t, root, 123, "123 (srs) S 1 1 1 0 0 0 0 0 0 0 100 50 0 0 0 0 0 0 0 0\n", "VmRSS:\t1024 kB\n")
	writeProcFixture(t, root, 456, "456 (worker) S 1\n", "VmRSS:\t512 kB\n")
	writeCgroupFixture(t, root, 123, "/docker/abc", 5000000)
	s := newProcSampler(filepath.Join(root, "proc"), filepath.Join(root, "cgroup"), 123, []int{456}, 100)
	if _, err := s.sampleAt(time.Unix(100, 0)); err == nil || !strings.Contains(err.Error(), "stat") {
		t.Fatalf("error = %v, want stat error", err)
	}
}

func TestProcSamplerMissingRSS(t *testing.T) {
	root := t.TempDir()
	writeProcFixture(t, root, 123, "123 (srs) S 1 1 1 0 0 0 0 0 0 0 100 50 0 0 0 0 0 0 0 0\n", "Name:\tsrs\n")
	writeCgroupFixture(t, root, 123, "/docker/abc", 5000000)
	s := newProcSampler(filepath.Join(root, "proc"), filepath.Join(root, "cgroup"), 123, nil, 100)
	if _, err := s.sampleAt(time.Unix(100, 0)); err == nil || !strings.Contains(err.Error(), "VmRSS") {
		t.Fatalf("error = %v, want VmRSS error", err)
	}
}
