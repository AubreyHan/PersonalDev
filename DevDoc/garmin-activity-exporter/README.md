# garmin-activity-exporter (Garmin Activity 下载助手)

`garmin-activity-exporter` 是一款基于 **macOS 原生 Swift + SwiftUI** 构建的轻量级桌面应用，对接官方 **Garmin Connect Personal API**，用于浏览、按起始日期与现有类型筛选、下载及同类型合并导出个人运动记录（Activity）。

---

## 第一版 (v1) 核心功能边界

严格遵循精简聚焦原则，仅包含以下经确认的核心功能：

1. **Personal API 认证与双区自动探测**：
   - 使用官方 Garmin Connect Personal API Key 登录。
   - **无需手动选择区域**：通过换取 Token 接口（`POST /personalapitoken-service/personal-api/personal-api-key/token`）自动探测国际区（`personal.connectapi.garmin.com`）与中国区（`personal.connectapi.garmin.cn`），匹配成功后自动锁定区域并获取 24 小时有效的 JWT Bearer Token。
   - **安全持久化**：将 API Key 与所属区域保存在 **macOS Keychain** 中，下次启动自动静默换取/复用 Token 登录；界面提供“更换/退出 Key”按钮。
2. **起始日期日历筛选**：
   - 提供**单个「起始日期」日历控件**（无结束日期）。
   - 最早可选日期为 **`2000-01-01`**。
   - **每次打开软件时，起始日期默认重置为当天**。
3. **动态运动类型多选筛选**：
   - 采用**多选下拉框（Dropdown + 复选框）**。
   - **只显示选定起始日期至今实际存在的运动类型**（从拉取到的活动列表中动态提取）。
   - 日期变更后默认全选（显示全部），勾选/取消即时过滤列表。
4. **Activity 列表展示**：
   - 列表展示 **5 项基础信息**：**「名称」、「日期/时间」、「类型」、「距离」、「时长」**。
   - 支持单选或多选勾选 Activity。
5. **勾选下载与同类型合并**：
   - **导出格式**：支持选择 **GPX (`.gpx`) / TCX (`.tcx`) / FIT (`.fit`)**（API 原始下载为包含 `.fit` 的 ZIP 包，由应用在本地解压并按需转换；对于无 GPS 轨迹的活动，若选 GPX 则提示或回退使用 TCX/FIT）。
   - **同类型合并下载**：勾选多个 Activity 时，**仅当所有勾选活动的类型完全相同时**才提供「合并」选项；开启合并后按活动开始时间升序拼接数据，**仅生成 1 个合并后的文件**。
   - **保存目录与命名规则**：
     - 界面显示当前保存目录（默认 `~/Downloads`，可点击更改并记住），点击下载直接平铺保存至该目录：
     - 单文件：`{YYYY-MM-DD}_{类型}_{名称}_{activityId}.{ext}`
     - 合并文件：`{最早日期}_至_{最晚日期}_{类型}_合并({N}条).{ext}`

---

## 文档导航

- [PRD.md](file:///Users/aubreyhan/Tools/git_repo/PersonalDev/DevDoc/garmin-activity-exporter/PRD.md)：第一版产品需求规格与验收标准。
- [ARCHITECTURE.md](file:///Users/aubreyhan/Tools/git_repo/PersonalDev/DevDoc/garmin-activity-exporter/ARCHITECTURE.md)：macOS Swift + SwiftUI 架构设计、Garmin Personal API 交互时序与 FIT 解析合并设计。
- [SPEC.md](file:///Users/aubreyhan/Tools/git_repo/PersonalDev/DevDoc/garmin-activity-exporter/SPEC.md)：Garmin Personal API 端点定义、数据模型、FIT/GPX/TCX 转换与合并规则、文件命名规范。
- [IMPLEMENTATION_PLAN.md](file:///Users/aubreyhan/Tools/git_repo/PersonalDev/DevDoc/garmin-activity-exporter/IMPLEMENTATION_PLAN.md)：分阶段原子化实施路线图。
