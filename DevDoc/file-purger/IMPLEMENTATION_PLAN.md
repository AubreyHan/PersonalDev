# FilePurger Vibe Coding 实施路线图 (IMPLEMENTATION PLAN)

> **使用指南**:
> 本文档面向在独立工程仓库中使用 Antigravity / AI 编程助手进行 Vibe Coding 时使用。
> 整个开发流程拆解为 6 个有序阶段（Phases）与原子级任务（Atomic Tasks）。
> 开发者在新工程仓库中可逐项下达指令（例如：*“请根据 file-purger/IMPLEMENTATION_PLAN.md 执行 Phase 1.1”*）。

---

## 阶段一：托盘常驻脚手架与依赖搭建 (Phase 1: Scaffolding & System Tray)

### 任务 1.1：初始化 Tauri 2.x + React + Tailwind CSS 脚手架
- **目标**: 初始化工程，配置现代托盘应用骨架与前端设计系统。
- **操作步骤**:
  1. 使用 `pnpm create tauri-app@latest file-purger` 初始化工程，选用 `React` + `TypeScript` 模板。
  2. 安装前端核心依赖：
     - UI 图标与工具: `lucide-react`, `clsx`, `tailwind-merge`
     - 虚拟滚动: `@tanstack/react-virtual`
     - 状态管理: `zustand`
     - 提示通知: `sonner`
  3. 配置 `tailwind.config.js` 与 `index.css`，配置现代 macOS / Windows 沉浸式暗色与亮色主题。
- **验证指令**:
  ```bash
  pnpm install
  pnpm tauri dev
  ```
- **验收标准**: 前端应用主窗口正常拉起，Tailwind 样式类生效，无控制台警告。

---

### 任务 1.2：配置 Rust 后端核心依赖与 Tauri 2.x 托盘特性
- **目标**: 在 `src-tauri/Cargo.toml` 中配置原生托盘、文件监听、正则与压缩依赖。
- **操作步骤**:
  1. 在 `src-tauri/Cargo.toml` 中声明依赖：
     ```toml
     [dependencies]
     tauri = { version = "2.0", features = ["tray-icon", "notification"] }
     serde = { version = "1.0", features = ["derive"] }
     serde_json = "1.0"
     tokio = { version = "1", features = ["full"] }
     notify = "6.1"                 # 跨平台文件变动监听 (FSEvents/inotify)
     regex = "1.10"                 # 正则表达式引擎 (线性时间保障)
     globset = "0.4"                # 高性能通配符编译匹配
     trash = "5.2"                  # 跨平台原生废纸篓集成
     zip = { version = "2.1", default-features = false, features = ["deflate"] }
     tokio-cron-scheduler = "0.10"  # 异步 Cron 定时调度器
     chrono = { version = "0.4", features = ["serde"] }
     uuid = { version = "1.8", features = ["v4"] }
     walkdir = "2.5"
     ```
  2. 在 `src-tauri/src/main.rs` 中初始化系统托盘（`TrayIconBuilder`），配置托盘图标、左键唤起控制台与右键菜单项骨架。
- **验证指令**:
  ```bash
  cd src-tauri && cargo check
  ```
- **验收标准**: `cargo check` 退出码为 0，系统托盘图标在 macOS 菜单栏或 Windows 任务栏正常出现。

---

## 阶段二：后端核心匹配引擎研发 (Phase 2: Core Matching Engine)

### 任务 2.1：实现系统路径安全保护守卫 (`core/path_guard.rs`)
- **目标**: 构建路径拦截机制，杜绝添加任何操作系统保留路径为目标或归档目录。
- **操作步骤**:
  1. 建立硬编码黑名单集合（`/`, `/System`, `/Library`, `/Applications`, `C:\Windows`, `C:\Program Files` 等）。
  2. 实现路径规范化防穿透（`Path::canonicalize`），防范 `../` 越界与软链跳转。
  3. 编写单元测试验证边界情况。
- **验证指令**:
  ```bash
  cargo test path_guard
  ```
- **验收标准**: 单元测试覆盖常见系统路径与常规目录，非法路径 100% 被识别拦截。

