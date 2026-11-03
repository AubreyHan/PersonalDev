use crate::auth::sso::SsoClient;
use crate::db::history_repo::ActivityRecord;
use crate::garmin::endpoints::GarminEndpoints;
use serde::{Deserialize, Serialize};
use thiserror::Error;

#[derive(Error, Debug)]
pub enum GarminApiError {
    #[error("Network error: {0}")]
    Network(#[from] reqwest::Error),
    #[error("JSON deserialization error: {0}")]
    Json(#[from] serde_json::Error),
    #[error("Authentication required: {0}")]
    Unauthorized(String),
    #[error("Garmin API rate limit (429): {0}")]
    RateLimited(String),
    #[error("API request failed with status: {0}")]
    Http(u16),
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct GarminActivityTypeRaw {
    pub type_id: Option<i64>,
    pub type_key: Option<String>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct GarminActivityRaw {
    pub activity_id: i64,
    pub activity_name: Option<String>,
    pub activity_type: Option<GarminActivityTypeRaw>,
    pub start_time_local: Option<String>,
    pub distance: Option<f64>,
    pub duration: Option<f64>,
    pub average_speed: Option<f64>,
    #[serde(rename = "averageHR")]
    pub average_hr: Option<f64>,
    #[serde(rename = "maxHR")]
    pub max_hr: Option<f64>,
    pub elevation_gain: Option<f64>,
}

impl GarminActivityRaw {
    pub fn to_activity_record(&self, user_id: &str, region: &str) -> ActivityRecord {
        let raw_json = serde_json::to_string(self).ok();
        ActivityRecord {
            activity_id: self.activity_id,
            user_id: user_id.to_string(),
            region: region.to_string(),
            activity_name: self.activity_name.clone().unwrap_or_else(|| "未命名活动".to_string()),
            activity_type: self
                .activity_type
                .as_ref()
                .and_then(|t| t.type_key.clone())
                .unwrap_or_else(|| "other".to_string()),
            start_time_local: self.start_time_local.clone().unwrap_or_default(),
            distance_meters: self.distance.unwrap_or(0.0),
            duration_seconds: self.duration.map(|d| d.round() as i64).unwrap_or(0),
            avg_speed_mps: self.average_speed.unwrap_or(0.0),
            avg_hr_bpm: self.average_hr.map(|hr| hr.round() as i32),
            max_hr_bpm: self.max_hr.map(|hr| hr.round() as i32),
            elevation_gain_m: self.elevation_gain.unwrap_or(0.0),
            raw_json,
        }
    }
}

pub struct GarminActivityClient;

impl GarminActivityClient {
    /// 解析从 Garmin search API 接收到的 JSON 响应（支持裸数组、各种包裹格式与宽容字段解析）
    pub fn parse_activities(json_str: &str, user_id: &str, region: &str) -> Result<Vec<ActivityRecord>, serde_json::Error> {
        let root: serde_json::Value = serde_json::from_str(json_str)?;

        // 提取活动列表数组（可能是顶层数组，也可能是包装对象中的字段）
        let raw_array_opt = if let Some(arr) = root.as_array() {
            Some(arr)
        } else if let Some(obj) = root.as_object() {
            obj.get("activityList")
                .or_else(|| obj.get("activities"))
                .or_else(|| obj.get("activityListEntries"))
                .and_then(|v| v.as_array())
        } else {
            None
        };

        let raw_array = match raw_array_opt {
            Some(arr) => arr,
            None => {
                // 如果返回的是空的字典如 `{}`，说明当前范围没有活动记录
                return Ok(Vec::new());
            }
        };

        let mut records = Vec::new();
        for item in raw_array {
            // 提取 activity_id (支持 i64, u64 或 string)
            let activity_id = item
                .get("activityId")
                .and_then(|v| v.as_i64().or_else(|| v.as_u64().map(|u| u as i64)).or_else(|| v.as_str().and_then(|s| s.parse().ok())));

            let activity_id = match activity_id {
                Some(id) => id,
                None => continue, // 跳过无有效 ID 的异常项
            };

            let activity_name = item
                .get("activityName")
                .and_then(|v| v.as_str())
                .unwrap_or("未命名活动")
                .to_string();

            // 提取运动类型，可能为对象 {"typeKey": "running"} 或直接字符串 "running"
            let activity_type = item
                .get("activityType")
                .and_then(|v| {
                    if let Some(s) = v.as_str() {
                        Some(s.to_string())
                    } else if let Some(obj) = v.as_object() {
                        obj.get("typeKey").and_then(|k| k.as_str()).map(|s| s.to_string())
                    } else {
                        None
                    }
                })
                .unwrap_or_else(|| "other".to_string());

            let start_time_local = item
                .get("startTimeLocal")
                .and_then(|v| v.as_str())
                .unwrap_or("")
                .to_string();

            let distance_meters = item.get("distance").and_then(|v| v.as_f64()).unwrap_or(0.0);
            let duration_seconds = item
                .get("duration")
                .and_then(|v| v.as_f64())
                .map(|d| d.round() as i64)
                .unwrap_or(0);
            let avg_speed_mps = item
                .get("averageSpeed")
                .and_then(|v| v.as_f64())
                .unwrap_or(0.0);

            let avg_hr_bpm = item
                .get("averageHR")
                .or_else(|| item.get("averageHeartRate"))
                .and_then(|v| v.as_f64())
                .map(|hr| hr.round() as i32);

            let max_hr_bpm = item
                .get("maxHR")
                .or_else(|| item.get("maxHeartRate"))
                .and_then(|v| v.as_f64())
                .map(|hr| hr.round() as i32);

            let elevation_gain_m = item
                .get("elevationGain")
                .and_then(|v| v.as_f64())
                .unwrap_or(0.0);

            let raw_json = serde_json::to_string(item).ok();

            records.push(ActivityRecord {
                activity_id,
                user_id: user_id.to_string(),
                region: region.to_string(),
                activity_name,
                activity_type,
                start_time_local,
                distance_meters,
                duration_seconds,
                avg_speed_mps,
                avg_hr_bpm,
                max_hr_bpm,
                elevation_gain_m,
                raw_json,
            });
        }

        Ok(records)
    }

    /// 向 Garmin 云端拉取指定范围的活动记录（纯异步网络请求，不占用数据库锁）
    pub async fn fetch_activities(
        client: &SsoClient,
        user_id: &str,
        start: i64,
        limit: i64,
        activity_type: Option<&str>,
        start_date: Option<&str>,
        end_date: Option<&str>,
    ) -> Result<Vec<ActivityRecord>, GarminApiError> {
        let url = GarminEndpoints::activity_search(
            client.region,
            start,
            limit,
            activity_type,
            start_date,
            end_date,
        );

        let mut req = client
            .http_client
            .get(&url)
            .header("Referer", format!("{}/app", client.region.connect_host()))
            .header("Accept", "application/json, text/plain, */*")
            .header("DI-Backend", "connectapi.garmin.com");

        if let Some(ref jwt) = client.jwt_token {
            req = req.header("Authorization", format!("Bearer {}", jwt));
        }
        if let Some(ref cookies) = client.cookie_header {
            req = req.header("Cookie", cookies);
        }

        let response = req.send().await?;

        let status = response.status();
        let final_url = response.url().to_string();

        // 若被重定向至登录/SSO页面，说明会话未登录或已过期
        if final_url.contains("/signin") || final_url.contains("/sso") {
            return Err(GarminApiError::Unauthorized(
                "Garmin 会话未登录或已失效，请点击下方「重新连接 Garmin 账号」".to_string(),
            ));
        }

        if status.as_u16() == 401 || status.as_u16() == 403 {
            return Err(GarminApiError::Unauthorized(
                "Garmin 会话已失效(401/403)，请点击下方「重新连接 Garmin 账号」".to_string(),
            ));
        }
        if status.as_u16() == 429 {
            return Err(GarminApiError::RateLimited("Garmin 429 请求过于频繁，请稍后重试".to_string()));
        }
        if !status.is_success() {
            return Err(GarminApiError::Http(status.as_u16()));
        }

        let json_text = response.text().await?;
        let trimmed = json_text.trim();

        // 容错：空响应表示无更多数据
        if trimmed.is_empty() {
            return Ok(Vec::new());
        }

        // 容错：若返回 HTML（以 '<' 开头），说明返回了重定向落地页而非 API JSON
        if trimmed.starts_with('<') {
            return Err(GarminApiError::Unauthorized(
                "Garmin 返回了网页而非活动数据，说明当前账号尚未登录，请点击下方「重新连接 Garmin 账号」".to_string(),
            ));
        }

        let records = Self::parse_activities(trimmed, user_id, client.region.tld())?;
        Ok(records)
    }
}

#[cfg(test)]
pub mod tests {
    use super::*;

    #[test]
    fn test_parse_garmin_raw_json() {
        let sample_json = r#"
        [
            {
                "activityId": 16892401234,
                "activityName": "朝阳公园晨跑 10K",
                "activityType": {
                    "typeId": 1,
                    "typeKey": "running"
                },
                "startTimeLocal": "2026-11-01 06:30:15",
                "distance": 10052.4,
                "duration": 3120.0,
                "averageSpeed": 3.22,
                "averageHR": 152.0,
                "maxHR": 168.0,
                "elevationGain": 35.0
            },
            {
                "activityId": 16892405678,
                "activityName": null,
                "activityType": {
                    "typeId": 2,
                    "typeKey": "cycling"
                },
                "startTimeLocal": "2026-11-02 14:00:00",
                "distance": 25000.0,
                "duration": 4200.0,
                "averageSpeed": 5.95,
                "averageHR": null,
                "maxHR": null,
                "elevationGain": 120.5
            }
        ]
        "#;

        let records = GarminActivityClient::parse_activities(sample_json, "test_user", "cn").unwrap();
        assert_eq!(records.len(), 2);

        // 验证第一条完整数据
        assert_eq!(records[0].activity_id, 16892401234);
        assert_eq!(records[0].activity_name, "朝阳公园晨跑 10K");
        assert_eq!(records[0].activity_type, "running");
        assert_eq!(records[0].start_time_local, "2026-11-01 06:30:15");
        assert_eq!(records[0].distance_meters, 10052.4);
        assert_eq!(records[0].duration_seconds, 3120);
        assert_eq!(records[0].avg_hr_bpm, Some(152));
        assert_eq!(records[0].max_hr_bpm, Some(168));
        assert_eq!(records[0].elevation_gain_m, 35.0);

        // 验证第二条缺省值与容错
        assert_eq!(records[1].activity_id, 16892405678);
        assert_eq!(records[1].activity_name, "未命名活动");
        assert_eq!(records[1].activity_type, "cycling");
        assert_eq!(records[1].avg_hr_bpm, None);
        assert_eq!(records[1].max_hr_bpm, None);
    }
}
