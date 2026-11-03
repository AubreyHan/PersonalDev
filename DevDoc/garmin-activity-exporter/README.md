# garmin-activity-exporter (佳明运动数据导出助手)

`garmin-activity-exporter` 是一款基于 **Tauri 2.0 (Rust) + React 19 + TypeScript** 构建的现代化轻量级桌面客户端。专门用于帮助运动爱好者与数据极客从 **Garmin Connect** 高效、安全地浏览、筛选并批量导出个人运动轨迹与活动数据。

---

## 核心特性

- **双区无缝兼容 (Garmin Global & Garmin.cn)**：
  - 界面一键切换国际区与中国区服务器。
  - 深度兼容 Garmin SSO 认证机制与 MFA 二步验证交互。
- **凭据安全持久化**：
  - 接入操作系统原生钥匙串 (macOS Keychain / Windows Credential Manager / Linux Secret Service) 加密存储凭据。
  - 本地缓存 Session Token 与 OAuth 凭据，启动自动静默重连，无需反复输入账号密码。
- **极简活动列表与直观检索**：
  - 极简列表视图设计，仅聚焦展示「名字」、「日期」与「类型」3 大核心属性，拒绝冗余数据堆砌。
  - 双日历控件精确起止日期闭区间筛选，配合运动类型下拉菜单（Dropdown）即时过滤。
- **通用轨迹文件 (GPX / TCX) 批量、增量与多活动无损合并导出**：
  - 以行业通用轨迹格式（`.gpx` 与 `.tcx`）为核心，便于直接无缝导入 Strava、郁金香运动、咕咚、Keep 或自建 GIS 分析工具。
  - **多活动轨迹无损合并**：支持勾选多条前后连贯的运动记录，一键合并为一个完整的 GPX/TCX 文件，严格对齐时间戳并 100% 保留心率、踏频、高程与传感器扩展数据。
  - 智能增量同步：一键检测本地缺失的最新运动，自动化批量补齐下载。
  - 带速率保护的并发下载队列，智能避免触发 Garmin 429 限流保护。
- **自定义保存目录与平铺直存**：
  - 自由选择本地下载目录，所有 GPX/TCX 文件直接保存至该目录，无需建立任何嵌套子目录。
  - 支持规范化命名防重：`{date}_{type}_{name}_{id}.gpx`。
- **极简原生体验与极小体积**：
  - 基于 Tauri 2.0 编译为原生机器码，内存占用低至 30MB~50MB，安装包仅约 15MB，拒绝臃肿的 Electron 资源损耗。

---

## 文档导航

本项目文档完全遵循 Vibe Spec 架构规范，为后续 AI 辅助编码提供完备的上下文输入：

- [PRD.md](file:///Users/aubreyhan/Tools/git_repo/PersonalDev/DevDoc/garmin-activity-exporter/PRD.md)：完整产品需求文档、用户角色画像、核心用户流程与验收指标。
- [ARCHITECTURE.md](file:///Users/aubreyhan/Tools/git_repo/PersonalDev/DevDoc/garmin-activity-exporter/ARCHITECTURE.md)：技术选型架构图、Tauri IPC 机制、Garmin API 交互协议、目录结构与错误恢复设计。
- [SPEC.md](file:///Users/aubreyhan/Tools/git_repo/PersonalDev/DevDoc/garmin-activity-exporter/SPEC.md)：Tauri Commands / Events 规范、本地 SQLite 数据字典、Garmin SSO 状态机与导出命名模板解析器。
- [IMPLEMENTATION_PLAN.md](file:///Users/aubreyhan/Tools/git_repo/PersonalDev/DevDoc/garmin-activity-exporter/IMPLEMENTATION_PLAN.md)：分阶段原子化 Vibe Coding 实施路线图与每一步的确定性验证指令。
