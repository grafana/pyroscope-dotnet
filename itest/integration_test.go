package itest

import (
	"bytes"
	"context"
	"crypto/ecdsa"
	"crypto/elliptic"
	"crypto/rand"
	"crypto/tls"
	"crypto/x509"
	"crypto/x509/pkix"
	"encoding/pem"
	"fmt"
	"math/big"
	"net"
	"net/http"
	"os"
	"path/filepath"
	"regexp"
	"runtime"
	"strings"
	"testing"
	"time"

	"pyroscope-dotnet-itest/pyroscope/model"

	"connectrpc.com/connect"
	dockercontainer "github.com/docker/docker/api/types/container"
	dockernetwork "github.com/docker/docker/api/types/network"
	dockerclient "github.com/docker/docker/client"
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

func flavour() string       { return envOrDefault("FLAVOUR", "glibc") }
func dotnetVersion() string { return envOrDefault("DOTNET_VERSION", "8.0") }
func isOTEL() bool          { return os.Getenv("OTEL") == "true" }

func rideshareImage() string {
	base := fmt.Sprintf("rideshare-app-%s", flavour())
	if isOTEL() {
		base += "-otel"
	}
	return fmt.Sprintf("%s:net%s", base, dotnetVersion())
}

func serviceName() string {
	base := fmt.Sprintf("rideshare.dotnet.%s.%s.app", flavour(), dotnetVersion())
	if isOTEL() {
		base += "-otel"
	}
	return base
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

func startApp(ctx context.Context, t *testing.T, net *testcontainers.DockerNetwork) string {
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
			WaitingFor:   wait.ForListeningPort("5000/tcp").WithStartupTimeout(120 * time.Second),
		},
		Started: true,
	})
	require.NoError(t, err)
	t.Cleanup(func() { _ = c.Terminate(ctx) })

	host, err := c.Host(ctx)
	require.NoError(t, err)
	port, err := c.MappedPort(ctx, "5000/tcp")
	require.NoError(t, err)
	return fmt.Sprintf("http://%s:%s", host, port.Port())
}

