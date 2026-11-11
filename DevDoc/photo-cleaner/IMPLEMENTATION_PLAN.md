# PhotoCleaner Vibe Coding 实施路线图 (IMPLEMENTATION PLAN)

> **使用指南**:
> 本文档面向在独立工程仓库 `/Users/aubreyhan/Tools/git_repo/PersonalDev/PhotoCleaner` 中使用 Antigravity 进行 Vibe Coding 开发。
> 整个开发流程拆解为 **6 个有序阶段（Phases）与 14 个原子级任务（Atomic Tasks）**。
> 建议按顺序逐阶段推进（例如下达指令：*“请阅读 `/Users/aubreyhan/Tools/git_repo/PersonalDev/DevDoc/photo-cleaner/` 下的全套文档，执行 `IMPLEMENTATION_PLAN.md` 的 Phase 1”*）。

---

## 阶段一：原生 SwiftUI 工程脚手架、本地状态存储与领域模型 (Phase 1: Scaffolding & Domain Core)

### 任务 1.1：初始化 macOS 14+ 原生 Xcode 工程与权限配置
- **目标**: 在 `/Users/aubreyhan/Tools/git_repo/PersonalDev/PhotoCleaner` 创建标准 Swift 6 + SwiftUI 工程与单元测试 Target，配置沙盒/非沙盒 PhotoKit 访问权限。
- **操作步骤**:
  1. 创建工程目录结构（`App/`, `Models/`, `Services/`, `ViewModels/`, `Views/`, `Utilities/`, `PhotoCleanerTests/`），可通过 `project.yml`（XcodeGen）或直接生成标准 `.xcodeproj`。
  2. 配置 `MACOSX_DEPLOYMENT_TARGET = 14.0`，启用 Swift 6 严格并发或 Swift 5.10+ `@Observable` 支持。
  3. 配置 `Info.plist`：写入 `NSPhotoLibraryUsageDescription`（*"PhotoCleaner 需要访问您的系统照片图库，以便按日期整理照片、调整相簿归属并将待清理项目移入最近删除。"*）；配置 `Entitlements` 允许访问用户照片图库（`com.apple.security.personal-information.photos-library`）。
  4. 实现 `AppDelegate`：确保 `applicationShouldTerminateAfterLastWindowClosed` 返回 `true`（关闭主窗口彻底退出，零后台驻留）。
- **验证指令**:
  ```bash
  xcodebuild -project PhotoCleaner.xcodeproj -scheme PhotoCleaner -destination 'platform=macOS' build
  ```
- **验收标准**: 工程零错误编译通过，拉起空白主窗口后关闭窗口进程立即退出。

---

### 任务 1.2：实现 `MonthDay` 366 天领域模型与 `LocalStateStore` 持久化层
- **目标**: 实现含闰日 `02-29` 的 366 天切片计算、四态流转逻辑与 `Application Support` 原子 JSON 存储，并附带完整单元测试。
- **操作步骤**:
  1. 编写 `Models/MonthDay.swift`：生成固定 366 个 `MonthDay` 序列，实现四态判定纯函数 `computeStatus(currentIDs: Set<String>, reviewedIDs: Set<String>) -> DayReviewStatus`。
  2. 编写 `Models/PersistedState.swift` 与 `Services/LocalStateStore.swift`：存储路径指向 `~/Library/Application Support/PhotoCleaner/state.json`，采用临时文件写入 + `replaceItemAt` 原子替换。
  3. 在 `PhotoCleanerTests/MonthDayProgressTests.swift` 编写单元测试，覆盖：`empty`、`pending`、`completed`、隔年新增 1 张变为 `hasNew`、删掉新增项自动恢复 `completed` 以及 `02-29` 边界。
- **验证指令**:
  ```bash
  xcodebuild test -project PhotoCleaner.xcodeproj -scheme PhotoCleaner -destination 'platform=macOS'
  ```
- **验收标准**: 所有单元测试 100% 通过。

---

## 阶段二：PhotoKit 366 桶极速索引、权限流与缩略图缓存管线 (Phase 2: PhotoKit Indexer & Media Pipeline)

