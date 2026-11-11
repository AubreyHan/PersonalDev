# PhotoCleaner — Agent 研发与 Swarm 协同准则

## 1. 工程与设计文档映射 (Workspace & DevDoc Mapping)

- **业务工程仓库**: `/Users/aubreyhan/Tools/git_repo/PersonalDev/PhotoCleaner`
- **权威设计规格库**: `/Users/aubreyhan/Tools/git_repo/PersonalDev/DevDoc/photo-cleaner/`（所有 Agent 具备免确认直接读写权限）：
  - [README.md](file:///Users/aubreyhan/Tools/git_repo/PersonalDev/DevDoc/photo-cleaner/README.md) — 项目定位与核心特性概览
  - [PRD.md](file:///Users/aubreyhan/Tools/git_repo/PersonalDev/DevDoc/photo-cleaner/PRD.md) — 产品需求规格（`FR-1 ~ FR-8`）与验收清单（`AC-1 ~ AC-8`）
  - [ARCHITECTURE.md](file:///Users/aubreyhan/Tools/git_repo/PersonalDev/DevDoc/photo-cleaner/ARCHITECTURE.md) — Swift 6 + SwiftUI + AppKit 分层架构与推荐目录结构
  - [SPEC.md](file:///Users/aubreyhan/Tools/git_repo/PersonalDev/DevDoc/photo-cleaner/SPEC.md) — 领域模型、相簿三态逻辑、空相簿/文件夹自底向上级联清理算法、持久化 Schema 与快捷键规范
  - [IMPLEMENTATION_PLAN.md](file:///Users/aubreyhan/Tools/git_repo/PersonalDev/DevDoc/photo-cleaner/IMPLEMENTATION_PLAN.md) — 6 大阶段（Phase 1 ~ Phase 6）与 14 个原子任务及验证指令

## 2. 核心技术红线 (Mandatory Engineering Invariants)

1. **纯原生技术栈**: macOS 14.0+ (Sonoma)、Swift 6 / Swift 5.10+ (`@Observable`、`@MainActor`、`actor`)、SwiftUI + AppKit 按需桥接（`PHLivePhotoView`、`AVPlayerView`）。
2. **100% 官方 PhotoKit API**: 严禁直接读写系统私有 `Photos.sqlite`，所有查询、收藏切换、相簿成员增删、空相簿/文件夹删除及资产删除均通过 `PHPhotoLibrary` 执行。
3. **iCloud 优化存储安全防线**:
   - 网格缩略图与 `DaySimilarityEngine` 特征提取**强制设置 `isNetworkAccessAllowed = false`**，严禁在列表浏览或后台聚类时触发批量云端原片下载。
   - 仅在大图快审、同屏高清对比或播放视频时允许 `isNetworkAccessAllowed = true` 按需流式拉取。
4. **空相簿与空文件夹清理安全边界 (`EmptyCollectionSweeper`)**:
   - 仅針對 `.albumRegular`（`canPerform(.delete)`）与 `.regularFolder`（`canPerform(.delete)`）。
   - 判定空相簿时**必须使用 `includeHiddenAssets = true`** 统计真实资产数 `== 0`，防止误删含有已隐藏照片的非空相簿（`EC-2`）。
   - 仅在①冷启动索引完成、②本应用内移出相簿成员、③本应用内提交待删暂存篮三个时机自动触发；外部 `photoLibraryDidChange` 绝不抢删空相簿。
   - 捕获 `PHPhotosError.userCancelled` 并记入 `sessionIgnoredEmptyAlbumIDs`（`EC-3`）。
5. **收藏 ❤️ 免弹窗锁定保护 (`FR-7.1`)**:
   - 凡 `isFavorite == true` 的资产默认免疫单删、框选批删与相似组“删除其余”，自动跳过并触发非阻塞 Toast。
   - 处于待删暂存篮中的资产若被点亮 `isFavorite = true`，自动从待删篮移出。
6. **纯按需桌面形态 (`FR-8.1`)**:
   - `applicationShouldTerminateAfterLastWindowClosed` 返回 `true`，关闭窗口彻底退出进程，零后台常驻。

## 3. `/swarm` 命令执行规范

当用户输入 `/swarm`（或要求以 Swarm 多智能体模式开发/推进 PhotoCleaner）时，**必须立即加载并遵循 `.agents/skills/swarm/SKILL.md` 中的 PhotoCleaner 专属 Swarm 编排指令**，按模块所有权边界（File Ownership Matrix）组建并调度 Swarm 团队。
