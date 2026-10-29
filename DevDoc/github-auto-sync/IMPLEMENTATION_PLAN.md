# Vibe Coding 实施路线图 (IMPLEMENTATION_PLAN) - GitHub 自动同步

本文档专为 AI 编程助手（如 Antigravity / Claude Code / Cursor）执行 Vibe Coding 设计。整个开发流程被拆解为 **6 个阶段 (Phases)** 与 **16 个原子化任务 (Atomic Tasks)**。每个任务颗粒度独立，附带明确的修改范围与验证标准。

---

## 阶段概览 (Milestones)

* **Phase 1: 脚手架搭建与系统托盘骨架** (Tauri 2.0 + React + Tailwind + 系统托盘基础)
* **Phase 2: 左栏文件夹管理与递归扫描引擎** (文件夹添加、持久化、递归发现 `.git` 仓库)
* **Phase 3: 右栏仓库视图与右键策略菜单** (双栏联动、4色状态徽标、自定义右键菜单)
* **Phase 4: Git 执行管道与三大同步策略** (调用本机 git、Commit Message 摘要生成器、新旧覆盖与强推)
* **Phase 5: 防抖调度器与定时轮询闭环** (5秒本地变动防抖、5分钟远端拉取、后台无感运行)
* **Phase 6: 边界防护、体验优化与跨平台打包** (防抖防环路、网络异常重试、DMG/EXE 打包构建)

---

## Phase 1: 脚手架搭建与系统托盘骨架

### 任务 1.1: 初始化 Tauri 2.0 + React + Tailwind 基础工程
* **目标**：搭建前后端骨架，确保开发环境能够成功拉起窗口。
* **文件范围**：
  * 创建 `package.json`, `tsconfig.json`, `tailwind.config.js`, `src/App.tsx`
  * 创建 `src-tauri/Cargo.toml`, `src-tauri/tauri.conf.json`, `src-tauri/src/main.rs`
* **Vibe Prompt 示范**：
  > “请根据 `ARCHITECTURE.md` 初始化一个 Tauri 2.0 + React 18 + TypeScript + Tailwind CSS 的轻量级桌面项目。窗口标题为 'GitHub Auto Sync'，尺寸 900x600，居中显示。”
* **验收验证**：
  * 运行 `npm run tauri dev`，能正常启动并渲染带有 Tailwind 样式的测试界面。

### 1.2: 系统托盘与优雅关停所有服务集成 (Graceful Shutdown)
* **目标**：实现启动自动最小化至托盘，托盘右键仅包含“打开主界面”与“退出应用”。点击“退出”时优雅关停全部监听与后台服务。
* **文件范围**：
  * `src-tauri/src/tray.rs`
  * `src-tauri/src/main.rs`
* **Vibe Prompt 示范**：
  > “在 `src-tauri/src/tray.rs` 中使用 Tauri 2.0 的 TrayIconBuilder 创建系统托盘。右键菜单仅添加两个项：'打开主界面' (点击显示并激活窗口) 和 '退出'。点击主窗口的关闭按钮时，拦截为隐藏窗口 `window.hide()` 而不是退出。点击‘退出’时，触发全局 `CancellationToken` 取消，卸载所有 `notify` 监听器，等待在途 Git 命令返回并保存状态，最后 `app.exit(0)` 退出，避免任何僵尸进程或锁文件残留。”
* **验收验证**：
  * 启动后托盘出现小图标，点击关闭窗口后程序不退出；右键托盘点击“打开主界面”重新呼出窗口；点击“退出”时，终端/控制台输出优雅停机日志，后台进程彻底干净退出。

### 任务 1.3: 初次使用磁盘权限检测与引导 (Permission Onboarding)
* **目标**：初次启动时检查磁盘读写权限，向用户弹出友好引导弹窗，支持一键打开系统设置。
* **文件范围**：
  * `src/components/common/PermissionModal.tsx`
  * `src-tauri/src/commands.rs` (实现 `check_disk_permission`, `open_privacy_settings`, `complete_first_run`)
* **Vibe Prompt 示范**：
  > “编写权限引导组件 PermissionModal。在首次运行 (`firstRunCompleted === false`) 时居中弹窗，提示用户‘应用需要读取与写入代码目录权限以执行自动同步’。提供按钮【前往系统设置授权】，调用 Tauri Command 打开 macOS 隐私与安全性面板。用户点击【我已完成授权】后，调用 `complete_first_run` 写入配置，关闭弹窗且后续不再弹出。”
* **验收验证**：
  * 清除配置文件后全新启动，主界面弹出授权引导弹窗；点击跳转按钮可唤起 macOS 系统设置面板；完成授权后关闭弹窗，重启不再弹出。

