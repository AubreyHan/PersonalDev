# PhotoCleaner (macOS 原生相册“那年今日”渐进式清理与相簿整理工具)

[![Platform](https://img.shields.io/badge/Platform-macOS%2014+-black.svg)](https://apple.com/macos)
[![Swift](https://img.shields.io/badge/Swift-6.0-F05138.svg)](https://swift.org/)
[![SwiftUI](https://img.shields.io/badge/UI-SwiftUI%20%2B%20AppKit-0A84FF.svg)](https://developer.apple.com/xcode/swiftui/)
[![PhotoKit](https://img.shields.io/badge/Framework-PhotoKit%20%2B%20Vision-34C759.svg)](https://developer.apple.com/documentation/photokit)

> **PhotoCleaner** 是一款专为 **macOS** 原生“照片 (Photos.app)”打造的轻量级桌面清理与相簿归属整理应用。
> 针对数万张历史照片难以一次性整理完的核心痛点，采用 **“按月-日（同月同日）366 天切片”** 的渐进式清理范式：例如选定 `8月13日`，即聚合展示历年所有 `8月13日` 拍摄的照片、实况（Live Photo）与视频，在高效筛除废片的同时一键理顺相簿归属并自动清除空相簿。

---

## 核心设计与特性亮点 (Core Highlights)

1. **“当年今日 + 全年 366 天”渐进式切片管理**:
   - 启动默认定位**今天（同月同日）**，左侧配备全年 **366 天（含 2月29日）** 日历进度面板，可任选任意月-日或一键跳转“下一个待整理日期”。
   - **基于 `localIdentifier` 快照的动态完成态**：标记某月-日整理完成时持久化当日已审资产 ID 集合；隔年今日拍了新照片或后续导入该日期旧照时，自动识别差集并将该日状态更新为 **“有新增待审”**，精准高亮新照片。

2. **三模协同极速筛片体验**:
   - **按年份倒序分组网格**: 直观浏览每年同一天的影像，支持鼠标框选/多选与顶部多维快捷过滤（`全部` / `仅视频` / `仅截屏·录屏` / `大体积优先` / `仅看新增` / `相似组` / `未入任何相簿`）。
   - **大图快审流 (Quick Review)**: 空格键秒开大图，底部附胶片缩略图条与右侧详情及相簿检视栏；原生播放 **Live Photo** 与 **HDR/慢动作视频**，全键盘快捷键极速标记“保留/待删”与同步切换原生 **“收藏 ❤️”**。
   - **当日相似/连拍同屏对比挑片**: 基于 Apple 原生 `Vision` 框架（`VNGenerateImageFeaturePrintRequest`）对当日缩略图做毫秒级特征聚类；网格中**平铺框选不折叠**，点开“对比此组”即可并排同步缩放查看细节，选出最佳照片后一键将其余移入待删篮。

3. **相簿归属即时管理与空相簿/空文件夹自动清零**:
   - **所属 / 未加入相簿一目了然**: 选中单张或多张照片/影片时，右侧面板清晰分组展示 **【所属相簿】** 与 **【未加入相簿】**（支持三态批量勾选、相簿名称实时搜索、面板内一键**新建相簿并自动加入当前选中项**）。
   - **空相簿与空文件夹即刻自动删除**: 启动时自动扫描历史空用户相簿与空相簿文件夹，且每当移出最后一张成员或提交照片删除导致相簿变空时，**立即自底向上递归调用系统接口删除空相簿及因此变空的上级相簿文件夹**，保持相簿目录树极致干净。

4. **双重防误删与全局待删暂存篮**:
   - **收藏 ❤️ 免弹窗锁定保护**: 凡带有原生相册“收藏 ❤️”标记的照片，默认免疫单删、框选批删及相似组“删除其余”操作（自动跳过并轻提示），需显式取消收藏后方可标记待删。
   - **全局跨日期“待删暂存篮” + 持久化**: 连续整理多天时待删项统一汇入全局暂存篮（实时计算预计释放空间，关闭未提交自动落盘保存）；最终一键提交调用 `PHAssetChangeRequest.deleteAssets`，照片删除全程仅触发 **1 次** macOS 系统确认框，安全移入系统相册“最近删除（30天可恢复）”。

5. **iCloud 优化存储友好与纯粹桌面形态**:
   - **零磁盘撑爆风险**: 网格浏览与相似图视觉特征提取严格仅使用本地缓存缩略图，绝不在后台批量下载云端原片；仅在点开单张大图或视频时按需流式拉取 iCloud 高清资源。
   - **隐私与零打扰**: 默认排除“已隐藏”相簿（可在设置中手动开启）；纯按需运行的标准 macOS 窗口应用（Dock 启动，关闭即彻底退出，无后台常驻与通知打扰）。

---

## 规格文档导航 (Documentation Index)

本项目遵循 Vibe Coding 规范化工程体系，所有设计规格文档归档如下：

| 文档名称 | 核心内容说明 | 直接阅读入口 |
| :--- | :--- | :--- |
| **产品需求文档 (PRD)** | 产品定位、用户旅程、FR-1 ~ FR-8 详细功能规格（含相簿归属与空相簿自动删除）、非功能指标与验收清单 | [PRD.md](file:///Users/aubreyhan/Tools/git_repo/PersonalDev/DevDoc/photo-cleaner/PRD.md) |
| **技术架构设计 (ARCHITECTURE)** | Swift + SwiftUI + AppKit 分层架构、366 桶内存索引与反向相簿归属索引、自底向上空相簿/文件夹清理流 | [ARCHITECTURE.md](file:///Users/aubreyhan/Tools/git_repo/PersonalDev/DevDoc/photo-cleaner/ARCHITECTURE.md) |
| **详细规格说明 (SPEC)** | Swift 核心领域模型、三态相簿归属逻辑、递归空相簿清理算法、持久化 Schema、快捷键表与异常边界矩阵 | [SPEC.md](file:///Users/aubreyhan/Tools/git_repo/PersonalDev/DevDoc/photo-cleaner/SPEC.md) |
| **实施路线图 (IMPLEMENTATION PLAN)** | 面向 AI 编码的 6 大阶段原子级任务拆解、Swift 构建测试验证指令与全链路验收检查点 | [IMPLEMENTATION_PLAN.md](file:///Users/aubreyhan/Tools/git_repo/PersonalDev/DevDoc/photo-cleaner/IMPLEMENTATION_PLAN.md) |

---

## 技术栈概览 (Tech Stack)

- **目标系统**: macOS 14.0 (Sonoma) 及以上（原生适配 Apple Silicon）
- **开发语言与框架**: Swift 6 + SwiftUI（Observation `@Observable` 状态驱动）+ AppKit 按需桥接
- **Apple 原生媒体与视觉能力**:
  - `PhotoKit` (`PHPhotoLibrary`, `PHAsset`, `PHAssetCollection`, `PHCollectionList`, `PHCachingImageManager`)
  - `PhotosUI` (`PHLivePhotoView` 原生实况照片渲染与播放)
  - `AVKit` (`AVPlayerView` 原生 HDR / 慢动作 / iCloud 流式视频播放)
  - `Vision` (`VNGenerateImageFeaturePrintRequest` 端侧神经网络特征距离聚类)
- **本地状态存储**: 轻量本地持久化（`~/Library/Application Support/PhotoCleaner/` 下 JSON 存储 366 天已审 ID 快照、全局待删暂存篮与累计释放空间统计）
- **独立代码仓库路径**: `/Users/aubreyhan/Tools/git_repo/PersonalDev/PhotoCleaner`

---

## 声明

本目录（`DevDoc/photo-cleaner`）专注于需求规格与技术架构沉淀，不包含业务实现源码。实际编码与构建在独立仓库 `/Users/aubreyhan/Tools/git_repo/PersonalDev/PhotoCleaner` 中开展。
