use std::ffi::c_char;
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::sync::Arc;

use crate::{BasicAuth, Config, LogLevel, Logger, Profile, Uploader};

pub type PyroscopeLogFn =
    Option<unsafe extern "C" fn(level: i32, message: *const c_char, len: usize)>;

#[repr(C)]
pub struct PyroscopeStr {
    pub data: *const c_char,
    pub len: usize,
}

#[repr(C)]
pub struct PyroscopeLabel {
    pub name: PyroscopeStr,
    pub value: PyroscopeStr,
}

#[repr(C)]
pub struct PyroscopeUploaderConfig {
    pub server_address: PyroscopeStr,
    pub basic_auth_user: PyroscopeStr,
    pub basic_auth_password: PyroscopeStr,
    pub tenant_id: PyroscopeStr,
    pub http_headers_json: PyroscopeStr,
    pub user_agent: PyroscopeStr,
    pub log: PyroscopeLogFn,
}

#[repr(C)]
pub struct PyroscopeProfile {
    pub labels: *const PyroscopeLabel,
    pub labels_len: usize,
    pub pprof: *const u8,
    pub pprof_len: usize,
}

unsafe fn slice<'a, T>(data: *const T, len: usize) -> &'a [T] {
    if data.is_null() || len == 0 {
        &[]
    } else {
        std::slice::from_raw_parts(data, len)
    }
}

impl PyroscopeStr {
    unsafe fn to_owned_string(&self) -> String {
        String::from_utf8_lossy(slice(self.data.cast::<u8>(), self.len)).into_owned()
    }
}

fn logger(log: PyroscopeLogFn) -> Logger {
    Arc::new(move |level: LogLevel, message: &str| {
        if let Some(log) = log {
            unsafe { log(level as i32, message.as_ptr().cast(), message.len()) };
        }
    })
}

#[no_mangle]
pub unsafe extern "C" fn pyroscope_uploader_new(
    config: *const PyroscopeUploaderConfig,
) -> *mut Uploader {
    catch_unwind(AssertUnwindSafe(|| {
        let Some(config) = config.as_ref() else {
            return std::ptr::null_mut();
        };
        let log = logger(config.log);
        let config_ = Config {
            server_address: config.server_address.to_owned_string(),
            basic_auth: BasicAuth {
                user: config.basic_auth_user.to_owned_string(),
                password: config.basic_auth_password.to_owned_string(),
            },
            tenant_id: config.tenant_id.to_owned_string(),
            http_headers_json: config.http_headers_json.to_owned_string(),
            user_agent: config.user_agent.to_owned_string(),
        };
        match Uploader::new(config_, log.clone()) {
            Ok(uploader) => Box::into_raw(Box::new(uploader)),
            Err(e) => {
                log(LogLevel::Error, &e);
                std::ptr::null_mut()
            }
        }
    }))
    .unwrap_or(std::ptr::null_mut())
}

#[no_mangle]
pub unsafe extern "C" fn pyroscope_uploader_export(
    uploader: *const Uploader,
    profiles: *const PyroscopeProfile,
    profiles_len: usize,
) {
    let _ = catch_unwind(AssertUnwindSafe(|| {
        let Some(uploader) = uploader.as_ref() else {
            return;
        };
        let profiles = slice(profiles, profiles_len)
            .iter()
            .map(|p| Profile {
                labels: slice(p.labels, p.labels_len)
                    .iter()
                    .map(|l| (l.name.to_owned_string(), l.value.to_owned_string()))
                    .collect(),
                pprof: slice(p.pprof, p.pprof_len).to_vec(),
            })
            .collect();
        uploader.export(profiles);
    }));
}

#[no_mangle]
pub unsafe extern "C" fn pyroscope_uploader_set_basic_auth(
    uploader: *const Uploader,
    user: PyroscopeStr,
    password: PyroscopeStr,
) {
    let _ = catch_unwind(AssertUnwindSafe(|| {
        let Some(uploader) = uploader.as_ref() else {
            return;
        };
        uploader.set_basic_auth(BasicAuth {
            user: user.to_owned_string(),
            password: password.to_owned_string(),
        });
    }));
}

#[no_mangle]
pub unsafe extern "C" fn pyroscope_uploader_free(uploader: *mut Uploader) {
    if uploader.is_null() {
        return;
    }
    let _ = catch_unwind(AssertUnwindSafe(|| drop(Box::from_raw(uploader))));
}
