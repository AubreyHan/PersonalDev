use serde::{Deserialize, Serialize};
use super::rule::FolderRule;

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct FolderItem {
    pub id: String,
    pub name: String,
    pub path: String,
    pub is_trash: bool,
    pub is_preset: bool,
    pub top_whitelist: String,
    pub rules: Vec<FolderRule>,
    pub created_at: i64,
}
