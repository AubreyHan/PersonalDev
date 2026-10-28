# 详细技术规范与接口定义 (SPEC) - GitHub 自动同步

## 1. 核心数据模型 (Data Models)

### 1.1 TypeScript 接口定义 (`src/types/`)

```typescript
// 同步模式定义
export type SyncMode = 
  | 'bidirectional_newest' // 双向同步（新覆盖旧）
  | 'local_first'          // 本地优先（本地覆盖远端）
  | 'remote_first'         // 远端优先（远端覆盖本地）
  | 'stopped';             // 停止同步（默认）

// 同步运行状态
export type SyncStatus = 
  | 'idle'       // 空闲就绪
  | 'syncing'    // 正在同步（显示 Loading 动画）
  | 'success'    // 最近一次同步成功
  | 'error'      // 同步出错（显示红色告警）
  | 'offline';   // 网络离线

// 用户添加的根监视文件夹
export interface WatchedFolder {
  id: string;               // 唯一 UUID
  name: string;             // 文件夹显示名称，如 "Projects"
  path: string;             // 绝对路径，如 "/Users/aubrey/Projects"
  addedAt: number;          // 添加时间戳（毫秒）
  repoCount: number;        // 该文件夹下发现的 repo 总数
}

// 自动发现的 Git 仓库实体
export interface GitRepo {
  id: string;               // 仓库唯一标识 (路径的 hash 或标准化绝对路径)
  folderId: string;         // 所属根文件夹 ID
  name: string;             // 仓库目录名，如 "my-web-app"
  path: string;             // 仓库绝对根路径
  currentBranch: string;    // 当前分支，如 "main"
  syncMode: SyncMode;       // 当前配置的同步模式（默认为 'stopped'）
  status: SyncStatus;       // 当前运行状态
  lastSyncTime: number | null; // 最近同步成功的时间戳
  lastErrorMessage?: string; // 错误信息（若 status === 'error'）
  uncommittedChanges: number;// 未提交文件数量
}

// 同步历史记录项
export interface SyncLogItem {
  timestamp: number;
  repoPath: string;
  mode: SyncMode;
  success: boolean;
  commitHash?: string;
  commitMessage?: string;
  error?: string;
}
```

### 1.2 Rust 结构体与序列化 (`src-tauri/src/types.rs`)

```rust
use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "snake_case")]
pub enum SyncMode {
    BidirectionalNewest,
    LocalFirst,
    RemoteFirst,
    Stopped,
}

impl Default for SyncMode {
    fn default() -> Self {
        SyncMode::Stopped
    }
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "snake_case")]
pub enum SyncStatus {
    Idle,
    Syncing,
    Success,
    Error,
    Offline,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct WatchedFolder {
    pub id: String,
    pub name: String,
    pub path: String,
    pub added_at: i64,
    pub repo_count: usize,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct GitRepo {
    pub id: String,
    pub folder_id: String,
    pub name: String,
    pub path: String,
    pub current_branch: String,
    pub sync_mode: SyncMode,
    pub status: SyncStatus,
    pub last_sync_time: Option<i64>,
    pub last_error_message: Option<String>,
    pub uncommitted_changes: usize,
}
```

---

## 2. Tauri IPC 接口规范 (Commands & Events)

### 2.1 Rust Commands (前端 Invoke 调用)

