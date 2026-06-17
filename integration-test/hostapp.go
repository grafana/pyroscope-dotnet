package integrationtest

// Windows backend: the Pyroscope server still runs in the same pinned Linux
// container (in WSL2 Docker — see the DOCKERTEST_DOCKER_COMMAND override in
// dockertest), but the profiled app runs as a Windows host process, which is
// the point of the Windows tests. The profiler binaries are prebuilt and
// located via WINDOWS_PROFILER_DIR (in CI: the build job's artifact).

import (
	"archive/zip"
	"bytes"
	"fmt"
	"io"
	"maps"
	"net"
	"net/http"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"sync"
	"testing"
	"time"
)

const (
	windowsProfilerDirEnv = "WINDOWS_PROFILER_DIR"
	profilerCLSID         = "{BD1A650D-AC5D-4896-B64F-D6FA25D6B26A}"
	otelProfilerCLSID     = "{918728DD-259F-4A6A-AC2B-B85E1B658318}"
	// Keep in sync with integration-test-with-otel.Dockerfile.
	otelAutoVersion = "1.14.1"
)

func windowsProfilerDir(t *testing.T) string {
	t.Helper()
	dir := os.Getenv(windowsProfilerDirEnv)
	if dir == "" {
		t.Fatalf("%s is not set; point it at a directory containing the built Windows profiler", windowsProfilerDirEnv)
	}
	abs, err := filepath.Abs(dir)
	if err != nil {
		t.Fatalf("%s: %v", windowsProfilerDirEnv, err)
	}
	if _, err := os.Stat(filepath.Join(abs, "Pyroscope.Profiler.Native.dll")); err != nil {
		t.Fatalf("%s does not contain the profiler: %v", windowsProfilerDirEnv, err)
	}
	return abs
}

// publishRideshare publishes the IntegrationTest app for the given .NET
// version, once per version per test process.
type publishResult struct {
	once sync.Once
	dir  string
	err  error
}

var publishes sync.Map

func publishRideshare(t *testing.T, version string) string {
	t.Helper()
	val, _ := publishes.LoadOrStore(version, &publishResult{})
	pr := val.(*publishResult)
	pr.once.Do(func() {
		// A dedicated output dir: .NET 10+ cleans the output dir before
		// compiling, so it must not be the source dir.
		out := filepath.Join(repoRoot(), "IntegrationTest", "bin", "integration-win", version)
		cmd := exec.Command("dotnet", "publish", filepath.Join(repoRoot(), "IntegrationTest"),
			"--framework", "net"+version, "-c", "Release",
			"--runtime", "win-x64", "--no-self-contained", "-o", out)
		if output, err := cmd.CombinedOutput(); err != nil {
			pr.err = fmt.Errorf("dotnet publish (net%s) failed: %w\n%s", version, err, output)
			return
		}
		pr.dir = out
	})
	if pr.err != nil {
		t.Fatal(pr.err)
	}
	return pr.dir
}

// ensureOTelAuto downloads and extracts the OpenTelemetry .NET automatic
// instrumentation for Windows, once per test process. The WithOTEL tests use
// it to occupy the classic profiler slot so Pyroscope attaches through the
// notification-profiler mechanism.
var otelAuto publishResult

func ensureOTelAutoHome(t *testing.T) string {
	t.Helper()
	otelAuto.once.Do(func() {
		home := filepath.Join(repoRoot(), "IntegrationTest", "bin", "otel-dotnet-auto")
		marker := filepath.Join(home, "win-x64", "OpenTelemetry.AutoInstrumentation.Native.dll")
		if _, err := os.Stat(marker); err == nil {
			otelAuto.dir = home
			return
		}
		url := fmt.Sprintf("https://github.com/open-telemetry/opentelemetry-dotnet-instrumentation/releases/download/v%s/opentelemetry-dotnet-instrumentation-windows.zip", otelAutoVersion)
		zipPath := home + ".zip"
		if err := downloadFile(url, zipPath); err != nil {
			otelAuto.err = err
			return
		}
		if err := unzip(zipPath, home); err != nil {
			otelAuto.err = err
			return
		}
		if _, err := os.Stat(marker); err != nil {
			otelAuto.err = fmt.Errorf("otel auto instrumentation extracted but %s is missing", marker)
			return
		}
		otelAuto.dir = home
	})
	if otelAuto.err != nil {
		t.Fatal(otelAuto.err)
	}
	return otelAuto.dir
}

