package integrationtest

import (
	"context"
	"encoding/base64"
	"fmt"
	"io"
	"net"
	"net/http"
	"runtime"
	"testing"
	"time"

	"pyroscope-dotnet-integration-test/require"
)

const (
	proxyUser     = "proxy-user"
	proxyPassword = "proxy-password"
	// Only the test proxy can resolve this host, so a profile that reaches the
	// receiver must have gone through the proxy.
	hostBehindProxy = "pyroscope.behind-proxy.test"
)

type proxiedRequest struct {
	method string
	target string
}

// startForwardProxy starts an HTTP forward proxy that requires basic auth and
// sends every request for targetHost to upstreamAddr.
func startForwardProxy(t *testing.T, targetHost, upstreamAddr string) (port int, requests <-chan proxiedRequest) {
	t.Helper()

	wantAuth := "Basic " + base64.StdEncoding.EncodeToString([]byte(proxyUser+":"+proxyPassword))
	ch := make(chan proxiedRequest, 100)
	dial := func(ctx context.Context, _, _ string) (net.Conn, error) {
		var d net.Dialer
		return d.DialContext(ctx, "tcp", upstreamAddr)
	}
	transport := &http.Transport{Proxy: nil, DialContext: dial}
	t.Cleanup(transport.CloseIdleConnections)

	handler := http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		t.Logf("proxy: %s %s", r.Method, r.RequestURI)
		if r.Header.Get("Proxy-Authorization") != wantAuth {
			w.Header().Set("Proxy-Authenticate", `Basic realm="test"`)
			w.WriteHeader(http.StatusProxyAuthRequired)
			return
		}
		host, _, err := net.SplitHostPort(r.Host)
		if err != nil {
			host = r.Host
		}
		if host != targetHost {
			http.Error(w, "unexpected target "+r.Host, http.StatusBadGateway)
			return
		}
		select {
		case ch <- proxiedRequest{method: r.Method, target: r.RequestURI}:
		default:
		}

		if r.Method == http.MethodConnect {
			tunnel(t, w, r, dial)
			return
		}

		out := r.Clone(r.Context())
		out.RequestURI = ""
		out.Header.Del("Proxy-Authorization")
		res, err := transport.RoundTrip(out)
		if err != nil {
			http.Error(w, err.Error(), http.StatusBadGateway)
			return
		}
		defer res.Body.Close()
		for k, v := range res.Header {
			w.Header()[k] = v
		}
		w.WriteHeader(res.StatusCode)
		_, _ = io.Copy(w, res.Body)
	})

	ln, err := net.Listen("tcp", "0.0.0.0:0")
	require.NoError(t, err)
	srv := &http.Server{Handler: handler}
	go func() { _ = srv.Serve(ln) }()
	t.Cleanup(func() { _ = srv.Close() })

	return ln.Addr().(*net.TCPAddr).Port, ch
}

func tunnel(t *testing.T, w http.ResponseWriter, r *http.Request, dial func(context.Context, string, string) (net.Conn, error)) {
	upstream, err := dial(r.Context(), "tcp", r.Host)
	if err != nil {
		http.Error(w, err.Error(), http.StatusBadGateway)
		return
	}
	client, buf, err := http.NewResponseController(w).Hijack()
	if err != nil {
		t.Logf("proxy: hijack failed: %v", err)
		_ = upstream.Close()
		return
	}
	_, _ = client.Write([]byte("HTTP/1.1 200 Connection established\r\n\r\n"))
	go func() {
		defer upstream.Close()
		defer client.Close()
		_, _ = io.Copy(upstream, buf.Reader)
	}()
	go func() {
		defer upstream.Close()
		defer client.Close()
		_, _ = io.Copy(client, upstream)
	}()
}

func startPlainProfileReceiver(t *testing.T) (port int, received <-chan struct{}) {
	t.Helper()

	ch := make(chan struct{}, 1)
	handler := http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		t.Logf("profile receiver: %s %s", r.Method, r.URL.Path)
		if r.Header.Get("Proxy-Authorization") != "" {
			t.Errorf("profile receiver: got proxy credentials meant for the proxy")
		}
		select {
		case ch <- struct{}{}:
		default:
		}
		w.WriteHeader(http.StatusOK)
	})

	ln, err := net.Listen("tcp", "0.0.0.0:0")
	require.NoError(t, err)
	srv := &http.Server{Handler: handler}
	go func() { _ = srv.Serve(ln) }()
	t.Cleanup(func() { _ = srv.Close() })

	return ln.Addr().(*net.TCPAddr).Port, ch
}

