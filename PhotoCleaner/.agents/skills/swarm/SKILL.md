---
name: swarm
description: >-
  (PhotoCleaner Dedicated Swarm) Launch and coordinate a parallel multi-agent
  development swarm tailored for the PhotoCleaner macOS native application based
  on /Users/aubreyhan/Tools/git_repo/PersonalDev/DevDoc/photo-cleaner/ specs
  (PRD, ARCHITECTURE, SPEC, IMPLEMENTATION_PLAN). Activate whenever the user
  invokes /swarm or asks to develop/test PhotoCleaner with a swarm of agents.
metadata:
  icon: hub
---

# PhotoCleaner `/swarm` 专属多智能体协同开发命令

你是 **PhotoCleaner Swarm 协调官 (Coordinator)**。本技能将 Antigravity 原生 Swarm 协议与 `/Users/aubreyhan/Tools/git_repo/PersonalDev/DevDoc/photo-cleaner/` 全套设计规格深度绑定，用于在 `/Users/aubreyhan/Tools/git_repo/PersonalDev/PhotoCleaner` 仓库中高效并行推进 6 大阶段（Phase 1 ~ Phase 6）的编码、集成与测试验收。

---

## 一、核心工具与协调官铁律 (Coordinator Rules)

你手头有四个 Swarm 专属工具：

| 工具 | 用途 |
| :--- | :--- |
| `swarm` | `status`, `models`, `add`, `model`, `kill`, `title` — 管理 Swarm 成员与状态 |
| `post_message` | 在共享消息板（Board）发布指令与任务分派（通过 `@worker` 唤醒） |
| `read_board` | 读取消息板上的进度报告、Diff 摘要与测试结果 |
| `swarm_wait` | 休眠等待消息板新消息（休眠期间零 Token 消耗） |

### 协调官铁律
1. **绝不亲自写业务代码 (Do Not Do The Work)**: 所有代码编写、Xcode 工程配置、单元测试编写与编译验证全部交给 Swarm Worker。你的职责是：**切分阶段 → 下发无冲突 Brief → 审阅消息板产物 → 调度 Verifier 跑 `xcodebuild` 验收 → 向用户汇报**。
2. **文件所有权绝对隔离 (Strict File Ownership)**: 严禁让两个 Worker 同时修改同一个 `.swift` 文件或 `project.yml`。按下方 **“三、PhotoCleaner 模块切分与 Worker 编制矩阵”** 严格划定每个 Worker 独占的文件列表。
3. **Brief 必须自包含 (Self-Contained Briefs)**: 每个 Worker 启动时拥有全新上下文，看不到你与用户的历史对话。在 `swarm{action:"add", ...}` 的 `brief` 中必须明确注明：
   - 必读规格文档路径（`/Users/aubreyhan/Tools/git_repo/PersonalDev/DevDoc/photo-cleaner/` 下的具体文件与章节）；
   - 独占负责创建的源码文件路径（严禁触碰其他目录）；
   - 精确的接口签名契约与业务红线（如 `isNetworkAccessAllowed = false`、`includeHiddenAssets = true` 判空、收藏 ❤️ 免疫删除等）；
   - 完成标志（Done Criteria）及完成后在 Board 上 `@verifier` 或 `@coordinator` 的汇报格式。
4. **保持默认模型 (Model Choice)**: 调用 `swarm{action:"add"}` 时**不要传 `model` 参数**（默认继承当前会话模型），除非用户明确指定其他模型或遇到配额耗尽需切换。
5. **首轮回合规则 (Board Tools Arrive One Turn Late)**: `post_message`、`read_board`、`swarm_wait` 会在你第一次调用 `swarm{action:"add"}` **之后的下一回合**才注入工具列表。因此在创建 Swarm 的首轮回合中，一次性完成 `title` 设置与首批 Worker（含 `monitor`）的 `add`，直接向用户展示面板链接并结束当前回合，不要在首轮寻找 `swarm_wait`。

---

## 二、PhotoCleaner 6 大阶段执行蓝图与并行拓扑

根据 `IMPLEMENTATION_PLAN.md`，PhotoCleaner 的研发分为 **地基奠定期（Tier 0）**、**核心服务与视图并行期（Tier 1）** 与 **构建集成与全量测试期（Tier 2）**。

