package integrationtest

import (
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strings"
	"sync"
	"testing"
)

func envOrDefault(key, defaultValue string) string {
	if v := os.Getenv(key); v != "" {
		return v
	}
	return defaultValue
}

func envLibcType() string      { return envOrDefault("LIBC_TYPE", "glibc") }
func envDotnetVersion() string { return envOrDefault("DOTNET_VERSION", "10.0") }

func envRunASAN() string {
	value := envOrDefault("RUN_ASAN", "OFF")
	switch strings.ToUpper(value) {
	case "1", "TRUE", "YES", "ON":
		return "ON"
	case "0", "FALSE", "NO", "OFF":
		return "OFF"
	default:
		panic(fmt.Sprintf("unknown RUN_ASAN value %q: expected ON or OFF", value))
	}
}

func runASANEnabled() bool { return envRunASAN() == "ON" }

func asanImageSuffix() string {
	if runASANEnabled() {
		return "-asan"
	}
	return ""
}

func asanRuntimePreloadPrefix() string {
	if runASANEnabled() {
		return "/opt/llvm-asan/libasan.so:"
	}
	return ""
}

func sdkImageSuffix(libcType, version string) string {
	if libcType == "musl" {
		return "-alpine"
	}
	switch version {
	case "8.0":
		return "-jammy"
	case "9.0", "10.0":
		return "-noble"
	default:
		panic(fmt.Sprintf("unknown dotnet version %q: add the SDK image suffix mapping", version))
	}
}

func profilerDockerfile(libcType string) string {
	if libcType == "musl" {
		return "Pyroscope.musl.Dockerfile"
	}
	return "Pyroscope.Dockerfile"
}

func profilerImageTag(libcType string) string {
	return fmt.Sprintf("pyroscope-dotnet-%s%s:test", libcType, asanImageSuffix())
}

func appDockerfile(otel bool) string {
	if otel {
		return "integration-test-with-otel.Dockerfile"
	}
	return "integration-test.Dockerfile"
}

func rideshareServiceName(libcType, version string, otel bool) string {
	base := fmt.Sprintf("rideshare.dotnet.%s.%s.app", libcType, version)
	if otel {
		base += "-otel"
	}
	return base
}

func repoRoot() string {
	_, filename, _, _ := runtime.Caller(0)
	return filepath.Dir(filepath.Dir(filename))
}

type profilerBuildResult struct {
	once sync.Once
	err  error
}

var profilerBuilds sync.Map

func ensureProfilerImage(t *testing.T, libcType string) {
	t.Helper()
	runASAN := envRunASAN()
	key := libcType + "-" + runASAN
	val, _ := profilerBuilds.LoadOrStore(key, &profilerBuildResult{})
	pb := val.(*profilerBuildResult)
	pb.once.Do(func() {
		tag := profilerImageTag(libcType)
		t.Logf("building profiler image %s ...", tag)
		cmd := exec.Command("docker", "build",
			"--platform", "linux/amd64",
			"-t", tag,
			"-f", filepath.Join(repoRoot(), profilerDockerfile(libcType)),
			"--build-arg", "CMAKE_BUILD_TYPE=Debug",
			"--build-arg", "RUN_ASAN="+runASAN,
			repoRoot(),
		)
		out, err := cmd.CombinedOutput()
		if err != nil {
			pb.err = fmt.Errorf("failed to build profiler image %s: %w\n%s", tag, err, out)
			return
		}
		t.Logf("profiler image %s built successfully", tag)
	})
	if pb.err != nil {
		t.Fatal(pb.err)
	}
}
