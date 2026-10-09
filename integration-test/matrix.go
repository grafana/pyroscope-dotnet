package integrationtest

import (
	"fmt"
	"os"
	"path/filepath"
	"runtime"
	"strings"
	"testing"
)

func envOrDefault(key, defaultValue string) string {
	if v := os.Getenv(key); v != "" {
		return v
	}
	return defaultValue
}

func envLibcType() string {
	def := "glibc"
	if runtime.GOOS == "windows" {
		// No libc axis on Windows; the value only feeds service names.
		def = "win"
	}
	return envOrDefault("LIBC_TYPE", def)
}
func envDotnetVersion() string { return envOrDefault("DOTNET_VERSION", "10.0") }

func dockerPlatform(goarch string) string {
	switch goarch {
	case "amd64":
		return "linux/amd64"
	case "arm64":
		return "linux/arm64"
	default:
		panic(fmt.Sprintf("unsupported architecture %q", goarch))
	}
}

func dotnetRuntimeID(goarch, libcType string) string {
	var arch string
	switch goarch {
	case "amd64":
		arch = "x64"
	case "arm64":
		arch = "arm64"
	default:
		panic(fmt.Sprintf("unsupported architecture %q", goarch))
	}

	switch libcType {
	case "glibc":
		return "linux-" + arch
	case "musl":
		return "linux-musl-" + arch
	default:
		panic(fmt.Sprintf("unsupported libc type %q", libcType))
	}
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

func profilerBinariesDir(t *testing.T) string {
	t.Helper()

	configuredDir := envOrDefault("PROFILER_BINARIES_DIR", "profiler-bin")
	fullDir := configuredDir
	if !filepath.IsAbs(fullDir) {
		fullDir = filepath.Join(repoRoot(), fullDir)
	}

	relativeDir, err := filepath.Rel(repoRoot(), fullDir)
	if err != nil || relativeDir == ".." || strings.HasPrefix(relativeDir, ".."+string(filepath.Separator)) {
		t.Fatalf("PROFILER_BINARIES_DIR must be inside the repository build context, got %q", configuredDir)
	}

	for _, name := range []string{"Pyroscope.Profiler.Native.so", "Pyroscope.Linux.ApiWrapper.x64.so"} {
		path := filepath.Join(fullDir, name)
		if _, err := os.Stat(path); err != nil {
			t.Fatalf("required profiler binary %q is unavailable: %v; build and stage profiler binaries before running integration tests", path, err)
		}
	}

	return filepath.ToSlash(relativeDir)
}
