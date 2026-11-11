# 详细设计与规范 (SPEC) - Garmin Activity Exporter (v1)

## 1. Garmin Connect Personal API 对接规范

### 1.1 服务端点 (Base URLs)

| 区域枚举 (`GarminRegion`) | Base URL | 说明 |
| :--- | :--- | :--- |
| `.global` (国际区) | `https://personal.connectapi.garmin.com` | 国际区账号网关 |
| `.china` (中国区) | `https://personal.connectapi.garmin.cn` | 中国区账号网关 |

### 1.2 换取 Bearer Token 与区域探测接口

- **Endpoint**：`POST /personalapitoken-service/personal-api/personal-api-key/token`
- **Request Headers**：
  ```http
  Authorization: ApiKey <user-personal-api-key>
  Accept: */*
  ```
- **Response (`200 OK`)**：
  - 响应体为纯文本字符串，即 **JWT Access Token**（`header.payload.signature`）。
  - 解码 JWT 第 2 段（Base64URL）可读取 `exp`（Unix 过期时间戳，通常为签发后 24 小时）。
  - 缓存策略：在内存/本地缓存该 `access_token` 与 `expires_at`；当剩余有效期 `< 1 小时` 或业务接口返回 `401` 时，自动使用 Keychain 中的 `Personal API Key` 重新调用本接口换取新 Token。

### 1.3 活动列表检索接口

- **Endpoint**：`GET /activitylist-service/activities/search/activities`
- **Request Headers**：
  ```http
  Authorization: Bearer <access_token>
  Accept: application/json
  ```
- **Query Parameters**：
  - `start`：`Int`（分页起始偏移，从 `0` 开始，每次递增 `limit`）
  - `limit`：`Int`（单页条数，设为 `99`）
  - `startDate`：`String`（格式 `YYYY-MM-DD`，由界面日历控件传入；包含当天）
  - *注：不传 `endDate`（需求规定无结束日期），也不传 `activityType`（为了提取选定日期内的所有实际类型并支持本地多选过滤）。*
- **Response (`200 OK`)**：返回 `SummarizedActivityDTO` 数组，核心映射字段如下：

```json
[
  {
    "activityId": 16892401234,
    "activityName": "晨跑",
    "startTimeLocal": "2026-11-11 07:15:20",
    "startTimeGMT": "2026-11-10 23:15:20",
    "activityType": {
      "typeId": 1,
      "typeKey": "running",
      "parentTypeId": 17
    },
    "distance": 10025.5,
    "duration": 3120.4,
    "hasPolyline": true
  }
]
```

### 1.4 原始活动文件下载接口

- **Endpoint**：`GET /download-service/files/activity/{activityId}`
- **Request Headers**：
  ```http
  Authorization: Bearer <access_token>
  Accept: application/zip, application/x-zip-compressed, application/octet-stream
  ```
- **Response (`200 OK`)**：
  - 返回 ZIP 二进制字节流，解压后内含单条活动的原始 `.fit` 文件（通常命名为 `{activityId}_ACTIVITY.fit`）。

---

## 2. 界面组件与交互状态规范

### 2.1 日历控件 (起始日期)
- **控件类型**：SwiftUI `DatePicker`（图形日历弹窗或紧凑日历样式，`displayedComponents: .date`）。
- **日期范围**：`2000-01-01` 至 `Date.now`（`in: minDate...Date()`）。
- **启动默认值**：每次应用启动时，`startDate = Calendar.current.startOfDay(for: Date())`。

### 2.2 类型筛选控件 (多选下拉框)
- **数据提取**：
  ```swift
  let availableTypes: [String] // 从当前日期范围拉取的 activities 中提取唯一 typeKey 列表
  ```
- **控件展示**：使用下拉菜单/Popover 展示复选框列表，顶部附带「全选 / 取消全选」快捷项。
- **空状态**：若当前起始日期（例如当天）暂无任何活动，类型下拉框显示“暂无类型”并禁用。

### 2.3 Activity 列表 5 项展示规格