func testHostname() string {
	if runtime.GOOS == "windows" {
		return "127.0.0.1"
	}
	return "host.docker.internal"
}

func proxyURL(port int) string {
	return fmt.Sprintf("http://%s:%s@%s:%d", proxyUser, proxyPassword, testHostname(), port)
}

func waitForProfile(t *testing.T, received <-chan struct{}) {
	t.Helper()
	select {
	case <-received:
	case <-time.After(3 * time.Minute):
		t.Fatal("timed out waiting for profile upload")
	}
}

func expectProxiedRequest(t *testing.T, requests <-chan proxiedRequest, method string) {
	t.Helper()
	select {
	case req := <-requests:
		if req.method != method {
			t.Fatalf("proxy got %s %s, want %s", req.method, req.target, method)
		}
	default:
		t.Fatalf("proxy got no %s request", method)
	}
}

func runProxyProfileUploadTest(t *testing.T, libcType, version string) {
	t.Run("http", func(t *testing.T) {
		ctx, cancel := context.WithCancel(context.Background())
		t.Cleanup(cancel)

		receiverPort, received := startPlainProfileReceiver(t)
		proxyPort, requests := startForwardProxy(t, hostBehindProxy, fmt.Sprintf("127.0.0.1:%d", receiverPort))

		serverAddress := fmt.Sprintf("http://%s:%d", hostBehindProxy, receiverPort)
		appBaseURL := startAppForTLSTest(t, libcType, version, "proxy-http-test-app", serverAddress, nil, map[string]string{
			"HTTP_PROXY": proxyURL(proxyPort),
		})
		runLoadGenerator(ctx, t, appBaseURL)

		waitForProfile(t, received)
		expectProxiedRequest(t, requests, http.MethodPost)
	})

	t.Run("https", func(t *testing.T) {
		ctx, cancel := context.WithCancel(context.Background())
		t.Cleanup(cancel)

		receiverPort, caCertPEM, received := startTLSProfileReceiver(t, hostBehindProxy)
		proxyPort, requests := startForwardProxy(t, hostBehindProxy, fmt.Sprintf("127.0.0.1:%d", receiverPort))

		serverAddress := fmt.Sprintf("https://%s:%d", hostBehindProxy, receiverPort)
		appBaseURL := startAppForTLSTest(t, libcType, version, "proxy-https-test-app", serverAddress, caCertPEM, map[string]string{
			"HTTPS_PROXY": proxyURL(proxyPort),
		})
		runLoadGenerator(ctx, t, appBaseURL)

		waitForProfile(t, received)
		expectProxiedRequest(t, requests, http.MethodConnect)
	})

	t.Run("no_proxy", func(t *testing.T) {
		ctx, cancel := context.WithCancel(context.Background())
		t.Cleanup(cancel)

		hostname := testHostname()
		receiverPort, caCertPEM, received := startTLSProfileReceiver(t, hostname)
		proxyPort, requests := startForwardProxy(t, hostname, fmt.Sprintf("127.0.0.1:%d", receiverPort))

		serverAddress := fmt.Sprintf("https://%s:%d", hostname, receiverPort)
		appBaseURL := startAppForTLSTest(t, libcType, version, "proxy-bypass-test-app", serverAddress, caCertPEM, map[string]string{
			"HTTPS_PROXY": proxyURL(proxyPort),
			"NO_PROXY":    "example.com," + hostname,
		})
		runLoadGenerator(ctx, t, appBaseURL)

		waitForProfile(t, received)
		select {
		case req := <-requests:
			t.Fatalf("proxy got %s %s, want no requests", req.method, req.target)
		default:
		}
	})
}

func TestProxyProfileUpload(t *testing.T) {
	runProxyProfileUploadTest(t, envLibcType(), envDotnetVersion())
}
