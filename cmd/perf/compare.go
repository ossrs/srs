package main

import (
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"os"
	"reflect"
)

// readComparisonResults reads one result.json, or the results of a combined run.json.
func readComparisonResults(path string) ([]result, bool, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		return nil, false, fmt.Errorf("read %s: %w", path, err)
	}
	var fields map[string]json.RawMessage
	if err := json.Unmarshal(data, &fields); err != nil {
		return nil, false, fmt.Errorf("decode %s: %w", path, err)
	}
	if raw, ok := fields["results"]; ok {
		var values []result
		if err := json.Unmarshal(raw, &values); err != nil {
			return nil, false, fmt.Errorf("decode %s: %w", path, err)
		}
		if len(values) == 0 {
			return nil, false, fmt.Errorf("decode %s: %w", path, errors.New("no results"))
		}
		return values, true, nil
	}
	var value result
	if err := json.Unmarshal(data, &value); err != nil {
		return nil, false, fmt.Errorf("decode %s: %w", path, err)
	}
	return []result{value}, false, nil
}

func sameComparisonSetup(old, newer result) bool {
	return old.Setup.Machine == newer.Setup.Machine &&
		old.Setup.Cores == newer.Setup.Cores &&
		old.Setup.MemoryBytes == newer.Setup.MemoryBytes &&
		old.Setup.DockerVersion == newer.Setup.DockerVersion &&
		old.Setup.Kernel == newer.Setup.Kernel &&
		old.Scenario.Name == newer.Scenario.Name &&
		old.Scenario.Publisher == newer.Scenario.Publisher &&
		old.Scenario.Player == newer.Scenario.Player &&
		old.Scenario.Unit == newer.Scenario.Unit &&
		reflect.DeepEqual(old.Scenario.Parameters, newer.Scenario.Parameters)
}

func changeFlag(old, newer float64, metric string, lowerIsBetter bool) string {
	if old <= 0 {
		return ""
	}
	change := (newer - old) / old
	if change > -0.1+1e-9 && change < 0.1-1e-9 {
		return ""
	}
	better := change > 0
	if lowerIsBetter {
		better = change < 0
	}
	if better {
		return metric + " gain >= 10%"
	}
	return metric + " regression >= 10%"
}

func compareResults(output io.Writer, old, newer result) error {
	if !sameComparisonSetup(old, newer) {
		_, err := fmt.Fprintln(output, "different setup, no comparison; new report is a new baseline")
		return err
	}
	if _, err := fmt.Fprintf(output, "capacity: %d -> %d\ncpu per client: %g -> %g\n", old.Capacity, newer.Capacity, old.CPUPerClient, newer.CPUPerClient); err != nil {
		return err
	}
	flags := []string{
		changeFlag(float64(old.Capacity), float64(newer.Capacity), "capacity", false),
		changeFlag(old.CPUPerClient, newer.CPUPerClient, "cpu per client", true),
	}
	count := 0
	for _, flag := range flags {
		if flag == "" {
			continue
		}
		count++
		if _, err := fmt.Fprintln(output, flag); err != nil {
			return err
		}
	}
	if count == 0 {
		_, err := fmt.Fprintln(output, "no changes >= 10%")
		return err
	}
	return nil
}

func comparisonKey(value result) string {
	return value.Scenario.Parameters["mode"] + " " + value.Scenario.Parameters["clients"] + " clients"
}

// compareRunFiles compares two results, or pairs the results of combined runs by mode and client
// count, in the old run's order; a repeated pair, such as the two 50% holds, pairs in order.
func compareRunFiles(output io.Writer, oldPath, newPath string) error {
	olds, oldCombined, err := readComparisonResults(oldPath)
	if err != nil {
		return err
	}
	newers, newCombined, err := readComparisonResults(newPath)
	if err != nil {
		return err
	}
	if !oldCombined && !newCombined {
		return compareResults(output, olds[0], newers[0])
	}

	used := make([]bool, len(newers))
	for _, old := range olds {
		key := comparisonKey(old)
		match := -1
		for i, newer := range newers {
			if !used[i] && comparisonKey(newer) == key {
				match = i
				break
			}
		}
		if match < 0 {
			if _, err := fmt.Fprintf(output, "%s: only in old\n", key); err != nil {
				return err
			}
			continue
		}
		used[match] = true
		if _, err := fmt.Fprintf(output, "%s:\n", key); err != nil {
			return err
		}
		if err := compareResults(output, old, newers[match]); err != nil {
			return err
		}
	}
	for i, newer := range newers {
		if used[i] {
			continue
		}
		if _, err := fmt.Fprintf(output, "%s: only in new\n", comparisonKey(newer)); err != nil {
			return err
		}
	}
	return nil
}
