use crate::auth::keychain;
use crate::auth::session::AuthManager;
use crate::auth::sso::{GarminRegion, LoginState, SsoClient, UserProfile};
use serde::{Deserialize, Serialize};
use tauri::{AppHandle, Emitter, Manager, State, WebviewUrl, WebviewWindowBuilder};
use url::Url;

#[derive(Debug, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct LoginPayload {
    pub region: String,
    pub email: String,
    pub password: String,
    pub remember_me: bool,
}

#[derive(Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct AuthResponse {
    pub success: bool,
    pub require_mfa: bool,
    pub user: Option<UserProfile>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub error: Option<String>,
}

#[derive(Debug, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct MfaPayload {
    pub code: String,
}

#[derive(Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct CurrentUserResponse {
    pub is_logged_in: bool,
    pub user: Option<UserProfile>,
}

#[tauri::command]
pub async fn auth_login(
    payload: LoginPayload,
    auth_manager: State<'_, AuthManager>,
) -> Result<AuthResponse, String> {
    let region = match payload.region.to_lowercase().as_str() {
        "cn" | "china" => GarminRegion::Cn,
        _ => GarminRegion::Global,
    };

    let client = SsoClient::new(region);

    match client.login(&payload.email, &payload.password).await {
        Ok(LoginState::Success { ticket: _ }) => {
            let user = client.fetch_user_profile().await.unwrap_or_else(|_| UserProfile {
                display_name: payload.email.split('@').next().unwrap_or("GarminUser").to_string(),
                full_name: None,
                profile_image_url: None,
                region,
            });

            // 存储钥匙串凭据
            if payload.remember_me {
                let _ = keychain::save_credential(&payload.email, &payload.password);
            }

            auth_manager.set_logged_in(client, user.clone()).await;

            Ok(AuthResponse {
                success: true,
                require_mfa: false,
                user: Some(user),
                error: None,
            })
        }
        Ok(LoginState::NeedMfa { csrf_token, flow_ticket }) => {
            auth_manager.set_pending_mfa(region, csrf_token, flow_ticket).await;

            Ok(AuthResponse {
                success: false,
                require_mfa: true,
                user: None,
                error: None,
            })
        }
        Ok(LoginState::Failed(msg)) => Ok(AuthResponse {
            success: false,
            require_mfa: false,
            user: None,
            error: Some(msg),
        }),
        Err(err) => Ok(AuthResponse {
            success: false,
            require_mfa: false,
            user: None,
            error: Some(err.to_string()),
        }),
    }
}

