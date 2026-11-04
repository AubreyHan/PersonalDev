use std::path::{Path, PathBuf};
use std::time::SystemTime;
use uuid::Uuid;
use walkdir::WalkDir;

use super::date_added::get_date_added;
use super::pattern_matcher::PatternMatcher;
use crate::models::{AskItem, DateAddedCondition, FolderItem, FolderRule, MatchListItem};

#[derive(Debug, Default)]
pub struct WaterfallEngineResult {
    pub matched_list: Vec<MatchListItem>,
    pub ask_items: Vec<AskItem>,
    pub trashed_paths: Vec<PathBuf>,
    pub kept_paths: Vec<PathBuf>,
}

pub struct WaterfallEngine;

impl WaterfallEngine {
    /// Evaluate a single date-added condition against the actual date added timestamp.
    pub fn is_date_condition_met(condition: &DateAddedCondition, date_added: SystemTime, now: SystemTime) -> bool {
        if condition.is_any || condition.value == 0 {
            return true;
        }

        let threshold_secs = match condition.unit.as_str() {
            "minutes" => condition.value as u64 * 60,
            "hours" => condition.value as u64 * 3600,
            "days" => condition.value as u64 * 86400,
            _ => condition.value as u64 * 60,
        };

        match now.duration_since(date_added) {
            Ok(elapsed) => elapsed.as_secs() >= threshold_secs,
            Err(_) => false, // Date added is in the future
        }
    }

