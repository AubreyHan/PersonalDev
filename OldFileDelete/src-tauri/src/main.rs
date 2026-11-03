#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

mod core;
mod models;

use tauri::{
    image::Image,
    menu::{CheckMenuItem, Menu, MenuItem, PredefinedMenuItem},
    tray::TrayIconBuilder,
    Emitter, Manager, WindowEvent,
};

fn main() {
    tauri::Builder::default()
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
                            // Handled in Phase 6 via SMAppService
                            println!("[FilePurger] Autostart menu clicked");
                        }
                        "uninstall" => {
                            // Request uninstall via event to frontend / confirmation modal
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

            Ok(())
        })
        .on_window_event(|window, event| {
            if let WindowEvent::CloseRequested { api, .. } = event {
                // macOS Status Bar App convention: Close window hides window rather than quitting application
                api.prevent_close();
                let _ = window.hide();
            }
        })
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}
