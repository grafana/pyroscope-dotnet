use std::collections::HashMap;
use std::io::{BufRead, BufReader, Read, Write};
use std::net::TcpListener;
use std::sync::mpsc::{channel, Receiver};
use std::sync::{Arc, Mutex, MutexGuard};
use std::time::Duration;

use base64::Engine;
use prost::Message;
use pyroscope_uploader::proto::PushRequest;
use pyroscope_uploader::{
    parse_headers_json, BasicAuth, Config, LogLevel, Logger, Profile, Uploader,
};

static ENV_LOCK: Mutex<()> = Mutex::new(());

struct Request {
    target: String,
    headers: HashMap<String, String>,
    body: Vec<u8>,
}

fn lock_env() -> MutexGuard<'static, ()> {
    let guard = ENV_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    for name in [
        "HTTP_PROXY",
        "http_proxy",
        "HTTPS_PROXY",
        "https_proxy",
        "ALL_PROXY",
        "all_proxy",
        "NO_PROXY",
        "no_proxy",
    ] {
        std::env::remove_var(name);
    }
    guard
}

fn serve() -> (u16, Receiver<Request>) {
    let listener = TcpListener::bind("127.0.0.1:0").unwrap();
    let port = listener.local_addr().unwrap().port();
    let (tx, rx) = channel();
    std::thread::spawn(move || {
        for stream in listener.incoming() {
            let Ok(mut stream) = stream else { return };
            let mut reader = BufReader::new(stream.try_clone().unwrap());
            let mut line = String::new();
            reader.read_line(&mut line).unwrap();
            let target = line
                .split_whitespace()
                .nth(1)
                .unwrap_or_default()
                .to_owned();
            let mut headers = HashMap::new();
            loop {
                let mut line = String::new();
                reader.read_line(&mut line).unwrap();
                let line = line.trim_end();
                if line.is_empty() {
                    break;
                }
                let (name, value) = line.split_once(':').unwrap();
                headers
                    .entry(name.trim().to_ascii_lowercase())
                    .and_modify(|v: &mut String| *v = format!("{v}, {}", value.trim()))
                    .or_insert_with(|| value.trim().to_owned());
            }
            let len = headers
                .get("content-length")
                .map_or(0, |v| v.parse().unwrap());
            let mut body = vec![0; len];
            reader.read_exact(&mut body).unwrap();
            stream
                .write_all(b"HTTP/1.1 200 OK\r\nContent-Length: 0\r\nConnection: close\r\n\r\n")
                .unwrap();
            let _ = tx.send(Request {
                target,
                headers,
                body,
            });
        }
    });
    (port, rx)
}

fn logger() -> Logger {
    Arc::new(|level: LogLevel, message: &str| eprintln!("{level:?} {message}"))
}

fn upload(config: Config, rx: &Receiver<Request>) -> Request {
    let uploader = Uploader::new(config, logger()).unwrap();
    uploader.export(vec![Profile {
        labels: vec![("__name__".to_owned(), "process_cpu".to_owned())],
        pprof: b"fake-pprof".to_vec(),
    }]);
    rx.recv_timeout(Duration::from_secs(5))
        .expect("no request received")
}

fn config(server_address: String) -> Config {
    Config {
        server_address,
        user_agent: "pyroscope-dotnet/test".to_owned(),
        ..Default::default()
    }
}

fn basic(user: &str, password: &str) -> String {
    format!(
        "Basic {}",
        base64::engine::general_purpose::STANDARD.encode(format!("{user}:{password}"))
    )
}

fn assert_push_path(server_path: &str, expected: &str) {
    let _env = lock_env();
    let (port, rx) = serve();
    let req = upload(config(format!("http://127.0.0.1:{port}{server_path}")), &rx);
    assert_eq!(req.target, expected);
}

#[test]
fn push_path_without_base_path() {
    assert_push_path("", "/push.v1.PusherService/Push");
}

#[test]
fn push_path_with_trailing_slash() {
    assert_push_path("/tenant/", "/tenant/push.v1.PusherService/Push");
}

#[test]
fn push_path_with_dot_segments() {
    assert_push_path(
        "/tenant/./profiles/../",
        "/tenant/push.v1.PusherService/Push",
    );
}

#[test]
fn push_path_nested() {
    assert_push_path("/api/profiles/", "/api/profiles/push.v1.PusherService/Push");
}

#[test]
fn sends_push_request_body() {
    let _env = lock_env();
    let (port, rx) = serve();
    let req = upload(config(format!("http://127.0.0.1:{port}")), &rx);
    assert_eq!(req.headers["content-type"], "application/proto");
    assert_eq!(req.headers["user-agent"], "pyroscope-dotnet/test");
    let push = PushRequest::decode(req.body.as_slice()).unwrap();
    assert_eq!(push.series.len(), 1);
    let series = &push.series[0];
    assert_eq!(series.labels.len(), 1);
    assert_eq!(series.labels[0].name, "__name__");
    assert_eq!(series.labels[0].value, "process_cpu");
    assert_eq!(series.samples.len(), 1);
    assert_eq!(series.samples[0].raw_profile, b"fake-pprof");
}

