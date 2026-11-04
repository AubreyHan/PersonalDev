import http.server
import socketserver
import json
import urllib.parse
import os
import mimetypes
import sys
import database

PORT = 8088
STATIC_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'static')

class CarTrackerHandler(http.server.BaseHTTPRequestHandler):
    def end_headers(self):
        # 允许跨域及本地调试
        self.send_header('Access-Control-Allow-Origin', '*')
        self.send_header('Access-Control-Allow-Methods', 'GET, POST, PUT, DELETE, OPTIONS')
        self.send_header('Access-Control-Allow-Headers', 'Content-Type')
        self.send_header('Cache-Control', 'no-cache, no-store, must-revalidate')
        super().end_headers()

    def do_OPTIONS(self):
        self.send_response(200)
        self.end_headers()

    def send_json(self, data, status=200):
        body = json.dumps(data, ensure_ascii=False, indent=2).encode('utf-8')
        self.send_response(status)
        self.send_header('Content-Type', 'application/json; charset=utf-8')
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def send_error_json(self, message, status=400):
        self.send_json({"error": True, "message": message}, status=status)

    def read_json_body(self):
        content_len = int(self.headers.get('Content-Length', 0))
        if content_len == 0:
            return {}
        post_data = self.rfile.read(content_len)
        try:
            return json.loads(post_data.decode('utf-8'))
        except Exception as e:
            return {}

    def do_GET(self):
        parsed_url = urllib.parse.urlparse(self.path)
        path = parsed_url.path
        query = urllib.parse.parse_qs(parsed_url.query)

        # API 路由
        if path == '/api/vehicles':
            vehicles = database.get_all_vehicles()
            self.send_json(vehicles)
            return

        if path.startswith('/api/vehicles/'):
            try:
                vid = int(path.split('/')[-1])
                vehicle = database.get_vehicle_by_id(vid)
                if vehicle:
                    self.send_json(vehicle)
                else:
                    self.send_error_json('车辆不存在', status=404)
            except ValueError:
                self.send_error_json('无效的车辆ID', status=400)
            return

        if path == '/api/records/fuel':
            vid = query.get('vehicle_id', [None])[0]
            vehicle_id = int(vid) if vid else None
            records = database.get_fuel_records(vehicle_id)
            self.send_json(records)
            return

        if path == '/api/records/electricity':
            vid = query.get('vehicle_id', [None])[0]
            vehicle_id = int(vid) if vid else None
            records = database.get_electricity_records(vehicle_id)
            self.send_json(records)
            return

        if path == '/api/records/maintenance':
            vid = query.get('vehicle_id', [None])[0]
            vehicle_id = int(vid) if vid else None
            records = database.get_maintenance_records(vehicle_id)
            self.send_json(records)
            return

        if path == '/api/records/status':
            vid = query.get('vehicle_id', [None])[0]
            vehicle_id = int(vid) if vid else None
            records = database.get_status_records(vehicle_id)
            self.send_json(records)
            return

        if path == '/api/statistics':
            vid = query.get('vehicle_id', [None])[0]
            if not vid:
                self.send_error_json('缺少 vehicle_id 参数', status=400)
                return
            stats = database.get_vehicle_statistics(int(vid))
            self.send_json(stats)
            return

        if path == '/api/energy-analysis':
            vid = query.get('vehicle_id', [None])[0]
            if not vid:
                self.send_error_json('缺少 vehicle_id 参数', status=400)
                return
            try:
                analysis = database.get_energy_interval_analysis(int(vid))
                self.send_json(analysis)
            except Exception as e:
                self.send_error_json(f'分析计算异常: {str(e)}', status=500)
            return

        if path == '/api/export':
            data = {
                "vehicles": database.get_all_vehicles(),
                "fuel_records": database.get_fuel_records(),
                "electricity_records": database.get_electricity_records(),
                "maintenance_records": database.get_maintenance_records()
            }
            self.send_json(data)
            return

        if path == '/api/baidu-search':
            q_term = query.get('query', ['加油站'])[0]
            lat = query.get('lat', [''])[0]
            lng = query.get('lng', [''])[0]
            radius = query.get('radius', ['15000'])[0]
            page_size = query.get('page_size', ['20'])[0]
            ak = query.get('ak', ['3YJcotkPHfGjr1vN50trOKrHoWUAwWCS'])[0]
            if not lat or not lng:
                self.send_error_json('缺少 lat/lng 坐标参数', status=400)
                return
            baidu_url = f"https://api.map.baidu.com/place/v2/search?query={urllib.parse.quote(q_term)}&location={lat},{lng}&radius={radius}&output=json&page_size={page_size}&ak={ak}&coord_type=2&ret_coordtype=gcj02ll"
            try:
                req = urllib.request.Request(baidu_url, headers={'User-Agent': 'Mozilla/5.0'})
                with urllib.request.urlopen(req, timeout=6) as resp:
                    data = json.loads(resp.read().decode('utf-8'))
                    self.send_json(data)
            except Exception as e:
                self.send_error_json(f'请求百度接口失败: {str(e)}', status=502)
            return

        # 静态文件服务
        self.serve_static(path)

    def do_POST(self):
        parsed_url = urllib.parse.urlparse(self.path)
        path = parsed_url.path
        body = self.read_json_body()

        # ----------------- 用户认证接口 -----------------
        if path == '/api/auth/login':
            username = body.get('username', '').strip()
            password = body.get('password', '')
            if not username or not password:
                self.send_error_json('用户名和密码不能为空')
                return
            res = database.verify_user(username, password)
            if res:
                self.send_json({"success": True, "token": res["token"], "user": {"username": res["username"]}})
            else:
                self.send_error_json('用户名或密码错误', status=401)
            return

        if path == '/api/auth/change-password':
            username = body.get('username', '').strip()
            old_password = body.get('old_password', '')
            new_password = body.get('new_password', '')
            if not username or not old_password or not new_password:
                self.send_error_json('请完整填写原密码与新密码')
                return
            if len(new_password) < 4:
                self.send_error_json('新密码长度不能少于4位')
                return
            success, msg_or_token = database.change_user_password(username, old_password, new_password)
            if success:
                self.send_json({"success": True, "token": msg_or_token, "message": "密码修改成功"})
            else:
                self.send_error_json(msg_or_token, status=400)
            return

        if path == '/api/auth/verify':
            token = body.get('token', '')
            res = database.verify_token(token)
            if res:
                self.send_json({"success": True, "authenticated": True, "user": res})
            else:
                self.send_error_json('登录态已失效或无效', status=401)
            return

        if path == '/api/vehicles':
            if not body.get('name') or not body.get('brand'):
                self.send_error_json('车名与品牌为必填项')
                return
            vehicle_id = database.create_vehicle(body)
            new_vehicle = database.get_vehicle_by_id(vehicle_id)
            self.send_json(new_vehicle, status=201)
            return

        if path == '/api/records/fuel':
            required = ['vehicle_id', 'mileage', 'fuel_amount', 'unit_price', 'total_price']
            for field in required:
                if field not in body:
                    self.send_error_json(f'缺少必填字段: {field}')
                    return
            rec_id = database.add_fuel_record(body)
            self.send_json({"id": rec_id, "success": True}, status=201)
            return

        if path == '/api/records/electricity':
            required = ['vehicle_id', 'mileage', 'electricity_amount', 'unit_price', 'total_price']
            for field in required:
                if field not in body:
                    self.send_error_json(f'缺少必填字段: {field}')
                    return
            rec_id = database.add_electricity_record(body)
            self.send_json({"id": rec_id, "success": True}, status=201)
            return

        if path == '/api/records/maintenance':
            required = ['vehicle_id', 'mileage', 'item_type']
            for field in required:
                if field not in body:
                    self.send_error_json(f'缺少必填字段: {field}')
                    return
            rec_id = database.add_maintenance_record(body)
            self.send_json({"id": rec_id, "success": True}, status=201)
            return

        if path == '/api/records/status':
            required = ['vehicle_id', 'mileage']
            for field in required:
                if field not in body:
                    self.send_error_json(f'缺少必填字段: {field}')
                    return
            rec_id = database.add_status_record(body)
            self.send_json({"id": rec_id, "success": True}, status=201)
            return

        if path == '/api/import':
            # 恢复数据
            try:
                vehicles = body.get('vehicles', [])
                for v in vehicles:
                    database.create_vehicle(v)
                self.send_json({"success": True, "message": "数据导入完成"})
            except Exception as e:
                self.send_error_json(f'导入失败: {str(e)}')
            return

        self.send_error_json('未知路由', status=404)

    def do_PUT(self):
        parsed_url = urllib.parse.urlparse(self.path)
        path = parsed_url.path
        body = self.read_json_body()

        if path.startswith('/api/vehicles/'):
            try:
                vid = int(path.split('/')[-1])
                conflict = database.check_optimistic_lock('vehicles', vid, body)
                if conflict:
                    self.send_json({"error": True, "message": "检测到多端并发修改冲突", **conflict}, status=409)
                    return
                success = database.update_vehicle(vid, body)
                if success:
                    self.send_json({"success": True, "vehicle": database.get_vehicle_by_id(vid)})
                else:
                    self.send_error_json('车辆更新失败或不存在', status=404)
            except ValueError:
                self.send_error_json('无效的车辆ID')
            return

        if path.startswith('/api/records/fuel/'):
            try:
                rid = int(path.split('/')[-1])
                conflict = database.check_optimistic_lock('fuel_records', rid, body)
                if conflict:
                    self.send_json({"error": True, "message": "检测到多端并发修改冲突", **conflict}, status=409)
                    return
                success = database.update_fuel_record(rid, body)
                if success:
                    self.send_json({"success": True, "record": database.get_fuel_record_by_id(rid)})
                else:
                    self.send_error_json('油耗记录更新失败或不存在', status=404)
            except ValueError:
                self.send_error_json('无效的记录ID')
            return

        if path.startswith('/api/records/electricity/'):
            try:
                rid = int(path.split('/')[-1])
                conflict = database.check_optimistic_lock('electricity_records', rid, body)
                if conflict:
                    self.send_json({"error": True, "message": "检测到多端并发修改冲突", **conflict}, status=409)
                    return
                success = database.update_electricity_record(rid, body)
                if success:
                    self.send_json({"success": True, "record": database.get_electricity_record_by_id(rid)})
                else:
                    self.send_error_json('电耗记录更新失败或不存在', status=404)
            except ValueError:
                self.send_error_json('无效的记录ID')
            return

        if path.startswith('/api/records/maintenance/'):
            try:
                rid = int(path.split('/')[-1])
                conflict = database.check_optimistic_lock('maintenance_records', rid, body)
                if conflict:
                    self.send_json({"error": True, "message": "检测到多端并发修改冲突", **conflict}, status=409)
                    return
                success = database.update_maintenance_record(rid, body)
                if success:
                    self.send_json({"success": True, "record": database.get_maintenance_record_by_id(rid)})
                else:
                    self.send_error_json('费用支出记录更新失败或不存在', status=404)
            except ValueError:
                self.send_error_json('无效的记录ID')
            return

        if path.startswith('/api/records/status/'):
            try:
                rid = int(path.split('/')[-1])
                conflict = database.check_optimistic_lock('status_records', rid, body)
                if conflict:
                    self.send_json({"error": True, "message": "检测到多端并发修改冲突", **conflict}, status=409)
                    return
                success = database.update_status_record(rid, body)
                if success:
                    self.send_json({"success": True, "record": database.get_status_record_by_id(rid)})
                else:
                    self.send_error_json('状态记录更新失败或不存在', status=404)
            except ValueError:
                self.send_error_json('无效的记录ID')
            return

        self.send_error_json('未知路由', status=404)

    def do_DELETE(self):
        parsed_url = urllib.parse.urlparse(self.path)
        path = parsed_url.path

        if path.startswith('/api/vehicles/'):
            try:
                vid = int(path.split('/')[-1])
                deleted = database.delete_vehicle(vid)
                self.send_json({"success": deleted})
            except ValueError:
                self.send_error_json('无效的车辆ID')
            return

        if path.startswith('/api/records/fuel/'):
            try:
                rid = int(path.split('/')[-1])
                deleted = database.delete_fuel_record(rid)
                self.send_json({"success": deleted})
            except ValueError:
                self.send_error_json('无效的记录ID')
            return

        if path.startswith('/api/records/electricity/'):
            try:
                rid = int(path.split('/')[-1])
                deleted = database.delete_electricity_record(rid)
                self.send_json({"success": deleted})
            except ValueError:
                self.send_error_json('无效的记录ID')
            return

        if path.startswith('/api/records/maintenance/'):
            try:
                rid = int(path.split('/')[-1])
                deleted = database.delete_maintenance_record(rid)
                self.send_json({"success": deleted})
            except ValueError:
                self.send_error_json('无效的记录ID')
            return

        if path.startswith('/api/records/status/'):
            try:
                rid = int(path.split('/')[-1])
                deleted = database.delete_status_record(rid)
                self.send_json({"success": deleted})
            except ValueError:
                self.send_error_json('无效的记录ID')
            return

        self.send_error_json('未知路由', status=404)

    def serve_static(self, path):
        if path == '/' or not path:
            path = '/index.html'

        # 确保安全路径
        safe_path = os.path.normpath(path.lstrip('/'))
        file_path = os.path.join(STATIC_DIR, safe_path)

        if not os.path.commonpath([STATIC_DIR, file_path]) == STATIC_DIR:
            self.send_error(403, "Forbidden")
            return

        if os.path.exists(file_path) and os.path.isfile(file_path):
            mime_type, _ = mimetypes.guess_type(file_path)
            if file_path.endswith('manifest.json'):
                mime_type = 'application/manifest+json'
            elif file_path.endswith('.js'):
                mime_type = 'application/javascript'
            elif not mime_type:
                mime_type = 'application/octet-stream'

            if mime_type.startswith('text/') or mime_type in ['application/javascript', 'application/json', 'application/manifest+json']:
                mime_type += '; charset=utf-8'

            try:
                with open(file_path, 'rb') as f:
                    content = f.read()
                self.send_response(200)
                self.send_header('Content-Type', mime_type)
                self.send_header('Content-Length', str(len(content)))
                self.send_header('Access-Control-Allow-Origin', '*')
                if file_path.endswith('sw.js'):
                    self.send_header('Service-Worker-Allowed', '/')
                    self.send_header('Cache-Control', 'no-cache, no-store, must-revalidate')
                self.end_headers()
                self.wfile.write(content)
            except Exception as e:
                self.send_error(500, f"Internal Error: {str(e)}")
        else:
            self.send_error(404, "File Not Found")

class ThreadedHTTPServer(socketserver.ThreadingMixIn, http.server.HTTPServer):
    daemon_threads = True

def run(port=PORT):
    database.init_db()
    server_address = ('127.0.0.1', port)
    try:
        httpd = ThreadedHTTPServer(server_address, CarTrackerHandler)
        print(f"=== 汽车维护与能耗记录服务已启动 ===")
        print(f"本地访问地址: http://127.0.0.1:{port}")
        httpd.serve_forever()
    except OSError as e:
        if e.errno == 48: # Address already in use
            print(f"端口 {port} 被占用，尝试端口 {port+1}...")
            run(port + 1)
        else:
            raise e

if __name__ == '__main__':
    port = int(sys.argv[1]) if len(sys.argv) > 1 else PORT
    run(port)
