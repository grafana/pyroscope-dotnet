package itest

import (
	"bytes"
	"context"
	"fmt"
	"net/http"
	"os"
	"path/filepath"
	"runtime"
	"strings"
	"testing"
	"time"

	"pyroscope-dotnet-itest/pyroscope/model"

	"connectrpc.com/connect"
	querierv1 "github.com/grafana/pyroscope/api/gen/proto/go/querier/v1"
	"github.com/grafana/pyroscope/api/gen/proto/go/querier/v1/querierv1connect"
	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
	"github.com/testcontainers/testcontainers-go"
	"github.com/testcontainers/testcontainers-go/network"
	"github.com/testcontainers/testcontainers-go/wait"
)

func envOrDefault(key, defaultValue string) string {
	if v := os.Getenv(key); v != "" {
		return v
	}
	return defaultValue
}

func repoRoot() string {
	_, filename, _, _ := runtime.Caller(0)
	return filepath.Dir(filepath.Dir(filename))
}

func flavour() string      { return envOrDefault("FLAVOUR", "glibc") }
func dotnetVersion() string { return envOrDefault("DOTNET_VERSION", "8.0") }

func rideshareImage() string {
	return fmt.Sprintf("rideshare-app-%s:net%s", flavour(), dotnetVersion())
}

func serviceName() string {
	return fmt.Sprintf("rideshare.dotnet.%s.%s.app", flavour(), dotnetVersion())
}

func startPyroscope(ctx context.Context, t *testing.T, net *testcontainers.DockerNetwork) string {
	t.Helper()
	c, err := testcontainers.GenericContainer(ctx, testcontainers.GenericContainerRequest{
		ContainerRequest: testcontainers.ContainerRequest{
			Image:        "grafana/pyroscope@sha256:0ad897bb457228c7d1133ce7004f83052e635be9daf4a7cc90364317637e9754",
			ExposedPorts: []string{"4040/tcp"},
			Networks:     []string{net.Name},
			NetworkAliases: map[string][]string{
				net.Name: {"pyroscope"},
			},
			WaitingFor: wait.ForHTTP("/ready").WithPort("4040/tcp").WithStartupTimeout(60 * time.Second),
		},
		Started: true,
	})
	require.NoError(t, err)
	t.Cleanup(func() { _ = c.Terminate(ctx) })

	host, err := c.Host(ctx)
	require.NoError(t, err)
	port, err := c.MappedPort(ctx, "4040/tcp")
	require.NoError(t, err)
	return fmt.Sprintf("http://%s:%s", host, port.Port())
}

func startApp(ctx context.Context, t *testing.T, net *testcontainers.DockerNetwork) {
	t.Helper()
	c, err := testcontainers.GenericContainer(ctx, testcontainers.GenericContainerRequest{
		ContainerRequest: testcontainers.ContainerRequest{
			Image:         rideshareImage(),
			ImagePlatform: "linux/amd64",
			Networks:      []string{net.Name},
			NetworkAliases: map[string][]string{
				net.Name: {"rideshare"},
			},
			Env: map[string]string{
				"REGION":                     "us-east",
				"PYROSCOPE_APPLICATION_NAME": serviceName(),
				"PYROSCOPE_SERVER_ADDRESS":   "http://pyroscope:4040",
				"DD_TRACE_DEBUG":             "true",
			},
			ExposedPorts: []string{"5000/tcp"},
			WaitingFor:   wait.ForHTTP("/bike").WithPort("5000/tcp").WithStartupTimeout(60 * time.Second),
		},
		Started: true,
	})
	require.NoError(t, err)
	t.Cleanup(func() { _ = c.Terminate(ctx) })
}

func startLoadGenerator(ctx context.Context, t *testing.T, net *testcontainers.DockerNetwork) {
	t.Helper()
	c, err := testcontainers.GenericContainer(ctx, testcontainers.GenericContainerRequest{
		ContainerRequest: testcontainers.ContainerRequest{
			FromDockerfile: testcontainers.FromDockerfile{
				Dockerfile: "Dockerfile.load-generator",
				Context:    filepath.Join(repoRoot(), "IntegrationTest"),
			},
			Networks: []string{net.Name},
			Env: map[string]string{
				"SERVICE_NAME": "rideshare",
			},
		},
		Started: true,
	})
	require.NoError(t, err)
	t.Cleanup(func() { _ = c.Terminate(ctx) })
}

