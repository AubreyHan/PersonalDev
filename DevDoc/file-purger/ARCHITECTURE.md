# FilePurger 技术架构设计 (ARCHITECTURE)

---

## 1. 架构总览 (System Architecture Overview)

FilePurger 采用 **Tauri 2.x** 跨平台体系架构，以 **Rust** 构建常驻后台核心服务，以 **React 18 + Tailwind CSS** 构建现代化的管理控制台与规则沙盒。

### 1.1 分层架构与核心模块拓扑图 (Architecture Diagram)

```mermaid
flowchart TB
    subgraph Client_Layer["交互展现层 (UI & System Tray)"]
        Tray["系统托盘模块 (macOS MenuBar / Win Taskbar)"]
        Dashboard["独立控制台 (React 18 + Tailwind CSS)"]
        subgraph UI_Modules["控制台子模块"]
            PolicyView["多目录策略卡片管理"]
            RuleEditor["可视化规则构建器"]
            RegexSandbox["正则表达式即时测试沙盒"]
            DryRunPreview["Dry-Run 真实目录预演对比"]
            AuditView["审计日志与执行历史"]
        end
        UIStore["全局状态流 (Zustand: Policies / Logs / Config)"]
    end

    subgraph IPC_Layer["Tauri 2.x 桥梁 (IPC Commands & Events)"]
        Cmds["Tauri RPC Commands (策略增删改查, 手动执行, 导出导入, 沙盒预演)"]
        Events["Tauri Event Stream (实时日志流, 调度触发通知, 托盘状态广播)"]
    end

    subgraph Native_Core["Rust 原生常驻后台引擎 (Native Core)"]
        subgraph Scheduler_Engine["混合调度核心 (Hybrid Scheduler)"]
            CronManager["Cron 定时计划调度器 (tokio-cron-scheduler)"]
            WatcherManager["实时变动监听器 (notify: FSEvents / ReadDirectoryChangesW)"]
            DebounceQueue["智能聚合与防抖队列 (Debounce Window: 3~30s)"]
        end

        Guard["系统安全守卫 (PathGuard: 硬编码保护目录绝对阻断)"]

        subgraph Matcher_Engine["高级复合匹配引擎 (Matcher)"]
            GlobMatcher["Glob 通配符预编译匹配 (globset)"]
            RegexMatcher["正则表达式预编译引擎 (regex)"]
            MetaEvaluator["文件元数据条件评估 (mtime / atime / 大小 / 属性)"]
        end

        subgraph Action_Engine["多样化处置动作流 (Action Handlers)"]
            TrashHandler["系统原生废纸篓 (trash API)"]
            ArchiveHandler["动态路径移动归档 (Path Templating)"]
            ZipHandler["流式 Zip 打包归档 (zip-rs)"]
            HardDeleteHandler["永久物理擦除 (fs::remove_file + 安全锁)"]
        end

        StorageEngine["本地持久化存储 (JSON / SQLite: 策略配置 & 审计日志)"]
    end

    subgraph OS_Layer["操作系统底层 (OS Integration)"]
        FS_Events["OS 文件事件通知 (FSEvents / inotify)"]
        File_System["本地/外置文件系统 (APFS / NTFS / ext4)"]
        OS_Trash["系统废纸篓 (macOS Trash / Windows Recycle Bin)"]
        Desktop_Notify["系统原生通知中心 (Tauri Notification)"]
    end

    Dashboard --> UI_Modules
    UI_Modules --> UIStore
    UIStore <--> Cmds
    Events --> UIStore
    Tray <--> Cmds

    Cmds <--> Guard
    Guard --> Matcher_Engine
    
    FS_Events --> WatcherManager
    WatcherManager --> DebounceQueue
    DebounceQueue --> Matcher_Engine
    CronManager --> Matcher_Engine

    Matcher_Engine --> Action_Engine
    Action_Engine --> OS_Trash
    Action_Engine --> File_System
    Action_Engine --> StorageEngine
    Action_Engine -.->|完成广播| Desktop_Notify
    Action_Engine -.->|流式日志| Events
```

---

## 2. 模块职责与工程目录结构 (Module Responsibilities & Layout)

