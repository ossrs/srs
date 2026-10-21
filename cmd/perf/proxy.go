package main

import (
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"os"
	"path/filepath"
	"sort"
	"strconv"
	"strings"
	"time"
)

// proxyAPI is the proxy's HTTP API in the load container's network
// namespace, which it shares with the proxy (perf-up.sh -target proxy).
const proxyAPI = "http://127.0.0.1:1985"

// findOrigins gives the pids of the SRS origins beside the subject in its pid
// namespace, in start order, so origin1, which perf-up.sh starts first, comes
// first.
func findOrigins(root string, subject int) ([]int, error) {
	entries, err := os.ReadDir(root)
	if err != nil {
		return nil, fmt.Errorf("read %s: %w", root, err)
	}
	var pids []int
	for _, entry := range entries {
		pid, err := strconv.Atoi(entry.Name())
		if err != nil || pid == subject {
			continue
		}
		comm, err := os.ReadFile(filepath.Join(root, entry.Name(), "comm"))
		if err != nil || strings.TrimSpace(string(comm)) != "srs" {
			continue
		}
		pids = append(pids, pid)
	}
	if len(pids) == 0 {
		return nil, fmt.Errorf("no SRS origin beside pid %d in %s; start the proxy with perf-up.sh -target proxy", subject, root)
	}
	sort.Ints(pids)
	return pids, nil
}

// fetchProxyVersion reads the proxy's version from its HTTP API. The SRSX
// signature tells the proxy from an SRS answering on the same port.
func fetchProxyVersion(api string) (string, error) {
	client := http.Client{Timeout: 5 * time.Second}
	resp, err := client.Get(api + "/api/v1/versions")
	if err != nil {
		return "", fmt.Errorf("proxy version: %w", err)
	}
	defer resp.Body.Close()
	body, err := io.ReadAll(resp.Body)
	if err != nil {
		return "", fmt.Errorf("proxy version: %w", err)
	}
	var v struct {
		Signature string `json:"signature"`
		Version   string `json:"version"`
	}
	if err := json.Unmarshal(body, &v); err != nil || v.Signature != "SRSX" || v.Version == "" {
		return "", fmt.Errorf("proxy version: %s answers %q, not the proxy", api, strings.TrimSpace(string(body)))
	}
	return v.Version, nil
}

// targetSetup records the measured server in a run's or smoke's setup. On the
// proxy it also records the proxy's version from api and its origins found
// under root beside the subject, and gives their pids for their CPU.
func targetSetup(s setup, target, api, root string, subject int) (setup, []int, error) {
	if target != "proxy" {
		s.Target = targetOf(s)
		return s, nil, nil
	}
	version, err := fetchProxyVersion(api)
	if err != nil {
		return s, nil, err
	}
	origins, err := findOrigins(root, subject)
	if err != nil {
		return s, nil, err
	}
	s.Target, s.ProxyVersion, s.Origins = "proxy", version, len(origins)
	return s, origins, nil
}

// targetOf is the server a result measured; a result from before the target
// field measured SRS.
func targetOf(s setup) string {
	if s.Target == "" {
		return "srs"
	}
	return s.Target
}
