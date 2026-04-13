package itest

import (
	"fmt"
	"os/exec"
	"path/filepath"
	"runtime"
	"sync"
	"testing"
)

var Flavours = []string{"glibc", "musl"}
var DotnetVersions = []string{"6.0", "8.0", "9.0", "10.0"}

func sdkImageSuffix(flavour, version string) string {
	if flavour == "musl" {
		return "-alpine"
	}
	switch version {
	case "6.0", "8.0":
		return "-jammy"
	default:
		return "-noble"
	}
}

func profilerDockerfile(flavour string) string {
	if flavour == "musl" {
		return "Pyroscope.musl.Dockerfile"
	}
	return "Pyroscope.Dockerfile"
}

func profilerImageTag(flavour string) string {
	return fmt.Sprintf("pyroscope-dotnet-%s:test", flavour)
}

func appDockerfile(otel bool) string {
	if otel {
		return "itest-with-otel.Dockerfile"
	}
	return "itest.Dockerfile"
}

func rideshareServiceName(flavour, version string, otel bool) string {
	base := fmt.Sprintf("rideshare.dotnet.%s.%s.app", flavour, version)
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

func ensureProfilerImage(t *testing.T, flavour string) {
	t.Helper()
	val, _ := profilerBuilds.LoadOrStore(flavour, &profilerBuildResult{})
	pb := val.(*profilerBuildResult)
	pb.once.Do(func() {
		tag := profilerImageTag(flavour)
		t.Logf("building profiler image %s ...", tag)
		cmd := exec.Command("docker", "build",
			"--platform", "linux/amd64",
			"-t", tag,
			"-f", filepath.Join(repoRoot(), profilerDockerfile(flavour)),
			"--build-arg", "CMAKE_BUILD_TYPE=Debug",
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