推荐的标准 Tauri 2.x 独立工程目录组织规范如下：

```text
file-purger/
├── src-tauri/                         # Rust 后端原生工程
│   ├── Cargo.toml                     # Rust 依赖清单
│   ├── tauri.conf.json                # Tauri 2.x 窗口、托盘、权限与插件声明
│   ├── src/
│   │   ├── main.rs                    # 托盘创建、后台常驻初始化与生命周期
│   │   ├── commands/                  # 暴露给前端的 Tauri Command 集合
│   │   │   ├── mod.rs
│   │   │   ├── policy.rs              # 策略 CRUD 指令
│   │   │   ├── execution.rs           # 手动触发策略、单策略跑批
│   │   │   ├── preview.rs             # 规则匹配 Dry-Run 预演指令
│   │   │   ├── sandbox.rs             # 正则与 Glob 沙盒测试
│   │   │   └── history.rs             # 审计日志读写与导出
│   │   ├── core/                      # 原生核心引擎层
│   │   │   ├── mod.rs
│   │   │   ├── path_guard.rs          # 系统保护路径绝对阻断器
│   │   │   ├── matcher/               # 高级复合匹配引擎
│   │   │   │   ├── mod.rs
│   │   │   │   ├── glob_engine.rs     # Glob 通配符预编译与匹配
│   │   │   │   ├── regex_engine.rs    # 正则表达式验证与匹配
│   │   │   │   └── rule_evaluator.rs  # 时间/大小/属性综合评估
│   │   │   ├── scheduler/             # 调度与事件监听
│   │   │   │   ├── mod.rs
│   │   │   │   ├── cron_manager.rs    # 定时调度任务注册与管理
│   │   │   │   ├── watcher_manager.rs # notify 文件目录监听器
│   │   │   │   └── debounce_queue.rs  # 变动事件防抖与去重队列
│   │   │   └── actions/               # 处置动作执行器
│   │   │       ├── mod.rs
│   │   │       ├── trash.rs           # 移入原生废纸篓
│   │   │       ├── archive.rs         # 动态模板移动归档
│   │   │       ├── zip.rs             # 打包为 Zip 归档并清理
│   │   │       └── hard_delete.rs     # 物理擦除与安全锁保护
│   │   ├── models/                    # 数据契约模型 (Rust Structs)
│   │   │   ├── policy.rs              # 策略与规则模型
│   │   │   ├── action.rs              # 处置动作枚举与参数
│   │   │   ├── schedule.rs            # 调度模式配置
│   │   │   └── audit.rs               # 审计流水模型
│   │   └── storage/                   # 持久化存储驱动
│   │       └── store.rs               # 策略与日志 JSON 本地存储
├── src/                               # React 18 前端控制台工程
│   ├── index.html                     # 控制台主模板
│   ├── package.json                   # 前端依赖配置
│   ├── vite.config.ts                 # Vite 构建配置
│   ├── tailwind.config.js             # Tailwind CSS 样式配置
│   └── src/
│       ├── main.tsx                   # 前端入口
│       ├── App.tsx                    # 控制台根布局与主路由
│       ├── api/                       # Tauri IPC 强类型封装
│       │   ├── policy.ts              # 策略调用 API
│       │   ├── preview.ts             # 预览与沙盒 API
│       │   └── history.ts             # 审计日志 API
│       ├── store/                     # Zustand 状态切片
│       │   ├── usePolicyStore.ts      # 策略列表状态
│       │   ├── useAuditStore.ts       # 实时审计流与历史记录
│       │   └── useAppStore.ts         # 控制台外观与全局配置
│       ├── components/                # 业务组件
│       │   ├── PolicyCard.tsx         # 策略卡片 (展示状态/路径/动作/调度)
│       │   ├── PolicyEditorModal.tsx  # 策略编辑弹窗 (全配置项)
│       │   ├── RuleBuilder.tsx        # 可视化规则构建器 (条件行增删)
│       │   ├── RegexSandboxModal.tsx  # 正则测试沙盒 (实时命中调试)
│       │   ├── DryRunModal.tsx        # 真实目录 Dry-Run 预演清单表格
│       │   ├── ActionConfigurator.tsx # 处置动作配置组件 (Trash/Archive/Zip/Hard)
│       │   ├── SchedulePicker.tsx     # 调度选择器 (Cron / Watcher / Manual)
│       │   ├── AuditTable.tsx         # 审计历史虚拟滚动表格
│       │   └── HeaderBar.tsx          # 顶栏快捷操作 (全局运行/暂停/添加策略)
│       ├── types/                     # 共享类型定义 (与 Rust 模型对齐)
│       │   └── index.ts
│       └── utils/                     # 辅助工具函数
│           ├── format.ts              # 字节换算与相对时间格式化
│           └── path_template.ts       # 归档路径模板预解析
```

