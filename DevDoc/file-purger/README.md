# FilePurger (macOS 纯菜单栏旧文件自动清理助手)

[![Platform](https://img.shields.io/badge/Platform-macOS%2012+-black.svg)](https://apple.com/macos)
[![Tauri](https://img.shields.io/badge/Tauri-2.x-blue.svg)](https://tauri.app/)
[![Rust](https://img.shields.io/badge/Rust-1.75+-orange.svg)](https://www.rust-lang.org/)
[![React](https://img.shields.io/badge/React-18-61dafb.svg)](https://react.dev/)
[![TailwindCSS](https://img.shields.io/badge/TailwindCSS-v3-38bdf8.svg)](https://tailwindcss.com/)

> **FilePurger** 是一款专为 **macOS** 深度定制的轻量级纯菜单栏（Status Bar）旧文件自动清理工具。
> 致力于解决 Mac 用户日常“下载 (Downloads)”目录爆满、时效性临时文件堆积、系统废纸篓缺乏自动容量管理的核心痛点。
>
> 秉承极简纯粹的设计哲学：**纯菜单栏常驻（Dock 栏不驻留）、左键直出控制菜单、多目录独立瀑布式规则、FSEvents 内核级零压力监听（CPU 0.0%）、置顶白名单免死保护与废纸篓容量 FIFO 自动清倒**，让 Mac 磁盘始终保持极致清爽。

---

## 核心设计与特性亮点 (Core Highlights)

1. **macOS 纯菜单栏形态 (Pure Status Bar App)**:
   - 配置 `LSUIElement = true`，Dock（程序坞）栏**完全不显示图标**。
   - 启动后自动静默最小化到顶部菜单栏；左键单击图标直接弹出原生下拉菜单：【打开主界面】、【✓ 开机自启动】、【彻底卸载本应用】、【退出应用】。
   - 默认开启开机自启；支持一键干净彻底卸载（Clean Uninstall），注销登录项并擦除全部本地配置，0 残留。

2. **双栏界面与严密的目录层级防线**:
   - **左侧顶部常驻回收站**: 不可删除、不可移出。
   - **智能预置**: 首次启动默认预置 macOS 用户 `~/Downloads` 目录。
   - **父子目录排他拦截**: 若尝试添加现有目录的父文件夹或子文件夹，系统即刻弹出明确警告并**坚决拒绝添加**，从源头杜绝嵌套冲突；同时严禁添加 macOS 系统核心目录。
   - **安全移除**: 随时从监控列表中删除文件夹，物理磁盘文件不受任何影响，无弹窗打扰。

3. **系统废纸篓双轮管家 (Trash Controller)**:
   - 规则 1：超龄天数自动清除（填 0 不执行，默认 0）。
   - 规则 2：废纸篓容量上限 (GB) 自动控制（填 0 不执行，默认 0）。超出容量时按 **FIFO（以文件移入废纸篓的时间戳）** 逐个彻底物理删除，直至容量回落达标。

4. **瀑布式优先规则引擎 (Waterfall Rule Engine)**:
   - **置顶保留规则（白名单最高优先级）**: 每个文件夹常驻置顶，不可调整顺序、不可删除。只要填入通配符即生效，匹配的项及其内部子文件永久免死保留。
   - **普通规则优先匹配 (First-Match 短路机制)**: 从上到下依次比对，一旦命中首条规则即刻执行并终止匹配。支持自由拖拽调整顺序，每条规则支持独立启用/停用 Toggle。
   - **规则核心维度**:
     - `Date Added`: 基于 macOS 原生 `kMDItemDateAdded`（添加到该文件夹的时间），支持分/小时/天，选 Any 或填 0 为不限时间。
     - `Pattern`: 组合路径通配符语法（大小写严格敏感）。支持 `**` 递归、`*/` 单层、`*a` 主干结尾、`a*.j*` 以及逗号分隔多模式（OR 关系）。
   - **四大处置动作**:
     1. **列出 (List Matches)**: 仅在下方清单中列出展示，不做任何删除；
     2. **保留 (Keep)**: 命中后锁定保留，停止后续规则；
     3. **直接删除 (Directly Move to Trash)**: 直接移入系统废纸篓；
     4. **询问后删除 (Ask before Trashing)**: 应用内弹出确认卡片，对满足条件的文件**严格逐一手动确认**。

5. **全局安全与零压力内核监听**:
   - 默认免疫所有以 `.` 开头的隐藏文件与系统保留文件；若整份子文件夹触发删除，允许连带清除其内部隐藏文件。
   - 基于 macOS 内核级 **`FSEvents`** 实时监听变动，配合 3 秒智能防抖（Debounce），无变动时 CPU 占用率为 **0.0%**，常驻内存低于 **30MB**；辅以 1 小时一次的轻量心跳巡检自然超龄。

---

## 规格文档导航 (Documentation Index)

本项目为规范化 Vibe Coding 研发体系，所有规格文档已全面归档：

| 文档名称 | 核心内容说明 | 直接阅读入口 |
| :--- | :--- | :--- |
| **产品需求文档 (PRD)** | 纯菜单栏定位、用户用例、FR-1 ~ FR-7 详细需求、非功能指标、明确不做的边界与详细验收标准 | [PRD.md](file:///Users/aubreyhan/Tools/git_repo/PersonalDev/DevDoc/file-purger/PRD.md) |
| **技术架构设计 (ARCHITECTURE)** | macOS 深度集成机制 (`LSUIElement`, `FSEvents`, `kMDItemDateAdded`)、分层架构图、瀑布流与防抖时序图 | [ARCHITECTURE.md](file:///Users/aubreyhan/Tools/git_repo/PersonalDev/DevDoc/file-purger/ARCHITECTURE.md) |
| **详细规格说明 (SPEC)** | 契约模型 (Folder/Rule/Trash/AskQueue)、Tauri 2.x IPC 接口协议、通配符语法标准与 10 大边界异常矩阵 | [SPEC.md](file:///Users/aubreyhan/Tools/git_repo/PersonalDev/DevDoc/file-purger/SPEC.md) |
| **实施路线图 (IMPLEMENTATION PLAN)** | 面向 AI 的 6 大阶段原子级任务清单（含详细步骤、验证指令与全链路走查标准） | [IMPLEMENTATION_PLAN.md](file:///Users/aubreyhan/Tools/git_repo/PersonalDev/DevDoc/file-purger/IMPLEMENTATION_PLAN.md) |

---

## 快速上手与技术栈概览 (Tech Stack)

### 架构栈一览
- **宿主底座**: Tauri 2.x (macOS Native Menu Bar 模式)
- **后端引擎 (Rust Native)**:
  - `notify`: macOS 原生 FSEvents 内核变动监听
  - `globset`: 大小写敏感通配符预编译匹配
  - `trash`: 跨平台原生废纸篓集成
  - `cocoa` & `objc`: macOS 原生 `kMDItemDateAdded` 元数据读取与 `SMAppService` 登录项调用
  - `tokio`: 异步防抖队列与小时级心跳定时器
- **前端控制台 (React 18 + Tailwind CSS)**:
  - 核心框架: React 18 + Vite + TypeScript
  - 样式设计: Tailwind CSS (macOS 系统原生风格)
  - 状态管理: Zustand
  - 拖拽排序: `@hello-pangea/dnd`
  - 交互提示: Sonner Toast

---

## 声明

本项目（`file-purger`）位于 `DevDoc` 文档工程下，本目录专注技术方案设计与标准规格沉淀。实际业务编码与发布将在独立的工程仓库中开展。
