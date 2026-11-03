use crate::auth::session::AuthManager;
use crate::db::history_repo::{ActivityListItem, ActivityQuery, HistoryRepo};
use crate::db::DbManager;
use crate::garmin::activities::GarminActivityClient;
use serde::Serialize;
use tauri::State;

#[derive(Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct ActivitiesPageResponse {
    pub total_count: i64,
    pub items: Vec<ActivityListItem>,
    pub from_cache: bool,
}

#[tauri::command]
pub async fn activities_fetch_page(
    query: ActivityQuery,
    auth_manager: State<'_, AuthManager>,
    db_manager: State<'_, DbManager>,
) -> Result<ActivitiesPageResponse, String> {
    let client_opt = {
        let lock = auth_manager.client.read().await;
        lock.clone()
    };
    let user_opt = auth_manager.get_current_user().await;

    // 1. 若有活动会话，先进行异步网络请求（不占持有数据库锁）
    let mut online_synced = false;
    if let (Some(client), Some(user)) = (client_opt, user_opt) {
        match GarminActivityClient::fetch_activities(
            &client,
            &user.display_name,
            query.start,
            query.limit,
            query.activity_type.as_deref(),
            query.start_date.as_deref(),
            query.end_date.as_deref(),
        )
        .await
        {
            Ok(records) => {
                // 网络拉取成功，写入数据库
                if !records.is_empty() {
                    let mut conn = db_manager.conn.lock().map_err(|e| e.to_string())?;
                    let _ = HistoryRepo::upsert_activities(&mut conn, &records);
                }
                online_synced = true;
            }
            Err(err) => {
                eprintln!("Online sync failed, falling back to local cache: {:?}", err);
            }
        }
    }

    // 2. 从本地 SQLite 数据库中提取分页数据（含本地导出标记与文件绝对路径）
    let conn = db_manager.conn.lock().map_err(|e| e.to_string())?;
    let (total_count, items) = HistoryRepo::query_activities_page(&conn, &query)
        .map_err(|e| e.to_string())?;

    Ok(ActivitiesPageResponse {
        total_count,
        items,
        from_cache: !online_synced,
    })
}
