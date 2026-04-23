package integrationtest

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
	"io"
	"math/big"
	"net"
	"net/http"
	"path/filepath"
	"regexp"
	"runtime"
	"strings"
	"testing"
	"time"

	"pyroscope-dotnet-integration-test/dockertest"
	"pyroscope-dotnet-integration-test/pyroscope/model"

	"connectrpc.com/connect"
	querierv1 "github.com/grafana/pyroscope/api/gen/proto/go/querier/v1"
	"github.com/grafana/pyroscope/api/gen/proto/go/querier/v1/querierv1connect"
	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
)

func startPyroscope(t *testing.T, net *dockertest.Network) string {
	t.Helper()
	c := dockertest.StartContainer(t, dockertest.ContainerRequest{
		Image:          "grafana/pyroscope@sha256:0ad897bb457228c7d1133ce7004f83052e635be9daf4a7cc90364317637e9754",
		ExposedPorts:   []string{"4040/tcp"},
		Network:        net.Name,
		NetworkAliases: []string{"pyroscope"},
		WaitFor:        dockertest.WaitForHTTP("/ready", "4040/tcp", 60*time.Second),
	})
	return fmt.Sprintf("http://%s", c.HostPort(t, "4040/tcp"))
}

func buildAppImage(t *testing.T, libcType, version string, otel bool) string {
	t.Helper()
	tag := fmt.Sprintf("rideshare-app-%s:integration-test-%s", libcType, version)
	if otel {
		tag = fmt.Sprintf("rideshare-app-%s-otel:integration-test-%s", libcType, version)
	}
	dockertest.BuildImage(t, dockertest.BuildRequest{
		Context:    repoRoot(),
		Dockerfile: filepath.Join(repoRoot(), appDockerfile(otel)),
		Platform:   "linux/amd64",
		Tag:        tag,
		BuildArgs: map[string]string{
			"PYROSCOPE_SDK_IMAGE": profilerImageTag(libcType),
			"SDK_VERSION":         version,
			"SDK_IMAGE_SUFFIX":    sdkImageSuffix(libcType, version),
		},
	})
	return tag
}

func startAppWithEnv(t *testing.T, net *dockertest.Network, libcType, version string, otel bool, extraEnv map[string]string) string {
	t.Helper()
	ensureProfilerImage(t, libcType)
	image := buildAppImage(t, libcType, version, otel)

	svcName := rideshareServiceName(libcType, version, otel)
	env := map[string]string{
		"REGION":                     "us-east",
		"PYROSCOPE_APPLICATION_NAME": svcName,
		"PYROSCOPE_SERVER_ADDRESS":   "http://pyroscope:4040",
		"DD_TRACE_DEBUG":             "true",
	}
	for k, v := range extraEnv {
		env[k] = v
	}
	c := dockertest.StartContainer(t, dockertest.ContainerRequest{
		Image:          image,
		Platform:       "linux/amd64",
		Network:        net.Name,
		NetworkAliases: []string{"rideshare"},
		Env:            env,
		ExposedPorts:   []string{"5000/tcp"},
		WaitFor:        dockertest.WaitForHTTP("/healthz", "5000/tcp", 120*time.Second),
	})
	return fmt.Sprintf("http://%s", c.HostPort(t, "5000/tcp"))
}

