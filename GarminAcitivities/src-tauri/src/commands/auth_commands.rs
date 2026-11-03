use crate::auth::keychain;
use crate::auth::session::AuthManager;
use crate::auth::sso::{GarminRegion, LoginState, SsoClient, UserProfile};
use regex::Regex;
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

    // 如果之前已有残留登录窗口，先关闭
    if let Some(win) = app.get_webview_window("garmin_sso_window") {
        let _ = win.close();
    }

    let app_handle = app.clone();
    let auth_mgr = (*auth_manager).clone();

    let window = WebviewWindowBuilder::new(
        &app,
        "garmin_sso_window",
        WebviewUrl::External(parsed_url),
    )
    .title(if reg == GarminRegion::Cn { "Garmin 中国区官方登录" } else { "Garmin Global 官方登录" })
    .inner_size(480.0, 720.0)
    .resizable(true)
    .center()
    .on_navigation(move |nav_url| {
        let url_str = nav_url.as_str();

        // 1. 拦截从 Webview 回传的 session cookie 与 profile
        if url_str.contains("tauri-auth-callback") {
            let app_inner = app_handle.clone();
            let auth_mgr_inner = auth_mgr.clone();
            let url_clone = nav_url.clone();

            tauri::async_runtime::spawn(async move {
                let mut cookie_val = String::new();
                let mut name_val = "GarminUser".to_string();

                for (k, v) in url_clone.query_pairs() {
                    if k == "cookie" {
                        cookie_val = v.to_string();
                    } else if k == "name" {
                        name_val = v.to_string();
                    }
                }

                if !cookie_val.is_empty() {
                    let client = SsoClient::new(reg);
                    if let Ok(target_url) = reg.connect_host().parse::<Url>() {
                        for part in cookie_val.split(';') {
                            let trimmed = part.trim();
                            if !trimmed.is_empty() {
                                client.cookie_store.add_cookie_str(trimmed, &target_url);
                            }
                        }
                    }

                    let user = UserProfile {
                        display_name: name_val,
                        full_name: None,
                        profile_image_url: None,
                        region: reg,
                    };

                    let _ = keychain::save_credential("session_cookie", &cookie_val);
                    auth_mgr_inner.set_logged_in(client, user.clone()).await;
                    let _ = app_inner.emit("auth:login-success", user);

                    if let Some(win) = app_inner.get_webview_window("garmin_sso_window") {
                        let _ = win.close();
                    }
                }
            });
            return false;
        }

        // 2. 拦截 ticket=ST-，利用后端 reqwest 独占消费服务凭据
        if url_str.contains("ticket=ST-") {
            let re = Regex::new(r"ticket=(ST-[a-zA-Z0-9_\-]+)").unwrap();
            if let Some(caps) = re.captures(url_str) {
                let ticket = caps[1].to_string();
                let app_inner = app_handle.clone();
                let auth_mgr_inner = auth_mgr.clone();

                tauri::async_runtime::spawn(async move {
                    let client = SsoClient::new(reg);
                    match client.consume_service_ticket(&ticket).await {
                        Ok(_) => {
                            let profile = client.fetch_user_profile().await.unwrap_or_else(|e| {
                                eprintln!("Failed to fetch profile: {:?}, fallback default", e);
                                UserProfile {
                                    display_name: "GarminUser".to_string(),
                                    full_name: None,
                                    profile_image_url: None,
                                    region: reg,
                                }
                            });
                            auth_mgr_inner.set_logged_in(client, profile.clone()).await;
                            let _ = app_inner.emit("auth:login-success", profile);
                            if let Some(win) = app_inner.get_webview_window("garmin_sso_window") {
                                let _ = win.close();
                            }
                        }
                        Err(err) => {
                            eprintln!("Failed to consume ticket: {:?}", err);
                        }
                    }
                });
                return false; // 必须拦截并取消 WebKit 导航，防止单次票据被前端并发消费作废
            }
        }
        true
    })
    .on_page_load(move |win, payload| {
        let c: () = win.cookies();
        if payload.event() == tauri::webview::PageLoadEvent::Finished {
            let u = payload.url().as_str();
            if (u.contains("/modern") || u.contains("/app")) && !u.contains("/signin") && !u.contains("/sso") {
                let _ = win.eval(r#"
                    (function() {
                        try {
                            const cookies = document.cookie || '';
                            if (cookies.includes('JWT_WEB') || cookies.includes('SESSIONID')) {
                                fetch('/modern/proxy/userprofile-service/userprofile/personal-information')
                                    .then(r => r.json())
                                    .then(d => {
                                        const name = d.displayName || d.userName || 'GarminUser';
                                        window.location.replace('https://connect.garmin.com/tauri-auth-callback?cookie=' + encodeURIComponent(cookies) + '&name=' + encodeURIComponent(name));
                                    })
                                    .catch(() => {
                                        window.location.replace('https://connect.garmin.com/tauri-auth-callback?cookie=' + encodeURIComponent(cookies) + '&name=GarminUser');
                                    });
                            }
                        } catch (e) {}
                    })();
                "#);
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
    let client = SsoClient::new(reg);

    let url: Url = reg.connect_host().parse().map_err(|e: url::ParseError| e.to_string())?;
    for part in cookie_str.split(';') {
        let trimmed = part.trim();
        if !trimmed.is_empty() {
            client.cookie_store.add_cookie_str(trimmed, &url);
        }
    }

    // 验证会话连通性
    let test_url = format!(
        "{}/modern/proxy/activitylist-service/activities/search/activities?start=0&limit=1",
        reg.connect_host()
    );
    let resp = client
        .http_client
        .get(&test_url)
        .header("Referer", format!("{}/modern", reg.connect_host()))
        .send()
        .await
        .map_err(|e| e.to_string())?;

    if resp.status().is_success() {
        let user = UserProfile {
            display_name: "GarminUser".to_string(),
            full_name: None,
            profile_image_url: None,
            region: reg,
        };
        let _ = keychain::save_credential("session_cookie", &cookie_str);
        auth_manager.set_logged_in(client, user.clone()).await;
        Ok(AuthResponse {
            success: true,
            require_mfa: false,
            user: Some(user),
            error: None,
        })
    } else {
        Ok(AuthResponse {
            success: false,
            require_mfa: false,
            user: None,
            error: Some("Cookie/Token 无效或已过期，请重新从浏览器复制".to_string()),
        })
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
    let user = auth_manager.get_current_user().await;
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
    }

    Ok(serde_json::json!({ "success": true }))
}
