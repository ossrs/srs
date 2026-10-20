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
		old.Setup.Source == newer.Setup.Source &&
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

// compareResults flags capacity, and the CPU per client of a hold. A ramp
// step's CPU per client includes connecting its new clients, so it is a note,
// never flagged; a profile hold's CPU includes perf, so it is not compared.
func compareResults(output io.Writer, old, newer result) error {
	if old.Scenario.Parameters["mode"] == "profile" || newer.Scenario.Parameters["mode"] == "profile" {
		_, err := fmt.Fprintln(output, "profile hold, not a measurement; no comparison")
		return err
	}
	if !sameComparisonSetup(old, newer) {
		_, err := fmt.Fprintln(output, "different setup, no comparison; new report is a new baseline")
		return err
	}
	if old.Scenario.Parameters["mode"] == "probe" {
		return compareDelays(output, old, newer)
	}
	ramp := old.Scenario.Parameters["mode"] == "ramp"
	cpuLabel := "cpu per client"
	if ramp {
		cpuLabel = "ramp cpu per client, a note (includes connecting, not flagged)"
	}
	if _, err := fmt.Fprintf(output, "capacity: %d -> %d\n", old.Capacity, newer.Capacity); err != nil {
		return err
	}
	if !ramp && old.Window != nil && newer.Window != nil {
		o, n := old.Window, newer.Window
		if _, err := fmt.Fprintf(output, "srs cpu %%: old mean %.4g sd %.4g (n %d), new mean %.4g sd %.4g (n %d)\n", o.SRSCPUPercent.Mean, o.SRSCPUPercent.SD, o.Samples, n.SRSCPUPercent.Mean, n.SRSCPUPercent.SD, n.Samples); err != nil {
			return err
		}
	}
	if _, err := fmt.Fprintf(output, "%s: %g -> %g\n", cpuLabel, old.CPUPerClient, newer.CPUPerClient); err != nil {
		return err
	}
	flags := []string{changeFlag(float64(old.Capacity), float64(newer.Capacity), "capacity", false)}
	if !ramp {
		d, ok, err := holdDifference(output, old, newer)
		if err != nil {
			return err
		}
		if ok {
			flags = append(flags, changeFlag(d.OldMean, d.OldMean+d.Mean, "cpu per client", true))
		} else {
			flags = append(flags, changeFlag(old.CPUPerClient, newer.CPUPerClient, "cpu per client", true))
		}
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

// countedSamples are a hold's samples after its settle, the ones its window
// summarizes.
func countedSamples(value result) []sample {
	if len(value.Steps) == 0 {
		return nil
	}
	s := value.Steps[0]
	return steadySamples(s.Samples, s.SettleSeconds)
}

func srsCPUSeries(samples []sample) []float64 {
	values := make([]float64, len(samples))
	for i, s := range samples {
		values[i] = s.SRSCPUPercent
	}
	return values
}

// holdDifference prints the difference of two holds' SRS CPU with its 95% CI:
// paired per second when both are sides of one A/B run, otherwise unpaired, a
// sequential comparison, which is noisier. The clients are equal, so the
// relative change is that of CPU per client. ok is false when a side has fewer
// than 2 counted samples, such as a result from before per-second windows.
func holdDifference(output io.Writer, old, newer result) (difference, bool, error) {
	olds, newers := countedSamples(old), countedSamples(newer)
	if old.Window == nil || newer.Window == nil || len(olds) < 2 || len(newers) < 2 {
		return difference{}, false, nil
	}
	var d difference
	var err error
	if old.ABRun != "" && old.ABRun == newer.ABRun {
		a, b := pairBySecond(olds, newers)
		if len(a) >= 2 {
			d, err = pairedDifference(a, b)
		} else if _, err = fmt.Fprintln(output, "A/B sides share fewer than 2 seconds, so they compare unpaired"); err != nil {
			return difference{}, false, err
		}
	}
	if d.Method == "" && err == nil {
		d, err = welchDifference(srsCPUSeries(olds), srsCPUSeries(newers))
	}
	if err != nil {
		return difference{}, false, err
	}
	method := fmt.Sprintf("paired per second (A/B, n %d, df %.4g)", d.N, d.DF)
	if d.Method == "unpaired" {
		method = fmt.Sprintf("unpaired (sequential, df %.4g), noisier than a paired A/B", d.DF)
	}
	zero := "CI excludes 0"
	if d.containsZero() {
		zero = "CI contains 0"
	}
	relative := 0.0
	if d.OldMean > 0 {
		relative = d.Mean / d.OldMean * 100
	}
	_, err = fmt.Fprintf(output, "difference: %.4g points (%+.1f%%), 95%% CI %.4g to %.4g, %s; %s\n", d.Mean, relative, d.Low, d.High, method, zero)
	return d, err == nil, err
}
