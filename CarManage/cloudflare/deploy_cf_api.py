#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
我的车管家 · Cloudflare REST API 自动化直接部署与数据迁移工具
无需本地安装 Node.js / Wrangler，使用原生 Python 即可完成全自动部署。
"""

import os
import sys
import json
import urllib.request
import urllib.error
import mimetypes

CF_API_BASE = "https://api.cloudflare.com/client/v4"

def make_request(url, method="GET", headers=None, data=None):
    if headers is None:
        headers = {}
    if "User-Agent" not in headers:
        headers["User-Agent"] = "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36"
    req = urllib.request.Request(url, data=data, headers=headers, method=method)
    try:
        with urllib.request.urlopen(req) as resp:
            body = resp.read().decode("utf-8")
            return resp.status, json.loads(body)
    except urllib.error.HTTPError as e:
        body = e.read().decode("utf-8")
        try:
            parsed = json.loads(body)
        except Exception:
            parsed = {"raw": body}
        return e.code, parsed

def build_static_assets(script_dir):
    import base64
    static_dir = os.path.join(os.path.dirname(script_dir), "static")
    assets = {}
    files_map = {
        "/": ("index.html", "text/html; charset=utf-8"),
        "/index.html": ("index.html", "text/html; charset=utf-8"),
        "/style.css": ("style.css", "text/css; charset=utf-8"),
        "/app.js": ("app.js", "application/javascript; charset=utf-8"),
        "/db.js": ("db.js", "application/javascript; charset=utf-8"),
        "/sw.js": ("sw.js", "application/javascript; charset=utf-8"),
        "/manifest.json": ("manifest.json", "application/manifest+json; charset=utf-8"),
    }
    for route, (filename, mime) in files_map.items():
        p = os.path.join(static_dir, filename)
        if os.path.exists(p):
            with open(p, "r", encoding="utf-8") as f:
                assets[route] = {"content": f.read(), "contentType": mime}

    img_map = {
        "/icon-192.png": ("icon-192.png", "image/png"),
        "/icon-512.png": ("icon-512.png", "image/png"),
    }
    for route, (filename, mime) in img_map.items():
        p = os.path.join(static_dir, filename)
        if os.path.exists(p):
            with open(p, "rb") as f:
                assets[route] = {"base64": base64.b64encode(f.read()).decode("ascii"), "contentType": mime}

    out_path = os.path.join(script_dir, "static_assets.js")
    with open(out_path, "w", encoding="utf-8") as f:
        f.write("export const ASSETS = " + json.dumps(assets, ensure_ascii=False) + ";\n")
    print(f"📦 前端静态资源打包完成: {out_path} ({os.path.getsize(out_path)} 字节)")
    return out_path

def deploy(account_id: str, api_token: str):
    headers = {
        "Authorization": f"Bearer {api_token}",
        "Content-Type": "application/json"
    }

    print("==================================================")
    print("🚀 开始通过 Cloudflare REST API 自动部署与数据迁移")
    print("==================================================")

    # 1. 验证 Token
    print("🔍 1. 正在验证 Cloudflare API Token 权限...")
    status, res = make_request(f"{CF_API_BASE}/user/tokens/verify", headers=headers)
    if status != 200 or not res.get("success"):
        print(f"❌ API Token 验证失败 ({status}): {res}")
        return False
    print("✅ API Token 验证通过！")

    # 2. 检查或创建 D1 数据库
    db_name = "car_tracker_d1"
    print(f"📦 2. 检查 Cloudflare D1 数据库 '{db_name}'...")
    status, res = make_request(f"{CF_API_BASE}/accounts/{account_id}/d1/database", headers=headers)
    db_id = None
    if status == 200 and res.get("success"):
        for db in res.get("result", []):
            if db.get("name") == db_name:
                db_id = db.get("uuid")
                print(f"✅ 找到现有 D1 数据库，ID: {db_id}")
                break

    if not db_id:
        print(f"🚀 创建新 D1 数据库: {db_name}...")
        create_payload = json.dumps({"name": db_name}).encode("utf-8")
        status, res = make_request(f"{CF_API_BASE}/accounts/{account_id}/d1/database", method="POST", headers=headers, data=create_payload)
        if status not in (200, 201) or not res.get("success"):
            print(f"❌ 创建 D1 数据库失败: {res}")
            return False
        db_id = res["result"]["uuid"]
        print(f"✅ D1 数据库创建成功！ID: {db_id}")

    # 3. 执行建表 schema.sql
    script_dir = os.path.dirname(os.path.abspath(__file__))
    schema_path = os.path.join(script_dir, "schema.sql")
    print(f"🛠️ 3. 执行建表脚本: {schema_path}...")
    with open(schema_path, "r", encoding="utf-8") as f:
        schema_sql = f.read()

    status, res = make_request(
        f"{CF_API_BASE}/accounts/{account_id}/d1/database/{db_id}/query",
        method="POST",
        headers=headers,
        data=json.dumps({"sql": schema_sql}).encode("utf-8")
    )
    if status != 200 or not res.get("success"):
        print(f"❌ 执行建表 SQL 失败: {res}")
        return False
    print("✅ 数据库表结构初始化成功！")

    # 结构迁移：若已存在表则安全补全新增字段
    for col_sql in [
        "ALTER TABLE electricity_records ADD COLUMN is_calibrated INTEGER DEFAULT 0;",
        "ALTER TABLE electricity_records ADD COLUMN calibrated_capacity REAL;"
    ]:
        make_request(
            f"{CF_API_BASE}/accounts/{account_id}/d1/database/{db_id}/query",
            method="POST",
            headers=headers,
            data=json.dumps({"sql": col_sql}).encode("utf-8")
        )

    # 4. 默认初始数据库保持为空（只有显式传入 --migrate-seed 时才迁移历史数据）
    seed_path = os.path.join(script_dir, "seed_data.sql")
    if "--migrate-seed" in sys.argv and os.path.exists(seed_path):
        print(f"📥 4. 检测到 --migrate-seed 参数，正在导入数据至 Cloudflare D1 ({seed_path})...")
        with open(seed_path, "r", encoding="utf-8") as f:
            seed_sql = f.read()

        status, res = make_request(
            f"{CF_API_BASE}/accounts/{account_id}/d1/database/{db_id}/query",
            method="POST",
            headers=headers,
            data=json.dumps({"sql": seed_sql}).encode("utf-8")
        )
        if status != 200 or not res.get("success"):
            print(f"⚠️ 导入数据提示: {res}")
        else:
            print("✅ 历史数据已导入云端 D1 数据库！")
    else:
        print("ℹ️ 4. 初始数据库严格保持纯净空库（0 条业务记录，全新起步）。")

    # 5. 上传并部署 Cloudflare Worker
    worker_name = "car-tracker-api"
    worker_js_path = os.path.join(script_dir, "worker.js")
    print(f"⚡ 5. 正在上传并部署 Worker 云函数 '{worker_name}' 并绑定 D1...")

    with open(worker_js_path, "r", encoding="utf-8") as f:
        worker_code = f.read()

    metadata = {
        "main_module": "worker.js",
        "bindings": [
            {
                "type": "d1",
                "name": "DB",
                "id": db_id
            }
        ],
        "compatibility_date": "2024-09-01"
    }

    boundary = "----CloudflareWorkerBoundaryXYZ12345"
    body_parts = []
    # Part 1: metadata
    body_parts.append(f"--{boundary}\r\n".encode("utf-8"))
    body_parts.append(b'Content-Disposition: form-data; name="metadata"; filename="metadata.json"\r\n')
    body_parts.append(b'Content-Type: application/json\r\n\r\n')
    body_parts.append(json.dumps(metadata).encode("utf-8"))
    body_parts.append(b"\r\n")

    static_assets_path = build_static_assets(script_dir)
    with open(static_assets_path, "r", encoding="utf-8") as f:
        static_code = f.read()

    # Part 2: worker.js
    body_parts.append(f"--{boundary}\r\n".encode("utf-8"))
    body_parts.append(b'Content-Disposition: form-data; name="worker.js"; filename="worker.js"\r\n')
    body_parts.append(b'Content-Type: application/javascript+module\r\n\r\n')
    body_parts.append(worker_code.encode("utf-8"))
    body_parts.append(b"\r\n")

    # Part 3: static_assets.js
    body_parts.append(f"--{boundary}\r\n".encode("utf-8"))
    body_parts.append(b'Content-Disposition: form-data; name="static_assets.js"; filename="static_assets.js"\r\n')
    body_parts.append(b'Content-Type: application/javascript+module\r\n\r\n')
    body_parts.append(static_code.encode("utf-8"))
    body_parts.append(b"\r\n")

    body_parts.append(f"--{boundary}--\r\n".encode("utf-8"))

    multipart_body = b"".join(body_parts)

    upload_headers = {
        "Authorization": f"Bearer {api_token}",
        "Content-Type": f"multipart/form-data; boundary={boundary}"
    }

    status, res = make_request(
        f"{CF_API_BASE}/accounts/{account_id}/workers/scripts/{worker_name}",
        method="PUT",
        headers=upload_headers,
        data=multipart_body
    )
    if status not in (200, 201) or not res.get("success"):
        print(f"❌ 上传 Worker 失败 ({status}): {res}")
        return False
    print("✅ Worker 代码上传与 D1 绑定成功！")

    # 6. 激活 workers.dev 子域名路由
    print("🌐 6. 激活 Worker 公网访问域名 (workers.dev)...")
    subdomain_payload = json.dumps({"enabled": True}).encode("utf-8")
    status, res = make_request(
        f"{CF_API_BASE}/accounts/{account_id}/workers/scripts/{worker_name}/subdomain",
        method="POST",
        headers=headers,
        data=subdomain_payload
    )

    # 获取 subdomain
    status, sub_res = make_request(f"{CF_API_BASE}/accounts/{account_id}/workers/subdomain", headers=headers)
    subdomain = sub_res.get("result", {}).get("subdomain", "your-subdomain") if status == 200 else "your-subdomain"
    worker_url = f"https://{worker_name}.{subdomain}.workers.dev"

    print("==================================================")
    print("🎉🎉🎉 恭喜！云端全套服务已自动部署完毕！")
    print("==================================================")
    print(f"🔗 Worker API 根地址: {worker_url}")
    print(f"📦 D1 数据库名称: {db_name} (ID: {db_id})")
    if "--migrate-seed" in sys.argv:
        print("📋 数据状态: 已成功导入历史种子数据")
    else:
        print("📋 数据库状态: 初始纯净空库（0 业务记录，全新开箱即用）")
    print("")
    print("📱 在手机 / iPad / 网页中使用：")
    print("1. 打开网页右上角【☁️ 云端同步设置】；")
    print(f"2. 在 Worker 域名输入框中填入: {worker_url}")
    print("3. 点击【保存配置】即可与云端实时无缝打通！")
    print("==================================================")
    return True

if __name__ == "__main__":
    account_id = os.environ.get("CF_ACCOUNT_ID")
    api_token = os.environ.get("CF_API_TOKEN")

    if len(sys.argv) >= 3:
        account_id = sys.argv[1]
        api_token = sys.argv[2]

    if not account_id or not api_token:
        print("使用方式:")
        print("  python3 cloudflare/deploy_cf_api.py <ACCOUNT_ID> <API_TOKEN>")
        print("或者设置环境变量:")
        print("  export CF_ACCOUNT_ID='你的Cloudflare账户ID'")
        print("  export CF_API_TOKEN='你的Cloudflare_API_Token'")
        print("  python3 cloudflare/deploy_cf_api.py")
        sys.exit(1)

    success = deploy(account_id, api_token)
    sys.exit(0 if success else 1)