/// 打开原生 Webview 登录窗口（天生支持 Cloudflare Turnstile 验证码人机交互与官方 SSO 握手）
#[tauri::command]
pub async fn auth_open_web_login(
    app: AppHandle,
    region: String,
    auth_manager: State<'_, AuthManager>,
) -> Result<(), String> {
    let reg = if region == "cn" { GarminRegion::Cn } else { GarminRegion::Global };
    let signin_url = reg.portal_signin_url();
    let parsed_url: Url = signin_url.parse().map_err(|e: url::ParseError| e.to_string())?;

    // 如果之前已有残留登录窗口，直接重新导航并置顶显示
    if let Some(existing) = app.get_webview_window("garmin_sso_window") {
        let _ = existing.navigate(parsed_url);
        let _ = existing.show();
        let _ = existing.set_focus();
        return Ok(());
    }

    let is_handled = std::sync::Arc::new(std::sync::atomic::AtomicBool::new(false));
    let app_handle_for_load = app.clone();
    let auth_mgr_for_load = (*auth_manager).clone();
    let is_handled_for_load = is_handled.clone();

    let window = WebviewWindowBuilder::new(
        &app,
        "garmin_sso_window",
        WebviewUrl::External(parsed_url),
    )
    .title(if reg == GarminRegion::Cn { "Garmin 中国区官方登录" } else { "Garmin Global 官方登录" })
    .inner_size(480.0, 720.0)
    .resizable(true)
    .center()
    .user_agent("Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/133.0.0.0 Safari/537.36")
    .on_navigation(move |nav_url| {
        let u = nav_url.as_str();
        eprintln!("SSO Window Navigating: {}", u);
        true
    })
    .on_page_load(move |win, payload| {
        if payload.event() == tauri::webview::PageLoadEvent::Finished {
            let u = payload.url().as_str();
            eprintln!("SSO Window Page: Finished loading {}", u);

            let is_on_dashboard = if let Ok(parsed) = Url::parse(u) {
                let path = parsed.path();
                (path.starts_with("/modern") || path.starts_with("/app"))
                    && !path.contains("signin")
                    && !path.contains("sso")
                    && !u.contains("service=")
            } else {
                false
            };

            if is_on_dashboard && !is_handled_for_load.swap(true, std::sync::atomic::Ordering::SeqCst) {
                let app_in = app_handle_for_load.clone();
                let auth_in = auth_mgr_for_load.clone();
                let win_clone = win.clone();
                let is_handled_reset = is_handled_for_load.clone();

                tauri::async_runtime::spawn(async move {
                    // 等待 800ms 确保重定向 Cookie 全部写入 WebKit CookieJar
                    tokio::time::sleep(std::time::Duration::from_millis(800)).await;

                    let mut cookie_pairs = Vec::new();
                    let mut jwt_token = None;
                    if let Ok(cookies) = win_clone.cookies() {
                        for c in cookies {
                            cookie_pairs.push(format!("{}={}", c.name(), c.value()));
                            if c.name() == "JWT_WEB" {
                                jwt_token = Some(c.value().to_string());
                            }
                        }
                    }

                    let full_cookie_str = cookie_pairs.join("; ");
                    eprintln!(">>> Webview cookies captured: count={}, len={}", cookie_pairs.len(), full_cookie_str.len());

                    if !cookie_pairs.is_empty() {
                        let client = SsoClient::new(reg).with_tokens(
                            jwt_token.clone(),
                            Some(full_cookie_str.clone()),
                            None,
                        );

                        if let Ok(target_url) = reg.connect_host().parse::<Url>() {
                            for pair in &cookie_pairs {
                                client.cookie_store.add_cookie_str(pair, &target_url);
                            }
                        }

                        let profile = client.fetch_user_profile().await.unwrap_or_else(|e| {
                            eprintln!("Warning: fetch_user_profile error: {}, using fallback", e);
                            UserProfile {
                                display_name: "GarminUser".to_string(),
                                full_name: None,
                                profile_image_url: None,
                                region: reg,
                            }
                        });

                        let _ = keychain::save_credential("session_cookie", &full_cookie_str);
                        if let Some(ref jwt) = jwt_token {
                            let _ = keychain::save_credential("jwt_token", jwt);
                        }
                        let _ = keychain::save_credential("region", if reg == GarminRegion::Cn { "cn" } else { "global" });

                        auth_in.set_logged_in(client, profile.clone()).await;
                        let _ = app_in.emit("auth:login-success", profile);
                        let _ = win_clone.close();
                        eprintln!(">>> LOGIN SUCCEEDED & SSO Window Closed!");
                    } else {
                        is_handled_reset.store(false, std::sync::atomic::Ordering::SeqCst);
                    }
                });
            }
        }
    })
    .build()
    .map_err(|e| e.to_string())?;

    let _ = window.show();
    Ok(())
}

/// 支持通过 Session Cookie 字符串直接认证（类似于 API Key 模式，绕过所有人机验证）
#[tauri::command]
pub async fn auth_login_with_token(
    region: String,
    cookie_str: String,
    auth_manager: State<'_, AuthManager>,
) -> Result<AuthResponse, String> {
    let reg = if region == "cn" { GarminRegion::Cn } else { GarminRegion::Global };
    let mut jwt_opt = None;
    for part in cookie_str.split(';') {
        let trimmed = part.trim();
        if trimmed.starts_with("JWT_WEB=") {
            jwt_opt = Some(trimmed.trim_start_matches("JWT_WEB=").to_string());
        }
    }

    let client = SsoClient::new(reg).with_tokens(jwt_opt.clone(), Some(cookie_str.clone()), None);

    let url: Url = reg.connect_host().parse().map_err(|e: url::ParseError| e.to_string())?;
    for part in cookie_str.split(';') {
        let trimmed = part.trim();
        if !trimmed.is_empty() {
            client.cookie_store.add_cookie_str(trimmed, &url);
        }
    }

    match client.fetch_user_profile().await {
        Ok(user) => {
            let _ = keychain::save_credential("session_cookie", &cookie_str);
            if let Some(ref jwt) = jwt_opt {
                let _ = keychain::save_credential("jwt_token", jwt);
            }
            auth_manager.set_logged_in(client, user.clone()).await;
            Ok(AuthResponse {
                success: true,
                require_mfa: false,
                user: Some(user),
                error: None,
            })
        }
        Err(_) => Ok(AuthResponse {
            success: false,
            require_mfa: false,
            user: None,
            error: Some("Cookie/Token 无效或已过期，请重新从浏览器复制".to_string()),
        }),
    }
}