// runLoadGenerator cycles through /bike, /scooter, /car requests in a goroutine
// until ctx is cancelled.
func runLoadGenerator(ctx context.Context, t *testing.T, appBaseURL string) {
	t.Helper()
	vehicles := []string{"bike", "scooter", "car"}
	go func() {
		client := &http.Client{Timeout: 10 * time.Second}
		i := 0
		for {
			select {
			case <-ctx.Done():
				return
			default:
			}
			vehicle := vehicles[i%len(vehicles)]
			i++
			url := appBaseURL + "/" + vehicle
			resp, err := client.Get(url)
			if err == nil {
				_ = resp.Body.Close()
			}
			select {
			case <-ctx.Done():
				return
			case <-time.After(300 * time.Millisecond):
			}
		}
	}()
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

// frameContains checks that the collapsed stack output contains a frame where
// the class name and method name both match. The .NET profiler emits frames in
// the format:
//
//	Namespace!ClassName<Generics>.MethodName<MethodGenerics>
//
// The "!" separates namespace from type; "." separates type from method.
// Frames within a stack are separated by semicolons; stacks are separated by newlines.
func frameContains(collapsed, className, methodName string) bool {
	pattern := `(?:^|!)` + regexp.QuoteMeta(className) + `(?:<[^>]*>)?\.` + regexp.QuoteMeta(methodName) + `(?:<|;|\s|$)`
	re := regexp.MustCompile(pattern)
	for _, line := range strings.Split(collapsed, "\n") {
		for _, frame := range strings.Split(line, ";") {
			if re.MatchString(frame) {
				return true
			}
		}
	}
	return false
}

type vehicleCheck struct {
	vehicle        string
	expectedFrames [][2]string // each entry is [className, methodName]
}

func TestRideshareProfiles(t *testing.T) {
	ctx, cancel := context.WithCancel(context.Background())
	t.Cleanup(cancel)

	net, err := network.New(ctx)
	require.NoError(t, err)
	t.Cleanup(func() { _ = net.Remove(ctx) })

	pyroscopeURL := startPyroscope(ctx, t, net)
	appBaseURL := startApp(ctx, t, net)
	runLoadGenerator(ctx, t, appBaseURL)

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

// generateTestCerts creates a self-signed CA and a server certificate signed by
// it for the given hostname. Returns CA cert PEM, server cert PEM, server key PEM.
func generateTestCerts(t *testing.T, hostname string) (caCertPEM, serverCertPEM, serverKeyPEM []byte) {
	t.Helper()

	caKey, err := ecdsa.GenerateKey(elliptic.P256(), rand.Reader)
	require.NoError(t, err)

	caTemplate := &x509.Certificate{
		SerialNumber:          big.NewInt(1),
		Subject:               pkix.Name{CommonName: "pyroscope-test-ca"},
		NotBefore:             time.Now().Add(-time.Minute),
		NotAfter:              time.Now().Add(24 * time.Hour),
		IsCA:                  true,
		KeyUsage:              x509.KeyUsageCertSign,
		BasicConstraintsValid: true,
	}
	caDER, err := x509.CreateCertificate(rand.Reader, caTemplate, caTemplate, &caKey.PublicKey, caKey)
	require.NoError(t, err)
	caCertPEM = pem.EncodeToMemory(&pem.Block{Type: "CERTIFICATE", Bytes: caDER})
	caCert, err := x509.ParseCertificate(caDER)
	require.NoError(t, err)

	serverKey, err := ecdsa.GenerateKey(elliptic.P256(), rand.Reader)
	require.NoError(t, err)

	serverTemplate := &x509.Certificate{
		SerialNumber: big.NewInt(2),
		Subject:      pkix.Name{CommonName: hostname},
		DNSNames:     []string{hostname},
		NotBefore:    time.Now().Add(-time.Minute),
		NotAfter:     time.Now().Add(24 * time.Hour),
		KeyUsage:     x509.KeyUsageDigitalSignature,
		ExtKeyUsage:  []x509.ExtKeyUsage{x509.ExtKeyUsageServerAuth},
	}
	serverDER, err := x509.CreateCertificate(rand.Reader, serverTemplate, caCert, &serverKey.PublicKey, caKey)
	require.NoError(t, err)
	serverCertPEM = pem.EncodeToMemory(&pem.Block{Type: "CERTIFICATE", Bytes: serverDER})

	serverKeyDER, err := x509.MarshalECPrivateKey(serverKey)
	require.NoError(t, err)
	serverKeyPEM = pem.EncodeToMemory(&pem.Block{Type: "EC PRIVATE KEY", Bytes: serverKeyDER})

	return
}

// startTLSProfileReceiver starts a Go TLS HTTP server on 0.0.0.0 and returns
// the port it is listening on, the CA certificate PEM, and a channel that
// receives a value when the first profile request arrives. The server is
// stopped when the test finishes.
func startTLSProfileReceiver(t *testing.T, hostname string) (port int, caCertPEM []byte, received <-chan struct{}) {
	t.Helper()

	caCertPEM, serverCertPEM, serverKeyPEM := generateTestCerts(t, hostname)

	cert, err := tls.X509KeyPair(serverCertPEM, serverKeyPEM)
	require.NoError(t, err)

	ch := make(chan struct{}, 1)
	handler := http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		t.Logf("TLS profile receiver: %s %s", r.Method, r.URL.Path)
		select {
		case ch <- struct{}{}:
		default:
		}
		w.WriteHeader(http.StatusOK)
	})

	ln, err := tls.Listen("tcp", "0.0.0.0:0", &tls.Config{
		Certificates: []tls.Certificate{cert},
	})
	require.NoError(t, err)

	srv := &http.Server{Handler: handler}
	go func() { _ = srv.Serve(ln) }()
	t.Cleanup(func() { _ = srv.Close() })

	port = ln.Addr().(*net.TCPAddr).Port
	received = ch
	return
}

// dockerBridgeGatewayIP returns the gateway IP of the Docker bridge network.
// On Linux this is the IP address that containers use to reach the host.
func dockerBridgeGatewayIP(ctx context.Context, t *testing.T) string {
	t.Helper()
	cli, err := dockerclient.NewClientWithOpts(dockerclient.FromEnv, dockerclient.WithAPIVersionNegotiation())
	require.NoError(t, err)
	defer cli.Close()

	info, err := cli.NetworkInspect(ctx, "bridge", dockernetwork.InspectOptions{})
	require.NoError(t, err)
	for _, cfg := range info.IPAM.Config {
		if cfg.Gateway != "" {
			return cfg.Gateway
		}
	}
	t.Fatal("could not determine Docker bridge gateway IP")
	return ""
}

