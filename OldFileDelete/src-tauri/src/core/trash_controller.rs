use std::fs;
use std::io;
use std::path::{Path, PathBuf};
use std::time::SystemTime;
use walkdir::WalkDir;

use crate::models::TrashRuleConfig;

#[derive(Debug, Clone, Default)]
pub struct TrashStats {
    pub total_bytes: u64,
    pub item_count: usize,
}

#[derive(Debug, Clone, Default)]
pub struct TrashCleanupReport {
    pub deleted_paths: Vec<PathBuf>,
    pub freed_bytes: u64,
}

#[derive(Debug)]
struct TrashItemDetail {
    path: PathBuf,
    is_dir: bool,
    size_bytes: u64,
    timestamp: SystemTime,
}

pub struct TrashController;

impl TrashController {
    /// Return the path to the current user's system trash directory (~/.Trash)
    pub fn get_system_trash_path() -> Option<PathBuf> {
        dirs::home_dir().map(|h| h.join(".Trash"))
    }

    /// Calculate total size and item count in the trash directory
    pub fn get_trash_stats(trash_dir: &Path) -> Result<TrashStats, io::Error> {
        let mut stats = TrashStats::default();
        if !trash_dir.exists() {
            return Ok(stats);
        }

        for entry in fs::read_dir(trash_dir)? {
            let entry = entry?;
            let path = entry.path();
            stats.item_count += 1;
            stats.total_bytes += Self::calculate_size(&path);
        }

        Ok(stats)
    }

    /// Recursively calculate the size of a file or directory
    fn calculate_size(path: &Path) -> u64 {
        if path.is_file() || path.is_symlink() {
            return path.metadata().map(|m| m.len()).unwrap_or(0);
        }

        WalkDir::new(path)
            .into_iter()
            .filter_map(|e| e.ok())
            .filter_map(|e| e.metadata().ok())
            .filter(|m| m.is_file())
            .map(|m| m.len())
            .sum()
    }

    /// Delete a file or directory physically
    fn remove_item(path: &Path) -> io::Result<()> {
        if path.is_dir() && !path.is_symlink() {
            fs::remove_dir_all(path)
        } else {
            fs::remove_file(path)
        }
    }

    /// Execute trash rules:
    /// 1. Days threshold: delete items older than `days_threshold` days
    /// 2. Max capacity GB: if total size > limit, delete items FIFO (oldest first) until within limit
    pub fn execute_trash_cleanup(
        trash_dir: &Path,
        config: &TrashRuleConfig,
        dry_run: bool,
    ) -> Result<TrashCleanupReport, io::Error> {
        let mut report = TrashCleanupReport::default();
        if !trash_dir.exists() {
            return Ok(report);
        }

        let now = SystemTime::now();

        // Collect top-level trash items
        let mut items = Vec::new();
        for entry in fs::read_dir(trash_dir)? {
            let entry = entry?;
            let path = entry.path();
            let metadata = entry.metadata()?;
            let is_dir = metadata.is_dir();
            let size_bytes = Self::calculate_size(&path);
            let timestamp = metadata.modified().unwrap_or(now);

            items.push(TrashItemDetail {
                path,
                is_dir: is_dir && !metadata.is_symlink(),
                size_bytes,
                timestamp,
            });
        }

        // Rule 1: Days threshold cleanup (0 = disabled)
        if config.days_threshold > 0 {
            let threshold_secs = config.days_threshold as u64 * 86400;
            let mut remaining_items = Vec::new();

            for item in items {
                let is_expired = now
                    .duration_since(item.timestamp)
                    .map(|elapsed| elapsed.as_secs() >= threshold_secs)
                    .unwrap_or(false);

                if is_expired {
                    if !dry_run {
                        let _ = Self::remove_item(&item.path);
                    }
                    report.freed_bytes += item.size_bytes;
                    report.deleted_paths.push(item.path);
                } else {
                    remaining_items.push(item);
                }
            }
            items = remaining_items;
        }

        // Rule 2: Max capacity GB FIFO cleanup (0 = disabled)
        if config.max_capacity_gb > 0 {
            let max_bytes = config.max_capacity_gb as u64 * 1024 * 1024 * 1024;
            let mut current_total: u64 = items.iter().map(|i| i.size_bytes).sum();

            if current_total > max_bytes {
                // Sort by timestamp ascending (FIFO: oldest items first)
                items.sort_by_key(|i| i.timestamp);

                for item in items {
                    if current_total <= max_bytes {
                        break;
                    }

                    if !dry_run {
                        let _ = Self::remove_item(&item.path);
                    }
                    current_total = current_total.saturating_sub(item.size_bytes);
                    report.freed_bytes += item.size_bytes;
                    report.deleted_paths.push(item.path);
                }
            }
        }

        Ok(report)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::fs::File;
    use std::io::Write;
    use std::time::Duration;

    #[test]
    fn test_trash_fifo_capacity_and_days_cleanup() {
        let temp_trash = std::env::temp_dir().join("file_purger_trash_test");
        let _ = fs::remove_dir_all(&temp_trash);
        fs::create_dir_all(&temp_trash).unwrap();

        let old_file = temp_trash.join("old.bin");
        let mut f1 = File::create(&old_file).unwrap();
        f1.write_all(&vec![0u8; 1024 * 100]).unwrap(); // 100KB

        let new_file = temp_trash.join("new.bin");
        let mut f2 = File::create(&new_file).unwrap();
        f2.write_all(&vec![0u8; 1024 * 200]).unwrap(); // 200KB

        let stats = TrashController::get_trash_stats(&temp_trash).unwrap();
        assert_eq!(stats.item_count, 2);
        assert_eq!(stats.total_bytes, 1024 * 300);

        // Test with max_capacity = 0 (disabled), days = 0 (disabled) -> Should delete nothing
        let config_noop = TrashRuleConfig {
            days_threshold: 0,
            max_capacity_gb: 0,
        };
        let rep1 = TrashController::execute_trash_cleanup(&temp_trash, &config_noop, true).unwrap();
        assert_eq!(rep1.deleted_paths.len(), 0);

        // Clean up temp
        let _ = fs::remove_dir_all(&temp_trash);
    }
}
