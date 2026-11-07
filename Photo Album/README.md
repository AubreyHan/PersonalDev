# Photo Album

QNAP TS-873A（QTS 5.x）上的照片导入软件，以 QPKG 形式安装，独立运行。第一期只做照片导入和冲突处理。

需求和设计文档在 DevDoc 仓库的 `photo-album/` 目录：

- `PRD.md`：需求（第一期已定稿）
- `ARCHITECTURE.md`：架构设计
- `IMPLEMENTATION_PLAN.md`：实施计划与验收清单

## 目录结构

| 路径 | 说明 |
| --- | --- |
| `cmd/photo-album-server` | 程序入口（HTTP 服务 + 状态机），包含卸载清理 `-uninstall-cleanup` |
| `internal/walk` | 遍历、跳过规则（`@`/`.` 开头）、扩展名白名单 |
| `internal/metadata` | 纯 Go 元数据解析：EXIF（JPEG/PNG/WebP/TIFF 系 RAW/RAF/CR3/HEIC）、QuickTime、Apple ContentIdentifier、NAS 时区 |
| `internal/store` | SQLite（`<相册目录>/.photo-album/photo-album.db`） |
| `internal/indexer` | 相册 MD5 索引（全量可续做 / 增量） |
| `internal/importer` | 一次扫描：写入检测、Live Photo 配对、判重、改名、移动、空目录清理 |
| `internal/mover` | 文件移动（跨卷时经临时文件，保留修改时间，继承权限） |
| `internal/importlog` | `logs/YYYYMMDD.log`，保留 90 天 |
| `internal/scheduler` | 定时扫描（按开始时间计时，同一时间只跑一次扫描） |
| `internal/app`、`internal/server` | 启动状态机、HTTP API |
| `web/` | React + Vite 前端（引导页 / 状态页），构建产物内嵌进二进制 |
| `qpkg/` | QDK 打包文件：`qpkg.cfg`、`package_routines`、服务脚本 |
| `scripts/` | `build_qpkg.sh`、`nas_probe.sh`（NAS 环境探查）、`genicons`（生成图标） |

## 开发

需要 Go 1.22+ 和 Node 20+。

```sh
go test ./...                       # 单元测试和端到端测试
(cd web && npm install && npm run build)
go run ./cmd/photo-album-server -data-dir /tmp/pa-data -share-root /tmp/pa-share -listen :8090
# 前端热更新：另开终端执行 (cd web && npm run dev)，/api 会代理到 :8090
```

## 打包

```sh
brew install gnu-sed gnu-tar grep coreutils   # 仅 macOS 首次需要
./scripts/build_qpkg.sh
```

产物是 `build/photo-album_<版本>_x86_64.qpkg`，可以直接在 App Center 中“手动安装”。

- 本机有 QDK 的 `qbuild` 时直接用它。
- 没有时，`scripts/qbuild_local.sh` 会拉取官方 QDK，在本机运行它的 `qbuild`：命令路径临时指向 GNU 工具，写进安装包的安装脚本保持原样。

安装包没有 QNAP 数字签名。安装前需要在 App Center → 设置中勾选“允许安装没有有效数字签名的应用程序”。

## 真机预研（Phase 0）

把 `scripts/nas_probe.sh` 拷贝到 NAS，通过 SSH 执行：

```sh
sh nas_probe.sh > probe.txt 2>&1
```

这个脚本只读，不修改任何文件。把输出发回，用于确认时区来源、`ionice` 是否可用、`/share` 结构、QPKG 安装位置和 ACL 设置。
