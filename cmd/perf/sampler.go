package main

import (
	"bufio"
	"context"
	"fmt"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"time"
)

type procUsage struct {
	srsCPUPercent    float64
	srsCPUUsageUsec  uint64
	srsRSSBytes      int64
	workerCPUPercent []float64
	// The origins' CPU, each from its own container's cgroup, when the
	// subject is the proxy; nil for SRS.
	originCPUPercent   []float64
	originCPUUsageUsec []uint64
}

// procSampler takes the subject's CPU (SRS, or the proxy) from its
// container's cgroup usage_usec, which counts every thread and child of the
// container to the microsecond, and RSS and each worker's CPU from /proc.
// Behind the proxy, each origin's CPU comes from its own cgroup too.
type procSampler struct {
	root           string
	cgroupRoot     string
	srsPID         int
	srsCgroup      string
	workerPIDs     []int
	ticksPerSecond float64
	previous       map[int]uint64
	previousUsage  uint64
	previousTime   time.Time
	originPIDs     []int
	originCgroups  []string
	previousOrigin []uint64
}

func newProcSampler(root, cgroupRoot string, srsPID int, workerPIDs []int, ticksPerSecond float64) *procSampler {
	return &procSampler{root: root, cgroupRoot: cgroupRoot, srsPID: srsPID, workerPIDs: append([]int(nil), workerPIDs...), ticksPerSecond: ticksPerSecond}
}

// withOrigins samples the CPU of each origin beside the subject too.
func (s *procSampler) withOrigins(pids []int) *procSampler {
	s.originPIDs = append([]int(nil), pids...)
	return s
}

// originUsage reads each origin's cgroup usage_usec, finding the cgroups at
// the first call.
func (s *procSampler) originUsage() ([]uint64, error) {
	if len(s.originPIDs) == 0 {
		return nil, nil
	}
	if s.originCgroups == nil {
		for _, pid := range s.originPIDs {
			cgroup, err := readCgroupPath(s.root, pid)
			if err != nil {
				return nil, err
			}
			s.originCgroups = append(s.originCgroups, cgroup)
		}
	}
	usage := make([]uint64, len(s.originCgroups))
	for i, cgroup := range s.originCgroups {
		v, err := readCgroupUsage(s.cgroupRoot, cgroup)
		if err != nil {
			return nil, err
		}
		usage[i] = v
	}
	return usage, nil
}

// readCgroupPath reads the cgroup v2 path of a process. The load container
// must share the host cgroup namespace (docker run --cgroupns host): from a
// private namespace the path of another container starts with "/..".
func readCgroupPath(root string, pid int) (string, error) {
	path := filepath.Join(root, strconv.Itoa(pid), "cgroup")
	data, err := os.ReadFile(path)
	if err != nil {
		return "", fmt.Errorf("read %s: %w", path, err)
	}
	for _, line := range strings.Split(string(data), "\n") {
		if !strings.HasPrefix(line, "0::") {
			continue
		}
		cgroup := strings.TrimPrefix(line, "0::")
		if cgroup == "" || !strings.HasPrefix(cgroup, "/") || strings.HasPrefix(cgroup, "/..") {
			return "", fmt.Errorf("cgroup %q of pid %d is outside this cgroup namespace; run the load container with --cgroupns host", cgroup, pid)
		}
		return cgroup, nil
	}
	return "", fmt.Errorf("no cgroup v2 entry in %s", path)
}

func readCgroupUsage(cgroupRoot, cgroup string) (uint64, error) {
	path := filepath.Join(cgroupRoot, cgroup, "cpu.stat")
	data, err := os.ReadFile(path)
	if err != nil {
		return 0, fmt.Errorf("read %s: %w", path, err)
	}
	for _, line := range strings.Split(string(data), "\n") {
		fields := strings.Fields(line)
		if len(fields) != 2 || fields[0] != "usage_usec" {
			continue
		}
		usage, err := strconv.ParseUint(fields[1], 10, 64)
		if err != nil {
			return 0, fmt.Errorf("invalid usage_usec in %s: %w", path, err)
		}
		return usage, nil
	}
	return 0, fmt.Errorf("usage_usec missing in %s", path)
}

func readProcTicks(root string, pid int) (uint64, error) {
	path := filepath.Join(root, strconv.Itoa(pid), "stat")
	data, err := os.ReadFile(path)
	if err != nil {
		return 0, fmt.Errorf("read %s: %w", path, err)
	}
	// The comm field is parenthesized and may itself contain spaces or parentheses.
	close := strings.LastIndexByte(string(data), ')')
	if close < 0 {
		return 0, fmt.Errorf("invalid %s: missing command terminator", path)
	}
	fields := strings.Fields(string(data[close+1:]))
	if len(fields) < 13 {
		return 0, fmt.Errorf("invalid %s: missing CPU fields", path)
	}
	user, err := strconv.ParseUint(fields[11], 10, 64)
	if err != nil {
		return 0, fmt.Errorf("invalid %s utime: %w", path, err)
	}
	system, err := strconv.ParseUint(fields[12], 10, 64)
	if err != nil {
		return 0, fmt.Errorf("invalid %s stime: %w", path, err)
	}
	return user + system, nil
}

