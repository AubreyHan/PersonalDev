# FilePurger Vibe Coding 实施路线图 (IMPLEMENTATION PLAN)

> **使用指南**:
> 本文档面向在独立工程仓库中使用 Antigravity / AI 编程助手进行 Vibe Coding 时使用。
> 整个开发流程拆解为 6 个有序阶段（Phases）与原子级任务（Atomic Tasks）。
> 开发者在新工程仓库中可逐项下达指令（例如：*“请根据 file-purger/IMPLEMENTATION_PLAN.md 执行 Phase 1.1”*）。

---

## 阶段一：macOS 纯菜单栏工程脚手架与依赖搭建 (Phase 1: macOS Menu Bar & Scaffolding)

### 任务 1.1：初始化 Tauri 2.x + React + Tailwind CSS 脚手架
- **目标**: 初始化 macOS 专属工程，配置双栏现代控制台与组件库。
- **操作步骤**:
  1. 使用 `pnpm create tauri-app@latest file-purger` 初始化工程，选用 `React` + `TypeScript` 模板。
  2. 安装前端核心依赖：
     - UI 图标与工具: `lucide-react`, `clsx`, `tailwind-merge`
     - 拖拽重排库: `@hello-pangea/dnd` (或原生 HTML5 拖拽)
     - 状态管理: `zustand`
     - 提示通知: `sonner`
  3. 配置 `tailwind.config.js` 与 `index.css`，配置 macOS 原生系统风格的浅色/深色主题。
- **验证指令**:
  ```bash
  pnpm install
  pnpm tauri dev
  ```
- **验收标准**: 前端主窗口拉起，Tailwind 样式生效，无控制台警告。

---

### 任务 1.2：配置 macOS 纯菜单栏属性与原生托盘 (`LSUIElement`)
- **目标**: 实现 Dock 栏不显示图标，启动后仅驻留在 macOS 顶部菜单栏，左键点击直接弹出原生菜单。
- **操作步骤**:
  1. 在 `src-tauri/Info.plist` 中添加 `<key>LSUIElement</key><true/>`。
  2. 在 `src-tauri/Cargo.toml` 中声明依赖：
     ```toml
     [dependencies]
     tauri = { version = "2.0", features = ["tray-icon", "notification"] }
     serde = { version = "1.0", features = ["derive"] }
     serde_json = "1.0"
     tokio = { version = "1", features = ["full"] }
     notify = "6.1"                 # macOS FSEvents 变动监听
     globset = "0.4"                # 高性能通配符编译
     trash = "5.2"                  # 原生废纸篓 API
     chrono = { version = "0.4", features = ["serde"] }
     uuid = { version = "1.8", features = ["v4"] }
     walkdir = "2.5"
     objc = "0.2"                   # macOS Objective-C 桥接
     cocoa = "0.25"                 # macOS Cocoa API 绑定
     ```
  3. 在 `src-tauri/src/main.rs` 中初始化 `TrayIconBuilder`，设置左键直接弹出下拉菜单，内含：【打开主界面】、【✓ 开机自启动】、【彻底卸载本应用】、【退出应用】。
- **验证指令**:
  ```bash
  cd src-tauri && cargo check
  ```
- **验收标准**: 启动后 Dock 栏无图标，macOS 顶部状态栏出现图标，点击左键直接呼出菜单。

---

## 阶段二：macOS 原生能力与路径防护引擎 (Phase 2: macOS Native & Guard)

### 任务 2.1：实现目录层级排他防护守卫 (`core/path_hierarchy_guard.rs`)
- **目标**: 严密拦截现有目录的父级、子级以及 macOS 核心系统路径。
- **操作步骤**:
  1. 实现 `PathHierarchyGuard::check_can_add(new_path: &Path, existing_paths: &[PathBuf]) -> Result<(), GuardError>`。
  2. 针对路径调用 `std::fs::canonicalize` 解析物理路径。
  3. 双向比对：检测是否为现有任一路径的前缀（父目录）或被现有路径作为前缀（子目录）。
  4. 检查是否命中硬编码核心黑名单（`/`, `/System`, `/Library`, `/Applications` 等）。
  5. 编写单元测试覆盖各种父子嵌套与非法目录场景。
- **验证指令**:
  ```bash
  cargo test path_hierarchy_guard
  ```
- **验收标准**: 所有重叠路径与系统目录 100% 拦截并返回清晰错误。

---

