# FilePurger 详细规格说明书 (SPEC)

---

## 1. 核心数据契约模型 (Core Data Models)

所有数据结构在前端 React (TypeScript) 与后端 Rust 中保持强类型对应，采用 `camelCase` 风格在 Tauri IPC 中透明序列化。

### 1.1 策略与规则模型 (`Policy`, `RuleSet`, `RuleCondition`)

#### TypeScript 类型定义 (`src/types/index.ts`)
```typescript
export type ConditionLogic = 'AND' | 'OR';

export type ConditionType = 
  | 'glob'             // Glob 通配符 (如 *.dmg, **/*.tmp)
  | 'regex'            // 正则表达式 (如 ^backup_\d{4}\.tar$)
  | 'time'             // 时间条件 (修改/访问/创建时间)
  | 'size'             // 文件体积大小
  | 'exclude_hidden';  // 排除隐藏文件

export type TimeField = 'mtime' | 'atime' | 'ctime';
export type TimeUnit = 'minutes' | 'hours' | 'days' | 'months';
export type TimeOperator = 'older_than' | 'newer_than' | 'between';

export interface TimeConditionData {
  field: TimeField;
  operator: TimeOperator;
  value: number;
  unit: TimeUnit;
  maxValue?: number;   // 针对 between 操作符
}

export interface SizeConditionData {
  operator: 'greater_than' | 'less_than' | 'between';
  minBytes: number;
  maxBytes?: number;
}

export interface RuleCondition {
  id: string;
  type: ConditionType;
  enabled: boolean;
  invertMatch: boolean; // 是否取反 (NOT)
  // 具体子条件载荷
  globPattern?: string;
  regexPattern?: string;
  timeData?: TimeConditionData;
  sizeData?: SizeConditionData;
}

export interface RuleSet {
  logic: ConditionLogic;      // AND (全部满足) 或 OR (任一满足)
  conditions: RuleCondition[];
  maxScanDepth?: number;      // 最大扫描目录递归深度 (可选，未指定为无限制)
  followSymlinks: boolean;    // 是否追踪软链接 (默认 false)
}
```

---

### 1.2 处置动作模型 (`ActionConfig`)

```typescript
export type ActionType = 
  | 'trash'          // 移入系统废纸篓 (默认安全)
  | 'archive'        // 移动归档至指定目录
  | 'zip'            // 压缩为 Zip 归档并清理源文件
  | 'hard_delete';   // 永久物理粉碎 (高危)

export interface ActionConfig {
  type: ActionType;
  // archive / zip 专用字段
  targetDirectory?: string;         // 归档目标根目录
  pathTemplate?: string;            // 归档子路径模板，支持 {YYYY}/{MM}/{DD}/{EXT}
  conflictStrategy?: 'rename' | 'overwrite' | 'skip'; // 重名处理策略 (默认 rename: 自动加序号)
  // zip 专用字段
  zipFileNameTemplate?: string;     // zip 包命名模板，如 "archive_{YYYY}{MM}{DD}_{POLICY}.zip"
  zipCompressionLevel?: number;     // 1~9，默认 6
  // hard_delete 专用字段
  safetyConfirmed?: boolean;        // 用户是否已在前端明确确认高危动作
}
```

---

### 1.3 调度模式模型 (`ScheduleConfig`)

```typescript
export type ScheduleType = 'manual' | 'cron' | 'watcher' | 'hybrid';

export interface ScheduleConfig {
  type: ScheduleType;
  // Cron 定时参数
  cronExpression?: string;          // 5位或6位 Cron 表达式，如 "0 18 * * 5"
  // Watcher 文件监听参数
  watcherEnabled: boolean;          // 是否启用变动监听
  debounceSeconds: number;          // 防抖延迟窗口 (秒，默认 5，范围 1~60)
  // 运行约束
  runOnStartup: boolean;            // 应用启动/托盘拉起时是否立即执行一次
}
```

---

### 1.4 完整策略模型 (`Policy`)