func readProcRSS(root string, pid int) (int64, error) {
	path := filepath.Join(root, strconv.Itoa(pid), "status")
	file, err := os.Open(path)
	if err != nil {
		return 0, fmt.Errorf("read %s: %w", path, err)
	}
	defer file.Close()
	scanner := bufio.NewScanner(file)
	for scanner.Scan() {
		fields := strings.Fields(scanner.Text())
		if len(fields) == 0 || fields[0] != "VmRSS:" {
			continue
		}
		if len(fields) != 3 || fields[2] != "kB" {
			return 0, fmt.Errorf("invalid VmRSS in %s", path)
		}
		kb, err := strconv.ParseInt(fields[1], 10, 64)
		if err != nil || kb < 0 {
			return 0, fmt.Errorf("invalid VmRSS in %s", path)
		}
		return kb * 1024, nil
	}
	if err := scanner.Err(); err != nil {
		return 0, fmt.Errorf("read %s: %w", path, err)
	}
	return 0, fmt.Errorf("VmRSS missing in %s", path)
}

func (s *procSampler) sampleAt(now time.Time) (procUsage, error) {
	if s.ticksPerSecond <= 0 {
		return procUsage{}, fmt.Errorf("ticks per second must be positive")
	}
	if s.srsCgroup == "" {
		cgroup, err := readCgroupPath(s.root, s.srsPID)
		if err != nil {
			return procUsage{}, err
		}
		s.srsCgroup = cgroup
	}
	srsUsage, err := readCgroupUsage(s.cgroupRoot, s.srsCgroup)
	if err != nil {
		return procUsage{}, err
	}
	current := make(map[int]uint64, len(s.workerPIDs))
	for _, pid := range s.workerPIDs {
		ticks, err := readProcTicks(s.root, pid)
		if err != nil {
			return procUsage{}, err
		}
		current[pid] = ticks
	}
	rss, err := readProcRSS(s.root, s.srsPID)
	if err != nil {
		return procUsage{}, err
	}
	originUsage, err := s.originUsage()
	if err != nil {
		return procUsage{}, err
	}
	if !s.previousTime.IsZero() && !now.After(s.previousTime) {
		return procUsage{}, fmt.Errorf("sample time must advance")
	}
	usage := procUsage{srsCPUUsageUsec: srsUsage, srsRSSBytes: rss, workerCPUPercent: make([]float64, len(s.workerPIDs))}
	if originUsage != nil {
		usage.originCPUUsageUsec = originUsage
		usage.originCPUPercent = make([]float64, len(originUsage))
	}
	if !s.previousTime.IsZero() {
		elapsed := now.Sub(s.previousTime).Seconds()
		if srsUsage < s.previousUsage {
			return procUsage{}, fmt.Errorf("cgroup usage_usec went backwards for %s", s.srsCgroup)
		}
		usage.srsCPUPercent = float64(srsUsage-s.previousUsage) / (elapsed * 1e6) * 100
		for i, pid := range s.workerPIDs {
			if current[pid] < s.previous[pid] {
				return procUsage{}, fmt.Errorf("CPU ticks went backwards for pid %d", pid)
			}
			usage.workerCPUPercent[i] = float64(current[pid]-s.previous[pid]) / (elapsed * s.ticksPerSecond) * 100
		}
		for i, v := range originUsage {
			if v < s.previousOrigin[i] {
				return procUsage{}, fmt.Errorf("cgroup usage_usec went backwards for %s", s.originCgroups[i])
			}
			usage.originCPUPercent[i] = float64(v-s.previousOrigin[i]) / (elapsed * 1e6) * 100
		}
	}
	s.previous = current
	s.previousOrigin = originUsage
	s.previousUsage = srsUsage
	s.previousTime = now
	return usage, nil
}

func (s *procSampler) sampleEverySecond(ctx context.Context, output chan<- procUsage) error {
	if _, err := s.sampleAt(time.Now()); err != nil {
		return err
	}
	ticker := time.NewTicker(time.Second)
	defer ticker.Stop()
	for {
		select {
		case <-ctx.Done():
			return ctx.Err()
		case now := <-ticker.C:
			usage, err := s.sampleAt(now)
			if err != nil {
				return err
			}
			select {
			case output <- usage:
			case <-ctx.Done():
				return ctx.Err()
			}
		}
	}
}
