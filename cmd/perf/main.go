package main

import (
	"encoding/json"
	"errors"
	"flag"
	"fmt"
	"io"
	"os"
	"time"
)

type command struct {
	kind          string
	scenario      string
	repeat        int
	mode          string
	clients       int
	settle        int
	window        int
	cap           int
	cores         int
	srsPID        int
	output        string
	machine       string
	dockerVersion string
	noPublisher   bool
	media         string
	abRun         string
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
		mode := flags.String("mode", "ramp", "ramp, hold, profile (a hold for perf, not a measurement), or probe (the delay probes' only mode)")
		clients := flags.Int("clients", 0, "starting or fixed client count")
		settle := flags.Int("settle", -1, "seconds a step settles before its samples count; default 2 for ramp, 30 for a hold")
		window := flags.Int("window", 0, "counted seconds of a step; default 3 for ramp, 60 for hold, 75 for profile")
		cap := flags.Int("cap", 0, "ramp client cap")
		cores := flags.Int("cores", 0, "worker processes")
		srsPID := flags.Int("srs-pid", 0, "SRS process ID")
		output := flags.String("output", "", "output directory")
		machine := flags.String("machine", "", "host model for comparisons")
		dockerVersion := flags.String("docker-version", "", "Docker version for comparisons")
		noPublisher := flags.Bool("no-publisher", false, "players connect and wait with no media, to check connection limits")
		media := flags.String("media", defaultMedia, "flat FLV source with its -flatness.json record")
		abRun := flags.String("ab-run", "", "id shared by the two sides of an A/B run, so compare pairs their seconds")
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
		if *media == "" {
			return command{}, errors.New("media is required")
		}
		if *noPublisher && scenarios[*name].Unit != "clients" {
			return command{}, fmt.Errorf("scenario %s has its publishers in the workers, so -no-publisher does not apply", *name)
		}
		// The delay probes run only as a probe, and only they do.
		probes := scenarios[*name].Unit == "probes"
		if probes && !flagSet(flags, "mode") {
			*mode = "probe"
		}
		if probes != (*mode == "probe") {
			return command{}, fmt.Errorf("scenario %s: -mode probe is the delay probes' only mode, and only theirs", *name)
		}
		if (*mode != "ramp" && *mode != "hold" && *mode != "profile" && *mode != "probe") || *clients < 0 || *settle < -1 || *window < 0 || *cap < 0 || *cores < 0 || *srsPID < 0 {
			return command{}, errors.New("invalid run options")
		}
		defaultSettle, defaultWindow := scenarios[*name].stepTiming(*mode)
		if *settle == -1 {
			*settle = defaultSettle
		}
		if *window == 0 {
			if flagSet(flags, "window") {
				return command{}, errors.New("invalid run options: window must be positive")
			}
			*window = defaultWindow
		}
		parsed := command{kind: "run", scenario: *name, repeat: *repeat, clients: *clients, settle: *settle, window: *window, cap: *cap, cores: *cores, srsPID: *srsPID, output: *output, machine: *machine, dockerVersion: *dockerVersion, noPublisher: *noPublisher, media: *media, abRun: *abRun}
		if *mode != "ramp" {
			parsed.mode = *mode
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
		media := flags.String("media", defaultMedia, "flat FLV source with its -flatness.json record")
		if err := flags.Parse(args[1:]); err != nil {
			return command{}, err
		}
		if flags.NArg() != 0 || *name == "" {
			return command{}, errors.New("smoke requires -scenario <name>")
		}
		if _, ok := scenarioByName(*name); !ok {
			return command{}, fmt.Errorf("unknown scenario %q", *name)
		}
		if *media == "" {
			return command{}, errors.New("media is required")
		}
		return command{kind: "smoke", scenario: *name, media: *media}, nil
	default:
		return command{}, fmt.Errorf("unknown command %q", args[0])
	}
}

func flagSet(flags *flag.FlagSet, name string) bool {
	found := false
	flags.Visit(func(f *flag.Flag) {
		if f.Name == name {
			found = true
		}
	})
	return found
}