func startApp(t *testing.T, net *dockertest.Network, libcType, version string, otel bool) string {
	t.Helper()
	return startAppWithEnv(t, net, libcType, version, otel, nil)
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

func queryProfile(t *testing.T, pyroscopeURL string, labelSelector string, profileTypeID string) (string, error) {
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

	net := dockertest.CreateNetwork(t)

	pyroscopeURL := startPyroscope(t, net)
	appBaseURL := startApp(t, net, libcType, version, otel)
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
				lastCollapsed, lastErr = queryProfile(t, pyroscopeURL, labelSelector, "process_cpu:cpu:nanoseconds:cpu:nanoseconds")
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

func TestDynamicCpuToggleDoesNotCrash(t *testing.T) {
	net := dockertest.CreateNetwork(t)

	_ = startPyroscope(t, net)
	appBaseURL := startAppWithEnv(t, net, envLibcType(), envDotnetVersion(), false, map[string]string{
		"PYROSCOPE_PROFILING_ENABLED":            "true",
		"PYROSCOPE_PROFILING_CPU_ENABLED":        "true",
		"PYROSCOPE_PROFILING_WALLTIME_ENABLED":   "true",
		"PYROSCOPE_PROFILING_ALLOCATION_ENABLED": "true",
		"PYROSCOPE_PROFILING_CONTENTION_ENABLED": "true",
		"PYROSCOPE_PROFILING_EXCEPTION_ENABLED":  "true",
	})
	client := &http.Client{Timeout: 30 * time.Second}

	warmupResp, err := client.Get(appBaseURL + "/bike")
	require.NoError(t, err)
	require.Equal(t, http.StatusOK, warmupResp.StatusCode)
	_ = warmupResp.Body.Close()

	disableURL := appBaseURL + "/profiling?cpu=false&allocation=false&contention=false&exception=false"
	enableURL := appBaseURL + "/profiling?cpu=true"

	for i := 0; i < 3; i++ {
		resp, err := client.Post(disableURL, "text/plain", nil)
		require.NoError(t, err)
		body, _ := io.ReadAll(resp.Body)
		_ = resp.Body.Close()
		require.Equal(t, http.StatusOK, resp.StatusCode, "iteration %d body: %s", i, string(body))

		for j := 0; j < 20; j++ {
			loadResp, err := client.Get(appBaseURL + "/car")
			require.NoError(t, err)
			_ = loadResp.Body.Close()
			require.Equal(t, http.StatusOK, loadResp.StatusCode)
		}

		resp, err = client.Post(enableURL, "text/plain", nil)
		require.NoError(t, err)
		body, _ = io.ReadAll(resp.Body)
		_ = resp.Body.Close()
		require.Equal(t, http.StatusOK, resp.StatusCode, "iteration %d body: %s", i, string(body))
	}

	require.Eventually(t, func() bool {
		resp, err := client.Get(appBaseURL + "/car")
		if err != nil {
			return false
		}
		_ = resp.Body.Close()
		return resp.StatusCode == http.StatusOK
	}, 30*time.Second, time.Second, "rideshare app is not reachable after dynamic CPU toggling")
}

func TestDynamicCpuProfilingToggleAffectsProfileData(t *testing.T) {
	net := dockertest.CreateNetwork(t)

	pyroscopeURL := startPyroscope(t, net)
	appName := fmt.Sprintf("rideshare.dynamic-cpu-toggle.%d", time.Now().UnixNano())
	appBaseURL := startAppWithEnv(t, net, envLibcType(), envDotnetVersion(), false, map[string]string{
		"PYROSCOPE_APPLICATION_NAME":             appName,
		"PYROSCOPE_PROFILING_ENABLED":            "true",
		"PYROSCOPE_PROFILING_CPU_ENABLED":        "true",
		"PYROSCOPE_PROFILING_WALLTIME_ENABLED":   "true",
		"PYROSCOPE_PROFILING_ALLOCATION_ENABLED": "true",
		"PYROSCOPE_PROFILING_CONTENTION_ENABLED": "true",
		"PYROSCOPE_PROFILING_EXCEPTION_ENABLED":  "true",
	})
	client := &http.Client{Timeout: 30 * time.Second}

	disableResp, err := client.Post(appBaseURL+"/profiling?cpu=false", "text/plain", nil)
	require.NoError(t, err)
	require.Equal(t, http.StatusOK, disableResp.StatusCode)
	_ = disableResp.Body.Close()

	labelSelector := fmt.Sprintf(`{service_name="%s",vehicle="bike"}`, appName)
	assert.Never(t, func() bool {
		resp, err := client.Get(appBaseURL + "/bike")
		if err != nil {
			t.Logf("bike request failed while CPU disabled: %v", err)
			return false
		}
		_ = resp.Body.Close()
		if resp.StatusCode != http.StatusOK {
			t.Logf("bike request returned status %d while CPU disabled", resp.StatusCode)
			return false
		}

		collapsed, err := queryProfile(t, pyroscopeURL, labelSelector, "process_cpu:cpu:nanoseconds:cpu:nanoseconds")
		if err != nil {
			t.Logf("profile query failed while CPU disabled: %v", err)
			return false
		}
		return collapsed != ""
	}, 45*time.Second, 5*time.Second, "expected no CPU profile data while CPU profiling is disabled")

	enableResp, err := client.Post(appBaseURL+"/profiling?cpu=true", "text/plain", nil)
	require.NoError(t, err)
	require.Equal(t, http.StatusOK, enableResp.StatusCode)
	_ = enableResp.Body.Close()

	var lastCollapsed string
	var lastErr error
	ok := assert.Eventually(t, func() bool {
		resp, err := client.Get(appBaseURL + "/bike")
		if err != nil {
			lastErr = err
			return false
		}
		_ = resp.Body.Close()
		if resp.StatusCode != http.StatusOK {
			lastErr = fmt.Errorf("bike request returned status %d", resp.StatusCode)
			return false
		}

		lastCollapsed, lastErr = queryProfile(t, pyroscopeURL, labelSelector, "process_cpu:cpu:nanoseconds:cpu:nanoseconds")
		if lastErr != nil || lastCollapsed == "" {
			return false
		}
		return frameContains(lastCollapsed, "OrderService", "FindNearestVehicle")
	}, 2*time.Minute, 5*time.Second, "expected CPU profile data after CPU profiling is re-enabled")

	if !ok {
		t.Logf("label selector: %s", labelSelector)
		t.Logf("last profile query error: %v", lastErr)
		t.Logf("last collapsed profile:\n%s", lastCollapsed)
		t.FailNow()
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

// hostDockerInternalExtraHost returns the --add-host entry that makes
// host.docker.internal reachable from inside a container.
//
// On macOS (Docker Desktop / OrbStack) host.docker.internal is configured
// automatically by the runtime, so no extra entry is needed.
// On Linux the hostname is not injected by default; we map it to the Docker
// bridge gateway, which is the address containers use to reach the host.
func hostDockerInternalExtraHost(t *testing.T) string {
	t.Helper()
	if runtime.GOOS != "linux" {
		return ""
	}
	return "host.docker.internal:" + dockertest.BridgeGatewayIP(t)
}

func startAppForTLSTest(t *testing.T, libcType, version, serverAddress string, caCertPEM []byte) string {
	t.Helper()
	ensureProfilerImage(t, libcType)
	image := buildAppImage(t, libcType, version, false)

	extraHost := hostDockerInternalExtraHost(t)
	var extraHosts []string
	if extraHost != "" {
		extraHosts = []string{extraHost}
	}

	c := dockertest.StartContainer(t, dockertest.ContainerRequest{
		Image:    image,
		Platform: "linux/amd64",
		Env: map[string]string{
			"REGION":                     "us-east",
			"PYROSCOPE_APPLICATION_NAME": "tls-test-app",
			"PYROSCOPE_SERVER_ADDRESS":   serverAddress,
			"DD_TRACE_DEBUG":             "true",
		},
		ExtraHosts: extraHosts,
		Files: []dockertest.ContainerFile{
			{
				Reader:        bytes.NewReader(caCertPEM),
				ContainerPath: "/tmp/ca.crt",
			},
		},
		PostStart: [][]string{
			{"sh", "-c", "cat /tmp/ca.crt >> /etc/ssl/cert.pem"},
		},
		ExposedPorts: []string{"5000/tcp"},
		WaitFor:      dockertest.WaitForHTTP("/healthz", "5000/tcp", 120*time.Second),
	})
	return fmt.Sprintf("http://%s", c.HostPort(t, "5000/tcp"))
}

func runTLSProfileUploadTest(t *testing.T, libcType, version string) {
	ctx, cancel := context.WithCancel(context.Background())
	t.Cleanup(cancel)

	const hostname = "host.docker.internal"
	port, caCertPEM, received := startTLSProfileReceiver(t, hostname)
	serverAddress := fmt.Sprintf("https://%s:%d", hostname, port)

	appBaseURL := startAppForTLSTest(t, libcType, version, serverAddress, caCertPEM)
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