// hostDockerInternalExtraHost returns the ExtraHosts entry that makes
// host.docker.internal reachable from inside a container.
//
// On macOS (Docker Desktop / OrbStack) host.docker.internal is configured
// automatically by the runtime, so no extra entry is needed.
// On Linux the hostname is not injected by default; we map it to the Docker
// bridge gateway, which is the address containers use to reach the host.
func hostDockerInternalExtraHost(ctx context.Context, t *testing.T) string {
	t.Helper()
	if runtime.GOOS != "linux" {
		return ""
	}
	return "host.docker.internal:" + dockerBridgeGatewayIP(ctx, t)
}

// startAppForTLSTest starts the rideshare app configured to send profiles to
// the Go TLS receiver running on the Docker host via host.docker.internal.
// The CA certificate is appended to /etc/ssl/cert.pem (the default cert file
// for our statically-linked OpenSSL build with --openssldir=/etc/ssl).
func startAppForTLSTest(ctx context.Context, t *testing.T, imageName, serverAddress string, caCertPEM []byte) string {
	t.Helper()

	extraHost := hostDockerInternalExtraHost(ctx, t)

	c, err := testcontainers.GenericContainer(ctx, testcontainers.GenericContainerRequest{
		ContainerRequest: testcontainers.ContainerRequest{
			Image:         imageName,
			ImagePlatform: "linux/amd64",
			Env: map[string]string{
				"REGION":                     "us-east",
				"PYROSCOPE_APPLICATION_NAME": "tls-test-app",
				"PYROSCOPE_SERVER_ADDRESS":   serverAddress,
				"DD_TRACE_DEBUG":             "true",
			},
			Files: []testcontainers.ContainerFile{
				{
					Reader:            bytes.NewReader(caCertPEM),
					ContainerFilePath: "/tmp/ca.crt",
					FileMode:          0644,
				},
			},
			// Append the test CA to /etc/ssl/cert.pem before the profiler
			// starts sending. Our OpenSSL (--openssldir=/etc/ssl) uses this
			// file as its default bundle on both glibc (Ubuntu/jammy) and
			// musl (Alpine) images.
			LifecycleHooks: []testcontainers.ContainerLifecycleHooks{
				{
					PostStarts: []testcontainers.ContainerHook{
						func(ctx context.Context, c testcontainers.Container) error {
							_, _, err := c.Exec(ctx, []string{"sh", "-c", "cat /tmp/ca.crt >> /etc/ssl/cert.pem"})
							return err
						},
					},
				},
			},
			// On Linux, add host.docker.internal → bridge gateway so the
			// container can reach the Go TLS server running on the host.
			// On macOS the entry is empty and the modifier is a no-op.
			HostConfigModifier: func(hc *dockercontainer.HostConfig) {
				if extraHost != "" {
					hc.ExtraHosts = append(hc.ExtraHosts, extraHost)
				}
			},
			ExposedPorts: []string{"5000/tcp"},
			WaitingFor:   wait.ForListeningPort("5000/tcp").WithStartupTimeout(120 * time.Second),
		},
		Started: true,
	})
	require.NoError(t, err)
	t.Cleanup(func() { _ = c.Terminate(ctx) })

	host, err := c.Host(ctx)
	require.NoError(t, err)
	mappedPort, err := c.MappedPort(ctx, "5000/tcp")
	require.NoError(t, err)
	return fmt.Sprintf("http://%s:%s", host, mappedPort.Port())
}

