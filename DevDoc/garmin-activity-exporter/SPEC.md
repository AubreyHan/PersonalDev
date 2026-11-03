# 详细设计与规范 (SPEC) - Garmin Activity Exporter

## 1. Tauri IPC 接口规范 (Commands & Events)

### 1.1 Tauri Commands (Frontend -> Rust)

#### `auth_login`
- **说明**：执行 Garmin SSO 账号密码登录流程。
- **请求参数**：
  ```json
  {
    "region": "cn" | "global",
    "email": "user@example.com",
    "password": "user_password",
    "rememberMe": true
  }
  ```
- **返回结果**：
  ```json
  {
    "success": true,
    "requireMfa": false,
    "user": {
      "displayName": "RunnerAubrey",
      "fullName": "Aubrey Han",
      "profileImageUrl": "https://...",
      "region": "cn"
    }
  }
  ```
- **异常响应**：
  - `INVALID_CREDENTIALS`：账号或密码错误。
  - `CAPTCHA_BLOCKED`：触发 Garmin 官方反爬虫滑块/验证码。
  - `NETWORK_ERROR`：网络不可达。

#### `auth_submit_mfa`
- **说明**：当登录触发 MFA 二次验证时，提交 6 位验证码。
- **请求参数**：
  ```json
  {
    "code": "123456"
  }
  ```
- **返回结果**：同 `auth_login` 返回结构。

#### `auth_get_current_user`
- **说明**：应用启动时检查本地是否有有效会话或已保存凭据。
- **返回结果**：`{ "isLoggedIn": boolean, "user": UserProfile | null }`

#### `auth_logout`
- **说明**：注销登录，清空当前内存中的 CookieJar，可选择是否清除 Keyring 中的密码。
- **请求参数**：`{ "clearSavedCredentials": boolean }`
- **返回结果**：`{ "success": true }`

#### `activities_fetch_page`
- **说明**：从 Garmin 获取活动列表分页数据。
- **请求参数**：
  ```json
  {
    "start": 0,
    "limit": 50,
    "activityType": "all" | "running" | "cycling" | "swimming" | "hiking" | "walking" | "fitness_equipment", // 下拉菜单选择项
    "startDate": "2026-10-01", // 起始日期 (YYYY-MM-DD，闭区间包含当天)
    "endDate": "2026-11-03"    // 截止日期 (YYYY-MM-DD，闭区间包含当天)
  }
  ```
- **返回结果**：
  ```json
  {
    "totalCount": 420,
    "items": [
      {
        "activityId": 16892401234,
        "activityName": "朝阳公园晨跑 10K",
        "activityType": "running",
        "startTimeLocal": "2026-11-01 06:30:15",
        "distanceMeters": 10052.4,
        "durationSeconds": 3120,
        "averageSpeedMps": 3.22,
        "averageHeartRateBpm": 152,
        "maxHeartRateBpm": 168,
        "elevationGainMeters": 35.0,
        "isExported": true,
        "localFilePath": "/Users/aubreyhan/Garmin/2026-11-01_running_朝阳公园晨跑10K_16892401234.gpx"
      }
    ]
  }
  ```

#### `export_start_batch`
- **说明**：将一组选中的活动添加到导出队列并启动下载引擎。
- **请求参数**：
  ```json
  {
    "activityIds": [16892401234, 16892405678],
    "format": "gpx" | "tcx" | "both",
    "overrideExisting": false
  }
  ```
- **返回结果**：`{ "queuedCount": 2, "jobId": "job_uuid_1234" }`

#### `export_start_incremental`
- **说明**：自动比对云端与本地数据库，将所有未导出的活动加入队列。
- **请求参数**：`{ "format": "gpx" | "tcx" | "both" }`
- **返回结果**：`{ "queuedCount": 15, "jobId": "job_uuid_5678" }`

#### `export_merge_activities`
- **说明**：将选中的多个活动下载并按照时间顺序无损合并为一个 GPX 或 TCX 文件，保持轨迹点、高程与传感器扩展完整。
- **请求参数**：
  ```json
  {
    "activityIds": [16892401234, 16892405678],
    "format": "gpx" | "tcx",
    "customFileName": "2026-11-03_跑步_双桥半程马拉松合并.gpx" // 可选，若为空则按默认规则命名
  }
  ```
- **返回结果**：
  ```json
  {
    "success": true,
    "mergedFilePath": "/Users/aubreyhan/Downloads/Garmin_Activities/2026-11-03_跑步_双桥半程马拉松合并.gpx",
    "totalPoints": 3420,
    "totalDurationSeconds": 7240,
    "totalDistanceMeters": 21097.5
  }
  ```

#### `export_control_queue`
- **说明**：暂停、恢复或取消导出队列。
- **请求参数**：`{ "action": "pause" | "resume" | "cancel" }`
- **返回结果**：`{ "currentStatus": "paused" | "running" | "idle" }`

#### `settings_get` / `settings_update`
- **说明**：读取或更新导出路径、命名模板与归档模式设置。
- **设置对象格式**：
  ```json
  {
    "exportDirectory": "/Users/aubreyhan/GarminExport",
    "defaultFormat": "gpx",
    "namingTemplate": "{date}_{type}_{name}_{id}", // 包含活动ID，确保平铺目录下无重名冲突
    "concurrency": 2,
    "delayJitterMs": 500
  }
  ```

---

### 1.2 Tauri Events (Rust -> Frontend 推送)

- **`export:task-progress`**：单任务下载进度更新。
  ```json
  {
    "activityId": 16892401234,
    "status": "downloading" | "saved" | "failed",
    "progressPercent": 85,
    "savedPath": null,
    "errorMessage": null
  }
  ```
