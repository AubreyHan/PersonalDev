use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct AskItem {
    pub id: String,
    pub folder_id: String,
    pub file_path: String,
    pub file_name: String,
    pub size_bytes: u64,
    pub date_added: i64,
    pub rule_name: String,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct MatchListItem {
    pub file_path: String,
    pub file_name: String,
    pub size_bytes: u64,
    pub date_added: i64,
    pub rule_name: String,
}
