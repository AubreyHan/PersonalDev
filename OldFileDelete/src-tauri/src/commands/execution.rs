use std::path::{Path, PathBuf};
use tauri::{AppHandle, Emitter, State};

use crate::core::{TrashController, WaterfallEngine};
use crate::storage::ConfigStore;

#[tauri::command]
pub async fn trigger_scan_now(
    folder_id: Option<String>,
    app: AppHandle,
    store: State<'_, ConfigStore>,
) -> Result<bool, String> {
    let cfg = store.get_config();

    // 1. Scan folders
    let target_folders: Vec<_> = if let Some(ref fid) = folder_id {
        cfg.folders.iter().filter(|f| &f.id == fid).collect()
    } else {
        cfg.folders.iter().filter(|f| !f.is_trash).collect()
    };

    for folder in target_folders {
        let path = PathBuf::from(&folder.path);
        if let Ok(res) = WaterfallEngine::execute_scan(&path, folder, false) {
            // Emit matched list update to frontend
            let _ = app.emit("app:match_list_update", &res.matched_list);

            // If there are ask items, emit to frontend ask queue
            if !res.ask_items.is_empty() {
                let _ = app.emit("app:ask_queue_update", &res.ask_items);
            }
        }
    }

    // 2. Trash cleanup
    if let Some(trash_path) = TrashController::get_system_trash_path() {
        let _ = TrashController::execute_trash_cleanup(&trash_path, &cfg.trash_config, false);
        if let Ok(stats) = TrashController::get_trash_stats(&trash_path) {
            let _ = app.emit("app:trash_status_changed", (stats.total_bytes, stats.item_count));
        }
    }

    Ok(true)
}

#[tauri::command]
pub fn respond_ask_item(
    item_path: String,
    decision: String,
) -> Result<bool, String> {
    let p = Path::new(&item_path);
    if decision == "trash" && p.exists() {
        trash::delete(p).map_err(|e| e.to_string())?;
    }
    Ok(true)
}
