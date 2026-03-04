package itest

import (
	"bytes"
	"context"
	"fmt"
	"net/http"
	"os"
	"os/exec"
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

func serviceName() string {
	flavour := envOrDefault("FLAVOUR", "glibc")
	dotnetVersion := envOrDefault("DOTNET_VERSION", "8.0")
	return fmt.Sprintf("rideshare.dotnet.%s.%s.app", flavour, dotnetVersion)
}

func composeServiceName() string {
	flavour := envOrDefault("FLAVOUR", "glibc")
	dotnetVersion := envOrDefault("DOTNET_VERSION", "8.0")
	versionHyphen := strings.ReplaceAll(dotnetVersion, ".", "-")
	return fmt.Sprintf("rideshare-%s-net-%s", flavour, versionHyphen)
}

func composeProjectName() string {
	flavour := envOrDefault("FLAVOUR", "glibc")
	dotnetVersion := envOrDefault("DOTNET_VERSION", "8.0")
	versionHyphen := strings.ReplaceAll(dotnetVersion, ".", "-")
	return fmt.Sprintf("pyroscope-dotnet-%s-net%s", flavour, versionHyphen)
}

func composeUp(t *testing.T) {
	t.Helper()
	svcName := composeServiceName()
	cmd := exec.Command("docker", "compose",
		"-f", "docker-compose-itest.yml",
		"-p", composeProjectName(),
		"up", "--build", "--force-recreate", "-d",
		"pyroscope", "load-generator", svcName,
	)
	cmd.Dir = repoRoot()
	cmd.Env = append(os.Environ(), "SERVICE_NAME="+svcName)
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr
	require.NoError(t, cmd.Run(), "docker compose up failed")
}

func composeDown(t *testing.T) {
	t.Helper()
	cmd := exec.Command("docker", "compose",
		"-f", "docker-compose-itest.yml",
		"-p", composeProjectName(),
		"down", "--remove-orphans", "--volumes",
	)
	cmd.Dir = repoRoot()
	cmd.Env = os.Environ()
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr
	_ = cmd.Run() // best-effort cleanup
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
	if resp.Msg.Tree == nil || len(resp.Msg.Tree) == 0 {
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

type vehicleCheck struct {
	vehicle           string
	expectedFunctions []string
}

func TestRideshareProfiles(t *testing.T) {
	pyroscopeURL := envOrDefault("PYROSCOPE_URL", "http://localhost:4040")
	svcName := serviceName()

	composeUp(t)
	t.Cleanup(func() { composeDown(t) })

	checks := []vehicleCheck{
		{
			vehicle: "bike",
			expectedFunctions: []string{
				"OrderService.FindNearestVehicle",
			},
		},
		{
			vehicle: "scooter",
			expectedFunctions: []string{
				"OrderService.FindNearestVehicle",
			},
		},
		{
			vehicle: "car",
			expectedFunctions: []string{
				"OrderService.FindNearestVehicle",
				"OrderService.CheckDriverAvailability",
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
				for _, fn := range check.expectedFunctions {
					if !strings.Contains(lastCollapsed, fn) {
						return false
					}
				}
				return true
			}, 90*time.Second, 2*time.Second)

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
