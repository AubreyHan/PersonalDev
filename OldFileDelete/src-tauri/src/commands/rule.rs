use tauri::State;
use uuid::Uuid;

use crate::models::FolderRule;
use crate::storage::ConfigStore;

#[tauri::command]
pub fn save_rule(
    folder_id: String,
    mut rule: FolderRule,
    store: State<ConfigStore>,
) -> Result<FolderRule, String> {
    if rule.id.trim().is_empty() {
        rule.id = Uuid::new_v4().to_string();
    }

    let saved_rule = rule.clone();
    store
        .update_config(move |cfg| {
            if let Some(folder) = cfg.folders.iter_mut().find(|f| f.id == folder_id) {
                if let Some(pos) = folder.rules.iter().position(|r| r.id == saved_rule.id) {
                    folder.rules[pos] = saved_rule;
                } else {
                    folder.rules.push(saved_rule);
                }
            }
        })
        .map_err(|e| e.to_string())?;

    Ok(rule)
}

#[tauri::command]
pub fn delete_rule(
    folder_id: String,
    rule_id: String,
    store: State<ConfigStore>,
) -> Result<bool, String> {
    store
        .update_config(move |cfg| {
            if let Some(folder) = cfg.folders.iter_mut().find(|f| f.id == folder_id) {
                folder.rules.retain(|r| r.id != rule_id);
            }
        })
        .map_err(|e| e.to_string())?;

    Ok(true)
}

#[tauri::command]
pub fn reorder_rules(
    folder_id: String,
    rule_ids: Vec<String>,
    store: State<ConfigStore>,
) -> Result<bool, String> {
    store
        .update_config(move |cfg| {
            if let Some(folder) = cfg.folders.iter_mut().find(|f| f.id == folder_id) {
                let mut reordered = Vec::new();
                for (new_order, id) in rule_ids.iter().enumerate() {
                    if let Some(mut rule) = folder.rules.iter().find(|r| &r.id == id).cloned() {
                        rule.order = new_order;
                        reordered.push(rule);
                    }
                }
                folder.rules = reordered;
            }
        })
        .map_err(|e| e.to_string())?;

    Ok(true)
}
