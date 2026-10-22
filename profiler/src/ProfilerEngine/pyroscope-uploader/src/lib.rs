mod ffi;
pub mod proto;

use std::panic::{catch_unwind, AssertUnwindSafe};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::mpsc::{sync_channel, Receiver, SyncSender, TrySendError};
use std::sync::{Arc, Mutex};
use std::thread::JoinHandle;
use std::time::Duration;

use base64::Engine;
use prost::Message;
use reqwest::header::{
    HeaderMap, HeaderName, HeaderValue, AUTHORIZATION, CONTENT_TYPE, USER_AGENT,
};
use url::Url;

use crate::proto::{LabelPair, PushRequest, RawProfileSeries, RawSample};

const QUEUE_CAPACITY: usize = 3;
const CONNECT_TIMEOUT: Duration = Duration::from_secs(10);
const READ_TIMEOUT: Duration = Duration::from_secs(10);

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(i32)]
pub enum LogLevel {
    Debug = 0,
    Info = 1,
    Warn = 2,
    Error = 3,
}

pub type Logger = Arc<dyn Fn(LogLevel, &str) + Send + Sync>;

#[derive(Clone, Default, PartialEq, Eq)]
pub struct BasicAuth {
    pub user: String,
    pub password: String,
}

#[derive(Default)]
pub struct Config {
    pub server_address: String,
    pub basic_auth: BasicAuth,
    pub tenant_id: String,
    pub http_headers_json: String,
    pub user_agent: String,
}

pub struct Profile {
    pub labels: Vec<(String, String)>,
    pub pprof: Vec<u8>,
}

pub struct Uploader {
    shared: Arc<Shared>,
    tx: Option<SyncSender<Vec<Profile>>>,
    worker: Option<JoinHandle<()>>,
}

struct Shared {
    log: Logger,
    running: AtomicBool,
    basic_auth: Mutex<BasicAuth>,
}

struct Worker {
    shared: Arc<Shared>,
    client: reqwest::Client,
    url: Url,
    url_auth: Option<BasicAuth>,
    headers: HeaderMap,
}

impl Uploader {
    pub fn new(config: Config, log: Logger) -> Result<Self, String> {
        let (url, url_auth) = push_url(&config.server_address, &log)?;
        let headers = static_headers(&config, &log);
        let client = build_client(&log)?;

        let shared = Arc::new(Shared {
            log,
            running: AtomicBool::new(true),
            basic_auth: Mutex::new(config.basic_auth),
        });
        let worker = Worker {
            shared: shared.clone(),
            client,
            url,
            url_auth,
            headers,
        };
        let (tx, rx) = sync_channel(QUEUE_CAPACITY);
        let handle = std::thread::Builder::new()
            .name("Pyroscope.Pprof.Uploader".to_owned())
            .spawn(move || worker.run(rx))
            .map_err(|e| format!("failed to start uploader thread: {e}"))?;

        Ok(Uploader {
            shared,
            tx: Some(tx),
            worker: Some(handle),
        })
    }

    pub fn export(&self, profiles: Vec<Profile>) {
        let Some(tx) = &self.tx else { return };
        if let Err(TrySendError::Full(_)) = tx.try_send(profiles) {
            (self.shared.log)(LogLevel::Warn, "queue is too big. Dropping pprof data.");
        }
    }

    pub fn set_basic_auth(&self, basic_auth: BasicAuth) {
        *self
            .shared
            .basic_auth
            .lock()
            .unwrap_or_else(|e| e.into_inner()) = basic_auth;
    }
}

impl Drop for Uploader {
    fn drop(&mut self) {
        self.shared.running.store(false, Ordering::SeqCst);
        drop(self.tx.take());
        if let Some(handle) = self.worker.take() {
            let _ = handle.join();
        }
    }
}

impl Worker {
    fn run(self, rx: Receiver<Vec<Profile>>) {
        let runtime = match tokio::runtime::Builder::new_current_thread()
            .enable_all()
            .build()
        {
            Ok(runtime) => runtime,
            Err(e) => {
                (self.shared.log)(LogLevel::Error, &format!("failed to create runtime: {e}"));
                return;
            }
        };
        while let Ok(profiles) = rx.recv() {
            if !self.shared.running.load(Ordering::SeqCst) {
                break;
            }
            for profile in profiles {
                let upload = AssertUnwindSafe(|| runtime.block_on(self.upload(profile)));
                if catch_unwind(upload).is_err() {
                    (self.shared.log)(LogLevel::Error, "upload panicked");
                }
            }
        }
    }

    async fn upload(&self, profile: Profile) {
        let body = encode_push_request(profile);
        let mut headers = HeaderMap::new();
        if let Some(authorization) = self.authorization() {
            headers.insert(AUTHORIZATION, authorization);
        }
        headers.extend(self.headers.clone());

        let res = self
            .client
            .post(self.url.clone())
            .headers(headers)
            .body(body)
            .send()
            .await;
        match res {
            Ok(res) if res.status().is_success() => {
                (self.shared.log)(LogLevel::Debug, &res.status().as_u16().to_string());
            }
            Ok(res) => {
                let status = res.status().as_u16();
                let text = res.text().await.unwrap_or_default();
                (self.shared.log)(
                    LogLevel::Error,
                    &format!("err status {status} {} {text}", self.url),
                );
            }
            Err(e) => {
                (self.shared.log)(
                    LogLevel::Error,
                    &format!("err {} {}", error_chain(&e.without_url()), self.url),
                );
            }
        }
    }

