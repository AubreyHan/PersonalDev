---
name: 3d-track-animation
description: Generate 3D GPS trajectory flyover animation videos (both 9:16 vertical 720x1280 and 16:9 horizontal 1280x720 MP4s) from one or more GPX (.gpx) files, compressed archives (.zip, .tar.gz, .tgz, .tar.bz2, .tar.xz, .gz, .bz2, .xz, .7z, .rar), or the local ./GPX subdirectory across macOS and Linux. Activate this skill when the user issues a command like "3D轨迹： 行走｜骑行｜自驾（如没有说明，根据速度判断） 轨迹名称" (or asks to generate a 3D trajectory animation).
---

# 3D Track Flyover Animation Skill (`3d-track-animation`)

Generate smooth 60fps 3D satellite-terrain GPS flyover videos in both **16:9 horizontal (`1280x720`)** and **9:16 vertical (`720x1280`)** formats, compatible with both **macOS (`Darwin`)** and **Linux (`Linux`)**.

---

## 1. Command Trigger & Parsing Rules

### User Command Format
```text
3D轨迹： [行走|骑行|自驾] [轨迹名称]
```
Examples:
- `3D轨迹： 骑行 20261101奥森` → Mode = `骑行`, Title = `20261101奥森`
- `3D轨迹： 自驾 武汉到北京` → Mode = `自驾`, Title = `武汉到北京`
- `3D轨迹： 2026暑期西北自驾` → Mode = `auto` (inferred from average speed), Title = `2026暑期西北自驾`
- `3D轨迹：` → Mode = `auto`, Title = extracted from `<name>` tag in GPX

### Input GPX File & `./GPX` Subdirectory Auto-Discovery
1. **Uploaded or Specified Files / Archives**:
   - Supports **1 or multiple `.gpx` files** and **compressed archives** (`.zip`, `.tar.gz`, `.tgz`, `.tar.bz2`, `.tar.xz`, `.gz`, `.bz2`, `.xz`, `.7z`, `.rar`).
   - Note that Antigravity may save uploaded `.zip` or `.gz` files in `.user_uploaded/` with a `.gpx` extension or generic name; `prepare_and_download.py` inspects magic bytes automatically and unpacks them recursively.
2. **Automatic Fallback to `./GPX` Subdirectory (with Auto-Cleanup)**:
   - If the user issues a `3D轨迹：...` command **without uploading or specifying any GPX/archive files**, omit `--gpx` (or pass `--gpx-dir "<workspace_dir>/GPX"`).
   - `prepare_and_download.py` will automatically scan `<workspace_dir>/GPX` for all `.gpx` files and compressed archives.
   - Once both `16:9` and `9:16` MP4 videos have been generated and verified in `./output/`, `run_3d_track.sh` **automatically clears all files inside `<workspace_dir>/GPX`**.

### Output Subdirectory (`./output`)
- All generated MP4 videos MUST be saved into the `./output` subdirectory of the workspace (`<workspace_dir>/output/`), creating `./output` automatically if it does not exist:
  - `<workspace_dir>/output/<轨迹名称>_16x9.mp4` (`1280x720`, 60fps)
  - `<workspace_dir>/output/<轨迹名称>_9x16.mp4` (`720x1280`, 60fps)

### Progress Reporting Every 2 Minutes
- Whenever a task (such as downloading hundreds of map tiles or rendering long trajectories) runs in the background or takes longer than 2 minutes, use the `schedule` tool (`DurationSeconds: 120`, `TimerCondition: "any"`) to report concrete progress (e.g., tile download percentage or video rendering stage) to the user every 2 minutes until completion.

---

## 2. Step-by-Step Execution Workflow

### Step 1: Prepare GPX Data & Download/Verify Cached Map Tiles

Run `scripts/prepare_and_download.py`.
- **Case A: User uploaded or specified file(s)**: pass them via `--gpx "<file1>" "<file2>" ...`
- **Case B: User did NOT upload or specify file(s)**: omit `--gpx` and pass `--gpx-dir "<workspace_dir>/GPX"` so it automatically scans `./GPX` and marks it for post-render cleanup.

```bash
python3 "<skill_dir>/scripts/prepare_and_download.py" \
  --gpx-dir "./GPX" \
  --name "<轨迹名称>" \
  --mode "<行走|骑行|自驾|auto>" \
  --work-dir "<scratch_or_tmp_dir>/track_work"
```
*(Note: `<skill_dir>` is the directory containing this `SKILL.md`. If the user uploaded files, add `--gpx "<path1>" "<path2>"` to the command above.)*

`prepare_and_download.py` automatically:
1. Unpacks any compressed archives or scans `./GPX` (relative to the current working directory, or the path passed to `--gpx-dir`) if `--gpx` is omitted.
2. Parses all GPX files, sorts them chronologically, merges points, and smooths elevation.
3. Determines the activity mode (`行走` / `骑行` / `自驾`) and selects optimal zoom level `z` (`8..17`).
4. Uses the persistent local cache directory (`<skill_dir>/cache/`) and verifies whether cached map tiles (`ETag` / `Last-Modified` / `SHA-256`) have remote updates.

### Step 2: Compile & Render Both 16:9 and 9:16 Videos into `./output`

Run `<skill_dir>/scripts/run_3d_track.sh`:

```bash
"<skill_dir>/scripts/run_3d_track.sh" \
  "<scratch_or_tmp_dir>/track_work/track_data.json" \
  "./output/<轨迹名称>"
```

What `run_3d_track.sh` does:
1. Detects the OS (`Darwin` vs `Linux`):
   - **macOS (`Darwin`)**: Compiles `scripts/render_3d_track.mm` (if updated) and renders via native `CoreGraphics` + `AVFoundation`.
   - **Linux (`Linux`)**: Compiles `scripts/render_3d_track_linux.cpp` (if updated), runs `scripts/prepare_linux_assets.py`, and renders via multi-threaded C++17 software rasterization piped into `ffmpeg`.
2. Creates `<workspace_dir>/output/` automatically if it does not exist, and outputs:
   - `<workspace_dir>/output/<轨迹名称>_16x9.mp4` (`1280x720`, 60fps)
   - `<workspace_dir>/output/<轨迹名称>_9x16.mp4` (`720x1280`, 60fps)
3. If the GPX files were loaded from `<workspace_dir>/GPX`, automatically empties `<workspace_dir>/GPX` once both videos are verified non-empty.

### Step 3: Verify & Present Results

Verify that both `<workspace_dir>/output/<轨迹名称>_16x9.mp4` and `<workspace_dir>/output/<轨迹名称>_9x16.mp4` exist and are non-empty, then report the summary metrics and clickable `file://` links to the user.
