#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Prepare and merge one or more GPX files, detect activity mode (walk/run, bike, drive),
calculate optimal 3D map tile zoom & bounds, and download satellite + road overlay tiles.
"""

import argparse
import bz2
import concurrent.futures
import gzip
import hashlib
import json
import lzma
import math
import os
import re
import shutil
import ssl
import subprocess
import sys
import tarfile
import threading
import time
import urllib.error
import urllib.request
import xml.etree.ElementTree as ET
import zipfile
from datetime import datetime


def haversine(lat1, lon1, lat2, lon2):
    R = 6371000.0
    phi1, phi2 = math.radians(lat1), math.radians(lat2)
    dphi = math.radians(lat2 - lat1)
    dlambda = math.radians(lon2 - lon1)
    a = math.sin(dphi / 2.0) ** 2 + math.cos(phi1) * math.cos(phi2) * math.sin(dlambda / 2.0) ** 2
    return 2.0 * R * math.atan2(math.sqrt(a), math.sqrt(max(0.0, 1.0 - a)))


def transform_lat(x, y):
    ret = -100.0 + 2.0 * x + 3.0 * y + 0.2 * y * y + 0.1 * x * y + 0.2 * math.sqrt(abs(x))
    ret += (20.0 * math.sin(6.0 * x * math.pi) + 20.0 * math.sin(2.0 * x * math.pi)) * 2.0 / 3.0
    ret += (20.0 * math.sin(y * math.pi) + 40.0 * math.sin(y / 3.0 * math.pi)) * 2.0 / 3.0
    ret += (160.0 * math.sin(y / 12.0 * math.pi) + 320.0 * math.sin(y * math.pi / 30.0)) * 2.0 / 3.0
    return ret


def transform_lon(x, y):
    ret = 300.0 + x + 2.0 * y + 0.1 * x * x + 0.1 * x * y + 0.1 * math.sqrt(abs(x))
    ret += (20.0 * math.sin(6.0 * x * math.pi) + 20.0 * math.sin(2.0 * x * math.pi)) * 2.0 / 3.0
    ret += (20.0 * math.sin(x * math.pi) + 40.0 * math.sin(x / 3.0 * math.pi)) * 2.0 / 3.0
    ret += (150.0 * math.sin(x / 12.0 * math.pi) + 300.0 * math.sin(x / 30.0 * math.pi)) * 2.0 / 3.0
    return ret


def is_in_china(lat, lon):
    return 18.0 <= lat <= 54.0 and 73.0 <= lon <= 135.5


def wgs84_to_gcj02(wg_lat, wg_lon):
    if not is_in_china(wg_lat, wg_lon):
        return wg_lat, wg_lon
    a = 6378245.0
    ee = 0.00669342162296594323
    d_lat = transform_lat(wg_lon - 105.0, wg_lat - 35.0)
    d_lon = transform_lon(wg_lon - 105.0, wg_lat - 35.0)
    rad_lat = wg_lat / 180.0 * math.pi
    magic = math.sin(rad_lat)
    magic = 1.0 - ee * magic * magic
    sqrt_magic = math.sqrt(magic)
    d_lat = (d_lat * 180.0) / ((a * (1.0 - ee)) / (magic * sqrt_magic) * math.pi)
    d_lon = (d_lon * 180.0) / (a / sqrt_magic * math.cos(rad_lat) * math.pi)
    return wg_lat + d_lat, wg_lon + d_lon


def latlon_to_tile_frac(lat, lon, z):
    n = float(1 << z)
    tx = (lon + 180.0) / 360.0 * n
    lat_rad = math.radians(lat)
    ty = (1.0 - math.asinh(math.tan(lat_rad)) / math.pi) / 2.0 * n
    return tx, ty


def parse_single_gpx(gpx_path):
    tree = ET.parse(gpx_path)
    root = tree.getroot()

    track_name = None
    for el in root.iter():
        tag = el.tag.split('}')[-1] if '}' in el.tag else el.tag
        if tag == 'name' and el.text and el.text.strip():
            track_name = el.text.strip()
            break

    pts = []
    for pt in root.iter():
        tag = pt.tag.split('}')[-1] if '}' in pt.tag else pt.tag
        if tag not in ('trkpt', 'rtept'):
            continue
        if 'lat' not in pt.attrib or 'lon' not in pt.attrib:
            continue
        lat = float(pt.attrib['lat'])
        lon = float(pt.attrib['lon'])
        ele = 0.0
        dt_val = None
        hr_val = 0
        for child in pt.iter():
            ctag = child.tag.split('}')[-1] if '}' in child.tag else child.tag
            if ctag == 'ele' and child.text:
                try:
                    ele = float(child.text.strip())
                except ValueError:
                    pass
            elif ctag == 'time' and child.text:
                txt = child.text.strip().replace('Z', '+00:00')
                try:
                    dt_val = datetime.fromisoformat(txt)
                except ValueError:
                    pass
            elif ctag == 'hr' and child.text:
                try:
                    hr_val = int(float(child.text.strip()))
                except ValueError:
                    pass
        pts.append({
            'lat': lat,
            'lon': lon,
            'ele': ele,
            'dt': dt_val,
            'hr': hr_val,
        })

    first_dt = next((p['dt'] for p in pts if p['dt'] is not None), None)
    return track_name, first_dt, pts


def resolve_icon_mode(mode_str, avg_speed_kmh):
    if mode_str:
        m = mode_str.strip().lower()
        if m in ('0', 'person', 'walk', 'run', 'running', 'walking', 'hiking', '行走', '跑步', '徒步', '越野'):
            return 0, '行走/跑步'
        if m in ('1', 'bike', 'bicycle', 'cycling', 'ride', '骑行', '骑车', '单车', '自行车'):
            return 1, '骑行'
        if m in ('2', 'car', 'drive', 'driving', 'auto_drive', '自驾', '开车', '驾车', '汽车'):
            return 2, '自驾'
    # Auto-detect by average speed (km/h)
    if avg_speed_kmh < 12.0:
        return 0, f'行走/跑步 (自动识别: 均速 {avg_speed_kmh:.1f} km/h)'
    elif avg_speed_kmh <= 35.0:
        return 1, f'骑行 (自动识别: 均速 {avg_speed_kmh:.1f} km/h)'
    else:
        return 2, f'自驾 (自动识别: 均速 {avg_speed_kmh:.1f} km/h)'


def choose_optimal_zoom(lats, lons, use_gcj02):
    if use_gcj02:
        conv = [wgs84_to_gcj02(la, lo) for la, lo in zip(lats, lons)]
        min_lat = min(c[0] for c in conv)
        max_lat = max(c[0] for c in conv)
        min_lon = min(c[1] for c in conv)
        max_lon = max(c[1] for c in conv)
    else:
        min_lat, max_lat = min(lats), max(lats)
        min_lon, max_lon = min(lons), max(lons)

    lat_pad = max(0.0015, (max_lat - min_lat) * 0.12)
    lon_pad = max(0.0015, (max_lon - min_lon) * 0.22)
    b_min_lat = max(-85.0, min_lat - lat_pad)
    b_max_lat = min(85.0, max_lat + lat_pad)
    b_min_lon = max(-179.9, min_lon - lon_pad)
    b_max_lon = min(179.9, max_lon + lon_pad)

    best_z = 12
    for z in range(16, 5, -1):
        tx0, ty1 = latlon_to_tile_frac(b_min_lat, b_min_lon, z)
        tx1, ty0 = latlon_to_tile_frac(b_max_lat, b_max_lon, z)
        x_min, x_max = int(math.floor(tx0)), int(math.floor(tx1))
        y_min, y_max = int(math.floor(ty0)), int(math.floor(ty1))
        cols = x_max - x_min + 1
        rows = y_max - y_min + 1
        if max(cols, rows) <= 26 and cols * rows <= 400:
            best_z = z
            break

    tx0, ty1 = latlon_to_tile_frac(b_min_lat, b_min_lon, best_z)
    tx1, ty0 = latlon_to_tile_frac(b_max_lat, b_max_lon, best_z)
    x_min = max(0, int(math.floor(tx0)) - 1)
    x_max = min((1 << best_z) - 1, int(math.floor(tx1)) + 1)
    y_min = max(0, int(math.floor(ty0)) - 1)
    y_max = min((1 << best_z) - 1, int(math.floor(ty1)) + 1)
    while (x_max - x_min + 1) < 11:
        x_min = max(0, x_min - 1)
        x_max = min((1 << best_z) - 1, x_max + 1)
    while (y_max - y_min + 1) < 9:
        y_min = max(0, y_min - 1)
        y_max = min((1 << best_z) - 1, y_max + 1)
    return best_z, x_min, x_max, y_min, y_max


def is_valid_tile_file(path):
    if not os.path.exists(path):
        return False
    try:
        sz = os.path.getsize(path)
        if sz <= 100:
            return False
        with open(path, 'rb') as f:
            magic = f.read(8)
        if path.lower().endswith('.jpg'):
            return magic.startswith(b'\xff\xd8')
        if path.lower().endswith('.png'):
            return magic.startswith(b'\x89PNG\r\n\x1a\n')
        return True
    except Exception:
        return False


def file_sha256(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        while True:
            chunk = f.read(65536)
            if not chunk:
                break
            h.update(chunk)
    return h.hexdigest()


def download_tiles(
    z, x_min, x_max, y_min, y_max, sat_dir, road_dir, use_road_overlay,
    cache_dir=None, check_updates=True, max_age_hours=24.0
):
    os.makedirs(sat_dir, exist_ok=True)
    if use_road_overlay:
        os.makedirs(road_dir, exist_ok=True)

    meta_root = cache_dir if cache_dir else os.path.dirname(sat_dir)
    os.makedirs(meta_root, exist_ok=True)
    meta_path = os.path.join(meta_root, "tile_cache_meta.json")
    tile_meta = {}
    if os.path.exists(meta_path):
        try:
            with open(meta_path, 'r', encoding='utf-8') as f:
                tile_meta = json.load(f)
        except Exception:
            tile_meta = {}

    meta_lock = threading.Lock()
    now_ts = time.time()
    max_age_sec = max(0.0, float(max_age_hours) * 3600.0)

    ctx = ssl.create_default_context()
    ctx.check_hostname = False
    ctx.verify_mode = ssl.CERT_NONE

    all_items = []
    for y in range(y_min, y_max + 1):
        for x in range(x_min, x_max + 1):
            sat_out = os.path.join(sat_dir, f"{z}_{x}_{y}.jpg")
            sat_url = f"https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/{z}/{y}/{x}"
            all_items.append((f"sat/{z}_{x}_{y}.jpg", sat_url, sat_out))
            if use_road_overlay and z <= 16:
                road_out = os.path.join(road_dir, f"{z}_{x}_{y}.png")
                srv = ((x + y) % 4) + 1
                road_url = f"https://webst0{srv}.is.autonavi.com/appmaptile?style=8&x={x}&y={y}&z={z}"
                all_items.append((f"road/{z}_{x}_{y}.png", road_url, road_out))

    fresh_cached_count = 0
    work_items = []
    for key, url, out_path in all_items:
        valid_local = is_valid_tile_file(out_path)
        entry = tile_meta.get(key)
        if valid_local:
            # Backfill metadata if tile was migrated from legacy directory without metadata
            if not entry or entry.get('size') != os.path.getsize(out_path) or not entry.get('sha256'):
                entry = {
                    'sha256': file_sha256(out_path),
                    'size': os.path.getsize(out_path),
                    'etag': (entry or {}).get('etag', ''),
                    'last_modified': (entry or {}).get('last_modified', ''),
                    'checked_at': (entry or {}).get('checked_at', now_ts)
                }
                tile_meta[key] = entry
            age = now_ts - float(entry.get('checked_at', 0.0))
            if not check_updates or age < max_age_sec:
                fresh_cached_count += 1
                continue
            work_items.append((key, url, out_path, True, entry))
        else:
            work_items.append((key, url, out_path, False, entry or {}))

    total_needed = len(all_items)
    if not work_items:
        # Save any backfilled metadata
        try:
            tmp_meta = f"{meta_path}.tmp.{os.getpid()}"
            with open(tmp_meta, 'w', encoding='utf-8') as f:
                json.dump(tile_meta, f, ensure_ascii=False)
            os.replace(tmp_meta, meta_path)
        except Exception:
            pass
        print(f"Map tiles 100% hit & verified from local cache: {fresh_cached_count}/{total_needed} (z={z}, x={x_min}..{x_max}, y={y_min}..{y_max}).")
        return

    def fetch_or_verify(item):
        key, url, out_path, has_valid_local, entry = item
        tmp_path = f"{out_path}.tmp.{os.getpid()}.{threading.get_ident()}"
        headers = {'User-Agent': 'Mozilla/5.0'}
        if has_valid_local:
            if entry.get('etag'):
                headers['If-None-Match'] = entry['etag']
            if entry.get('last_modified'):
                headers['If-Modified-Since'] = entry['last_modified']

        for attempt in range(2):
            try:
                req = urllib.request.Request(url, headers=headers)
                with urllib.request.urlopen(req, timeout=6, context=ctx) as resp:
                    data = resp.read()
                    etag = resp.headers.get('ETag', '') or ''
                    last_mod = resp.headers.get('Last-Modified', '') or ''
                    if len(data) > 100:
                        new_sha = hashlib.sha256(data).hexdigest()
                        old_sha = entry.get('sha256', '')
                        is_updated = bool(has_valid_local and old_sha and new_sha != old_sha)
                        if not has_valid_local or is_updated:
                            with open(tmp_path, 'wb') as f:
                                f.write(data)
                            os.replace(tmp_path, out_path)
                        with meta_lock:
                            tile_meta[key] = {
                                'sha256': new_sha,
                                'size': len(data),
                                'etag': etag or entry.get('etag', ''),
                                'last_modified': last_mod or entry.get('last_modified', ''),
                                'checked_at': time.time()
                            }
                        if not has_valid_local:
                            return 'downloaded'
                        return 'updated' if is_updated else 'verified'
            except urllib.error.HTTPError as he:
                if he.code == 304 and has_valid_local:
                    with meta_lock:
                        entry['checked_at'] = time.time()
                        tile_meta[key] = entry
                    return 'verified'
            except Exception:
                try:
                    if os.path.exists(tmp_path):
                        os.remove(tmp_path)
                except Exception:
                    pass
        if has_valid_local:
            return 'cached_offline'
        return 'failed'

    dl_count = 0
    upd_count = 0
    ver_count = 0
    off_count = 0
    with concurrent.futures.ThreadPoolExecutor(max_workers=36) as ex:
        for status in ex.map(fetch_or_verify, work_items):
            if status == 'downloaded':
                dl_count += 1
            elif status == 'updated':
                upd_count += 1
            elif status == 'verified':
                ver_count += 1
            elif status == 'cached_offline':
                off_count += 1

    try:
        tmp_meta = f"{meta_path}.tmp.{os.getpid()}"
        with open(tmp_meta, 'w', encoding='utf-8') as f:
            json.dump(tile_meta, f, ensure_ascii=False)
        os.replace(tmp_meta, meta_path)
    except Exception:
        pass

    ok_count = fresh_cached_count + ver_count + off_count + upd_count + dl_count
    print(
        f"Map tiles ready: {ok_count}/{total_needed} "
        f"(Cache hit: {fresh_cached_count + ver_count + off_count}, Updated: {upd_count}, Newly downloaded: {dl_count}) "
        f"[z={z}, x={x_min}..{x_max}, y={y_min}..{y_max}]."
    )


def is_valid_gpx_candidate(filepath):
    base = os.path.basename(filepath)
    if base.startswith('.') or base.startswith('._') or '__MACOSX' in filepath:
        return False
    if base.lower().endswith('.gpx'):
        return True
    try:
        with open(filepath, 'rb') as f:
            head = f.read(512)
        return b'<gpx' in head or b'<trkpt' in head or b'<rtept' in head
    except Exception:
        return False


def expand_gpx_inputs(input_paths, extract_root):
    """
    Accepts file paths (GPX files, compressed archives like .zip/.tar/.tar.gz/.tgz/.gz/.bz2/.xz/.7z/.rar,
    or directories), automatically unpacks any archives (including nested archives), and returns a sorted
    list of extracted GPX file paths.
    """
    gpx_files = []
    os.makedirs(extract_root, exist_ok=True)
    archive_counter = [0]

    def process_path(path):
        if not os.path.exists(path):
            print(f"Error: Input path not found: {path}", file=sys.stderr)
            sys.exit(1)
        base = os.path.basename(path)
        if base.startswith('._') or base == '__MACOSX':
            return

        if os.path.isdir(path):
            for entry in sorted(os.listdir(path)):
                if entry.startswith('.') or entry == '__MACOSX':
                    continue
                process_path(os.path.join(path, entry))
            return

        lower = base.lower()
        # Read magic bytes to detect archives even if uploaded with a generic extension
        magic = b''
        try:
            with open(path, 'rb') as f:
                magic = f.read(265)
        except Exception:
            pass

        is_zip = lower.endswith('.zip') or magic.startswith(b'PK\x03\x04')
        is_tar = (
            lower.endswith(('.tar', '.tar.gz', '.tgz', '.tar.bz2', '.tbz2', '.tar.xz', '.txz'))
            or (len(magic) >= 262 and magic[257:262] == b'ustar')
        )
        is_7z_or_rar = (
            lower.endswith(('.7z', '.rar'))
            or magic.startswith(b'7z\xbc\xaf\x27\x1c')
            or magic.startswith(b'Rar!\x1a\x07')
        )
        is_single_gz = (lower.endswith('.gz') and not is_tar) or (magic.startswith(b'\x1f\x8b') and not is_tar)
        is_single_bz2 = (lower.endswith('.bz2') and not is_tar) or (magic.startswith(b'BZh') and not is_tar)
        is_single_xz = (lower.endswith('.xz') and not is_tar) or (magic.startswith(b'\xfd7zXZ\x00') and not is_tar)

        if is_zip and zipfile.is_zipfile(path):
            archive_counter[0] += 1
            sub_dir = os.path.join(extract_root, f"unzip_{archive_counter[0]}")
            os.makedirs(sub_dir, exist_ok=True)
            print(f"Unpacking ZIP archive: {path} -> {sub_dir}")
            with zipfile.ZipFile(path, 'r') as zf:
                zf.extractall(sub_dir)
            process_path(sub_dir)
            return

        if is_tar or tarfile.is_tarfile(path):
            archive_counter[0] += 1
            sub_dir = os.path.join(extract_root, f"untar_{archive_counter[0]}")
            os.makedirs(sub_dir, exist_ok=True)
            print(f"Unpacking TAR archive: {path} -> {sub_dir}")
            with tarfile.open(path, 'r:*') as tf:
                tf.extractall(sub_dir)
            process_path(sub_dir)
            return

        if is_7z_or_rar:
            archive_counter[0] += 1
            sub_dir = os.path.join(extract_root, f"unarchive_{archive_counter[0]}")
            os.makedirs(sub_dir, exist_ok=True)
            print(f"Unpacking archive ({base}): {path} -> {sub_dir}")
            # Try bsdtar / /usr/bin/tar / 7z / unrar across macOS and Linux
            extracted_ok = False
            for cmd in [
                ['bsdtar', '-xf', path, '-C', sub_dir],
                ['/usr/bin/tar', '-xf', path, '-C', sub_dir],
                ['7z', 'x', path, f'-o{sub_dir}', '-y'],
                ['7za', 'x', path, f'-o{sub_dir}', '-y'],
                ['unrar', 'x', '-y', path, sub_dir],
            ]:
                try:
                    res = subprocess.run(cmd, capture_output=True)
                    if res.returncode == 0 and os.listdir(sub_dir):
                        extracted_ok = True
                        break
                except FileNotFoundError:
                    continue
            if not extracted_ok:
                print(f"Warning: Could not unpack {path}; ensure 7z/bsdtar/unrar is installed.", file=sys.stderr)
            process_path(sub_dir)
            return

        if is_single_gz or is_single_bz2 or is_single_xz:
            archive_counter[0] += 1
            stem = re.sub(r'\.(gz|bz2|xz)$', '', base, flags=re.IGNORECASE)
            if not stem.lower().endswith('.gpx'):
                stem += '.gpx'
            out_file = os.path.join(extract_root, f"{archive_counter[0]}_{stem}")
            print(f"Decompressing single file: {path} -> {out_file}")
            opener = gzip.open if is_single_gz else (bz2.open if is_single_bz2 else lzma.open)
            with opener(path, 'rb') as fin, open(out_file, 'wb') as fout:
                shutil.copyfileobj(fin, fout)
            process_path(out_file)
            return

        if is_valid_gpx_candidate(path):
            gpx_files.append(path)

    for p in input_paths:
        process_path(p)

    return gpx_files


def compute_gpx_signature(gpx_paths, name, mode):
    h = hashlib.sha256()
    h.update(f"name={name.strip()}|mode={mode.strip()}\n".encode('utf-8'))
    for p in sorted(gpx_paths):
        h.update(f"file={os.path.basename(p)}|sha256={file_sha256(p)}\n".encode('utf-8'))
    return h.hexdigest()


def main():
    parser = argparse.ArgumentParser(description="Prepare and merge GPX files (or archives) for 3D track animation")
    default_cache = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "cache"))
    parser.add_argument("--gpx", nargs="*", default=[], help="One or more GPX files, compressed archives (.zip/.tar.gz/.gz/.7z), or folders. If omitted, automatically searches ./GPX")
    parser.add_argument("--gpx-dir", default="GPX", help="Fallback subdirectory to search when --gpx is not provided (default: ./GPX)")
    parser.add_argument("--name", default="", help="Track title displayed in overview banner")
    parser.add_argument("--mode", default="auto", help="Activity mode: 行走|骑行|自驾 or auto")
    parser.add_argument("--work-dir", required=True, help="Working directory for track_data.json")
    parser.add_argument("--cache-dir", default=default_cache, help="Persistent local cache directory for map tiles")
    parser.add_argument("--check-updates", action="store_true", default=True, help="Check whether cached content has updates (default: True)")
    parser.add_argument("--cache-max-age-hours", type=float, default=24.0, help="Max age in hours before re-verifying cached map tiles with remote server (set 0 to force check every tile)")
    parser.add_argument("--skip-tiles", action="store_true", help="Skip downloading map tiles")
    args = parser.parse_args()

    os.makedirs(args.work_dir, exist_ok=True)
    os.makedirs(args.cache_dir, exist_ok=True)
    extract_dir = os.path.join(args.work_dir, "extracted_gpx")
    if os.path.exists(extract_dir):
        shutil.rmtree(extract_dir)

    gpx_inputs = [p for p in args.gpx if p and p.strip()]
    cleanup_gpx_dir = ""
    fallback_dir = os.path.abspath(args.gpx_dir)

    if not gpx_inputs:
        os.makedirs(fallback_dir, exist_ok=True)
        print(f"No --gpx files specified; automatically searching subdirectory: {fallback_dir}")
        gpx_inputs = [fallback_dir]
        cleanup_gpx_dir = fallback_dir
    elif len(gpx_inputs) == 1 and os.path.isdir(gpx_inputs[0]) and os.path.abspath(gpx_inputs[0]) == fallback_dir:
        cleanup_gpx_dir = fallback_dir

    expanded_gpx_paths = expand_gpx_inputs(gpx_inputs, extract_dir)
    if not expanded_gpx_paths:
        print(f"Error: No valid GPX or archive files found in input(s): {gpx_inputs}", file=sys.stderr)
        sys.exit(1)

    input_sig = compute_gpx_signature(expanded_gpx_paths, args.name, args.mode)
    tracks_cache_dir = os.path.join(args.cache_dir, "tracks")
    os.makedirs(tracks_cache_dir, exist_ok=True)
    cached_track_json = os.path.join(tracks_cache_dir, f"track_{input_sig[:16]}.json")
    json_path = os.path.join(args.work_dir, "track_data.json")

    # Check if existing track_data.json is up-to-date with current GPX files & parameters
    if os.path.exists(json_path):
        try:
            with open(json_path, 'r', encoding='utf-8') as f:
                existing_td = json.load(f)
            if existing_td.get('input_signature') != input_sig:
                print("Detected GPX/parameter update compared to cached track_data.json -> re-parsing.")
        except Exception:
            pass

    # 1. Parse all GPX files
    parsed_files = []
    for gpath in expanded_gpx_paths:
        t_name, first_dt, pts = parse_single_gpx(gpath)
        if pts:
            parsed_files.append((first_dt, t_name, gpath, pts))

    if not parsed_files:
        print("Error: No valid track points found in GPX file(s).", file=sys.stderr)
        sys.exit(1)

    # Sort multiple GPX files chronologically if timestamps exist
    if len(parsed_files) > 1 and all(pf[0] is not None for pf in parsed_files):
        parsed_files.sort(key=lambda x: x[0])

    raw_pts = []
    default_name = parsed_files[0][1] or "3D运动轨迹"
    for idx, (_, _, gpath, pts) in enumerate(parsed_files):
        if raw_pts and pts:
            # Skip duplicate boundary point if identical
            if abs(raw_pts[-1]['lat'] - pts[0]['lat']) < 1e-7 and abs(raw_pts[-1]['lon'] - pts[0]['lon']) < 1e-7:
                pts = pts[1:]
        raw_pts.extend(pts)

    print(f"Merged {len(parsed_files)} GPX file(s) -> {len(raw_pts)} raw track points.")

    # Compute relative elapsed seconds `t` (handling gaps between multiple merged files)
    cum_t = 0.0
    for i, p in enumerate(raw_pts):
        if i > 0:
            if p['dt'] is not None and raw_pts[i - 1]['dt'] is not None:
                dt_diff = (p['dt'] - raw_pts[i - 1]['dt']).total_seconds()
                # Cap inter-file rest gaps at 60s for average moving speed calculation
                if 0.0 < dt_diff < 3600.0:
                    cum_t += dt_diff
                else:
                    d_step = haversine(raw_pts[i - 1]['lat'], raw_pts[i - 1]['lon'], p['lat'], p['lon'])
                    cum_t += max(0.0, d_step / 10.0)
            else:
                d_step = haversine(raw_pts[i - 1]['lat'], raw_pts[i - 1]['lon'], p['lat'], p['lon'])
                cum_t += d_step / 3.0
        p['t'] = cum_t

    # Smooth elevation to remove GPS/barometric jitter
    n_pts = len(raw_pts)
    win = max(3, min(90, n_pts // 150))
    prefix_ele = [0.0] * (n_pts + 1)
    for i in range(n_pts):
        prefix_ele[i + 1] = prefix_ele[i] + raw_pts[i]['ele']
    for i in range(n_pts):
        i0 = max(0, i - win)
        i1 = min(n_pts, i + win + 1)
        raw_pts[i]['ele_smooth'] = (prefix_ele[i1] - prefix_ele[i0]) / (i1 - i0)

    # Calculate distance, ascent, descent
    cum_dist = 0.0
    cum_asc = 0.0
    cum_desc = 0.0
    ref_ele = raw_pts[0]['ele_smooth']
    # Use 2.0m hysteresis threshold for long routes, 1.0m for short routes
    for i, p in enumerate(raw_pts):
        if i > 0:
            d = haversine(raw_pts[i - 1]['lat'], raw_pts[i - 1]['lon'], p['lat'], p['lon'])
            cum_dist += d
        p['dist'] = cum_dist

    hyst = 2.0 if cum_dist > 50000.0 else 0.8
    for i, p in enumerate(raw_pts):
        if i > 0:
            de = p['ele_smooth'] - ref_ele
            if abs(de) >= hyst:
                if de > 0:
                    cum_asc += de
                else:
                    cum_desc += -de
                ref_ele = p['ele_smooth']
        p['asc'] = cum_asc
        p['desc'] = cum_desc

    total_dur_sec = max(1.0, raw_pts[-1]['t'] - raw_pts[0]['t'])
    avg_speed_kmh = (cum_dist / total_dur_sec) * 3.6
    icon_mode, mode_desc = resolve_icon_mode(args.mode, avg_speed_kmh)

    hrs = [p['hr'] for p in raw_pts if p['hr'] > 0]
    avg_hr = int(round(sum(hrs) / len(hrs))) if hrs else 0

    # Downsample to at most 2400 evenly-spaced distance points
    target_n = min(2400, max(400, n_pts))
    sampled = []
    step_dist = cum_dist / max(1, target_n - 1)
    idx = 0
    for k in range(target_n):
        target_d = k * step_dist
        while idx < n_pts - 1 and raw_pts[idx]['dist'] < target_d:
            idx += 1
        p = raw_pts[idx]
        sampled.append({
            'lat': round(p['lat'], 6),
            'lon': round(p['lon'], 6),
            'ele': round(p['ele_smooth'], 2),
            'dist': round(p['dist'], 1),
            'asc': round(p['asc'], 1),
            'desc': round(p['desc'], 1),
            't': round(p['t'], 1),
            'hr': int(p['hr'])
        })

    lats = [p['lat'] for p in sampled]
    lons = [p['lon'] for p in sampled]
    center_lat = 0.5 * (min(lats) + max(lats))
    center_lon = 0.5 * (min(lons) + max(lons))
    in_china = is_in_china(center_lat, center_lon)

    # Use hybrid road overlay + GCJ-02 alignment for driving/cycling or overland routes in China
    use_road_overlay = bool(in_china and (icon_mode in (1, 2) or cum_dist >= 15000.0))
    use_gcj02 = use_road_overlay

    z, x_min, x_max, y_min, y_max = choose_optimal_zoom(lats, lons, use_gcj02)

    final_name = args.name.strip() if args.name and args.name.strip() else default_name
    eles = [p['ele'] for p in sampled]

    os.makedirs(args.cache_dir, exist_ok=True)
    sat_dir = os.path.join(args.cache_dir, f"tiles_sat_z{z}")
    road_dir = os.path.join(args.cache_dir, f"tiles_road_z{z}")
    os.makedirs(sat_dir, exist_ok=True)
    if use_road_overlay:
        os.makedirs(road_dir, exist_ok=True)

    # Migrate or update any tiles that exist in work_dir into the persistent local cache
    for sub_name, target_cache_dir in [(f"tiles_sat_z{z}", sat_dir), (f"tiles_road_z{z}", road_dir)]:
        legacy_dir = os.path.join(args.work_dir, sub_name)
        if os.path.abspath(legacy_dir) != os.path.abspath(target_cache_dir) and os.path.isdir(legacy_dir):
            os.makedirs(target_cache_dir, exist_ok=True)
            for fname in os.listdir(legacy_dir):
                src_f = os.path.join(legacy_dir, fname)
                dst_f = os.path.join(target_cache_dir, fname)
                if is_valid_tile_file(src_f):
                    if not is_valid_tile_file(dst_f) or (
                        os.path.getmtime(src_f) > os.path.getmtime(dst_f) and file_sha256(src_f) != file_sha256(dst_f)
                    ):
                        shutil.copy2(src_f, dst_f)

    track_data = {
        'input_signature': input_sig,
        'updated_at': int(time.time()),
        'name': final_name,
        'icon_mode': icon_mode,
        'mode_desc': mode_desc,
        'avg_speed_kmh': round(avg_speed_kmh, 2),
        'avg_hr': avg_hr,
        'z': z,
        'x_min': x_min,
        'x_max': x_max,
        'y_min': y_min,
        'y_max': y_max,
        'use_gcj02': use_gcj02,
        'use_road_overlay': use_road_overlay,
        'sat_tiles_dir': sat_dir,
        'road_tiles_dir': road_dir,
        'cleanup_gpx_dir': cleanup_gpx_dir,
        'total_dist': round(cum_dist, 1),
        'total_asc': round(cum_asc, 1),
        'total_desc': round(cum_desc, 1),
        'min_ele': round(min(eles), 1),
        'max_ele': round(max(eles), 1),
        'points': sampled
    }

    with open(json_path, 'w', encoding='utf-8') as f:
        json.dump(track_data, f, ensure_ascii=False)
    with open(cached_track_json, 'w', encoding='utf-8') as f:
        json.dump(track_data, f, ensure_ascii=False)

    print(f"Track Name : {final_name}")
    print(f"Mode       : {mode_desc} (icon_mode={icon_mode})")
    print(f"Distance   : {cum_dist/1000.0:.2f} km | Ascent: {cum_asc:.0f} m | Descent: {cum_desc:.0f} m | Avg HR: {avg_hr}")
    print(f"Tile Grid  : z={z}, x={x_min}..{x_max} ({x_max-x_min+1} cols), y={y_min}..{y_max} ({y_max-y_min+1} rows)")
    print(f"Saved JSON : {json_path} (Cached: {cached_track_json})")

    if not args.skip_tiles:
        download_tiles(
            z, x_min, x_max, y_min, y_max, sat_dir, road_dir, use_road_overlay,
            cache_dir=args.cache_dir, check_updates=args.check_updates, max_age_hours=args.cache_max_age_hours
        )


if __name__ == "__main__":
    main()