| 命令名称 | 请求入参 | 返回值 | 职责描述 |
| :--- | :--- | :--- | :--- |
| `get_initial_data` | 无 | `{ folders: WatchedFolder[], repos: GitRepo[] }` | 窗口初始化加载时获取全量配置与仓库列表 |
| `add_watched_folder` | `{ path: string }` | `Result<WatchedFolder, string>` | 添加根监视目录，写入持久化并触发首次异步递归扫描 |
| `remove_watched_folder`| `{ folderId: string }` | `Result<(), string>` | 从监视列表中移除根目录，同时从内存和存储中卸载下属 Repos |
| `set_repo_sync_mode` | `{ repoId: string, mode: SyncMode }` | `Result<(), string>` | 右键切换仓库同步策略并持久化；若切换为有效同步状态，立即自动派发该仓库的首次同步 |
| `open_in_finder` | `{ path: string }` | `Result<(), string>` | 在系统文件管理器（Finder / Explorer）中高亮打开路径 |
| `get_settings` | 无 | `AppSettings` | 获取全局设置项（开机自启状态、防抖时长等） |
| `set_autostart_enabled` | `{ enabled: boolean }` | `Result<(), string>` | 开启或关闭系统开机自启动 |
| `factory_reset_and_exit` | 无 | `Result<(), string>` | 彻底注销系统自启项、关闭所有服务、删除配置文件并干净退出 |
| `check_disk_permission` | 无 | `Result<bool, string>` | 检测当前是否有磁盘/用户目录读取与写入权限 |
| `open_privacy_settings` | 无 | `Result<(), string>` | 打开 macOS 系统的“隐私与安全性 -> 完全磁盘访问权限”面板 |
| `complete_first_run` | 无 | `Result<(), string>` | 标记首次启动引导完成，不再弹窗提示 |

#### 2.1.1 文件夹添加防嵌套校验逻辑 (`add_watched_folder`)
在向监视池添加新路径前，后端通过 `std::fs::canonicalize` 解析软链接并标准化路径，防止父子重叠监控：
```rust
let new_canonical = std::fs::canonicalize(&input_path)
    .map_err(|e| format!("路径不存在或无法访问: {}", e))?;

for existing in &stored_folders {
    let exist_canonical = std::fs::canonicalize(&existing.path).unwrap_or_else(|_| PathBuf::from(&existing.path));
    if new_canonical == exist_canonical {
        return Err("该文件夹已存在于监控列表中".into());
    }
    if new_canonical.starts_with(&exist_canonical) {
        return Err(format!("无法添加：该目录属于已添加文件夹【{}】的子目录", existing.name));
    }
    if exist_canonical.starts_with(&new_canonical) {
        return Err(format!("无法添加：该目录包含了已监控的子文件夹【{}】，请先移除子文件夹", existing.name));
    }
}
```

### 2.2 Tauri Events (后端向前端异步广播)

| 事件名称 | Payload 结构 | 触发时机 |
| :--- | :--- | :--- |
| `repo:discovered` | `GitRepo` | 扫描器发现新仓库时增量通知 |
| `repo:removed` | `{ repoId: string }` | 仓库物理目录被删除时通知 |
| `repo:status_changed` | `{ repoId: string, status: SyncStatus, lastSyncTime?: number, error?: string }` | 某仓库进入同步、完成或报错时通知 |
| `repo:branch_changed` | `{ repoId: string, newBranch: string }` | 用户在终端切换分支时通知 |

### 2.3 窗口与托盘生命周期事件拦截 (Window Lifecycle)

主窗口点击右上角关闭按钮（`X`）时，系统自动拦截该关闭事件并转为隐藏窗口，保持应用常驻托盘：

```rust
// src-tauri/src/main.rs (Tauri 2.0 窗口事件监听)
.on_window_event(|window, event| match event {
    tauri::WindowEvent::CloseRequested { api, .. } => {
        api.prevent_close();
        window.hide().unwrap();
    }
    _ => {}
})
```
* **托盘退出与优雅关停所有服务 (Graceful Shutdown)**：
  当用户在系统托盘右键菜单中点击“退出应用 (Quit)”时，触发全局服务优雅终止流水线：
  ```rust
  // 1. 触发取消令牌，通知所有正在执行与等待的后台异步任务
  cancellation_token.cancel();
  
  // 2. 卸载并释放 notify 文件监听器线程池与通道
  watcher_manager.shutdown_all().await;
  
  // 3. 停止 5s 防抖与 5m 远程轮询调度器，等待正在执行的 Git 子进程安全返回（最长等待 3 秒）
  scheduler.drain_and_stop().await;
  
  // 4. 将内存中未落盘的配置与最新同步时间戳写入本地 JSON 存储
  storage.flush().await;
  
  // 5. 退出进程，确保不产生孤儿 Git 进程或 .git/index.lock 残留
  app_handle.exit(0);
  ```

