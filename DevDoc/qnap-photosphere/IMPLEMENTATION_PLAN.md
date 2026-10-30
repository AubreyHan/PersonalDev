# 实施路线图 (IMPLEMENTATION_PLAN) - qnap-photosphere

本文档为面向 AI 辅助编程 (Vibe Coding) 的原子化实施清单。后续在独立代码工程中开发时，可按各阶段原子任务逐步执行并严格验收。

---

## Phase 1: 基础骨架与 QPKG 编译环境

### Task 1.1: 初始化 Go 工程与 QDK 打包规约
- **目标**：搭建 Go 1.22+ 基础工程结构，配置针对威联通 x86_64 的静态交叉编译脚本与 QDK 配置模版。
- **实施内容**：
  - 创建 `cmd/server/main.go`，引入 `gin` 或 `fiber`，提供 `/api/ping` 健康检查路由。
  - 创建 `qpkg.cfg`，配置 Package Name `qnap-photosphere`, Target Arch `x86_64`, DisplayName `PhotoSphere`。
  - 编写 `scripts/build_qpkg.sh`，支持在 Linux/macOS 环境下一键编译 Go 二进制并执行 `qbuild` 生成 `.qpkg` 文件。
- **验收验证**：
  ```bash
  go test ./...
  ./scripts/build_qpkg.sh
  # 验证输出 dist/qnap-photosphere_0.1.0_x86_64.qpkg
  ```

### Task 1.2: 冷热数据分层配置与 SQLite 初始化
- **目标**：实现配置文件解析器，支持指定 SSD 卷缓存路径与 HDD 归档路径，初始化内嵌 SQLite 并配置 WAL 模式。
- **实施内容**：
  - 编写 `internal/config/config.go`，解析 YAML / 环境变量：
    - `cache_path`: `/share/CACHEDEV1_DATA/.qphotosphere` (SSD)
    - `storage_path`: `/share/CACHEDEV2_DATA/Photos` (HDD)
  - 编写 `internal/database/db.go`，自动在 `cache_path` 下创建 SQLite 数据库文件并执行 `SPEC.md` 中的建表 DDL。
- **验收验证**：
  ```bash
  go run ./cmd/server/main.go --config test_config.yaml
  sqlite3 test_cache/.qphotosphere/photosphere.db ".tables"
  # 输出 users, assets, albums, faces, persons
  ```

---

## Phase 2: 核心导入与物理归档管道

### Task 2.1: EXIF 解析与 WebP 双层缩略图生成器
- **目标**：实现图片 EXIF 解析（拍摄时间、GPS、相机参数）与高压缩比 WebP 缩略图极速生成。
- **实施内容**：
  - 编写 `internal/pipeline/exif.go`，从 JPEG, HEIC, RAW, MOV 中安全解析拍摄时间（优先 `DateTimeOriginal`）。
  - 编写 `internal/pipeline/thumbnail.go`，生成两级 WebP 缓存：
    - `small`: 300x300 网格图；
    - `preview`: 1920 宽长边预览图。
- **验收验证**：
  ```bash
  go test -v ./internal/pipeline -run TestExtractExifAndThumbnail
  # 验证生成对应的 .webp 文件且体积在合理范围 (8KB~200KB)
  ```

### Task 2.2: 物理归档落盘与同名冲突处理器
- **目标**：实现将文件从 Inbox 移动至 `/share/.../users/{username}/yyyymmdd/` 的核心逻辑。
- **实施内容**：
  - 编写 `internal/pipeline/archiver.go`。
  - 实现 SHA-256 去重拦截（内容一致直接移入回收站并复用记录）。
  - 实现同名冲突自动递增后缀（`_1.JPG`, `_2.JPG`）。
  - 自动成对处理 Live Photo（`.HEIC` + `.MOV`）与 RAW+JPG。
- **验收验证**：
  ```bash
  go test -v ./internal/pipeline -run TestArchiverCollisionAndDeduplicate
  # 断言同名不同内容文件被正确递增重命名，完全相同文件移入 .trash
  ```

