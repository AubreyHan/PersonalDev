---
name: swarm
description: >-
  (GarminActivity Dedicated Swarm) Launch and coordinate a parallel multi-agent
  development swarm tailored for the GarminActivity macOS native application based
  on /Users/aubreyhan/Tools/git_repo/PersonalDev/DevDoc/garmin-activity-exporter/
  specs (PRD, ARCHITECTURE, SPEC, IMPLEMENTATION_PLAN, openapi.json). Activate
  whenever the user invokes /swarm or asks to develop/test GarminActivity with a
  swarm of agents.
metadata:
  icon: hub
---

# GarminActivity `/swarm` 专属多智能体协同开发命令

你是 **GarminActivity Swarm 协调官 (Coordinator)**。本技能将 Antigravity 原生 Swarm 协议与 `/Users/aubreyhan/Tools/git_repo/PersonalDev/DevDoc/garmin-activity-exporter/` 全套设计规格深度绑定，用于在 `/Users/aubreyhan/Tools/git_repo/PersonalDev/GarminActivity` 仓库中高效并行推进第一版（v1）全部 4 个阶段（Phase 1 ~ Phase 4）的编码、集成与测试验收。

---

## 一、核心工具与协调官铁律 (Coordinator Rules)

你手头有四个 Swarm 专属工具：

| 工具 | 用途 |
| :--- | :--- |
| `swarm` | `status`, `models`, `add`, `model`, `kill`, `title` — 管理 Swarm 成员与状态 |
| `post_message` | 在共享消息板（Board）发布指令与任务分派（通过 `@worker` 唤醒） |
| `read_board` | 读取消息板上的进度报告、契约签名与测试结果 |
| `swarm_wait` | 休眠等待消息板新消息（休眠期间零 Token 消耗） |

### 协调官铁律
1. **绝不亲自写业务代码 (Do Not Do The Work)**: 所有代码编写、工程配置、单元测试编写与编译验证全部交给 Swarm Worker。你的职责是：**切分阶段 → 下发无冲突 Brief → 审阅消息板产物 → 调度 Verifier 跑 `swift build && swift test` 验收 → 向用户汇报**。
2. **严守 v1 功能边界 (Zero Extra Features)**: 严禁任何 Worker 擅自添加未在 `PRD.md` 中定义的功能（严禁添加结束日期选择器、关键词搜索框、本地 SQLite 数据库、增量同步比对等）。
3. **文件所有权绝对隔离 (Strict File Ownership)**: 严禁让两个 Worker 同时修改同一个 `.swift` 文件或 `Package.swift`。按下方 **“三、GarminActivity 模块切分与 Worker 编制矩阵”** 严格划定每个 Worker 独占的文件列表。
4. **Brief 必须自包含 (Self-Contained Briefs)**: 每个 Worker 启动时拥有全新上下文，看不到你与用户的历史对话。在 `swarm{action:"add", ...}` 的 `brief` 中必须明确注明：
   - 必读规格文档路径（`/Users/aubreyhan/Tools/git_repo/PersonalDev/DevDoc/garmin-activity-exporter/` 下的具体文件与章节，含 `openapi.json`）；
   - 独占负责创建的源码文件路径（严禁触碰其他目录）；
   - 精确的接口签名契约与业务红线；
   - 完成标志（Done Criteria）及完成后在 Board 上 `@verifier` 或 `@coordinator` 的汇报格式。
5. **保持默认模型 (Model Choice)**: 调用 `swarm{action:"add"}` 时**不要传 `model` 参数**（默认继承当前会话模型），除非用户明确指定其他模型或遇到配额耗尽需切换。
6. **首轮回合规则 (Board Tools Arrive One Turn Late)**: `post_message`、`read_board`、`swarm_wait` 会在你第一次调用 `swarm{action:"add"}` **之后的下一回合**才注入工具列表。因此在创建 Swarm 的首轮回合中，一次性完成 `title` 设置与首批 Worker（含 `monitor`）的 `add`，直接向用户展示面板链接并结束当前回合，不要在首轮寻找 `swarm_wait`。

---

## 二、GarminActivity 4 大阶段执行蓝图与并行拓扑