* **移除文件夹时的关联服务与缓存彻底注销 (`remove_watched_folder`)**：
  ```rust
  // 1. 查找属于该 folder_id 的所有 Repo
  let repos_to_remove = storage.get_repos_by_folder(folder_id).await;
  
  // 2. 终止并注销对应目录的 notify watcher
  watcher_manager.unwatch_folder(folder_path).await;
  
  // 3. 取消这些 repo 在调度器中的防抖任务与定时轮询句柄
  for repo in repos_to_remove {
      scheduler.cancel_repo(&repo.id).await;
      storage.remove_repo_config(&repo.path).await;
  }
  
  // 4. 从存储中移除该 folder 并立即落盘，通知前端更新
  storage.remove_folder(folder_id).await;
  storage.flush().await;
  ```

* **彻底注销与数据清理 (`factory_reset_and_exit`)**：
  ```rust
  // 1. 注销操作系统开机自启动配置项
  let _ = autostart_manager.disable();
  
  // 2. 停掉全局所有 watcher 和后台任务
  cancellation_token.cancel();
  watcher_manager.shutdown_all().await;
  scheduler.drain_and_stop().await;
  
  // 3. 彻底删除本地配置文件目录
  if let Ok(config_dir) = app_handle.path().app_config_dir() {
      let _ = std::fs::remove_dir_all(config_dir);
  }
  
  // 4. 彻底退出应用
  app_handle.exit(0);
  ```

---

## 3. Git 同步策略核心算法 (Sync Strategy Algorithms)

所有 Git 操作通过 `tokio::process::Command` 异步调度，执行工作目录设为目标 Repo 的根目录。

### 3.0 首次开启同步前置预检 (Pre-flight Health Check)
当用户首次从 `stopped` 切换到任一非停止模式时，在正式注册到调度器前必须通过此同步健康自检：
```rust
pub async fn run_preflight_check(repo_path: &Path) -> Result<(), String> {
    // 1. 检查是否存在 remote origin
    let remote_output = run_git_cmd(repo_path, &["remote", "get-url", "origin"]).await?;
    if remote_output.trim().is_empty() {
        return Err("该仓库未配置远程源 (Remote origin 不存在)，无法开启自动同步".into());
    }

    // 2. 检查是否处于冲突中断状态 (例如 rebase-apply, MERGE_HEAD)
    let git_dir = repo_path.join(".git");
    if git_dir.join("MERGE_HEAD").exists() || git_dir.join("rebase-apply").exists() {
        return Err("当前工作区处于 Merge / Rebase 冲突中断状态，请先手动处理解决".into());
    }

    // 3. 校验远端连通性与鉴权 (轻量级探测)
    let ls_remote = run_git_cmd(repo_path, &["ls-remote", "--exit-code", "-h", "origin"]).await;
    if let Err(e) = ls_remote {
        return Err(format!("无法连接到远程仓库或缺少访问权限: {}", e));
    }

    // 4. 检查当前分支是否已建立远程追踪分支 (Upstream)
    let upstream_check = run_git_cmd(repo_path, &["rev-parse", "--abbrev-ref", "@{u}"]).await;
    if upstream_check.is_err() {
        return Err("当前分支在远端尚无对应分支，已暂停自动同步，请先手动 push 建立关联".into());
    }

    Ok(())
}

### 3.0.1 大文件 (>100MB) 自动检测与 `.gitignore` 标签写入
```rust
pub async fn handle_large_files(repo_path: &Path, changed_files: &[String]) -> Result<Vec<String>, String> {
    const MAX_FILE_SIZE: u64 = 100 * 1024 * 1024; // 100MB 限制
    let mut detected_large_files = Vec::new();

    for rel_path in changed_files {
        let full_path = repo_path.join(rel_path);
        if let Ok(meta) = std::fs::metadata(&full_path) {
            if meta.is_file() && meta.len() >= MAX_FILE_SIZE {
                detected_large_files.push(rel_path.clone());
            }
        }
    }

    if !detected_large_files.is_empty() {
        let gitignore_path = repo_path.join(".gitignore");
        let existing = std::fs::read_to_string(&gitignore_path).unwrap_or_default();
        let mut updated = existing.clone();

        if !updated.contains("# large files") {
            if !updated.is_empty() && !updated.ends_with('\n') {
                updated.push('\n');
            }
            updated.push_str("\n# large files\n");
        }

        for path in &detected_large_files {
            if !updated.lines().any(|l| l.trim() == path) {
                updated.push_str(&format!("{}\n", path));
            }
        }

        std::fs::write(&gitignore_path, updated).map_err(|e| format!("写入 .gitignore 失败: {}", e))?;

        // 从暂存区中剔除大文件
        for path in &detected_large_files {
            let _ = run_git_cmd(repo_path, &["rm", "--cached", path]).await;
        }
    }

    Ok(detected_large_files)
}
```
```

