#!/usr/bin/env python3
"""
TrailMov Automated 3D Video Exporter
Runs headless Google Chrome with GPU acceleration to record pixel-perfect 60fps
3D terrain animations with HUD telemetry, transcoding to broadcast-grade MP4s.
"""

import os
import sys
import time
import argparse
import subprocess
import shutil
from http.server import HTTPServer
import threading

import server

CHROME_PATHS = [
    "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
    "/Applications/Chromium.app/Contents/MacOS/Chromium",
    shutil.which("google-chrome"),
    shutil.which("chromium")
]


def find_chrome():
    for p in CHROME_PATHS:
        if p and os.path.isfile(p) and os.access(p, os.X_OK):
            return p
    return None


def find_ffmpeg():
    for p in [shutil.which("ffmpeg"), "/opt/homebrew/bin/ffmpeg", "/usr/local/bin/ffmpeg", "/usr/bin/ffmpeg"]:
        if p and os.path.isfile(p) and os.access(p, os.X_OK):
            return p
    return None


def run_headless_recording(chrome_bin, ffmpeg_bin, aspect_name, width, height, duration, avatar, camera, port):
    raw_webm = f"output/trailmov_raw_{aspect_name}.webm"
    final_mp4 = f"output/trailmov_aosen_{aspect_name.replace('-', 'x')}.mp4"

    if os.path.exists(raw_webm):
        os.remove(raw_webm)

    url = f"http://127.0.0.1:{port}/?export=true&aspect={aspect_name}&duration={duration}&avatar={avatar}&camera={camera}"

    print(f"\n[*] Launching Headless Chrome for {aspect_name} ({width}x{height})...")
    chrome_cmd = [
        chrome_bin,
        "--headless=new",
        "--disable-gpu-sandbox",
        "--use-gl=angle",
        "--use-angle=metal",
        "--hide-scrollbars",
        "--autoplay-policy=no-user-gesture-required",
        f"--window-size={width},{height}",
        url
    ]

    proc = subprocess.Popen(chrome_cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    # Wait for recording to complete and upload
    max_wait = int(duration + 15)
    start_t = time.time()
    uploaded = False

    while time.time() - start_t < max_wait:
        if os.path.exists(raw_webm) and os.path.getsize(raw_webm) > 10000:
            # File exists, wait 1 second for stream flush
            time.sleep(1.5)
            uploaded = True
            break
        elapsed = int(time.time() - start_t)
        print(f"    Recording progress: {elapsed}s / {duration}s...", end="\r", flush=True)
        time.sleep(1.0)

    try:
        proc.terminate()
        proc.wait(timeout=2)
    except Exception:
        proc.kill()

    print()
    if not uploaded or not os.path.exists(raw_webm):
        print(f"[!] Warning: Recording timeout for {aspect_name}")
        return None

    print(f"[*] Transcoding {raw_webm} to high-definition MP4 via FFmpeg...")
    transcode_cmd = [
        ffmpeg_bin,
        "-y",
        "-i", raw_webm,
        "-vf", "scale=trunc(iw/2)*2:trunc(ih/2)*2",
        "-c:v", "libx264",
        "-preset", "medium",
        "-crf", "18",
        "-pix_fmt", "yuv420p",
        final_mp4
    ]

    res = subprocess.run(transcode_cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if res.returncode == 0 and os.path.exists(final_mp4):
        size_mb = round(os.path.getsize(final_mp4) / (1024 * 1024), 2)
        print(f"[✓] Successfully exported: {final_mp4} ({size_mb} MB)")
        try:
            os.remove(raw_webm)
        except Exception:
            pass
        return final_mp4
    else:
        print(f"[!] Error: FFmpeg transcoding failed for {final_mp4}")
        return None


def main():
    parser = argparse.ArgumentParser(description="Export TrailMov 3D Videos")
    parser.add_argument("--aspect", choices=["both", "9:16", "16:9"], default="both")
    parser.add_argument("--duration", type=float, default=36.0, help="Duration in seconds [default: 36.0]")
    parser.add_argument("--avatar", default="cyclist", choices=["cyclist", "car", "runner", "hiker", "dot"])
    parser.add_argument("--camera", default="chase", choices=["chase", "fixed", "orbit", "overview"], help="Camera mode [default: chase]")
    parser.add_argument("--port", type=int, default=8096, help="Internal port [default: 8096]")
    args = parser.parse_args()

    chrome_bin = find_chrome()
    if not chrome_bin:
        print("[!] Error: Google Chrome not found! Please ensure Chrome is installed.", file=sys.stderr)
        sys.exit(1)

    ffmpeg_bin = find_ffmpeg()
    if not ffmpeg_bin:
        print("[!] Error: FFmpeg not found! Please install FFmpeg.", file=sys.stderr)
        sys.exit(1)

    os.makedirs("output", exist_ok=True)

    # Start internal server in background thread
    server_address = ("127.0.0.1", args.port)
    httpd = HTTPServer(server_address, lambda *a, **k: server.TrailmovServer(*a, directory="web", **k))
    srv_thread = threading.Thread(target=httpd.serve_forever, daemon=True)
    srv_thread.start()
    print(f"[*] Local TrailMov service active at http://127.0.0.1:{args.port}/")

    tasks = []
    if args.aspect in ("both", "9:16"):
        tasks.append(("9-16", 720, 1280))
    if args.aspect in ("both", "16:9"):
        tasks.append(("16-9", 1920, 1080))

    exported_files = []
    for aspect_name, w, h in tasks:
        out = run_headless_recording(
            chrome_bin, ffmpeg_bin,
            aspect_name, w, h,
            args.duration, args.avatar, args.camera, args.port
        )
        if out:
            exported_files.append(out)

    httpd.shutdown()

    print("\n" + "="*50)
    print("🎉 TrailMov 风格 3D 轨迹视频生成完成！")
    print("输出文件:")
    for f in exported_files:
        sz = round(os.path.getsize(f) / (1024*1024), 2)
        print(f"  - {f} ({sz} MB)")
    print("="*50)
    print("\n[安全提示] 原 GPX 文件依然安全保存在 GPX/ 目录。")
    print("在您检查视频合格后，只需回复'合格'或执行确认，即可安全删除原文件。\n")


if __name__ == "__main__":
    main()
