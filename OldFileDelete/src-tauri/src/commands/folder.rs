use std::path::{Path, PathBuf};
use tauri::State;
use uuid::Uuid;

use crate::core::PathHierarchyGuard;
use crate::models::FolderItem;
use crate::storage::ConfigStore;

#[tauri::command]
pub fn get_all_folders(store: State<ConfigStore>) -> Result<Vec<FolderItem>, String> {
    Ok(store.get_config().folders)
}

#[tauri::command]
pub fn add_folder(path: String, store: State<ConfigStore>) -> Result<FolderItem, String> {
    let p = Path::new(&path);
    let current_cfg = store.get_config();

    let existing_paths: Vec<PathBuf> = current_cfg
        .folders
        .iter()
        .filter(|f| !f.is_trash)
        .map(|f| PathBuf::from(&f.path))
        .collect();

    let canonical = PathHierarchyGuard::check_can_add(p, &existing_paths)
        .map_err(|e| e.to_string())?;

    let name = canonical
        .file_name()
        .and_then(|n| n.to_str())
        .unwrap_or("Folder")
        .to_string();

    let new_folder = FolderItem {
        id: Uuid::new_v4().to_string(),
        name,
        path: canonical.to_string_lossy().to_string(),
        is_trash: false,
        is_preset: false,
        top_whitelist: String::new(),
        rules: Vec::new(),
        created_at: chrono::Utc::now().timestamp_millis(),
    };

    let folder_clone = new_folder.clone();
    store
        .update_config(move |cfg| {
            cfg.folders.push(folder_clone);
        })
        .map_err(|e| e.to_string())?;

    Ok(new_folder)
}

#[tauri::command]
pub fn remove_folder(folder_id: String, store: State<ConfigStore>) -> Result<bool, String> {
    store
        .update_config(move |cfg| {
            cfg.folders.retain(|f| f.id != folder_id || f.is_trash);
        })
        .map_err(|e| e.to_string())?;

    Ok(true)
}

#[tauri::command]
pub fn update_top_whitelist(
    folder_id: String,
    whitelist: String,
    store: State<ConfigStore>,
) -> Result<bool, String> {
    store
        .update_config(move |cfg| {
            if let Some(folder) = cfg.folders.iter_mut().find(|f| f.id == folder_id) {
                folder.top_whitelist = whitelist;
            }
        })
        .map_err(|e| e.to_string())?;

    Ok(true)
}