func downloadFile(url, dest string) error {
	if err := os.MkdirAll(filepath.Dir(dest), 0o755); err != nil {
		return err
	}
	resp, err := http.Get(url)
	if err != nil {
		return fmt.Errorf("downloading %s: %w", url, err)
	}
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		return fmt.Errorf("downloading %s: HTTP %d", url, resp.StatusCode)
	}
	f, err := os.Create(dest)
	if err != nil {
		return err
	}
	defer f.Close()
	_, err = io.Copy(f, resp.Body)
	return err
}

func unzip(src, dest string) error {
	r, err := zip.OpenReader(src)
	if err != nil {
		return err
	}
	defer r.Close()
	for _, f := range r.File {
		path := filepath.Join(dest, f.Name)
		if !strings.HasPrefix(path, filepath.Clean(dest)+string(os.PathSeparator)) {
			return fmt.Errorf("zip entry escapes destination: %s", f.Name)
		}
		if f.FileInfo().IsDir() {
			if err := os.MkdirAll(path, 0o755); err != nil {
				return err
			}
			continue
		}
		if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
			return err
		}
		rc, err := f.Open()
		if err != nil {
			return err
		}
		out, err := os.Create(path)
		if err != nil {
			rc.Close()
			return err
		}
		_, err = io.Copy(out, rc)
		rc.Close()
		out.Close()
		if err != nil {
			return err
		}
	}
	return nil
}

func freePort(t *testing.T) int {
	t.Helper()
	ln, err := net.Listen("tcp4", "127.0.0.1:0")
	if err != nil {
		t.Fatalf("allocating port: %v", err)
	}
	port := ln.Addr().(*net.TCPAddr).Port
	_ = ln.Close()
	return port
}

