package integrationtest

import (
	"fmt"
	"io"
	"net"
	"net/http"
	"strings"
	"testing"
	"time"

	"pyroscope-dotnet-integration-test/dockertest"
	"pyroscope-dotnet-integration-test/require"
)

func TestNativeUploadLogLevel(t *testing.T) {
	if containerLogsUnavailable(t) {
		return
	}
	for _, tc := range []struct {
		status   int
		severity string
	}{
		{200, "DEBUG"}, {204, "DEBUG"}, {302, "ERROR"},
		{400, "ERROR"}, {429, "WARNING"}, {500, "ERROR"},
	} {
		t.Run(fmt.Sprint(tc.status), func(t *testing.T) {
			const responseBody = "private-server-response-must-not-be-logged"
			ln, err := net.Listen("tcp", "0.0.0.0:0")
			require.NoError(t, err)
			received := make(chan struct{}, 1)
			server := &http.Server{Handler: http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
				if r.Method != http.MethodPost || r.URL.Path != "/push.v1.PusherService/Push" {
					http.NotFound(w, r)
					return
				}
				_, _ = io.Copy(io.Discard, r.Body)
				w.WriteHeader(tc.status)
				_, _ = io.WriteString(w, responseBody)
				select {
				case received <- struct{}{}:
				default:
				}
			})}
			go func() { _ = server.Serve(ln) }()
			t.Cleanup(func() { _ = server.Close() })
			libc, version := envLibcType(), envDotnetVersion()
			ensureProfilerImage(t, libc)
			var extraHosts []string
			if host := hostDockerInternalExtraHost(t); host != "" {
				extraHosts = append(extraHosts, host)
			}
			level := "info"
			if tc.severity == "DEBUG" {
				level = "debug"
			}
			container := dockertest.StartContainer(t, dockertest.ContainerRequest{
				Image:    buildAppImage(t, libc, version, false),
				Platform: "linux/amd64",
				Env: map[string]string{
					"REGION":                     "us-east",
					"PYROSCOPE_APPLICATION_NAME": "upload-log-test",
					"PYROSCOPE_SERVER_ADDRESS":   fmt.Sprintf("http://host.docker.internal:%d", ln.Addr().(*net.TCPAddr).Port),
					"PYROSCOPE_LOG_FORMAT":       "json",
					"PYROSCOPE_LOG_LEVEL":        level,
				},
				ExtraHosts:   extraHosts,
				ExposedPorts: []string{"5000/tcp"},
				WaitFor:      dockertest.WaitForHTTP("/healthz", "5000/tcp", 120*time.Second),
			})
			exerciseApp(t, "http://"+container.HostPort(t, "5000/tcp"))
			select {
			case <-received:
			case <-time.After(60 * time.Second):
				t.Fatal("profiler did not upload to the response fixture")
			}
			message := fmt.Sprintf("PyroscopePprofSink upload rejected with HTTP status %d", tc.status)
			if tc.severity == "DEBUG" {
				message = fmt.Sprintf("PyroscopePprofSink %d", tc.status)
			}
			deadline := time.Now().Add(30 * time.Second)
			for time.Now().Before(deadline) {
				logs := container.Logs(t)
				if strings.Contains(logs, responseBody) {
					t.Fatal("upload logging exposed the server response body")
				}
				lines, _ := collectProfilerLines(t, logs)
				found := false
				for _, line := range lines {
					if line.Message == message {
						require.Equal(t, tc.severity, line.Severity)
						found = true
					}
				}
				if found {
					return
				}
				time.Sleep(200 * time.Millisecond)
			}
			t.Fatalf("expected upload log %q at %s", message, tc.severity)
		})
	}
}