type setup struct {
	Machine            string          `json:"machine"`
	Cores              int             `json:"cores"`
	MemoryBytes        int64           `json:"memory_bytes"`
	DockerVersion      string          `json:"docker_version"`
	Kernel             string          `json:"kernel"`
	CoreCalibrationGap float64         `json:"core_calibration_gap"`
	Source             string          `json:"source,omitempty"`
	SourceFlatness     json.RawMessage `json:"source_flatness,omitempty"`
}

type scenario struct {
	Name       string            `json:"name"`
	Publisher  string            `json:"publisher"`
	Player     string            `json:"player"`
	Unit       string            `json:"unit"`
	Parameters map[string]string `json:"parameters"`
	// start and cap are the ramp's first and last client counts, when they
	// differ from 100 and the TCP cap.
	start, cap int
	// segment is the seconds of a segment, the shipped default, in a
	// segmented scenario such as HLS; 0 for a stream.
	segment int
}

var scenarios = map[string]scenario{
	"rtmp-hls":  {Name: "rtmp-hls", Publisher: "rtmp", Player: "hls", Unit: "clients", segment: 10},
	"rtmp-dash": {Name: "rtmp-dash", Publisher: "rtmp", Player: "dash", Unit: "clients", segment: 10},
	"rtmp-rtmp": {Name: "rtmp-rtmp", Publisher: "rtmp", Player: "rtmp", Unit: "clients"},
	"rtmp-flv":  {Name: "rtmp-flv", Publisher: "rtmp", Player: "flv", Unit: "clients"},
	"rtmp-ts":   {Name: "rtmp-ts", Publisher: "rtmp", Player: "ts", Unit: "clients"},
	"rtmp-rtsp": {Name: "rtmp-rtsp", Publisher: "rtmp", Player: "rtsp", Unit: "clients"},
	"srt-srt":   {Name: "srt-srt", Publisher: "srt", Player: "srt", Unit: "clients", start: 10, cap: 2000},
	"rtmp-whep": {Name: "rtmp-whep", Publisher: "rtmp", Player: "whep", Unit: "clients", start: 10, cap: 2000},
	"whip-whep": {Name: "whip-whep", Publisher: "whip", Player: "whep", Unit: "clients", start: 10, cap: 2000},
	// The cross-protocol scenarios publish in one protocol and play in
	// another, through SRS's bridges: SRT to RTMP, SRT to RTC (AAC to Opus)
	// and RTC to RTMP (Opus to AAC). The players are the load, so the ramp
	// takes their protocol's bounds.
	"srt-rtmp":  {Name: "srt-rtmp", Publisher: "srt", Player: "rtmp", Unit: "clients"},
	"srt-whep":  {Name: "srt-whep", Publisher: "srt", Player: "whep", Unit: "clients", start: 10, cap: 2000},
	"whip-rtmp": {Name: "whip-rtmp", Publisher: "whip", Player: "rtmp", Unit: "clients"},
	"whip-flv":  {Name: "whip-flv", Publisher: "whip", Player: "flv", Unit: "clients"},
	// The publisher scenarios have N publishers, each of its own stream, and
	// no player; many-streams has N streams, each with one RTMP publisher and
	// two RTMP players, so its cap is the TCP cap's connections over three.
	"rtmp-pub":     {Name: "rtmp-pub", Publisher: "rtmp", Unit: "publishers", start: 10},
	"whip-pub":     {Name: "whip-pub", Publisher: "whip", Unit: "publishers", start: 10, cap: 2000},
	"many-streams": {Name: "many-streams", Publisher: "rtmp", Player: "rtmp", Unit: "streams", start: 10, cap: defaultClientCap / 3},
	// The delay probes (C7) measure delays, not capacity: an RTMP publisher
	// and four RTMP players at the origin, the forward target and the edge.
	"delays": {Name: "delays", Publisher: "rtmp", Player: "rtmp", Unit: "probes"},
}

func (s scenario) rampStart() int {
	if s.start > 0 {
		return s.start
	}
	return 100
}

func (s scenario) rampCap() int {
	if s.cap > 0 {
		return s.cap
	}
	return defaultClientCap
}

// gap is how long a player may get nothing before it fails: 5s for a stream,
// three segment durations for a segmented scenario (D30).
func (s scenario) gap() time.Duration {
	if s.segment > 0 {
		return time.Duration(3*s.segment) * time.Second
	}
	return 5 * time.Second
}