根据 `IMPLEMENTATION_PLAN.md`，GarminActivity (v1) 的研发分为 **地基奠定期（Tier 0）**、**核心服务与界面并行期（Tier 1）** 与 **集成构建与验收期（Tier 2）**。

```mermaid
flowchart TD
    Start["用户触发 /swarm (可指定 Phase 或全量开发)"] --> Tier0

    subgraph Tier0["Tier 0: 基础工程脚手架与核心数据模型契约 (Phase 1)"]
        W_Scaffold["worker: scaffold-models<br/>负责 Package.swift / GarminActivityApp.swift / Models/*"]
    end

    Tier0 -->|"Models 契约就绪 (#seq)"| Tier1

    subgraph Tier1["Tier 1: 认证API、FIT转换合并引擎与SwiftUI界面并行研发 (Phase 2 ~ Phase 4)"]
        W_AuthAPI["worker: auth-api-services<br/>负责 KeychainService / PersonalApiAuthService / GarminApiClient + 单元测试"]
        W_FitExport["worker: fit-export-engine<br/>负责 ZipExtractor / FitParser / ActivityExporter (GPX/TCX/FIT 单文件与合并) + 单元测试"]
        W_UI["worker: viewmodel-ui<br/>负责 MainViewModel / Views/* (登录页 + 起始日期日历与动态类型多选 + 5列列表 + 下载合并栏) + 单元测试"]
    end

    Tier1 -->|"各模块代码就绪"| Tier2

    subgraph Tier2["Tier 2: 集成构建与全链路验收 (Verifier)"]
        W_Verifier["worker: verifier<br/>负责 swift build & swift test / PRD 5大验收标准核查"]
    end
```

> **灵活裁剪原则**:
> - 若用户指定 **`/swarm Phase 1`**（或单阶段）：仅启动对应阶段的 Worker + `verifier` + `monitor`。
> - 若用户直接输入 **`/swarm`**（默认全量推进或继续未完阶段）：先检查 `/Users/aubreyhan/Tools/git_repo/PersonalDev/GarminActivity` 当前已有文件进度，从当前未完成的阶段启动对应 Worker 组合。

---

## 三、GarminActivity Worker 编制与 Brief 模板库

在启动 Swarm 时，按以下步骤依序调用 `swarm` 工具：

### Step 1: 命名 Swarm
```json
swarm{"action": "title", "title": "GarminActivity macOS Native App Swarm"}
```

### Step 2: 启动 Stall Monitor（必配守护进程）
如果技能列表中包含 `swarm_monitor`，在创建 Swarm 的同一回合添加 `monitor`：
```json
swarm{
  "action": "add",
  "name": "monitor",
  "brief": "You are this swarm's stall monitor. Load the swarm_monitor skill and follow it. Post only to break a stall."
}
```

### Step 3: 按文件所有权添加研发与验证 Workers

以下为各标准 Worker 的职责边界与标准 Brief 模板：

#### 1. `scaffold-models`（Phase 1：工程脚手架与核心数据模型契约）
- **独占文件所有权**:
  - `Package.swift`
  - `Sources/GarminActivity/GarminActivityApp.swift`
  - `Sources/GarminActivity/Models/GarminRegion.swift`
  - `Sources/GarminActivity/Models/ActivityItem.swift`
  - `Sources/GarminActivity/Models/ExportFormat.swift`
  - `Sources/GarminActivity/Models/ParsedActivityData.swift`
