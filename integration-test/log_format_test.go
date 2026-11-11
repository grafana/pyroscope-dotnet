package integrationtest

import (
	"encoding/json"
	"net/http"
	"regexp"
	"runtime"
	"strings"
	"testing"
	"time"

	"pyroscope-dotnet-integration-test/dockertest"
	"pyroscope-dotnet-integration-test/require"
)

// legacyProfilerLine matches the native logger's plain-text pattern,
// "[%Y-%m-%d %H:%M:%S.%e | %l | PId: %P | TId: %t] %v".
var legacyProfilerLine = regexp.MustCompile(`^\[\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3} \| \w+ \| PId: \d+ \| TId: \d+\]`)

const profilerLoggerName = "Pyroscope-DotNet-Profiler-Native"

// validSeverities is the set the JSON sink is allowed to emit. Anything else
// means the spdlog level mapping has drifted.
var validSeverities = map[string]bool{
	"DEBUG": true, "INFO": true, "WARNING": true, "ERROR": true, "CRITICAL": true,
}

type profilerLogLine struct {
	Severity string `json:"severity"`
	Time     string `json:"time"`
	Message  string `json:"message"`
	Logger   string `json:"logger"`
	PID      *int64 `json:"pid"`
	TID      *int64 `json:"tid"`
}

// collectProfilerLines splits container output into the profiler's JSON lines
// and any line still using the legacy plain-text pattern. Application output
// is ignored: the application has its own logger and is not what this test is
// about.
func collectProfilerLines(t *testing.T, logs string) (jsonLines []profilerLogLine, legacyLines []string) {
	t.Helper()
	for _, raw := range strings.Split(logs, "\n") {
		line := strings.TrimRight(raw, "\r")
		if line == "" {
			continue
		}
		if legacyProfilerLine.MatchString(line) {
			legacyLines = append(legacyLines, line)
			continue
		}
		if !strings.HasPrefix(line, "{") {
			// A line that names the profiler's logger but is not JSON was
			// written with some other pattern, such as spdlog's built-in one.
			if strings.Contains(line, profilerLoggerName) {
				legacyLines = append(legacyLines, line)
			}
			continue
		}
		var parsed profilerLogLine
		if err := json.Unmarshal([]byte(line), &parsed); err != nil {
			continue
		}
		if parsed.Logger != profilerLoggerName {
			continue
		}
		jsonLines = append(jsonLines, parsed)
	}
	return jsonLines, legacyLines
}

// exerciseApp drives a little traffic so the profiler gets past startup and
// has something to say.
func exerciseApp(t *testing.T, appBaseURL string) {
	t.Helper()
	client := &http.Client{Timeout: 30 * time.Second}
	for _, path := range []string{"/bike", "/scooter", "/car"} {
		resp := getOK(t, client, appBaseURL+path)
		require.Equal(t, http.StatusOK, resp.StatusCode)
		_ = resp.Body.Close()
	}
}

// TestNativeLogFormat covers both halves of the contract in one test: the
// default output is unchanged, and PYROSCOPE_LOG_FORMAT=json switches the
// container stream to single-line JSON with an explicit severity.
//
// Both are asserted together because the valuable property is the pair. JSON
// output that silently became the default would be a breaking change for every
// existing consumer, and a flag that quietly did nothing would be worse than no
// flag at all.
func TestNativeLogFormat(t *testing.T) {
	if containerLogsUnavailable(t) {
		return
	}

	t.Run("default is unchanged", func(t *testing.T) {
		net := dockertest.CreateNetwork(t)
		pyroscopeURL := startPyroscope(t, net)
		appBaseURL, container := startAppContainerWithEnv(t, net, pyroscopeURL, envLibcType(), envDotnetVersion(), false, nil)
		exerciseApp(t, appBaseURL)

		jsonLines, legacyLines := collectProfilerLines(t, container.Logs(t))
		if len(legacyLines) == 0 {
			t.Fatal("expected the profiler's plain-text lines by default, found none; the default output must not change")
		}
		if len(jsonLines) != 0 {
			t.Fatalf("expected no profiler JSON lines without PYROSCOPE_LOG_FORMAT, found %d (e.g. %+v)", len(jsonLines), jsonLines[0])
		}
	})

	t.Run("json format", func(t *testing.T) {
		assertJSONOutput(t, map[string]string{
			"PYROSCOPE_LOG_FORMAT": "json",
		})
	})

	// When the log file cannot be created the logger falls back to stderr
	// alone and reports the failure. That report must be JSON too. A non-root
	// container cannot create the default log directory, so in practice this
	// is the path most containers take.
	t.Run("json format when the log file cannot be created", func(t *testing.T) {
		jsonLines := assertJSONOutput(t, map[string]string{
			"PYROSCOPE_LOG_FORMAT":        "json",
			"PYROSCOPE_PROFILING_LOG_DIR": "/proc/pyroscope-unwritable",
		})
		for _, line := range jsonLines {
			if strings.Contains(line.Message, "Error creating native log file") {
				return
			}
		}
		t.Fatal("expected the log file failure to be reported as a JSON line; the fallback path was not exercised")
	})
}

