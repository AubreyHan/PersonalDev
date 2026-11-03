use crate::auth::keychain;
use crate::auth::session::AuthManager;
use crate::auth::sso::{GarminRegion, LoginState, SsoClient, UserProfile};
use serde::{Deserialize, Serialize};
use tauri::State;

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
            let user = UserProfile {
                display_name: payload.email.split('@').next().unwrap_or("GarminUser").to_string(),
                full_name: None,
                profile_image_url: None,
                region,
            };

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
                error: Some("No pending MFA challenge found. Please log in again.".to_string()),
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
            error: Some("MFA code verification rejected. Please retry.".to_string()),
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
    }

    Ok(serde_json::json!({ "success": true }))
}
