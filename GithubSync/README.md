# GitHub Auto Sync (GitHub 自动同步应用)

> 针对 macOS 深度定制的原生轻量级系统托盘应用（基于 **Swift 6.4 + SwiftUI + AppKit** 构建），零外部环境依赖，运行时内存仅占用约 **15MB**。实现本地工作区多 Git 仓库的自动发现、变动监听与全自动按策略同步。

---

## 🌟 核心特性与架构实现

### 1. 极简系统托盘与常驻生命周期 (Menu Bar Tray & Lifecycle)
* **系统托盘极简常驻**：常驻 macOS 菜单栏状态栏（Menu Bar Status Item），右键菜单仅保留**【打开主界面】**与**【退出应用】**，无冗余业务按钮。
* **关闭窗口拦截**：主窗口点击右上角关闭按钮（`X`）时自动拦截为隐藏窗口（`orderOut`），后台服务无缝保持常驻。
* **优雅停机 (Graceful Shutdown)**：在托盘点击【退出应用】时，自动注销 `FSEvents` 监听流、停止所有防抖与定时轮询计时器、确保在途 Git 操作安全落盘并干净退出进程。

### 2. 双栏式工作区管理 (Split Layout & Folder Management)
* **左栏（文件夹管理）**：
  * **全部文件夹**：顶部固定汇总视图，点击展示所有添加根目录下的全部 Git 仓库。
  * **根监视目录列表**：展示已添加的监控路径与发现的 Repo 数量。
  * **防嵌套防冲突校验**：添加新文件夹时自动解析规范化绝对路径（`resolvingSymlinksInPath`），若选取的路径已存在、或者是已有文件夹的子目录、或者是已有文件夹的父目录，系统自动拦截并弹出友好中文提示。
  * **彻底注销与解绑**：右键删除已监控文件夹时，立即终止对应文件系统的监听句柄，抹除所有下属仓库的调度配置，不留后台残留。
* **右栏（Git Repo 列表）**：
  * 根据左栏选中的文件夹或全局汇总进行仓库过滤。
  * 顶部支持按仓库名、分支名、文件路径即时搜索。
  * 展示仓库名、物理路径、当前分支（`arrow.triangle.branch`）、未提交变动文件数量与上次同步成功时间。

### 3. 4 色同步状态徽标与右键策略切换 (4-Mode Status Badges)
每个仓库卡片直观展示状态徽标：
* 🟢 **`[双向同步]`（新覆盖旧）**：拉取与推送双向同步，发生分叉冲突时自动对比本地最后修改时间戳与远端提交时间戳，新覆盖旧。
* 🔵 **`[本地优先]`（本地覆盖远端）**：本地提交作为不可动摇的基准，推送遇阻时使用 `--force-with-lease` 强制拉齐远端。
* 🟣 **`[远端优先]`（远端覆盖本地）**：以 GitHub 远端代码为基准，清理本地工作区草稿（`git reset --hard` & `git clean -fd`）并强制拉齐到 `origin/<branch>`。
* ⚪ **`[已暂停]`（停止同步）**：新发现仓库的**默认初始策略**，不执行任何后台同步。
* **动态运行态**：同步中显示微型转圈动画（Spinner）；同步出错标红显示感叹号并可悬浮/查看错误原因。

### 4. 首次激活健康预检机制 (Pre-flight Check)
当仓库首次从【停止同步】切换至任意开启同步状态时，系统自动执行 4 项健康检查：
1. **远程源存在性**：校验 `git remote get-url origin` 是否配置；
2. **冲突中断检测**：校验是否存在 `.git/MERGE_HEAD` 或 `.git/rebase-apply`；
3. **远端连通性与权限**：执行 `git ls-remote --exit-code -h origin` 测试网络与 SSH/Token 凭据；
4. **上游分支关联**：校验当前分支是否已建立远程追踪分支 (`@{u}`)。
若未通过，弹窗提示确切拦截原因并保持暂停状态，防止误推送或冲突蔓延。

### 5. 大文件自动检测与保护 (Large Files Protection >= 100MB)
* 自动扫描工作区变动文件中体积 $\ge 100\text{MB}$ 的大文件。
* 检测到大文件时，自动在仓库根目录 `.gitignore` 下创建/追加 `# large files` 标签段落及对应相对路径。
* 自动执行 `git rm --cached` 将大文件从暂存区剔除，避免被 GitHub 100MB 物理限制拒推。

### 6. 结构化 Commit Message 自动生成
每次自动提交均自动汇总变动文件列表与时间戳：
* 变动文件 $\le 3$ 项：`auto-sync: 2026-10-27 15:00:00 - 2 files (modified: App.tsx; added: README.md)`
* 变动文件 $> 3$ 项：`auto-sync: 2026-10-27 15:00:00 - 8 files (modified: index.ts, style.css, +6 more)`

### 7. 纯自动化调度引擎 (Mode B & Debounce)
应用坚守纯粹的自动化哲学，界面不设手动同步干扰按钮：
* **本地代码变动即时监听**：`FSEvents` 内核级监听，代码保存后重置 **5 秒防抖** 倒计时，5 秒静默后自动提交并同步。
* **远端定时轮询**：默认每隔 **5 分钟** 自动发起后台轻量检查，保证多设备代码实时拉齐。
* **防循环保护**：主动忽略 `.git/` 内部目录的所有写入事件与大型构建产物目录。