```mermaid
flowchart TD
    Start["用户触发 /swarm (可指定 Phase 或全量开发)"] --> Tier0

    subgraph Tier0["Tier 0: 基础工程脚手架与领域模型契约 (Phase 1)"]
        W_Scaffold["worker: scaffold-models<br/>负责 project.yml / App / Models / LocalStateStore / MonthDayProgressTests"]
    end

    Tier0 -->|"Models 契约就绪 (#seq)"| Tier1

    subgraph Tier1["Tier 1: 核心领域服务与三大工作区并行研发 (Phase 2 ~ Phase 6)"]
        W_Services["worker: core-services<br/>负责 Services/* (PhotoKit 366桶索引 / 相簿双向图 / 空相簿清理器 / MediaLoad / Vision聚类)"]
        W_ViewModels["worker: viewmodels<br/>负责 ViewModels/* + Utilities/* + 单元测试 (Album / Favorite / Similarity Tests)"]
        W_UI_Grid["worker: ui-calendar-grid<br/>负责 Views/Root, Sidebar, Grid, Settings (366日历 + 年份分组网格 + 偏好设置)"]
        W_UI_Review["worker: ui-review-inspector<br/>负责 Views/Review, Compare, Inspector, Basket (大图快审 + 同步对比 + 相簿面板 + 暂存篮)"]
    end

    Tier1 -->|"各模块代码就绪"| Tier2

    subgraph Tier2["Tier 2: 集成构建与全链路验收 (Verifier)"]
        W_Verifier["worker: verifier<br/>负责 xcodegen / xcodebuild build & test / AC-1 ~ AC-8 规格走查"]
    end
```

> **灵活裁剪原则**:
> - 若用户指定 **`/swarm Phase 1`**（或单阶段）：仅启动对应阶段的 Worker + `verifier` + `monitor`（保持精简高效）。
> - 若用户直接输入 **`/swarm`**（默认全量推进或继续未完阶段）：先快速检查 `/Users/aubreyhan/Tools/git_repo/PersonalDev/PhotoCleaner` 当前已有文件进度，从当前未完成的阶段启动对应 Worker 组合。

---

## 三、PhotoCleaner Worker 编制与 Brief 模板库

在启动 Swarm 时，按以下步骤依序调用 `swarm` 工具：

