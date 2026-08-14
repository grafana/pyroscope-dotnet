package integrationtest

import "testing"

func TestDockerPlatform(t *testing.T) {
	tests := map[string]string{
		"amd64": "linux/amd64",
		"arm64": "linux/arm64",
	}
	for goarch, want := range tests {
		t.Run(goarch, func(t *testing.T) {
			if got := dockerPlatform(goarch); got != want {
				t.Fatalf("dockerPlatform(%q) = %q, want %q", goarch, got, want)
			}
		})
	}
}

func TestDotnetRuntimeID(t *testing.T) {
	tests := []struct {
		goarch   string
		libcType string
		want     string
	}{
		{goarch: "amd64", libcType: "glibc", want: "linux-x64"},
		{goarch: "amd64", libcType: "musl", want: "linux-musl-x64"},
		{goarch: "arm64", libcType: "glibc", want: "linux-arm64"},
		{goarch: "arm64", libcType: "musl", want: "linux-musl-arm64"},
	}
	for _, test := range tests {
		t.Run(test.goarch+"-"+test.libcType, func(t *testing.T) {
			if got := dotnetRuntimeID(test.goarch, test.libcType); got != test.want {
				t.Fatalf("dotnetRuntimeID(%q, %q) = %q, want %q", test.goarch, test.libcType, got, test.want)
			}
		})
	}
}

func TestArchitectureHelpersRejectUnsupportedValues(t *testing.T) {
	assertPanics(t, func() { dockerPlatform("386") })
	assertPanics(t, func() { dotnetRuntimeID("386", "glibc") })
	assertPanics(t, func() { dotnetRuntimeID("amd64", "unknown") })
}

func assertPanics(t *testing.T, fn func()) {
	t.Helper()
	defer func() {
		if recover() == nil {
			t.Fatal("expected panic")
		}
	}()
	fn()
}
