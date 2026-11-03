use keyring::Entry;
use thiserror::Error;

pub const SERVICE_NAME: &str = "com.garmin.activity.exporter";

#[derive(Error, Debug)]
pub enum KeychainError {
    #[error("Keyring entry error: {0}")]
    EntryError(String),
    #[error("Keyring operation error: {0}")]
    Keyring(#[from] keyring::Error),
}

/// 保存凭据至系统安全钥匙串
pub fn save_credential(account: &str, secret: &str) -> Result<(), KeychainError> {
    let entry = Entry::new(SERVICE_NAME, account)
        .map_err(|e| KeychainError::EntryError(e.to_string()))?;
    entry.set_password(secret)?;
    Ok(())
}

/// 从系统钥匙串读取凭据
pub fn get_credential(account: &str) -> Result<Option<String>, KeychainError> {
    let entry = Entry::new(SERVICE_NAME, account)
        .map_err(|e| KeychainError::EntryError(e.to_string()))?;
    match entry.get_password() {
        Ok(password) => Ok(Some(password)),
        Err(keyring::Error::NoEntry) => Ok(None),
        Err(err) => Err(KeychainError::Keyring(err)),
    }
}

/// 从系统钥匙串删除凭据
pub fn delete_credential(account: &str) -> Result<bool, KeychainError> {
    let entry = Entry::new(SERVICE_NAME, account)
        .map_err(|e| KeychainError::EntryError(e.to_string()))?;
    match entry.delete_credential() {
        Ok(_) => Ok(true),
        Err(keyring::Error::NoEntry) => Ok(false),
        Err(err) => Err(KeychainError::Keyring(err)),
    }
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
