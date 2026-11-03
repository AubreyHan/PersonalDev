# Vibe Coding 实施路线图 (IMPLEMENTATION_PLAN) - Garmin Activity Exporter

> [!IMPORTANT]
> **执行提示 (Execution Notice)**：
> 本文档是提供给 Antigravity 等 AI 编程助手在**独立业务工程代码仓库**中进行 Vibe Coding 时的实施指南。
> 开发者请勿在 `DevDoc` 文档仓库中执行实际代码创建。在新仓库中编码时，可逐阶段引用以下原子任务。

---

## 阶段一：项目脚手架与基础环境搭建 (Phase 1: Project Scaffolding)

### 任务 1.1：初始化 Tauri 2.0 + React 19 + TypeScript 脚手架
- **目标**：建立跨平台桌面应用基础结构，集成 Tailwind CSS 与图标库。
- **实施要点**：
  - 使用 `npm create tauri-app@latest`（选择 React + TypeScript + Vite）。
  - 安装前端依赖：`tailwindcss`, `postcss`, `autoprefixer`, `lucide-react`, `zustand`, `@tanstack/react-table`, `@tanstack/react-virtual`, `clsx`, `tailwind-merge`。
  - 在 `src-tauri/Cargo.toml` 中引入 Rust 依赖：`reqwest = { version = "0.12", features = ["json", "cookies", "rustls-tls"] }`, `tokio = { version = "1", features = ["full"] }`, `rusqlite = { version = "0.31", features = ["bundled"] }`, `keyring = "3"`, `serde`, `serde_json`, `chrono`, `regex`。
- **验证指令**：
  ```bash
  npm run build
  cargo check --manifest-path src-tauri/Cargo.toml
  ```

---

## 阶段二：安全凭据与 Garmin SSO 认证内核 (Phase 2: Auth & Keyring)

### 任务 2.1：实现系统钥匙串 (Keyring) 接口
- **目标**：封装安全保存/读取用户密码与 Session Token 的 Rust 模块。
- **实施要点**：
  - 创建 `src-tauri/src/auth/keychain.rs`。
  - 基于 `keyring::Entry` 封装 `save_credential`, `get_credential`, `delete_credential`，服务名设定为 `com.garmin.activity.exporter`。
- **验证指令**：
  ```bash
  cargo test --manifest-path src-tauri/Cargo.toml auth::keychain::tests
  ```

### 任务 2.2：构建 Garmin SSO 双区认证握手器
- **目标**：支持 `garmin.cn` 与 `garmin.com` 的登录流程与 CSRF/Cookie 管理。
- **实施要点**：
  - 创建 `src-tauri/src/auth/sso.rs`。
  - 模拟真实浏览器 Header（User-Agent, Origin, Referer），实现对 `/sso/signin` 的 GET（抓取 ticket flow 参数与 CSRF token）与 POST（提交凭据）。
  - 检测响应状态：若需 MFA，提取 ticket 标识并返回 `NEED_MFA` 状态；若成功，跟踪 302 重定向并持久化认证 Cookie。
- **验证指令**：
  ```bash
  cargo test --manifest-path src-tauri/Cargo.toml auth::sso::tests
  ```

---

## 阶段三：数据存储与 Garmin API 客户端 (Phase 3: DB & Garmin API)

### 任务 3.1：本地 SQLite 数据库与缓存层
- **目标**：实现活动元数据与导出历史记录的持久化。
- **实施要点**：
  - 创建 `src-tauri/src/db/schema.rs` 与 `history_repo.rs`。
  - 初始化 `activities`, `export_history`, `app_settings` 数据表。
  - 提供 `upsert_activities`、`mark_as_exported`、`query_unexported_ids`、`query_activities_page` 方法。
- **验证指令**：
  ```bash
  cargo test --manifest-path src-tauri/Cargo.toml db::history_repo::tests
  ```

### 任务 3.2：Garmin 活动列表分页检索客户端
- **目标**：拉取云端运动列表并同步写入本地缓存。
- **实施要点**：
  - 创建 `src-tauri/src/garmin/activities.rs`。
  - 请求 `proxy/activitylist-service/activities/search/activities`，按指定 `start`、`limit`、`activityType` 抓取数据，支持与本地 `export_history` 关联计算 `isExported` 状态。
- **验证指令**：
  ```bash
  cargo test --manifest-path src-tauri/Cargo.toml garmin::activities::tests
  ```

---

## 阶段四：导出调度引擎与文件归档 (Phase 4: Export Pipeline)

### 任务 4.1：命名模板与字符净化引擎
- **目标**：解析动态命名规则，支持直接平铺写入目标目录（无需且不建立任何嵌套子目录）。
- **实施要点**：
  - 创建 `src-tauri/src/exporter/template.rs` 与 `file_writer.rs`。
  - 支持 `{date}`, `{time}`, `{type}`, `{name}`, `{id}` 占位符解析，默认模板带 `{id}` 彻底杜绝平铺目录重名覆盖。
  - 内置字符过滤：将非法字符 `\ / : * ? " < > |` 转换为下划线，直接平铺写入用户选定的下载根目录。