---

### 任务 2.2：构建正则与 Glob 预编译匹配引擎 (`core/matcher/`)
- **目标**: 实现对文件名的极速通配符与正则表达式匹配。
- **操作步骤**:
  1. 编写 `glob_engine.rs`：使用 `globset::GlobSetBuilder` 将多条通配符预编译为一个匹配器，单次遍历即时比对。
  2. 编写 `regex_engine.rs`：封装带有语法预校验与 ReDoS 防御的正则匹配器，并提供测试字符串命中的方法。
  3. 编写 `rule_evaluator.rs`：集成文件的 `mtime/atime/ctime`、文件大小区间以及隐藏文件排除，综合判断单文件是否满足规则集（支持 AND/OR 逻辑与取反）。
- **验证指令**:
  ```bash
  cargo test matcher
  ```
- **验收标准**: 测试用例包含复杂通配符（`*.{dmg,zip}`）与正则（`^log_\d{8}\.txt$`）以及时间天数判定，全部测试通过。

---

## 阶段三：混合调度器与多样化动作执行器 (Phase 3: Schedulers & Action Handlers)

### 任务 3.1：实现四大处置动作处理器 (`core/actions/`)
- **目标**: 实现移入回收站、动态归档移动、流式 Zip 压缩与安全物理粉碎。
- **操作步骤**:
  1. `trash.rs`: 调用 `trash::delete(path)` 将文件移入操作系统原生废纸篓。
  2. `archive.rs`: 解析动态路径模板（`{YYYY}/{MM}/{EXT}`），递归创建父级目录，按配置执行 `fs::rename`（支持重名自动序号）。
  3. `zip.rs`: 流式读取命中文件并打包为单个 `.zip` 归档包，完成校验后安全清除源文件。
  4. `hard_delete.rs`: 校验安全确认标记，调用 `fs::remove_file` 彻底擦除。
- **验证指令**:
  ```bash
  cargo test actions
  ```
- **验收标准**: 在临时目录下分别测试四种动作，验证文件去向、路径模板正确性与 Zip 完整性。

---

### 任务 3.2：实现全能混合调度器 (`core/scheduler/`)
- **目标**: 实现基于 `notify` 的实时监听、智能防抖聚合队列与 Cron 定时调度。
- **操作步骤**:
  1. 编写 `debounce_queue.rs`: 基于 `tokio::time::sleep` 实现事件防抖，相同目录快速产生的连续文件变动在防抖窗口（默认 5s）结束后统一分发。
  2. 编写 `watcher_manager.rs`: 为每个启用了 Watcher 的策略挂载独立文件监听，捕获事件传入防抖队列。
  3. 编写 `cron_manager.rs`: 使用 `tokio-cron-scheduler` 注册策略的 Cron 表达式定时任务。
- **验证指令**:
  ```bash
  cargo test scheduler
  ```
- **验收标准**: 在测试目录快速连续写入 10 个测试文件，防抖聚合器在设定的防抖延迟后仅触发一次处理回调。

---

## 阶段四：Tauri IPC 接口与托盘联动 (Phase 4: IPC & Tray Integration)

### 任务 4.1：暴露核心 Tauri Commands 与事件通道
- **目标**: 将策略管理、Dry-Run 真实预演、正则沙盒与审计日志接口暴露给前端。
- **操作步骤**:
  1. 实现 `list_policies`, `save_policy`, `delete_policy`, `toggle_policy`。
  2. 实现 `dry_run_policy`: 遍历目标目录，调用 `Matcher` 比对但不执行动作，返回命中文件列表、大小与距今年代。
  3. 实现 `test_regex_sandbox`: 接收正则与测试文本，即时返回命中状态与捕获组。
  4. 实现 `pause_all_schedulers` 与 `resume_all_schedulers`。
  5. 注册上述指令并在应用启动时自恢复所有启用的策略。
- **验证指令**:
  ```bash
  cargo build
  ```
- **验收标准**: 后端工程编译通过无警告，指令签名与前端类型完全契合。

---

## 阶段五：前端控制台界面与交互系统 (Phase 5: Frontend Dashboard)

