---
name: 3d-track-animation
description: >-
  Generate 3D GPS trajectory flyover animation videos (both 9:16 vertical 720x1280
  and 16:9 horizontal 1280x720 MP4s) from one or more GPX (.gpx) files. Activate this
  skill when the user uploads or specifies GPX file(s) and issues a command like
  "3D轨迹： 行走｜骑行｜自驾（如没有说明，根据速度判断） 轨迹名称" (or asks to generate a 3D trajectory
  animation from GPX files).
---

# 3D轨迹动画生成技能 (3D GPX Trajectory Animation Skill)

当用户上传或指定一个或多个 `.gpx` 轨迹文件，并发出如下格式的指令时自动触发本技能，生成 **16:9 横屏 (`1280x720`)** 与 **9:16 竖屏 (`720x1280`)** 两种分辨率的 3D 轨迹巡航动画（无任何第三方水印）：

1. **上传或指定 GPX 文件**（支持单个或多个 `.gpx` 文件；如有多个 `.gpx` 文件，脚本会自动按时间顺序合并为一条完整轨迹）。
2. **发出指令格式**：
   `3D轨迹： [行走|骑行|自驾] <轨迹名称>`
   - 示例 1（指定运动类型）：`3D轨迹： 自驾 武汉回京`
   - 示例 2（指定运动类型）：`3D轨迹： 骑行 环青海湖`
   - 示例 3（指定运动类型）：`3D轨迹： 行走 20261101奥森`
   - 示例 4（未说明运动类型，自动根据轨迹平均速度判断）：`3D轨迹： 20261101奥森`
     - 平均速度 `< 12 km/h` -> 自动识别为 **行走/跑步**（使用人形立牌图标 [person_icon.png](./resources/person_icon.png)）
     - 平均速度 `12 ~ 35 km/h` -> 自动识别为 **骑行**（使用自行车图标 [bike_icon.png](./resources/bike_icon.png)）
     - 平均速度 `> 35 km/h` -> 自动识别为 **自驾**（使用小汽车图标 [car_icon.png](./resources/car_icon.png)）
3. **自动输出双分辨率 3D 轨迹动画**：
   - `<输出前缀>_16x9.mp4` (`1280x720`, 30fps)
   - `<输出前缀>_9x16.mp4` (`720x1280`, 30fps)

---

## 核心视觉与动画特性

- **3D 立体地形与路网地图**：
  - 采用 Esri `World_Imagery` 高清卫星影像底图，对中国境内骑行/自驾/长途路线自动叠加高德透明道路/高速编号/地名图层（自动进行 `WGS-84 -> GCJ-02` 坐标对齐）。
  - 两侧山丘立体起伏明显，路线走廊纵向坡度平滑自然（25 点高斯平滑 + 26° 俯仰跟随视角），行进中具有自然的上下坡立体感。
- **平滑速度控制与运镜**：
  - **片头总览 (0~4s)**：展示完整行程轨迹与顶部标题横幅（`<轨迹名称>  <总里程>KM  <累计爬升>m`）。
  - **俯冲切入 (4~6.5s)**：平滑拉近至起点。
  - **3D 巡航跟随 (6.5~51.5s)**：采用 C1 连续的速度控制曲线（7% 平滑加速 + 86% 匀速巡航 + 7% 平滑减速），全程无停顿，动态渲染荧光绿轨迹 (`#B8FF28`)、地面投影及移动图标。
  - **HUD 仪表盘**：左上角无水印黑白卫星小地图（含红点起点与荧光绿实时位置）、`Distance` 距离卡片、左下角 `Ascent` / `Descent` 爬升下降卡片、底部动态海拔剖面图（若 GPX 含心率数据则自动在左上角展示 `Average Heart Rate` 卡片）。
  - **片尾拉升全览 (51.5~56.5s)**：视角从终点平滑拉升回全景高度，完整展示整个行程轨迹及顶部标题统计信息。