### 2.2：提取 macOS 原生 DateAdded 与通配符匹配引擎
- **目标**: 读取真实的 `kMDItemDateAdded` 并实现组合通配符匹配。
- **操作步骤**:
  1. 编写 `core/date_added.rs`：使用 `MDItemCreateWithURL` 读取 macOS 原生 `kMDItemDateAdded`；若不可读则平滑降级到 `btime`。
  2. 编写 `core/pattern_matcher.rs`：
     - 支持大小写严格敏感。
     - 支持组合路径语法：`**`（多层子目录）、`*/` 或 `a*/`（单层子目录）、`*a`（不含扩展名主干结尾）、`a*.j*`。
     - 自动兼容中英文逗号隔开多个模式，实现 OR 关系逻辑。
  3. 编写单元测试验证通配符命中准确性。
- **验证指令**:
  ```bash
  cargo test pattern_matcher
  ```
- **验收标准**: 单元测试覆盖各类复杂 Glob 表达式与大小写比对，全部通过。

---

## 阶段三：瀑布式优先匹配流与废纸篓管理器 (Phase 3: Waterfall Engine & Trash)

### 任务 3.1：实现瀑布式优先短路规则引擎 (`core/waterfall_engine.rs`)
- **目标**: 落实“置顶保留绝对免死”与“普通规则从上到下 First-Match 短路”逻辑。
- **操作步骤**:
  1. 实现目录扫描时自动跳过所有以 `.` 开头的隐藏文件与 macOS 系统保留文件；若子文件夹被整体命中，允许连带删除内部隐藏文件。
  2. 优先比对置顶保留规则，一旦命中直接短路退出。
  3. 循环按顺序比对各启用的普通规则，同时满足 `DateAdded` 超限与通配符命中的文件，根据动作分流：
     - `list`: 收集到匹配结果清单；
     - `keep`: 锁定保留，短路终止；
     - `direct_trash`: 调用 `trash::delete` 移入废纸篓；
     - `ask_trash`: 压入待询问队列，派发事件至前端。
- **验证指令**:
  ```bash
  cargo test waterfall_engine
  ```
- **验收标准**: 规则顺序调整、置顶保留与四大动作分流逻辑单元测试完全通过。

---

### 任务 3.2：实现系统废纸篓超龄天数与容量 FIFO 管理器 (`core/trash_controller.rs`)
- **目标**: 对整个系统废纸篓执行天数超龄清理与容量超限 FIFO 物理清除。
- **操作步骤**:
  1. 读取 `~/.Trash` 下条目大小与移入废纸篓时间戳。
  2. 若 `days_threshold > 0`，物理删除超龄文件。
  3. 若 `max_capacity_gb > 0`，当总容量超出时，按移入时间升序排列，逐个调用 `fs::remove_file / remove_dir_all` 物理擦除，直到总容量低于阈值。
- **验证指令**:
  ```bash
  cargo test trash_controller
  ```
- **验收标准**: 模拟废纸篓超量文件，验证超龄清理与 FIFO 清理符合预期。

---

## 阶段四：FSEvents 内核变动监听与防抖调度器 (Phase 4: FSEvents & Scheduler)

### 任务 4.1：构建内核变动监听器与 3 秒防抖队列 (`core/fsevents_watcher.rs`)
- **目标**: 利用 `notify` 监听监控文件夹变动，实现无变动 0% CPU 占用。
- **操作步骤**:
  1. 使用 `notify::RecommendedWatcher`（macOS 原生使用 FSEvents）绑定所有监控文件夹。
  2. 构建 `debounce_queue.rs`：变动事件进入 Tokio 异步防抖队列，设置 3 秒静默倒计时。
  3. 倒计时结束后唤醒瀑布规则评估引擎执行增量扫描。
  4. 启动一个后台异步心跳循环，每 1 小时对全部监控文件夹及废纸篓执行一次巡检（处理自然超龄）。
- **验证指令**:
  ```bash
  cargo test fsevents_watcher
  ```
- **验收标准**: 连续写入文件时仅在静默 3 秒后触发 1 次扫描回调，日常 CPU 为 0.0%。

---

## 阶段五：前端双栏界面与交互系统 (Phase 5: Frontend UI)

### 任务 5.1：实现左侧栏与防重叠添加文件夹对话框
- **目标**: 呈现常驻废纸篓卡片、文件夹列表，并提供严密防重叠拦截。
- **操作步骤**:
  1. 构建左侧栏：顶部常驻【回收站】（不可删除、不可移出）；下方呈现监控文件夹列表（默认预置 `~/Downloads`）。
  2. 每项文件夹悬浮展示“移除”按钮（点击直接从列表移除，磁盘无影响，无弹窗打扰）。
  3. 编写 `AddFolderModal.tsx`：调用 Tauri 目录选择弹窗，添加前调用后端校验，若命中华父子重叠或系统核心目录，弹出醒目警告对话框拒绝添加。