#[tauri::command]
pub async fn auth_submit_mfa(
    payload: MfaPayload,
    auth_manager: State<'_, AuthManager>,
) -> Result<AuthResponse, String> {
    let pending = {
        let lock = auth_manager.pending_mfa.read().await;
        lock.clone()
    };

    let (region, csrf, flow_ticket) = match pending {
        Some(ctx) => ctx,
        None => {
            return Ok(AuthResponse {
                success: false,
                require_mfa: false,
                user: None,
                error: Some("未找到待验证的 MFA 会话，请重新发起登录".to_string()),
            });
        }
    };

    let client = SsoClient::new(region);
    match client.submit_mfa(&payload.code, &csrf, flow_ticket.as_deref()).await {
        Ok(LoginState::Success { ticket: _ }) => {
            let user = UserProfile {
                display_name: "GarminUser".to_string(),
                full_name: None,
                profile_image_url: None,
                region,
            };

            auth_manager.set_logged_in(client, user.clone()).await;

            Ok(AuthResponse {
                success: true,
                require_mfa: false,
                user: Some(user),
                error: None,
            })
        }
        Ok(LoginState::NeedMfa { .. }) => Ok(AuthResponse {
            success: false,
            require_mfa: true,
            user: None,
            error: Some("MFA 验证码已被拒绝，请重新核对输入".to_string()),
        }),
        Ok(LoginState::Failed(msg)) => Ok(AuthResponse {
            success: false,
            require_mfa: false,
            user: None,
            error: Some(msg),
        }),
        Err(err) => Ok(AuthResponse {
            success: false,
            require_mfa: false,
            user: None,
            error: Some(err.to_string()),
        }),
    }
}

#[tauri::command]
pub async fn auth_get_current_user(
    auth_manager: State<'_, AuthManager>,
) -> Result<CurrentUserResponse, String> {
    let mut user = auth_manager.get_current_user().await;

    // 若内存中暂无会话，尝试从本地安全存储恢复已持久化的 session_cookie 与 jwt_token
    if user.is_none() {
        if let Ok(Some(cookie_str)) = keychain::get_credential("session_cookie") {
            let reg = if let Ok(Some(r)) = keychain::get_credential("region") {
                if r == "cn" { GarminRegion::Cn } else { GarminRegion::Global }
            } else {
                GarminRegion::Global
            };
            let jwt_opt = keychain::get_credential("jwt_token").ok().flatten();
            let csrf_opt = keychain::get_credential("csrf_token").ok().flatten();
            let client = SsoClient::new(reg).with_tokens(jwt_opt, Some(cookie_str.clone()), csrf_opt);
            if let Ok(url) = reg.connect_host().parse::<Url>() {
                for part in cookie_str.split(';') {
                    let trimmed = part.trim();
                    if !trimmed.is_empty() {
                        client.cookie_store.add_cookie_str(trimmed, &url);
                    }
                }
            }
            let profile = client.fetch_user_profile().await.unwrap_or_else(|_| UserProfile {
                display_name: "GarminUser".to_string(),
                full_name: None,
                profile_image_url: None,
                region: reg,
            });
            auth_manager.set_logged_in(client, profile.clone()).await;
            user = Some(profile);
        }
    }

    Ok(CurrentUserResponse {
        is_logged_in: user.is_some(),
        user,
    })
}

#[tauri::command]
pub async fn auth_logout(
    clear_saved_credentials: Option<bool>,
    account: Option<String>,
    auth_manager: State<'_, AuthManager>,
) -> Result<serde_json::Value, String> {
    auth_manager.clear_session().await;

    if clear_saved_credentials.unwrap_or(false) {
        if let Some(acc) = account {
            let _ = keychain::delete_credential(&acc);
        }
        let _ = keychain::delete_credential("session_cookie");
        let _ = keychain::delete_credential("jwt_token");
        let _ = keychain::delete_credential("csrf_token");
        let _ = keychain::delete_credential("region");
    }

    Ok(serde_json::json!({ "success": true }))
}