#[test]
fn uses_basic_auth_and_tenant() {
    let _env = lock_env();
    let (port, rx) = serve();
    let mut config = config(format!("http://127.0.0.1:{port}/tenant/"));
    config.basic_auth = BasicAuth {
        user: "pyroscope-user".to_owned(),
        password: "pyroscope-password".to_owned(),
    };
    config.tenant_id = "pyroscope-tenant".to_owned();
    let req = upload(config, &rx);
    assert_eq!(
        req.headers["authorization"],
        basic("pyroscope-user", "pyroscope-password")
    );
    assert_eq!(req.headers["x-scope-orgid"], "pyroscope-tenant");
}

#[test]
fn uses_url_credentials_as_basic_auth() {
    let _env = lock_env();
    let (port, rx) = serve();
    let req = upload(
        config(format!("http://us%40er:p%3Ass@127.0.0.1:{port}")),
        &rx,
    );
    assert_eq!(req.headers["authorization"], basic("us@er", "p:ss"));
}

#[test]
fn set_basic_auth_overrides_url_credentials() {
    let _env = lock_env();
    let (port, rx) = serve();
    let uploader = Uploader::new(
        config(format!("http://user:pass@127.0.0.1:{port}")),
        logger(),
    )
    .unwrap();
    uploader.set_basic_auth(BasicAuth {
        user: "new-user".to_owned(),
        password: "new-password".to_owned(),
    });
    uploader.export(vec![Profile {
        labels: vec![],
        pprof: vec![],
    }]);
    let req = rx.recv_timeout(Duration::from_secs(5)).unwrap();
    assert_eq!(
        req.headers["authorization"],
        basic("new-user", "new-password")
    );
}

#[test]
fn extra_headers_override_defaults() {
    let _env = lock_env();
    let (port, rx) = serve();
    let mut config = config(format!("http://user:pass@127.0.0.1:{port}"));
    config.http_headers_json = r#"{"Authorization":"Bearer token","X-Extra":"value"}"#.to_owned();
    let req = upload(config, &rx);
    assert_eq!(req.headers["authorization"], "Bearer token");
    assert_eq!(req.headers["x-extra"], "value");
}

#[test]
fn uses_http_proxy_from_environment() {
    let _env = lock_env();
    let (port, rx) = serve();
    std::env::set_var(
        "HTTP_PROXY",
        format!("http://proxy-user:proxy-password@127.0.0.1:{port}"),
    );
    let req = upload(config("http://pyroscope.invalid:4040/base".to_owned()), &rx);
    assert_eq!(
        req.target,
        "http://pyroscope.invalid:4040/base/push.v1.PusherService/Push"
    );
    assert_eq!(
        req.headers["proxy-authorization"],
        basic("proxy-user", "proxy-password")
    );
    assert!(!req.headers.contains_key("authorization"));
}

#[test]
fn no_proxy_bypasses_proxy() {
    let _env = lock_env();
    let (port, rx) = serve();
    let dead = TcpListener::bind("127.0.0.1:0").unwrap();
    std::env::set_var(
        "HTTP_PROXY",
        format!("http://127.0.0.1:{}", dead.local_addr().unwrap().port()),
    );
    drop(dead);
    std::env::set_var("NO_PROXY", "127.0.0.1");
    let req = upload(config(format!("http://127.0.0.1:{port}")), &rx);
    assert_eq!(req.target, "/push.v1.PusherService/Push");
}

#[test]
fn missing_scheme_falls_back_to_http() {
    let _env = lock_env();
    let (port, rx) = serve();
    let req = upload(config(format!("127.0.0.1:{port}")), &rx);
    assert_eq!(req.target, "/push.v1.PusherService/Push");
}

#[test]
fn invalid_server_address_fails() {
    assert!(Uploader::new(config("http://".to_owned()), logger()).is_err());
}

#[test]
fn parse_headers_json_returns_string_headers() {
    let headers = parse_headers_json(
        r#"{"Authorization":"Bearer token","X-Scope-OrgID":"tenant"}"#,
        &logger(),
    );
    let headers: HashMap<_, _> = headers.into_iter().collect();
    assert_eq!(headers.len(), 2);
    assert_eq!(headers["Authorization"], "Bearer token");
    assert_eq!(headers["X-Scope-OrgID"], "tenant");
}

#[test]
fn parse_headers_json_ignores_non_string_values() {
    let headers = parse_headers_json(
        r#"{"valid":"value","number":42,"boolean":true,"null":null,"object":{},"array":[]}"#,
        &logger(),
    );
    assert_eq!(headers, vec![("valid".to_owned(), "value".to_owned())]);
}

#[test]
fn parse_headers_json_returns_empty_for_invalid_input() {
    assert!(parse_headers_json("", &logger()).is_empty());
    assert!(parse_headers_json("{invalid", &logger()).is_empty());
    assert!(parse_headers_json(r#"["not","an","object"]"#, &logger()).is_empty());
}

#[test]
fn uses_https_proxy_tunnel_from_environment() {
    let _env = lock_env();
    let (port, rx) = serve();
    std::env::set_var(
        "HTTPS_PROXY",
        format!("http://proxy-user:proxy-password@127.0.0.1:{port}"),
    );
    let uploader = Uploader::new(config("https://pyroscope.invalid".to_owned()), logger()).unwrap();
    uploader.export(vec![Profile {
        labels: vec![],
        pprof: vec![],
    }]);
    let req = rx.recv_timeout(Duration::from_secs(5)).unwrap();
    assert_eq!(req.target, "pyroscope.invalid:443");
    assert_eq!(
        req.headers["proxy-authorization"],
        basic("proxy-user", "proxy-password")
    );
}
