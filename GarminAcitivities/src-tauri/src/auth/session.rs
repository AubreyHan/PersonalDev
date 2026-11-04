use std::sync::Arc;
use tokio::sync::RwLock;
use crate::auth::sso::{GarminRegion, SsoClient, UserProfile};

#[derive(Default, Clone)]
pub struct AuthManager {
    pub client: Arc<RwLock<Option<SsoClient>>>,
    pub current_user: Arc<RwLock<Option<UserProfile>>>,
    pub pending_mfa: Arc<RwLock<Option<(GarminRegion, String, Option<String>)>>>, // (region, csrf, flow_ticket)
}

impl AuthManager {
    pub fn new() -> Self {
        Self::default()
    }

    pub async fn set_logged_in(&self, client: SsoClient, user: UserProfile) {
        let mut client_lock = self.client.write().await;
        *client_lock = Some(client);

        let mut user_lock = self.current_user.write().await;
        *user_lock = Some(user);

        let mut mfa_lock = self.pending_mfa.write().await;
        *mfa_lock = None;
    }

    pub async fn set_pending_mfa(&self, region: GarminRegion, csrf: String, flow_ticket: Option<String>) {
        let mut mfa_lock = self.pending_mfa.write().await;
        *mfa_lock = Some((region, csrf, flow_ticket));
    }

    pub async fn clear_session(&self) {
        let mut client_lock = self.client.write().await;
        *client_lock = None;

        let mut user_lock = self.current_user.write().await;
        *user_lock = None;

        let mut mfa_lock = self.pending_mfa.write().await;
        *mfa_lock = None;
    }

    pub async fn get_current_user(&self) -> Option<UserProfile> {
        let user_lock = self.current_user.read().await;
        user_lock.clone()
    }
}
