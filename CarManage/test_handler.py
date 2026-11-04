import io
import json
import unittest
import os
import database
import server

class MockSocket:
    def makefile(self, *args, **kwargs):
        return io.BytesIO()

class TestServerHandlerDirect(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.orig_db = database.DB_FILE
        database.DB_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'test_handler_tracker.db')
        if os.path.exists(database.DB_FILE):
            os.remove(database.DB_FILE)
        database.init_db()

    @classmethod
    def tearDownClass(cls):
        if os.path.exists(database.DB_FILE):
            os.remove(database.DB_FILE)
        database.DB_FILE = cls.orig_db

    def call_handler(self, method, path, body_dict=None):
        handler = server.CarTrackerHandler.__new__(server.CarTrackerHandler)
        handler.headers = {}
        handler.path = path
        handler.wfile = io.BytesIO()

        if body_dict is not None:
            body_bytes = json.dumps(body_dict).encode('utf-8')
            handler.rfile = io.BytesIO(body_bytes)
            handler.headers['Content-Length'] = str(len(body_bytes))
        else:
            handler.rfile = io.BytesIO()
            handler.headers['Content-Length'] = '0'

        responses = []
        captured_headers = {}
        handler.send_response = lambda code, message=None: responses.append(code)
        handler.send_header = lambda k, v: captured_headers.__setitem__(k, v)
        handler.end_headers = lambda: None

        if method == 'GET':
            handler.do_GET()
        elif method == 'POST':
            handler.do_POST()
        elif method == 'PUT':
            handler.do_PUT()
        elif method == 'DELETE':
            handler.do_DELETE()

        raw_output = handler.wfile.getvalue()
        try:
            parsed_json = json.loads(raw_output.decode('utf-8'))
        except Exception:
            parsed_json = raw_output.decode('utf-8')

        return responses[0] if responses else 200, parsed_json, captured_headers

    def test_handler_flow(self):
        # 1. 车辆创建
        status, res, _ = self.call_handler('POST', '/api/vehicles', {
            "name": "蔚来ES6",
            "brand": "蔚来",
            "model": "ES6 75kWh",
            "power_type": "ev",
            "displacement": "",
            "fuel_tank_capacity": 0,
            "battery_capacity": 75.0,
            "initial_fuel_percent": 0.0,
            "initial_battery_percent": 90.0,
            "plate_number": "沪AD12345",
            "initial_mileage": 1000.0,
            "purchase_date": "2025-06-01",
            "notes": "双电机四驱"
        })
        self.assertEqual(status, 201)
        vid = res['id']
        self.assertEqual(res['name'], "蔚来ES6")
        self.assertEqual(res['battery_capacity'], 75.0)
        self.assertEqual(res['initial_battery_percent'], 90.0)
        self.assertEqual(res['initial_fuel_percent'], 0.0)

        # 2. 车辆列表查询
        status, v_list, _ = self.call_handler('GET', '/api/vehicles')
        self.assertEqual(status, 200)
        self.assertTrue(any(v['id'] == vid for v in v_list))

        # 3. 电耗录入 (录入两项自算第三项后保存)
        # 加电量 60度, 单价 1.25元/度, 总价 75.00元
        status, e_res, _ = self.call_handler('POST', '/api/records/electricity', {
            "vehicle_id": vid,
            "mileage": 1400.0,
            "electricity_amount": 60.0,
            "unit_price": 1.25,
            "total_price": 75.0,
            "charging_type": "快充 (DC直流)",
            "start_soc": 20.0,
            "end_soc": 90.0,
            "fuel_percent": 75.0,
            "station": "蔚来换电站/超充",
            "record_date": "2026-09-18 10:00",
            "notes": "快速充能"
        })
        self.assertEqual(status, 201)
        self.assertTrue(e_res['success'])

        # 4. 能耗统计查询 (累计百公里能耗: 行程为补能里程-初始里程，能耗为前序能耗总和)
        status, stats, _ = self.call_handler('GET', f'/api/statistics?vehicle_id={vid}')
        self.assertEqual(status, 200)
        self.assertEqual(stats['total_driven_km'], 400.0)
        self.assertEqual(stats['electricity']['total_amount'], 60.0)
        self.assertEqual(stats['electricity']['avg_per_hundred'], 13.12) # (52.5/400)*100 = 13.12

        # 5. 能耗深度区间分析查询
        status, ea, _ = self.call_handler('GET', f'/api/energy-analysis?vehicle_id={vid}')
        self.assertEqual(status, 200)
        self.assertIn('summary', ea)
        self.assertEqual(len(ea['elec_intervals']), 1)
        self.assertEqual(ea['elec_intervals'][0]['delta_mileage'], 400.0)
        # 单次能耗算法: (90% - 20%) * 75kWh = 52.5 kWh, 区间能耗 = (52.5 / 400) * 100 = 13.12 kWh/100km
        self.assertEqual(ea['elec_intervals'][0]['consumed_amount'], 52.5)
        self.assertEqual(ea['elec_intervals'][0]['interval_consumption'], 13.12)
        # 累计能耗算法: 行程=400km, 累计能耗=52.5kWh, 累计百公里能耗=13.12 kWh/100km
        self.assertEqual(ea['elec_intervals'][0]['cumulative_mileage'], 400.0)
        self.assertEqual(ea['elec_intervals'][0]['cumulative_consumed_amount'], 52.5)
        self.assertEqual(ea['elec_intervals'][0]['cumulative_consumption'], 13.12)
        self.assertEqual(ea['summary']['electricity']['overall_avg'], 13.12)

        print("\n🎉 Handler 级别 RESTful API 单元测试通过！")

    def test_edit_records_put(self):
        # 1. 创建测试车辆
        status, res, _ = self.call_handler('POST', '/api/vehicles', {
            "name": "理想L8",
            "brand": "理想",
            "model": "L8 Pro",
            "power_type": "erev",
            "displacement": "1.5",
            "fuel_tank_capacity": 65.0,
            "battery_capacity": 42.8,
            "plate_number": "京A88888",
            "initial_mileage": 5000.0,
        })
        self.assertEqual(status, 201)
        vid = res['id']

        # 2. 测试油耗记录编辑 (PUT /api/records/fuel/<id>)
        status, f_add, _ = self.call_handler('POST', '/api/records/fuel', {
            "vehicle_id": vid,
            "mileage": 5500.0,
            "fuel_amount": 50.0,
            "unit_price": 8.0,
            "total_price": 400.0,
            "fuel_type": "95#",
            "is_full": 1,
            "fuel_percent": 100.0,
            "station": "中石化",
            "record_date": "2026-09-19 12:00",
            "notes": "加油记录"
        })
        self.assertEqual(status, 201)
        fid = f_add['id']

        # 执行编辑修改：里程变为 5600, 油量 55L, 总价 440元
        status, f_put, _ = self.call_handler('PUT', f'/api/records/fuel/{fid}', {
            "vehicle_id": vid,
            "mileage": 5600.0,
            "fuel_amount": 55.0,
            "unit_price": 8.0,
            "total_price": 440.0,
            "fuel_type": "95#",
            "is_full": 1,
            "fuel_percent": 100.0,
            "station": "中石化朝阳站",
            "record_date": "2026-09-19 12:30",
            "notes": "修改后的加油记录"
        })
        self.assertEqual(status, 200)
        self.assertTrue(f_put['success'])
        self.assertEqual(f_put['record']['mileage'], 5600.0)
        self.assertEqual(f_put['record']['fuel_amount'], 55.0)
        self.assertEqual(f_put['record']['total_price'], 440.0)
        self.assertEqual(f_put['record']['notes'], "修改后的加油记录")

        # 验证车辆当前最高里程同步更新为 5600
        status, v_get, _ = self.call_handler('GET', '/api/vehicles')
        v = next(item for item in v_get if item['id'] == vid)
        self.assertEqual(v['current_mileage'], 5600.0)

        # 3. 测试电耗记录编辑 (PUT /api/records/electricity/<id>)
        status, e_add, _ = self.call_handler('POST', '/api/records/electricity', {
            "vehicle_id": vid,
            "mileage": 5800.0,
            "electricity_amount": 35.0,
            "unit_price": 1.5,
            "total_price": 52.5,
            "charging_type": "快充",
            "start_soc": 20.0,
            "end_soc": 90.0,
            "fuel_percent": 80.0,
            "station": "特来电",
            "record_date": "2026-09-19 18:00",
            "notes": "快充"
        })
        self.assertEqual(status, 201)
        eid = e_add['id']

        # 执行编辑修改：里程变为 5850, 加电量 38度, 总价 57元, 结束电量 95%
        status, e_put, _ = self.call_handler('PUT', f'/api/records/electricity/{eid}', {
            "vehicle_id": vid,
            "mileage": 5850.0,
            "electricity_amount": 38.0,
            "unit_price": 1.5,
            "total_price": 57.0,
            "charging_type": "快充 (DC直流)",
            "start_soc": 20.0,
            "end_soc": 95.0,
            "fuel_percent": 80.0,
            "station": "特来电超充中心",
            "record_date": "2026-09-19 18:30",
            "notes": "修改后的电耗记录"
        })
        self.assertEqual(status, 200)
        self.assertTrue(e_put['success'])
        self.assertEqual(e_put['record']['mileage'], 5850.0)
        self.assertEqual(e_put['record']['electricity_amount'], 38.0)
        self.assertEqual(e_put['record']['end_soc'], 95.0)

        # 4. 测试费用支出记录编辑 (PUT /api/records/maintenance/<id>)
        status, m_add, _ = self.call_handler('POST', '/api/records/maintenance', {
            "vehicle_id": vid,
            "mileage": 5850.0,
            "item_type": "洗车",
            "cost": 35.0,
            "record_date": "2026-09-20",
            "notes": "精细洗车"
        })
        self.assertEqual(status, 201)
        mid = m_add['id']

        # 执行编辑修改：项目变为 "精细洗车+打蜡", 金额变为 88.0
        status, m_put, _ = self.call_handler('PUT', f'/api/records/maintenance/{mid}', {
            "vehicle_id": vid,
            "mileage": 5850.0,
            "item_type": "精细洗车+打蜡",
            "cost": 88.0,
            "record_date": "2026-09-20",
            "notes": "洗车打蜡优惠套餐"
        })
        self.assertEqual(status, 200)
        self.assertTrue(m_put['success'])
        self.assertEqual(m_put['record']['item_type'], "精细洗车+打蜡")
        self.assertEqual(m_put['record']['cost'], 88.0)
        self.assertEqual(m_put['record']['notes'], "洗车打蜡优惠套餐")

        print("🎉 油耗、电耗、费用支出三大项目编辑 PUT 接口单元测试通过！")

    def test_static_pwa_files(self):
        # 1. 静态主页
        status, content, headers = self.call_handler('GET', '/')
        self.assertEqual(status, 200)
        self.assertIn('text/html', headers.get('Content-Type', ''))

        # 2. Web App Manifest
        status, content, headers = self.call_handler('GET', '/manifest.json')
        self.assertEqual(status, 200)
        self.assertIn('application/manifest+json', headers.get('Content-Type', ''))

        # 3. Service Worker 离线缓存脚本与专属头部
        status, content, headers = self.call_handler('GET', '/sw.js')
        self.assertEqual(status, 200)
        self.assertIn('application/javascript', headers.get('Content-Type', ''))
        self.assertEqual(headers.get('Service-Worker-Allowed'), '/')
        self.assertIn('no-cache', headers.get('Cache-Control', ''))

        # 4. 离线数据库与前端引擎
        status, content, headers = self.call_handler('GET', '/db.js')
        self.assertEqual(status, 200)
        self.assertIn('application/javascript', headers.get('Content-Type', ''))

        print("🎉 静态资源与 PWA Service Worker 离线服务测试通过！")

    def test_current_status_update_handler(self):
        # 1. 创建增程车
        status, v_res, _ = self.call_handler('POST', '/api/vehicles', {
            "name": "理想L8",
            "brand": "理想",
            "model": "L8 Max",
            "power_type": "erev",
            "fuel_tank_capacity": 65.0,
            "battery_capacity": 42.8,
            "initial_fuel_percent": 100.0,
            "initial_battery_percent": 100.0,
            "initial_mileage": 0.0
        })
        self.assertEqual(status, 201)
        vid = v_res['id']

        # 2. 更新当前状态（当前里程、当前电量%、当前油量%）
        status, put_res, _ = self.call_handler('PUT', f'/api/vehicles/{vid}', {
            "current_mileage": 350.0,
            "current_battery_percent": 65.0,
            "current_fuel_percent": 70.0
        })
        self.assertEqual(status, 200)

        # 3. GET /api/vehicles/:id 验证更新结果
        status, v_get, _ = self.call_handler('GET', f'/api/vehicles/{vid}')
        self.assertEqual(status, 200)
        self.assertEqual(v_get['current_mileage'], 350.0)
        self.assertEqual(v_get['current_battery_percent'], 65.0)
        self.assertEqual(v_get['current_fuel_percent'], 70.0)

        # 4. GET /api/energy-analysis 验证未补能区间
        status, ea_get, _ = self.call_handler('GET', f'/api/energy-analysis?vehicle_id={vid}')
        self.assertEqual(status, 200)
        self.assertEqual(ea_get['summary']['total_driven_km'], 350.0)
        self.assertIsNotNone(ea_get.get('ongoing_interval'))
        self.assertEqual(ea_get['ongoing_interval']['delta_mileage'], 350.0)
        self.assertEqual(ea_get['ongoing_interval']['interval_fuel_consumed'], 19.5) # 30% * 65 = 19.5L
        self.assertEqual(ea_get['ongoing_interval']['interval_elec_consumed'], 14.98) # 35% * 42.8 = 14.98kWh

        print("🎉 录入当前状态（当前里程、当前电量%、当前油量%）API 及在途能耗分析测试通过！")

    def test_status_records_api_handler(self):
        # 1. 创建测试车
        status, v_res, _ = self.call_handler('POST', '/api/vehicles', {
            "name": "极氪001",
            "brand": "极氪",
            "model": "YOU版 100kWh",
            "power_type": "ev",
            "battery_capacity": 100.0,
            "initial_battery_percent": 100.0,
            "initial_mileage": 0.0
        })
        self.assertEqual(status, 201)
        vid = v_res['id']

        # 2. POST /api/records/status 录入状态
        status, s_res, _ = self.call_handler('POST', '/api/records/status', {
            "vehicle_id": vid,
            "mileage": 420.0,
            "battery_percent": 45.0,
            "record_date": "2026-04-01 10:00",
            "notes": "高速服务区快照"
        })
        self.assertEqual(status, 201)
        sid = s_res['id']

        # 3. GET /api/records/status?vehicle_id=...
        status, s_list, _ = self.call_handler('GET', f'/api/records/status?vehicle_id={vid}')
        self.assertEqual(status, 200)
        self.assertEqual(len(s_list), 1)
        self.assertEqual(s_list[0]['mileage'], 420.0)
        self.assertEqual(s_list[0]['battery_percent'], 45.0)

        # 4. PUT /api/records/status/:id (在历史明细中修改当前状态)
        status, s_put, _ = self.call_handler('PUT', f'/api/records/status/{sid}', {
            "vehicle_id": vid,
            "mileage": 435.0,
            "battery_percent": 42.0,
            "record_date": "2026-04-01 10:15",
            "notes": "修正高速服务区快照"
        })
        self.assertEqual(status, 200)
        self.assertTrue(s_put['success'])
        self.assertEqual(s_put['record']['mileage'], 435.0)
        self.assertEqual(s_put['record']['battery_percent'], 42.0)

        # 验证车辆最新状态也同步更新
        status, v_get, _ = self.call_handler('GET', f'/api/vehicles/{vid}')
        self.assertEqual(v_get['current_mileage'], 435.0)
        self.assertEqual(v_get['current_battery_percent'], 42.0)

        # 5. DELETE /api/records/status/:id
        status, s_del, _ = self.call_handler('DELETE', f'/api/records/status/{sid}')
        self.assertEqual(status, 200)
        self.assertTrue(s_del['success'])

        print("🎉 状态记录 RESTful API (GET/POST/PUT/DELETE) 单元测试通过！")

    def test_09_auth_endpoints(self):
        """测试用户登录与修改密码 RESTful API 接口"""
        # 1. 登录成功
        status, login_res, _ = self.call_handler('POST', '/api/auth/login', {
            "username": "AubreyHan",
            "password": "P@ss=w0rd"
        })
        self.assertEqual(status, 200)
        self.assertTrue(login_res['success'])
        self.assertEqual(login_res['user']['username'], 'AubreyHan')
        token = login_res['token']

        # 2. 错误密码登录失败 401
        status, err_res, _ = self.call_handler('POST', '/api/auth/login', {
            "username": "AubreyHan",
            "password": "wrong_password"
        })
        self.assertEqual(status, 401)
        self.assertTrue(err_res['error'])

        # 3. 校验 Token 登录态
        status, verify_res, _ = self.call_handler('POST', '/api/auth/verify', {
            "token": token
        })
        self.assertEqual(status, 200)
        self.assertTrue(verify_res['authenticated'])

        # 4. 修改密码
        status, chg_res, _ = self.call_handler('POST', '/api/auth/change-password', {
            "username": "AubreyHan",
            "old_password": "P@ss=w0rd",
            "new_password": "AubreyHanNewPassword#2026"
        })
        self.assertEqual(status, 200)
        self.assertTrue(chg_res['success'])
        new_token = chg_res['token']

        # 验证新密码登录成功
        status, new_login, _ = self.call_handler('POST', '/api/auth/login', {
            "username": "AubreyHan",
            "password": "AubreyHanNewPassword#2026"
        })
        self.assertEqual(status, 200)
        self.assertEqual(new_login['token'], new_token)

        # 恢复默认初始密码
        self.call_handler('POST', '/api/auth/change-password', {
            "username": "AubreyHan",
            "old_password": "AubreyHanNewPassword#2026",
            "new_password": "P@ss=w0rd"
        })
        print("🎉 用户登录与修改密码 RESTful API (POST /api/auth/login, verify, change-password) 单元测试通过！")

    def test_10_modular_architecture_and_static_serving(self):
        """测试第一阶段前端模块化拆分后的 6 大子模块静态路由与内容完整性"""
        module_paths = [
            '/modules/auth.js',
            '/modules/sync_ui.js',
            '/modules/tri_calc.js',
            '/modules/stations.js',
            '/modules/records.js',
            '/modules/energy_charts.js'
        ]
        for mod_path in module_paths:
            status, content, headers = self.call_handler('GET', mod_path)
            self.assertEqual(status, 200, f"Module {mod_path} should return HTTP 200")
            self.assertIn('application/javascript', headers.get('Content-Type', ''))
            self.assertIn('global.AppModules', content)
        print("🎉 前端 6 大模块化子系统静态服务与结构完整性测试通过！")

    def test_11_optimistic_locking_and_conflict_resolution(self):
        """测试第一阶段乐观锁版本递增、HTTP 409 多端冲突检测与强制合并裁决"""
        # 1. 创建测试车辆与油耗记录 (初始 sync_version = 1)
        status, v_res, _ = self.call_handler('POST', '/api/vehicles', {
            "name": "问界M5",
            "brand": "AITO",
            "model": "M5 增程版",
            "power_type": "erev",
            "fuel_tank_capacity": 56.0,
            "battery_capacity": 40.0,
            "initial_mileage": 1000.0
        })
        self.assertEqual(status, 201)
        vid = v_res['id']

        status, f_add, _ = self.call_handler('POST', '/api/records/fuel', {
            "vehicle_id": vid,
            "mileage": 1500.0,
            "fuel_amount": 40.0,
            "unit_price": 8.0,
            "total_price": 320.0,
            "fuel_type": "95#",
            "is_full": 1,
            "fuel_percent": 100.0,
            "station": "中石化一号站",
            "record_date": "2026-10-01 10:00"
        })
        self.assertEqual(status, 201)
        fid = f_add['id']

        # 2. 设备 A 正常提交修改 (基于版本 1 -> 更新后 sync_version 变为 2)
        status, put_a, _ = self.call_handler('PUT', f'/api/records/fuel/{fid}', {
            "vehicle_id": vid,
            "mileage": 1500.0,
            "fuel_amount": 42.0,
            "unit_price": 8.0,
            "total_price": 336.0,
            "fuel_type": "95#",
            "is_full": 1,
            "fuel_percent": 100.0,
            "station": "中石化一号站(设备A修改)",
            "record_date": "2026-10-01 10:00",
            "_base_version": 1,
            "_check_conflict": True
        })
        self.assertEqual(status, 200)
        self.assertEqual(put_a['record']['sync_version'], 2)

        # 3. 设备 B 离线时仍持有旧基准 _base_version=1，联网推送时触发 409 Conflict 乐观锁保护
        status, put_b_conflict, _ = self.call_handler('PUT', f'/api/records/fuel/{fid}', {
            "vehicle_id": vid,
            "mileage": 1500.0,
            "fuel_amount": 45.0,
            "unit_price": 8.0,
            "total_price": 360.0,
            "fuel_type": "95#",
            "is_full": 1,
            "fuel_percent": 100.0,
            "station": "中石化一号站(设备B并发修改)",
            "record_date": "2026-10-01 10:05",
            "_base_version": 1,
            "_check_conflict": True
        })
        self.assertEqual(status, 409)
        self.assertTrue(put_b_conflict.get('conflict'))
        self.assertEqual(put_b_conflict.get('client_base_version'), 1)
        self.assertEqual(put_b_conflict.get('server_version'), 2)
        self.assertEqual(put_b_conflict['server_record']['station'], "中石化一号站(设备A修改)")

        # 4. 设备 B 归档冲突快照后执行裁决覆盖 (_force_overwrite=True)，版本递增至 3
        status, put_b_resolve, _ = self.call_handler('PUT', f'/api/records/fuel/{fid}', {
            "vehicle_id": vid,
            "mileage": 1500.0,
            "fuel_amount": 45.0,
            "unit_price": 8.0,
            "total_price": 360.0,
            "fuel_type": "95#",
            "is_full": 1,
            "fuel_percent": 100.0,
            "station": "中石化一号站(设备B并发修改)",
            "record_date": "2026-10-01 10:05",
            "_base_version": 1,
            "_check_conflict": True,
            "_force_overwrite": True
        })
        self.assertEqual(status, 200)
        self.assertEqual(put_b_resolve['record']['sync_version'], 3)
        self.assertEqual(put_b_resolve['record']['station'], "中石化一号站(设备B并发修改)")
        print("🎉 乐观锁版本控制 (sync_version)、HTTP 409 冲突检测与裁决合并测试通过！")

if __name__ == '__main__':
    unittest.main()
