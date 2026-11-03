use std::fs;
use tauri::AppHandle;

use crate::core::autostart::{get_macos_autostart_status, set_macos_autostart};

#[tauri::command]
pub fn get_autostart_status() -> Result<bool, String> {
    Ok(get_macos_autostart_status())
}

#[tauri::command]
pub fn set_autostart(enabled: bool) -> Result<bool, String> {
    set_macos_autostart(enabled)
}

#[tauri::command]
pub fn clean_uninstall(app: AppHandle) -> Result<bool, String> {
    // 1. Unregister autostart if registered
    let _ = set_macos_autostart(false);

    // 2. Remove configuration directory in Application Support
    if let Some(config_dir) = dirs::config_dir().map(|p| p.join("FilePurger")) {
        let _ = fs::remove_dir_all(&config_dir);
    }

    // 3. Move running .app bundle to trash
    if let Ok(exe) = std::env::current_exe() {
        let mut cur = exe.as_path();
        while let Some(parent) = cur.parent() {
            if cur.extension().and_then(|e| e.to_str()) == Some("app") {
                let _ = trash::delete(cur);
                break;
            }
            cur = parent;
        }
    }

    // 4. Exit cleanly
    app.exit(0);
    Ok(true)
}