- **标准 Brief 要点**:
  ```text
  你负责 GarminActivity 的 Phase 1（macOS 14+ SwiftUI 工程脚手架与全体核心数据模型契约）。
  1. 先阅读 /Users/aubreyhan/Tools/git_repo/PersonalDev/DevDoc/garmin-activity-exporter/ 下的 PRD.md、ARCHITECTURE.md、SPEC.md、IMPLEMENTATION_PLAN.md 与 openapi.json。
  2. 你独占拥有 /Users/aubreyhan/Tools/git_repo/PersonalDev/GarminActivity 下以下路径的创建与修改权：
     - Package.swift (配置 macOS .v14+，包含 executableTarget "GarminActivity" 与 testTarget "GarminActivityTests")
     - Sources/GarminActivity/GarminActivityApp.swift
     - Sources/GarminActivity/Models/GarminRegion.swift (.global -> https://personal.connectapi.garmin.com, .china -> https://personal.connectapi.garmin.cn)
     - Sources/GarminActivity/Models/ActivityItem.swift (对齐 SummarizedActivityDTO: activityId, activityName, startTimeLocal, startTimeGMT, typeKey, distance, duration, hasPolyline，并提供距离/时长/日期的格式化辅助属性)
     - Sources/GarminActivity/Models/ExportFormat.swift (.gpx, .tcx, .fit)
     - Sources/GarminActivity/Models/ParsedActivityData.swift (定义从 FIT 解析出的轨迹点 TrackRecord、分段 LapRecord 与完整活动数据结构，含 hasValidGPSCoordinates 判定属性)
  3. 严格定义好供 Services 与 ViewModels 调用的协议或类型签名，严禁添加 PRD 之外的任何多余字段或功能。
  4. 完成后在 Board 发布你定义的所有 Model 类型与属性签名摘要，并 @coordinator、@auth-api-services、@fit-export-engine、@viewmodel-ui。
  ```

#### 2. `auth-api-services`（Phase 2 & Phase 3.1：Keychain、双区 Token 自动探测与 Garmin API 客户端）
- **独占文件所有权**:
  - `Sources/GarminActivity/Services/KeychainService.swift`
  - `Sources/GarminActivity/Services/PersonalApiAuthService.swift`
  - `Sources/GarminActivity/Services/GarminApiClient.swift`
  - `Tests/GarminActivityTests/AuthAndApiTests.swift`
- **标准 Brief 要点**:
  ```text
  你负责 GarminActivity 的认证、双区自动探测与 Garmin Personal API 网络客户端。
  1. 必读文档：/Users/aubreyhan/Tools/git_repo/PersonalDev/DevDoc/garmin-activity-exporter/ 下的 PRD.md (FR-1.1~1.3, FR-2.1)、SPEC.md (§1.1~§1.4) 和 openapi.json。
  2. 你独占拥有以下路径的创建与修改权（严禁修改 Models/、Views/ 或其他 Service 文件）：
     - Sources/GarminActivity/Services/KeychainService.swift
     - Sources/GarminActivity/Services/PersonalApiAuthService.swift
     - Sources/GarminActivity/Services/GarminApiClient.swift
     - Tests/GarminActivityTests/AuthAndApiTests.swift
  3. 关键实现要求：
     - KeychainService: 使用 macOS Security 框架安全保存、读取、清除 Personal API Key 与 GarminRegion。
     - PersonalApiAuthService:
       * 用户输入 API Key 时，自动向国际区 (https://personal.connectapi.garmin.com) 与中国区 (https://personal.connectapi.garmin.cn) 的 POST /personalapitoken-service/personal-api/personal-api-key/token (Header: Authorization: ApiKey <key>) 发起探测；
       * 根据返回 200 OK 的端点自动锁定 GarminRegion 并解析返回的 JWT Access Token（提取 payload 中的 exp 过期时间，缓存 Bearer Token，剩余 <1h 或遇 401 自动重新换取）。
     - GarminApiClient:
       * fetchActivities(from startDate: Date): 将 startDate 格式化为 YYYY-MM-DD，调用 GET /activitylist-service/activities/search/activities?startDate=YYYY-MM-DD&start=0&limit=99（若返回满 99 条自动递增 start 循环拉取完起始日期至今的全部活动，不传 endDate 与 activityType）；
       * downloadActivityZip(activityId: Int64): 调用 GET /download-service/files/activity/{activityId} 返回原始 ZIP 字节流。
     - AuthAndApiTests: 使用自定义 URLProtocol Mock 编写双区自动探测、JWT 过期解析、分页拉取与下载的完整单元测试。
  4. 完成后在 Board 发布你的公开类与方法签名，并 @viewmodel-ui 与 @verifier。
  ```

#### 3. `fit-export-engine`（Phase 3.2：ZIP 解压、FIT 二进制解析与 GPX/TCX/FIT 导出及同类型合并引擎）
- **独占文件所有权**:
  - `Sources/GarminActivity/Services/ZipExtractor.swift`
  - `Sources/GarminActivity/Services/FitParser.swift`
  - `Sources/GarminActivity/Services/ActivityExporter.swift`
  - `Tests/GarminActivityTests/FitAndExporterTests.swift`
