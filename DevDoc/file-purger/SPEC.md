# FilePurger 详细规格说明书 (SPEC)

---

## 1. 核心数据契约模型 (Core Data Models)

所有数据结构在前端 React (TypeScript) 与后端 Rust 中保持类型镜像，采用 `camelCase` 风格在 IPC 中进行无缝序列化。

### 1.1 文件夹与规则模型 (`FolderItem`, `FolderRule`)

#### TypeScript 类型定义 (`src/types/index.ts`)
```typescript
export type TimeUnit = 'minutes' | 'hours' | 'days';

export type RuleAction = 
  | 'list'          // 列出：仅在匹配清单中展示，不做删除移动
  | 'keep'          // 保留：命中后锁定保留，短路终止后续规则
  | 'direct_trash'  // 直接删除：直接移入系统废纸篓
  | 'ask_trash';    // 询问后删除：应用内逐一弹窗询问确认

export interface DateAddedCondition {
  value: number;        // 填 0 或选 Any 表示不限时间
  unit: TimeUnit;       // 'minutes' | 'hours' | 'days'
  isAny: boolean;       // 是否不限制时间
}

export interface FolderRule {
  id: string;           // 规则唯一 UUID
  name: string;         // 规则友好名称，如 "Archive PDFs"
  enabled: boolean;     // 启用/停用开关 Toggle (默认 true)
  dateAdded: DateAddedCondition; // 添加到该文件夹的时间条件
  pattern: string;      // 通配符路径表达式 (如 "*.dmg, a*.j*, **/temp*")
  action: RuleAction;   // 处置动作 (4 选 1)
  order: number;        // 从上到下的优先匹配序号 (0, 1, 2...)
}

export interface FolderItem {
  id: string;           // 文件夹唯一 UUID
  name: string;         // 文件夹显示名称，如 "Downloads"
  path: string;         // 文件夹绝对物理路径，如 "/Users/aubreyhan/Downloads"
  isTrash: boolean;     // 是否为左侧顶部常驻废纸篓项 (如果是，右侧使用 TrashRuleConfig)
  isPreset: boolean;    // 是否为预置文件夹 (如默认预置 Downloads)
  topWhitelist: string; // 置顶保留规则内容 (逗号分隔的通配符表达式)
  rules: FolderRule[];  // 属于该文件夹的普通规则列表 (严格按 order 升序排列)
  createdAt: number;
}
```

---

### 1.2 回收站专属配置模型 (`TrashRuleConfig`)

```typescript
export interface TrashRuleConfig {
  // 规则 1：超龄天数清理 (填 0 表示不执行)
  daysThreshold: number; // 默认初始值 0
  // 规则 2：容量上限清理 (填 0 表示不执行)
  maxCapacityGB: number; // 默认初始值 0，单位 GB
}
```

---

### 1.3 逐一询问项模型 (`AskItem`) 与 匹配结果清单模型 (`MatchListItem`)

```typescript
export interface AskItem {
  id: string;           // 唯一任务 ID
  folderId: string;     // 来源文件夹 ID
  filePath: string;     // 文件的绝对路径
  fileName: string;     // 文件名
  sizeBytes: number;    // 文件大小
  dateAdded: number;    // 添加时间戳 (毫秒)
  ruleName: string;     // 触发的规则名称
}

export interface MatchListItem {
  filePath: string;     // 文件路径
  fileName: string;     // 文件名
  sizeBytes: number;    // 文件大小
  dateAdded: number;    // 添加时间戳
  ruleName: string;     // 命中的"列出"规则名称
}
```

---

### 1.4 Rust 契约模型映射 (`src-tauri/src/models/mod.rs`)

```rust
use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct FolderItem {
    pub id: String,
    pub name: String,
    pub path: String,
    pub is_trash: bool,
    pub is_preset: bool,
    pub top_whitelist: String,
    pub rules: Vec<FolderRule>,
    pub created_at: i64,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct FolderRule {
    pub id: String,
    pub name: String,
    pub enabled: bool,
    pub date_added: DateAddedCondition,
    pub pattern: String,
    pub action: String, // "list" | "keep" | "direct_trash" | "ask_trash"
    pub order: usize,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct DateAddedCondition {
    pub value: u32,
    pub unit: String, // "minutes" | "hours" | "days"
    pub is_any: bool,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct TrashRuleConfig {
    pub days_threshold: u32,
    pub max_capacity_gb: u32,
}
```

---

## 2. Tauri IPC 通信协议接口 (Commands & Events)

### 2.1 双向调用指令 (Tauri Commands)

