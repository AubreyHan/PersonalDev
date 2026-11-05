# Release Artifacts Rule

打包编译规则（Release Artifacts Workflow）：
- 当完成桌面应用打包（例如运行 `tauri build` 或 `npm run build:release`）后，必须将最终生成的 `.app` 应用程序和 `.dmg` 安装镜像文件自动/手动放入项目根目录下的 `release/` 子目录中：
  - 路径：`GarminAcitivities/release/`
  - 包含文件：`Garmin Activity Exporter.app` 与 `Garmin Activity Exporter_*.dmg`
- 可通过执行项目内脚本 `./scripts/copy_to_release.sh` 或直接运行 `npm run build:release` 自动化同步。