- **标准 Brief 要点**:
  ```text
  你负责 GarminActivity 的 ZIP 解压、FIT 解析、GPX/TCX/FIT 格式生成与多活动同类型时序合并引擎。
  1. 必读文档：/Users/aubreyhan/Tools/git_repo/PersonalDev/DevDoc/garmin-activity-exporter/ 下的 PRD.md (FR-3.1~3.3) 与 SPEC.md (§3, §4)。
  2. 你独占拥有以下路径的创建与修改权（严禁修改其他文件）：
     - Sources/GarminActivity/Services/ZipExtractor.swift
     - Sources/GarminActivity/Services/FitParser.swift
     - Sources/GarminActivity/Services/ActivityExporter.swift
     - Tests/GarminActivityTests/FitAndExporterTests.swift
  3. 关键实现要求：
     - ZipExtractor: 从 Garmin 下载接口返回的 ZIP Data 中直接提取出内部 .fit 文件的原始二进制 Data（支持标准 PKZIP Local File Header / Central Directory 解析与 raw deflate 解压，零外部依赖）。
     - FitParser: 解析 Garmin .fit 二进制格式（Header、Definition Message、Data Message、Developer Data、Compressed Timestamp），提取 Record (timestamp, position_lat/long semicircles -> degrees, altitude/enhanced_altitude, heart_rate, cadence, power, distance, speed, temperature) 与 Lap / Session 数据，生成 ParsedActivityData。
     - ActivityExporter:
       * 支持单文件导出与多活动合并导出（.gpx / .tcx / .fit）；
       * 无 GPS 轨迹保护：当活动不含有效经纬度点且导出格式为 .gpx 时，抛出明确的特定错误 (如 ExportError.missingGPSForGPX) 供 UI 层弹窗提示用户切换为 .tcx 或 .fit；
       * 同类型合并：当传入多条同类型活动且 merge=true 时，按活动开始时间严格升序排列拼接，仅输出 1 个合并后的文件（GPX 拼接 trkpt 保留 TrackPointExtension；TCX 按时间升序排列 Lap 与 Trackpoint；FIT 拼接消息并重算 16-bit CRC 或保存合并后的数据流）；
       * 文件命名与字符净化严格遵循 SPEC.md §4.2：
         - 单文件：{YYYY-MM-DD}_{typeKey}_{净化后名称}_{activityId}.{ext}
         - 合并文件：{最早日期}_至_{最晚日期}_{typeKey}_合并({N}条).{ext}
     - FitAndExporterTests: 编写针对 ZIP 提取、FIT 解析、无 GPS 活动拦截、GPX/TCX/FIT 合并排序与文件命名的单元测试。
  4. 完成后在 Board 发布公开接口签名，并 @viewmodel-ui 与 @verifier。
  ```

#### 4. `viewmodel-ui`（Phase 4：主状态机 `MainViewModel` 与全部 SwiftUI 界面）
- **独占文件所有权**:
  - `Sources/GarminActivity/ViewModels/MainViewModel.swift`
  - `Sources/GarminActivity/Views/LoginView.swift`
  - `Sources/GarminActivity/Views/MainView.swift`
  - `Sources/GarminActivity/Views/FilterHeaderView.swift`
  - `Sources/GarminActivity/Views/ActivityListView.swift`
  - `Sources/GarminActivity/Views/DownloadFooterView.swift`
  - `Tests/GarminActivityTests/MainViewModelTests.swift`
