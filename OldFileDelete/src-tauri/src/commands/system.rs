use std::fs;
use tauri::AppHandle;

#[tauri::command]
pub fn get_autostart_status() -> Result<bool, String> {
    // macOS SMAppService or launchd check (mock default true for now, Phase 6 integration)
    Ok(true)
}

#[tauri::command]
pub fn set_autostart(enabled: bool) -> Result<bool, String> {
    // Phase 6 will hook SMAppService.mainApp
    println!("[FilePurger] set_autostart: {}", enabled);
    Ok(enabled)
}

#[tauri::command]
pub fn clean_uninstall(app: AppHandle) -> Result<bool, String> {
    // 1. Remove config folder
    if let Some(config_dir) = dirs::config_dir().map(|p| p.join("FilePurger")) {
        let _ = fs::remove_dir_all(&config_dir);
    }

    // 2. Trash running application bundle if possible
    if let Ok(exe) = std::env::current_exe() {
        // Traverse up to find .app bundle if packaged
        let mut cur = exe.as_path();
        while let Some(parent) = cur.parent() {
            if cur.extension().and_then(|e| e.to_str()) == Some("app") {
                let _ = trash::delete(cur);
                break;
            }
            cur = parent;
        }
    }

    // 3. Exit application
    app.exit(0);
    Ok(true)
}