---

## Phase 2: 左栏文件夹管理与递归扫描引擎

### 任务 2.1: 左栏 UI 与文件夹防嵌套及彻底注销
* **目标**：实现左栏文件夹列表，顶部固定【全部文件夹】，提供添加与删除功能。添加时校验子目录/父目录冲突并拦截提示；删除时彻底停用对应监听服务并抹除缓存。
* **文件范围**：
  * `src/components/sidebar/FolderList.tsx`
  * `src/components/sidebar/AddFolderBtn.tsx`
  * `src/store/useAppStore.ts`
  * `src-tauri/src/commands.rs` (实现 `add_watched_folder` 与 `remove_watched_folder`)
  * `src-tauri/src/storage/store.rs`
* **Vibe Prompt 示范**：
  > “实现左栏 FolderList 组件：顶部固定【全部文件夹】，高亮当前选中项；点击 `+ 添加文件夹` 调用 Tauri `dialog.open({ directory: true })` 原生选择目录。在后端的 `add_watched_folder` 中，使用 `canonicalize` 标准化路径，若新选路径是已有监控目录的子目录、父目录或同级重复，返回友好的中文错误拦截提示并在前端展示 Toast。成功则持久化到本地 JSON，并更新 Zustand 状态。实现 `remove_watched_folder`：删除指定文件夹时，即刻关闭对应 notify 监听服务、取消下属所有 repo 的后台防抖与定时任务，并在配置文件中彻底抹除记录（不删除物理磁盘文件）。”
* **验收验证**：
  * 先添加 `~/Projects`，再尝试添加 `~/Projects/app1`，界面弹出提示“无法添加：该目录属于已添加文件夹【Projects】的子目录”；反向添加父目录亦同理被拦截。删除文件夹后，后台相关监听线程全部销毁，无残留。

### 任务 2.2: 异步递归扫描 Git 仓库 (Discovery Engine)
* **目标**：扫描添加的根目录，递归发现所有包含 `.git` 的子目录。
* **文件范围**：
  * `src-tauri/src/scanner/discovery.rs`
  * `src-tauri/src/commands.rs`
* **Vibe Prompt 示范**：
  > “编写 `discovery.rs`：使用 `walkdir` 异步遍历用户添加的目录。排除 `node_modules`、`target`、`.cache` 等常见无用目录；一旦检测到子目录存在 `.git/`，停止向下遍历该分支并提取当前分支名，封装为 `GitRepo` 实体。所有新发现的仓库默认 `sync_mode` 为 `stopped`。”
* **验收验证**：
  * 选择一个包含 3 个 Git 项目的根目录，后台能在 500ms 内扫描出 3 个仓库并由前端显示。

### 任务 2.3: 文件监听动态感知增删仓库 (File Watcher)
* **目标**：监听根文件夹，新增或删除仓库时无感刷新。
* **文件范围**：
  * `src-tauri/src/scanner/watcher.rs`
* **Vibe Prompt 示范**：
  > “在 `src-tauri/src/scanner/watcher.rs` 中集成 `notify` crate 监听监视目录。当检测到目录下有新增 `.git` 文件夹或删除 `.git` 文件夹时，向前端发射 `repo:discovered` 或 `repo:removed` 事件。”
* **验收验证**：
  * 在受监控目录下执行 `mkdir demo && cd demo && git init`，前端列表在 1 秒内自动浮现该 demo 仓库。

---

## Phase 3: 右栏仓库视图与右键策略菜单

### 任务 3.1: 右栏仓库卡片列表与过滤
* **目标**：右侧展示 Git Repo 卡片列表，响应左栏选中过滤。
* **文件范围**：
  * `src/components/repolist/RepoList.tsx`
  * `src/components/repolist/RepoCard.tsx`
  * `src/components/repolist/StatusBadge.tsx`
* **Vibe Prompt 示范**：
  > “实现右栏 RepoList。若左栏选中【全部文件夹】，展示所有仓库；若选中某个特定文件夹，仅展示该文件夹下的仓库。每个 RepoCard 显示仓库名、路径、当前分支名、状态图标与同步徽标。”
* **验收验证**：
  * 切换左栏的不同文件夹，右侧精准展示对应的子仓库；点击【全部文件夹】展示全局合并列表。

### 3.2: 4 色同步状态徽标 (Badges)
* **目标**：直观展示当前仓库的同步策略与运行状态。
* **文件范围**：
  * `src/components/repolist/StatusBadge.tsx`
