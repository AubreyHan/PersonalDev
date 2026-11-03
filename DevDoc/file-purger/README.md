# FilePurger (深度规则驱动的智能文件清理与自动化管理工具)

[![Tauri](https://img.shields.io/badge/Tauri-2.x-blue.svg)](https://tauri.app/)
[![Rust](https://img.shields.io/badge/Rust-1.75+-orange.svg)](https://www.rust-lang.org/)
[![React](https://img.shields.io/badge/React-18-61dafb.svg)](https://react.dev/)
[![TailwindCSS](https://img.shields.io/badge/TailwindCSS-v3-38bdf8.svg)](https://tailwindcss.com/)

> **FilePurger** 是一款基于 **Tauri 2.x (Rust) + React 18 + Tailwind CSS** 构建的轻量级跨平台系统托盘应用。
> 专为追求极致秩序与磁盘自由的重度用户、开发者设计，定位为**“深度自定义通用文件清理与自动化管理引擎”**。
>
> 告别死板的单次全盘扫描，FilePurger 引入**多目录独立策略体系 (Policy-Driven)**、**高级复合匹配引擎 (Regex + Glob + 多维条件)**、**多样化处置动作 (回收站/动态归档/Zip压缩/永久粉碎)** 与 **全能混合调度 (定时 Cron + 实时文件变动 Watcher + 托盘一键即时触发)**，以不足 40MB 的托盘常驻内存，守护工作区与磁盘的绝对整洁。

---

## 核心架构特色 (Key Architectural Highlights)

1. **多目录独立策略架构 (Multi-Directory Policy-Driven)**:
   - 每个目标文件夹可挂载一条或多条独立策略（如 `Downloads 自动化周清`、`开发构建缓存即时粉碎`、`设计工程素材按月 Zip 归档`）。
   - 策略间互不干扰，支持独立启用/停用、单独调试与一键手动跑批。

2. **高级复合匹配引擎 (Advanced Multi-Tier Matching Engine)**:
   - **模式匹配**: 支持 **正则表达式 (Regex)** 与 **Glob 通配符**，内置即时正则语法测试器与命中沙盒预览。
   - **时间与元数据**: 支持针对 `修改时间 (mtime)`、`访问时间 (atime)`、`创建时间 (btime/ctime)` 设定精确天/小时级阈值。
   - **尺寸区间与属性**: 最小/最大字节区间，支持排除隐藏文件、排除系统点文件与多层级白名单防线。

3. **多样化处置动作流 (Flexible Action Handlers)**:
   - **安全入回收站 (Trash)**: 默认调用 macOS / Windows 原生废纸篓 API，误删一键无损放回。
   - **智能动态归档 (Archive Move)**: 移动到归档目录，支持按日期动态变量命名（如 `/Archives/{YYYY}/{MM}/`）。
   - **Zip 压缩归档 (Compress & Archive)**: 批量命中文件就地打包为 `.zip` 压缩归档并清除源文件，释放 80%+ 磁盘空间。
   - **永久物理擦除 (Hard Delete)**: 针对临时构建垃圾或安全缓存，跳过废纸篓直接擦除（附带严格安全保护拦截与二次防误触确认）。

4. **全能混合调度机制 (Hybrid Scheduling Engine)**:
   - **实时变动监听 (Realtime File Watcher)**: 基于 Rust `notify` crate 监听目标目录文件创建与变动，配合智能防抖 (Debounce)，文件落地达标即刻触发处理。
   - **定时周期调度 (Cron Scheduler)**: 支持灵活 Cron 表达式与预设快捷调度（如每日午夜、每周五 18:00）。
   - **托盘菜单即时触发 (Tray Quick Actions)**: 驻留 macOS 状态栏 / Windows 系统托盘，右键一键触发特定策略或全量检查。

5. **硬核安全与审计追溯 (Hardened Safety & Audit Trail)**:
   - **系统核心路径绝对隔离 (PathGuard)**: 硬编码拦截系统保护目录（如 `/System`, `/Library`, `C:\Windows` 等），禁止任何违规策略挂载。
   - **审计日志流水**: 本地记录每次触发时间、策略名、处理文件清单、释放容量与执行结果，支持操作溯源。

---

## 规格文档导航 (Documentation Index)

本项目为规范化 Vibe Coding 研发体系，所有规格文档已全面重构并归档：

| 文档名称 | 核心内容说明 | 直接阅读入口 |
| :--- | :--- | :--- |
| **产品需求文档 (PRD)** | 全新产品定位、用户画像、核心功能规格 (FR-1 ~ FR-7)、非功能指标、明确不做的边界与详细验收标准 | [PRD.md](file:///Users/aubreyhan/Tools/git_repo/PersonalDev/DevDoc/file-purger/PRD.md) |
| **技术架构设计 (ARCHITECTURE)** | 托盘与后台常驻架构图、Rust 后端各核心引擎划分 (Scanner/Watcher/Engine/Purger)、IPC 事件流与安全防护模型 | [ARCHITECTURE.md](file:///Users/aubreyhan/Tools/git_repo/PersonalDev/DevDoc/file-purger/ARCHITECTURE.md) |
| **详细规格说明 (SPEC)** | 策略模型 (Policy/Rule/Action)、Tauri 2.x IPC 接口与 Event 通道、状态机、通配符/正则语法规范与异常矩阵 | [SPEC.md](file:///Users/aubreyhan/Tools/git_repo/PersonalDev/DevDoc/file-purger/SPEC.md) |
| **实施路线图 (IMPLEMENTATION PLAN)** | 面向 AI 的分阶段原子级开发任务清单、阶段验收指令与端到端验证步骤 | [IMPLEMENTATION_PLAN.md](file:///Users/aubreyhan/Tools/git_repo/PersonalDev/DevDoc/file-purger/IMPLEMENTATION_PLAN.md) |

---

## 快速上手与技术栈概览 (Tech Stack)

### 架构栈一览
- **宿主底座**: Tauri 2.x (跨平台桌面托盘与窗口通信框架)
- **后端引擎 (Rust Native)**:
  - `notify`: 跨平台高效文件系统变动监听 (macOS FSEvents / Windows ReadDirectoryChangesW)
  - `regex` & `globset`: 高性能正则表达式与通配符预编译匹配
  - `trash`: 跨平台原生废纸篓安全集成 (支持外置硬盘)
  - `zip`: 原生多流 Zip 压缩引擎
  - `tokio`: 异步事件循环、定时调度器与防抖去重队列
  - `serde` / `serde_json`: 策略与审计数据持久化
- **前端控制台 (React 18 + TypeScript)**:
  - 核心框架: React 18 + Vite + TypeScript
  - 样式设计: Tailwind CSS + Lucide React 图标体系
  - 状态管理: Zustand (极简策略与流式日志状态)
  - 高性能列表: TanStack Virtual (虚拟滚动承载万级匹配文件预览)
  - 交互反馈: Sonner Toast + Framer Motion 微动效

---

## 声明

本项目（`file-purger`）位于 `DevDoc` 文档工程下，本目录专注技术方案设计与标准规格沉淀。实际业务编码与发布将在独立的工程仓库中开展。
