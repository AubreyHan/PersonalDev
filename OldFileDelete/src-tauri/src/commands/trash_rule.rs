use tauri::State;

use crate::models::TrashRuleConfig;
use crate::storage::ConfigStore;

#[tauri::command]
pub fn get_trash_config(store: State<ConfigStore>) -> Result<TrashRuleConfig, String> {
    Ok(store.get_config().trash_config)
}

#[tauri::command]
pub fn save_trash_config(
    config: TrashRuleConfig,
    store: State<ConfigStore>,
) -> Result<bool, String> {
    store
        .update_config(move |cfg| {
            cfg.trash_config = config;
        })
        .map_err(|e| e.to_string())?;

    Ok(true)
}