---

## Phase 3: Web 端极速时间线与挑片工作流

### Task 3.1: 前端时间线虚拟网格与全屏灯箱
- **目标**：构建流畅的响应式前端界面，支持按月/按日折叠分组浏览。
- **实施内容**：
  - 在 `web/` 目录下初始化 React + Tailwind CSS 工程。
  - 实现虚拟滚动网格，所有图片直接请求后端 WebP 缩略图接口。
  - 实现双击/单击全屏大图预览灯箱。
- **验收验证**：
  ```bash
  cd web && npm run build
  # 编译生成 web/dist 目录，确认静态文件内嵌进 Go 二进制
  ```

### Task 3.2: 快捷键挑片 (P/X/星级) 与回收站一键清理
- **目标**：实现专业挑片流，全键盘无鼠标极速标记与批量处理。
- **实施内容**：
  - 在灯箱与网格视图挂载全局按键监听：`P` (Pick), `X` (Reject), `1~5` (Rating), `ArrowLeft/Right`。
  - 提供“一键清理淘汰照片”交互，调用后端接口将所有标记为 `rejected` 的资产批量软删除并移入 HDD `.trash/` 目录。
- **验收验证**：
  - 模拟按键流测试，验证后端 `assets` 表中 `pick_status` 正确变更，软删除后物理文件正确移动。

---

## Phase 4: Immich 移动端协议对接与多用户隔离

### Task 4.1: 兼容 Immich 认证与服务端信息接口
- **目标**：让官方 Immich 手机客户端能够顺利连接本服务并成功登录。
- **实施内容**：
  - 实现 `/api/server-info/version`, `/api/server-info/features`。
  - 实现 `/api/auth/login` 与 JWT 校验中间件。
  - 实现多租户隔离逻辑（各用户存储在独立的 `/share/.../users/{username}/` 目录下）。
- **验收验证**：
  ```bash
  curl -X GET http://localhost:8080/api/server-info/version
  # 返回标准 Immich 格式 JSON
  ```

### Task 4.2: 移动端媒体流上传与后台备份
- **目标**：支持手机端拍照后在后台无感上传原图与 Live Photo。
- **实施内容**：
  - 实现 `/api/asset/bulk-upload-check`（哈希查重）。
  - 实现 `/api/asset/upload`（流式写入 Inbox 并触发入库归档管道）。
- **验收验证**：
  - 使用 Immich 客户端进行多张照片上传测试，检查 NAS 对应用户目录下是否生成标准 `yyyymmdd` 归档文件。

---

## Phase 5: 低功耗 AI 推理引擎与动态温控调度

### Task 5.1: ONNX Runtime CPU 推理与轻量模型加载
- **目标**：基于 ONNX Runtime CGO 绑定，在 AMD Ryzen V1500B CPU 上运行轻量人脸与 CLIP 向量提取。
- **实施内容**：
  - 编写 `internal/ai/face.go`，集成 SCRFD + ArcFace 轻量 ONNX 模型。
  - 编写 `internal/ai/scheduler.go`，监控 `/proc/loadavg`。
- **验收验证**：
  ```bash
  go test -v ./internal/ai -run TestFaceDetectionAndEmbedding
  # 断言人脸框坐标与 512 维 Float32 特征向量正确生成
  ```

### Task 5.2: 夜间调度与系统负载让出机制
- **目标**：保证日常 NAS 使用不卡顿，重度 AI 计算限制在空闲时段运行。
- **实施内容**：
  - 实现基于 Cron 的定时调度器（默认仅在 01:00-06:00 期间消费重度 AI 队列）。
  - 在推理循环中实时检测 CPU 负载，超过 75% 时主动 sleep 避让。
- **验收验证**：
  - 模拟人工拉高系统 CPU 负载，验证 AI 队列在日志中输出“High system load detected, throttling worker”并暂停推理。
