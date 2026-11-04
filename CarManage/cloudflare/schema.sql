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
    current_fuel_percent REAL DEFAULT NULL, -- 当前最新油量百分比 (%)
    current_battery_percent REAL DEFAULT NULL, -- 当前最新电量百分比 (%)
    purchase_date TEXT,                     -- 购车日期
    notes TEXT,                             -- 备注
    created_at TEXT NOT NULL,
    updated_at TEXT NOT NULL,
    sync_version INTEGER DEFAULT 1
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
    updated_at TEXT,
    sync_version INTEGER DEFAULT 1,
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
    is_calibrated INTEGER DEFAULT 0,        -- 是否为电池校准记录 (1: 是, 0: 否)
    calibrated_capacity REAL,               -- 经由此记录校准的电池容量 (kWh)
    record_date TEXT NOT NULL,              -- 记录时间
    notes TEXT,                             -- 备注
    created_at TEXT NOT NULL,
    updated_at TEXT,
    sync_version INTEGER DEFAULT 1,
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
    updated_at TEXT,
    sync_version INTEGER DEFAULT 1,
    FOREIGN KEY (vehicle_id) REFERENCES vehicles (id) ON DELETE CASCADE
);
CREATE INDEX IF NOT EXISTS idx_maint_vehicle_date ON maintenance_records (vehicle_id, record_date DESC);

-- 5. 车辆当前状态/表显快照记录表 (支持在历史明细中查看与修改)
CREATE TABLE IF NOT EXISTS status_records (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    vehicle_id INTEGER NOT NULL,
    mileage REAL NOT NULL,                  -- 录入时的里程 (km)
    battery_percent REAL DEFAULT NULL,      -- 录入时的电量 (%)
    fuel_percent REAL DEFAULT NULL,         -- 录入时的油量 (%)
    record_date TEXT NOT NULL,              -- 记录时间
    notes TEXT DEFAULT '',                  -- 备注
    created_at TEXT NOT NULL,
    updated_at TEXT,
    sync_version INTEGER DEFAULT 1,
    FOREIGN KEY (vehicle_id) REFERENCES vehicles (id) ON DELETE CASCADE
);
CREATE INDEX IF NOT EXISTS idx_status_vehicle_date ON status_records (vehicle_id, mileage DESC, record_date DESC);

-- 6. 用户认证与管理表
CREATE TABLE IF NOT EXISTS users (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    username TEXT UNIQUE NOT NULL,
    password_hash TEXT NOT NULL,
    salt TEXT NOT NULL,
    created_at TEXT NOT NULL,
    updated_at TEXT NOT NULL
);