// lagLimit is the lag in milliseconds that fails a step, or 0 when lag is
// reported but not judged, as for a segmented scenario, whose latency is
// seconds by design.
func (s scenario) lagLimit() int64 {
	if s.segment > 0 {
		return 0
	}
	return 2000
}

// stepTiming is the mode's default settle and window, except that a
// segmented scenario's ramp step settles two segment durations, for the first
// new segment, and counts three, so the gap rule can judge it.
func (s scenario) stepTiming(mode string) (settle, window int) {
	if s.segment > 0 && mode == "ramp" {
		return 2 * s.segment, 3 * s.segment
	}
	return stepTiming(mode)
}

// smokeSeconds is how long the smoke plays: 10s, or three segment durations.
func (s scenario) smokeSeconds() int {
	if s.segment > 0 {
		return 3 * s.segment
	}
	return 10
}

func scenarioByName(name string) (scenario, bool) {
	s, ok := scenarios[name]
	return s, ok
}

// sample is one second, with its wall-clock time so the two sides of an A/B
// run pair by second. SRS CPU is the change of its container's cgroup
// usage_usec over the second, kept raw too so the CPU can be recomputed.
type sample struct {
	Second           int       `json:"second"`
	UnixMillis       int64     `json:"unix_millis,omitempty"`
	SRSCPUPercent    float64   `json:"srs_cpu_percent"`
	SRSCPUUsageUsec  uint64    `json:"srs_cpu_usage_usec"`
	SRSRSSBytes      int64     `json:"srs_rss_bytes"`
	WorkerCPUPercent []float64 `json:"worker_cpu_percent"`
	Frames           int64     `json:"frames"`
	// Segments counts the segments players got, in a segmented scenario.
	Segments  int64 `json:"segments,omitempty"`
	Bytes     int64 `json:"bytes"`
	LagMillis int64 `json:"lag_millis"`
	Failures  int64 `json:"failures"`
}

type health struct {
	Healthy bool   `json:"healthy"`
	Reason  string `json:"reason,omitempty"`
}

type validity struct {
	Valid  bool   `json:"valid"`
	Reason string `json:"reason,omitempty"`
}

// stat is the mean and sample standard deviation of a metric over a window.
type stat struct {
	Mean float64 `json:"mean"`
	SD   float64 `json:"sd"`
}

// window summarizes a step's counted samples, one per second. Worker CPU is
// the workers' average in each second.
type window struct {
	Samples          int  `json:"samples"`
	SRSCPUPercent    stat `json:"srs_cpu_percent"`
	SRSRSSBytes      stat `json:"srs_rss_bytes"`
	WorkerCPUPercent stat `json:"worker_cpu_percent"`
	Frames           stat `json:"frames"`
	Bytes            stat `json:"bytes"`
	LagMillis        stat `json:"lag_millis"`
	Failures         stat `json:"failures"`
}

// step keeps every sample; only those after SettleSeconds count.
type step struct {
	Mode          string   `json:"mode"`
	Clients       int      `json:"clients"`
	SettleSeconds int      `json:"settle_seconds"`
	Samples       []sample `json:"samples"`
	Window        *window  `json:"window,omitempty"`
	Health        health   `json:"health"`
	Validity      validity `json:"validity"`
}

// result is one ramp or hold. ABRun is the id shared by the two sides of an
// A/B run; compare pairs their seconds only when both carry the same one.
type result struct {
	Setup        setup    `json:"setup"`
	ABRun        string   `json:"ab_run,omitempty"`
	Scenario     scenario `json:"scenario"`
	Steps        []step   `json:"steps"`
	Window       *window  `json:"window,omitempty"`
	Health       health   `json:"health"`
	Validity     validity `json:"validity"`
	Capacity     int      `json:"capacity"`
	CPUPerClient float64  `json:"cpu_per_client"`
	// Delays is what a delay probe measured; nil for any other result.
	Delays *delays `json:"delays,omitempty"`
}

func main() {
	cmd, err := parseCommand(os.Args[1:])
	if err == nil && cmd.kind == "worker" {
		err = serveWorker(os.Stdin, os.Stdout)
	} else if err == nil && cmd.kind == "smoke" {
		err = runSmoke(os.Stdout, cmd)
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