### 任务 2.1：实现 `PhotoPermissionService` 与 `PhotoLibraryIndexer`
- **目标**: 实现完整图库授权流、10 万级资产毫秒级 366 桶内存分桶以及 `PHPhotoLibraryChangeObserver` 实时同步。
- **操作步骤**:
  1. 实现 `PhotoPermissionService`：检查 `.authorized` 状态；未授权时展示 `PermissionOnboardingView`，受限/被拒时支持 `NSWorkspace.shared.open` 跳转系统隐私设置。
  2. 实现 `PhotoLibraryIndexer`：
     - 根据偏好设置 `includeHiddenAssets`（默认 `false`）配置 `PHFetchOptions`；
     - 遍历 `PHAsset.fetchAssets` 提取 `Calendar.current` 下的 `(month, day)`，构建 `[MonthDay: [PHAsset]]` 字典；
     - 注册 `PHPhotoLibraryChangeObserver`，外部图库增删或收藏变更时增量/快速重建受影响日期桶并通知 `@Observable` ViewModel。
- **验证指令**:
  ```bash
  xcodebuild -project PhotoCleaner.xcodeproj -scheme PhotoCleaner -destination 'platform=macOS' build
  ```
- **验收标准**: 首次打开正常弹出系统照片权限框，授权后 500ms 内完成全库 366 天计数统计。

---

### 任务 2.2：实现 iCloud 安全的 `MediaLoadService`（缩略图缓存 + 按需高清流 + EXIF）
- **目标**: 封装 `PHCachingImageManager`，严格隔离网格本地缩略图与大图按需 iCloud 拉取，支持读取真实文件体积与 EXIF 详情。
- **操作步骤**:
  1. 缩略图加载接口：`isNetworkAccessAllowed = false`，`deliveryMode = .opportunistic`，配合网格可见区域滑动预热缓存。
  2. 大图 / Live Photo / 视频播放接口：仅在进入快审或对比视图时开启 `isNetworkAccessAllowed = true`，通过 `progressHandler` 暴露 `0.0 ~ 1.0` 的 iCloud 下载进度与云端标识。
  3. 元数据与体积提取接口：从 `PHAssetResource.assetResources(for:)` 提取精确字节大小并做内存缓存；解析分辨率、机型镜头参数及 `CLGeocoder` 拍摄地点（带本地缓存防限流）。
- **验证指令**:
  ```bash
  xcodebuild -project PhotoCleaner.xcodeproj -scheme PhotoCleaner -destination 'platform=macOS' build
  ```
- **验收标准**: 滚动浏览含云端照片的网格不会触发批量原图下载，内存占用平稳。

---

## 阶段三：366 天日历侧栏、年份分组网格与多维过滤 (Phase 3: Calendar Sidebar & Year-Grouped Grid)

### 任务 3.1：构建三栏主框架与左侧 366 天进度日历面板 (`Calendar366Sidebar`)
- **目标**: 启动默认进入“今天（同月同日）”，左侧展示统计看板与 12 个月 / 366 天状态网格。
- **操作步骤**:
  1. 实现 `CleanupStatsCard`：显示 `已整理 X / Y 有照天数` 进度条、累计清理张数与累计释放空间。
  2. 实现 `Calendar366Sidebar`：按 1~12 月展示日期小方格，用视觉色彩清晰区分 `空置（浅灰）`、`待整理（默认深灰/微亮）`、`已整理（翠绿）`、`有新增待审（琥珀橙点）`，悬停显示该日跨年照片总数。
  3. 顶部提供【今天】、【下一待整理日 (`⌘]`)】以及当前日期【标记当日已整理 / 重置未整理】按钮。
- **验证指令**:
  ```bash
  xcodebuild -project PhotoCleaner.xcodeproj -scheme PhotoCleaner -destination 'platform=macOS' build
  ```
- **验收标准**: 冷启动自动选中今天；点击 `8月13日` 等任意日期瞬时切换；点击“完成当日整理”状态立即变绿并持久化。

---

### 任务 3.2：实现按年份倒序分组网格、多维过滤与橡皮筋多选 (`YearSectionGridView`)
- **目标**: 高帧率展示当前月-日跨年份照片网格，支持快捷过滤与多选交互。
- **操作步骤**:
  1. 实现 `DayHeaderFilterBar`：提供 `全部`、`仅照片`、`仅视频`、`仅截屏·录屏`、`大体积(>20MB)`、`相似组`、`未入相簿`、`仅看新增` 胶囊切换。
  2. 实现 `YearSectionGridView` 与 `AssetThumbnailCell`：
     - 按年份倒序分组，组头展示 `YYYY年M月D日 · 周X（N 项）` 与【全选本年未收藏项】；
     - 缩略图叠加角标：视频时长、实况标志、红心 `❤️`、所属相簿数 `📁 N`、新增角标、已在待删篮红色半透明遮罩；
     - 支持单击、`⌘+单击`、`Shift+连选` 及拖拽框选。
