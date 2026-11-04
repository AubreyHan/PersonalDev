use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct TrashRuleConfig {
    pub days_threshold: u32,
    #[serde(alias = "maxCapacityGB")]
    pub max_capacity_gb: u32,
}