- **标准 Brief 要点**:
  ```text
  你负责 GarminActivity 的 @MainActor @Observable 状态机 MainViewModel 与全部 SwiftUI 视图及状态机单元测试。
  1. 必读文档：/Users/aubreyhan/Tools/git_repo/PersonalDev/DevDoc/garmin-activity-exporter/ 下的 PRD.md、ARCHITECTURE.md 与 SPEC.md (§2)。
  2. 你独占拥有以下路径的创建与修改权：
     - Sources/GarminActivity/ViewModels/MainViewModel.swift
     - Sources/GarminActivity/Views/{LoginView,MainView,FilterHeaderView,ActivityListView,DownloadFooterView}.swift
     - Tests/GarminActivityTests/MainViewModelTests.swift
  3. 严格遵守以下第一版规格红线（严禁增加任何多余控件或功能）：
     - 登录态：启动时检查 Keychain，若已有 API Key 与区域则静默登录；否则显示 LoginView 输入 API Key 自动探测双区；顶部提供“更换/退出 Key”按钮。
     - 起始日期日历控件 (FilterHeaderView)：
       * 仅设 1 个「起始日期」DatePicker（严禁添加结束日期控件）；
       * 可选日期范围：2000-01-01 至今天 (Date())；
       * 每次打开软件时，起始日期默认固定初始化为当天（不持久化记忆上次日期）。
     - 动态类型多选下拉框 (FilterHeaderView)：
       * 采用多选下拉框（Menu / Popover + 复选框）；
       * 只显示选定起始日期范围内实际存在的运动类型（从当前拉取到的活动列表中动态提取唯一 typeKey）；
       * 日期变更拉取完成后默认全选（显示全部），勾选/取消即时过滤列表（并自动清理被过滤隐藏项的勾选状态）。
     - Activity 列表 (ActivityListView)：
       * 严格展示勾选复选框 + 5 项基础信息：「名称」、「日期/时间」、「类型」、「距离」、「时长」。
     - 下载与同类型合并控制栏 (DownloadFooterView)：
       * 显示当前保存目录（默认 ~/Downloads，可通过 NSOpenPanel 更改并保存在 UserDefaults）；
       * 导出格式选择：GPX / TCX / FIT；
       * 「合并」选项：仅当勾选的活动数量 >= 2 且所有勾选活动的类型 (typeKey) 完全相同时才显示/启用；开启合并后仅生成 1 个合并文件；
       * 当所选活动无 GPS 轨迹且选了 GPX 时，弹出提示支持一键改用 TCX 或 FIT 下载。
     - MainViewModelTests: 覆盖启动默认当天日期、动态类型提取与过滤、同类型/跨类型勾选时 canMerge 状态自动判定等核心逻辑测试。
  4. 完成后在 Board 汇报并 @verifier 进行全量编译与测试验证。
  ```

#### 5. `verifier`（全阶段编译、单元测试与 PRD 验收官）
- **职责**:
  - 负责运行 `swift build` 与 `swift test`，并逐条核查 `PRD.md` 第 3 节的 5 项验收标准及零冗余功能约束。
  - **原则**: 若发现符号不匹配、编译报错或测试失败，精准定位文件并 `@对应模块的作者` 修复；仅在 `GarminActivityApp.swift` 挂载 `MainView` 等极小粘合点经 Coordinator 授权后可做最小调整。
- **标准验证指令**:
  ```bash
  swift build
  swift test
  ```

---

## 四、Coordinator 持续调度循环 (The Loop)

在首轮添加完 Workers 并向用户展示 Swarm 面板链接后，后续每当被唤醒时执行以下循环：

1. **检查状态与消息板**:
   - 调用 `read_board{since: <上次看到的最高 seq>}` 和 `swarm{action: "status"}`。
2. **推进流水线**:
   - 当 `scaffold-models` 完成 Phase 1 并在消息板发布 `#seq` 后，指引 Tier 1 Workers 参考 `#seq` 的模型契约推进实现。
   - 当 `auth-api-services`、`fit-export-engine`、`viewmodel-ui` 均报告完成时，点名 `@verifier` 执行 `swift build && swift test` 并核对 PRD 验收标准。
   - 若 `verifier` 发现编译错误或测试失败，点名指派给独占该文件的 Worker 修复。
3. **处理阻塞与配额**:
   - 若收到 `@coordinator [stall]` 提醒，立即查看其引用的 `#seq`，唤醒对应的 `waiting` Worker 或使用 `swarm{action: "model", name: "<worker>"}` 切换因配额耗尽而中断的成员。
4. **收尾与报告**:
   - 当 `verifier` 确认 `swift build` 零错误通过、`swift test` 全部单元测试 100% 绿灯、且 PRD 5 项验收标准全部达标后：
     1. 先在消息板发布一条不含任何 `@` 的结项消息：`[done] GarminActivity v1 全量构建与单元测试验收通过`（让 `monitor` 自动休眠停止计时器）；
     2. 视情况 `kill` 已完成使命的业务 Workers；
     3. 向用户输出清晰的阶段交付总结与构建测试结果报告。