- **验证指令**:
  ```bash
  xcodebuild -project PhotoCleaner.xcodeproj -scheme PhotoCleaner -destination 'platform=macOS' build
  ```
- **验收标准**: 各过滤条件准确生效，单选/多选状态与右侧检视栏实时联动。

---

## 阶段四：相簿归属双向管理与空相簿/空文件夹即刻自动删除 (Phase 4: Album Membership & Empty Sweeper)

### 任务 4.1：实现 `AlbumGraphService` 双向内存索引与三态归属逻辑
- **目标**: 毫秒级计算任意单选/多选照片的【所属相簿】与【未加入相簿】，支持一键切换归属与原子新建相簿。
- **操作步骤**:
  1. 实现 `AlbumGraphService`：递归解析顶层与嵌套 `PHCollectionList`（文件夹）及 `.albumRegular` 普通相簿，构建 `albumToAssets` 与 `assetToAlbums` 双向哈希表。
  2. 实现 `AlbumMembershipViewModel`：
     - 计算单选/多选下的 `allSelectedInAlbum [✓]`、`partiallySelectedInAlbum [-]`、`noneSelectedInAlbum [ ]` 分组列表；
     - 支持按路径关键词实时搜索过滤；
     - 实现点击勾选/取消勾选调用 `addAssets` / `removeAssets`；
     - 实现【+ 新建相簿并加入】：在同一 `performChanges` 事务内完成创建相簿（可选父文件夹）并直接归入当前选中照片（重名时自动归入已有同名相簿）。
  3. 编写 `AlbumMembershipStateTests.swift` 单元测试验证单选与多选三态集合计算准确性。
- **验证指令**:
  ```bash
  xcodebuild test -project PhotoCleaner.xcodeproj -scheme PhotoCleaner -destination 'platform=macOS'
  ```
- **验收标准**: 选中单张或多张照片/视频时右侧秒出【所属相簿】与【未加入相簿】，勾选修改在原生“照片.app”实时可见。

---

### 任务 4.2：实现 `EmptyCollectionSweeper` 自底向上即刻清理空相簿与空文件夹
- **目标**: 在启动时、移出相簿成员后、提交暂存篮删照后，自动即刻删除空普通相簿及连带变空的上级相簿文件夹。
- **操作步骤**:
  1. 实现 `EmptyCollectionSweeper`：
     - 严格过滤可删普通用户相簿（`.albumRegular` 且 `canPerform(.delete)`），并使用 `includeHiddenAssets = true` 校验真实资产数 `== 0`（保护含隐藏照片的相簿）；
     - 合并本批次所有空相簿一次性调用 `PHAssetCollectionChangeRequest.deleteAssetCollections`；
     - 删除完成后自底向上递归扫描空 `PHCollectionList`（`.regularFolder` 且内部 `fetchCollections.count == 0`），批量调用 `PHCollectionListChangeRequest.deleteCollectionLists` 直至无空父文件夹；
     - 优雅处理 `PHPhotosError.userCancelled`（记入 `sessionIgnoredEmptyAlbumIDs`，不死循环弹窗）。
  2. 挂载触发点：① 冷启动索引完成回调；② `AlbumGraphService.removeAssets` 完成回调；③ `TrashBasketViewModel.commitDeletions` 成功回调。
- **验证指令**:
  ```bash
  xcodebuild test -project PhotoCleaner.xcodeproj -scheme PhotoCleaner -destination 'platform=macOS'
  ```
- **验收标准**: 将某测试相簿（位于测试空文件夹内）的最后一张照片取消勾选移出后，应用立即自动触发删除该空相簿及变空的上级测试文件夹。

---

## 阶段五：大图快审流、收藏锁定保护与全局待删暂存篮 (Phase 5: Quick Review, Favorite Lock & Trash Basket)

### 任务 5.1：实现空格大图快审视图（含原生 Live Photo、视频播放与键盘流）
- **目标**: 打通 `Space` 秒开大图快审、原生实况/视频预览、EXIF 面板及无焦点冲突全键盘快捷键。
- **操作步骤**:
  1. 实现 `ZoomableImageView`、`LivePhotoViewBridge` (`PHLivePhotoView`) 与 `VideoPlayerBridge` (`AVPlayerView`)。
  2. 实现 `QuickReviewContainer` 底部胶片缩略图条（前后高清预加载）与 `KeyboardShortcutMonitor`（当焦点不在搜索输入框时捕获 `Space`, `Esc`, `←/→`, `Delete/X`, `F`, `L`, `I`, `⌘Z`）。
  3. 实现 `F` 键同步修改原生 `PHAssetChangeRequest.isFavorite`。