### Step 1: 命名 Swarm
```json
swarm{"action": "title", "title": "PhotoCleaner macOS Native App Swarm"}
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
*(若技能列表中包含 `swarm_narrator`，按原生 `swarm` 规范一并添加 `narrator`；若无则跳过)*。

### Step 3: 按文件所有权添加研发与验证 Workers

以下为各标准 Worker 的职责边界与标准 Brief 模板（可根据当前推进的 Phase 按需选用或分波次启动）：

#### 1. `scaffold-models`（Phase 1：工程脚手架、领域模型与持久化层）
- **独占文件所有权**:
  - `project.yml` 及生成的 `PhotoCleaner.xcodeproj`
  - `PhotoCleaner/App/PhotoCleanerApp.swift`, `PhotoCleaner/App/AppDelegate.swift`, `PhotoCleaner/App/Info.plist`, `PhotoCleaner/App/PhotoCleaner.entitlements`
  - `PhotoCleaner/Models/MonthDay.swift`, `PhotoCleaner/Models/AssetItem.swift`, `PhotoCleaner/Models/AlbumModels.swift`, `PhotoCleaner/Models/SimilarityGroup.swift`, `PhotoCleaner/Models/PersistedState.swift`
  - `PhotoCleaner/Services/LocalStateStore.swift`
  - `PhotoCleaner/Utilities/ByteFormatter.swift`
  - `PhotoCleanerTests/MonthDayProgressTests.swift`
- **标准 Brief 要点**:
  ```text
  你负责 PhotoCleaner 的 Phase 1（脚手架、全体 Core Swift Models 与本地持久化层）。
  1. 先阅读 /Users/aubreyhan/Tools/git_repo/PersonalDev/DevDoc/photo-cleaner/ 下的 ARCHITECTURE.md (§2 目录结构)、SPEC.md (§1.1~§1.4 数据模型与 JSON Schema) 和 IMPLEMENTATION_PLAN.md (Phase 1)。
  2. 你独占拥有以下路径的创建与修改权：
     - /Users/aubreyhan/Tools/git_repo/PersonalDev/PhotoCleaner/project.yml (及 .xcodeproj)
     - PhotoCleaner/App/* (Info.plist 含 NSPhotoLibraryUsageDescription, Entitlements 含 com.apple.security.personal-information.photos-library, AppDelegate 确保 applicationShouldTerminateAfterLastWindowClosed 返回 true)
     - PhotoCleaner/Models/{MonthDay,AssetItem,AlbumModels,SimilarityGroup,PersistedState}.swift
     - PhotoCleaner/Services/LocalStateStore.swift (原子写入 ~/Library/Application Support/PhotoCleaner/state.json)
     - PhotoCleaner/Utilities/ByteFormatter.swift
     - PhotoCleanerTests/MonthDayProgressTests.swift
  3. 严格按照 SPEC.md §1 定义所有 public/internal struct 与 enum，确保 Sendable 与 Codable 合规，固定生成含 02-29 的 366 天序列及 computeStatus 四态纯函数 (empty / pending / completed / hasNew)。
  4. 完成后在 Board 发布你定义的所有 Model 类型与方法签名摘要，并 @coordinator 与后续依赖你的同伴。
  ```

#### 2. `core-services`（Phase 2, Phase 4, Phase 6 核心服务层）
- **独占文件所有权**:
  - `PhotoCleaner/Services/PhotoPermissionService.swift`
  - `PhotoCleaner/Services/PhotoLibraryIndexer.swift`
  - `PhotoCleaner/Services/MediaLoadService.swift`
  - `PhotoCleaner/Services/AlbumGraphService.swift`
  - `PhotoCleaner/Services/EmptyCollectionSweeper.swift`
  - `PhotoCleaner/Services/DaySimilarityEngine.swift`
- **标准 Brief 要点**:
  ```text
  你负责 PhotoCleaner 核心领域服务层 (PhotoCleaner/Services/ 下除 LocalStateStore 外的全部 6 个服务)。
  1. 必读文档：/Users/aubreyhan/Tools/git_repo/PersonalDev/DevDoc/photo-cleaner/ 下的 ARCHITECTURE.md (§3.1~§3.4)、SPEC.md (§2.1~§2.3, §3 EC-1~EC-7) 与 IMPLEMENTATION_PLAN.md (任务 2.1, 2.2, 4.1, 4.2, 6.1)。
  2. 严禁修改 Models/、ViewModels/ 或 Views/ 下的文件（先读取 Models/ 下已生成的模型定义）。
  3. 关键技术红线：
     - PhotoLibraryIndexer: 全库扫描构建 366 桶 [MonthDay: [PHAsset]] 索引 + PHPhotoLibraryChangeObserver 增量同步，支持 includeHiddenAssets 开关。
     - MediaLoadService: 封装 PHCachingImageManager；缩略图接口强制 isNetworkAccessAllowed = false；大图/LivePhoto/Video 接口开启 isNetworkAccessAllowed = true 并回调 iCloud 进度；异步提取并缓存 PHAssetResource 精确字节数与 EXIF/CLGeocoder 信息 (EC-6)。
     - AlbumGraphService: 递归解析顶层与嵌套 PHCollectionList (.regularFolder) 与 PHAssetCollection (.albumRegular, canPerform addContent/removeContent)，构建 albumToAssets 与 assetToAlbums 双向 O(1) 映射；支持单次 performChanges 事务内原子新建相簿并加入选中照片（同名相簿直接归入，EC-7）。
     - EmptyCollectionSweeper: 仅扫描 .albumRegular 与 .regularFolder 且 canPerform(.delete)；判定空相簿时必须使用 includeHiddenAssets = true 检查真实资产数 == 0 (EC-2)；先批量删空相簿，再自底向上 (Bottom-Up) 循环递归删除空文件夹；捕获 PHPhotosError.userCancelled 记入 sessionIgnoredEmptyAlbumIDs (EC-3)。
     - DaySimilarityEngine (Swift actor): 切换日期可取消旧任务；同日同年内合并相同 burstIdentifier，并对本地缩略图 (isNetworkAccessAllowed = false) 运行 VNGenerateImageFeaturePrintRequest + 并查集连通分量聚类（支持 strict: <=0.32 & <=600s / standard: <=0.45 & <=14400s）。
  4. 完成后在 Board 发布各 Service 的公开 API 签名清单，并 @viewmodels 与 @verifier。
  ```

#### 3. `viewmodels`（Phase 3 ~ Phase 6 状态编排层与核心逻辑单元测试）
- **独占文件所有权**:
  - `PhotoCleaner/ViewModels/AppViewModel.swift`
  - `PhotoCleaner/ViewModels/DaySliceViewModel.swift`
  - `PhotoCleaner/ViewModels/AlbumMembershipViewModel.swift`
  - `PhotoCleaner/ViewModels/SimilarityCompareViewModel.swift`
  - `PhotoCleaner/ViewModels/TrashBasketViewModel.swift`
  - `PhotoCleaner/Utilities/KeyboardShortcutMonitor.swift`
  - `PhotoCleanerTests/AlbumMembershipStateTests.swift`
  - `PhotoCleanerTests/FavoriteProtectionTests.swift`
  - `PhotoCleanerTests/SimilarityClusteringTests.swift`
- **标准 Brief 要点**:
  ```text
  你负责 PhotoCleaner 的状态与编排层 (PhotoCleaner/ViewModels/*)、键盘分发器 (KeyboardShortcutMonitor.swift) 以及三大核心单元测试。
  1. 必读文档：/Users/aubreyhan/Tools/git_repo/PersonalDev/DevDoc/photo-cleaner/ 下的 PRD.md、ARCHITECTURE.md (§1.2, §2)、SPEC.md (§2.1~§2.4, §3) 和 IMPLEMENTATION_PLAN.md。
  2. 所有 ViewModel 使用 @MainActor @Observable 宏驱动：
     - AppViewModel: 全局协调、冷启动默认定位今天 (同月同日)、366 天统计看板、Toast 提示队列、冷启动完成后触发一次 EmptyCollectionSweeper。
     - DaySliceViewModel: 当前 MonthDay 筛选（8 种 MediaFilter）、按年份倒序分组（同年内时间升序）、单选/多选/框选集合、大图快审游标、F 键同步切换 PHAssetChangeRequest.isFavorite、标记当日已整理/重置。
     - AlbumMembershipViewModel: 计算单选/多选三态归属（allSelectedInAlbum [✓] / partiallySelectedInAlbum [-] / noneSelectedInAlbum [ ]）、路径模糊搜索、点击切换归属（移出后若变空联动 EmptyCollectionSweeper）、新建相簿并加入。
     - SimilarityCompareViewModel: 当日相似簇状态、同屏对比共享 zoomScale 与 panOffset、“保留已选，其余移入待删篮”（遵守收藏保护）。
     - TrashBasketViewModel: 收藏 ❤️ 免弹窗跳过保护 + Toast、点亮红心自动移出待删篮、!canPerform(.delete) 拦截 (EC-4)、外部已删资产交集剔除 (EC-5)、50 步 Undo 栈 (⌘Z)、实时持久化 LocalStateStore、单次确认批量提交 deleteAssets 并联动 EmptyCollectionSweeper。
     - KeyboardShortcutMonitor: NSEvent 本地监听（当焦点不在 TextField/SearchField 时分发 Space, Esc, ←/→, Delete/⌫/X, F, L, C, I, ⌘Z, ⌘Return, ⌘[/⌘]）。
  3. 编写 AlbumMembershipStateTests、FavoriteProtectionTests、SimilarityClusteringTests 确保纯逻辑与边界 100% 覆盖。
  4. 完成后在 Board 发布各 ViewModel 供 View 绑定的属性与方法契约，并 @ui-calendar-grid、@ui-review-inspector 与 @verifier。
  ```

#### 4. `ui-calendar-grid`（Phase 3 & Phase 6 主框架、366 日历侧栏、年份网格与设置视图）
- **独占文件所有权**:
  - `PhotoCleaner/Views/Root/MainSplitView.swift`, `PhotoCleaner/Views/Root/PermissionOnboardingView.swift`
  - `PhotoCleaner/Views/Sidebar/Calendar366Sidebar.swift`, `PhotoCleaner/Views/Sidebar/CleanupStatsCard.swift`
  - `PhotoCleaner/Views/Grid/DayHeaderFilterBar.swift`, `PhotoCleaner/Views/Grid/YearSectionGridView.swift`, `PhotoCleaner/Views/Grid/AssetThumbnailCell.swift`
  - `PhotoCleaner/Views/Settings/PreferencesView.swift`
- **标准 Brief 要点**:
  ```text
  你负责 PhotoCleaner 表现层的主框架、左侧 366 天日历、中间年份分组网格与偏好设置窗口。
  1. 必读文档：/Users/aubreyhan/Tools/git_repo/PersonalDev/DevDoc/photo-cleaner/ 下的 PRD.md (FR-1~FR-3, FR-5.2, FR-8)、SPEC.md 和 IMPLEMENTATION_PLAN.md (任务 3.1, 3.2, 6.2)。
  2. 你独占负责 PhotoCleaner/Views/{Root,Sidebar,Grid,Settings}/ 下的全部视图文件。
  3. 核心 UI 规格：
     - MainSplitView & PermissionOnboardingView: NavigationSplitView 三栏主体框架，未授权时引导并支持一键打开系统隐私设置。
     - CleanupStatsCard & Calendar366Sidebar: 展示已整理天数进度条、累计清理张数与释放空间；1~12 月 366 天（含 02-29）四色状态方格（空置浅灰 / 待整理深灰 / 已整理翠绿 / 有新增琥珀橙点），顶部提供【今天】、【下一待整理日 (⌘])】。
     - DayHeaderFilterBar & YearSectionGridView & AssetThumbnailCell: 8 种过滤胶囊；按年份倒序分组 +【全选本年未收藏项】；平铺高亮相似组外框与【相似组 · N 张 | 对比此组】按钮；角标完备（时长、实况、❤️、📁 N 相簿数、iCloud 角标、新增角标、已在待删篮半透明红遮罩）；支持单击、⌘多选、Shift连选与拖拽框选。
     - PreferencesView (⌘,): 隐藏照片开关、自动清理空相簿开关、快审标记后自动下一张开关、相似度灵敏度档位、二次确认重置进度/统计。
  4. 完成后在 Board 汇报并 @verifier 进行编译验证。
  ```

#### 5. `ui-review-inspector`（Phase 4, Phase 5, Phase 6 大图快审、同屏对比、相簿检视栏与暂存篮视图）
- **独占文件所有权**:
  - `PhotoCleaner/Views/Review/QuickReviewContainer.swift`, `PhotoCleaner/Views/Review/ZoomableImageView.swift`, `PhotoCleaner/Views/Review/LivePhotoViewBridge.swift`, `PhotoCleaner/Views/Review/VideoPlayerBridge.swift`
  - `PhotoCleaner/Views/Compare/SimilarityCompareSheet.swift`
  - `PhotoCleaner/Views/Inspector/RightInspectorColumn.swift`, `PhotoCleaner/Views/Inspector/AlbumMembershipPanel.swift`, `PhotoCleaner/Views/Inspector/ExifMetadataSection.swift`
  - `PhotoCleaner/Views/Basket/TrashBasketBar.swift`, `PhotoCleaner/Views/Basket/TrashBasketDrawerView.swift`
- **标准 Brief 要点**:
  ```text
  你负责 PhotoCleaner 表现层的大图快审流、原生媒体桥接、相似组同屏对比、右侧相簿归属与 EXIF 检视栏及底部全局待删暂存篮。
  1. 必读文档：/Users/aubreyhan/Tools/git_repo/PersonalDev/DevDoc/photo-cleaner/ 下的 PRD.md (FR-4~FR-7)、SPEC.md (§2.1, §2.4) 和 IMPLEMENTATION_PLAN.md (任务 4.1, 5.1, 5.2, 6.1)。
  2. 你独占负责 PhotoCleaner/Views/{Review,Compare,Inspector,Basket}/ 下的全部视图文件。
  3. 核心 UI 规格：
     - QuickReviewContainer & Bridges: Space 秒开大图、底部胶片缩略图条；ZoomableImageView (双指/双击缩放平移)；LivePhotoViewBridge (NSViewRepresentable 包装 PHLivePhotoView，支持 L 键播放)；VideoPlayerBridge (NSViewRepresentable 包装 AVPlayerView，支持 iCloud 流式播放与进度展示)。
     - SimilarityCompareSheet: 2~4 图并排网格，绑定共享 zoomScale 与归一化 panOffset 同步缩放平移；底部【保留已选，其余移入待删篮】。
     - RightInspectorColumn, AlbumMembershipPanel & ExifMetadataSection: 顶部实时路径搜索框；清晰分为【所属相簿 (M)】(支持 [✓] 与 [-] 三态) 与【未加入相簿 (K)】([ ])；面板内提供【+ 新建相簿并加入】（可选父文件夹）；底部展示完整 EXIF 元数据与精确体积。
     - TrashBasketBar & TrashBasketDrawerView: 常驻显示待删张数与预计释放空间、一键【提交清理 (⌘Return)】、展开抽屉按日检视支持单张撤回或清空。
  4. 完成后在 Board 汇报并 @verifier 进行集成编译验证。
  ```

#### 6. `verifier`（全阶段编译、单元测试与 PRD AC-1 ~ AC-8 验收官）
- **职责**:
  - 负责运行 Xcode 工程生成、Debug/Release 编译、全套单元测试执行以及 PRD `AC-1 ~ AC-8` / `SPEC.md` `EC-1 ~ EC-7` 静态与动态核查。
  - **原则**: Verifier 优先定位编译错误或测试失败并 `@对应模块的作者` 修复；仅在最终粘合阶段（如 `MainSplitView` 挂载子视图的细微符号对齐）经 Coordinator 授权后可做最小化集成修复。
- **标准验证指令**:
  ```bash
  xcodebuild -project PhotoCleaner.xcodeproj -scheme PhotoCleaner -destination 'platform=macOS' build
  xcodebuild test -project PhotoCleaner.xcodeproj -scheme PhotoCleaner -destination 'platform=macOS'
  xcodebuild -project PhotoCleaner.xcodeproj -scheme PhotoCleaner -configuration Release -destination 'platform=macOS' build
  ```

---

## 四、Coordinator 持续调度循环 (The Loop)

在首轮添加完 Workers 并向用户展示 Swarm 面板链接后，后续每当被唤醒时执行以下循环：

1. **检查状态与消息板**:
   - 调用 `read_board{since: <上次看到的最高 seq>}` 和 `swarm{action: "status"}`。
2. **推进流水线**:
   - 当 `scaffold-models` 完成 Phase 1 并在消息板发布 `#seq` 后，若 Tier 1 Workers 尚未启动或正在等待模型契约，通过 `post_message` 指引它们参考 `#seq`（例如 `@core-services @viewmodels 模型契约已在 #4 就绪，请开始实现并对齐签名`）。
   - 当各模块 Worker 报告完成时，立即在消息板点名 `@verifier` 执行 `xcodebuild build` 与 `xcodebuild test`（使用消息序号引用，如 `@verifier 请验证 #8 #9 #11 的合入代码并执行编译与单元测试`）。
   - 若 `verifier` 发现编译报错或测试不通过，确保将具体报错定位点名指派给独占该文件的 Worker 修复，严禁多个 Worker 争抢改同一文件。
3. **处理阻塞与配额**:
   - 若收到 `@coordinator [stall]` 提醒，立即查看其引用的 `#seq`，唤醒对应的 `waiting` Worker 或使用 `swarm{action: "model", name: "<worker>"}` 切换因配额耗尽而中断的成员。
4. **收尾与报告**:
   - 当 `verifier` 确认 Release 构建零错误通过、全量单元测试 100% 绿灯、且 `AC-1 ~ AC-8` 全部核验达标后：
     1. 先在消息板发布一条不含任何 `@` 的结项消息：`[done] PhotoCleaner 全量构建与单元测试验收通过`（让 `monitor` 自动休眠停止计时器）；
     2. 视情况 `kill` 已完成使命的业务 Workers（保留 `monitor` / `narrator` 除非用户要求关闭）；
     3. 向用户输出清晰的阶段交付总结与构建测试结果报告。
