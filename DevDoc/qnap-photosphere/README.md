# qnap-photosphere (威联通原生全能相册与资产整理中枢)

`qnap-photosphere` 是一款专为威联通 (QNAP) NAS（x86_64 架构，典型机型如 TS-873A）量身打造的原生 **QPKG 相册与智能媒体管理应用**。

它旨在彻底替代威联通官方 QuMagie，兼具**自动化照片清洗整理与物理归档**，以及**面向多家庭成员的现代化全能智能相册**体验。

---

## 核心技术与架构特性

- **纯原生 QPKG 打包**：通过 QDK 打包为 `.qpkg` 原生格式，集成在 QTS / QuTS hero App Center 中，开箱即用，具备独立桌面图标，零 Docker/Python 污染。
- **高并发极速单二进制架构**：后端采用 **Go (Golang)** 构建，静态编译并内嵌前端资源，单文件即可驱动全套业务与 I/O 流水线。
- **单存储池与 QTS SSD 高速缓存自适应**：
  - 由于 M.2 NVMe SSD 被 QTS 作为系统全局只读/读写高速缓存（SSD Cache Acceleration），所有应用数据与原图统一存放于机械硬盘主存储池（如 `/share/Photos` 与 `/share/.qphotosphere`）。
  - 核心热数据（SQLite 数据库、高频访问的 WebP 响应式缩略图缓存、AI 向量索引）因高频顺序/随机读取，会**被 QTS 自动命中并钉在 SSD 缓存层**中，无需手动跨卷拆分配置，运维更省心且兼顾极致读取性能。
  - **冷数据归档**：原图严格归档存放于 `/share/Photos/users/{username}/yyyymmdd/`，不修改原图文件名。
- **Immich 移动端协议兼容**：后端原生模拟兼容 Immich 客户端移动端 API，家庭成员手机直接使用现成成熟的 Immich iOS/Android App，即可无感后台自动备份原图与 Live Photo。
- **多用户隔离与家庭共享**：支持物理目录级的用户完全隔离，同时支持数据库级的家庭虚拟共享大相册。
- **CPU 友好型轻量 AI 推理引擎**：
  - 基于 **ONNX Runtime (C-Go 绑定)** 直接进行 CPU 推理（针对 AMD Ryzen V1500B x86_64 AVX2 指令集优化）。
  - **软硬双重智能调度**：新照片即时入库（轻量提取 EXIF、快速生成 WebP 缩略图）；重度 AI（人脸聚类检测、CLIP 语义搜索、全库 pHash 对比）限制在低负载或夜间可配置时间窗口运行，自带 CPU 动态负载保护（>75% 自动避让）。
- **专业级挑片与清洗工作流**：
  - 支持键盘快捷键流（`P` Pick 保留、`X` Reject 淘汰、`1-5` 星级打标）。
  - 双重重复检测：精确哈希 (SHA256) 拦截 + 视觉感知哈希 (pHash) 连拍/相似合并对比。
  - EXIF 元数据缺失人工复核队列，支持 ExifTool 标准写回图片。

---

## 文档导航

本项目文档严格遵循 Vibe Spec 架构规范，为后续 AI 辅助编码提供完备上下文：

- [PRD.md](file:///Users/aubreyhan/Tools/git_repo/PersonalDev/DevDoc/qnap-photosphere/PRD.md)：完整产品需求文档、用户画像、核心用例与验收条件。
- [ARCHITECTURE.md](file:///Users/aubreyhan/Tools/git_repo/PersonalDev/DevDoc/qnap-photosphere/ARCHITECTURE.md)：系统架构设计、冷热分层、Mermaid 流程图、QPKG 打包规约与代码目录树。
- [SPEC.md](file:///Users/aubreyhan/Tools/git_repo/PersonalDev/DevDoc/qnap-photosphere/SPEC.md)：数据模型定义 (SQLite DDL)、Immich 兼容 API 规范、任务调度状态机。
- [IMPLEMENTATION_PLAN.md](file:///Users/aubreyhan/Tools/git_repo/PersonalDev/DevDoc/qnap-photosphere/IMPLEMENTATION_PLAN.md)：分阶段原子化 Vibe Coding 实施路线图与测试验收命令。