    fn authorization(&self) -> Option<HeaderValue> {
        let explicit = self
            .shared
            .basic_auth
            .lock()
            .unwrap_or_else(|e| e.into_inner())
            .clone();
        let auth = if !explicit.user.is_empty() && !explicit.password.is_empty() {
            explicit
        } else {
            self.url_auth.clone()?
        };
        let encoded = base64::engine::general_purpose::STANDARD
            .encode(format!("{}:{}", auth.user, auth.password));
        let mut value = HeaderValue::from_str(&format!("Basic {encoded}")).ok()?;
        value.set_sensitive(true);
        Some(value)
    }
}

fn build_client(log: &Logger) -> Result<reqwest::Client, String> {
    let mut builder = reqwest::Client::builder()
        .connect_timeout(CONNECT_TIMEOUT)
        .read_timeout(READ_TIMEOUT);
    if let Some(path) = std::env::var_os("SSL_CERT_FILE").filter(|p| !p.is_empty()) {
        match std::fs::read(&path)
            .map_err(|e| e.to_string())
            .and_then(|pem| reqwest::Certificate::from_pem_bundle(&pem).map_err(|e| e.to_string()))
        {
            Ok(certs) => builder = builder.tls_certs_merge(certs),
            Err(e) => log(
                LogLevel::Error,
                &format!(
                    "failed to load SSL_CERT_FILE {}: {e}",
                    path.to_string_lossy()
                ),
            ),
        }
    }
    builder
        .build()
        .map_err(|e| format!("failed to create http client: {}", error_chain(&e)))
}

fn push_url(server_address: &str, log: &Logger) -> Result<(Url, Option<BasicAuth>), String> {
    let address = if server_address.contains("://") {
        server_address.to_owned()
    } else {
        log(LogLevel::Error, "url scheme empty, falling back to http");
        format!("http://{server_address}")
    };
    let mut url = Url::parse(&address).map_err(|e| format!("invalid server address: {e}"))?;
    if url.host_str().is_none_or(str::is_empty) {
        return Err("invalid server address: empty host".to_owned());
    }

    let url_auth = (!url.username().is_empty() || url.password().is_some()).then(|| BasicAuth {
        user: percent_decode(url.username()),
        password: percent_decode(url.password().unwrap_or_default()),
    });
    let _ = url.set_username("");
    let _ = url.set_password(None);
    url.set_query(None);
    url.set_fragment(None);
    url.path_segments_mut()
        .map_err(|_| "invalid server address: url cannot be a base".to_owned())?
        .pop_if_empty()
        .extend(["push.v1.PusherService", "Push"]);
    Ok((url, url_auth))
}

fn percent_decode(s: &str) -> String {
    percent_encoding::percent_decode_str(s)
        .decode_utf8_lossy()
        .into_owned()
}

fn static_headers(config: &Config, log: &Logger) -> HeaderMap {
    let mut headers = HeaderMap::new();
    headers.insert(CONTENT_TYPE, HeaderValue::from_static("application/proto"));
    match HeaderValue::from_str(&config.user_agent) {
        Ok(v) => {
            headers.insert(USER_AGENT, v);
        }
        Err(_) => log(LogLevel::Error, "invalid user agent"),
    }
    if !config.tenant_id.is_empty() {
        match HeaderValue::from_str(&config.tenant_id) {
            Ok(v) => {
                headers.insert("X-Scope-OrgID", v);
            }
            Err(_) => log(LogLevel::Error, "invalid tenant id"),
        }
    }
    for (name, value) in parse_headers_json(&config.http_headers_json, log) {
        match (
            HeaderName::from_bytes(name.as_bytes()),
            HeaderValue::from_str(&value),
        ) {
            (Ok(name), Ok(mut value)) => {
                value.set_sensitive(true);
                headers.insert(name, value);
            }
            _ => log(LogLevel::Error, &format!("invalid header: {name}")),
        }
    }
    headers
}

pub fn parse_headers_json(json: &str, log: &Logger) -> Vec<(String, String)> {
    if json.is_empty() {
        return Vec::new();
    }
    let object = match serde_json::from_str::<serde_json::Value>(json) {
        Ok(serde_json::Value::Object(object)) => object,
        Ok(_) => {
            log(
                LogLevel::Error,
                "failed to parse headers json: not an object",
            );
            return Vec::new();
        }
        Err(e) => {
            log(
                LogLevel::Error,
                &format!("failed to parse headers json: {e}"),
            );
            return Vec::new();
        }
    };
    let mut headers = Vec::with_capacity(object.len());
    for (key, value) in object {
        match value {
            serde_json::Value::String(value) => headers.push((key, value)),
            _ => log(
                LogLevel::Error,
                &format!("header value is not a string: {key}"),
            ),
        }
    }
    headers
}

fn encode_push_request(profile: Profile) -> Vec<u8> {
    let labels = profile
        .labels
        .into_iter()
        .map(|(name, value)| LabelPair { name, value })
        .collect();
    PushRequest {
        series: vec![RawProfileSeries {
            labels,
            samples: vec![RawSample {
                raw_profile: profile.pprof,
                id: String::new(),
            }],
        }],
    }
    .encode_to_vec()
}

fn error_chain(e: &dyn std::error::Error) -> String {
    let mut s = e.to_string();
    let mut source = e.source();
    while let Some(cause) = source {
        s.push_str(": ");
        s.push_str(&cause.to_string());
        source = cause.source();
    }
    s
}
