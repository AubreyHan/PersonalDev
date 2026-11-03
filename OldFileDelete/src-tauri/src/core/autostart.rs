#![allow(unexpected_cfgs)]

#[cfg(target_os = "macos")]
pub fn set_macos_autostart(enable: bool) -> Result<bool, String> {
    use objc::runtime::Object;
    use objc::{class, msg_send, sel, sel_impl};

    unsafe {
        let cls = class!(SMAppService);
        let service: *mut Object = msg_send![cls, mainAppService];
        if !service.is_null() {
            let mut err: *mut Object = std::ptr::null_mut();
            if enable {
                let _: bool = msg_send![service, registerAndReturnError: &mut err];
            } else {
                let _: bool = msg_send![service, unregisterAndReturnError: &mut err];
            }
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
    use objc::runtime::Object;
    use objc::{class, msg_send, sel, sel_impl};

    unsafe {
        let cls = class!(SMAppService);
        let service: *mut Object = msg_send![cls, mainAppService];
        if service.is_null() {
            return false;
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