```typescript
export interface Policy {
  id: string;                       // 策略唯一 UUID
  name: string;                     // 策略友好名称，如 "下载目录双周保洁"
  description?: string;             // 策略说明
  enabled: boolean;                 // 是否全局启用该策略
  targetDirectories: string[];      // 目标监听/扫描目录集合 (绝对路径)
  ruleSet: RuleSet;                 // 复合规则集合
  action: ActionConfig;             // 处置动作配置
  schedule: ScheduleConfig;         // 调度方式配置
  createdAt: number;                // 创建毫秒时间戳
  updatedAt: number;                // 最后修改时间戳
  lastExecutedAt?: number;          // 最后一次触发时间
  lastExecuteResult?: {
    success: boolean;
    filesCount: number;
    bytesProcessed: number;
    message?: string;
  };
}
```

---

### 1.5 审计流水模型 (`AuditRecord`)

```typescript
export interface AuditRecord {
  id: string;                       // 审计记录 UUID
  timestamp: number;                // 执行时间戳
  policyId: string;                 // 关联策略 ID
  policyName: string;               // 策略名称
  triggerType: 'manual' | 'cron' | 'watcher'; // 触发源
  actionType: ActionType;           // 执行动作
  totalFilesProcessed: number;      // 成功处理的文件数
  totalBytesFreed: number;          // 释放/归档的总字节数
  durationMs: number;               // 耗时毫秒
  status: 'success' | 'partial_error' | 'failed';
  errorMessage?: string;
  processedItems: {
    originalPath: string;
    newPath?: string;               // 归档目标路径 (若有)
    sizeBytes: number;
    status: 'ok' | 'skipped_locked' | 'permission_denied';
  }[];
}
```

---

### 1.6 Rust 数据结构映射 (`src-tauri/src/models/mod.rs`)

```rust
use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct Policy {
    pub id: String,
    pub name: String,
    pub description: Option<String>,
    pub enabled: bool,
    pub target_directories: Vec<String>,
    pub rule_set: RuleSet,
    pub action: ActionConfig,
    pub schedule: ScheduleConfig,
    pub created_at: i64,
    pub updated_at: i64,
    pub last_executed_at: Option<i64>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct RuleSet {
    pub logic: String, // "AND" | "OR"
    pub conditions: Vec<RuleCondition>,
    pub max_scan_depth: Option<usize>,
    pub follow_symlinks: bool,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ActionConfig {
    pub r#type: String, // "trash" | "archive" | "zip" | "hard_delete"
    pub target_directory: Option<String>,
    pub path_template: Option<String>,
    pub conflict_strategy: Option<String>,
    pub zip_file_name_template: Option<String>,
    pub zip_compression_level: Option<u32>,
    pub safety_confirmed: Option<bool>,
}
```

---

## 2. Tauri IPC 通信协议接口 (Commands & Events)

### 2.1 双向指令接口 (Tauri Commands)

| 指令名称 | 输入参数 (Payload) | 返回值 (Result) | 说明 |
| :--- | :--- | :--- | :--- |
| `list_policies` | `None` | `Result<Vec<Policy>, String>` | 获取当前所有已配置的策略列表 |
| `save_policy` | `{ policy: Policy }` | `Result<Policy, String>` | 新增或更新策略（触发后端 Watcher/Cron 动态重载） |
| `delete_policy` | `{ policyId: String }` | `Result<bool, String>` | 删除指定策略，销毁关联的监听器与定时器 |
| `toggle_policy` | `{ policyId: String, enabled: bool }` | `Result<bool, String>` | 启用或停用单个策略 |
| `dry_run_policy` | `{ policy: Policy }` | `Result<DryRunResult, String>` | **真实目录预演**: 仅扫描比对，返回命中清单与体积，不执行真实改动 |
| `test_regex_sandbox` | `{ pattern: String, testStrings: Vec<String> }` | `Result<Vec<RegexMatchResult>, String>` | **沙盒测试**: 验证正则表达式合法性与命中详情 |
| `execute_policy_now` | `{ policyId: String, targetPaths?: Vec<String> }` | `Result<AuditRecord, String>` | 立即手动触发执行指定策略（支持仅执行部分勾选文件） |
| `pause_all_schedulers` | `None` | `Result<bool, String>` | 托盘快捷操作：全局暂停所有后台 Watcher 和 Cron |
| `resume_all_schedulers` | `None` | `Result<bool, String>` | 恢复所有后台调度 |
| `get_audit_history` | `{ limit: usize, offset: usize }` | `Result<Vec<AuditRecord>, String>` | 分页获取审计历史日志流水 |
| `clear_audit_history` | `None` | `Result<bool, String>` | 清空审计日志 |
| `export_config` | `None` | `Result<String, String>` | 导出全部策略配置为 JSON 字符串 |
| `import_config` | `{ jsonContent: String }` | `Result<bool, String>` | 导入并校验策略配置文件 |

