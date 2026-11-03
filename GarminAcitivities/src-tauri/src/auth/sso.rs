use reqwest::header::{HeaderMap, HeaderValue, ACCEPT, ACCEPT_LANGUAGE, ORIGIN, REFERER, USER_AGENT};
use regex::Regex;
use serde::{Deserialize, Serialize};
use std::sync::Arc;
use thiserror::Error;

const DEFAULT_USER_AGENT: &str =
    "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/133.0.0.0 Safari/537.36";

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum GarminRegion {
    Cn,
    Global,
}

impl GarminRegion {
    pub fn sso_host(&self) -> &'static str {
        match self {
            GarminRegion::Cn => "https://sso.garmin.cn/sso",
            GarminRegion::Global => "https://sso.garmin.com/sso",
        }
    }

    pub fn connect_host(&self) -> &'static str {
        match self {
            GarminRegion::Cn => "https://connect.garmin.cn",
            GarminRegion::Global => "https://connect.garmin.com",
        }
    }

    pub fn tld(&self) -> &'static str {
        match self {
            GarminRegion::Cn => "cn",
            GarminRegion::Global => "com",
        }
    }

    pub fn portal_signin_url(&self) -> String {
        let tld = self.tld();
        format!(
            "https://connect.garmin.{tld}/signin/?service=https%3A%2F%2Fconnect.garmin.{tld}%2Fmodern"
        )
    }

    pub fn signin_url(&self) -> String {
        let tld = self.tld();
        format!(
            "{}/signin?service=https%3A%2F%2Fconnect.garmin.{tld}%2Fmodern&webhost=https%3A%2F%2Fconnect.garmin.{tld}%2Fmodern&source=https%3A%2F%2Fconnect.garmin.{tld}%2Fmodern&redirectAfterAccountLoginUrl=https%3A%2F%2Fconnect.garmin.{tld}%2Fmodern&redirectAfterAccountCreationUrl=https%3A%2F%2Fconnect.garmin.{tld}%2Fmodern&gauthHost=https%3A%2F%2Fsso.garmin.{tld}%2Fsso&locale=zh_CN&id=gauth-widget&cssUrl=https%3A%2F%2Fstatic.garmincdn.com%2Fcom.garmin.connect%2Fui%2Fcss%2Fgauth-custom-v1.2-min.css&privacyStatementUrl=https%3A%2F%2Fwww.garmin.{tld}%2Fzh-CN%2Fprivacy%2Fconnect%2F&clientId=GarminConnect&rememberMeShown=true&rememberMeChecked=true&createAccountShown=true&openCreateAccount=false&displayNameShown=false&consumeServiceTicket=false&initialDisplay=first_page",
            self.sso_host()
        )
    }

    pub fn verify_mfa_url(&self) -> String {
        format!("{}/verifyMfa/loginEnterMfaCode", self.sso_host())
    }
}

