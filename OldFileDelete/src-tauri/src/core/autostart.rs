#[cfg(target_os = "macos")]
pub fn set_macos_autostart(enable: bool) -> Result<bool, String> {
    use objc::{class, msg_send, sel, sel_impl};
    use std::os::raw::c_void;

    unsafe {
        let cls = class!(SMAppService);
        if cls.as_ptr().is_null() {
            return Ok(enable);
        }

        let service: *mut c_void = msg_send![cls, mainAppService];
        if service.is_null() {
            return Ok(enable);
        }

        let mut err: *mut c_void = std::ptr::null_mut();
        if enable {
            let _: bool = msg_send![service, registerAndReturnError: &mut err];
        } else {
            let _: bool = msg_send![service, unregisterAndReturnError: &mut err];
        }
    }
    Ok(enable)
}

#[cfg(not(target_os = "macos"))]
pub fn set_macos_autostart(enable: bool) -> Result<bool, String> {
    Ok(enable)
}

#[cfg(target_os = "macos")]
pub fn get_macos_autostart_status() -> bool {
    use objc::{class, msg_send, sel, sel_impl};
    use std::os::raw::c_void;

    unsafe {
        let cls = class!(SMAppService);
        if cls.as_ptr().is_null() {
            return true;
        }

        let service: *mut c_void = msg_send![cls, mainAppService];
        if service.is_null() {
            return true;
        }

        // SMAppServiceStatusEnabled is 1
        let status: i64 = msg_send![service, status];
        status == 1
    }
}

#[cfg(not(target_os = "macos"))]
pub fn get_macos_autostart_status() -> bool {
    true
}
