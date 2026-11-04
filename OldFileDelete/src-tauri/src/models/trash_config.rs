use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct TrashRuleConfig {
    pub days_threshold: u32,
    #[serde(alias = "maxCapacityGB")]
    pub max_capacity_gb: u32,
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_trash_config_serde_alias() {
        let json_gb = r#"{"daysThreshold": 7, "maxCapacityGB": 20}"#;
        let parsed_gb: TrashRuleConfig = serde_json::from_str(json_gb).unwrap();
        assert_eq!(parsed_gb.days_threshold, 7);
        assert_eq!(parsed_gb.max_capacity_gb, 20);

        let json_gb_lower = r#"{"daysThreshold": 5, "maxCapacityGb": 10}"#;
        let parsed_gb_lower: TrashRuleConfig = serde_json::from_str(json_gb_lower).unwrap();
        assert_eq!(parsed_gb_lower.days_threshold, 5);
        assert_eq!(parsed_gb_lower.max_capacity_gb, 10);
    }
}
