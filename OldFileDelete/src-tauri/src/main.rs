#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

mod commands;
mod core;
mod models;
mod storage;

use std::path::PathBuf;
use std::time::Duration;
use tauri::{
    image::Image,
    menu::{CheckMenuItem, Menu, MenuItem, PredefinedMenuItem},
    tray::TrayIconBuilder,
    Emitter, Manager, WindowEvent,
};
use tokio::sync::mpsc;

use commands::*;
use core::{spawn_hourly_heartbeat, FsEventsWatcher, TrashController, WaterfallEngine};
use storage::ConfigStore;

fn main() {
    let store = ConfigStore::new();

    tauri::Builder::default()
        .manage(store)
        .setup(|app| {
            // Build Tray Menu items as required by specification:
            // 1. 打开主界面
            // 2. ✓ 开机自启动
            // 3. 彻底卸载本应用
            // 4. 退出应用
            let open_i = MenuItem::with_id(app, "open", "打开主界面", true, None::<&str>)?;
            let autostart_i = CheckMenuItem::with_id(app, "autostart", "✓ 开机自启动", true, true, None::<&str>)?;
            let separator = PredefinedMenuItem::separator(app)?;
            let uninstall_i = MenuItem::with_id(app, "uninstall", "彻底卸载本应用", true, None::<&str>)?;
            let quit_i = MenuItem::with_id(app, "quit", "退出应用", true, None::<&str>)?;

            let tray_menu = Menu::with_items(
                app,
                &[
                    &open_i,
                    &autostart_i,
                    &separator,
                    &uninstall_i,
                    &quit_i,
                ],
            )?;

            // Load 32x32 tray icon from embedded bytes
            let icon_bytes = include_bytes!("../icons/32x32.png");
            let icon = Image::from_bytes(icon_bytes)?;

            // Configure macOS pure menu bar tray icon (Left click directly opens popup menu)
            let _tray = TrayIconBuilder::with_id("main_tray")
                .icon(icon)
                .menu(&tray_menu)
                .show_menu_on_left_click(true)
                .on_menu_event(|app, event| {
                    match event.id.as_ref() {
                        "open" => {
                            if let Some(window) = app.get_webview_window("main") {
                                let _ = window.show();
                                let _ = window.set_focus();
                            }
                        }
                        "autostart" => {
                            println!("[FilePurger] Autostart menu clicked");
                        }
                        "uninstall" => {
                            if let Some(window) = app.get_webview_window("main") {
                                let _ = window.show();
                                let _ = window.set_focus();
                                let _ = window.emit("app:request_uninstall", ());
                            }
                        }
                        "quit" => {
                            app.exit(0);
                        }
                        _ => {}
                    }
                })
                .build(app)?;

            // Setup FSEvents Watcher with 3-second debounce
            let (scan_tx, mut scan_rx) = mpsc::channel::<PathBuf>(32);
            let mut watcher = FsEventsWatcher::new(Duration::from_secs(3), scan_tx)?;

            let store_state = app.state::<ConfigStore>();
            let cfg = store_state.get_config();
            let watch_paths: Vec<PathBuf> = cfg
                .folders
                .iter()
                .filter(|f| !f.is_trash)
                .map(|f| PathBuf::from(&f.path))
                .collect();
            let _ = watcher.sync_watched_folders(&watch_paths);

            // Spawn background task to listen for debounced scan triggers
            let app_handle = app.handle().clone();
            tauri::async_runtime::spawn(async move {
                while let Some(changed_path) = scan_rx.recv().await {
                    let store = app_handle.state::<ConfigStore>();
                    let current_cfg = store.get_config();

                    if let Some(folder) = current_cfg
                        .folders
                        .iter()
                        .find(|f| PathBuf::from(&f.path) == changed_path || changed_path.starts_with(&f.path))
                    {
                        let root_path = PathBuf::from(&folder.path);
                        if let Ok(res) = WaterfallEngine::execute_scan(&root_path, folder, false) {
                            let _ = app_handle.emit("app:match_list_update", &res.matched_list);
                            if !res.ask_items.is_empty() {
                                let _ = app_handle.emit("app:ask_queue_update", &res.ask_items);
                            }
                        }
                    }
                }
            });

            // Hourly Heartbeat
            let (hb_tx, mut hb_rx) = mpsc::channel::<()>(4);
            spawn_hourly_heartbeat(hb_tx);
            let app_handle_hb = app.handle().clone();
            tauri::async_runtime::spawn(async move {
                while let Some(()) = hb_rx.recv().await {
                    let store = app_handle_hb.state::<ConfigStore>();
                    let current_cfg = store.get_config();
                    for folder in &current_cfg.folders {
                        if !folder.is_trash {
                            let path = PathBuf::from(&folder.path);
                            let _ = WaterfallEngine::execute_scan(&path, folder, false);
                        }
                    }
                    if let Some(trash_dir) = TrashController::get_system_trash_path() {
                        let _ = TrashController::execute_trash_cleanup(&trash_dir, &current_cfg.trash_config, false);
                    }
                }
            });

            Ok(())
        })
        .on_window_event(|window, event| {
            if let WindowEvent::CloseRequested { api, .. } = event {
                // macOS status bar behavior: Close window hides window rather than quitting application
                api.prevent_close();
                let _ = window.hide();
            }
        })
        .invoke_handler(tauri::generate_handler![
            get_all_folders,
            add_folder,
            remove_folder,
            update_top_whitelist,
            save_rule,
            delete_rule,
            reorder_rules,
            get_trash_config,
            save_trash_config,
            trigger_scan_now,
            respond_ask_item,
            get_autostart_status,
            set_autostart,
            clean_uninstall,
        ])
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}