---

## 3. 技术选型与选型理由 (Tech Stack & Rationale)

| 技术模块 | 选型组件 | 核心选型考量与优势 |
| :--- | :--- | :--- |
| **桌面托盘与跨平台底座** | **Tauri 2.x** | 原生托盘能力（`tray-icon` 插件）；相比 Electron，内存常驻仅 ~30MB，启动极速，开机自启无卡顿。 |
| **原生后台语言** | **Rust (1.75+)** | 内存安全、零成本抽象，完美胜任 7x24 小时后台稳定运行，杜绝内存泄漏。 |
| **文件变动监听** | **notify (v6.x)** | Rust 工业级文件监听库，深度集成 macOS FSEvents、Windows ReadDirectoryChangesW 和 Linux inotify，支持异步流式处理。 |
| **正则与通配符** | **regex + globset** | `regex` crate 具备无回溯线性时间复杂度保障，彻底防范正则表达式 ReDoS 拒绝服务攻击；`globset` 支持将多条通配符预编译为 DFA 单次匹配。 |
| **原生废纸篓集成** | **trash (v5.x)** | 跨平台原生 API 支持（macOS `NSFileManager` / Windows `IFileOperation`），且能自动适配外置 USB 驱动器上的回收区。 |
| **流式 Zip 归档** | **zip (v2.x)** | 原生 Rust 编写，支持 Deflate 压缩与流式文件打包写入，无需依赖外部系统命令。 |
| **定时调度引擎** | **tokio-cron-scheduler** | 配合 Tokio 异步事件循环的轻量 Cron 调度器，支持秒/分级精度，资源开销趋近于零。 |
| **前端交互体系** | **React 18 + Vite + Tailwind** | 组件化开发效率高，Tailwind 提供极客感精致暗色/亮色外观；Zustand 极简单向数据流。 |
| **超大列表虚拟滚动** | **TanStack Virtual v3** | 用于 Dry-Run 预演和审计日志，支持万级文件 60fps 流畅滚动渲染。 |

---

## 4. 核心工作流与时序设计 (Core Sequence Diagrams)

### 4.1 实时变动监听与智能防抖处理流 (Watcher & Debounce Flow)

```mermaid
sequenceDiagram
    autonumber
    actor OS as 文件系统 / 用户行为
    participant Watcher as Rust Watcher (notify)
    participant Debounce as 防抖队列 (DebounceQueue)
    participant Matcher as 复合匹配引擎 (Matcher)
    participant Guard as 安全守卫 (PathGuard)
    participant Action as 处置执行器 (ActionEngine)
    participant Audit as 审计与持久化 (Storage)
    participant UI as 托盘与控制台 (IPC Event)

    OS->>Watcher: 产生文件新建/修改事件 (如下载了大文件)
    Watcher->>Debounce: 压入待处理事件 (Path, PolicyId, Timestamp)
    Note over Debounce: 启动/重置 5 秒防抖计时窗口
    Debounce->>Debounce: 等待写入完毕，去重聚合
    Debounce->>Matcher: 触发该策略规则评估 (Evaluate Target File)
    
    Matcher->>Matcher: 正则/Glob 匹配 + 修改时间/大小比对
    alt 未命中规则
        Matcher-->>Debounce: 忽略该文件
    else 命中规则
        Matcher->>Guard: 校验文件路径是否合法 (非系统保护路径)
        Guard-->>Matcher: 验证通过 (Safe)
        Matcher->>Action: 执行绑定的处置动作 (Trash / Archive / Zip / Delete)
        Action->>OS: 执行系统文件操作
        Action->>Audit: 记录本次自动化处理日志流水
        Action->>UI: 发送通知与流式事件 (emit: "policy_triggered")
    end
```