func queryProfile(t *testing.T, pyroscopeURL string, labelSelector string) (string, error) {
	t.Helper()
	qc := querierv1connect.NewQuerierServiceClient(http.DefaultClient, pyroscopeURL)

	to := time.Now()
	from := to.Add(-1 * time.Hour)
	maxNodes := int64(65536)
	resp, err := qc.SelectMergeStacktraces(context.Background(),
		connect.NewRequest(&querierv1.SelectMergeStacktracesRequest{
			ProfileTypeID: "process_cpu:cpu:nanoseconds:cpu:nanoseconds",
			Start:         from.UnixMilli(),
			End:           to.UnixMilli(),
			LabelSelector: labelSelector,
			MaxNodes:      &maxNodes,
			Format:        querierv1.ProfileFormat_PROFILE_FORMAT_TREE,
		}))
	if err != nil {
		return "", err
	}
	if len(resp.Msg.Tree) == 0 {
		return "", nil
	}
	tt, err := model.UnmarshalTree(resp.Msg.Tree)
	if err != nil {
		return "", err
	}
	buf := bytes.NewBuffer(nil)
	tt.WriteCollapsed(buf)
	return buf.String(), nil
}

// frameContains checks that the collapsed stack output contains a frame with the
// given class name and method name. The .NET profiler emits frames in the format:
//
//	|ct:ClassName |cg:... |fn:MethodName |fg:... |sg:...
//
// Frames are separated by semicolons.
func frameContains(collapsed, className, methodName string) bool {
	needle := "|ct:" + className + " "
	idx := 0
	for {
		i := strings.Index(collapsed[idx:], needle)
		if i < 0 {
			return false
		}
		frameStart := idx + i
		frameEnd := strings.Index(collapsed[frameStart:], ";")
		var frame string
		if frameEnd < 0 {
			frame = collapsed[frameStart:]
		} else {
			frame = collapsed[frameStart : frameStart+frameEnd]
		}
		fnNeedle := "|fn:" + methodName + " "
		if strings.Contains(frame, fnNeedle) || strings.Contains(frame, "|fn:"+methodName+"\n") {
			return true
		}
		idx = frameStart + 1
		if idx >= len(collapsed) {
			return false
		}
	}
}

type vehicleCheck struct {
	vehicle        string
	expectedFrames [][2]string // each entry is [className, methodName]
}

func TestRideshareProfiles(t *testing.T) {
	ctx := context.Background()

	net, err := network.New(ctx)
	require.NoError(t, err)
	t.Cleanup(func() { _ = net.Remove(ctx) })

	pyroscopeURL := startPyroscope(ctx, t, net)
	startApp(ctx, t, net)
	startLoadGenerator(ctx, t, net)

	svcName := serviceName()

	checks := []vehicleCheck{
		{
			vehicle: "bike",
			expectedFrames: [][2]string{
				{"OrderService", "FindNearestVehicle"},
			},
		},
		{
			vehicle: "scooter",
			expectedFrames: [][2]string{
				{"OrderService", "FindNearestVehicle"},
			},
		},
		{
			vehicle: "car",
			expectedFrames: [][2]string{
				{"OrderService", "FindNearestVehicle"},
				{"OrderService", "CheckDriverAvailability"},
			},
		},
	}

	for _, check := range checks {
		check := check
		t.Run(check.vehicle, func(t *testing.T) {
			labelSelector := fmt.Sprintf(`{service_name="%s",vehicle="%s"}`, svcName, check.vehicle)
			var lastCollapsed string
			var lastErr error
			ok := assert.Eventually(t, func() bool {
				lastCollapsed, lastErr = queryProfile(t, pyroscopeURL, labelSelector)
				if lastErr != nil || lastCollapsed == "" {
					return false
				}
				for _, f := range check.expectedFrames {
					if !frameContains(lastCollapsed, f[0], f[1]) {
						return false
					}
				}
				return true
			}, 3*time.Minute, 5*time.Second)

			if !ok {
				t.Logf("label selector: %s", labelSelector)
				t.Logf("last profile query error: %v", lastErr)
				t.Logf("last collapsed profile:\n%s", lastCollapsed)
				t.FailNow()
			}
			t.Logf("collapsed profile for %s:\n%s", check.vehicle, lastCollapsed)
		})
	}
}
