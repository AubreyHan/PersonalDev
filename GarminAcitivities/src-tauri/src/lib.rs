pub mod auth;
pub mod commands;
pub mod db;
pub mod exporter;
pub mod garmin;

use auth::session::AuthManager;
use commands::activity_commands::activities_fetch_page;
use commands::auth_commands::{
    auth_get_current_user, auth_login, auth_login_with_token, auth_logout, auth_open_web_login,
    auth_submit_mfa,
};
use commands::export_commands::{
    export_control_queue, export_merge_activities, export_start_batch, export_start_incremental,
    settings_get, settings_update,
};
use db::DbManager;
use exporter::queue::ExportQueue;
use tauri::Manager;

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    let auth_manager = AuthManager::new();
    let export_queue = ExportQueue::default();

    tauri::Builder::default()
        .plugin(tauri_plugin_opener::init())
        .setup(|app| {
            // 解析用户应用数据目录中的 SQLite 数据库路径
            let app_data_dir = app.path().app_data_dir().unwrap_or_else(|_| std::path::PathBuf::from("."));
            let db_path = app_data_dir.join("metadata.db");
            let db_manager = DbManager::new_file(db_path)
                .unwrap_or_else(|_| DbManager::new_in_memory().expect("In-memory SQLite failed"));
            app.manage(db_manager);
            Ok(())
        })
        .manage(auth_manager)
        .manage(export_queue)
        .invoke_handler(tauri::generate_handler![
            auth_login,
            auth_open_web_login,
            auth_login_with_token,
            auth_submit_mfa,
            auth_get_current_user,
            auth_logout,
            activities_fetch_page,
            export_start_batch,
            export_start_incremental,
            export_merge_activities,
            export_control_queue,
            settings_get,
            settings_update,
        ])
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}
