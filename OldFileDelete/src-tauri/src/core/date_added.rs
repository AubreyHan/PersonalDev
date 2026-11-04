use std::fs;
use std::path::Path;
use std::time::SystemTime;

#[cfg(target_os = "macos")]
pub fn get_date_added(path: &Path) -> SystemTime {
    use core_foundation::base::TCFType;
    use core_foundation::date::CFDate;
    use core_foundation::string::CFString;
    use core_foundation::url::CFURL;
    use std::os::raw::c_void;

    let fallback = || {
        fs::metadata(path)
            .and_then(|m| m.created().or_else(|_| m.modified()))
            .unwrap_or_else(|_| SystemTime::now())
    };

    #[link(name = "CoreServices", kind = "framework")]
    extern "C" {
        fn MDItemCreateWithURL(allocator: *const c_void, url: *const c_void) -> *mut c_void;
        fn MDItemCopyAttribute(item: *const c_void, name: *const c_void) -> *mut c_void;
        fn CFRelease(cf: *const c_void);
    }

    let canonical = match path.canonicalize() {
        Ok(p) => p,
        Err(_) => return fallback(),
    };

    let path_str = match canonical.to_str() {
        Some(s) => s,
        None => return fallback(),
    };

    unsafe {
        let url = match CFURL::from_path(Path::new(path_str), canonical.is_dir()) {
            Some(u) => u,
            None => return fallback(),
        };

        let item = MDItemCreateWithURL(std::ptr::null(), url.as_concrete_TypeRef() as *const c_void);
        if item.is_null() {
            return fallback();
        }

        let attr_name = CFString::new("kMDItemDateAdded");
        let date_attr = MDItemCopyAttribute(item, attr_name.as_concrete_TypeRef() as *const c_void);
        CFRelease(item);

        if date_attr.is_null() {
            return fallback();
        }

        let cf_date = CFDate::wrap_under_create_rule(date_attr as _);
        let abs_time = cf_date.abs_time();
        // macOS CFAbsoluteTime epoch: 2001-01-01 00:00:00 UTC = 978307200 UNIX epoch
        let unix_secs = abs_time + 978_307_200.0;
        if unix_secs < 0.0 {
            return fallback();
        }
        SystemTime::UNIX_EPOCH + std::time::Duration::from_secs_f64(unix_secs)
    }
}

#[cfg(not(target_os = "macos"))]
pub fn get_date_added(path: &Path) -> SystemTime {
    fs::metadata(path)
        .and_then(|m| m.created().or_else(|_| m.modified()))
        .unwrap_or_else(|_| SystemTime::now())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_date_added_on_temp_file() {
        let temp_dir = std::env::temp_dir();
        let date = get_date_added(&temp_dir);
        let now = SystemTime::now();
        // Should be a sensible time in the past
        assert!(date <= now + std::time::Duration::from_secs(60));
    }
}