### 8. 系统集成、日志管理与彻底注销 (Logging & Factory Reset)
* **独立日志查看器 (LogViewer)**：
  * 应用内置原生日志控制台，支持实时滚动、按日志级别（`ALL`, `SYNC`, `INFO`, `WARN`, `ERROR`）筛选及全文关键词即时检索；
  * 支持一键拷贝日志、清空日志以及在访达（Finder）中高亮定位日志文件；
  * **FIFO 环形缓冲容量控制**：日志文件大小严格限制在 **1.0 MB** 以内。当累积写入逼近 1MB 时，触发 FIFO 队列机制，自动淘汰头部最早的历史日志，保留最新约 700KB 记录，永不膨胀爆盘；
  * **随 App 一并删除**：日志文件存放在 `GitHubSync.app/Contents/Logs/app.log`，用户将 App 移入废纸篓时，所有日志一同彻底删除，无任何外置残留。
* **磁盘权限引导**：首次启动弹出权限引导弹窗，支持一键唤起 macOS“隐私与安全性 -> 完全磁盘访问权限”设置面板。
* **开机自启动**：基于 macOS `ServiceManagement` (`SMAppService`) 原生开机自启集成。
* **彻底注销与数据清理**：设置中心提供【彻底注销并退出应用】，自动注销开机项、停止所有监听服务、抹除本地 Application Support 配置缓存，干净退出无残留。

---

## 📂 项目结构

```text
GithubSync/
├── Package.swift                  # Swift Package Manager 配置文件
├── build.sh                       # Release 一键编译、打包与签名脚本
├── Resources/
│   ├── Info.plist                 # macOS App Bundle 元信息与权限声明
│   └── AppIcon.icns               # 高清 macOS 原生应用图标
├── Sources/
│   └── GitHubSync/
│       ├── Main.swift             # App 主入口、系统托盘 (NSStatusItem) 与窗口生命周期拦截
│       ├── Models/
│       │   ├── Models.swift       # 核心模型 (WatchedFolder, GitRepo, SyncMode, AppSettings)
│       │   ├── Storage.swift      # 本地持久化管理器 (config.json)
│       │   └── AppState.swift     # 响应式全局状态树 (MainActor ObservableObject)
│       ├── Services/
│       │   ├── GitRunner.swift    # 异步调度系统 Git CLI 管道
│       │   ├── DiscoveryEngine.swift # 异步递归发现 .git 仓库与防嵌套路径校验
│       │   ├── FSEventsWatcher.swift # macOS 内核级 FSEventStream 实时文件监听
│       │   ├── SyncEngine.swift   # 3大同步策略算法与冲突新旧覆盖逻辑
│       │   ├── LargeFileHandler.swift# 100MB 大文件扫描与 .gitignore 自动打标
│       │   ├── CommitFormatter.swift # 结构化 Commit 信息生成器
│       │   ├── Scheduler.swift    # 5秒本地变动防抖与 5分钟远端轮询闭环
│       │   ├── AutoStartManager.swift# SMAppService / LaunchAgent 开机自启
│       │   └── PermissionService.swift # 磁盘权限检测与系统偏好跳转
│       └── Views/
│           ├── MainWindowView.swift # 左右双栏分栏主窗口容器
│           ├── SidebarView.swift    # 左栏文件夹列表与全部文件夹
│           ├── RepoListView.swift   # 右栏仓库网格列表与即时搜索
│           ├── RepoCardView.swift   # 仓库卡片与右键策略上下文菜单
│           ├── StatusBadgeView.swift# 4色状态徽标与动画指示
│           ├── SettingsModalView.swift # 偏好设置面板与彻底注销清理
│           └── PermissionModalView.swift# 首次启动权限引导弹窗
└── dist/
    └── GitHubSync.app             # 构建生成的独立可运行 macOS 应用程序
```

---

## 🚀 构建与运行

### 环境要求
* 操作系统：macOS 13.0 (Ventura) 及以上
* 开发环境：已安装 Xcode Command Line Tools 或 Xcode (`swift --version` >= 5.9)
* 系统工具：已安装 Git（直接复用用户本机的 SSH 密钥与 GitHub 鉴权）

### 1. 一键构建 Release 发布包与 DMG 安装包
在当前项目根目录下运行：
```bash
# 1. 编译并签名生成 .app 应用程序
./build.sh

# 2. 一键打包生成与 App 图标一致的 DMG 安装包
./package_dmg.sh
```
生成的产物位于 `dist/` 目录：
* 独立应用程序：`./dist/GitHubSync.app`
* 独立安装包：`./dist/GitHubSync-1.0.0.dmg`（DMG 卷宗与文件自身均已注入相同高清浅色 AppIcon）

### 2. 启动与安装
* **方式 A（DMG 拖拽安装）**：
  双击打开 `./dist/GitHubSync-1.0.0.dmg`，将 `GitHubSync.app` 拖入 `Applications` 即可完成安装。
* **方式 B（直接启动测试）**：
  ```bash
  open ./dist/GitHubSync.app
  ```

### 3. 开发模式调试
在开发过程中若需即时运行调试：
```bash
swift run
```