---

## 标准执行步骤 (Step-by-Step Procedure)

### 第 1 步：解析用户指令与 GPX 文件路径

1. **收集 GPX 文件**：
   - 检查用户在当前对话中上传的 `.gpx` 附件（位于 `<appDataDir>/brain/<conversation-id>/.user_uploaded/*.gpx`）或用户在指令中指定的本地 `.gpx` 文件路径。
   - 若有多个 `.gpx` 文件，全部作为参数传给 `--gpx`，由 [prepare_and_download.py](./scripts/prepare_and_download.py) 自动按时间戳排序合并。
2. **解析运动类型与轨迹名称**：
   - 从 `3D轨迹： ...` 指令中提取可选的运动方式（`行走`/`跑步` -> `行走`，`骑行`/`骑车` -> `骑行`，`自驾`/`开车` -> `自驾`；若未提供则设为 `auto`）以及 `<轨迹名称>`。

### 第 2 步：运行轨迹合并与地图瓦片下载

使用 [prepare_and_download.py](./scripts/prepare_and_download.py) 解析合并 GPX 文件、计算最佳缩放级别 `z` 并下载卫星与路网瓦片至 `<scratchDir>/track_work`：

```bash
python3 /Users/aubreyhan/Tools/git_repo/PersonalDev/EPS32/.agents/skills/3d-track-animation/scripts/prepare_and_download.py \
  --gpx <file1.gpx> [file2.gpx ...] \
  --name "<轨迹名称>" \
  --mode "<行走|骑行|自驾|auto>" \
  --work-dir "<scratchDir>/track_work"
```

> [!NOTE]
> 先在标准沙盒模式 (`BypassSandbox: false`) 下运行；若因沙盒无网络权限导致地图瓦片下载数量为 0，则仅对该瓦片下载命令使用 `BypassSandbox: true` 重新执行。

### 第 3 步：编译并渲染 16:9 与 9:16 双分辨率 3D 轨迹视频

在标准沙盒模式 (`BypassSandbox: false`) 下运行 [run_3d_track.sh](./scripts/run_3d_track.sh)，自动编译 [render_3d_track.mm](./scripts/render_3d_track.mm) 并输出 `16:9` 与 `9:16` 两个 MP4 视频到工作区目录 `/Users/aubreyhan/Tools/git_repo/PersonalDev/EPS32/`：

```bash
zsh /Users/aubreyhan/Tools/git_repo/PersonalDev/EPS32/.agents/skills/3d-track-animation/scripts/run_3d_track.sh \
  "<scratchDir>/track_work/track_data.json" \
  "/Users/aubreyhan/Tools/git_repo/PersonalDev/EPS32/<输出文件名前缀>"
```

- **进度汇报规则**：若渲染或下载任务转入后台异步执行且预计耗时超过 2 分钟，使用 `schedule` 工具设置 `DurationSeconds: 120` 定时器，每 2 分钟向用户汇报一次当前渲染进度，直至两个分辨率视频全部生成完毕。

### 第 4 步：校验输出并展示结果

1. 确认 `<输出文件名前缀>_16x9.mp4` 与 `<输出文件名前缀>_9x16.mp4` 均已生成且文件大小正常。
2. 如需生成预览关键帧，可执行：
   ```bash
   /Users/aubreyhan/Tools/git_repo/PersonalDev/EPS32/.agents/skills/3d-track-animation/scripts/bin/render_3d_track \
     --json "<scratchDir>/track_work/track_data.json" \
     --res-dir "/Users/aubreyhan/Tools/git_repo/PersonalDev/EPS32/.agents/skills/3d-track-animation/resources" \
     --preview "<scratchDir>" --16x9
   ```
3. 向用户返回生成的 `16:9` 和 `9:16` 视频文件链接、自动识别或指定的运动模式、合并后的总里程与累计爬升数据。
