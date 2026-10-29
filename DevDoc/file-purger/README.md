# FilePurger (智能旧文件安全清理助手)

[![Tauri](https://img.shields.io/badge/Tauri-2.x-blue.svg)](https://tauri.app/)
[![Rust](https://img.shields.io/badge/Rust-1.75+-orange.svg)](https://www.rust-lang.org/)
[![React](https://img.shields.io/badge/React-18-61dafb.svg)](https://react.dev/)
[![TailwindCSS](https://img.shields.io/badge/TailwindCSS-v3-38bdf8.svg)](https://tailwindcss.com/)

> **FilePurger** 是一款基于 **Tauri 2.x (Rust) + React + Tailwind CSS** 构建的现代化轻量级跨平台桌面端旧文件清理工具。
> 专为解决个人电脑（macOS / Windows / Linux）长期累积陈旧下载文件、冗余缓存、废弃镜像包导致的磁盘告急问题而设计。
> 凭借 Rust 多线程极速遍历与原生系统废纸篓安全集成，为用户提供**极速扫描、复合规则筛选、零风险删除（默认入回收站）与清理历史审计**的卓越体验。

---

## 核心设计理念 (Core Philosophies)

1. **绝对安全第一 (Safety First)**:
   - **默认系统回收站**: 删除操作调用操作系统原生 API（通过 Rust `trash` crate）移至系统废纸篓/回收站，误删可在系统回收站中一键放回。
   - **系统核心目录隔离**: 内置关键系统目录与敏感路径（如 `/System`, `/Library`, `C:\Windows` 等）硬编码保护屏障，杜绝误操作。
   - **全景预览与精细勾选**: 扫描完成后提供分类筛选、大小排序、路径查看与单项反选功能，用户拥有最终决定权。

2. **极致性能与低资源占用 (Blazing Fast & Ultra Lightweight)**:
   - 基于 Rust `rayon` / `jwalk` 多线程并行遍历，数十万级文件几秒内完成元数据提取。
   - Tauri 2.x 原生编译，安装包仅十几兆，内存占用低于 60MB（相比 Electron 节省 80%+ 资源）。

3. **灵活可控的复合规则 (Flexible Composite Rules)**:
   - 自由组合：**距今未修改天数 (mtime) / 未访问天数 (atime)** + **文件扩展名/类型** + **文件体积阈值**。
   - 支持预设模板（如“下载目录瘦身”、“大型过期镜像/安装包”、“开发缓存包”）与自定义规则。

4. **主动提醒与非侵入设计 (Non-Intrusive & Thoughtful)**:
   - 主流程完全由用户手动触发确认。
   - 可选开启轻量级定时扫描提醒（如每周/每月自动提醒），仅推送系统通知建议清理，**绝不在后台擅自静默删除**。

---

## 文档导航 (Documentation Index)

本项目为规范化 Vibe Coding 研发体系，所有规格文档已完备归档：

| 文档名称 | 核心内容说明 | 直接阅读入口 |
| :--- | :--- | :--- |
| **产品需求文档 (PRD)** | 用户角色、典型使用场景、核心功能需求、非功能指标、详细验收标准清单 | [PRD.md](file:///Users/aubreyhan/Tools/git_repo/PersonalDev/DevDoc/file-purger/PRD.md) |
| **技术架构设计 (ARCHITECTURE)** | 整体分层架构图、Rust 后端与 React 前端职责划分、目录结构、IPC 通信机制与状态设计 | [ARCHITECTURE.md](file:///Users/aubreyhan/Tools/git_repo/PersonalDev/DevDoc/file-purger/ARCHITECTURE.md) |
| **详细规格说明 (SPEC)** | 数据模型 (TypeScript/Rust)、Tauri IPC 接口清单、状态机、错误码与边界保护机制 | [SPEC.md](file:///Users/aubreyhan/Tools/git_repo/PersonalDev/DevDoc/file-purger/SPEC.md) |
| **实施路线图 (IMPLEMENTATION PLAN)** | 面向 AI 的分阶段原子级开发任务清单、阶段验收指令与端到端验证步骤 | [IMPLEMENTATION_PLAN.md](file:///Users/aubreyhan/Tools/git_repo/PersonalDev/DevDoc/file-purger/IMPLEMENTATION_PLAN.md) |

---

## 快速上手与技术栈概览 (Tech Stack)

### 架构栈一览
- **宿主与核心底座**: Tauri 2.x
- **后端 (Native)**: Rust (edition 2021)
  - `jwalk` / `rayon`: 多线程并行目录快速扫描
  - `trash`: 跨平台安全删除（macOS / Windows / Linux 原生废纸篓接口）
  - `serde` / `serde_json`: 高性能序列化传输
  - `chrono`: 精确文件时间处理与计算
  - `rusqlite` / `sled` (可选) 或 JSON 本地持久化: 规则预设与清理历史审计
- **前端 (UI/UX)**:
  - 核心框架: React 18 + Vite + TypeScript
  - 样式设计: Tailwind CSS + Lucide React 图标库
  - 状态管理: Zustand (轻量极简状态流转)
  - UI 动画与反馈: Framer Motion + Sonner Toast

---

## 声明

本项目（`file-purger`）位于 `DevDoc` 文档工程下，本目录专注技术方案设计与标准规格沉淀。实际业务编码与发布将在独立的工程仓库中开展。
