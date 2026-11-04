-- ==============================================================================
-- 我的车管家 · Cloudflare D1 一站式建表与现有全量数据导入脚本 (All-In-One SQL)
-- 包含：表结构定义 (Schema) + 现有车辆与全部历史记录 (Seed Data)
-- 在 Cloudflare D1 控制台直接全选复制并执行本文件即可一次性完成所有初始化！
-- ==============================================================================

-- ==========================================================
-- Cloudflare D1 数据库初始化表结构 (Car Tracker D1 Schema)
-- ==========================================================

-- 1. 车辆信息表
CREATE TABLE IF NOT EXISTS vehicles (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL,                     -- 车名 / 昵称 (如: 我的通勤车)
    brand TEXT NOT NULL,                    -- 品牌 (如: 比亚迪, 特斯拉, 丰田)
    model TEXT NOT NULL,                    -- 型号 (如: 汉EV, 秦PLUS, Model Y)
    power_type TEXT NOT NULL DEFAULT 'gas', -- 动力类型: gas, ev, phev, erev, hybrid
    displacement TEXT,                      -- 排量 (如: 2.0T, 1.5L)
    fuel_tank_capacity REAL DEFAULT 0,      -- 油箱容量 (L)
    battery_capacity REAL DEFAULT 0,        -- 电池容量 (kWh)
    plate_number TEXT,                      -- 车牌号
    initial_mileage REAL NOT NULL DEFAULT 0,-- 初始里程 (km)
    current_mileage REAL NOT NULL DEFAULT 0,-- 当前最新里程 (km)
    initial_fuel_percent REAL DEFAULT 0,    -- 初始油量百分比 (%)
    initial_battery_percent REAL DEFAULT 0, -- 初始电量百分比 (%)
    purchase_date TEXT,                     -- 购车日期
    notes TEXT,                             -- 备注
    created_at TEXT NOT NULL,
    updated_at TEXT NOT NULL
);

