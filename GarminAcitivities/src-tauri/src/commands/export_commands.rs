use crate::auth::session::AuthManager;
use crate::db::history_repo::HistoryRepo;
use crate::db::DbManager;
use crate::exporter::file_writer::FileWriter;
use crate::exporter::merger::TrackMerger;
use crate::exporter::queue::{BatchExportRequest, ExportQueue};
use crate::exporter::template::DEFAULT_NAMING_TEMPLATE;
use crate::garmin::endpoints::GarminEndpoints;
use serde::{Deserialize, Serialize};
use std::path::PathBuf;
use tauri::{AppHandle, State};

#[derive(Debug, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct IncrementalExportRequest {
    pub format: String, // "gpx" | "tcx" | "both"
    pub export_directory: Option<String>,
    pub naming_template: Option<String>,
}

#[derive(Debug, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct MergeActivitiesRequest {
    pub activity_ids: Vec<i64>,
    pub format: String, // "gpx" | "tcx"
    pub custom_file_name: Option<String>,
    pub export_directory: Option<String>,
}

#[derive(Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct MergeResponse {
    pub success: bool,
    pub merged_file_path: String,
    pub point_count: usize,
}

#[derive(Debug, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct QueueControlRequest {
    pub action: String, // "pause" | "resume" | "cancel"
}

#[derive(Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct AppSettingsPayload {
    pub export_directory: String,
    pub default_format: String,
    pub naming_template: String,
    pub concurrency: usize,
    pub delay_jitter_ms: u64,
}

#[tauri::command]
pub async fn export_start_batch(
    app: AppHandle,
    payload: BatchExportRequest,
    auth_manager: State<'_, AuthManager>,
    db_manager: State<'_, DbManager>,
    _queue: State<'_, ExportQueue>,
) -> Result<serde_json::Value, String> {
    let client = {
        let lock = auth_manager.client.read().await;
        lock.clone().ok_or_else(|| "Not logged in".to_string())?
    };

    // 获取导出目录与命名模板（若未提供则从 settings 或默认配置读取）
    let (exp_dir, tpl) = {
        let conn = db_manager.conn.lock().map_err(|e| e.to_string())?;
        let dir = payload.export_directory.unwrap_or_else(|| {
            HistoryRepo::get_setting(&conn, "exportDirectory")
                .ok()
                .flatten()
                .unwrap_or_else(|| {
                    dirs::download_dir()
                        .map(|d| d.join("Garmin_Activities").to_string_lossy().to_string())
                        .unwrap_or_else(|| "./Garmin_Activities".to_string())
                })
        });
        let naming = payload.naming_template.unwrap_or_else(|| {
            HistoryRepo::get_setting(&conn, "namingTemplate")
                .ok()
                .flatten()
                .unwrap_or_else(|| DEFAULT_NAMING_TEMPLATE.to_string())
        });
        (PathBuf::from(dir), naming)
    };

    // 从数据库中查询所勾选活动的元数据
    let mut selected_records = Vec::new();
    {
        let conn = db_manager.conn.lock().map_err(|e| e.to_string())?;
        for id in &payload.activity_ids {
            let res = conn.query_row(
                "SELECT activity_id, user_id, region, activity_name, activity_type, start_time_local, distance_meters, duration_seconds, avg_speed_mps, avg_hr_bpm, max_hr_bpm, elevation_gain_m, raw_json FROM activities WHERE activity_id = ?1",
                rusqlite::params![id],
                |row| {
                    Ok(crate::db::history_repo::ActivityRecord {
                        activity_id: row.get(0)?,
                        user_id: row.get(1)?,
                        region: row.get(2)?,
                        activity_name: row.get(3)?,
                        activity_type: row.get(4)?,
                        start_time_local: row.get(5)?,
                        distance_meters: row.get(6)?,
                        duration_seconds: row.get(7)?,
                        avg_speed_mps: row.get(8)?,
                        avg_hr_bpm: row.get(9)?,
                        max_hr_bpm: row.get(10)?,
                        elevation_gain_m: row.get(11)?,
                        raw_json: row.get(12)?,
                    })
                },
            );
            if let Ok(rec) = res {
                selected_records.push(rec);
            }
        }
    }

    let queued_count = selected_records.len();
    let override_existing = payload.override_existing.unwrap_or(false);

    // 启动后台队列
    let q_clone = ExportQueue::new(2);
    let app_clone = app.clone();
    let db_clone = (*db_manager).clone();

    tokio::spawn(async move {
        q_clone
            .process_batch(
                app_clone,
                client,
                db_clone,
                selected_records,
                &payload.format,
                exp_dir,
                tpl,
                override_existing,
            )
            .await;
    });

    Ok(serde_json::json!({
        "queuedCount": queued_count,
        "jobId": format!("job_{}", chrono::Utc::now().timestamp_millis()),
    }))
}

#[tauri::command]
pub async fn export_start_incremental(
    app: AppHandle,
    payload: IncrementalExportRequest,
    auth_manager: State<'_, AuthManager>,
    db_manager: State<'_, DbManager>,
    queue: State<'_, ExportQueue>,
) -> Result<serde_json::Value, String> {
    let unexported_ids = {
        let conn = db_manager.conn.lock().map_err(|e| e.to_string())?;
        HistoryRepo::query_unexported_ids(&conn, &payload.format).map_err(|e| e.to_string())?
    };

    if unexported_ids.is_empty() {
        return Ok(serde_json::json!({
            "queuedCount": 0,
            "message": "所有记录均已导出完毕",
        }));
    }

    export_start_batch(
        app,
        BatchExportRequest {
            activity_ids: unexported_ids,
            format: payload.format,
            override_existing: Some(false),
            export_directory: payload.export_directory,
            naming_template: payload.naming_template,
        },
        auth_manager,
        db_manager,
        queue,
    )
    .await
}

