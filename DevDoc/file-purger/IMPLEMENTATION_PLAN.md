# FilePurger Vibe Coding 实施路线图 (IMPLEMENTATION PLAN)

> **使用指南**:
> 本文档专门面向在独立工程仓库中使用 Antigravity / AI 编程助手进行 Vibe Coding 时使用。
> 整个开发流程拆解为 5 个有序阶段（Phases）与原子级任务（Atomic Tasks）。
> 开发者在新工程仓库中可逐项下达指令（例如：*“请根据 file-purger/IMPLEMENTATION_PLAN.md 执行 Phase 1.1”*）。

---

## 阶段一：工程脚手架与基础环境搭建 (Phase 1: Scaffolding)

### 任务 1.1：初始化 Tauri 2.x + React + TypeScript + Tailwind CSS 脚手架
- **目标**: 创建标准 Tauri 2.0 跨平台桌面端应用骨架，配置前端样式与图标系统。
- **操作步骤**:
  1. 使用 `pnpm create tauri-app@latest` 初始化工程，选用 `React` + `TypeScript` 模板。
  2. 安装前端核心依赖：
     - UI 图标与工具: `lucide-react`, `clsx`, `tailwind-merge`
     - 虚拟滚动与组件: `@tanstack/react-virtual`
     - 状态管理: `zustand`
     - 提示通知组件: `sonner`
  3. 配置 `tailwind.config.js` 与 `index.css`，配置现代 macOS / Windows 沉浸式暗色与亮色主题变量。
- **验证指令**:
  ```bash
  pnpm install
  pnpm tauri dev
  ```
- **验收标准**: 应用主窗口正常拉起，空白窗口正常呈现，无控制台警告，Tailwind 样式类生效。

---

### 任务 1.2：配置 Rust 后端核心依赖库
- **目标**: 在 `src-tauri/Cargo.toml` 中引入所有必要的底层系统依赖。
- **操作步骤**:
  1. 在 `[dependencies]` 下引入：
     ```toml
     [dependencies]
     tauri = { version = "2.0", features = ["tray-icon", "notification"] }
     serde = { version = "1.0", features = ["derive"] }
     serde_json = "1.0"
     tokio = { version = "1", features = ["full"] }
     jwalk = "0.8"               # 多线程快速目录遍历
     rayon = "1.10"              # 并行计算库
     trash = "5.2"               # 原生系统废纸篓调用
     chrono = { version = "0.4", features = ["serde"] }
     uuid = { version = "1.8", features = ["v4"] }
     dirs = "5.0"                # 跨平台标准目录查找 (Downloads, Desktop 等)
     regex = "1.10"
     ```
  2. 运行 `cargo check` 验证所有依赖正常拉取并无版本冲突。
- **验证指令**:
  ```bash
  cd src-tauri && cargo check
  ```
- **验收标准**: `cargo check` 退出码为 0，依赖解析无报错。

---

## 阶段二：Rust 后端核心引擎研发 (Phase 2: Core Native Engine)

### 任务 2.1：实现系统路径安全保护守卫 (`core/path_guard.rs`)
- **目标**: 构建路径拦截机制，杜绝扫描与操作任何系统保留路径。
- **操作步骤**:
  1. 建立全局黑名单路径集合（跨平台适配：macOS `/`, `/System`, `/Library`, `/Applications`；Windows `C:\Windows`, `C:\Program Files` 等）。
  2. 实现 `PathGuard::is_protected(path: &Path) -> bool`。
  3. 实现路径规范化（`canonicalize`），防范 `../` 越界逃逸与恶意符号链接。
  4. 编写对应的单元测试，覆盖常见非法路径与合法下载目录。
- **验证指令**:
  ```bash
  cargo test path_guard
  ```
- **验收标准**: 所有单元测试用例通过，非法系统目录均被准确识别并拦截。

---

