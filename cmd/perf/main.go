package main

import (
	"errors"
	"flag"
	"fmt"
	"io"
	"os"
)

type command struct {
	kind          string
	scenario      string
	repeat        int
	mode          string
	clients       int
	seconds       int
	cap           int
	cores         int
	srsPID        int
	output        string
	machine       string
	dockerVersion string
	noPublisher   bool
	workerID      int
	paths         []string
}

func parseCommand(args []string) (command, error) {
	if len(args) == 0 {
		return command{}, errors.New("command is required: run, worker, or compare")
	}
	flags := flag.NewFlagSet(args[0], flag.ContinueOnError)
	flags.SetOutput(io.Discard)
	switch args[0] {
	case "run":
		name := flags.String("scenario", "", "scenario name")
		repeat := flags.Int("repeat", 1, "number of repeats")
		mode := flags.String("mode", "ramp", "ramp or hold")
		clients := flags.Int("clients", 0, "starting or fixed client count")
		seconds := flags.Int("seconds", 0, "seconds per step")
		cap := flags.Int("cap", 0, "ramp client cap")
		cores := flags.Int("cores", 0, "worker processes")
		srsPID := flags.Int("srs-pid", 0, "SRS process ID")
		output := flags.String("output", "", "output directory")
		machine := flags.String("machine", "", "host model for comparisons")
		dockerVersion := flags.String("docker-version", "", "Docker version for comparisons")
		noPublisher := flags.Bool("no-publisher", false, "players connect and wait with no media, to check connection limits")
		if err := flags.Parse(args[1:]); err != nil {
			return command{}, err
		}
		if flags.NArg() != 0 {
			return command{}, errors.New("run accepts no positional arguments")
		}
		if *name == "" {
			return command{}, errors.New("scenario is required")
		}
		if _, ok := scenarioByName(*name); !ok {
			return command{}, fmt.Errorf("unknown scenario %q", *name)
		}
		if *repeat <= 0 {
			return command{}, errors.New("repeat must be positive")
		}
		if (*mode != "ramp" && *mode != "hold") || *clients < 0 || *seconds < 0 || *cap < 0 || *cores < 0 || *srsPID < 0 {
			return command{}, errors.New("invalid run options")
		}
		parsed := command{kind: "run", scenario: *name, repeat: *repeat, clients: *clients, seconds: *seconds, cap: *cap, cores: *cores, srsPID: *srsPID, output: *output, machine: *machine, dockerVersion: *dockerVersion, noPublisher: *noPublisher}
		if *mode == "hold" {
			parsed.mode = "hold"
		}
		return parsed, nil
	case "worker":
		id := flags.Int("id", -1, "worker identifier")
		if err := flags.Parse(args[1:]); err != nil {
			return command{}, err
		}
		if flags.NArg() != 0 {
			return command{}, errors.New("worker accepts no positional arguments")
		}
		if *id < 0 {
			return command{}, errors.New("worker id is required and must be nonnegative")
		}
		return command{kind: "worker", workerID: *id}, nil
	case "compare":
		if err := flags.Parse(args[1:]); err != nil {
			return command{}, err
		}
		if flags.NArg() != 2 {
			return command{}, errors.New("compare requires two result files")
		}
		return command{kind: "compare", paths: flags.Args()}, nil
	case "smoke":
		name := flags.String("scenario", "", "scenario name")
		if err := flags.Parse(args[1:]); err != nil {
			return command{}, err
		}
		if flags.NArg() != 0 || *name != "rtmp-rtmp" {
			return command{}, errors.New("smoke requires -scenario rtmp-rtmp")
		}
		return command{kind: "smoke", scenario: *name}, nil
	default:
		return command{}, fmt.Errorf("unknown command %q", args[0])
	}
}

type setup struct {
	Machine            string  `json:"machine"`
	Cores              int     `json:"cores"`
	MemoryBytes        int64   `json:"memory_bytes"`
	DockerVersion      string  `json:"docker_version"`
	Kernel             string  `json:"kernel"`
	CoreCalibrationGap float64 `json:"core_calibration_gap"`
}

type scenario struct {
	Name       string            `json:"name"`
	Publisher  string            `json:"publisher"`
	Player     string            `json:"player"`
	Unit       string            `json:"unit"`
	Parameters map[string]string `json:"parameters"`
}

var scenarios = map[string]scenario{
	"rtmp-rtmp": {Name: "rtmp-rtmp", Publisher: "rtmp", Player: "rtmp", Unit: "clients"},
}

func scenarioByName(name string) (scenario, bool) {
	s, ok := scenarios[name]
	return s, ok
}

type sample struct {
	Second           int       `json:"second"`
	SRSCPUPercent    float64   `json:"srs_cpu_percent"`
	SRSRSSBytes      int64     `json:"srs_rss_bytes"`
	WorkerCPUPercent []float64 `json:"worker_cpu_percent"`
	Frames           int64     `json:"frames"`
	Bytes            int64     `json:"bytes"`
	LagMillis        int64     `json:"lag_millis"`
	Failures         int64     `json:"failures"`
}

type health struct {
	Healthy bool   `json:"healthy"`
	Reason  string `json:"reason,omitempty"`
}

type validity struct {
	Valid  bool   `json:"valid"`
	Reason string `json:"reason,omitempty"`
}

type step struct {
	Mode     string   `json:"mode"`
	Clients  int      `json:"clients"`
	Samples  []sample `json:"samples"`
	Health   health   `json:"health"`
	Validity validity `json:"validity"`
}

type result struct {
	Setup        setup    `json:"setup"`
	Scenario     scenario `json:"scenario"`
	Steps        []step   `json:"steps"`
	Health       health   `json:"health"`
	Validity     validity `json:"validity"`
	Capacity     int      `json:"capacity"`
	CPUPerClient float64  `json:"cpu_per_client"`
}

func main() {
	cmd, err := parseCommand(os.Args[1:])
	if err == nil && cmd.kind == "worker" {
		err = serveWorker(os.Stdin, os.Stdout)
	} else if err == nil && cmd.kind == "smoke" {
		err = runRTMPSmoke(os.Stdout)
	} else if err == nil && cmd.kind == "run" {
		err = runPerformance(os.Stdout, cmd)
	} else if err == nil && cmd.kind == "compare" {
		err = compareRunFiles(os.Stdout, cmd.paths[0], cmd.paths[1])
	} else if err == nil {
		err = fmt.Errorf("%s is not implemented yet", cmd.kind)
	}
	if err == nil {
		return
	}
	fmt.Fprintln(os.Stderr, err)
	os.Exit(2)
}