-- 2. 油耗录入表
CREATE TABLE IF NOT EXISTS fuel_records (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    vehicle_id INTEGER NOT NULL,
    mileage REAL NOT NULL,                  -- 当前里程 (km)
    fuel_amount REAL NOT NULL,              -- 加油量 (L)
    unit_price REAL NOT NULL,               -- 单价 (元/L)
    total_price REAL NOT NULL,              -- 总价 (元)
    fuel_type TEXT NOT NULL DEFAULT '92#',  -- 油品 (92#, 95#, 98#, 0#柴油 等)
    is_full INTEGER DEFAULT 1,              -- 是否加满 (1: 是, 0: 否)
    start_fuel_percent REAL,                -- 加油前初始起步油量 (%)
    fuel_percent REAL DEFAULT 100,          -- 加油后油量百分比 (%)
    battery_percent REAL,                   -- 加油时当前电量/剩余电量 (%) (混动/增程车专用)
    station TEXT,                           -- 加油站
    record_date TEXT NOT NULL,              -- 记录时间
    notes TEXT,                             -- 备注
    created_at TEXT NOT NULL,
    FOREIGN KEY (vehicle_id) REFERENCES vehicles (id) ON DELETE CASCADE
);
CREATE INDEX IF NOT EXISTS idx_fuel_vehicle_mileage ON fuel_records (vehicle_id, mileage ASC);

-- 3. 电耗录入表
CREATE TABLE IF NOT EXISTS electricity_records (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    vehicle_id INTEGER NOT NULL,
    mileage REAL NOT NULL,                  -- 当前里程 (km)
    electricity_amount REAL NOT NULL,       -- 加电量 (kWh)
    unit_price REAL NOT NULL,               -- 单价 (元/度)
    total_price REAL NOT NULL,              -- 总价 (元)
    charging_type TEXT NOT NULL DEFAULT '快充', -- 充电类型
    start_soc REAL,                         -- 起始电量 (%)
    end_soc REAL,                           -- 结束电量 (%)
    fuel_percent REAL,                      -- 充电时油量百分比 (%) (混动/增程专用)
    station TEXT,                           -- 充电站
    record_date TEXT NOT NULL,              -- 记录时间
    notes TEXT,                             -- 备注
    created_at TEXT NOT NULL,
    FOREIGN KEY (vehicle_id) REFERENCES vehicles (id) ON DELETE CASCADE
);
CREATE INDEX IF NOT EXISTS idx_elec_vehicle_mileage ON electricity_records (vehicle_id, mileage ASC);

-- 4. 维护与保养记录表
CREATE TABLE IF NOT EXISTS maintenance_records (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    vehicle_id INTEGER NOT NULL,
    mileage REAL NOT NULL,                  -- 发生时里程 (km)
    item_type TEXT NOT NULL,                -- 维护项目
    cost REAL NOT NULL DEFAULT 0,           -- 费用 (元)
    provider TEXT,                          -- 服务商 / 4S店
    record_date TEXT NOT NULL,              -- 记录日期
    next_mileage_reminder REAL,             -- 下次建议里程
    next_date_reminder TEXT,                -- 下次建议日期
    notes TEXT,                             -- 备注
    created_at TEXT NOT NULL,
    FOREIGN KEY (vehicle_id) REFERENCES vehicles (id) ON DELETE CASCADE
);
CREATE INDEX IF NOT EXISTS idx_maint_vehicle_date ON maintenance_records (vehicle_id, record_date DESC);


-- ==============================================================================
-- 以下为现有本地真实数据导入
-- ==============================================================================

-- =============================================
-- 我的车管家 · 现有本地数据完整迁移脚本 (Seed Data)
-- 自动从本地 SQLite (car_tracker.db) 导出
-- =============================================

-- 表: vehicles (1 笔数据)
INSERT INTO vehicles (id, name, brand, model, power_type, displacement, fuel_tank_capacity, battery_capacity, plate_number, initial_mileage, current_mileage, purchase_date, notes, created_at, updated_at, initial_fuel_percent, initial_battery_percent) VALUES (15, '京AFY6227', '理想', '26款L9 Livis', 'erev', '1.5T', 60.0, 72.7, '京AFY6227', 15.0, 309.0, '2026-09-06', '', '2026-09-20 21:36:53', '2026-09-21 08:42:48', 8.0, 25.0);

-- 表: fuel_records (1 笔数据)
INSERT INTO fuel_records (id, vehicle_id, mileage, fuel_amount, unit_price, total_price, fuel_type, is_full, station, record_date, notes, created_at, fuel_percent, start_fuel_percent, battery_percent) VALUES (4, 15, 70.0, 57.07, 7.242, 413.31, '95#', 1, '海湾石油北苑路站', '2026-09-08 22:18', '', '2026-09-20 22:20:51', 100.0, 3.0, 79.0);

-- 表: electricity_records (4 笔数据)
INSERT INTO electricity_records (id, vehicle_id, mileage, electricity_amount, unit_price, total_price, charging_type, start_soc, end_soc, station, record_date, notes, created_at, fuel_percent) VALUES (20, 15, 68.0, 47.98, 1.372, 65.81, '快充 (DC直流)', 14.0, 80.0, '理想5C超充站 理想汽车北京来广营汽车城零售中心', '2026-09-08 19:49', '', '2026-09-20 21:50:41', 3.0);
INSERT INTO electricity_records (id, vehicle_id, mileage, electricity_amount, unit_price, total_price, charging_type, start_soc, end_soc, station, record_date, notes, created_at, fuel_percent) VALUES (25, 15, 84.0, 22.48, 1.278, 28.73, '快充 (DC直流)', 51.0, 82.0, '理想5C超充站 理想汽车北京来广营汽车城零售中心', '2026-09-12 22:18', '', '2026-09-20 22:22:25', 100.0);
INSERT INTO electricity_records (id, vehicle_id, mileage, electricity_amount, unit_price, total_price, charging_type, start_soc, end_soc, station, record_date, notes, created_at, fuel_percent) VALUES (26, 15, 290.0, 44.38, 1.288, 57.14, '快充 (DC直流)', 27.0, 88.0, '理想5C超充站 大广高速土沟服务区（广州方向）', '2026-09-13 22:18', '', '2026-09-20 22:23:53', 96.0);
INSERT INTO electricity_records (id, vehicle_id, mileage, electricity_amount, unit_price, total_price, charging_type, start_soc, end_soc, station, record_date, notes, created_at, fuel_percent) VALUES (28, 15, 309.0, 18.4, 0.428, 7.88, '快充 (DC直流)', 68.0, 93.3, '理想5C超充站 北京朝阳区秋实街（近望春园）', '2026-09-18 07:51', '', '2026-09-21 07:59:07', 96.0);

-- 表: maintenance_records (29 笔数据)
INSERT INTO maintenance_records (id, vehicle_id, mileage, item_type, cost, provider, record_date, next_mileage_reminder, next_date_reminder, notes, created_at) VALUES (4, 15, 309.0, '保险', 10417.06, '', '2026-09-06', NULL, '', '', '2026-09-21 06:13:43');
INSERT INTO maintenance_records (id, vehicle_id, mileage, item_type, cost, provider, record_date, next_mileage_reminder, next_date_reminder, notes, created_at) VALUES (5, 15, 309.0, '维修保养', 16998.5, '', '2026-09-06', NULL, '', '延保到8年', '2026-09-21 08:01:42');
INSERT INTO maintenance_records (id, vehicle_id, mileage, item_type, cost, provider, record_date, next_mileage_reminder, next_date_reminder, notes, created_at) VALUES (6, 15, 309.0, '维修保养', 1319.11, '', '2026-09-06', NULL, '', '脚垫', '2026-09-21 08:02:31');
INSERT INTO maintenance_records (id, vehicle_id, mileage, item_type, cost, provider, record_date, next_mileage_reminder, next_date_reminder, notes, created_at) VALUES (7, 15, 309.0, '维修保养', 112.9, '', '2026-09-06', NULL, '', 'hud盖', '2026-09-21 08:03:55');
INSERT INTO maintenance_records (id, vehicle_id, mileage, item_type, cost, provider, record_date, next_mileage_reminder, next_date_reminder, notes, created_at) VALUES (8, 15, 309.0, '维修保养', 256.97, '', '2026-09-06', NULL, '', '二排充电扩展坞', '2026-09-21 08:04:48');
INSERT INTO maintenance_records (id, vehicle_id, mileage, item_type, cost, provider, record_date, next_mileage_reminder, next_date_reminder, notes, created_at) VALUES (9, 15, 309.0, '维修保养', 120.0, '', '2026-09-06', NULL, '', '后备箱储物盒', '2026-09-21 08:05:22');
INSERT INTO maintenance_records (id, vehicle_id, mileage, item_type, cost, provider, record_date, next_mileage_reminder, next_date_reminder, notes, created_at) VALUES (10, 15, 309.0, '维修保养', 123.0, '', '2026-09-06', NULL, '', '冰箱助推器', '2026-09-21 08:06:01');
INSERT INTO maintenance_records (id, vehicle_id, mileage, item_type, cost, provider, record_date, next_mileage_reminder, next_date_reminder, notes, created_at) VALUES (11, 15, 309.0, '维修保养', 2175.17, '', '2026-09-07', NULL, '', 'livis眼镜', '2026-09-21 08:07:00');
INSERT INTO maintenance_records (id, vehicle_id, mileage, item_type, cost, provider, record_date, next_mileage_reminder, next_date_reminder, notes, created_at) VALUES (12, 15, 309.0, '停车', 1.5, '', '2026-09-08', NULL, '', '', '2026-09-21 08:07:56');
INSERT INTO maintenance_records (id, vehicle_id, mileage, item_type, cost, provider, record_date, next_mileage_reminder, next_date_reminder, notes, created_at) VALUES (13, 15, 309.0, '维修保养', 699.0, '', '2026-09-09', NULL, '', '行车记录仪u盘', '2026-09-21 08:08:55');
INSERT INTO maintenance_records (id, vehicle_id, mileage, item_type, cost, provider, record_date, next_mileage_reminder, next_date_reminder, notes, created_at) VALUES (14, 15, 309.0, '维修保养', 396.6, '', '2026-09-09', NULL, '', '内饰硅胶', '2026-09-21 08:10:03');
INSERT INTO maintenance_records (id, vehicle_id, mileage, item_type, cost, provider, record_date, next_mileage_reminder, next_date_reminder, notes, created_at) VALUES (15, 15, 309.0, '维修保养', 275.0, '', '2026-09-10', NULL, '', '二排充电扩展坞二代', '2026-09-21 08:10:49');
INSERT INTO maintenance_records (id, vehicle_id, mileage, item_type, cost, provider, record_date, next_mileage_reminder, next_date_reminder, notes, created_at) VALUES (16, 15, 309.0, '维修保养', 308.9, '', '2026-09-12', NULL, '', '遥控器等', '2026-09-21 08:33:33');
INSERT INTO maintenance_records (id, vehicle_id, mileage, item_type, cost, provider, record_date, next_mileage_reminder, next_date_reminder, notes, created_at) VALUES (17, 15, 309.0, '维修保养', 128.0, '', '2026-09-12', NULL, '', '运动相机外挂', '2026-09-21 08:34:06');
INSERT INTO maintenance_records (id, vehicle_id, mileage, item_type, cost, provider, record_date, next_mileage_reminder, next_date_reminder, notes, created_at) VALUES (18, 15, 309.0, '维修保养', 199.0, '', '2026-09-12', NULL, '', '采集卡', '2026-09-21 08:34:56');
INSERT INTO maintenance_records (id, vehicle_id, mileage, item_type, cost, provider, record_date, next_mileage_reminder, next_date_reminder, notes, created_at) VALUES (19, 15, 309.0, '维修保养', 1699.0, '', '2026-09-12', NULL, '', '三年服务包', '2026-09-21 08:35:24');
INSERT INTO maintenance_records (id, vehicle_id, mileage, item_type, cost, provider, record_date, next_mileage_reminder, next_date_reminder, notes, created_at) VALUES (20, 15, 309.0, '维修保养', 41.6, '', '2026-09-12', NULL, '', '眼镜夹', '2026-09-21 08:35:57');
INSERT INTO maintenance_records (id, vehicle_id, mileage, item_type, cost, provider, record_date, next_mileage_reminder, next_date_reminder, notes, created_at) VALUES (21, 15, 309.0, '停车', 1.5, '', '2026-09-12', NULL, '', '', '2026-09-21 08:36:26');
INSERT INTO maintenance_records (id, vehicle_id, mileage, item_type, cost, provider, record_date, next_mileage_reminder, next_date_reminder, notes, created_at) VALUES (22, 15, 309.0, '过桥过路费', 38.0, '', '2026-09-13', NULL, '', '', '2026-09-21 08:37:02');
INSERT INTO maintenance_records (id, vehicle_id, mileage, item_type, cost, provider, record_date, next_mileage_reminder, next_date_reminder, notes, created_at) VALUES (23, 15, 309.0, '维修保养', 68.41, '', '2026-09-14', NULL, '', '玻璃水', '2026-09-21 08:37:33');
INSERT INTO maintenance_records (id, vehicle_id, mileage, item_type, cost, provider, record_date, next_mileage_reminder, next_date_reminder, notes, created_at) VALUES (24, 15, 309.0, '维修保养', 162.24, '', '2026-09-14', NULL, '', '前挡玻璃盖', '2026-09-21 08:38:09');
INSERT INTO maintenance_records (id, vehicle_id, mileage, item_type, cost, provider, record_date, next_mileage_reminder, next_date_reminder, notes, created_at) VALUES (25, 15, 309.0, '维修保养', 17.3, '', '2026-09-15', NULL, '', '折叠垃圾桶', '2026-09-21 08:38:46');
INSERT INTO maintenance_records (id, vehicle_id, mileage, item_type, cost, provider, record_date, next_mileage_reminder, next_date_reminder, notes, created_at) VALUES (26, 15, 309.0, '税', 28345.13, '', '2026-09-18', NULL, '', '购置税', '2026-09-21 08:39:39');
INSERT INTO maintenance_records (id, vehicle_id, mileage, item_type, cost, provider, record_date, next_mileage_reminder, next_date_reminder, notes, created_at) VALUES (27, 15, 309.0, '停车', 38.0, '', '2026-09-18', NULL, '', '', '2026-09-21 08:40:01');
INSERT INTO maintenance_records (id, vehicle_id, mileage, item_type, cost, provider, record_date, next_mileage_reminder, next_date_reminder, notes, created_at) VALUES (28, 15, 309.0, '违章', 99.88, '', '2026-09-18', NULL, '', '', '2026-09-21 08:40:28');
INSERT INTO maintenance_records (id, vehicle_id, mileage, item_type, cost, provider, record_date, next_mileage_reminder, next_date_reminder, notes, created_at) VALUES (29, 15, 309.0, '停车', 4.0, '', '2026-09-19', NULL, '', '', '2026-09-21 08:41:05');
INSERT INTO maintenance_records (id, vehicle_id, mileage, item_type, cost, provider, record_date, next_mileage_reminder, next_date_reminder, notes, created_at) VALUES (30, 15, 309.0, '过桥过路费', 50.0, '', '2026-09-19', NULL, '', 'etc开通', '2026-09-21 08:41:36');
INSERT INTO maintenance_records (id, vehicle_id, mileage, item_type, cost, provider, record_date, next_mileage_reminder, next_date_reminder, notes, created_at) VALUES (31, 15, 309.0, '维修保养', 997.59, '', '2026-09-20', NULL, '', '吉安特储值', '2026-09-21 08:42:16');
INSERT INTO maintenance_records (id, vehicle_id, mileage, item_type, cost, provider, record_date, next_mileage_reminder, next_date_reminder, notes, created_at) VALUES (32, 15, 309.0, '维修保养', 224.0, '', '2026-09-20', NULL, '', '屏幕膜和防虫网', '2026-09-21 08:42:48');