### 任务 2.2：定义数据模型与复合规则评估器 (`core/rule_engine.rs`)
- **目标**: 实现对单个文件元数据的复合条件判定。
- **操作步骤**:
  1. 在 `models/rule.rs` 中定义 `PurgeRule` 结构体。
  2. 在 `core/rule_engine.rs` 中实现 `RuleEvaluator::matches(path: &Path, metadata: &Metadata, rule: &PurgeRule) -> bool`：
     - **时间比对**: 计算当前时间与 `metadata.modified()` 差值，换算为天数，比对是否超出阈值。
     - **大小比对**: 比对 `metadata.len() >= min_size_bytes`。
     - **扩展名匹配**: 提取文件后缀转小写，比对黑白名单列表。
     - **排除模式**: 忽略隐藏文件与自定义排除通配符。
  3. 编写针对各组合规则的单元测试。
- **验证指令**:
  ```bash
  cargo test rule_engine
  ```
- **验收标准**: 时间、大小、后缀、排除模式组合评估逻辑 100% 覆盖并通过。

---

### 任务 2.3：构建高性能多线程扫描引擎 (`core/scanner.rs`)
- **目标**: 使用 `jwalk` 实现多线程遍历，集成流式进度通知与取消支持。
- **操作步骤**:
  1. 实现 `Scanner::scan_directory(target_dirs, rules, progress_callback, cancel_flag)`。
  2. 启用多线程并行，设置不追踪软链接 (`follow_links(false)`)。
  3. 每遍历 50 项命中文件或每 100ms 触发一次批次回调。
  4. 支持原子布尔变量 `AtomicBool` 随时响应前端取消信号。
- **验证指令**:
  ```bash
  cargo test scanner -- --nocapture
  ```
- **验收标准**: 在包含 10,000+ 个文件的测试目录下运行扫描，在 1 秒内完成匹配并输出流式统计。

---

## 阶段三：安全清理引擎与 IPC 桥梁打通 (Phase 3: Trash & IPC)

### 任务 3.1：集成原生废纸篓与操作审计 (`core/trash_manager.rs`)
- **目标**: 调用系统废纸篓 API 安全删除文件，并记录操作历史。
- **操作步骤**:
  1. 实现 `TrashManager::purge_to_trash(paths: Vec<PathBuf>) -> PurgeSummary`。
  2. 循环调用 `trash::delete(&path)`，捕获文件不存在或文件占用等异常并优雅跳过。
  3. 统计成功释放的字节总数、成功移入数与跳过数。
  4. 将清理批次结果序列化追加写入本地 `~/.config/file-purger/history.json`。
- **验证指令**:
  ```bash
  cargo test trash_manager
  ```
- **验收标准**: 在临时测试目录创建临时文件，执行清理后文件在系统废纸篓中存在，测试目录中已移除。

---

### 任务 3.2：暴露 Tauri Commands 与 Event 通道 (`commands/*.rs`)
- **目标**: 将扫描、取消、清理、打开访达/资源管理器等接口暴露给前端。
- **操作步骤**:
  1. 实现 `start_scan` 指令：接收参数，在后台 Tokio Task 中执行扫描，通过 `app_handle.emit("scan:batch", ...)` 发送事件。
  2. 实现 `cancel_scan` 指令：修改取消标记。
  3. 实现 `purge_files` 指令：执行废纸篓移动并返回统计结果。
  4. 实现 `reveal_in_folder` 指令：macOS 调用 `open -R <path>`，Windows 调用 `explorer /select,<path>`。
  5. 注册上述 commands 到 `tauri::Builder`。
- **验证指令**:
  ```bash
  cargo build
  ```
- **验收标准**: Rust 后端编译无警告无报错。

---

## 阶段四：前端 UI 界面与交互系统研发 (Phase 4: Frontend UI)

### 任务 4.1：前端状态管理与 Tauri IPC 客户端封装 (`src/store/` & `src/api/`)
- **目标**: 建立高内聚的前端状态流与强类型 API 调用层。
- **操作步骤**:
  1. 封装 `src/api/scan.ts`、`src/api/purge.ts`，提供带 TypeScript 类型声明的 `invoke` 与事件监听器。
  2. 搭建 `useScanStore`（管理目标目录、扫描状态、流式结果列表、选择项、统计值）。
  3. 搭建 `useRuleStore`（管理天数滑块、大小选择器、扩展名多选、预设模板切换）。
- **验证指令**:
  ```bash
  pnpm tsc --noEmit
  ```
- **验收标准**: TypeScript 类型检查完全通过，无 any 逃逸。

---