* **Vibe Prompt 示范**：
  > “编写 StatusBadge 组件：绿色为 `[双向同步]`、蓝色为 `[本地优先]`、紫色为 `[远端优先]`、灰色为 `[已暂停]`。若状态为 `syncing` 显示微型旋转 Spinner；若为 `error` 显示红色感叹号。”
* **验收验证**：
  * 状态徽标颜色分明，视觉清晰，支持悬浮展示上次同步时间与错误原因 Tooltip。

### 任务 3.3: 自定义右键策略切换与首次开启前置预检 (Pre-flight Check)
* **目标**：在 RepoCard 上右键弹出 4 种同步策略选项。首次从【停止同步】激活时，后端自动执行健康与远程连通性预检，确认状态正确后再放行。
* **文件范围**：
  * `src/components/common/ContextMenu.tsx`
  * `src-tauri/src/git/preflight.rs`
  * `src-tauri/src/commands.rs` (实现 `set_repo_sync_mode`)
* **Vibe Prompt 示范**：
  > “在 RepoCard 上挂载右键上下文菜单，选项包括：1. 双向同步（如有冲突，新覆盖旧）；2. 本地优先（本地覆盖远端）；3. 远端优先（远端覆盖本地）；4. 停止同步。调用 `set_repo_sync_mode` 时，若该 Repo 首次开启同步，后台调用 `run_preflight_check`：校验是否存在 remote origin、是否处于未完成的 merge/rebase 冲突状态、远端是否可通。通过后切换徽标并注册到防抖调度；未通过则弹窗拦截错误并标红显示。”
* **验收验证**：
  * 在一个纯本地没有 remote 的 git 仓库上右键开启【双向同步】，界面精准拦截并提示“该仓库未配置远程源”；在一个已正常关联 GitHub 的仓库上开启，预检瞬间通过并显示就绪徽标。

---

## Phase 4: Git 执行管道与三大同步策略

### 任务 4.1: 宿主系统 Git 命令调度器
* **目标**：通过 Rust 异步执行本机 `git` 命令，复用系统 SSH Key。
* **文件范围**：
  * `src-tauri/src/git/runner.rs`
* **Vibe Prompt 示范**：
  > “编写 `runner.rs`：使用 `tokio::process::Command` 异步调度系统的 `git` 命令，设置 `current_dir` 为目标仓库目录。提供异步包装函数：`git_status()`, `git_fetch()`, `git_add()`, `git_commit()`, `git_push()` 等，并捕获 stderr/stdout。”
* **验收验证**：
  * 在测试 repo 上调用 `git_status`，能正确拿到修改文件列表与当前分支。

### 任务 4.2: Commit Message 摘要生成器与大文件 (>100MB) 自动忽略
* **目标**：自动提取变动文件生成格式化提交信息；对 $\ge 100\text{MB}$ 的大文件自动追加至 `.gitignore` 的 `# large files` 标签下，防止 GitHub 拒推。
* **文件范围**：
  * `src-tauri/src/git/commit_formatter.rs`
  * `src-tauri/src/git/large_files.rs`
* **Vibe Prompt 示范**：
  > “编写 `large_files.rs`：根据 `SPEC.md` 3.0.1 节，扫描待提交改动中体积 $\ge 100\text{MB}$ 的文件，若存在，自动在仓库根目录 `.gitignore` 下创建/追加 `# large files` 标签段落及对应相对路径，并执行 `git rm --cached` 移出暂存区。编写 `commit_formatter.rs` 解析 `git status --porcelain` 剩余小文件生成格式化信息：`auto-sync: YYYY-MM-DD HH:mm:ss - {N} files ({details})`。”
* **验收验证**：
  * 放入一个 105MB 文件，执行后 `.gitignore` 自动出现 `# large files` 段落与该文件名，`git status` 显示该文件已被忽略未被 commit。

### 任务 4.3: 三大核心同步策略实现 (Strategies)
* **目标**：实现双向（新覆盖旧）、本地优先（强推覆盖）、远端优先（强制拉齐）。
* **文件范围**：
  * `src-tauri/src/git/strategies.rs`
* **Vibe Prompt 示范**：
  > “根据 `SPEC.md` 第 3 节实现三种同步策略的执行流程：
  > 1. `local_first`: 提交后执行 `git push --force-with-lease`；
  > 2. `remote_first`: 强制 `git reset --hard origin/<branch>` 清理本地与远端对齐；
  > 3. `bidirectional_newest`: 智能对比本地修改时间与远端提交时间，新覆盖旧。”
* **验收验证**：
  * 在测试仓库中分别触发三种模式，验证远端与本地的覆盖行为符合预期。

---

