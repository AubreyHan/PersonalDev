use crate::auth::sso::SsoClient;
use crate::db::history_repo::{ActivityRecord, HistoryRepo};
use crate::db::DbManager;
use crate::exporter::file_writer::FileWriter;
use crate::exporter::template::{TemplateContext, TemplateEngine, DEFAULT_NAMING_TEMPLATE};
use crate::garmin::endpoints::GarminEndpoints;
use serde::{Deserialize, Serialize};
use std::path::PathBuf;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;
use std::time::Duration;
use tauri::{AppHandle, Emitter};
use tokio::sync::{Mutex, Semaphore};

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct TaskProgressEvent {
    pub activity_id: i64,
    pub status: String, // "downloading" | "saved" | "failed"
    pub progress_percent: u32,
    pub saved_path: Option<String>,
    pub error_message: Option<String>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct QueueSummaryEvent {
    pub total_tasks: usize,
    pub completed_tasks: usize,
    pub failed_tasks: usize,
    pub remaining_tasks: usize,
    pub is_paused: bool,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct BatchExportRequest {
    pub activity_ids: Vec<i64>,
    pub format: String, // "gpx" | "tcx" | "both"
    pub override_existing: Option<bool>,
    pub export_directory: Option<String>,
    pub naming_template: Option<String>,
}

pub struct ExportQueue {
    pub is_paused: Arc<AtomicBool>,
    pub is_cancelled: Arc<AtomicBool>,
    pub semaphore: Arc<Semaphore>,
    pub active_job_count: Arc<Mutex<usize>>,
}

impl Default for ExportQueue {
    fn default() -> Self {
        Self::new(2)
    }
}

impl ExportQueue {
    pub fn new(concurrency: usize) -> Self {
        Self {
            is_paused: Arc::new(AtomicBool::new(false)),
            is_cancelled: Arc::new(AtomicBool::new(false)),
            semaphore: Arc::new(Semaphore::new(concurrency.max(1))),
            active_job_count: Arc::new(Mutex::new(0)),
        }
    }

    pub fn pause(&self) {
        self.is_paused.store(true, Ordering::SeqCst);
    }

    pub fn resume(&self) {
        self.is_paused.store(false, Ordering::SeqCst);
    }

    pub fn cancel(&self) {
        self.is_cancelled.store(true, Ordering::SeqCst);
    }

    pub fn reset_state(&self) {
        self.is_paused.store(false, Ordering::SeqCst);
        self.is_cancelled.store(false, Ordering::SeqCst);
    }

    /// 执行批量下载任务调度队列
    pub async fn process_batch(
        &self,
        app_handle: AppHandle,
        client: SsoClient,
        db_manager: DbManager,
        records: Vec<ActivityRecord>,
        format: &str,
        export_dir: PathBuf,
        naming_template: String,
        override_existing: bool,
    ) {
        self.reset_state();
        let total_tasks = records.len();
        let completed = Arc::new(Mutex::new(0));
        let failed = Arc::new(Mutex::new(0));

        let formats_to_export: Vec<String> = if format == "both" {
            vec!["gpx".to_string(), "tcx".to_string()]
        } else {
            vec![format.to_lowercase()]
        };

        for record in records {
            if self.is_cancelled.load(Ordering::SeqCst) {
                break;
            }

            // 暂停检查
            while self.is_paused.load(Ordering::SeqCst) {
                tokio::time::sleep(Duration::from_millis(300)).await;
                if self.is_cancelled.load(Ordering::SeqCst) {
                    break;
                }
            }

            let permit = match self.semaphore.clone().acquire_owned().await {
                Ok(p) => p,
                Err(_) => break,
            };

            let app = app_handle.clone();
            let cli = client.clone();
            let db = db_manager.clone();
            let rec = record.clone();
            let exp_dir = export_dir.clone();
            let tpl = naming_template.clone();
            let fmts = formats_to_export.clone();
            let comp_arc = completed.clone();
            let fail_arc = failed.clone();

            tokio::spawn(async move {
                let _permit = permit;

                // 注入随机延时抖动 (300~700ms) 保护用户 IP
                let jitter = 300 + (rec.activity_id % 400) as u64;
                tokio::time::sleep(Duration::from_millis(jitter)).await;

                // 广播开始下载事件
                let _ = app.emit(
                    "export:task-progress",
                    TaskProgressEvent {
                        activity_id: rec.activity_id,
                        status: "downloading".to_string(),
                        progress_percent: 20,
                        saved_path: None,
                        error_message: None,
                    },
                );

                let mut last_saved_path = None;
                let mut has_error = false;
                let mut err_msg = None;

                for fmt in &fmts {
                    let download_url = if fmt == "tcx" {
                        GarminEndpoints::tcx_download(cli.region, rec.activity_id)
                    } else {
                        GarminEndpoints::gpx_download(cli.region, rec.activity_id)
                    };

                    match Self::download_with_retry(&cli, &download_url).await {
                        Ok(bytes) => {
                            let ctx = TemplateContext {
                                activity_id: rec.activity_id,
                                activity_name: &rec.activity_name,
                                activity_type: &rec.activity_type,
                                start_time_local: &rec.start_time_local,
                                distance_meters: rec.distance_meters,
                            };
                            let filename = TemplateEngine::build_filename(
                                if tpl.is_empty() { DEFAULT_NAMING_TEMPLATE } else { &tpl },
                                &ctx,
                                fmt,
                            );

                            match FileWriter::save_flat_file(&exp_dir, &filename, &bytes, override_existing) {
                                Ok(saved) => {
                                    let path_str = saved.file_path.to_string_lossy().to_string();
                                    last_saved_path = Some(path_str.clone());

                                    // 同步更新 SQLite 导出记录
                                    if let Ok(conn) = db.conn.lock() {
                                        let _ = HistoryRepo::mark_as_exported(
                                            &conn,
                                            rec.activity_id,
                                            fmt,
                                            &path_str,
                                            saved.file_size_bytes,
                                            saved.checksum_md5.as_deref(),
                                        );
                                    }
                                }
                                Err(e) => {
                                    has_error = true;
                                    err_msg = Some(e.to_string());
                                }
                            }
                        }
                        Err(e) => {
                            has_error = true;
                            err_msg = Some(e.to_string());
                        }
                    }
                }

                if has_error {
                    let mut f = fail_arc.lock().await;
                    *f += 1;
                    let _ = app.emit(
                        "export:task-progress",
                        TaskProgressEvent {
                            activity_id: rec.activity_id,
                            status: "failed".to_string(),
                            progress_percent: 100,
                            saved_path: None,
                            error_message: err_msg,
                        },
                    );
                } else {
                    let mut c = comp_arc.lock().await;
                    *c += 1;
                    let _ = app.emit(
                        "export:task-progress",
                        TaskProgressEvent {
                            activity_id: rec.activity_id,
                            status: "saved".to_string(),
                            progress_percent: 100,
                            saved_path: last_saved_path,
                            error_message: None,
                        },
                    );
                }

                // 广播全局摘要
                let c_val = *comp_arc.lock().await;
                let f_val = *fail_arc.lock().await;
                let rem = total_tasks.saturating_sub(c_val + f_val);
                let _ = app.emit(
                    "export:queue-summary",
                    QueueSummaryEvent {
                        total_tasks,
                        completed_tasks: c_val,
                        failed_tasks: f_val,
                        remaining_tasks: rem,
                        is_paused: false,
                    },
                );
            });
        }
    }

    /// 下载请求重试（遇到 429 触发指数退避）
    async fn download_with_retry(client: &SsoClient, url: &str) -> Result<Vec<u8>, String> {
        let max_retries = 3;
        let mut delay = Duration::from_secs(3);

        for attempt in 1..=max_retries {
            let res = client
                .http_client
                .get(url)
                .header("Referer", format!("{}/modern", client.region.connect_host()))
                .send()
                .await
                .map_err(|e| e.to_string())?;

            let status = res.status();
            if status.is_success() {
                let bytes = res.bytes().await.map_err(|e| e.to_string())?;
                return Ok(bytes.to_vec());
            }

            if status.as_u16() == 429 && attempt < max_retries {
                // 指数退避 3s -> 9s
                tokio::time::sleep(delay).await;
                delay *= 3;
                continue;
            }

            return Err(format!("Download failed with status: {}", status));
        }

        Err("Exceeded maximum download retry attempts".to_string())
    }
}

#[cfg(test)]
pub mod tests {
    use super::*;

    #[test]
    fn test_queue_controls() {
        let queue = ExportQueue::new(2);
        assert!(!queue.is_paused.load(Ordering::SeqCst));
        assert!(!queue.is_cancelled.load(Ordering::SeqCst));

        queue.pause();
        assert!(queue.is_paused.load(Ordering::SeqCst));

        queue.resume();
        assert!(!queue.is_paused.load(Ordering::SeqCst));

        queue.cancel();
        assert!(queue.is_cancelled.load(Ordering::SeqCst));
    }
}