| 指令名称 | 输入参数 (Payload) | 返回值 (Result) | 说明 |
| :--- | :--- | :--- | :--- |
| `get_all_folders` | `None` | `Result<Vec<FolderItem>, String>` | 获取左侧全部文件夹列表（包含首项常驻废纸篓） |
| `add_folder` | `{ path: String }` | `Result<FolderItem, String>` | 添加新监控文件夹（执行父子重叠检测与系统路径检测） |
| `remove_folder` | `{ folderId: String }` | `Result<bool, String>` | 从监控列表中移除文件夹（物理文件夹不受影响） |
| `update_top_whitelist` | `{ folderId: String, whitelist: String }` | `Result<bool, String>` | 更新选中文件夹的置顶保留规则 |
| `save_rule` | `{ folderId: String, rule: FolderRule }` | `Result<FolderRule, String>` | 新增或编辑修改单条普通规则 |
| `delete_rule` | `{ folderId: String, ruleId: String }` | `Result<bool, String>` | 删除指定普通规则 |
| `reorder_rules` | `{ folderId: String, ruleIds: Vec<String> }` | `Result<bool, String>` | 按前端拖拽后的 ID 列表重新设定所有规则的 order |
| `get_trash_config` | `None` | `Result<TrashRuleConfig, String>` | 获取废纸篓双规则配置 |
| `save_trash_config` | `{ config: TrashRuleConfig }` | `Result<bool, String>` | 保存废纸篓双规则配置 |
| `trigger_scan_now` | `{ folderId?: Option<String> }` | `Result<bool, String>` | 立即手动触发全量或单个文件夹的规则执行与扫描 |
| `respond_ask_item` | `{ itemId: String, decision: "trash" \| "keep" }` | `Result<bool, String>` | 用户对“询问后删除”单项做出手动决策 |
| `get_autostart_status`| `None` | `Result<bool, String>` | 查询当前开机自启动是否已在 macOS 激活 |
| `set_autostart` | `{ enabled: bool }` | `Result<bool, String>` | 切换 macOS 开机自启动注册状态 (`SMAppService`) |
| `clean_uninstall` | `None` | `Result<bool, String>` | 执行彻底卸载：注销自启、清除全部本地配置、移至废纸篓自杀 |

---

### 2.2 流式事件接口 (Event Channels)

- `app:ask_queue_update`: 当后台扫描命中“询问后删除”时向前端派发待确认项队列，唤起前端逐一询问弹窗。
- `app:match_list_update`: 派发当前文件夹最新命中的“列出”结果集给前端更新下方表格。
- `app:trash_status_changed`: 广播废纸篓当前总大小与清理统计。

---

## 3. 通配符与组合路径语法规范 (Pattern Grammar)

系统使用严格大小写敏感的通配符语法，支持单层、多层与多文件组合：

### 3.1 语法对照表

| 语法符号 | 语义说明 | 典型示例 | 匹配结果示例 |
| :--- | :--- | :--- | :--- |
| `*` | 匹配当前层级所有文件 | `*` | 匹配当前文件夹下的所有文件 |
| `a*` | 以 `a` 开头的所有文件 | `a*` | `apple.dmg`, `abc.pdf` |
| `*a` | 以 `a` 结尾的文件（**不含扩展名**） | `*a` | `banana.txt` (主干以 a 结尾), `data.log` (不匹配) |
| `a*.j*` | 文件名以 `a` 开头且扩展名以 `j` 开头 | `a*.j*` | `app.jpeg`, `apple.jpg` |
| `*/` 或 `a*/` | 匹配一级单层子文件夹 | `*/temp*` | 任意一级子目录下的 `temp*` 文件 |
| `**` | 递归所有子文件夹（多层） | `**/*.dmg` | 无论多深子目录下的所有 `.dmg` 文件 |
| `,` 或 `，` | 多个表达式分隔符（**OR 逻辑**） | `*.dmg, *.pkg, **/test*` | 命中这 3 项中任意 1 项即算命中 |

---

## 4. 边界条件与异常处理矩阵 (Edge Cases Matrix)

| 场景编号 | 异常/边界情形 | 发生原因 | 系统处理机制与防御策略 |
| :--- | :--- | :--- | :--- |
| **EC-01** | 用户尝试添加现有文件夹的父级 | 已有 `~/Downloads/code`，尝试添加 `~/Downloads` | `PathHierarchyGuard` 检测到父子重叠，前端弹出明确警告对话框并直接拒绝添加。 |
| **EC-02** | 用户尝试添加现有文件夹的子级 | 已有 `~/Downloads`，尝试添加 `~/Downloads/temp` | `PathHierarchyGuard` 识别为子级重叠，弹出警告并直接拒绝添加。 |
| **EC-03** | 尝试添加 macOS 核心系统目录 | 误选 `/System`, `/Library`, `/Applications` 等 | 后端直接拦截阻断，拒绝加入监控列表。 |
| **EC-04** | 目录中存在以 `.` 开头的隐藏文件 | 目录中有 `.DS_Store`, `.localized`, `.git/` | 扫描层自动忽略，通配符永远无法单独命中隐藏文件。 |
| **EC-05** | 子文件夹整体被规则命中移入废纸篓 | 规则为 `**/temp*/`，匹配了整份子文件夹 | 允许连带将该子文件夹内部的 `.DS_Store` 等隐藏文件一并移入废纸篓，确保目录整洁。 |
| **EC-06** | 目标文件正被其他应用程序占用独占写入 | 浏览器正在向 Downloads 写入未完成的文件 | 捕获 OS 文件独占锁定错误，优雅跳过，等待下一个防抖周期或下次巡检处理。 |
| **EC-07** | 大文件写入触发频繁变动通知 | 用户解压一个 10GB 包含上千文件的压缩包 | 防抖队列设定 3 秒窗口，写入期间不断重置倒计时，直至全部写入完毕静止 3 秒后仅触发 1 次扫描。 |
| **EC-08** | 用户对“询问后删除”弹窗长期不操作 | 弹窗唤起后用户未进行确认 | 状态保持挂起等待，文件依然保持在原处，绝不擅自删除；只有用户手动点击【移至废纸篓】才执行。 |
| **EC-09** | 废纸篓容量设置了 0 或天数设置了 0 | 用户只希望做普通文件夹清理，不碰废纸篓 | 值为 0 判定为规则关闭，后台绝不主动清倒系统废纸篓。 |
| **EC-10** | 彻底卸载应用 (Clean Uninstall) | 用户选择彻底清除 | 调用 macOS `SMAppService.unregister` 注销自启，抹除全部本地配置，将自身移入废纸篓并正常退出，0 垃圾残留。 |
