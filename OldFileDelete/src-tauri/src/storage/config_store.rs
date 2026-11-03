use std::fs;
use std::io;
use std::path::PathBuf;
use std::sync::{Arc, Mutex};
use serde::{Deserialize, Serialize};

use crate::models::{FolderItem, TrashRuleConfig};

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct AppConfig {
    pub folders: Vec<FolderItem>,
    pub trash_config: TrashRuleConfig,
}

impl Default for AppConfig {
    fn default() -> Self {
        let mut folders = Vec::new();

        // 1. Permanent Trash Item
        folders.push(FolderItem {
            id: "trash".to_string(),
            name: "系统废纸篓".to_string(),
            path: dirs::home_dir().map(|h| h.join(".Trash").to_string_lossy().to_string()).unwrap_or_else(|| "~/.Trash".to_string()),
            is_trash: true,
            is_preset: false,
            top_whitelist: String::new(),
            rules: Vec::new(),
            created_at: 0,
        });

        // 2. Preset Downloads Folder
        if let Some(downloads) = dirs::download_dir() {
            folders.push(FolderItem {
                id: "preset_downloads".to_string(),
                name: "Downloads".to_string(),
                path: downloads.to_string_lossy().to_string(),
                is_trash: false,
                is_preset: true,
                top_whitelist: String::new(),
                rules: Vec::new(),
                created_at: 1,
            });
        }

        Self {
            folders,
            trash_config: TrashRuleConfig {
                days_threshold: 0,
                max_capacity_gb: 0,
            },
        }
    }
}

pub struct ConfigStore {
    config_path: PathBuf,
    config: Arc<Mutex<AppConfig>>,
}

impl ConfigStore {
    pub fn new() -> Self {
        let config_dir = dirs::config_dir()
            .map(|p| p.join("FilePurger"))
            .unwrap_or_else(|| PathBuf::from("./config"));

        let _ = fs::create_dir_all(&config_dir);
        let config_path = config_dir.join("config.json");

        let config = if config_path.exists() {
            match fs::read_to_string(&config_path) {
                Ok(content) => serde_json::from_str(&content).unwrap_or_default(),
                Err(_) => AppConfig::default(),
            }
        } else {
            let default_cfg = AppConfig::default();
            let _ = fs::write(
                &config_path,
                serde_json::to_string_pretty(&default_cfg).unwrap_or_default(),
            );
            default_cfg
        };

        Self {
            config_path,
            config: Arc::new(Mutex::new(config)),
        }
    }

    pub fn get_config(&self) -> AppConfig {
        self.config.lock().unwrap().clone()
    }

    pub fn update_config<F>(&self, update_fn: F) -> Result<AppConfig, io::Error>
    where
        F: FnOnce(&mut AppConfig),
    {
        let mut cfg = self.config.lock().unwrap();
        update_fn(&mut cfg);
        let json = serde_json::to_string_pretty(&*cfg)
            .map_err(|e| io::Error::new(io::ErrorKind::Other, e))?;
        fs::write(&self.config_path, json)?;
        Ok(cfg.clone())
    }
}