- **验收标准**: 父子目录添加拦截直观明确，常驻废纸篓不可删除。

---

### 任务 5.2：实现右侧规则工作区与创建/编辑弹窗 (严格对齐图片设计)
- **目标**: 构建置顶保留规则栏、瀑布规则卡片流、创建规则弹窗与“列出”结果清单。
- **操作步骤**:
  1. **右侧上部 - 置顶保留规则**: 输入框常驻，输入通配符即生效，无删除或调序按钮。
  2. **右侧中部 - 普通规则瀑布列表**:
     - 展示规则名称、时间条件、通配符标签、Action 徽章与启用/停用 Toggle。
     - 支持上下拖拽（Drag & Drop）调整从上到下的匹配优先级顺序。
     - 支持点击卡片重新编辑修改，或点击删除按钮移除。
  3. **右侧弹窗 - Create/Edit Rule Modal**:
     - 严格对照用户提供的设计图：
       - `Rule Name` 输入框；
       - `Date Added`: exceeds [ 数值输入/Any ] [ 分/小时/天 下拉 ]；
       - `File Name`: matches pattern 输入框；
       - `Execution Action`: 下拉选择（`List Matches`, `Keep`, `Directly Move to Trash`, `Ask before Trashing`）。
  4. **右侧下部 - 匹配结果清单**: 呈现命中了“列出”规则的文件表格，支持查看名称、路径与添加时间。
  5. **回收站视图**: 当左侧选中回收站时，右侧展示两条专属内置规则输入项（天数与容量 GB，填 0 不执行）。
- **验收标准**: 界面操作流畅，弹窗字段与设计图 100% 吻合，拖拽重排即时持久化。

---

### 任务 5.3：实现“询问后删除”逐一确认模态卡片 (`AskConfirmModal.tsx`)
- **目标**: 对命中“询问后删除”的文件严格逐一向用户弹窗确认。
- **操作步骤**:
  1. 监听后端 `app:ask_queue_update` 事件流，将待确认文件压入 Zustand 队列。
  2. 队列不为空时唤起专属全屏或居中卡片，展示当前文件的名称、路径、大小与添加时间。
  3. 仅提供两个明确按钮：【移至废纸篓】与【保留】。
  4. 用户点击后即刻发送决策到后端处理该文件，并自动滑入下一个待确认项，严格逐一手工确认。
- **验收标准**: 多个文件命中时依次逐个询问，绝无批量误删风险。

---

## 阶段六：开机自启、彻底卸载与端到端闭环验证 (Phase 6: E2E Validation)

### 任务 6.1：实现开机自启动集成与彻底卸载 (Clean Uninstall)
- **目标**: 落实菜单栏开机自启切换与 0 残留清理。
- **操作步骤**:
  1. 使用 macOS `SMAppService.mainApp` 实现开机登录项注册与注销，菜单栏下拉菜单实时打勾切换。
  2. 编写 `system::clean_uninstall` 指令：
     - 注销开机登录项；
     - 删除 `~/Library/Application Support/FilePurger` 等所有配置；
     - 将当前执行的 `.app` 自身移入废纸篓并正常调用 `exit(0)` 退出。
- **验收标准**: 在系统设置中可见自启项，点击彻底卸载后配置与应用完全擦除。

---

### 任务 6.2：端到端全链路闭环走查验证
- **测试流程**:
  1. 启动应用，检查 Dock 栏无图标，顶部菜单栏左键点击弹出【打开主界面】等菜单。
  2. 打开主界面，左侧显示常驻回收站与预置的 `~/Downloads`。
  3. 测试父子目录拦截：尝试添加 `~` 或 `~/Downloads/test`，验证警告弹窗正确拦截。
  4. 在 `Downloads` 规则中配置：置顶保留 `keep*`，普通规则 1：`*.dmg` 超过 0 分钟直接删除。
  5. 复制一个 `keep_test.dmg` 和一个 `normal.dmg` 到下载目录，等待 3 秒防抖。
  6. 验证：`keep_test.dmg` 完好保留，`normal.dmg` 移入废纸篓。
  7. 配置规则 2：`*.zip` 超过 0 分钟“询问后删除”。放入一个 `.zip` 文件，验证界面弹出逐一询问卡片，点击移入废纸篓后文件进入回收站。
  8. 测试回收站规则：配置容量上限 1GB，塞入大文件验证 FIFO 物理彻底清空。
  9. 测试点击彻底卸载，验证本地配置与程序本身完全清理。
- **验收标准**: 全流程无缝运转，CPU 占用稳定为 0.0%，内存 < 30MB。