// TestTLSProfileUpload verifies that the profiler can deliver profiles over
// HTTPS with TLS certificate verification enabled. A Go TLS server running on
// the test host receives the profile; the app container reaches it via the
// "profile-receiver" host alias (resolved to the Docker host gateway).
//
// The image under test is selected by FLAVOUR (glibc or musl) and
// DOTNET_VERSION, following the same convention as the other integration tests.
// Two Makefile targets drive this test: itest/tls-profile-upload/glibc/8.0 and
// itest/tls-profile-upload/musl/8.0.
func TestTLSProfileUpload(t *testing.T) {
	ctx, cancel := context.WithCancel(context.Background())
	t.Cleanup(cancel)

	imageName := fmt.Sprintf("rideshare-app-%s:net%s", flavour(), dotnetVersion())

	// host.docker.internal is the standard hostname for reaching the Docker
	// host from inside a container (macOS: built-in; Linux: mapped via ExtraHosts).
	const hostname = "host.docker.internal"
	port, caCertPEM, received := startTLSProfileReceiver(t, hostname)
	serverAddress := fmt.Sprintf("https://%s:%d", hostname, port)

	appBaseURL := startAppForTLSTest(ctx, t, imageName, serverAddress, caCertPEM)
	runLoadGenerator(ctx, t, appBaseURL)

	select {
	case <-received:
		t.Logf("profile received over TLS from %s", imageName)
	case <-time.After(3 * time.Minute):
		t.Fatalf("timed out waiting for profile upload from %s", imageName)
	}
}

// TestDynamicProfilingToggle verifies that dynamically disabling and re-enabling
// profiling does not crash the process. This reproduces
// https://github.com/grafana/pyroscope-dotnet/issues/259 where calling
// SetCPUTrackingEnabled(true) after a disable would SIGSEGV because
// StackSamplerLoop::ResetThreadsCpuConsumption dereferenced a null _targetThread.
func TestDynamicProfilingToggle(t *testing.T) {
	ctx, cancel := context.WithCancel(context.Background())
	t.Cleanup(cancel)

	net, err := network.New(ctx)
	require.NoError(t, err)
	t.Cleanup(func() { _ = net.Remove(ctx) })

	pyroscopeURL := startPyroscope(ctx, t, net)
	appBaseURL := startApp(ctx, t, net)

	// Generate some load first so managed threads are registered.
	runLoadGenerator(ctx, t, appBaseURL)
	time.Sleep(3 * time.Second)

	// Trigger the dynamic disable/re-enable cycle.
	// Before the fix this would kill the process with SIGSEGV (exit 139).
	client := &http.Client{Timeout: 30 * time.Second}
	resp, err := client.Get(appBaseURL + "/dynamic-toggle")
	require.NoError(t, err, "dynamic-toggle request failed — the app likely crashed")
	_ = resp.Body.Close()
	require.Equal(t, http.StatusOK, resp.StatusCode)
	t.Log("dynamic-toggle succeeded — profiling re-enabled without crash")

	// Verify profiles still arrive after re-enabling.
	svcName := serviceName()
	labelSelector := fmt.Sprintf(`{service_name="%s",vehicle="bike"}`, svcName)
	var lastCollapsed string
	var lastErr error
	ok := assert.Eventually(t, func() bool {
		lastCollapsed, lastErr = queryProfile(t, pyroscopeURL, labelSelector)
		if lastErr != nil || lastCollapsed == "" {
			return false
		}
		return frameContains(lastCollapsed, "OrderService", "FindNearestVehicle")
	}, 3*time.Minute, 5*time.Second)

	if !ok {
		t.Logf("label selector: %s", labelSelector)
		t.Logf("last profile query error: %v", lastErr)
		t.Logf("last collapsed profile:\n%s", lastCollapsed)
		t.FailNow()
	}
	t.Logf("profiles still collected after dynamic toggle")
}

// TestRideshareProfilesWithOTEL tests Pyroscope as a notification profiler
// when OTEL .NET auto-instrumentation occupies the classic profiler slot.
// This test is nearly identical to TestRideshareProfiles - the only difference
// is that OTEL=true environment variable is set by the Makefile, which causes
// rideshareImage() and serviceName() to return OTEL variants.
func TestRideshareProfilesWithOTEL(t *testing.T) {
	ctx, cancel := context.WithCancel(context.Background())
	t.Cleanup(cancel)

	net, err := network.New(ctx)
	require.NoError(t, err)
	t.Cleanup(func() { _ = net.Remove(ctx) })

	pyroscopeURL := startPyroscope(ctx, t, net)
	appBaseURL := startApp(ctx, t, net) // Uses OTEL image because OTEL=true
	runLoadGenerator(ctx, t, appBaseURL)

	svcName := serviceName() // Returns "rideshare.dotnet.{flavour}.{version}.app-otel"

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
