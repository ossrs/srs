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
	srsRSSBytes      int64
	workerCPUPercent []float64
}

type procSampler struct {
	root           string
	srsPID         int
	workerPIDs     []int
	ticksPerSecond float64
	previous       map[int]uint64
	previousTime   time.Time
}

func newProcSampler(root string, srsPID int, workerPIDs []int, ticksPerSecond float64) *procSampler {
	return &procSampler{root: root, srsPID: srsPID, workerPIDs: append([]int(nil), workerPIDs...), ticksPerSecond: ticksPerSecond}
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
	pids := append([]int{s.srsPID}, s.workerPIDs...)
	current := make(map[int]uint64, len(pids))
	for _, pid := range pids {
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
	if !s.previousTime.IsZero() && !now.After(s.previousTime) {
		return procUsage{}, fmt.Errorf("sample time must advance")
	}
	usage := procUsage{srsRSSBytes: rss, workerCPUPercent: make([]float64, len(s.workerPIDs))}
	if !s.previousTime.IsZero() {
		elapsed := now.Sub(s.previousTime).Seconds()
		for i, pid := range pids {
			if current[pid] < s.previous[pid] {
				return procUsage{}, fmt.Errorf("CPU ticks went backwards for pid %d", pid)
			}
			cpu := float64(current[pid]-s.previous[pid]) / (elapsed * s.ticksPerSecond) * 100
			if i == 0 {
				usage.srsCPUPercent = cpu
			} else {
				usage.workerCPUPercent[i-1] = cpu
			}
		}
	}
	s.previous = current
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