// startHostApp runs the published rideshare app as a host process under the
// prebuilt Windows profiler and waits for /healthz. Mirrors the env baked
// into integration-test.Dockerfile (and its -with-otel variant).
func startHostApp(t *testing.T, version string, otel bool, svcName, serverAddress string, extraEnv map[string]string) string {
	t.Helper()
	profDir := windowsProfilerDir(t)
	profilerDLL := filepath.Join(profDir, "Pyroscope.Profiler.Native.dll")
	exeDir := publishRideshare(t, version)
	port := freePort(t)

	env := map[string]string{
		"REGION":                                 "us-east",
		"PYROSCOPE_APPLICATION_NAME":             svcName,
		"PYROSCOPE_SERVER_ADDRESS":               serverAddress,
		"DD_TRACE_DEBUG":                         "true",
		"PYROSCOPE_LOG_LEVEL":                    "debug",
		"PYROSCOPE_PROFILING_ENABLED":            "1",
		"PYROSCOPE_PROFILING_ALLOCATION_ENABLED": "true",
		"PYROSCOPE_PROFILING_CONTENTION_ENABLED": "true",
		"PYROSCOPE_PROFILING_EXCEPTION_ENABLED":  "true",
		"PYROSCOPE_PROFILING_HEAP_ENABLED":       "true",
		"ASPNETCORE_URLS":                        fmt.Sprintf("http://127.0.0.1:%d", port),
	}
	if otel {
		home := ensureOTelAutoHome(t)
		env["OTEL_DOTNET_AUTO_HOME"] = home
		env["CORECLR_ENABLE_PROFILING"] = "1"
		env["CORECLR_PROFILER"] = otelProfilerCLSID
		env["CORECLR_PROFILER_PATH"] = filepath.Join(home, "win-x64", "OpenTelemetry.AutoInstrumentation.Native.dll")
		env["DOTNET_ADDITIONAL_DEPS"] = filepath.Join(home, "AdditionalDeps")
		env["DOTNET_SHARED_STORE"] = filepath.Join(home, "store")
		env["DOTNET_STARTUP_HOOKS"] = filepath.Join(home, "net", "OpenTelemetry.AutoInstrumentation.StartupHook.dll")
		env["OTEL_TRACES_EXPORTER"] = "none"
		env["OTEL_METRICS_EXPORTER"] = "none"
		env["OTEL_LOGS_EXPORTER"] = "none"
		env["CORECLR_ENABLE_NOTIFICATION_PROFILERS"] = "1"
		// Trailing semicolon required for .NET 9+; see dotnet/runtime#126197.
		env["CORECLR_NOTIFICATION_PROFILERS"] = profilerDLL + "=" + profilerCLSID + ";"
	} else {
		env["CORECLR_ENABLE_PROFILING"] = "1"
		env["CORECLR_PROFILER"] = profilerCLSID
		env["CORECLR_PROFILER_PATH"] = profilerDLL
	}
	maps.Copy(env, extraEnv)

	cmd := exec.Command(filepath.Join(exeDir, "rideshare.exe"))
	cmd.Dir = exeDir
	cmd.Env = os.Environ()
	for k, v := range env {
		cmd.Env = append(cmd.Env, k+"="+v)
	}
	var output syncBuffer
	cmd.Stdout = &output
	cmd.Stderr = &output
	if err := cmd.Start(); err != nil {
		t.Fatalf("starting rideshare.exe: %v", err)
	}
	t.Cleanup(func() {
		_ = cmd.Process.Kill()
		_, _ = cmd.Process.Wait()
		if t.Failed() {
			full := output.String()
			// The load generator floods stdout with request logs; surface the
			// profiler's own sink/upload lines separately so they aren't lost.
			if sink := profilerLines(full); sink != "" {
				t.Logf("rideshare profiler lines:\n%s", sink)
			}
			t.Logf("rideshare output (tail):\n%s", tailLines(full, 60))
		}
	})

	baseURL := fmt.Sprintf("http://127.0.0.1:%d", port)
	deadline := time.Now().Add(120 * time.Second)
	client := &http.Client{Timeout: 2 * time.Second}
	for time.Now().Before(deadline) {
		if resp, err := client.Get(baseURL + "/healthz"); err == nil {
			_ = resp.Body.Close()
			if resp.StatusCode == http.StatusOK {
				return baseURL
			}
		}
		time.Sleep(time.Second)
	}
	t.Fatalf("rideshare.exe did not become healthy; output (tail):\n%s", tailLines(output.String(), 60))
	return ""
}

type syncBuffer struct {
	mu  sync.Mutex
	buf bytes.Buffer
}

func (b *syncBuffer) Write(p []byte) (int, error) {
	b.mu.Lock()
	defer b.mu.Unlock()
	return b.buf.Write(p)
}

func (b *syncBuffer) String() string {
	b.mu.Lock()
	defer b.mu.Unlock()
	return b.buf.String()
}

// profilerLines returns the lines of native-profiler output that concern the
// upload sink or TLS, filtering out the app's own request logging.
func profilerLines(s string) string {
	var b strings.Builder
	for _, ln := range strings.Split(s, "\n") {
		if strings.Contains(ln, "PyroscopePprofSink") ||
			strings.Contains(ln, "SSL") ||
			strings.Contains(ln, "ssl") {
			b.WriteString(ln)
			b.WriteByte('\n')
		}
	}
	return b.String()
}

func tailLines(s string, n int) string {
	lines := strings.Split(s, "\n")
	if len(lines) > n {
		lines = lines[len(lines)-n:]
	}
	return strings.Join(lines, "\n")
}