---

### 2.2 流式事件接口 (Event Channels)

- `app:tray_status_change`: 广播后台常驻状态（空闲 / 正在执行 / 暂停）。
- `policy:triggered`: 某条策略被 Cron 或 Watcher 触发执行，携带策略 ID 与简要概况。
- `policy:progress`: 策略批量处理中派发百分比进度条（如归档/压缩中）。
- `policy:completed`: 策略处理完成，携带本次生成的一条 `AuditRecord`。
- `policy:error`: 处理过程捕获异常（如权限拒绝、目录被移除）。

---

## 3. 模板变量与路径规范 (Path Templating Specification)

当策略动作选用 `archive`（移动归档）或 `zip`（压缩归档）时，支持在路径中内嵌动态变量。后端负责在运行时原子解析：

| 模板变量 | 说明 | 解析示例 (当前时间 2026-11-03 20:30) |
| :--- | :--- | :--- |
| `{YYYY}` | 4 位当前年份 | `2026` |
| `{MM}` | 2 位当前月份 (补零) | `11` |
| `{DD}` | 2 位当前日期 (补零) | `03` |
| `{POLICY}` | 当前策略名称的 Slug 形式 | `downloads-cleaner` |
| `{EXT}` | 原始文件的小写扩展名 (无点) | `dmg`, `zip`, `log` |
| `{FILE_MTIME_YYYY}` | 目标文件本身的最后修改年份 | `2024` |
| `{FILE_MTIME_MM}` | 目标文件本身的最后修改月份 | `08` |

**示例应用**:
- 配置归档路径模板: `/Volumes/Storage/Archives/{YYYY}/{MM}/{EXT}/`
- 输入文件: `app_installer.dmg`
- 实际落地路径: `/Volumes/Storage/Archives/2026/11/dmg/app_installer.dmg`

---

## 4. 边界条件与异常处理矩阵 (Edge Cases Matrix)

| 场景编号 | 边界/异常情形 | 发生原因 | 系统处理机制与防御策略 |
| :--- | :--- | :--- | :--- |
| **EC-01** | 用户配置目标包含核心系统目录 | 误选 `/System` 或 `C:\Windows` | `PathGuard` 在前端与后端拦截，直接拒绝创建/保存该策略，返回 `403 Forbidden Directory`。 |
| **EC-02** | 软链接形成死循环 (Symlink Loops) | 目录内包含软链接指向自己或上级 | 扫描器默认 `follow_symlinks = false`，将软链接视为普通孤立条目，绝不递归深入。 |
| **EC-03** | 文件正被外部进程写入 (大文件下载未完成) | 浏览器正在向 Downloads 写入 `.crdownload` | Watcher 具备 5~30 秒防抖窗口；动作执行时检测文件独占锁，若报错优雅跳过并在下周期重试。 |
| **EC-04** | 正则表达式语法错误或 ReDoS 攻击 | 用户输入了非法或极端复杂正则 | 后端使用 Rust `regex` crate，在线性时间保证下解析；解析失败返回具体语法错误行，前端标红提示。 |
| **EC-05** | 归档目标目录所在外置硬盘被拔出 | 外置磁盘意外离线 | 捕获 I/O 错误，策略执行失败并写入审计异常日志，系统推送桌面错误通知，不造成程序崩溃。 |
| **EC-06** | 移动归档时发生文件名冲突 | 目标路径已存在同名文件 | 依据配置策略处理：默认 `rename`（自动追加 `(1)`, `(2)` 序列后缀）；或 `skip`（跳过当前文件）。 |
| **EC-07** | 用户在托盘一键关闭全部调度 | 用户进入专注模式不希望有后台磁盘操作 | `pause_all_schedulers` 暂时挂起 Tokio Watcher 接收器与 Cron 计时器，托盘图标变灰展示“暂停中”。 |
| **EC-08** | 高危直接粉碎误触防范 | 用户选择了 `hard_delete` | UI 强制要求输入大写确认文本（如 `DELETE PERMANENTLY`），后端检查 `safety_confirmed` 标记，否则拒绝擦除。 |