### 任务 5.1：搭建策略卡片式管理中心 (`components/PolicyCard.tsx`)
- **目标**: 直观呈现所有已配置策略的状态、目标目录、动作类型与调度信息。
- **操作步骤**:
  1. 构建策略卡片：展示策略名称、开关 Toggle、目标路径标签、动作徽章（回收站/归档/Zip/粉碎）、调度类型标识（Cron/Watcher）。
  2. 提供卡片操作：立即执行 (Run Now)、Dry-Run 预演、编辑策略、删除策略。
  3. 顶栏添加全局操作：添加新策略、全局暂停/恢复开关、配置导入导出。
- **验收标准**: 页面卡片展示整洁，开关切换即刻调用 IPC 更新。

---

### 5.2：可视化规则构建器与正则测试沙盒 (`RuleBuilder.tsx` & `RegexSandboxModal.tsx`)
- **目标**: 用户无门槛组合高级规则，并在内嵌沙盒中测试正则。
- **操作步骤**:
  1. `RuleBuilder.tsx`:
     - 逻辑切换器（AND / OR）。
     - 支持动态增删条件行：类型下拉（Glob、Regex、修改时间、文件大小、排除隐藏文件）。
     - 时间选择器（包含运算符 `older_than`、数值与单位）。
  2. `RegexSandboxModal.tsx`:
     - 左侧输入正则表达式，右侧多行文本域输入示例文件名，实时高亮命中行与语法报错。
- **验收标准**: 规则构建器能灵活保存为复杂的 JSON 数据结构，沙盒测试实时反馈无明显延迟。

---

### 5.3：真实目录 Dry-Run 预演与审计流水面板 (`DryRunModal.tsx` & `AuditTable.tsx`)
- **目标**: 执行前清晰预览预处理清单，执行后完整审计追溯。
- **操作步骤**:
  1. `DryRunModal.tsx`:
     - 弹窗展示当前策略在真实目录中扫描出的命中项。
     - 使用 `@tanstack/react-virtual` 渲染虚拟滚动列表，展示文件名、路径、大小、修改时间，支持表头排序与多项勾选。
     - 底部显示“预计释放空间”，支持点击“确认执行已选项目”。
  2. `AuditTable.tsx`:
     - 分页展示审计流水记录（触发时间、策略名、触发类型、处理文件数、释放容量、状态）。
- **验收标准**: 万级文件列表在预演弹窗中秒级渲染并支持平滑滚动，无卡顿丢帧。

---

## 阶段六：端到端闭环验证与验收 (Phase 6: E2E Validation)

### 任务 6.1：端到端全链路操作闭环验证
- **测试流程**:
  1. **新建策略 A (回收站 + Watcher)**:
     - 目标目录为新建临时目录 `test_downloads/`。
     - 规则：Glob `*.dmg`，修改时间 > 0 天。
     - 动作：移入系统废纸篓。
     - 调度：开启 Watcher 变动监听（防抖 3 秒）。
     - 验证：在该目录下复制一个 `.dmg` 文件，等待 3 秒，文件自动消失并在系统废纸篓中出现。
  2. **新建策略 B (Zip 归档 + 定时/手动)**:
     - 目标目录为 `test_assets/`，内含多个 `.log` 文件。
     - 动作：Zip 压缩归档至 `test_archives/{YYYY}/{MM}/`。
     - 点击「Dry-Run 预演」，核对弹窗中列出的 `.log` 清单与体积。
     - 点击「立即执行」，验证生成了对应的 `.zip` 压缩包，源文件被清除，且 Zip 解压正常。
  3. **托盘与后台常驻验证**:
     - 关闭控制台主窗口，观察应用常驻系统托盘，内存保持在 40MB 以内。
     - 点击托盘菜单项“全局暂停”，再向 `test_downloads/` 放入 `.dmg`，验证后台已暂停不触发。
     - 点击托盘菜单“打开控制台”，窗口秒级重新唤出，审计流水准确记录历史记录。
- **验收标准**: 以上三套全链路操作均符合预期，无崩溃、无内存泄漏。