## Phase 5: 防抖调度器与定时轮询闭环

### 任务 5.1: 5 秒本地变动防抖调度器 (Debouncer)
* **目标**：代码保存后延迟 5 秒触发，避免保存过程中的连续无意义提交。
* **文件范围**：
  * `src-tauri/src/scheduler/debouncer.rs`
* **Vibe Prompt 示范**：
  > “使用 `tokio::sync::mpsc` 与 `tokio::time::sleep` 编写防抖调度器。接收到某个 repo 的文件写入事件后，重置 5 秒计时器。5 秒内无新事件时，将该 repo 移入同步队列并发起执行。”
* **验收验证**：
  * 连续快速修改保存一个文件 5 次，系统仅在最后一次保存停止 5 秒后触发一次同步。

### 任务 5.2: 5 分钟远端定时拉取轮询
* **目标**：定期与 GitHub 远端对齐，拉取他人或另外一台电脑推送的更新。
* **文件范围**：
  * `src-tauri/src/scheduler/mod.rs`
* **Vibe Prompt 示范**：
  > “启动后台独立 Tokio 任务，每隔 5 分钟遍历所有处于非 `stopped` 模式的仓库，向远端发起轻量级 `git fetch` 检查，若有更新则依据模式触发对齐。”
* **验收验证**：
  * 观察后台日志，每隔设定间隔自动检查一次远程分支。

---

## Phase 6: 边界防护、体验优化与跨平台打包

### 任务 6.1: 忽略规则与防死循环保护
* **目标**：防止 `.git` 自身的写入和 `node_modules` 变动导致无限触发同步死循环。
* **文件范围**：
  * `src-tauri/src/scanner/watcher.rs`
* **Vibe Prompt 示范**：
  > “在 Watcher 事件过滤器中，强行过滤掉任何包含 `.git/`、`node_modules/`、`target/`、`.DS_Store` 的文件事件，确保 Git 自身的提交动作不会反向触发 Watcher。”
* **验收验证**：
  * 触发自动提交后，系统日志不会再次捕获到 `.git/COMMIT_EDITMSG` 等文件变动。

### 任务 6.2: 开机自启动与设置中心（含彻底注销与数据清理）
* **目标**：集成 `tauri-plugin-autostart`，在前端左栏底部设置项中支持开启/关闭开机自启，并提供【彻底清理与注销退出】功能。
* **文件范围**：
  * `src-tauri/Cargo.toml` (添加 `tauri-plugin-autostart`)
  * `src-tauri/src/main.rs` (注册插件)
  * `src-tauri/src/commands.rs` (实现 `factory_reset_and_exit`)
  * `src/components/sidebar/SettingsModal.tsx`
* **Vibe Prompt 示范**：
  > “引入官方 `tauri-plugin-autostart` 插件。在前端左栏底部增加‘⚙️ 设置’图标与弹窗，提供‘开机自动启动’开关切换。实现 `factory_reset_and_exit` Command：调用 autostart disable 注销系统开机项、停止所有后台服务与监听器、删除系统 application support 配置目录，最后 `app.exit(0)` 退出，实现零残留。”
* **验收验证**：
  * 开启自启动开关后，macOS 登录项或 LaunchAgents 中成功注册该应用；关闭后自动注销。点击【彻底注销与清理】后，所有配置目录与自启注册彻底清除，应用安全退出。

### 任务 6.3: 生产构建与打包测试
* **目标**：打包为 macOS `.dmg` / `.app` 以及 Windows `.exe` 安装包。
* **验证命令**：
  ```bash
  npm run tauri build
  ```
* **验收验证**：
  * 在 macOS 上成功生成目标安装包，双击打开常驻菜单栏，内存占用稳定在 35MB 以内。

### 任务 6.4: 运行日志控制台与 FIFO 1MB 自动淘汰
* **目标**：日志保存在 App 包体目录下（随 App 删除时一同清理），并按 FIFO 队列模式严格限制在 1MB 容量以下，提供原生可视化日志控制台。
* **实现规范**：
  * **存储路径**：`GitHubSync.app/Contents/Logs/app.log`；
  * **FIFO 限制**：写入超过 1.0MB 时自动修剪头部旧日志，保留尾部约 700KB 完整记录；
  * **查看器界面**：展示当前容量徽标、按等级过滤（ALL/SYNC/INFO/WARN/ERROR）、关键字实时检索、一键复制、访达定位与手动清空。
* **验收验证**：
  * 连续写入超过 1MB 数据，日志大小稳定截留在 1MB 以下，头部历史日志平滑淘汰，最新日志完整保留；在访达中将应用删除，日志一并被彻底移除。
