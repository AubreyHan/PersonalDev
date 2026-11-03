use crate::auth::sso::GarminRegion;

pub struct GarminEndpoints;

impl GarminEndpoints {
    /// 活动搜索列表 API
    /// URL: https://connect.garmin.{cn|com}/modern/proxy/activitylist-service/activities/search/activities
    pub fn activity_search(
        region: GarminRegion,
        start: i64,
        limit: i64,
        activity_type: Option<&str>,
        start_date: Option<&str>,
        end_date: Option<&str>,
    ) -> String {
        let mut url = format!(
            "{}/modern/proxy/activitylist-service/activities/search/activities?start={}&limit={}",
            region.connect_host(),
            start.max(0),
            limit.max(1)
        );

        if let Some(t) = activity_type {
            if t != "all" && !t.is_empty() {
                url.push_str(&format!("&activityType={}", t));
            }
        }

        if let Some(s) = start_date {
            if !s.is_empty() {
                url.push_str(&format!("&startDate={}", s));
            }
        }

        if let Some(e) = end_date {
            if !e.is_empty() {
                url.push_str(&format!("&endDate={}", e));
            }
        }

        url
    }

    /// GPX 轨迹下载 API
    pub fn gpx_download(region: GarminRegion, activity_id: i64) -> String {
        format!(
            "{}/modern/proxy/download-service/export/gpx/activity/{}",
            region.connect_host(),
            activity_id
        )
    }

    /// TCX 轨迹下载 API
    pub fn tcx_download(region: GarminRegion, activity_id: i64) -> String {
        format!(
            "{}/modern/proxy/download-service/export/tcx/activity/{}",
            region.connect_host(),
            activity_id
        )
    }

    /// 用户基础信息 API
    pub fn user_profile(region: GarminRegion) -> String {
        format!(
            "{}/modern/proxy/userprofile-service/userprofile/personal-information",
            region.connect_host()
        )
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_endpoints() {
        let cn_url = GarminEndpoints::activity_search(
            GarminRegion::Cn,
            0,
            20,
            Some("running"),
            Some("2026-10-01"),
            Some("2026-11-03"),
        );
        assert!(cn_url.starts_with("https://connect.garmin.cn"));
        assert!(cn_url.contains("activityType=running"));
        assert!(cn_url.contains("startDate=2026-10-01"));

        let global_gpx = GarminEndpoints::gpx_download(GarminRegion::Global, 12345);
        assert_eq!(
            global_gpx,
            "https://connect.garmin.com/modern/proxy/download-service/export/gpx/activity/12345"
        );
    }
}
