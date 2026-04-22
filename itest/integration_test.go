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
	"regexp"
	"runtime"
	"strings"
	"testing"
	"time"

	"pyroscope-dotnet-itest/pyroscope/model"

	"connectrpc.com/connect"
	dockertypes "github.com/docker/docker/api/types"
	dockercontainer "github.com/docker/docker/api/types/container"
	dockernetwork "github.com/docker/docker/api/types/network"
	dockerclient "github.com/docker/docker/client"
	querierv1 "github.com/grafana/pyroscope/api/gen/proto/go/querier/v1"
	"github.com/grafana/pyroscope/api/gen/proto/go/querier/v1/querierv1connect"
	typesv1 "github.com/grafana/pyroscope/api/gen/proto/go/types/v1"
	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
	"github.com/testcontainers/testcontainers-go"
	"github.com/testcontainers/testcontainers-go/network"
	"github.com/testcontainers/testcontainers-go/wait"
)

func strPtr(s string) *string { return &s }

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

func startApp(ctx context.Context, t *testing.T, net *testcontainers.DockerNetwork, libcType, version string, otel bool) string {
	t.Helper()
	ensureProfilerImage(t, libcType)

	svcName := rideshareServiceName(libcType, version, otel)
	c, err := testcontainers.GenericContainer(ctx, testcontainers.GenericContainerRequest{
		ContainerRequest: testcontainers.ContainerRequest{
			FromDockerfile: testcontainers.FromDockerfile{
				Context:       repoRoot(),
				Dockerfile:    appDockerfile(otel),
				PrintBuildLog: true,
				BuildArgs: map[string]*string{
					"PYROSCOPE_SDK_IMAGE": strPtr(profilerImageTag(libcType)),
					"SDK_VERSION":         strPtr(version),
					"SDK_IMAGE_SUFFIX":    strPtr(sdkImageSuffix(libcType, version)),
				},
				BuildOptionsModifier: func(opts *dockertypes.ImageBuildOptions) {
					opts.Platform = "linux/amd64"
				},
			},
			Networks: []string{net.Name},
			NetworkAliases: map[string][]string{
				net.Name: {"rideshare"},
			},
			Env: map[string]string{
				"REGION":                     "us-east",
				"PYROSCOPE_APPLICATION_NAME": svcName,
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
	return queryProfileForType(t, pyroscopeURL, "process_cpu:cpu:nanoseconds:cpu:nanoseconds", labelSelector)
}

func queryProfileForType(t *testing.T, pyroscopeURL, profileTypeID, labelSelector string) (string, error) {
	t.Helper()
	qc := querierv1connect.NewQuerierServiceClient(http.DefaultClient, pyroscopeURL)

	to := time.Now()
	from := to.Add(-1 * time.Hour)
	maxNodes := int64(65536)
	resp, err := qc.SelectMergeStacktraces(context.Background(),
		connect.NewRequest(&querierv1.SelectMergeStacktracesRequest{
			ProfileTypeID: profileTypeID,
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

func queryLabelValues(t *testing.T, pyroscopeURL, profileTypeID, labelName string) ([]string, error) {
	t.Helper()
	qc := querierv1connect.NewQuerierServiceClient(http.DefaultClient, pyroscopeURL)

	to := time.Now()
	from := to.Add(-1 * time.Hour)
	resp, err := qc.LabelValues(context.Background(),
		connect.NewRequest(&typesv1.LabelValuesRequest{
			Name:     labelName,
			Matchers: []string{fmt.Sprintf(`{__profile_type__="%s"}`, profileTypeID)},
			Start:    from.UnixMilli(),
			End:      to.UnixMilli(),
		}))
	if err != nil {
		return nil, err
	}
	return resp.Msg.GetNames(), nil
}

// collapsedContainsFrame checks whether any frame in the collapsed stack output
// exactly matches the given name. Unlike frameContains, this does not expect a
// class.method pattern — it matches the full frame string, which is useful for
// synthetic leaf frames like type names (e.g. "System.String").
func collapsedContainsFrame(collapsed, frameName string) bool {
	for _, line := range strings.Split(collapsed, "\n") {
		stack := strings.SplitN(line, " ", 2)[0]
		for _, frame := range strings.Split(stack, ";") {
			if frame == frameName {
				return true
			}
		}
	}
	return false
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

func runRideshareProfileTest(t *testing.T, libcType, version string, otel bool) {
	ctx, cancel := context.WithCancel(context.Background())
	t.Cleanup(cancel)

	net, err := network.New(ctx)
	require.NoError(t, err)
	t.Cleanup(func() { _ = net.Remove(ctx) })

	pyroscopeURL := startPyroscope(ctx, t, net)
	appBaseURL := startApp(ctx, t, net, libcType, version, otel)
	runLoadGenerator(ctx, t, appBaseURL)

	svcName := rideshareServiceName(libcType, version, otel)

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

func TestRideshareProfiles(t *testing.T) {
	runRideshareProfileTest(t, envLibcType(), envDotnetVersion(), false)
}

func TestRideshareProfilesWithOTEL(t *testing.T) {
	runRideshareProfileTest(t, envLibcType(), envDotnetVersion(), true)
}

func startAppWithAllocTypeConfig(ctx context.Context, t *testing.T, net *testcontainers.DockerNetwork, libcType, version string) (appBaseURL, svcName string) {
	t.Helper()
	ensureProfilerImage(t, libcType)

	svcName = fmt.Sprintf("rideshare.dotnet.%s.%s.alloc-type", libcType, version)
	c, err := testcontainers.GenericContainer(ctx, testcontainers.GenericContainerRequest{
		ContainerRequest: testcontainers.ContainerRequest{
			FromDockerfile: testcontainers.FromDockerfile{
				Context:       repoRoot(),
				Dockerfile:    appDockerfile(false),
				PrintBuildLog: true,
				BuildArgs: map[string]*string{
					"PYROSCOPE_SDK_IMAGE": strPtr(profilerImageTag(libcType)),
					"SDK_VERSION":         strPtr(version),
					"SDK_IMAGE_SUFFIX":    strPtr(sdkImageSuffix(libcType, version)),
				},
				BuildOptionsModifier: func(opts *dockertypes.ImageBuildOptions) {
					opts.Platform = "linux/amd64"
				},
			},
			Networks: []string{net.Name},
			NetworkAliases: map[string][]string{
				net.Name: {"rideshare"},
			},
			Env: map[string]string{
				"REGION":                                  "us-east",
				"PYROSCOPE_APPLICATION_NAME":              svcName,
				"PYROSCOPE_SERVER_ADDRESS":                "http://pyroscope:4040",
				"DD_TRACE_DEBUG":                          "true",
				"PYROSCOPE_ALLOCATION_TYPE_LEAF_ENABLED":  "true",
				"PYROSCOPE_ALLOCATION_TYPE_LABEL_ENABLED": "true",
				"PYROSCOPE_HEAP_TYPE_LEAF_ENABLED":        "true",
				"PYROSCOPE_HEAP_TYPE_LABEL_ENABLED":       "true",
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
	return fmt.Sprintf("http://%s:%s", host, port.Port()), svcName
}

func TestAllocatedTypeConfig(t *testing.T) {
	libcType := envLibcType()
	version := envDotnetVersion()

	ctx, cancel := context.WithCancel(context.Background())
	t.Cleanup(cancel)

	net, err := network.New(ctx)
	require.NoError(t, err)
	t.Cleanup(func() { _ = net.Remove(ctx) })

	pyroscopeURL := startPyroscope(ctx, t, net)
	appBaseURL, svcName := startAppWithAllocTypeConfig(ctx, t, net, libcType, version)
	runLoadGenerator(ctx, t, appBaseURL)

	allocProfileType := "alloc:alloc_samples:count:cpu:nanoseconds"
	heapProfileType := "heap:inuse_objects:count:cpu:nanoseconds"
	labelSelector := fmt.Sprintf(`{service_name="%s"}`, svcName)

	t.Run("alloc_type_leaf", func(t *testing.T) {
		var lastCollapsed string
		var lastErr error
		ok := assert.Eventually(t, func() bool {
			lastCollapsed, lastErr = queryProfileForType(t, pyroscopeURL, allocProfileType, labelSelector)
			if lastErr != nil || lastCollapsed == "" {
				return false
			}
			return collapsedContainsFrame(lastCollapsed, "System.String")
		}, 3*time.Minute, 5*time.Second)

		if !ok {
			t.Logf("alloc_type_leaf: last error: %v", lastErr)
			t.Logf("alloc_type_leaf: last collapsed:\n%s", lastCollapsed)
			t.FailNow()
		}
		t.Logf("alloc_type_leaf: collapsed profile:\n%s", lastCollapsed)
	})

	t.Run("alloc_type_label", func(t *testing.T) {
		var lastValues []string
		var lastErr error
		ok := assert.Eventually(t, func() bool {
			lastValues, lastErr = queryLabelValues(t, pyroscopeURL, allocProfileType, "allocation class")
			if lastErr != nil || len(lastValues) == 0 {
				return false
			}
			return true
		}, 3*time.Minute, 5*time.Second)

		if !ok {
			t.Logf("alloc_type_label: last error: %v", lastErr)
			t.Logf("alloc_type_label: last label values: %v", lastValues)
			t.FailNow()
		}
		t.Logf("alloc_type_label: label values: %v", lastValues)
	})

	t.Run("heap_type_leaf", func(t *testing.T) {
		var lastCollapsed string
		var lastErr error
		ok := assert.Eventually(t, func() bool {
			lastCollapsed, lastErr = queryProfileForType(t, pyroscopeURL, heapProfileType, labelSelector)
			if lastErr != nil || lastCollapsed == "" {
				return false
			}
			return collapsedContainsFrame(lastCollapsed, "System.String")
		}, 3*time.Minute, 5*time.Second)

		if !ok {
			t.Logf("heap_type_leaf: last error: %v", lastErr)
			t.Logf("heap_type_leaf: last collapsed:\n%s", lastCollapsed)
			t.FailNow()
		}
		t.Logf("heap_type_leaf: collapsed profile:\n%s", lastCollapsed)
	})

	t.Run("heap_type_label", func(t *testing.T) {
		var lastValues []string
		var lastErr error
		ok := assert.Eventually(t, func() bool {
			lastValues, lastErr = queryLabelValues(t, pyroscopeURL, heapProfileType, "allocation class")
			if lastErr != nil || len(lastValues) == 0 {
				return false
			}
			return true
		}, 3*time.Minute, 5*time.Second)

		if !ok {
			t.Logf("heap_type_label: last error: %v", lastErr)
			t.Logf("heap_type_label: last label values: %v", lastValues)
			t.FailNow()
		}
		t.Logf("heap_type_label: label values: %v", lastValues)
	})
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

func hostDockerInternalExtraHost(ctx context.Context, t *testing.T) string {
	t.Helper()
	if runtime.GOOS != "linux" {
		return ""
	}
	return "host.docker.internal:" + dockerBridgeGatewayIP(ctx, t)
}

func startAppForTLSTest(ctx context.Context, t *testing.T, libcType, version, serverAddress string, caCertPEM []byte) string {
	t.Helper()
	ensureProfilerImage(t, libcType)

	extraHost := hostDockerInternalExtraHost(ctx, t)

	c, err := testcontainers.GenericContainer(ctx, testcontainers.GenericContainerRequest{
		ContainerRequest: testcontainers.ContainerRequest{
			FromDockerfile: testcontainers.FromDockerfile{
				Context:       repoRoot(),
				Dockerfile:    appDockerfile(false),
				PrintBuildLog: true,
				BuildArgs: map[string]*string{
					"PYROSCOPE_SDK_IMAGE": strPtr(profilerImageTag(libcType)),
					"SDK_VERSION":         strPtr(version),
					"SDK_IMAGE_SUFFIX":    strPtr(sdkImageSuffix(libcType, version)),
				},
				BuildOptionsModifier: func(opts *dockertypes.ImageBuildOptions) {
					opts.Platform = "linux/amd64"
				},
			},
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

func runTLSProfileUploadTest(t *testing.T, libcType, version string) {
	ctx, cancel := context.WithCancel(context.Background())
	t.Cleanup(cancel)

	const hostname = "host.docker.internal"
	port, caCertPEM, received := startTLSProfileReceiver(t, hostname)
	serverAddress := fmt.Sprintf("https://%s:%d", hostname, port)

	appBaseURL := startAppForTLSTest(ctx, t, libcType, version, serverAddress, caCertPEM)
	runLoadGenerator(ctx, t, appBaseURL)

	select {
	case <-received:
		t.Logf("profile received over TLS (%s, .NET %s)", libcType, version)
	case <-time.After(3 * time.Minute):
		t.Fatalf("timed out waiting for TLS profile upload (%s, .NET %s)", libcType, version)
	}
}

func TestTLSProfileUpload(t *testing.T) {
	runTLSProfileUploadTest(t, envLibcType(), envDotnetVersion())
}