- **验证指令**：
  ```bash
  cargo test --manifest-path src-tauri/Cargo.toml exporter::template::tests
  ```

### 任务 4.2：带限流保护的并发下载队列
- **目标**：批量获取 GPX/TCX 轨迹流并安全落盘。
- **实施要点**：
  - 创建 `src-tauri/src/exporter/queue.rs`。
  - 使用 `tokio::sync::Semaphore` 限制最大并发数为 2。
  - 下载请求之间注入 300~800ms 随机抖动延时，遇到 429 自动指数退避。
  - 通过 Tauri Event (`export:task-progress`) 实时广播下载进度。
- **验证指令**：
  ```bash
  cargo test --manifest-path src-tauri/Cargo.toml exporter::queue::tests
  ```

### 任务 4.3：多活动 GPX/TCX 无损轨迹合并引擎
- **目标**：按时间顺序串联多条活动轨迹，严格保证点位、高程、心率与踏频等扩展数据完整性。
- **实施要点**：
  - 创建 `src-tauri/src/exporter/merger.rs`。
  - 基于 XML 解析器（如 `quick-xml`），解析各段原始 GPX (`<trkseg>/<trkpt>`) 与 TCX (`<Lap>/<Trackpoint>`)。
  - 提取并校验各段活动时间戳，按 UTC 时间严格升序排布。
  - 100% 保留 Garmin 特有扩展节点（`ns3:TrackPointExtension` 下的 `hr`, `cad`, `atemp`, `speed`）。
  - 合并生成符合 XML 语法的单个轨迹文件并输出至目标平铺目录。
- **验证指令**：
  ```bash
  cargo test --manifest-path src-tauri/Cargo.toml exporter::merger::tests
  ```

---

## 阶段五：前端界面与交互打通 (Phase 5: UI & State Integration)

### 任务 5.1：登录窗口与双区切换 UI
- **目标**：实现精美现代的登录弹窗与 MFA 验证码输入。
- **实施要点**：
  - 创建 `src/components/LoginModal.tsx`。
  - 提供 Global 与 China 区域单选切换卡片。
  - 支持记住账号密码与 MFA 6 位数字验证码交互。
- **验证标准**：
  - 视觉还原规范，错误信息友好显示。

### 5.2：高性能活动列表与多维筛选器
- **目标**：支持虚拟滚动的大数据量活动展示与快速过滤。
- **实施要点**：
  - 创建 `src/components/FilterBar.tsx`：集成可视化双日历控件 (Calendar Date Range Picker，闭区间包含当天) 与运动类型下拉选择框 (Dropdown Select，涵盖全部/跑步/骑行/游泳/徒步等选项)，支持即时触发列表重查与筛选。
  - 创建 `src/components/ActivityTable.tsx`：基于极简设计规范构建，表格列严格精简为「选择勾选框」、「名字 (Name)」、「日期 (Date)」、「类型 (Type)」以及「导出状态 (Status)」，拒绝多余指标列干扰；当勾选 >= 2 条时顶部浮动栏展示「合并为一个文件导出」操作按钮，并可快速定位本地文件。
- **验证标准**：
  - 加载 1000+ 条记录时滚动平滑无卡顿，勾选批量导出按钮正确联动计数。

### 5.3：导出管理抽屉与设置中心
- **目标**：可视化查看导出进度条，并可配置保存目录与命名模板。
- **实施要点**：
  - 创建 `src/components/ExportDrawer.tsx`：展示并发任务状态（排队中/下载中/已完成/重试中），支持暂停与继续。
  - 创建 `src/components/SettingsDialog.tsx`：提供原生文件夹选择对话框、导出格式单选（GPX / TCX）以及命名规则实时预览。
- **验证标准**：
  - 修改命名模板时能实时预览最终文件名示例。

---

## 阶段六：全链路端到端验收与打包 (Phase 6: E2E Verification & Release)

### 任务 6.1：端到端真实链路验证
- **测试场景清单**：
  1. 使用真实的 Garmin.cn 与 Garmin Global 账号分别登录并拉取历史记录。
  2. 验证筛选功能：使用日历控件选择起止日期（闭区间包含当天）与运动类型下拉菜单，列表仅显示「名字」、「日期」与「类型」。
  3. 勾选 5 条活动批量导出为 GPX 格式，验证文件直接平铺写入目标文件夹，未创建任何子目录。
  4. 勾选 3 条前后连贯活动点击「合并为一个文件导出」，验证合并生成的单文件 XML 轨迹点、心率与踏频等传感器数据 100% 完整，能被 Strava 识别为单次运动。
  5. 执行“一键增量同步”，验证是否准确跳过已导出的记录。
  6. 验证退出并重启应用后，免密自动登录是否生效。

### 任务 6.2：跨平台打包
- **构建命令**：
  ```bash
  npm run tauri build
  ```
- **输出产物检查**：
  - macOS: `.dmg` 与 `.app` 文件（支持 Apple Silicon 与 Intel）。
  - Windows: `.msi` 或 `.exe` 安装程序。