### 3.1 模式一：双向同步（新覆盖旧）- `bidirectional_newest`
1. **收集本地状态**：执行 `git status --porcelain`。若有本地改动：
   - 提取改动文件列表，生成结构化 Commit Message。
   - 执行 `git add -A` 与 `git commit -m "<structured_message>"`。
2. **拉取远端更新**：
   - 执行 `git fetch origin <current_branch>`。
   - 检查本地分支与远端 `origin/<current_branch>` 差异：
     * 若本地领先 (Ahead)：执行 `git push origin <current_branch>`。
     * 若远端领先 (Behind)：执行 `git merge --ff-only origin/<current_branch>` 或 `git rebase`。
     * 若发生分叉 (Diverged 冲突)：
       - 提取冲突文件的本地最后修改时间戳与远端最后 commit 时间戳。
       - **新覆盖旧策略**：若本地修改时间戳晚于远端 commit 时间戳，保留本地版本（`git checkout --ours <file>`）；反之采用远端版本（`git checkout --theirs <file>`）。
       - 重新 `git add -A` 并 `git commit -m "auto-sync: resolve conflict by newer timestamp"`，最后 `git push`。

### 3.2 模式二：本地优先（本地覆盖远端）- `local_first`
以本地代码为不可动摇的唯一标准，适合个人笔记库或单人专属项目。
1. 若有未暂存/未提交改动：
   - `git add -A`
   - `git commit -m "<structured_message>"`
2. 若本地当前没有新的 commit 且与远端无差异，直接结束。
3. 执行安全推送：
   - 首先尝试常规 `git push origin <current_branch>`。
   - 若因远端有新提交被拒绝（Non-fast-forward）：执行 `git push --force-with-lease origin <current_branch>`，强制将远端拉齐至本地状态。

### 3.3 模式三：远端优先（远端覆盖本地）- `remote_first`
以 GitHub/远端代码为权威版本，适合追踪开源仓库或不需要保存本地草稿的场景。
1. 执行 `git fetch origin <current_branch>`。
2. 丢弃或暂存本地未提交的改动：
   - 若本地有工作区未提交文件，执行 `git reset --hard` 并 `git clean -fd` 清除未跟踪文件。
3. 强制重置到远端最新 commit：
   - 执行 `git reset --hard origin/<current_branch>`。
4. 更新状态为“已同步”。

### 3.4 模式四：停止同步 - `stopped`
* Watcher 接收到文件变动事件后，匹配到该 Repo 为 `stopped` 模式，直接无视并丢弃事件，不执行任何 Git 指令。

---

## 4. Commit Message 自动生成规范 (Commit Formatter)

Commit Message 必须包含**时间戳**与**变动的文件摘要**，便于用户在 GitHub 提交历史中清晰识别变动范围。

### 4.1 格式模版
```text
auto-sync: YYYY-MM-DD HH:mm:ss - {TOTAL_FILES} files ({DETAILS})
```

### 4.2 解析逻辑
通过 `git status --porcelain` 输出的行前缀判断文件变动类别：
* `M ` / ` M` -> `modified`
* `A ` / `??` -> `added`
* `D ` / ` D` -> `deleted`
* `R ` -> `renamed`

### 4.3 摘要规则
1. 提取所有变动文件的文件名（不带冗长前缀）。
2. 若总变动文件数 $\le 3$：完整输出每一个文件的状态与文件名。
   * 示例：`auto-sync: 2026-10-27 14:05:00 - 2 files (modified: App.tsx; added: README.md)`
3. 若总变动文件数 $> 3$：输出前 2 个变动文件，其余以 `+N more` 折叠。
   * 示例：`auto-sync: 2026-10-27 14:05:00 - 8 files (modified: index.ts, style.css, +6 more)`