#[derive(Error, Debug)]
pub enum SsoError {
    #[error("Network request failed: {0}")]
    Network(#[from] reqwest::Error),
    #[error("Failed to extract CSRF token from Garmin SSO page")]
    CsrfMissing,
    #[error("Invalid username or password")]
    InvalidCredentials,
    #[error("Garmin anti-bot captcha/turnstile challenge triggered")]
    CaptchaBlocked,
    #[error("Failed to parse service ticket from authentication response")]
    TicketMissing,
    #[error("Authentication failed: {0}")]
    Other(String),
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct UserProfile {
    pub display_name: String,
    pub full_name: Option<String>,
    pub profile_image_url: Option<String>,
    pub region: GarminRegion,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub enum LoginState {
    Success { ticket: String },
    NeedMfa { csrf_token: String, flow_ticket: Option<String> },
    Failed(String),
}

#[derive(Clone)]
pub struct SsoClient {
    pub region: GarminRegion,
    pub http_client: reqwest::Client,
    pub cookie_store: Arc<reqwest::cookie::Jar>,
}

impl SsoClient {
    pub fn new(region: GarminRegion) -> Self {
        let cookie_store = Arc::new(reqwest::cookie::Jar::default());

        let mut headers = HeaderMap::new();
        headers.insert(USER_AGENT, HeaderValue::from_static(DEFAULT_USER_AGENT));
        headers.insert(
            ACCEPT,
            HeaderValue::from_static("text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,*/*;q=0.8"),
        );
        headers.insert(
            ACCEPT_LANGUAGE,
            HeaderValue::from_static("zh-CN,zh;q=0.9,en-US;q=0.8,en;q=0.7"),
        );

        let http_client = reqwest::Client::builder()
            .cookie_provider(Arc::clone(&cookie_store))
            .default_headers(headers)
            .redirect(reqwest::redirect::Policy::limited(10)) // 自动跟踪重定向以交换 ticket 并捕获后续 session cookies
            .build()
            .expect("Failed to initialize reqwest client");

        Self {
            region,
            http_client,
            cookie_store,
        }
    }

    /// 第一阶段：GET /sso/signin，抓取 CSRF Token 与初始 Session Cookie
    pub async fn fetch_csrf_token(&self) -> Result<String, SsoError> {
        let url = self.region.signin_url();
        let response = self.http_client.get(&url).send().await?;
        let status = response.status();
        let body = response.text().await?;

        if !status.is_success() && status.as_u16() != 302 {
            return Err(SsoError::Other(format!("Unexpected HTTP status when getting signin page: {}", status)));
        }

        Self::parse_csrf_token(&body).ok_or(SsoError::CsrfMissing)
    }

    /// 第二阶段：POST /sso/signin 提交账号凭据
    pub async fn login(&self, username: &str, password: &str) -> Result<LoginState, SsoError> {
        let csrf_token = self.fetch_csrf_token().await?;
        let signin_url = self.region.signin_url();

        let params = [
            ("username", username),
            ("password", password),
            ("embed", "true"),
            ("_csrf", &csrf_token),
            ("rememberme", "on"),
        ];

        let response = self
            .http_client
            .post(&signin_url)
            .header(ORIGIN, self.region.sso_host())
            .header(REFERER, &signin_url)
            .header("Content-Type", "application/x-www-form-urlencoded")
            .form(&params)
            .send()
            .await?;

        let status = response.status();
        let headers = response.headers().clone();
        let body = response.text().await?;

        // 检查是否触发验证码拦截
        if body.contains("captcha") || body.contains("cf-turnstile") || body.contains("recaptcha") {
            return Err(SsoError::CaptchaBlocked);
        }

        // 检查是否账号密码错误
        if body.contains("Invalid username/password")
            || body.contains("用户名或密码错误")
            || body.contains("errorMessage") && body.contains("USERNAME_OR_PASSWORD_INCORRECT")
        {
            return Err(SsoError::InvalidCredentials);
        }

        // 检查是否需要 MFA 二步验证
        if Self::is_mfa_required(&body) {
            let flow_ticket = Self::extract_mfa_flow_ticket(&body);
            let next_csrf = Self::parse_csrf_token(&body).unwrap_or(csrf_token);
            return Ok(LoginState::NeedMfa {
                csrf_token: next_csrf,
                flow_ticket,
            });
        }

        // 检查成功 Ticket (可能来自 302 Location 或 response HTML 脚本)
        if let Some(ticket) = Self::extract_ticket_from_response(&body, &headers) {
            // 消费 ticket 并激活现代网关 session
            self.consume_service_ticket(&ticket).await?;
            return Ok(LoginState::Success { ticket });
        }

        // 未匹配到已知路径，返回失败
        Err(SsoError::Other(format!(
            "Failed to complete login flow. HTTP status: {}, body preview: {}",
            status,
            body.chars().take(200).collect::<String>()
        )))
    }

    /// 第三阶段（若需）：提交 MFA 6 位数字验证码
    pub async fn submit_mfa(
        &self,
        mfa_code: &str,
        csrf_token: &str,
        flow_ticket: Option<&str>,
    ) -> Result<LoginState, SsoError> {
        let verify_url = self.region.verify_mfa_url();

        let mut params = vec![
            ("mfaCode", mfa_code),
            ("_csrf", csrf_token),
            ("embed", "true"),
        ];
        if let Some(ft) = flow_ticket {
            params.push(("fromPage", ft));
        }

        let response = self
            .http_client
            .post(&verify_url)
            .header(ORIGIN, self.region.sso_host())
            .header(REFERER, self.region.signin_url())
            .form(&params)
            .send()
            .await?;

        let headers = response.headers().clone();
        let body = response.text().await?;

        if let Some(ticket) = Self::extract_ticket_from_response(&body, &headers) {
            self.consume_service_ticket(&ticket).await?;
            return Ok(LoginState::Success { ticket });
        }

        if body.contains("Invalid") || body.contains("错误") {
            return Err(SsoError::Other("Invalid MFA verification code".to_string()));
        }

        Err(SsoError::TicketMissing)
    }

    /// 消费 Service Ticket：调用 /modern?ticket=ST-xxxx，写入 Connect 核心 JWT/Cookie
    pub async fn consume_service_ticket(&self, ticket: &str) -> Result<(), SsoError> {
        let modern_url = format!("{}/modern?ticket={}", self.region.connect_host(), ticket);
        let resp = self
            .http_client
            .get(&modern_url)
            .header(REFERER, self.region.signin_url())
            .send()
            .await?;

        let status = resp.status();
        if !status.is_success() && status.as_u16() != 302 {
            // 回退尝试 /app?ticket= 端点
            let app_url = format!("{}/app?ticket={}", self.region.connect_host(), ticket);
            let app_resp = self
                .http_client
                .get(&app_url)
                .header(REFERER, self.region.signin_url())
                .send()
                .await?;
            if !app_resp.status().is_success() && app_resp.status().as_u16() != 302 {
                return Err(SsoError::Other(format!(
                    "Failed to consume ticket: modern status {}, app status {}",
                    status,
                    app_resp.status()
                )));
            }
        }
        Ok(())
    }

    /// 拉取 Garmin 真实个人信息与显示名称
    pub async fn fetch_user_profile(&self) -> Result<UserProfile, SsoError> {
        let profile_url = format!(
            "{}/modern/proxy/userprofile-service/userprofile/personal-information",
            self.region.connect_host()
        );
        let resp = self
            .http_client
            .get(&profile_url)
            .header(REFERER, format!("{}/modern", self.region.connect_host()))
            .header("Accept", "application/json, text/plain, */*")
            .send()
            .await?;

        if !resp.status().is_success() {
            return Err(SsoError::Other(format!(
                "Failed to fetch profile: HTTP {}",
                resp.status()
            )));
        }

        #[derive(Deserialize)]
        #[serde(rename_all = "camelCase")]
        struct RawProfile {
            display_name: Option<String>,
            user_name: Option<String>,
            full_name: Option<String>,
            profile_image_url_large: Option<String>,
            profile_image_url_medium: Option<String>,
        }

        let raw: RawProfile = resp.json().await?;
        let display_name = raw
            .display_name
            .or(raw.user_name)
            .unwrap_or_else(|| "GarminUser".to_string());

        Ok(UserProfile {
            display_name,
            full_name: raw.full_name,
            profile_image_url: raw.profile_image_url_large.or(raw.profile_image_url_medium),
            region: self.region,
        })
    }

    /// 从 HTML 中提取 CSRF Token
    pub fn parse_csrf_token(html: &str) -> Option<String> {
        // 匹配 name="_csrf" value="..." 或 value="..." name="_csrf"
        let re1 = Regex::new(r#"name=["']_csrf["']\s+value=["']([^"']+)["']"#).ok()?;
        if let Some(caps) = re1.captures(html) {
            return Some(caps[1].to_string());
        }

        let re2 = Regex::new(r#"value=["']([^"']+)["']\s+name=["']_csrf["']"#).ok()?;
        if let Some(caps) = re2.captures(html) {
            return Some(caps[1].to_string());
        }

        // 匹配 JavaScript 变量 var _csrf = "...";
        let re3 = Regex::new(r#"["']_csrf["']\s*:\s*["']([^"']+)["']"#).ok()?;
        if let Some(caps) = re3.captures(html) {
            return Some(caps[1].to_string());
        }

        None
    }

    /// 从响应头或 HTML 正文中提取 Service Ticket (ST-xxxxxx)
    pub fn extract_ticket_from_response(body: &str, headers: &HeaderMap) -> Option<String> {
        // 1. 检查 Location 头部重定向 URL
        if let Some(loc) = headers.get(reqwest::header::LOCATION) {
            if let Ok(loc_str) = loc.to_str() {
                if let Some(ticket) = Self::extract_ticket_from_str(loc_str) {
                    return Some(ticket);
                }
            }
        }

        // 2. 检查 HTML 正文中的 redirect 脚本或 response_url
        Self::extract_ticket_from_str(body)
    }

    fn extract_ticket_from_str(text: &str) -> Option<String> {
        let re = Regex::new(r#"ticket=(ST-[a-zA-Z0-9_\-]+)"#).ok()?;
        if let Some(caps) = re.captures(text) {
            return Some(caps[1].to_string());
        }

        let re_raw = Regex::new(r#"(ST-[0-9a-zA-Z\-]{20,})"#).ok()?;
        if let Some(caps) = re_raw.captures(text) {
            return Some(caps[1].to_string());
        }

        None
    }

    /// 检查 HTML 是否表明需进行 MFA 验证
    pub fn is_mfa_required(body: &str) -> bool {
        body.contains("NEED_MFA")
            || body.contains("loginEnterMfaCode")
            || body.contains("mfaCode")
            || body.contains("verification-code")
            || body.contains("输入验证码")
    }

    /// 提取 MFA 流程标识 ticket
    pub fn extract_mfa_flow_ticket(body: &str) -> Option<String> {
        let re = Regex::new(r#"name=["']fromPage["']\s+value=["']([^"']+)["']"#).ok()?;
        re.captures(body).map(|c| c[1].to_string())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_garmin_region_endpoints() {
        let cn = GarminRegion::Cn;
        assert_eq!(cn.sso_host(), "https://sso.garmin.cn/sso");
        assert_eq!(cn.connect_host(), "https://connect.garmin.cn");
        assert!(cn.signin_url().contains("garmin.cn"));

        let global = GarminRegion::Global;
        assert_eq!(global.sso_host(), "https://sso.garmin.com/sso");
        assert_eq!(global.connect_host(), "https://connect.garmin.com");
        assert!(global.signin_url().contains("garmin.com"));
    }

    #[test]
    fn test_parse_csrf_token() {
        let sample_html_1 = r#"
            <html><body>
                <input type="hidden" name="_csrf" value="abc123csrf_token_xyz" />
            </body></html>
        "#;
        assert_eq!(
            SsoClient::parse_csrf_token(sample_html_1),
            Some("abc123csrf_token_xyz".to_string())
        );

        let sample_html_2 = r#"
            <input type="hidden" value="token_val_456" name="_csrf">
        "#;
        assert_eq!(
            SsoClient::parse_csrf_token(sample_html_2),
            Some("token_val_456".to_string())
        );

        let sample_html_json = r#"
            <script>window.gauth = { '_csrf': 'json_csrf_token_789' };</script>
        "#;
        assert_eq!(
            SsoClient::parse_csrf_token(sample_html_json),
            Some("json_csrf_token_789".to_string())
        );

        let sample_none = "<html><body>No csrf here</body></html>";
        assert_eq!(SsoClient::parse_csrf_token(sample_none), None);
    }

    #[test]
    fn test_extract_ticket_from_headers_and_body() {
        let mut headers = HeaderMap::new();
        headers.insert(
            reqwest::header::LOCATION,
            HeaderValue::from_static("https://connect.garmin.cn/modern?ticket=ST-12345678-ABCD-EFGH-IJKL"),
        );

        let ticket = SsoClient::extract_ticket_from_response("", &headers);
        assert_eq!(ticket, Some("ST-12345678-ABCD-EFGH-IJKL".to_string()));

        // 从 HTML 脚本中提取
        let body_with_ticket = r#"
            <script type="text/javascript">
                var response_url = "https://connect.garmin.com/modern?ticket=ST-98765432-ZYXW-VUTS-RQPO";
                window.location.replace(response_url);
            </script>
        "#;
        let empty_headers = HeaderMap::new();
        let ticket_from_body = SsoClient::extract_ticket_from_response(body_with_ticket, &empty_headers);
        assert_eq!(ticket_from_body, Some("ST-98765432-ZYXW-VUTS-RQPO".to_string()));
    }

    #[test]
    fn test_mfa_detection() {
        let html_need_mfa = r#"
            <div id="mfa-container">
                <input type="text" name="mfaCode" id="mfaCode" />
                <input type="hidden" name="fromPage" value="flow_ticket_999" />
            </div>
        "#;
        assert!(SsoClient::is_mfa_required(html_need_mfa));
        assert_eq!(
            SsoClient::extract_mfa_flow_ticket(html_need_mfa),
            Some("flow_ticket_999".to_string())
        );

        let html_normal = "<div>Welcome to Garmin</div>";
        assert!(!SsoClient::is_mfa_required(html_normal));
    }
}