- **`export:queue-summary`**：全局队列吞吐更新。
  ```json
  {
    "totalTasks": 20,
    "completedTasks": 12,
    "failedTasks": 1,
    "remainingTasks": 7,
    "isPaused": false
  }
  ```
- **`auth:session-expired`**：Token 过期且自动刷新失败通知。

---

## 2. Garmin API 端点映射规范 (Endpoints Mapping)

| 功能 | Global 端点 (国际区) | China 端点 (中国区) | HTTP Method | 备注 |
| :--- | :--- | :--- | :--- | :--- |
| **SSO 登录页** | `https://sso.garmin.com/sso/signin` | `https://sso.garmin.cn/sso/signin` | GET / POST | 参数 `service=https://connect.garmin.com(cn)/modern` |
| **MFA 验证** | `https://sso.garmin.com/sso/verifyMfa/loginEnterMfaCode` | `https://sso.garmin.cn/sso/verifyMfa/loginEnterMfaCode` | POST | 提交 `mfa-code` |
| **用户画像** | `https://connect.garmin.com/modern/proxy/userprofile-service/userprofile/personal-information` | `https://connect.garmin.cn/modern/proxy/userprofile-service/userprofile/personal-information` | GET | 获取用户昵称与头像 |
| **活动列表查询** | `https://connect.garmin.com/modern/proxy/activitylist-service/activities/search/activities` | `https://connect.garmin.cn/modern/proxy/activitylist-service/activities/search/activities` | GET | 参数 `start=0&limit=50&activityType=...` |
| **GPX 轨迹导出** | `https://connect.garmin.com/modern/proxy/download-service/export/gpx/activity/{activityId}` | `https://connect.garmin.cn/modern/proxy/download-service/export/gpx/activity/{activityId}` | GET | 返回标准 XML GPX 文本流 |
| **TCX 轨迹导出** | `https://connect.garmin.com/modern/proxy/download-service/export/tcx/activity/{activityId}` | `https://connect.garmin.cn/modern/proxy/download-service/export/tcx/activity/{activityId}` | GET | 返回标准 XML TCX 文本流 |

---

## 3. 本地 SQLite 数据模型设计 (Database Schema)

数据文件默认存放于操作系统用户配置目录：
- macOS: `~/Library/Application Support/garmin-activity-exporter/metadata.db`
- Windows: `%APPDATA%\garmin-activity-exporter\metadata.db`

### 3.1 `activities` 表 (活动缓存表)
```sql
CREATE TABLE IF NOT EXISTS activities (
    activity_id INTEGER PRIMARY KEY,
    user_id TEXT NOT NULL,
    region TEXT NOT NULL,                -- 'cn' | 'global'
    activity_name TEXT NOT NULL,
    activity_type TEXT NOT NULL,
    start_time_local TEXT NOT NULL,     -- 'YYYY-MM-DD HH:MM:SS'
    distance_meters REAL DEFAULT 0.0,
    duration_seconds INTEGER DEFAULT 0,
    avg_speed_mps REAL DEFAULT 0.0,
    avg_hr_bpm INTEGER,
    max_hr_bpm INTEGER,
    elevation_gain_m REAL DEFAULT 0.0,
    raw_json TEXT,                      -- Garmin 原始返回全量快照
    updated_at DATETIME DEFAULT CURRENT_TIMESTAMP
);
CREATE INDEX IF NOT EXISTS idx_activities_time ON activities(start_time_local DESC);
CREATE INDEX IF NOT EXISTS idx_activities_type ON activities(activity_type);
```

### 3.2 `export_history` 表 (导出历史与本地文件关联表)
```sql
CREATE TABLE IF NOT EXISTS export_history (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    activity_id INTEGER NOT NULL,
    format TEXT NOT NULL,               -- 'gpx' | 'tcx'
    file_path TEXT NOT NULL,            -- 磁盘绝对路径
    file_size_bytes INTEGER NOT NULL,
    exported_at DATETIME DEFAULT CURRENT_TIMESTAMP,
    checksum_md5 TEXT,
    FOREIGN KEY(activity_id) REFERENCES activities(activity_id) ON DELETE CASCADE
);
CREATE UNIQUE INDEX IF NOT EXISTS idx_export_unique ON export_history(activity_id, format);
```

### 3.3 `app_settings` 表 (本地键值对配置表)
```sql
CREATE TABLE IF NOT EXISTS app_settings (
    key TEXT PRIMARY KEY,
    value TEXT NOT NULL
);
```

---

## 4. 文件命名模板引擎规约 (Template Syntax)

### 4.1 占位符支持列表
- `{date}`：格式为 `YYYY-MM-DD`（如 `2026-11-03`）
- `{compact_date}`：格式为 `YYYYMMDD`（如 `20261103`）
- `{time}`：格式为 `HHmm`（如 `0630`）
- `{id}`：Garmin 活动 ID（如 `16892401234`）
- `{type}`：运动类型本地化描述（如 `跑步`、`公路骑行`、`泳池游泳`）
- `{type_en}`：运动类型英文标识（如 `running`、`cycling`）
- `{name}`：活动标题（自动移除非法字符）
- `{dist_km}`：距离数值格式化为 1 位小数（如 `10.2km`）

### 4.2 字符净化算法 (Sanitization Rule)
```text
输入字符串 -> 过滤所有系统非法字符: [ \ / : * ? " < > | \0 ]
           -> 连续空格合并为单个下划线 '_'
           -> 去除首尾空白与句点
           -> 截断至最大长度 120 字符 (防止超出操作系统路径限制 255)
```
