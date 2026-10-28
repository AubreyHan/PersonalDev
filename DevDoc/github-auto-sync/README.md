# GitHub 自动同步托盘应用 (GitHub Auto Sync)

> 基于 **Tauri 2.0 (Rust) + React + Tailwind CSS** 构建的轻量级、低内存桌面系统托盘应用。实现本地工作区多 Git 仓库的自动发现、变动监听与全自动按策略同步。

---

## 📖 项目定位与核心特性

* **系统托盘极简常驻**：启动后最小化至 macOS 菜单栏 / Windows 系统托盘，内存占用仅约 30MB。托盘右键仅保留“打开主界面”与“退出应用”。
* **双栏式工作区管理**：
  * **左栏（文件夹管理）**：顶部固定【全部文件夹】汇总视图；下方支持添加/移除本地工作根目录。
  * **右栏（Git Repo 列表）**：自动递归扫描所有子目录中的 Git 仓库，并实时监听文件系统变动（新建/克隆/删除仓库时无感自动更新）。
* **原子化四种同步策略（Repo 级右键切换）**：
  * 🟢 **双向同步（新覆盖旧）**：拉取与推送双向同步，冲突时以更新时间戳最新的一方优先。
  * 🔵 **本地优先（本地覆盖远端）**：本地修改为绝对基准，自动推送并覆盖远端。
  * 🟣 **远端优先（远端覆盖本地）**：远端仓库为绝对基准，本地自动对齐放弃未保存差异。
  * ⚪ **停止同步**：新扫描到的 Repo 默认策略，停止一切后台同步。
* **二合一智能触发引擎**：
  * 本地代码变动即时监听（5 秒防抖延迟自动触发）。
  * 远端定时轮询检查（定时拉取最新更新）。
* **详尽的 Commit Message 自动生成**：自动汇总本次提交的变动文件列表与时间戳（如 `auto-sync: 2026-10-27 14:05:00 - updated: App.tsx, added: config.json`）。
* **零配置复用本机 Git 凭证**：直接调度宿主机系统 `git` 命令，完全复用已有的 SSH Key 与 Git Credential Manager，无需在应用内二次输入 GitHub Token。
* **原生日志控制台与包体内聚 FIFO 存储**：提供可视化运行日志查看器；日志保存在 App 包体目录下（随 App 删除一并抹除）；日志大小严格按 FIFO 环形机制受控在 1.0 MB 以内，永不膨胀。

---

## 📚 文档索引

本项目已由 `vibe-spec-architect` 梳理为完整的 Vibe Coding 规范文档体系，可供 AI 编程助手直接分步实施：

| 文档名称 | 核心内容说明 |
| :--- | :--- |
| [**PRD.md**](./PRD.md) | 产品需求文档：用户角色、核心用例、8大功能规格与严谨的验收标准 (Acceptance Criteria) |
| [**ARCHITECTURE.md**](./ARCHITECTURE.md) | 系统技术架构：Tauri 2.0 架构图、Rust 后端与 React 前端职责划分、目录结构与状态流转 |
| [**SPEC.md**](./SPEC.md) | 详细技术规格：TypeScript/Rust 数据模型、Tauri IPC 接口规范、三种同步算法与 Commit 信息格式 |
| [**IMPLEMENTATION_PLAN.md**](./IMPLEMENTATION_PLAN.md) | **Vibe Coding 执行路线图**：拆解为 6 个 Phase、18 个原子执行任务与确切验证命令 |

---

## 🛠️ 快速上手与技术选型

* **后端/桌面外壳**：Tauri 2.0 + Rust (`notify` 监听文件, `tokio` 异步调度, `std::process::Command` 调度本机 git)
* **前端界面**：React 18 + Vite + TypeScript + Tailwind CSS + Lucide React + Zustand
* **环境要求**：Node.js >= 18, Rust >= 1.75, 系统已安装 Git

```bash
# 启动开发模式
npm run tauri dev

# 编译构建发布包
npm run tauri build
```

---

## 📝 变更日志 (Changelog)

* **2026-10-27 (v1.0.0-spec)**: 初始化项目规范，完成 PRD、ARCHITECTURE、SPEC 和 IMPLEMENTATION_PLAN 全套文档。
