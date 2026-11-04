use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct DateAddedCondition {
    pub value: u32,
    pub unit: String, // "minutes" | "hours" | "days"
    pub is_any: bool,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct FolderRule {
    pub id: String,
    pub name: String,
    pub enabled: bool,
    pub date_added: DateAddedCondition,
    pub pattern: String,
    pub action: String, // "list" | "keep" | "direct_trash" | "ask_trash"
    pub order: usize,
}