// assertJSONOutput runs the app with env and checks that every profiler line
// on the container stream is well-formed JSON.
func assertJSONOutput(t *testing.T, env map[string]string) []profilerLogLine {
	t.Helper()
	net := dockertest.CreateNetwork(t)
	pyroscopeURL := startPyroscope(t, net)
	appBaseURL, container := startAppContainerWithEnv(t, net, pyroscopeURL, envLibcType(), envDotnetVersion(), false, env)
	exerciseApp(t, appBaseURL)

	logs := container.Logs(t)
	jsonLines, legacyLines := collectProfilerLines(t, logs)

	if len(jsonLines) == 0 {
		t.Fatalf("expected profiler JSON lines with PYROSCOPE_LOG_FORMAT=json, found none; logs:\n%s", logs)
	}
	if len(legacyLines) != 0 {
		t.Fatalf("expected no plain-text profiler lines with PYROSCOPE_LOG_FORMAT=json, found %d (e.g. %q)", len(legacyLines), legacyLines[0])
	}

	for i, line := range jsonLines {
		if !validSeverities[line.Severity] {
			t.Errorf("line %d: severity %q is not one of DEBUG/INFO/WARNING/ERROR/CRITICAL", i, line.Severity)
		}
		if line.Message == "" {
			t.Errorf("line %d: empty message", i)
		}
		if line.PID == nil || line.TID == nil {
			t.Errorf("line %d: pid and tid must be preserved, got pid=%v tid=%v", i, line.PID, line.TID)
		}
		// An explicit UTC offset is required. A bare local timestamp would
		// be assigned the wrong event time by the collector.
		if _, err := time.Parse(time.RFC3339, line.Time); err != nil {
			t.Errorf("line %d: time %q is not RFC 3339: %v", i, line.Time, err)
		}
		if !strings.HasSuffix(line.Time, "Z") {
			t.Errorf("line %d: time %q must carry an explicit UTC offset", i, line.Time)
		}
	}
	return jsonLines
}

// TestNativeLogLevel pins the level control. PYROSCOPE_LOG_LEVEL is set by this
// repository's own test images and documented for the Java SDK, but the native
// profiler ignored it, so a threshold that silently does nothing is exactly the
// failure this guards.
func TestNativeLogLevel(t *testing.T) {
	if containerLogsUnavailable(t) {
		return
	}

	net := dockertest.CreateNetwork(t)
	pyroscopeURL := startPyroscope(t, net)
	appBaseURL, container := startAppContainerWithEnv(t, net, pyroscopeURL, envLibcType(), envDotnetVersion(), false, map[string]string{
		"PYROSCOPE_LOG_FORMAT": "json",
		"PYROSCOPE_LOG_LEVEL":  "warn",
	})
	exerciseApp(t, appBaseURL)

	jsonLines, _ := collectProfilerLines(t, container.Logs(t))
	for i, line := range jsonLines {
		if line.Severity == "DEBUG" || line.Severity == "INFO" {
			t.Errorf("line %d: PYROSCOPE_LOG_LEVEL=warn must filter %s, got message %q", i, line.Severity, line.Message)
		}
	}
}

// containerLogsUnavailable skips on Windows, where the application runs on the
// host rather than in a container and there is no container stream to read.
func containerLogsUnavailable(t *testing.T) bool {
	t.Helper()
	if runtime.GOOS == "windows" {
		t.Skip("no container stream to read; the app runs on the host on Windows")
		return true
	}
	return false
}