### 任务 4.2：目录选择器与规则配置面板组件 (`components/`)
- **目标**: 实现直观、易用的目标路径添加与复合规则调节栏。
- **操作步骤**:
  1. 编写 `DirectorySelector.tsx`：展示常用一键添加卡片（如 `Downloads 目录`、`Desktop 目录`）、提供自定义目录选择弹窗。
  2. 编写 `RuleFilterBar.tsx`：
     - 时间选择器：快速药丸按钮（`30天`、`60天`、`90天`、`半年`、`1年`）与天数滑块。
     - 文件大小输入：支持快速勾选 `>10MB`, `>50MB`, `>100MB`, `>1GB`。
     - 扩展名选择器：常用类型标签（安装包/压缩包/日志临时文件）可多选勾选。
- **验证指令**:
  ```bash
  pnpm build
  ```
- **验收标准**: 组件渲染整洁，响应用户点击即刻同步更新 Zustand 规则状态。

---

### 4.3：虚拟滚动结果列表与操作底栏 (`FileResultTable.tsx` & `SummaryFooter.tsx`)
- **目标**: 针对万级文件列表实现极速丝滑的虚拟渲染，提供便捷单项与批量控制。
- **操作步骤**:
  1. 使用 `@tanstack/react-virtual` 搭建虚拟列表表格，支持按文件大小降序/时间排序。
  2. 列表项展示：选择框、文件图标、文件名、所在路径简写、大小、距今修改天数、“在访达中显示”按钮。
  3. 编写 `SummaryFooter.tsx`：
     - 左侧展示已选统计（例如：“已勾选 38 项，共 14.8 GB”）。
     - 右侧主操作按钮：「安全移入废纸篓」，点击后弹出防误触次级确认提示（明示文件仅移入废纸篓，可从系统废纸篓放回）。
- **验证指令**:
  ```bash
  pnpm dev
  ```
- **验收标准**: 模拟渲染 20,000 条数据时，滚动丝滑无丢帧（60fps），勾选反选无延迟。

---

## 阶段五：历史审计、轻量定时提醒与端到端验收 (Phase 5: E2E Validation)

### 任务 5.1：实现历史记录审计面板与规则预设持久化
- **目标**: 用户随时查阅累计释放空间与历史批次明细，保存常用清理模板。
- **操作步骤**:
  1. 编写 `HistoryModal.tsx`，以时间轴形式展示过往清理记录，包含日期、释放空间、文件数量与清理目录。
  2. 编写预设保存对话框，支持将当前配置存为命名预设，随时下拉切换。
- **验证指令**:
  ```bash
  pnpm tauri dev
  ```
- **验收标准**: 清理完成后历史记录立刻新增一条，重启应用后历史记录与自定义预设依然保留。

---

### 任务 5.2：配置轻量定时扫描提醒与系统通知
- **目标**: 用户开启每周/每月提醒后，后台静默巡检并弹窗建议清理。
- **操作步骤**:
  1. 集成 `@tauri-apps/plugin-notification`。
  2. 在设置中添加“每周提醒我清理下载目录”开关。
  3. 到达周期且检测到符合条件旧文件超过阈值（如 5GB）时，发送桌面原生通知，引导用户点开应用。
- **验收标准**: 系统通知按预期弹出，点击通知成功拉起应用主界面。

---

### 任务 5.3：端到端完整闭环验证 (End-to-End Walkthrough)
- **目标**: 模拟用户真实使用场景，完成全链路验证。
- **验证清单**:
  1. [ ] 启动应用，添加真实临时测试目录（内置若干 >30 天的 `.dmg`, `.zip` 大小文件）。
  2. [ ] 调整规则为“修改时间 > 15 天，体积 > 10MB”。
  3. [ ] 点击「开始扫描」，流式进度条显示正常，扫描结果正确列出全部目标文件。
  4. [ ] 点击某文件的“在访达中定位”，系统原生文件管理器正确拉起并高亮该文件。
  5. [ ] 点击「移入废纸篓」，确认删除后文件移入系统回收站。
  6. [ ] 打开 macOS 废纸篓，右键点击测试文件“放回原处”，文件完好放回原目录。
  7. [ ] 查看 FilePurger「历史记录」，核对本次清理批次数据精确无误。
