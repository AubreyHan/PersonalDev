# Vibe Coding 实施路线图 (IMPLEMENTATION_PLAN) - Garmin Activity Exporter (v1)

> [!IMPORTANT]
> **执行提示 (Execution Notice)**：
> 本文档面向业务代码仓库 `/Users/aubreyhan/Tools/git_repo/PersonalDev/GarminActivity` 的实际编码工作。严格按照第一版（v1）范围实施，不添加任何未定义的功能。

---

## 阶段一：macOS Swift + SwiftUI 工程初始化与模型层 (Phase 1: Project Setup & Models)

### 任务 1.1：初始化 Swift Package / Xcode macOS App 工程
- **目标**：在 `/Users/aubreyhan/Tools/git_repo/PersonalDev/GarminActivity` 建立可编译运行的 macOS 原生 SwiftUI 工程。
- **实施要点**：
  - 配置支持 macOS 14.0+，启用 Swift 并发 (`async/await`)。
  - 定义核心数据模型：
    - `GarminRegion`（`.global`, `.china` 及其對應 Base URL）。
    - `ActivityItem`（映射 `SummarizedActivityDTO`：`activityId`, `activityName`, `startTimeLocal`, `startTimeGMT`, `typeKey`, `distance`, `duration`, `hasPolyline`）。
    - `ExportFormat`（`.gpx`, `.tcx`, `.fit`）。
- **验证指令**：
  ```bash
  swift build
  ```

---

## 阶段二：Keychain 存储与双区 Token 自动探测认证 (Phase 2: Auth & Auto Region Probe)

### 任务 2.1：实现 `KeychainService` 与 `PersonalApiAuthService`
- **目标**：实现输入 API Key 后自动探测国际区/中国区，并将凭据持久化至 macOS Keychain。
- **实施要点**：
  - `KeychainService`：保存、读取、删除 `apiKey` 与 `region`。
  - `PersonalApiAuthService`：
    - 并发或顺序请求 `https://personal.connectapi.garmin.com` 与 `https://personal.connectapi.garmin.cn` 的 `/personalapitoken-service/personal-api/personal-api-key/token` 接口（Header: `Authorization: ApiKey <key>`）。
    - 解析返回的 JWT 字符串，提取 `exp` 过期时间并在内存中缓存 Bearer Token。
- **验证指令**：
  ```bash
  swift test
  ```

---

## 阶段三：Garmin API 客户端与 FIT 解压转换/合并引擎 (Phase 3: API Client & FIT Exporter)

### 任务 3.1：实现 `GarminApiClient`
- **目标**：按起始日期拉取活动列表，以及按 `activityId` 下载活动原始 ZIP 包。
- **实施要点**：
  - `fetchActivities(from startDate: Date)`：格式化 `startDate` 为 `YYYY-MM-DD`，循环分页调用 `/activitylist-service/activities/search/activities` 直至拉取完该起始日期以来的全部活动。
  - `downloadActivityZip(activityId: Int64)`：调用 `/download-service/files/activity/{activityId}` 获取 ZIP 二进制字节。

### 任务 3.2：实现 ZIP 内存解压、FIT 解析与 GPX/TCX/FIT 导出及合并
- **目标**：从 ZIP 提取 `.fit` 文件，支持单文件转换与同类型多活动按时间升序合并。
- **实施要点**：
  - `ZipExtractor`：从 ZIP 数据流中提取 `.fit` 原始字节。
  - `FitParser`：解析 FIT 二进制消息（`file_id`, `session`, `lap`, `record`），提取 UTC 时间戳、经纬度（半圆 semicircle 转十进制度）、海拔、心率、踏频、功率、距离与分段汇总。
  - `ActivityExporter`：
    - 生成单文件或合并后的 `.gpx`、`.tcx`、`.fit` 文件。
    - 检测无 GPS 轨迹活动，在用户选择 `.gpx` 时抛出可交互提示以回退为 `.tcx` 或 `.fit`。
    - 按规范生成文件名并写入目标目录。

---

## 阶段四：SwiftUI 界面与状态集成 (Phase 4: SwiftUI Views & Integration)

### 任务 4.1：实现主状态机 `MainViewModel` 与全部视图
- **目标**：完成登录页、顶部筛选栏、活动列表、底部下载栏的完整闭环。
- **实施要点**：
  - `LoginView`：输入框粘贴 Personal API Key，显示自动探测区域进度与错误提示。
  - `FilterHeaderView`：
    - 单个「起始日期」`DatePicker`（范围 `2000-01-01...Date()`，每次启动默认当天）。
    - 动态「运动类型」多选下拉框（仅显示当前拉取结果中存在的类型，默认全选，点击复选框即时过滤）。
    - 「更换/退出 Key」按钮。
  - `ActivityListView`：展示复选框与「名称」、「日期/时间」、「类型」、「距离」、「时长」5 列数据。
  - `DownloadFooterView`：
    - 显示当前保存目录（默认 `~/Downloads`）与「更改目录」按钮。
    - 格式选择（`GPX` / `TCX` / `FIT`）。
    - 「合并为一个文件」复选框（仅当勾选 `>= 2` 条且 `typeKey` 完全相同时显示/可用）。
    - 「下载选中项」按钮与进度反馈。
- **验证指令**：
  ```bash
  swift build && swift test
  ```