    /// Process a folder with waterfall rules:
    /// 1. Skip hidden files (starting with '.')
    /// 2. Top whitelist takes highest priority (short-circuits immediately)
    /// 3. Normal rules evaluated from top to bottom (first-match short-circuits)
    pub fn execute_scan(
        root_path: &Path,
        folder_item: &FolderItem,
        dry_run: bool,
    ) -> Result<WaterfallEngineResult, Box<dyn std::error::Error>> {
        let mut result = WaterfallEngineResult::default();
        if !root_path.exists() || !root_path.is_dir() {
            return Ok(result);
        }

        let now = SystemTime::now();

        // 1. Prepare Top Whitelist Matcher
        let top_matcher = if !folder_item.top_whitelist.trim().is_empty() {
            Some(PatternMatcher::new(&folder_item.top_whitelist)?)
        } else {
            None
        };

        // 2. Prepare enabled normal rules sorted by order
        let mut sorted_rules: Vec<&FolderRule> = folder_item
            .rules
            .iter()
            .filter(|r| r.enabled)
            .collect();
        sorted_rules.sort_by_key(|r| r.order);

        let compiled_rules: Vec<(&FolderRule, PatternMatcher)> = sorted_rules
            .into_iter()
            .filter_map(|r| {
                PatternMatcher::new(&r.pattern).ok().map(|matcher| (r, matcher))
            })
            .collect();

        // 3. Walk directory entries
        for entry in WalkDir::new(root_path)
            .min_depth(1)
            .into_iter()
            .filter_entry(|e| {
                // Rule: Skip any file/folder starting with '.'
                let file_name = e.file_name().to_string_lossy();
                !file_name.starts_with('.')
            })
            .filter_map(|e| e.ok())
        {
            let path = entry.path();
            let file_name = entry.file_name().to_string_lossy().to_string();
            let rel_path = match path.strip_prefix(root_path) {
                Ok(p) => p,
                Err(_) => continue,
            };

            // 3.1 Check Top Whitelist (Absolute immunity)
            if let Some(ref whitelist) = top_matcher {
                if whitelist.is_match(rel_path, &file_name) {
                    result.kept_paths.push(path.to_path_buf());
                    continue; // Short-circuit, bypass all normal rules
                }
            }

            // 3.2 Evaluate waterfall rules in order (First Match)
            let date_added = get_date_added(path);
            let metadata = entry.metadata().ok();
            let size_bytes = metadata.as_ref().map(|m| m.len()).unwrap_or(0);
            let date_added_millis = date_added
                .duration_since(SystemTime::UNIX_EPOCH)
                .map(|d| d.as_millis() as i64)
                .unwrap_or(0);

            for (rule, matcher) in &compiled_rules {
                if matcher.is_match(rel_path, &file_name)
                    && Self::is_date_condition_met(&rule.date_added, date_added, now)
                {
                    match rule.action.as_str() {
                        "keep" => {
                            result.kept_paths.push(path.to_path_buf());
                            break; // Short-circuit
                        }
                        "list" => {
                            result.matched_list.push(MatchListItem {
                                file_path: path.to_string_lossy().to_string(),
                                file_name: file_name.clone(),
                                size_bytes,
                                date_added: date_added_millis,
                                rule_name: rule.name.clone(),
                            });
                            break; // Short-circuit
                        }
                        "direct_trash" => {
                            if !dry_run {
                                let _ = trash::delete(path);
                            }
                            result.trashed_paths.push(path.to_path_buf());
                            break; // Short-circuit
                        }
                        "ask_trash" => {
                            result.ask_items.push(AskItem {
                                id: Uuid::new_v4().to_string(),
                                folder_id: folder_item.id.clone(),
                                file_path: path.to_string_lossy().to_string(),
                                file_name: file_name.clone(),
                                size_bytes,
                                date_added: date_added_millis,
                                rule_name: rule.name.clone(),
                            });
                            break; // Short-circuit
                        }
                        _ => {}
                    }
                }
            }
        }

        Ok(result)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::fs::{self, File};

    #[test]
    fn test_top_whitelist_immunity_and_waterfall_order() {
        let temp_dir = std::env::temp_dir().join("file_purger_waterfall_test");
        let _ = fs::remove_dir_all(&temp_dir);
        fs::create_dir_all(&temp_dir).unwrap();

        // Create test files
        File::create(temp_dir.join("keep_me.dmg")).unwrap();
        File::create(temp_dir.join("normal.dmg")).unwrap();
        File::create(temp_dir.join(".hidden.dmg")).unwrap(); // hidden file, should be skipped
        File::create(temp_dir.join("ask.zip")).unwrap();
        File::create(temp_dir.join("list.pdf")).unwrap();

        let folder = FolderItem {
            id: "test_folder".into(),
            name: "Test".into(),
            path: temp_dir.to_string_lossy().into(),
            is_trash: false,
            is_preset: false,
            top_whitelist: "keep*".into(), // keep* has absolute immunity
            rules: vec![
                // Rule 0: DMG files -> direct_trash
                FolderRule {
                    id: "r0".into(),
                    name: "Delete DMGs".into(),
                    enabled: true,
                    date_added: DateAddedCondition {
                        value: 0,
                        unit: "minutes".into(),
                        is_any: true,
                    },
                    pattern: "*.dmg".into(),
                    action: "direct_trash".into(),
                    order: 0,
                },
                // Rule 1: ZIP files -> ask_trash
                FolderRule {
                    id: "r1".into(),
                    name: "Ask Zips".into(),
                    enabled: true,
                    date_added: DateAddedCondition {
                        value: 0,
                        unit: "minutes".into(),
                        is_any: true,
                    },
                    pattern: "*.zip".into(),
                    action: "ask_trash".into(),
                    order: 1,
                },
                // Rule 2: PDF files -> list
                FolderRule {
                    id: "r2".into(),
                    name: "List PDFs".into(),
                    enabled: true,
                    date_added: DateAddedCondition {
                        value: 0,
                        unit: "minutes".into(),
                        is_any: true,
                    },
                    pattern: "*.pdf".into(),
                    action: "list".into(),
                    order: 2,
                },
            ],
            created_at: 0,
        };

        // Run dry_run so files aren't really deleted from disk during test
        let res = WaterfallEngine::execute_scan(&temp_dir, &folder, true).unwrap();

        // 1. keep_me.dmg must be kept due to top whitelist, NOT trashed even though *.dmg matches rule 0
        assert!(res.kept_paths.iter().any(|p| p.ends_with("keep_me.dmg")));

        // 2. normal.dmg should be in trashed_paths
        assert!(res.trashed_paths.iter().any(|p| p.ends_with("normal.dmg")));

        // 3. .hidden.dmg must NOT be in any list (completely ignored)
        assert!(!res.trashed_paths.iter().any(|p| p.ends_with(".hidden.dmg")));
        assert!(!res.kept_paths.iter().any(|p| p.ends_with(".hidden.dmg")));

        // 4. ask.zip should be in ask_items
        assert_eq!(res.ask_items.len(), 1);
        assert_eq!(res.ask_items[0].file_name, "ask.zip");

        // 5. list.pdf should be in matched_list
        assert_eq!(res.matched_list.len(), 1);
        assert_eq!(res.matched_list[0].file_name, "list.pdf");

        let _ = fs::remove_dir_all(&temp_dir);
    }
}
