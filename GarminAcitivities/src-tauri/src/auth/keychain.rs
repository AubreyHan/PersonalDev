use std::collections::HashMap;
use std::fs;
use std::path::PathBuf;
use thiserror::Error;

#[cfg(unix)]
use std::os::unix::fs::PermissionsExt;

pub const SERVICE_NAME: &str = "com.garmin.activity.exporter";

#[derive(Error, Debug)]
pub enum KeychainError {
    #[error("IO error: {0}")]
    Io(#[from] std::io::Error),
    #[error("Serialization error: {0}")]
    Json(#[from] serde_json::Error),
}

fn get_storage_path() -> PathBuf {
    let base_dir = dirs::data_dir()
        .unwrap_or_else(|| dirs::home_dir().unwrap_or_else(|| PathBuf::from(".")))
        .join("com.garmin.activity.exporter");
    let _ = fs::create_dir_all(&base_dir);
    base_dir.join("credentials.json")
}

fn read_all_credentials() -> HashMap<String, String> {
    let path = get_storage_path();
    if let Ok(content) = fs::read_to_string(path) {
        serde_json::from_str(&content).unwrap_or_default()
    } else {
        HashMap::new()
    }
}

fn write_all_credentials(map: &HashMap<String, String>) -> Result<(), KeychainError> {
    let path = get_storage_path();
    let json = serde_json::to_string_pretty(map)?;
    fs::write(&path, json)?;

    #[cfg(unix)]
    {
        if let Ok(metadata) = fs::metadata(&path) {
            let mut permissions = metadata.permissions();
            permissions.set_mode(0o600); // 严格限制仅当前用户可读写 (rw-------)
            let _ = fs::set_permissions(&path, permissions);
        }
    }

    Ok(())
}

/// 保存凭据至本地隔离受保护存储中（避免 macOS Keychain 频繁触发未签名可执行文件的系统授权弹窗）
pub fn save_credential(account: &str, secret: &str) -> Result<(), KeychainError> {
    let mut map = read_all_credentials();
    map.insert(account.to_string(), secret.to_string());
    write_all_credentials(&map)
}

/// 从本地受保护存储读取凭据
pub fn get_credential(account: &str) -> Result<Option<String>, KeychainError> {
    let map = read_all_credentials();
    Ok(map.get(account).cloned())
}

/// 从本地受保护存储删除凭据
pub fn delete_credential(account: &str) -> Result<bool, KeychainError> {
    let mut map = read_all_credentials();
    let removed = map.remove(account).is_some();
    if removed {
        write_all_credentials(&map)?;
    }
    Ok(removed)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_keychain_crud() {
        let test_account = "test_user_keychain_verify";
        let test_secret = "secret_password_123456";

        // 清理旧状态
        let _ = delete_credential(test_account);

        // 验证读取不存在的账号
        let non_existent = get_credential(test_account).expect("query should succeed");
        assert_eq!(non_existent, None);

        // 验证保存
        save_credential(test_account, test_secret).expect("save should succeed");

        // 验证读取
        let loaded = get_credential(test_account).expect("get should succeed");
        assert_eq!(loaded, Some(test_secret.to_string()));

        // 验证删除
        let deleted = delete_credential(test_account).expect("delete should succeed");
        assert!(deleted);

        // 再次验证删除后
        let after_delete = get_credential(test_account).expect("get should succeed");
        assert_eq!(after_delete, None);
    }
}