---

## 5. 本地持久化格式 (JSON Storage Schema)

存储文件路径位于系统应用支持目录：
* macOS: `~/Library/Application Support/com.antigravity.github-auto-sync/config.json`
* Windows: `%APPDATA%\com.antigravity.github-auto-sync\config.json`

```json
{
  "version": 1,
  "settings": {
    "debounceSeconds": 5,
    "remotePollIntervalMinutes": 5,
    "launchAtStartup": true
  },
  "watchedFolders": [
    {
      "id": "e4b2d184-482a-4c28-9844-32b4b4550001",
      "name": "Projects",
      "path": "/Users/aubrey/Projects",
      "addedAt": 1793080800000
    }
  ],
  "repoConfigs": {
    "/Users/aubrey/Projects/my-app": {
      "syncMode": "bidirectional_newest",
      "lastSyncTime": 1793081200000
    },
    "/Users/aubrey/Projects/personal-notes": {
      "syncMode": "local_first",
      "lastSyncTime": 1793081150000
    }
  }
}
```

---

## 6. 运行日志与 FIFO 淘汰规范 (Logging & FIFO Rotation)

### 6.1 日志存储位置规范（App 目录内聚）
为确保应用被删除时日志同步抹除、不产生离散垃圾文件残留，日志文件统一存放于应用包体目录内：
* macOS: `GitHubSync.app/Contents/Logs/app.log`
* 本地开发调试兜底: `./Logs/app.log`

### 6.2 单行日志结构与等级规范
每行日志格式定义如下：
```text
[YYYY-MM-dd HH:mm:ss] [LEVEL] [TAG] Message
```
* **LEVEL 等级**：
  * `[INFO]`：应用生命周期、目录增删、配置加载、防抖计时器调度等常规运行事件；
  * `[SYNC]`：Git 同步管道发起、Commit 提交摘要、推送成功、合并与冲突按时间戳覆盖结果；
  * `[WARN]`：前置自检阻断、未关联远端、大文件防护拦截提示；
  * `[ERROR]`：Git 执行报错、网络连通性异常、非法路径冲突；
  * `[SYSTEM]`：系统级状态，如 FIFO 清理标记行。

### 6.3 FIFO 循环淘汰算法伪代码
```rust
const MAX_FILE_SIZE: u64 = 1024 * 1024;      // 1.0 MB 限制
const PRUNE_TARGET_SIZE: usize = 700 * 1024; // 触发修剪后的保留目标容量 (~700KB)

pub fn append_log(line: &str) {
    let current_size = get_file_size("app.log");
    if current_size + line.len() as u64 >= MAX_FILE_SIZE {
        prune_fifo("app.log");
    }
    write_to_end("app.log", line);
}

fn prune_fifo(path: &str) {
    let data = read_all_bytes(path);
    if data.len() <= PRUNE_TARGET_SIZE { return; }

    // 取尾部 PRUNE_TARGET_SIZE 字节
    let start_idx = data.len() - PRUNE_TARGET_SIZE;
    let mut slice = &data[start_idx..];

    // 寻找最近的换行符对齐到完整行首，防止半行截断
    if let Some(pos) = slice.iter().position(|&b| b == b'\n') {
        slice = &slice[pos + 1..];
    }

    let mut new_data = Vec::new();
    new_data.extend_from_slice(b"[SYSTEM] --- \xE8\xA7\xA6\xE5\x8F\x91 FIFO \xE8\x87\xAA\xE5\x8A\xA8\xE6\xB8\x85\xE7\x90\x86\xEF\xBC\x88\xE4\xBF\x9D\xE6\x8C\x81\xE5\xAE\xB9\xE9\x87\x8F\xE5\x9C\xA8 1MB \xE4\xBB\xA5\xE5\x86\x85\xEF\xBC\x89\xEF\xBC\x8C\xE6\x97\xA7\xE6\x97\xA5\xE5\xBF\x97\xE5\xB7\xB2\xE6\xB7\x98\xE6\xB1\xB0 ---\n");
    new_data.extend_from_slice(slice);

    atomic_write(path, &new_data);
}
```