#[tauri::command]
pub async fn export_merge_activities(
    payload: MergeActivitiesRequest,
    auth_manager: State<'_, AuthManager>,
    db_manager: State<'_, DbManager>,
) -> Result<MergeResponse, String> {
    if payload.activity_ids.len() < 2 {
        return Err("至少需选择 2 条活动进行合并导出".to_string());
    }

    let client = {
        let lock = auth_manager.client.read().await;
        lock.clone().ok_or_else(|| "Not logged in".to_string())?
    };

    let target_dir = {
        let conn = db_manager.conn.lock().map_err(|e| e.to_string())?;
        payload.export_directory.unwrap_or_else(|| {
            HistoryRepo::get_setting(&conn, "exportDirectory")
                .ok()
                .flatten()
                .unwrap_or_else(|| {
                    dirs::download_dir()
                        .map(|d| d.join("Garmin_Activities").to_string_lossy().to_string())
                        .unwrap_or_else(|| "./Garmin_Activities".to_string())
                })
        })
    };

    // 下载各个分段原始文本
    let mut downloaded_parts: Vec<String> = Vec::new();
    for id in &payload.activity_ids {
        let url = if payload.format == "tcx" {
            GarminEndpoints::tcx_download(client.region, *id)
        } else {
            GarminEndpoints::gpx_download(client.region, *id)
        };

        let resp = client
            .http_client
            .get(&url)
            .header("Referer", format!("{}/modern", client.region.connect_host()))
            .send()
            .await
            .map_err(|e| e.to_string())?;

        if !resp.status().is_success() {
            return Err(format!("Failed to download track segment for activity {}", id));
        }

        let body = resp.text().await.map_err(|e| e.to_string())?;
        downloaded_parts.push(body);
    }

    let slices: Vec<&str> = downloaded_parts.iter().map(|s| s.as_str()).collect();

    let (merged_content, extension) = if payload.format == "tcx" {
        let tcx_str = TrackMerger::merge_tcx(&slices, "Running").map_err(|e| e.to_string())?;
        (tcx_str, "tcx")
    } else {
        let gpx_str = TrackMerger::merge_gpx(&slices, "Merged Track").map_err(|e| e.to_string())?;
        (gpx_str, "gpx")
    };

    let default_name = format!(
        "{}_merged_{}_activities.{}",
        chrono::Local::now().format("%Y-%m-%d"),
        payload.activity_ids.len(),
        extension
    );
    let filename = payload.custom_file_name.unwrap_or(default_name);

    let saved = FileWriter::save_flat_file(
        &PathBuf::from(target_dir),
        &filename,
        merged_content.as_bytes(),
        true,
    )
    .map_err(|e| e.to_string())?;

    Ok(MergeResponse {
        success: true,
        merged_file_path: saved.file_path.to_string_lossy().to_string(),
        point_count: merged_content.matches("<trkpt").count() + merged_content.matches("<Trackpoint").count(),
    })
}

#[tauri::command]
pub async fn export_control_queue(
    payload: QueueControlRequest,
    queue: State<'_, ExportQueue>,
) -> Result<serde_json::Value, String> {
    match payload.action.as_str() {
        "pause" => queue.pause(),
        "resume" => queue.resume(),
        "cancel" => queue.cancel(),
        _ => return Err("Unknown action".to_string()),
    }

    let is_paused = queue.is_paused.load(std::sync::atomic::Ordering::SeqCst);
    Ok(serde_json::json!({
        "currentStatus": if is_paused { "paused" } else { "running" },
    }))
}

#[tauri::command]
pub async fn settings_get(db_manager: State<'_, DbManager>) -> Result<AppSettingsPayload, String> {
    let conn = db_manager.conn.lock().map_err(|e| e.to_string())?;
    let default_dir = dirs::download_dir()
        .map(|d| d.join("Garmin_Activities").to_string_lossy().to_string())
        .unwrap_or_else(|| "./Garmin_Activities".to_string());

    let dir = HistoryRepo::get_setting(&conn, "exportDirectory")
        .ok()
        .flatten()
        .unwrap_or(default_dir);
    let fmt = HistoryRepo::get_setting(&conn, "defaultFormat")
        .ok()
        .flatten()
        .unwrap_or_else(|| "gpx".to_string());
    let tpl = HistoryRepo::get_setting(&conn, "namingTemplate")
        .ok()
        .flatten()
        .unwrap_or_else(|| DEFAULT_NAMING_TEMPLATE.to_string());

    Ok(AppSettingsPayload {
        export_directory: dir,
        default_format: fmt,
        naming_template: tpl,
        concurrency: 2,
        delay_jitter_ms: 500,
    })
}

#[tauri::command]
pub async fn settings_update(
    payload: AppSettingsPayload,
    db_manager: State<'_, DbManager>,
) -> Result<serde_json::Value, String> {
    let conn = db_manager.conn.lock().map_err(|e| e.to_string())?;
    HistoryRepo::set_setting(&conn, "exportDirectory", &payload.export_directory)
        .map_err(|e| e.to_string())?;
    HistoryRepo::set_setting(&conn, "defaultFormat", &payload.default_format)
        .map_err(|e| e.to_string())?;
    HistoryRepo::set_setting(&conn, "namingTemplate", &payload.naming_template)
        .map_err(|e| e.to_string())?;

    Ok(serde_json::json!({ "success": true }))
}
