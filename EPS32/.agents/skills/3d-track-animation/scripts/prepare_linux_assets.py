#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Pre-process map tiles, road overlays, activity icon, and Chinese/English title banners
into raw RGBA buffers for the Linux C++ 3D renderer (render_3d_track_linux.cpp).
"""

import argparse
import json
import os
import struct
import subprocess
import sys


def write_rgba_file(path, width, height, rgba_bytes):
    with open(path, 'wb') as f:
        f.write(struct.pack('<II', int(width), int(height)))
        f.write(rgba_bytes)


def find_cjk_font(size):
    from PIL import ImageFont
    candidates = [
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Bold.ttc",
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/noto-cjk/NotoSansCJK-Bold.ttc",
        "/usr/share/fonts/truetype/noto/NotoSansCJK-Bold.ttc",
        "/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc",
        "/usr/share/fonts/truetype/wqy/wqy-microhei.ttc",
        "/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf",
        "/usr/share/fonts/truetype/arphic/uming.ttc",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
        "/System/Library/Fonts/PingFang.ttc",
        "/System/Library/Fonts/STHeiti Medium.ttc",
    ]
    for p in candidates:
        if os.path.exists(p):
            try:
                return ImageFont.truetype(p, size)
            except Exception:
                pass
    # Search /usr/share/fonts recursively for any CJK or Noto/WQY font
    if os.path.isdir("/usr/share/fonts"):
        for root, _, files in os.walk("/usr/share/fonts"):
            for fn in files:
                low = fn.lower()
                if any(k in low for k in ("cjk", "wqy", "zenhei", "microhei", "droidsansfallback", "noto")):
                    fp = os.path.join(root, fn)
                    try:
                        return ImageFont.truetype(fp, size)
                    except Exception:
                        pass
    return ImageFont.load_default()


def prepare_with_pil(td, res_dir, out_dir):
    from PIL import Image, ImageDraw, ImageEnhance

    z = int(td['z'])
    x_min, x_max = int(td['x_min']), int(td['x_max'])
    y_min, y_max = int(td['y_min']), int(td['y_max'])
    cols = x_max - x_min + 1
    rows = y_max - y_min + 1
    mw, mh = cols * 256, rows * 256

    sat_dir = td['sat_tiles_dir']
    road_dir = td.get('road_tiles_dir', '')
    use_road = bool(td.get('use_road_overlay', False))

    mosaic = Image.new('RGBA', (mw, mh), (20, 26, 33, 255))
    for ty in range(y_min, y_max + 1):
        for tx in range(x_min, x_max + 1):
            p = os.path.join(sat_dir, f"{z}_{tx}_{ty}.jpg")
            if os.path.exists(p):
                try:
                    im = Image.open(p).convert('RGBA')
                    if im.size != (256, 256):
                        im = im.resize((256, 256), Image.Resampling.BILINEAR)
                    mosaic.paste(im, ((tx - x_min) * 256, (ty - y_min) * 256))
                except Exception:
                    pass

    # Save grayscale darkened minimap mosaic BEFORE compositing road labels
    gray = mosaic.convert('L')
    gray_rgb = Image.merge('RGBA', (
        gray.point(lambda v: int(v * 0.62)),
        gray.point(lambda v: int(v * 0.64)),
        gray.point(lambda v: int(v * 0.66)),
        Image.new('L', (mw, mh), 255)
    ))
    write_rgba_file(os.path.join(out_dir, "mosaic_gray.rgba"), mw, mh, gray_rgb.tobytes())

    # Composite road overlay tiles
    if use_road and road_dir and os.path.isdir(road_dir):
        for ty in range(y_min, y_max + 1):
            for tx in range(x_min, x_max + 1):
                rp = os.path.join(road_dir, f"{z}_{tx}_{ty}.png")
                if os.path.exists(rp):
                    try:
                        rim = Image.open(rp).convert('RGBA')
                        if rim.size != (256, 256):
                            rim = rim.resize((256, 256), Image.Resampling.BILINEAR)
                        # Scale alpha by 0.92
                        r, g, b, a = rim.split()
                        a = a.point(lambda v: int(v * 0.92))
                        rim = Image.merge('RGBA', (r, g, b, a))
                        mosaic.alpha_composite(rim, ((tx - x_min) * 256, (ty - y_min) * 256))
                    except Exception:
                        pass

    write_rgba_file(os.path.join(out_dir, "mosaic_color.rgba"), mw, mh, mosaic.tobytes())

    # Load activity icon
    icon_mode = int(td.get('icon_mode', 0))
    icon_name = "car_icon.png" if icon_mode == 2 else ("bike_icon.png" if icon_mode == 1 else "person_icon.png")
    icon_path = os.path.join(res_dir, icon_name)
    if os.path.exists(icon_path):
        ic = Image.open(icon_path).convert('RGBA')
        write_rgba_file(os.path.join(out_dir, "icon.rgba"), ic.width, ic.height, ic.tobytes())

    # Pre-render overview title banner for 16x9 (1280x110) and 9x16 (720x110)
    title_str = f"{td.get('name', '轨迹')}  {float(td.get('total_dist', 0))/1000.0:.2f}KM  {float(td.get('total_asc', 0)):.0f}m"
    for tag, bw, fsize in [("16x9", 1280, 38), ("9x16", 720, 28)]:
        bh = 110
        banner = Image.new('RGBA', (bw, bh), (0, 0, 0, 0))
        draw = ImageDraw.Draw(banner)
        font = find_cjk_font(fsize)
        bbox = draw.textbbox((0, 0), title_str, font=font)
        tw = bbox[2] - bbox[0]
        th = bbox[3] - bbox[1]
        tx = max(12, (bw - tw) // 2)
        ty = (bh - th) // 2
        # Soft dark pill background
        pad_x, pad_y = 24, 12
        draw.rounded_rectangle(
            [tx - pad_x, ty - pad_y, tx + tw + pad_x, ty + th + pad_y],
            radius=18,
            fill=(10, 15, 22, 168)
        )
        # Drop shadow + white text
        draw.text((tx + 2, ty + 2), title_str, font=font, fill=(0, 0, 0, 210))
        draw.text((tx, ty), title_str, font=font, fill=(255, 255, 255, 255))
        write_rgba_file(os.path.join(out_dir, f"banner_{tag}.rgba"), bw, bh, banner.tobytes())


def decode_image_with_ffmpeg(path, target_w=0, target_h=0):
    vf = [f"scale={target_w}:{target_h}"] if (target_w > 0 and target_h > 0) else []
    if not vf:
        # Query dimensions via ffprobe
        pr = subprocess.run(
            ["ffprobe", "-v", "error", "-select_streams", "v:0",
             "-show_entries", "stream=width,height", "-of", "csv=s=x:p=0", path],
            capture_output=True, text=True
        )
        if pr.returncode != 0 or 'x' not in pr.stdout:
            return 0, 0, b''
        parts = pr.stdout.strip().split('x')
        target_w, target_h = int(parts[0]), int(parts[1])
    cmd = ["ffmpeg", "-v", "error", "-i", path]
    if vf:
        cmd += ["-vf", ",".join(vf)]
    cmd += ["-f", "rawvideo", "-pix_fmt", "rgba", "-"]
    res = subprocess.run(cmd, capture_output=True)
    if res.returncode == 0 and len(res.stdout) == target_w * target_h * 4:
        return target_w, target_h, res.stdout
    return 0, 0, b''


def prepare_with_ffmpeg_fallback(td, res_dir, out_dir):
    z = int(td['z'])
    x_min, x_max = int(td['x_min']), int(td['x_max'])
    y_min, y_max = int(td['y_min']), int(td['y_max'])
    cols = x_max - x_min + 1
    rows = y_max - y_min + 1
    mw, mh = cols * 256, rows * 256

    sat_dir = td['sat_tiles_dir']
    road_dir = td.get('road_tiles_dir', '')
    use_road = bool(td.get('use_road_overlay', False))

    mosaic = bytearray(mw * mh * 4)
    # Initialize dark slate background
    mosaic[0::4] = bytes([20]) * (mw * mh)
    mosaic[1::4] = bytes([26]) * (mw * mh)
    mosaic[2::4] = bytes([33]) * (mw * mh)
    mosaic[3::4] = bytes([255]) * (mw * mh)

    for ty in range(y_min, y_max + 1):
        for tx in range(x_min, x_max + 1):
            p = os.path.join(sat_dir, f"{z}_{tx}_{ty}.jpg")
            if os.path.exists(p):
                _, _, buf = decode_image_with_ffmpeg(p, 256, 256)
                if buf:
                    ox = (tx - x_min) * 256
                    oy = (ty - y_min) * 256
                    for r in range(256):
                        dst_off = ((oy + r) * mw + ox) * 4
                        src_off = r * 256 * 4
                        mosaic[dst_off:dst_off + 1024] = buf[src_off:src_off + 1024]

    # Build grayscale mosaic
    gray = bytearray(mw * mh * 4)
    for i in range(0, mw * mh * 4, 4):
        lum = (mosaic[i] * 77 + mosaic[i + 1] * 150 + mosaic[i + 2] * 29) >> 8
        gray[i] = int(lum * 0.62)
        gray[i + 1] = int(lum * 0.64)
        gray[i + 2] = int(lum * 0.66)
        gray[i + 3] = 255
    write_rgba_file(os.path.join(out_dir, "mosaic_gray.rgba"), mw, mh, gray)

    if use_road and road_dir and os.path.isdir(road_dir):
        for ty in range(y_min, y_max + 1):
            for tx in range(x_min, x_max + 1):
                rp = os.path.join(road_dir, f"{z}_{tx}_{ty}.png")
                if os.path.exists(rp):
                    _, _, buf = decode_image_with_ffmpeg(rp, 256, 256)
                    if buf:
                        ox = (tx - x_min) * 256
                        oy = (ty - y_min) * 256
                        for r in range(256):
                            dst_row = (oy + r) * mw + ox
                            src_row = r * 256
                            for c in range(256):
                                si = (src_row + c) * 4
                                sa = int(buf[si + 3] * 0.92)
                                if sa > 0:
                                    di = (dst_row + c) * 4
                                    inv = 255 - sa
                                    mosaic[di] = (buf[si] * sa + mosaic[di] * inv) // 255
                                    mosaic[di + 1] = (buf[si + 1] * sa + mosaic[di + 1] * inv) // 255
                                    mosaic[di + 2] = (buf[si + 2] * sa + mosaic[di + 2] * inv) // 255

    write_rgba_file(os.path.join(out_dir, "mosaic_color.rgba"), mw, mh, mosaic)

    icon_mode = int(td.get('icon_mode', 0))
    icon_name = "car_icon.png" if icon_mode == 2 else ("bike_icon.png" if icon_mode == 1 else "person_icon.png")
    icon_path = os.path.join(res_dir, icon_name)
    if os.path.exists(icon_path):
        iw, ih, ibuf = decode_image_with_ffmpeg(icon_path)
        if ibuf:
            write_rgba_file(os.path.join(out_dir, "icon.rgba"), iw, ih, ibuf)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--json", required=True)
    parser.add_argument("--res-dir", required=True)
    parser.add_argument("--out-dir", required=True)
    args = parser.parse_args()

    os.makedirs(args.out_dir, exist_ok=True)
    with open(args.json, 'r', encoding='utf-8') as f:
        td = json.load(f)

    try:
        import PIL  # noqa: F401
        has_pil = True
    except ImportError:
        has_pil = False

    if has_pil:
        prepare_with_pil(td, args.res_dir, args.out_dir)
    else:
        prepare_with_ffmpeg_fallback(td, args.res_dir, args.out_dir)
    print(f"Prepared Linux RGBA assets in {args.out_dir}")


if __name__ == "__main__":
    main()
