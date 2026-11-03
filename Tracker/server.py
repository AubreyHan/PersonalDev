#!/usr/bin/env python3
"""
TrailMov Web 3D Trajectory Backend Server
Provides GPX scanning, chronological merging, spatial telemetry parsing,
and static web hosting for the TrailMov 3D interactive editor.
"""

import os
import glob
import json
import math
import argparse
from http.server import HTTPServer, SimpleHTTPRequestHandler
from urllib.parse import urlparse, parse_qs
import gpxpy
import gpxpy.gpx
import numpy as np


class TrailmovServer(SimpleHTTPRequestHandler):
    def __init__(self, *args, directory=None, **kwargs):
        super().__init__(*args, directory=directory or "web", **kwargs)

    def do_GET(self):
        parsed = urlparse(self.path)
        if parsed.path == "/api/track":
            self.handle_get_track()
        elif parsed.path == "/api/list_gpx":
            self.handle_list_gpx()
        elif parsed.path == "/api/delete_gpx":
            self.handle_delete_gpx(parsed.query)
        else:
            super().do_GET()

    def do_POST(self):
        parsed = urlparse(self.path)
        if parsed.path == "/api/delete_gpx":
            content_length = int(self.headers.get("Content-Length", 0))
            body = self.rfile.read(content_length).decode("utf-8") if content_length > 0 else ""
            self.handle_delete_gpx(body)
        elif parsed.path == "/api/upload_export":
            self.handle_upload_export(parsed.query)
        else:
            self.send_error(404, "Endpoint not found")

    def handle_upload_export(self, query_string):
        params = parse_qs(query_string)
        filename = params.get("filename", ["output/recorded.webm"])[0]
        # Security: sanitize filename
        filename = os.path.basename(filename)
        out_path = os.path.join("output", filename)
        os.makedirs("output", exist_ok=True)

        content_length = int(self.headers.get("Content-Length", 0))
        if content_length > 0:
            with open(out_path, "wb") as f:
                f.write(self.rfile.read(content_length))
            print(f"[✓] Received exported recording: {out_path} ({round(os.path.getsize(out_path)/(1024*1024), 2)} MB)")
            self.send_json({"success": True, "saved_path": out_path})
        else:
            self.send_json({"success": False, "error": "Empty payload"}, status=400)

    def send_json(self, data, status=200):
        body = json.dumps(data, ensure_ascii=False).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()
        self.wfile.write(body)

    def handle_list_gpx(self):
        gpx_files = sorted(glob.glob("GPX/*.gpx"))
        file_info = []
        for f in gpx_files:
            file_info.append({
                "path": f,
                "name": os.path.basename(f),
                "size_mb": round(os.path.getsize(f) / (1024 * 1024), 2)
            })
        self.send_json({"files": file_info, "count": len(file_info)})

    def handle_delete_gpx(self, query_string):
        params = parse_qs(query_string)
        confirm = params.get("confirm", [""])[0].lower() in ("true", "1", "yes", "qualified", "合格")
        if not confirm:
            self.send_json({
                "success": False,
                "message": "Deletion blocked: User confirmation required. Please confirm the generated video is qualified."
            }, status=400)
            return

        gpx_files = glob.glob("GPX/*.gpx")
        deleted = []
        for f in gpx_files:
            try:
                os.remove(f)
                deleted.append(os.path.basename(f))
            except Exception as e:
                pass
        self.send_json({
            "success": True,
            "message": f"Successfully deleted {len(deleted)} GPX files after confirmation.",
            "deleted_files": deleted
        })

    def handle_get_track(self):
        gpx_files = sorted(glob.glob("GPX/*.gpx"))
        if not gpx_files:
            self.send_json({"error": "No GPX files found in GPX/ directory."}, status=404)
            return

        parsed_tracks = []
        for filepath in gpx_files:
            try:
                with open(filepath, "r", encoding="utf-8", errors="ignore") as f:
                    gpx = gpxpy.parse(f)
                
                pts = []
                track_name = None
                for track in gpx.tracks:
                    if track.name and not track_name:
                        track_name = track.name
                    for seg in track.segments:
                        for p in seg.points:
                            pts.append({
                                "lat": p.latitude,
                                "lon": p.longitude,
                                "ele": p.elevation if p.elevation is not None else 0.0,
                                "time": p.time.isoformat() if p.time else None,
                                "ts": p.time.timestamp() if p.time else None
                            })
                if pts:
                    first_ts = pts[0]["ts"] or 0
                    parsed_tracks.append({
                        "file": filepath,
                        "title": track_name or os.path.splitext(os.path.basename(filepath))[0],
                        "start_ts": first_ts,
                        "points": pts
                    })
            except Exception as e:
                print(f"[!] Error parsing {filepath}: {e}")

        if not parsed_tracks:
            self.send_json({"error": "Failed to parse any valid track points from GPX files."}, status=500)
            return

        # Sort tracks chronologically and merge
        parsed_tracks.sort(key=lambda t: t["start_ts"])
        merged_pts = []
        for t in parsed_tracks:
            merged_pts.extend(t["points"])

        primary_title = parsed_tracks[0]["title"]

        # Calculate distances, bearings, speeds, gradients, climbs
        R = 6378137.0
        n = len(merged_pts)
        lats = np.array([p["lat"] for p in merged_pts])
        lons = np.array([p["lon"] for p in merged_pts])
        eles = np.array([p["ele"] for p in merged_pts])

        lat0 = np.mean(lats)
        lon0 = np.mean(lons)
        lat_rad = np.radians(lat0)

        # Flat metric coordinates (local tangent)
        xs = R * np.radians(lons - lon0) * math.cos(lat_rad)
        ys = R * np.radians(lats - lat0)

        cum_dists = [0.0]
        cum_gains = [0.0]
        cum_losses = [0.0]
        speeds = [0.0]
        grades = [0.0]

        for i in range(1, n):
            dx = xs[i] - xs[i-1]
            dy = ys[i] - ys[i-1]
            step_d = math.hypot(dx, dy)
            cum_dists.append(cum_dists[-1] + step_d)

            # Elevation delta
            dele = eles[i] - eles[i-1]
            if dele > 0:
                cum_gains.append(cum_gains[-1] + dele)
                cum_losses.append(cum_losses[-1])
            else:
                cum_gains.append(cum_gains[-1])
                cum_losses.append(cum_losses[-1] + abs(dele))

            # Grade (%)
            grade = (dele / step_d * 100.0) if step_d > 1.0 else 0.0
            grades.append(round(min(max(grade, -45.0), 45.0), 1))

            # Speed (km/h) if timestamps exist
            t1 = merged_pts[i]["ts"]
            t0 = merged_pts[i-1]["ts"]
            if t1 and t0 and (t1 - t0) > 0:
                spd_kmh = (step_d / (t1 - t0)) * 3.6
                speeds.append(round(min(spd_kmh, 150.0), 1))
            else:
                speeds.append(0.0)

        # Ultra-Smooth Cinematic Bearing Calculation:
        # 1. 50-meter lookahead tangent to eliminate GPS micro-vibration
        lookahead_distance = 50.0
        tangent_bearings = []
        for i in range(n):
            j = i + 1
            while j < n and (cum_dists[j] - cum_dists[i]) < lookahead_distance:
                j += 1
            if j >= n:
                j = n - 1
            if j == i:
                tangent_bearings.append(tangent_bearings[-1] if tangent_bearings else 0.0)
            else:
                dx = xs[j] - xs[i]
                dy = ys[j] - ys[i]
                if math.hypot(dx, dy) < 0.5:
                    tangent_bearings.append(tangent_bearings[-1] if tangent_bearings else 0.0)
                else:
                    tangent_bearings.append(math.degrees(math.atan2(dx, dy)) % 360.0)

        # 2. Circular Gaussian Smoothing across angular domain
        window_size = 35
        sin_vals = np.sin(np.radians(tangent_bearings))
        cos_vals = np.cos(np.radians(tangent_bearings))
        kernel = np.ones(window_size) / window_size
        smooth_sin = np.convolve(sin_vals, kernel, mode="same")
        smooth_cos = np.convolve(cos_vals, kernel, mode="same")
        bearings = [float((np.degrees(np.arctan2(s, c))) % 360.0) for s, c in zip(smooth_sin, smooth_cos)]

        # Smooth elevation profile curve for bottom chart (200 sampled points)
        sample_indices = np.linspace(0, n - 1, min(200, n), dtype=int)
        profile_chart = []
        for idx in sample_indices:
            profile_chart.append({
                "dist_km": round(cum_dists[idx] / 1000.0, 2),
                "ele": round(float(eles[idx]), 1),
                "gain": round(cum_gains[idx], 1),
                "idx": int(idx)
            })

        # GeoJSON LineString
        coordinates = [[round(p["lon"], 6), round(p["lat"], 6), round(p["ele"], 1)] for p in merged_pts]

        response_data = {
            "metadata": {
                "title": primary_title,
                "total_points": n,
                "total_distance_km": round(cum_dists[-1] / 1000.0, 2),
                "total_gain_m": round(cum_gains[-1], 0),
                "total_loss_m": round(cum_losses[-1], 0),
                "min_ele_m": round(float(np.min(eles)), 0),
                "max_ele_m": round(float(np.max(eles)), 0),
                "start_point": [coordinates[0][0], coordinates[0][1]],
                "end_point": [coordinates[-1][0], coordinates[-1][1]],
                "bounds": {
                    "min_lon": float(np.min(lons)),
                    "max_lon": float(np.max(lons)),
                    "min_lat": float(np.min(lats)),
                    "max_lat": float(np.max(lats))
                },
                "source_files": [os.path.basename(t["file"]) for t in parsed_tracks]
            },
            "coordinates": coordinates,
            "telemetry": {
                "dists_km": [round(d / 1000.0, 3) for d in cum_dists],
                "gains_m": [round(g, 1) for g in cum_gains],
                "losses_m": [round(l, 1) for l in cum_losses],
                "eles_m": [round(float(e), 1) for e in eles],
                "speeds_kmh": speeds,
                "grades_pct": grades,
                "bearings_deg": [round(b, 1) for b in bearings]
            },
            "profile": profile_chart
        }

        self.send_json(response_data)


def main():
    parser = argparse.ArgumentParser(description="TrailMov 3D Web Server")
    parser.add_argument("--port", type=int, default=8000, help="Server port [default: 8000]")
    parser.add_argument("--dir", default="web", help="Web directory [default: web]")
    args = parser.parse_args()

    os.makedirs("GPX", exist_ok=True)
    os.makedirs(args.dir, exist_ok=True)

    server_address = ("", args.port)
    handler = lambda *h_args, **h_kwargs: TrailmovServer(*h_args, directory=args.dir, **h_kwargs)
    httpd = HTTPServer(server_address, handler)
    print(f"[*] TrailMov Web 3D server running at http://localhost:{args.port}/")
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\n[*] Server stopped.")


if __name__ == "__main__":
    main()