- **验证指令**:
  ```bash
  xcodebuild -project PhotoCleaner.xcodeproj -scheme PhotoCleaner -destination 'platform=macOS' build
  ```
- **验收标准**: 全键盘无需碰鼠标即可流畅完成翻片、播放实况/视频、点亮收藏与标记待删。

---

### 任务 5.2：实现“收藏 ❤️ 锁定保护”与全局持久化“待删暂存篮”
- **目标**: 确保收藏照片 100% 免弹窗锁定不被误删，实现跨日期暂存篮累积、撤销栈、单次确认批量提交与退出拦截。
- **操作步骤**:
  1. 实现 `TrashBasketViewModel`：
     - 加入待删篮前强制过滤 `isFavorite == true` 及 `!canPerform(.delete)` 资产，遇到收藏项自动跳过并触发非阻塞 Toast 提示；
     - 当某资产被点亮 `isFavorite = true` 时，若其在暂存篮中则自动移出；
     - 维护 50 步 Undo 栈支持 `⌘Z`；每次暂存篮变更实时写入 `LocalStateStore`；
     - 实现 `commitDeletions()`：单次调用 `PHAssetChangeRequest.deleteAssets`，成功后累加释放字节数并联动 `EmptyCollectionSweeper`。
  2. 实现 `TrashBasketBar`、`TrashBasketDrawerView` 以及 `AppDelegate.applicationShouldTerminate` 非空暂存篮三选一提醒弹窗。
  3. 编写 `FavoriteProtectionTests.swift` 单元测试。
- **验证指令**:
  ```bash
  xcodebuild test -project PhotoCleaner.xcodeproj -scheme PhotoCleaner -destination 'platform=macOS'
  ```
- **验收标准**: 收藏照片无法被误加入暂存篮；跨多天攒入多张照片点击提交仅弹 1 次系统删除确认框；未提交关闭窗口弹出保护提示。

---

## 阶段六：Vision 当日相似图识别、同屏对比视图与偏好设置收尾 (Phase 6: Vision Similarity, Compare View & Polish)

### 任务 6.1：实现 `DaySimilarityEngine` 与平铺高亮 + 同屏同步缩放对比视图
- **目标**: 对当前选中日期执行毫秒级本地缩略图特征聚类，网格平铺框选高亮，支持并排同步缩放挑出最佳照片。
- **操作步骤**:
  1. 实现 `DaySimilarityEngine`（Swift `actor`）：切换日期时取消上一日期未完任务，合并 `burstIdentifier` 并运行 `VNGenerateImageFeaturePrintRequest` + 并查集连通分量聚类（附 `SimilarityClusteringTests.swift` 测试）。
  2. 在 `YearSectionGridView` 中保持时间顺序平铺渲染，对命中相似组的卡片绘制同组联动边框与组头【相似组 · N 张 | 对比此组】按钮。
  3. 实现 `SimilarityCompareSheet`：2~4 图并排网格，绑定共享 `zoomScale` 与归一化 `panOffset` 实现同步放大平移查看细节；底部提供【保留已选，其余移入待删篮】（遵守收藏保护）。
- **验证指令**:
  ```bash
  xcodebuild test -project PhotoCleaner.xcodeproj -scheme PhotoCleaner -destination 'platform=macOS'
  ```
- **验收标准**: 同一天连拍或相似试拍自动平铺高亮，点开对比可同步放大眼部细节并一键将落选废片送入待删篮。

---

### 任务 6.2：实现 `PreferencesView (⌘,)` 与全链路验收走查
- **目标**: 完善设置项、深浅色模式视觉细节与异常兜底，完成 Release 构建验证。
- **操作步骤**:
  1. 实现 `PreferencesView`：包含隐藏相簿开关、自动即刻清理空相簿开关、快审自动跳下一张开关、相似度灵敏度切换及重置进度/统计操作。
  2. 检查所有空状态（当天无照片、过滤结果为空、无用户相簿、iCloud 离线报错）的友好占位 UI。
  3. 执行 Release 编译与全套单元测试回归。
- **验证指令**:
  ```bash
  xcodebuild -project PhotoCleaner.xcodeproj -scheme PhotoCleaner -configuration Release -destination 'platform=macOS' build
  xcodebuild test -project PhotoCleaner.xcodeproj -scheme PhotoCleaner -destination 'platform=macOS'
  ```
- **验收标准**: Release 包零警告构建成功，PRD 第 6 节 `AC-1 ~ AC-8` 全部达标。