| 列名 | 对应源字段 | 格式化规则 |
| :--- | :--- | :--- |
| **勾选框** | `isSelected: Bool` | 支持单行点击勾选，表头提供当前过滤列表全选/取消全选 |
| **名称** | `activityName` | 原始字符串（若为空则显示类型名称） |
| **日期/时间** | `startTimeLocal` | `YYYY-MM-DD HH:mm:ss`（或 `YYYY-MM-DD HH:mm`） |
| **类型** | `activityType.typeKey` | 显示运动类型名称（可配中英文映射，如 `running (跑步)`） |
| **距离** | `distance` (米) | `>= 1000m` 显示 `X.XX km`；`< 1000m` 显示 `X m`；无距离显示 `-` |
| **时长** | `duration` (秒) | 格式化为 `HH:mm:ss`（不足 1 小时为 `mm:ss`） |

### 2.4 多选与「合并」选项判定逻辑

设当前用户已勾选的活动集合为 `selectedActivities`：
- **可下载条件**：`!selectedActivities.isEmpty`
- **提供「合并」选项条件 (`canMerge`)**：
  ```swift
  var canMerge: Bool {
      guard selectedActivities.count >= 2 else { return false }
      let uniqueTypes = Set(selectedActivities.map { $0.typeKey })
      return uniqueTypes.count == 1
  }
  ```
  - 当 `canMerge == true` 时：显示并允许勾选「合并为一个文件」开关。
  - 当 `canMerge == false` 时：隐藏或禁用「合并为一个文件」开关，并将 `isMergeEnabled` 复位为 `false`。

---

## 3. 格式转换与同类型合并规范

### 3.1 无 GPS 轨迹活动在 GPX 下的保护逻辑
- 解析 `.fit` 后检查是否存在有效经纬度点（`record` 消息中包含合法 `position_lat` 与 `position_long`）：
  - 若存在有效坐标点：正常生成包含 `<trkpt lat="..." lon="...">`（含 `<ele>`, `<time>`, `<extensions>` 心率/踏频/功率/温度）的标准 GPX 1.1 文件。
  - 若**不含任何有效坐标点**且用户选择了 `GPX` 格式：弹窗提示用户“所选活动不含 GPS 轨迹，导出 GPX 将不包含数据点，是否改用 TCX 或 FIT 格式导出？”，提供一键切换为 `TCX` 或 `FIT` 导出。

### 3.2 多活动同类型合并规则
当勾选 `N` (`N >= 2`) 条同类型活动并开启「合并」时：
1. **排序**：按每条活动的开始时间（`startTimeGMT` / FIT 首条时间戳）**严格升序排列**。
2. **按选定格式输出单个合并文件**：
   - **GPX 合并**：生成单个 `<gpx><trk>`，将排序后的各活动轨迹点依次写入 `<trkseg>`（保留所有高程、UTC 时间与 Garmin `TrackPointExtension` 心率/踏频/功率数据）。
   - **TCX 合并**：生成单个 `<TrainingCenterDatabase><Activities><Activity Sport="...">`，将各活动的 `<Lap StartTime="...">` 按时间升序依次排列，保留全部 `<Trackpoint>` 与分段统计数据。
   - **FIT 合并**：保留首个活动的 `file_id` 头信息，按时间顺序拼接各活动的 `record` 与 `lap` 消息流，并汇总更新最终 `session` 消息的总时长、总距离等统计字段，重新计算末尾 16-bit CRC 校验和，生成单个合法 `.fit` 文件。

---

## 4. 文件保存与命名规范

### 4.1 保存目录
- 默认目录：`FileManager.default.urls(for: .downloadsDirectory, in: .userDomainMask).first!` (`~/Downloads`)。
- 用户更改目录后，将路径及 Security-Scoped Bookmark 保存至 `UserDefaults`，文件直接平铺保存在该目录下。

### 4.2 文件名模板与字符净化
- **非法字符净化**：将名称中的 `/`, `\`, `:`, `*`, `?`, `"`, `<`, `>`, `|` 及控制字符替换为 `_`。
- **单文件命名**：
  `{YYYY-MM-DD}_{typeKey}_{净化后名称}_{activityId}.{gpx|tcx|fit}`
  - 示例：`2026-11-11_running_晨跑_16892401234.gpx`
- **合并文件命名**：
  `{最早活动YYYY-MM-DD}_至_{最晚活动YYYY-MM-DD}_{typeKey}_合并({N}条).{gpx|tcx|fit}`
  - 示例：`2026-11-01_至_2026-11-11_running_合并(3条).gpx`