### 4.2 策略 Dry-Run 预演与手动执行流 (Dry-Run & Manual Execution Flow)

```mermaid
sequenceDiagram
    autonumber
    actor User as 用户
    participant UI as 前端控制台 (React)
    participant IPC as Tauri IPC Command
    participant Scanner as 目录遍历器 (jwalk)
    participant Matcher as 复合匹配器 (Matcher)
    participant Guard as 安全守卫 (PathGuard)
    participant Action as 处置动作器 (ActionHandler)

    User->>UI: 编辑策略，点击「Dry-Run 真实预演」
    UI->>IPC: invoke("dry_run_policy", { policy })
    IPC->>Guard: 预检目标目录
    Guard-->>IPC: 检查通过
    IPC->>Scanner: 启动多线程遍历目标目录
    Scanner->>Matcher: 逐个文件比对规则 (Glob / Regex / 尺寸 / 时间)
    Matcher-->>IPC: 收集命中文件列表与预计释放体积 (不作任何改动)
    IPC-->>UI: 返回命中项数组 (FileItem[])
    UI->>User: 弹出预演看板，展示预计处理项并支持单项勾选

    User->>UI: 确认无误，点击「立即执行」
    UI->>IPC: invoke("execute_policy_now", { policy_id, selected_paths })
    IPC->>Action: 批次执行动作 (移入废纸篓/归档/Zip/粉碎)
    Action-->>UI: 流式回报执行进度 (10%..50%..100%)
    Action-->>UI: 完成并刷新审计记录
```

---

## 5. 安全防御架构 (PathGuard & Defense-in-Depth)

作为一款具备“自动文件操作与粉碎”能力的强大工具，系统构建 4 重纵深安全防护盾：

```mermaid
flowchart TD
    Config["配置/触发策略 (Manual/Cron/Watcher)"] --> Level1["第 1 防线：目标根路径白名单 & 黑名单检查"]
    Level1 -->|"阻断 /System, /Library, C:\\Windows, /bin, /usr 等"| Level2["第 2 防线：符号链接逃逸防护 (Symlink Canonicalization)"]
    Level2 -->|"路径规范化 (canonicalize)，防止 ../ 跳入系统目录"| Level3["第 3 防线：操作动作分级安全锁 (Safety Lock)"]
    Level3 -->|"普通废纸篓: 默认开放\n物理粉碎: 强制要求输入危险确认文字"| Level4["第 4 防线：文件占用与权限异常优雅隔离 (Fault Tolerance)"]
    Level4 -->|"正被外部打开或只读: 捕获 OS 错误，优雅标记 Skipped，绝不硬删"| AuditDone["操作落地并记录全量审计日志"]
```

1. **绝对禁飞区 (Restricted Root Blacklist)**:
   - 系统级核心根路径严禁作为策略监控目录或归档目标。
   - 包含：macOS（`/`, `/System`, `/Library`, `/Applications`, `/usr`, `/bin`, `/private`, `~/.Trash`）；Windows（`C:\`, `C:\Windows`, `C:\Program Files`, `C:\Program Files (x86)`, `C:\ProgramData`）；Linux（`/`, `/bin`, `/boot`, `/etc`, `/lib`, `/usr`, `/var`）。
2. **规范化防穿透 (Canonicalize Path)**:
   - 任何相对路径、包含 `..` 的路径均被解析为物理绝对路径后再进行黑名单碰撞，防止路径穿越攻击。
   - 默认禁止追踪符号链接软链（`follow_symlinks = false`），杜绝指向系统目录的软链被误操作。
3. **物理粉碎动作安全隔离**:
   - 若策略动作选择“永久物理粉碎 (Hard Delete)”，前端弹窗给出高对比度告警标识，必须由用户手动输入特定确认词汇方可保存策略。
   - Rust 后端对永久删除动作执行二次 PathGuard 校验，双重把关。
