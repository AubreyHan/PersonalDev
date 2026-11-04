import os
import unittest
import database

class TestCarTracker(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        # 使用测试数据库
        cls.orig_db = database.DB_FILE
        database.DB_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'test_tracker.db')
        if os.path.exists(database.DB_FILE):
            os.remove(database.DB_FILE)
        database.init_db()

    @classmethod
    def tearDownClass(cls):
        if os.path.exists(database.DB_FILE):
            os.remove(database.DB_FILE)
        database.DB_FILE = cls.orig_db

    def test_01_create_vehicles(self):
        # 1. 录入燃油车（包含车名、品牌、型号、排量、油箱容量、车牌号、初始里程等）
        v1_data = {
            "name": "宝马3系日常代步",
            "brand": "宝马",
            "model": "325Li M运动套装",
            "power_type": "gas",
            "displacement": "2.0T",
            "fuel_tank_capacity": 59.0,
            "battery_capacity": 0,
            "plate_number": "粤B12345",
            "initial_mileage": 15000.0,
            "purchase_date": "2023-05-18",
            "notes": "第一台燃油车"
        }
        v1_id = database.create_vehicle(v1_data)
        self.assertGreater(v1_id, 0)
        v1 = database.get_vehicle_by_id(v1_id)
        self.assertEqual(v1['name'], "宝马3系日常代步")
        self.assertEqual(v1['displacement'], "2.0T")
        self.assertEqual(v1['fuel_tank_capacity'], 59.0)
        self.assertEqual(v1['initial_mileage'], 15000.0)
        self.assertEqual(v1['current_mileage'], 15000.0)
        self.assertEqual(v1['initial_fuel_percent'], 0.0)
        self.assertEqual(v1['initial_battery_percent'], 0.0)

        # 2. 录入纯电动车（包含电池容量、车牌号、初始里程、初始电量等）
        v2_data = {
            "name": "特斯拉家庭出行",
            "brand": "特斯拉",
            "model": "Model Y 后轮驱动版",
            "power_type": "ev",
            "displacement": "",
            "fuel_tank_capacity": 0,
            "battery_capacity": 60.0,
            "initial_fuel_percent": 0.0,
            "initial_battery_percent": 85.0,
            "plate_number": "粤BD88888",
            "initial_mileage": 2000.0,
            "purchase_date": "2024-01-10",
            "notes": "纯电SUV"
        }
        v2_id = database.create_vehicle(v2_data)
        self.assertGreater(v2_id, 0)
        v2 = database.get_vehicle_by_id(v2_id)
        self.assertEqual(v2['battery_capacity'], 60.0)
        self.assertEqual(v2['power_type'], "ev")
        self.assertEqual(v2['initial_fuel_percent'], 0.0)
        self.assertEqual(v2['initial_battery_percent'], 85.0)

        # 3. 验证多车辆列表
        vehicles = database.get_all_vehicles()
        self.assertEqual(len(vehicles), 2)

    def test_02_fuel_records_and_calculation(self):
        # 获取第一台车
        v = database.get_all_vehicles()[1] # 较早创建的那辆车
        vid = v['id']

        # 模拟“加油量(45L) + 单价(8.20元/L)” -> 自动算出“总价(369.00元)”
        amount = 45.0
        price = 8.20
        calculated_total = round(amount * price, 2)
        self.assertEqual(calculated_total, 369.00)

        # 模拟“总价(400元) + 单价(8.00元/L)” -> 自动算出“加油量(50.0L)”
        calc_amount = round(400.0 / 8.00, 2)
        self.assertEqual(calc_amount, 50.0)

        # 模拟“总价(300元) + 加油量(37.5L)” -> 自动算出“单价(8.000元/L)”
        calc_price = round(300.0 / 37.5, 3)
        self.assertEqual(calc_price, 8.000)

        # 录入油耗记录
        rec_id = database.add_fuel_record({
            "vehicle_id": vid,
            "mileage": 15500.0, # 行驶 500 km
            "fuel_amount": 40.0,
            "unit_price": 8.15,
            "total_price": 326.0,
            "fuel_type": "95#",
            "is_full": 1,
            "fuel_percent": 100.0,
            "station": "中石化科技园站",
            "record_date": "2026-09-10 12:00",
            "notes": "满箱油测算"
        })
        self.assertGreater(rec_id, 0)
        fuel_recs = database.get_fuel_records(vid)
        self.assertEqual(fuel_recs[0]['fuel_percent'], 100.0)

        # 检查当前最高里程已同步更新为 15500.0
        updated_v = database.get_vehicle_by_id(vid)
        self.assertEqual(updated_v['current_mileage'], 15500.0)

        # 检查统计数据
        stats = database.get_vehicle_statistics(vid)
        self.assertEqual(stats['total_driven_km'], 500.0)
        self.assertEqual(stats['fuel']['record_count'], 1)
        self.assertEqual(stats['fuel']['total_amount'], 40.0)
        self.assertEqual(stats['fuel']['total_cost'], 326.0)
        # 百公里油耗 = (40 / 500) * 100 = 8.0 L/100km
        self.assertEqual(stats['fuel']['avg_per_hundred'], 8.0)
        # 每公里费用 = 326 / 500 = 0.65 元/km
        self.assertEqual(stats['cost_per_km'], 0.65)
        self.assertIn('cost_per_day', stats)
        self.assertGreater(stats['cost_per_day'], 0)

    def test_03_electricity_records_and_calculation(self):
        # 获取电动车
        vehicles = database.get_all_vehicles()
        ev = [v for v in vehicles if v['power_type'] == 'ev'][0]
        ev_id = ev['id']

        # 模拟“加电量(50kWh) + 单价(1.20元/度)” -> 自动算出“总价(60.00元)”
        elec_amount = 50.0
        elec_price = 1.20
        calc_total = round(elec_amount * elec_price, 2)
        self.assertEqual(calc_total, 60.00)

        # 模拟“总价(75元) + 单价(1.50元/度)” -> 自动算出“加电量(50.00度)”
        calc_amount = round(75.0 / 1.50, 2)
        self.assertEqual(calc_amount, 50.0)

        # 录入电耗记录 (支持 fuel_percent 录入)
        rec_id = database.add_electricity_record({
            "vehicle_id": ev_id,
            "mileage": 2400.0, # 行驶 400 km
            "electricity_amount": 60.0,
            "unit_price": 1.25,
            "total_price": 75.0,
            "charging_type": "快充 (DC直流)",
            "start_soc": 20.0,
            "end_soc": 90.0,
            "fuel_percent": 80.0,
            "station": "特来电超级充电站",
            "record_date": "2026-09-12 18:30",
            "notes": "快充半小时"
        })
        self.assertGreater(rec_id, 0)
        elec_recs = database.get_electricity_records(ev_id)
        self.assertEqual(elec_recs[0]['fuel_percent'], 80.0)

        # 检查当前最高里程已同步为 2400.0
        updated_ev = database.get_vehicle_by_id(ev_id)
        self.assertEqual(updated_ev['current_mileage'], 2400.0)

        # 检查统计数据
        stats = database.get_vehicle_statistics(ev_id)
        self.assertEqual(stats['total_driven_km'], 400.0)
        self.assertEqual(stats['electricity']['record_count'], 1)
        self.assertEqual(stats['electricity']['total_amount'], 60.0)
        self.assertEqual(stats['electricity']['total_cost'], 75.0)
        # 累计百公里电耗 = (39.0 / 400) * 100 = 9.75 kWh/100km (消耗量 = (85% - 20%) * 60kWh = 39.0kWh)
        self.assertEqual(stats['electricity']['avg_per_hundred'], 9.75)
        # 每公里费用 = 75 / 400 = 0.19 元/km
        self.assertEqual(stats['cost_per_km'], 0.19)

    def test_04_maintenance_records(self):
        v = database.get_all_vehicles()[0]
        vid = v['id']

        rec_id = database.add_maintenance_record({
            "vehicle_id": vid,
            "mileage": 16000.0,
            "item_type": "常规小保养(机油+机滤)",
            "cost": 680.0,
            "provider": "4S官方授权服务中心",
            "record_date": "2026-09-15",
            "next_mileage_reminder": 26000.0,
            "next_date_reminder": "2027-09-15",
            "notes": "全合成机油 0W-20"
        })
        rec_id2 = database.add_maintenance_record({
            "vehicle_id": vid,
            "item_type": "停车费",
            "cost": 30.0,
            "record_date": "2026-09-18",
            "notes": "商场地下停车场"
        })
        self.assertGreater(rec_id2, 0)

        records = database.get_maintenance_records(vid)
        self.assertEqual(len(records), 2)
        self.assertEqual(records[0]['cost'], 30.0) # Sorted by date desc
        self.assertEqual(records[0]['notes'], "商场地下停车场")

        # 测试录入负数费用 (如保险返利、押金退还、费用冲减)
        refund_id = database.add_maintenance_record({
            "vehicle_id": vid,
            "item_type": "保险返现",
            "cost": -150.0,
            "record_date": "2026-09-20",
            "notes": "人保续保返点现金"
        })
        self.assertGreater(refund_id, 0)
        refund_rec = database.get_maintenance_record_by_id(refund_id)
        self.assertEqual(refund_rec['cost'], -150.0)

        # 测试修改维护记录允许负数
        database.update_maintenance_record(refund_id, {
            "vehicle_id": vid,
            "item_type": "保险返现",
            "cost": -200.0,
            "record_date": "2026-09-20",
            "notes": "人保续保返点现金(重核后增加)"
        })
        refund_rec_updated = database.get_maintenance_record_by_id(refund_id)
        self.assertEqual(refund_rec_updated['cost'], -200.0)

    def test_05_energy_interval_analysis(self):
        # 创建用于区间能耗测试的专用车辆
        test_v_id = database.create_vehicle({
            "name": "区间测试燃油车",
            "brand": "丰田",
            "model": "凯美瑞",
            "power_type": "gas",
            "displacement": "2.0L",
            "fuel_tank_capacity": 60.0,
            "battery_capacity": 0,
            "plate_number": "粤A66666",
            "initial_mileage": 10000.0
        })

        # 第 1 次加油：里程 10400km (区间 400km), 加油 32.0L, 单价 8.0, 总价 256.0
        # 区间油耗: (32/400)*100 = 8.0 L/100km, 区间每公里成本: 256/400 = 0.64 元/km
        database.add_fuel_record({
            "vehicle_id": test_v_id,
            "mileage": 10400.0,
            "fuel_amount": 32.0,
            "unit_price": 8.0,
            "total_price": 256.0,
            "fuel_type": "92#",
            "is_full": 1,
            "record_date": "2026-09-01 10:00"
        })

        # 第 2 次加油：里程 10900km (区间 500km), 加油 37.5L, 单价 8.0, 总价 300.0
        # 区间油耗: (37.5/500)*100 = 7.5 L/100km, 区间每公里成本: 300/500 = 0.60 元/km
        database.add_fuel_record({
            "vehicle_id": test_v_id,
            "mileage": 10900.0,
            "fuel_amount": 37.5,
            "unit_price": 8.0,
            "total_price": 300.0,
            "fuel_type": "92#",
            "is_full": 1,
            "record_date": "2026-09-10 12:00"
        })

        analysis = database.get_energy_interval_analysis(test_v_id)
        intervals = analysis['fuel_intervals']
        self.assertEqual(len(intervals), 2)

        # 检查第 1 次区间 (单次与累计)
        self.assertEqual(intervals[0]['order'], 1)
        self.assertEqual(intervals[0]['delta_mileage'], 400.0)
        self.assertEqual(intervals[0]['interval_consumption'], 8.0)
        self.assertEqual(intervals[0]['cost_per_km'], 0.64)
        self.assertEqual(intervals[0]['cumulative_mileage'], 400.0)
        self.assertEqual(intervals[0]['cumulative_consumed_amount'], 32.0)
        self.assertEqual(intervals[0]['cumulative_consumption'], 8.0)

        # 检查第 2 次区间 (单次与累计: 行程为本次里程减初始里程 900km, 能耗为所有能耗总和 69.5L)
        self.assertEqual(intervals[1]['order'], 2)
        self.assertEqual(intervals[1]['delta_mileage'], 500.0)
        self.assertEqual(intervals[1]['interval_consumption'], 7.5)
        self.assertEqual(intervals[1]['cost_per_km'], 0.60)
        self.assertEqual(intervals[1]['days_since_prev'], 9) # 9月1日到9月10日间隔9天
        self.assertEqual(intervals[1]['cumulative_mileage'], 900.0)
        self.assertEqual(intervals[1]['cumulative_consumed_amount'], 69.5)
        self.assertEqual(intervals[1]['cumulative_consumption'], 7.72)

        # 检查总的能耗统计
        summary = analysis['summary']
        self.assertEqual(summary['total_driven_km'], 900.0)
        self.assertEqual(summary['fuel']['count'], 2)
        self.assertEqual(summary['fuel']['total_amount'], 69.5)
        self.assertEqual(summary['fuel']['total_cost'], 556.0)
        # 综合百公里油耗 = (69.5 / 900) * 100 = 7.72 L/100km
        self.assertEqual(summary['fuel']['overall_avg'], 7.72)
        # 最佳油耗 = 7.5, 最高油耗 = 8.0
        self.assertEqual(summary['fuel']['best_consumption'], 7.5)
        self.assertEqual(summary['fuel']['worst_consumption'], 8.0)

    def test_06_erev_dual_energy_calculation(self):
        # 创建增程混动车 (EREV)，测试双能耗同步核算
        erev_id = database.create_vehicle({
            "name": "理想 L9 增程版",
            "brand": "理想",
            "model": "L9",
            "power_type": "erev",
            "displacement": "1.5T 增程器",
            "fuel_tank_capacity": 65.0,
            "battery_capacity": 52.3,
            "plate_number": "京A12345",
            "initial_mileage": 0.0,
            "initial_fuel_percent": 100.0,
            "initial_battery_percent": 100.0
        })

        # 1. 首次在 290km 充电，初始电量 27.0%，充至 81.9%，当前表显油量 96%
        # 耗电量 = (100% - 27%) * 52.3 = 38.18 kWh; 耗油量 = (100% - 96%) * 65.0 = 2.60 L
        database.add_electricity_record({
            "vehicle_id": erev_id,
            "mileage": 290.0,
            "electricity_amount": 30.0,
            "unit_price": 1.2,
            "total_price": 36.0,
            "charging_type": "快充 (DC直流)",
            "start_soc": 27.0,
            "end_soc": 81.9,
            "fuel_percent": 96.0,
            "record_date": "2026-09-01 10:00"
        })

        # 2. 第二次在 500km 加油，油量加满 100%，加前油量 90%，当前剩余电量 60.0%
        # 加油区间行驶 500 - 0 = 500km (因无前次加油记录，以初始里程为基准)
        # 耗电量 = 上次结束电量 (81.9%) - 本次当前电量 (60.0%) = 21.9% * 52.3 = 11.45 kWh
        database.add_fuel_record({
            "vehicle_id": erev_id,
            "mileage": 500.0,
            "fuel_amount": 6.5,
            "unit_price": 8.0,
            "total_price": 52.0,
            "fuel_type": "95#",
            "is_full": 1,
            "start_fuel_percent": 90.0,
            "fuel_percent": 100.0,
            "battery_percent": 60.0,
            "record_date": "2026-09-05 12:00"
        })

        analysis = database.get_energy_interval_analysis(erev_id)
        
        # 验证充电记录中是否同时核算了油耗和电耗及等效电耗
        elec_interval = analysis['elec_intervals'][0]
        self.assertEqual(elec_interval['interval_elec_consumed'], 38.18)
        self.assertEqual(elec_interval['interval_fuel_consumed'], 2.60)
        self.assertGreater(elec_interval['interval_elec_consumption'], 0)
        self.assertGreater(elec_interval['interval_fuel_consumption'], 0)
        # 等效电量 = 38.18 + 2.60 * 3.3 = 46.76 kWh
        self.assertEqual(elec_interval['interval_equivalent_elec_consumed'], 46.76)
        self.assertEqual(elec_interval['interval_equivalent_elec_consumption'], round((46.76 / 290.0) * 100, 2))

        # 验证加油记录中是否同时核算了油耗和电耗及等效电耗
        fuel_interval = analysis['fuel_intervals'][0]
        self.assertEqual(fuel_interval['interval_elec_consumed'], 11.45)
        self.assertGreater(fuel_interval['interval_elec_consumption'], 0)
        self.assertEqual(fuel_interval['battery_percent'], 60.0)
        self.assertIn('interval_equivalent_elec_consumed', fuel_interval)
        self.assertIn('interval_equivalent_elec_consumption', fuel_interval)

        # 验证全局统计中的等效电耗及最省/最高极值
        stats = database.get_vehicle_statistics(erev_id)
        self.assertIn('equivalent_avg_per_hundred', stats['electricity'])
        self.assertIn('equivalent_total_consumed', stats['electricity'])
        self.assertGreater(stats['electricity']['equivalent_avg_per_hundred'], 0)
        self.assertGreater(stats['electricity']['equivalent_total_consumed'], 0)

        # 验证能耗分析中等效电耗的历史最省与最高极值
        equiv_summary = analysis['summary']['equivalent_electricity']
        self.assertIn('best_consumption', equiv_summary)
        self.assertIn('worst_consumption', equiv_summary)
        self.assertGreater(equiv_summary['best_consumption'], 0)
        self.assertGreaterEqual(equiv_summary['worst_consumption'], equiv_summary['best_consumption'])

        # 验证按月费用统计结构
        self.assertIn('monthly_expenses', stats)
        self.assertIsInstance(stats['monthly_expenses'], list)
        self.assertGreater(len(stats['monthly_expenses']), 0)
        first_m = stats['monthly_expenses'][0]
        self.assertIn('month', first_m)
        self.assertIn('fuel_cost', first_m)
        self.assertIn('elec_cost', first_m)
        self.assertIn('other_cost', first_m)
        self.assertIn('total_cost', first_m)
        self.assertEqual(first_m['total_cost'], round(first_m['fuel_cost'] + first_m['elec_cost'] + first_m['other_cost'], 2))

    def test_07_fifo_energy_cost_calculation(self):
        # 1. 独立单元测试：测试用户给出的精确案例
        records = [
            {'electricity_amount': 50.0, 'unit_price': 1.2},
            {'electricity_amount': 35.0, 'unit_price': 1.0},
            {'electricity_amount': 52.0, 'unit_price': 1.7},
            {'electricity_amount': 40.0, 'unit_price': 1.5}
        ]
        # 消耗 82 度：前 50 度按 1.2 元 = 60 元，后 32 度按 1.0 元 = 32 元，总计 92 元；第三次 52 度不计入
        cost = database.compute_fifo_cost(82.0, records, 'electricity_amount', 'unit_price')
        self.assertEqual(cost, 92.0)

        # 2. 完整端到端测试：录入新车并模拟补能
        v_data = {
            "name": "FIFO测试车",
            "brand": "小鹏",
            "model": "G6",
            "power_type": "ev",
            "battery_capacity": 66.0,
            "initial_mileage": 100.0,
            "initial_battery_percent": 100.0,
            "current_mileage": 100.0
        }
        v_id = database.create_vehicle(v_data)

        # 第 1 次加电：里程 300km (走了 200km)，从 20% 充到 100% (耗电 80% = 52.8度)，单价 1.2，充 52.8 度
        database.add_electricity_record({
            "vehicle_id": v_id,
            "mileage": 300.0,
            "record_date": "2026-03-01 10:00",
            "electricity_amount": 52.8,
            "unit_price": 1.2,
            "total_price": round(52.8 * 1.2, 2),
            "start_soc": 20.0,
            "end_soc": 100.0
        })

        stats = database.get_vehicle_statistics(v_id)
        # 终极里程 = 300 - 100 = 200km
        self.assertEqual(stats['fifo_driven_km'], 200.0)
        self.assertEqual(stats['fifo_elec_cost'], round(52.8 * 1.2, 2))
        self.assertIn('km_per_day', stats)
        self.assertGreater(stats['km_per_day'], 0)
        self.assertEqual(stats['km_per_day'], round(stats['total_driven_km'] / stats['days_owned'], 2))
        self.assertIn('energy_cost_per_day', stats)
        self.assertIn('fifo_energy_cost_per_day', stats)
        self.assertEqual(stats['energy_cost_per_day'], round(stats['total_energy_cost'] / stats['days_owned'], 2))
        self.assertEqual(stats['fifo_energy_cost_per_day'], round(stats['fifo_energy_cost'] / stats['days_owned'], 2))

    def test_08_battery_calibration_temporal_isolation(self):
        # 测试电池校准及其严格的时序隔离（不追溯影响历史能耗）
        v_data = {
            "name": "电池校准测试车",
            "brand": "特斯拉",
            "model": "Model 3",
            "power_type": "ev",
            "battery_capacity": 60.0,
            "initial_mileage": 0.0,
            "initial_battery_percent": 100.0,
            "current_mileage": 0.0
        }
        v_id = database.create_vehicle(v_data)

        # 记录 1：行驶 100km，从 50% 充到 100%（耗电 50% = 30kWh，基于标称 60kWh），未矫正
        database.add_electricity_record({
            "vehicle_id": v_id,
            "mileage": 100.0,
            "record_date": "2026-03-01 10:00",
            "electricity_amount": 30.0,
            "unit_price": 1.0,
            "total_price": 30.0,
            "start_soc": 50.0,
            "end_soc": 100.0,
            "is_calibrated": 0
        })

        # 记录 2：行驶 100km (里程 200km)，从 20% 充到 90% (ΔSOC = 70%)，实际加电 35.0kWh
        # 勾选电池矫正：计算新容量 = 35.0 / 0.7 = 50.0 kWh
        # 注意：到达记录 2 之前的行驶耗电 (100% -> 20% = 80%) 必须仍采用旧容量 60.0 kWh 计算 (80% * 60 = 48kWh)
        database.add_electricity_record({
            "vehicle_id": v_id,
            "mileage": 200.0,
            "record_date": "2026-03-02 10:00",
            "electricity_amount": 35.0,
            "unit_price": 1.0,
            "total_price": 35.0,
            "start_soc": 20.0,
            "end_soc": 90.0,
            "is_calibrated": 1
        })

        # 记录 3：行驶 100km (里程 300km)，从 40% 充到 90% (耗电 90% - 40% = 50%)
        # 此时应该使用校准后的新容量 50.0 kWh 计算区间耗电：50% * 50 = 25kWh
        database.add_electricity_record({
            "vehicle_id": v_id,
            "mileage": 300.0,
            "record_date": "2026-03-03 10:00",
            "electricity_amount": 25.0,
            "unit_price": 1.0,
            "total_price": 25.0,
            "start_soc": 40.0,
            "end_soc": 90.0,
            "is_calibrated": 0
        })

        energy_stats = database.get_energy_interval_analysis(v_id)
        intervals = energy_stats['elec_intervals']
        self.assertEqual(len(intervals), 3)

        # 区间 1：耗电 30.0 kWh，容量 60.0
        self.assertEqual(intervals[0]['interval_elec_consumed'], 30.0)
        self.assertEqual(intervals[0]['drive_battery_capacity'], 60.0)
        self.assertEqual(intervals[0]['is_calibrated'], 0)

        # 区间 2：行车耗电基于旧容量 60.0 计算 (80% * 60 = 48.0 kWh)，随后校准为 50.0
        self.assertEqual(intervals[1]['interval_elec_consumed'], 48.0)
        self.assertEqual(intervals[1]['drive_battery_capacity'], 60.0)
        self.assertEqual(intervals[1]['is_calibrated'], 1)
        self.assertEqual(intervals[1]['calibrated_capacity'], 50.0)
        self.assertEqual(intervals[1]['active_battery_capacity'], 50.0)

        # 区间 3：行车耗电基于校准后的容量 50.0 计算 (50% * 50 = 25.0 kWh)
        self.assertEqual(intervals[2]['interval_elec_consumed'], 25.0)
        self.assertEqual(intervals[2]['drive_battery_capacity'], 50.0)
        self.assertEqual(intervals[2]['is_calibrated'], 0)

        # 全局统计指标验证
        summary_elec = energy_stats['summary']['electricity']
        self.assertEqual(summary_elec['nominal_battery_capacity'], 60.0)
        self.assertEqual(summary_elec['current_battery_capacity'], 50.0)
        # 总耗电 = 30 + 48 + 25 = 103.0 kWh
        self.assertEqual(summary_elec['total_consumed'], 103.0)

    def test_12_current_mileage_entry_and_stats(self):
        """测试用户直接录入当前里程，并验证累计里程及各项能耗、费用以此为最后里程基准"""
        # 创建一辆初始里程为 10000 km 的混动车
        v_data = {
            "name": "里程录入测试车",
            "brand": "比亚迪",
            "model": "汉DM-i",
            "power_type": "phev",
            "displacement": "1.5T",
            "fuel_tank_capacity": 50.0,
            "battery_capacity": 18.0,
            "initial_fuel_percent": 100.0,
            "initial_battery_percent": 100.0,
            "plate_number": "粤B99999",
            "initial_mileage": 10000.0,
            "purchase_date": "2024-01-01",
            "notes": "里程基准测试"
        }
        v_id = database.create_vehicle(v_data)
        self.assertGreater(v_id, 0)
        v = database.get_vehicle_by_id(v_id)
        self.assertEqual(v['current_mileage'], 10000.0)

        # 录入一笔油耗记录，里程 10200 km (行驶 200 km)
        database.add_fuel_record({
            "vehicle_id": v_id,
            "mileage": 10200.0,
            "fuel_amount": 10.0,
            "unit_price": 8.0,
            "total_price": 80.0,
            "start_fuel_percent": 80.0,
            "fuel_percent": 100.0,
            "record_date": "2024-01-10 10:00"
        })
        v = database.get_vehicle_by_id(v_id)
        self.assertEqual(v['current_mileage'], 10200.0)

        # 用户直接录入当前车辆表显总里程为 10500 km (未发生补能或保养)
        database.update_vehicle(v_id, {
            "name": v['name'],
            "brand": v['brand'],
            "model": v['model'],
            "power_type": v['power_type'],
            "displacement": v['displacement'],
            "fuel_tank_capacity": v['fuel_tank_capacity'],
            "battery_capacity": v['battery_capacity'],
            "plate_number": v['plate_number'],
            "initial_mileage": v['initial_mileage'],
            "current_mileage": 10500.0,
            "purchase_date": v['purchase_date'],
            "notes": v['notes'],
            "initial_fuel_percent": v['initial_fuel_percent'],
            "initial_battery_percent": v['initial_battery_percent']
        })

        # 验证车辆当前里程已更新为 10500 km
        v_updated = database.get_vehicle_by_id(v_id)
        self.assertEqual(v_updated['current_mileage'], 10500.0)

        # 验证再次触发里程同步时，10500 km 不会被旧记录 10200 冲掉覆盖
        database.sync_vehicle_current_mileage(v_id)
        v_synced = database.get_vehicle_by_id(v_id)
        self.assertEqual(v_synced['current_mileage'], 10500.0)

        # 验证能耗与费用分析：total_driven_km 必须以 10500 为最新里程算出的 500 km (10500 - 10000)
        ea = database.get_energy_interval_analysis(v_id)
        self.assertEqual(ea['summary']['total_driven_km'], 500.0)
        self.assertEqual(ea['summary']['fifo_driven_km'], 500.0)
        # 能耗费用单价 cost_per_km = 80.0 / 500 = 0.16
        self.assertEqual(ea['summary']['overall_energy_cost_per_km'], 0.16)

        # 验证车辆综合统计 get_vehicle_statistics 也以此为最后里程
        stats = database.get_vehicle_statistics(v_id)
        self.assertEqual(stats['total_driven_km'], 500.0)
        self.assertEqual(stats['cost_per_km'], 0.16)

    def test_13_current_status_entry_and_ongoing_consumption(self):
        """测试录入当前状态（里程、电量%、油量%）及未补能进行中区间(ongoing)的能耗与FIFO成本计算"""
        v_data = {
            "name": "状态录入测试车",
            "brand": "理想",
            "model": "L7",
            "power_type": "erev",
            "fuel_tank_capacity": 65.0,
            "battery_capacity": 42.8,
            "initial_mileage": 0.0,
            "initial_fuel_percent": 100.0,
            "initial_battery_percent": 100.0,
            "current_mileage": 0.0,
            "current_fuel_percent": 100.0,
            "current_battery_percent": 100.0
        }
        v_id = database.create_vehicle(v_data)
        v = database.get_vehicle_by_id(v_id)
        self.assertEqual(v['current_mileage'], 0.0)
        self.assertEqual(v['current_fuel_percent'], 100.0)
        self.assertEqual(v['current_battery_percent'], 100.0)

        # 1. 记一笔加油：里程 500 km，消耗油量 100% -> 20% (80% = 52L)，加满至 100% (52L, 单价 8.0, 416元)
        database.add_fuel_record({
            "vehicle_id": v_id,
            "mileage": 500.0,
            "fuel_amount": 52.0,
            "unit_price": 8.0,
            "total_price": 416.0,
            "start_fuel_percent": 20.0,
            "fuel_percent": 100.0,
            "battery_percent": 60.0,
            "record_date": "2026-03-01 10:00"
        })

        # 验证车辆状态自动被补能记录同步
        v = database.get_vehicle_by_id(v_id)
        self.assertEqual(v['current_mileage'], 500.0)
        self.assertEqual(v['current_fuel_percent'], 100.0)
        self.assertEqual(v['current_battery_percent'], 60.0)

        # 2. 用户直接录入当前状态：表显里程 800 km，当前油量 80%，当前电量 40% (未补能)
        database.update_vehicle(v_id, {
            "name": v['name'],
            "brand": v['brand'],
            "model": v['model'],
            "power_type": v['power_type'],
            "fuel_tank_capacity": v['fuel_tank_capacity'],
            "battery_capacity": v['battery_capacity'],
            "initial_mileage": v['initial_mileage'],
            "current_mileage": 800.0,
            "current_fuel_percent": 80.0,
            "current_battery_percent": 40.0
        })

        v_updated = database.get_vehicle_by_id(v_id)
        self.assertEqual(v_updated['current_mileage'], 800.0)
        self.assertEqual(v_updated['current_fuel_percent'], 80.0)
        self.assertEqual(v_updated['current_battery_percent'], 40.0)

        # 3. 验证能耗与费用分析计算：
        # 行驶总里程 = 800 km
        # 进行中区间(ongoing)：从 500km 到 800km (走了 300km)
        # 期间油量从 100% -> 80% (消耗 20% = 13.0L)
        # 期间电量从 60% -> 40% (消耗 20% = 8.56kWh)
        ea = database.get_energy_interval_analysis(v_id)
        self.assertIsNotNone(ea['ongoing_interval'])
        ongoing = ea['ongoing_interval']
        self.assertEqual(ongoing['delta_mileage'], 300.0)
        self.assertEqual(ongoing['interval_fuel_consumed'], 13.0)
        self.assertEqual(ongoing['interval_elec_consumed'], round(42.8 * 0.20, 2))

        # 累计总消耗油量：补能区间 52.0L + 进行中区间 13.0L = 65.0L
        summary = ea['summary']
        self.assertEqual(summary['total_driven_km'], 800.0)
        self.assertEqual(summary['fuel']['total_consumed'], 65.0)

        # 4. 再次加电：里程 900km，从 30% 充到 90% (加电 25.68kWh, 40元)
        database.add_electricity_record({
            "vehicle_id": v_id,
            "mileage": 900.0,
            "record_date": "2026-03-02 12:00",
            "electricity_amount": 25.68,
            "unit_price": 1.5576,
            "total_price": 40.0,
            "start_soc": 30.0,
            "end_soc": 90.0,
            "fuel_percent": 75.0
        })

        # 验证车辆当前状态自动更新为加电记录对应的状态
        v = database.get_vehicle_by_id(v_id)
        self.assertEqual(v['current_mileage'], 900.0)
        self.assertEqual(v['current_battery_percent'], 90.0)
        self.assertEqual(v['current_fuel_percent'], 75.0)

    def test_14_status_records_crud_and_history_edit(self):
        """测试当前状态记录在历史明细中的增加、查询、修改及删除全流程联动"""
        v_data = {
            "name": "状态明细修改测试车",
            "brand": "理想",
            "model": "L9",
            "power_type": "erev",
            "fuel_tank_capacity": 65.0,
            "battery_capacity": 52.3,
            "initial_mileage": 10.0,
            "initial_fuel_percent": 100.0,
            "initial_battery_percent": 100.0
        }
        v_id = database.create_vehicle(v_data)

        # 1. 录入一条当前状态记录 (里程 300km, 电量 80%, 油量 90%)
        s_id = database.add_status_record({
            "vehicle_id": v_id,
            "mileage": 300.0,
            "battery_percent": 80.0,
            "fuel_percent": 90.0,
            "record_date": "2026-03-05 15:30",
            "notes": "中途表显快照"
        })
        self.assertGreater(s_id, 0)

        # 验证车辆当前状态被联动同步
        v = database.get_vehicle_by_id(v_id)
        self.assertEqual(v['current_mileage'], 300.0)
        self.assertEqual(v['current_battery_percent'], 80.0)
        self.assertEqual(v['current_fuel_percent'], 90.0)

        # 2. 查询状态记录列表
        records = database.get_status_records(v_id)
        self.assertEqual(len(records), 1)
        self.assertEqual(records[0]['mileage'], 300.0)
        self.assertEqual(records[0]['battery_percent'], 80.0)
        self.assertEqual(records[0]['fuel_percent'], 90.0)

        # 3. 在历史明细中修改该状态记录 (例如修正里程为 350km, 电量 75%, 油量 85%)
        ok = database.update_status_record(s_id, {
            "vehicle_id": v_id,
            "mileage": 350.0,
            "battery_percent": 75.0,
            "fuel_percent": 85.0,
            "record_date": "2026-03-05 16:00",
            "notes": "修改后的快照"
        })
        self.assertTrue(ok)

        # 验证单条记录与车辆联动状态均已成功更新
        rec = database.get_status_record_by_id(s_id)
        self.assertEqual(rec['mileage'], 350.0)
        self.assertEqual(rec['battery_percent'], 75.0)
        self.assertEqual(rec['fuel_percent'], 85.0)

        v_after_update = database.get_vehicle_by_id(v_id)
        self.assertEqual(v_after_update['current_mileage'], 350.0)
        self.assertEqual(v_after_update['current_battery_percent'], 75.0)
        self.assertEqual(v_after_update['current_fuel_percent'], 85.0)

        # 4. 删除该状态记录
        del_ok = database.delete_status_record(s_id)
        self.assertTrue(del_ok)
        self.assertEqual(len(database.get_status_records(v_id)), 0)

    def test_14_user_authentication(self):
        """测试用户登录鉴权与修改密码核心能力"""
        # 1. 验证默认初始用户 AubreyHan 与密码 P@ss=w0rd
        auth_res = database.verify_user('AubreyHan', 'P@ss=w0rd')
        self.assertIsNotNone(auth_res)
        self.assertEqual(auth_res['username'], 'AubreyHan')
        token = auth_res['token']
        self.assertTrue(len(token) > 20)

        # 2. 错误密码应拒绝
        self.assertIsNone(database.verify_user('AubreyHan', 'WrongPassword123'))
        self.assertIsNone(database.verify_user('NonExistentUser', 'P@ss=w0rd'))

        # 3. 验证登录态 Token (持久保持)
        user_info = database.verify_token(token)
        self.assertIsNotNone(user_info)
        self.assertEqual(user_info['username'], 'AubreyHan')

        # 4. 修改密码
        # 原密码错误应失败
        ok, msg = database.change_user_password('AubreyHan', 'WrongOld', 'NewPwd#2026')
        self.assertFalse(ok)
        self.assertIn('原密码错误', msg)

        # 正常修改密码
        ok, new_token = database.change_user_password('AubreyHan', 'P@ss=w0rd', 'NewPwd#2026')
        self.assertTrue(ok)
        self.assertTrue(len(new_token) > 20)

        # 验证旧密码失效，新密码生效
        self.assertIsNone(database.verify_user('AubreyHan', 'P@ss=w0rd'))
        new_login = database.verify_user('AubreyHan', 'NewPwd#2026')
        self.assertIsNotNone(new_login)
        self.assertEqual(new_login['token'], new_token)

        # 5. 还原密码以便其他用例或系统平滑运行
        ok, _ = database.change_user_password('AubreyHan', 'NewPwd#2026', 'P@ss=w0rd')
        self.assertTrue(ok)
        self.assertIsNotNone(database.verify_user('AubreyHan', 'P@ss=w0rd'))


