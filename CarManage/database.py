import sqlite3
import os
import hashlib
from datetime import datetime
from typing import List, Dict, Any, Optional, Tuple

AUTH_SALT_DEFAULT = 'car_tracker_salt_2026'
AUTH_TOKEN_KEY = 'car_tracker_auth_token_secret_key_2026'

DB_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'car_tracker.db')

def get_db_connection():
    conn = sqlite3.connect(DB_FILE)
    conn.row_factory = sqlite3.Row
    return conn

def init_db():
    """初始化数据库与所有数据表"""
    conn = get_db_connection()
    cursor = conn.cursor()

    # 1. 车辆表
    cursor.execute('''
        CREATE TABLE IF NOT EXISTS vehicles (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT NOT NULL,                     -- 车名 / 昵称 (如: 我的通勤车)
            brand TEXT NOT NULL,                    -- 品牌 (如: 比亚迪, 特斯拉, 丰田)
            model TEXT NOT NULL,                    -- 型号 (如: 汉EV, 秦PLUS, Model Y)
            power_type TEXT NOT NULL DEFAULT 'gas', -- 动力类型: gas (燃油), ev (纯电动), phev (插电混动), erev (增程式), hybrid (油电混动)
            displacement TEXT,                      -- 排量 (如: 2.0T, 1.5L, 纯电可为空)
            fuel_tank_capacity REAL DEFAULT 0,      -- 油箱容量 (升, L)
            battery_capacity REAL DEFAULT 0,        -- 电池容量 (度, kWh)
            plate_number TEXT,                      -- 车牌号 (如: 粤B12345)
            initial_mileage REAL NOT NULL DEFAULT 0,-- 初始里程 (km)
            current_mileage REAL NOT NULL DEFAULT 0,-- 当前最新里程 (km)
            initial_fuel_percent REAL DEFAULT 0,    -- 初始油量百分比 (%)
            initial_battery_percent REAL DEFAULT 0, -- 初始电量百分比 (%)
            current_fuel_percent REAL DEFAULT NULL, -- 当前最新油量百分比 (%)
            current_battery_percent REAL DEFAULT NULL, -- 当前最新电量百分比 (%)
            purchase_date TEXT,                     -- 购车日期
            notes TEXT,                             -- 备注
            created_at TEXT NOT NULL,
            updated_at TEXT NOT NULL
        )
    ''')

    # 数据库平滑迁移检查 (补充添加当前状态字段)
    cursor.execute("PRAGMA table_info(vehicles)")
    columns = [col[1] for col in cursor.fetchall()]
    if 'current_fuel_percent' not in columns:
        cursor.execute("ALTER TABLE vehicles ADD COLUMN current_fuel_percent REAL")
    if 'current_battery_percent' not in columns:
        cursor.execute("ALTER TABLE vehicles ADD COLUMN current_battery_percent REAL")

    # 2. 油耗录入表
    cursor.execute('''
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
            record_date TEXT NOT NULL,              -- 记录时间 (YYYY-MM-DD HH:MM)
            notes TEXT,                             -- 备注
            created_at TEXT NOT NULL,
            FOREIGN KEY (vehicle_id) REFERENCES vehicles (id) ON DELETE CASCADE
        )
    ''')

    # 3. 电耗录入表
    cursor.execute('''
        CREATE TABLE IF NOT EXISTS electricity_records (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            vehicle_id INTEGER NOT NULL,
            mileage REAL NOT NULL,                  -- 当前里程 (km)
            electricity_amount REAL NOT NULL,       -- 加电量 (kWh / 度)
            unit_price REAL NOT NULL,               -- 单价 (元/度)
            total_price REAL NOT NULL,              -- 总价 (元)
            charging_type TEXT NOT NULL DEFAULT '快充', -- 充电类型/规格 (快充, 慢充, 家充桩, 超充 等)
            start_soc REAL,                         -- 起始电量 (%)
            end_soc REAL,                           -- 结束电量 (%)
            fuel_percent REAL,                      -- 充电时油量百分比 (%) (混动/增程车专用)
            station TEXT,                           -- 充电站名称
            is_calibrated INTEGER DEFAULT 0,        -- 是否电池校准 (0: 否, 1: 是)
            calibrated_capacity REAL,               -- 本次校准计算出的电池容量 (kWh)
            record_date TEXT NOT NULL,              -- 记录时间 (YYYY-MM-DD HH:MM)
            notes TEXT,                             -- 备注
            created_at TEXT NOT NULL,
            FOREIGN KEY (vehicle_id) REFERENCES vehicles (id) ON DELETE CASCADE
        )
    ''')

    # 4. 维护与保养记录表
    cursor.execute('''
        CREATE TABLE IF NOT EXISTS maintenance_records (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            vehicle_id INTEGER NOT NULL,
            mileage REAL NOT NULL,                  -- 发生时里程 (km)
            item_type TEXT NOT NULL,                -- 维护项目 (常规保养, 更换机油机滤, 更换刹车片, 轮胎更换, 年检, 保险, 其他维修)
            cost REAL NOT NULL DEFAULT 0,           -- 费用 (元)
            provider TEXT,                          -- 服务商 / 4S店 / 修理厂
            record_date TEXT NOT NULL,              -- 记录日期
            next_mileage_reminder REAL,             -- 下次建议里程
            next_date_reminder TEXT,                -- 下次建议日期
            notes TEXT,                             -- 维护详情/配件规格
            created_at TEXT NOT NULL,
            FOREIGN KEY (vehicle_id) REFERENCES vehicles (id) ON DELETE CASCADE
        )
    ''')

    # 动态升级车辆表及记录表字段 (若字段不存在则添加)
    cursor.execute("PRAGMA table_info(vehicles)")
    columns = [col[1] for col in cursor.fetchall()]
    if 'initial_fuel_percent' not in columns:
        cursor.execute("ALTER TABLE vehicles ADD COLUMN initial_fuel_percent REAL DEFAULT 0")
    if 'initial_battery_percent' not in columns:
        cursor.execute("ALTER TABLE vehicles ADD COLUMN initial_battery_percent REAL DEFAULT 0")

    cursor.execute("PRAGMA table_info(fuel_records)")
    fuel_cols = [col[1] for col in cursor.fetchall()]
    if 'fuel_percent' not in fuel_cols:
        cursor.execute("ALTER TABLE fuel_records ADD COLUMN fuel_percent REAL DEFAULT 100")
    if 'start_fuel_percent' not in fuel_cols:
        cursor.execute("ALTER TABLE fuel_records ADD COLUMN start_fuel_percent REAL")
    if 'battery_percent' not in fuel_cols:
        cursor.execute("ALTER TABLE fuel_records ADD COLUMN battery_percent REAL")

    cursor.execute("PRAGMA table_info(electricity_records)")
    elec_cols = [col[1] for col in cursor.fetchall()]
    if 'fuel_percent' not in elec_cols:
        cursor.execute("ALTER TABLE electricity_records ADD COLUMN fuel_percent REAL")
    if 'is_calibrated' not in elec_cols:
        cursor.execute("ALTER TABLE electricity_records ADD COLUMN is_calibrated INTEGER DEFAULT 0")
    if 'calibrated_capacity' not in elec_cols:
        cursor.execute("ALTER TABLE electricity_records ADD COLUMN calibrated_capacity REAL")

    # 5. 车辆当前状态/表显快照记录表 (支持在历史明细中查看与修改)
    cursor.execute('''
        CREATE TABLE IF NOT EXISTS status_records (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            vehicle_id INTEGER NOT NULL,
            mileage REAL NOT NULL,                  -- 录入时的里程 (km)
            battery_percent REAL,                   -- 录入时的电量 (%)
            fuel_percent REAL,                      -- 录入时的油量 (%)
            record_date TEXT NOT NULL,              -- 记录时间 (YYYY-MM-DD HH:MM)
            notes TEXT,                             -- 备注
            created_at TEXT NOT NULL,
            FOREIGN KEY (vehicle_id) REFERENCES vehicles (id) ON DELETE CASCADE
        )
    ''')

    # 为已有当前里程但尚无 status_records 的车辆生成初始状态快照
    try:
        cursor.execute('''
            SELECT v.id, v.current_mileage, v.current_battery_percent, v.current_fuel_percent, v.updated_at, v.created_at
            FROM vehicles v
            WHERE (v.current_mileage > v.initial_mileage OR v.current_battery_percent IS NOT NULL OR v.current_fuel_percent IS NOT NULL)
              AND NOT EXISTS (SELECT 1 FROM status_records s WHERE s.vehicle_id = v.id)
        ''')
        unseeded = cursor.fetchall()
        for row in unseeded:
            v_id, cm, cb, cf, up_at, cr_at = row
            r_date = (up_at or cr_at or datetime.now().strftime('%Y-%m-%d %H:%M'))[:16]
            cursor.execute('''
                INSERT INTO status_records (vehicle_id, mileage, battery_percent, fuel_percent, record_date, notes, created_at)
                VALUES (?, ?, ?, ?, ?, '当前车辆最新状态', ?)
            ''', (v_id, cm, cb, cf, r_date, up_at or cr_at or datetime.now().strftime('%Y-%m-%d %H:%M:%S')))
    except Exception:
        pass

    # 6. 用户认证与管理表
    cursor.execute('''
        CREATE TABLE IF NOT EXISTS users (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            username TEXT UNIQUE NOT NULL,
            password_hash TEXT NOT NULL,
            salt TEXT NOT NULL,
            created_at TEXT NOT NULL,
            updated_at TEXT NOT NULL
        )
    ''')

    # 初始化默认用户 AubreyHan
    cursor.execute("SELECT id FROM users WHERE username = ?", ('AubreyHan',))
    if not cursor.fetchone():
        salt = AUTH_SALT_DEFAULT
        now = datetime.now().strftime('%Y-%m-%d %H:%M:%S')
        pwd_hash = hashlib.sha256(('P@ss=w0rd' + salt).encode('utf-8')).hexdigest()
        cursor.execute('''
            INSERT INTO users (username, password_hash, salt, created_at, updated_at)
            VALUES (?, ?, ?, ?, ?)
        ''', ('AubreyHan', pwd_hash, salt, now, now))

    conn.commit()
    conn.close()

# ----------------- 用户认证与密码管理 -----------------

def _hash_password(password: str, salt: str = AUTH_SALT_DEFAULT) -> str:
    return hashlib.sha256((password + salt).encode('utf-8')).hexdigest()

def _make_auth_token(username: str, password_hash: str) -> str:
    return hashlib.sha256((username + ':' + password_hash + ':' + AUTH_TOKEN_KEY).encode('utf-8')).hexdigest()

def verify_user(username: str, password: str) -> Optional[Dict[str, Any]]:
    """验证用户名与密码"""
    conn = get_db_connection()
    cursor = conn.cursor()
    cursor.execute("SELECT * FROM users WHERE username = ?", (username,))
    user = cursor.fetchone()
    conn.close()
    if not user:
        return None
    salt = user['salt'] or AUTH_SALT_DEFAULT
    if _hash_password(password, salt) == user['password_hash']:
        token = _make_auth_token(user['username'], user['password_hash'])
        return {"username": user['username'], "token": token}
    return None

def verify_token(token: str) -> Optional[Dict[str, Any]]:
    """验证登录 Token (无主动登出则永久有效)"""
    if not token:
        return None
    conn = get_db_connection()
    cursor = conn.cursor()
    cursor.execute("SELECT username, password_hash FROM users")
    users = cursor.fetchall()
    conn.close()
    for u in users:
        expected = _make_auth_token(u['username'], u['password_hash'])
        if expected == token:
            return {"username": u['username']}
    return None

def change_user_password(username: str, old_password: str, new_password: str) -> Tuple[bool, str]:
    """修改指定用户的登录密码"""
    conn = get_db_connection()
    cursor = conn.cursor()
    cursor.execute("SELECT * FROM users WHERE username = ?", (username,))
    user = cursor.fetchone()
    if not user:
        conn.close()
        return False, "用户不存在"
    salt = user['salt'] or AUTH_SALT_DEFAULT
    if _hash_password(old_password, salt) != user['password_hash']:
        conn.close()
        return False, "原密码错误"

    now = datetime.now().strftime('%Y-%m-%d %H:%M:%S')
    new_hash = _hash_password(new_password, salt)
    cursor.execute('''
        UPDATE users SET password_hash = ?, updated_at = ? WHERE id = ?
    ''', (new_hash, now, user['id']))
    conn.commit()
    conn.close()
    new_token = _make_auth_token(username, new_hash)
    return True, new_token

def get_user_by_username(username: str) -> Optional[Dict[str, Any]]:
    conn = get_db_connection()
    cursor = conn.cursor()
    cursor.execute("SELECT id, username, created_at, updated_at FROM users WHERE username = ?", (username,))
    row = cursor.fetchone()
    conn.close()
    return dict(row) if row else None

# ----------------- 车辆数据操作 -----------------

def get_all_vehicles() -> List[Dict[str, Any]]:
    conn = get_db_connection()
    cursor = conn.cursor()
    cursor.execute('SELECT * FROM vehicles ORDER BY id DESC')
    rows = cursor.fetchall()
    vehicles = [dict(row) for row in rows]
    conn.close()
    return vehicles

def get_vehicle_by_id(vehicle_id: int) -> Optional[Dict[str, Any]]:
    conn = get_db_connection()
    cursor = conn.cursor()
    cursor.execute('SELECT * FROM vehicles WHERE id = ?', (vehicle_id,))
    row = cursor.fetchone()
    conn.close()
    return dict(row) if row else None

def create_vehicle(data: Dict[str, Any]) -> int:
    conn = get_db_connection()
    cursor = conn.cursor()
    now = datetime.now().strftime('%Y-%m-%d %H:%M:%S')
    initial_mileage = float(data.get('initial_mileage', 0) or 0)
    current_mileage = float(data.get('current_mileage', initial_mileage) or initial_mileage)

    init_fuel = float(data.get('initial_fuel_percent', 0) or 0)
    init_bat = float(data.get('initial_battery_percent', 0) or 0)
    curr_fuel = float(data['current_fuel_percent']) if data.get('current_fuel_percent') is not None and data.get('current_fuel_percent') != '' else init_fuel
    curr_bat = float(data['current_battery_percent']) if data.get('current_battery_percent') is not None and data.get('current_battery_percent') != '' else init_bat

    cursor.execute('''
        INSERT INTO vehicles (
            name, brand, model, power_type, displacement,
            fuel_tank_capacity, battery_capacity, plate_number,
            initial_mileage, current_mileage, purchase_date, notes,
            initial_fuel_percent, initial_battery_percent,
            current_fuel_percent, current_battery_percent,
            created_at, updated_at
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
    ''', (
        data.get('name', '未命名车辆'),
        data.get('brand', ''),
        data.get('model', ''),
        data.get('power_type', 'gas'),
        data.get('displacement', ''),
        float(data.get('fuel_tank_capacity', 0) or 0),
        float(data.get('battery_capacity', 0) or 0),
        data.get('plate_number', ''),
        initial_mileage,
        current_mileage,
        data.get('purchase_date', ''),
        data.get('notes', ''),
        init_fuel,
        init_bat,
        curr_fuel,
        curr_bat,
        now,
        now
    ))
    vehicle_id = cursor.lastrowid
    conn.commit()
    conn.close()
    return vehicle_id

def update_vehicle(vehicle_id: int, data: Dict[str, Any]) -> bool:
    v_old = get_vehicle_by_id(vehicle_id)
    if not v_old:
        return False

    conn = get_db_connection()
    cursor = conn.cursor()
    now = datetime.now().strftime('%Y-%m-%d %H:%M:%S')
    
    # 提取当前状态三项 (里程、油量%、电量%)
    extra_sets = []
    extra_vals = []

    curr_m = data.get('current_mileage')
    if curr_m is not None and curr_m != '':
        try:
            curr_m = float(curr_m)
            extra_sets.append('current_mileage = ?')
            extra_vals.append(curr_m)
        except (ValueError, TypeError):
            pass

    curr_fuel = data.get('current_fuel_percent')
    if curr_fuel is not None and curr_fuel != '':
        try:
            curr_fuel = float(curr_fuel)
            extra_sets.append('current_fuel_percent = ?')
            extra_vals.append(curr_fuel)
        except (ValueError, TypeError):
            pass

    curr_bat = data.get('current_battery_percent')
    if curr_bat is not None and curr_bat != '':
        try:
            curr_bat = float(curr_bat)
            extra_sets.append('current_battery_percent = ?')
            extra_vals.append(curr_bat)
        except (ValueError, TypeError):
            pass

    extra_sql = (', ' + ', '.join(extra_sets)) if extra_sets else ''

    cursor.execute('''
        UPDATE vehicles SET
            name = ?, brand = ?, model = ?, power_type = ?, displacement = ?,
            fuel_tank_capacity = ?, battery_capacity = ?, plate_number = ?,
            initial_mileage = ?, purchase_date = ?, notes = ?,
            initial_fuel_percent = ?, initial_battery_percent = ?, updated_at = ?
            ''' + extra_sql + '''
        WHERE id = ?
    ''', tuple([
        data.get('name') if data.get('name') is not None else v_old.get('name'),
        data.get('brand') if data.get('brand') is not None else v_old.get('brand'),
        data.get('model') if data.get('model') is not None else v_old.get('model'),
        data.get('power_type') if data.get('power_type') is not None else v_old.get('power_type'),
        data.get('displacement') if data.get('displacement') is not None else v_old.get('displacement'),
        float(data.get('fuel_tank_capacity') if data.get('fuel_tank_capacity') is not None else (v_old.get('fuel_tank_capacity') or 0)),
        float(data.get('battery_capacity') if data.get('battery_capacity') is not None else (v_old.get('battery_capacity') or 0)),
        data.get('plate_number') if data.get('plate_number') is not None else v_old.get('plate_number'),
        float(data.get('initial_mileage') if data.get('initial_mileage') is not None else (v_old.get('initial_mileage') or 0)),
        data.get('purchase_date') if data.get('purchase_date') is not None else v_old.get('purchase_date'),
        data.get('notes') if data.get('notes') is not None else v_old.get('notes'),
        float(data.get('initial_fuel_percent') if data.get('initial_fuel_percent') is not None else (v_old.get('initial_fuel_percent') or 0)),
        float(data.get('initial_battery_percent') if data.get('initial_battery_percent') is not None else (v_old.get('initial_battery_percent') or 0)),
        now
    ] + extra_vals + [vehicle_id]))
    updated = cursor.rowcount > 0
    conn.commit()
    conn.close()
    sync_vehicle_state(vehicle_id)
    return updated

def delete_vehicle(vehicle_id: int) -> bool:
    conn = get_db_connection()
    cursor = conn.cursor()
    cursor.execute('DELETE FROM vehicles WHERE id = ?', (vehicle_id,))
    deleted = cursor.rowcount > 0
    conn.commit()
    conn.close()
    return deleted

def sync_vehicle_state(vehicle_id: int):
    """根据最新油耗、电耗、保养记录或手动录入的当前状态刷新车辆的当前最新状态 (里程、电量%、油量%)"""
    conn = get_db_connection()
    cursor = conn.cursor()

    vehicle = get_vehicle_by_id(vehicle_id)
    if not vehicle:
        conn.close()
        return

    init_m = vehicle.get('initial_mileage') or 0.0
    curr_m = vehicle.get('current_mileage') or init_m

    cursor.execute('SELECT MAX(mileage) FROM fuel_records WHERE vehicle_id = ?', (vehicle_id,))
    max_fuel = cursor.fetchone()[0] or 0.0

    cursor.execute('SELECT MAX(mileage) FROM electricity_records WHERE vehicle_id = ?', (vehicle_id,))
    max_elec = cursor.fetchone()[0] or 0.0

    cursor.execute('SELECT MAX(mileage) FROM maintenance_records WHERE vehicle_id = ?', (vehicle_id,))
    max_maint = cursor.fetchone()[0] or 0.0

    cursor.execute('SELECT MAX(mileage) FROM status_records WHERE vehicle_id = ?', (vehicle_id,))
    max_status = cursor.fetchone()[0] or 0.0

    highest_m = max(init_m, curr_m, max_status, max_fuel, max_elec, max_maint)

    curr_fuel_pct = vehicle.get('current_fuel_percent')
    curr_bat_pct = vehicle.get('current_battery_percent')

    # 优先顺序：哪一个记录的里程最高（最新），就以哪一个作为最新状态
    # 1. 若 status_records 里程为最新
    if max_status >= highest_m and max_status > 0:
        cursor.execute('''
            SELECT fuel_percent, battery_percent FROM status_records 
            WHERE vehicle_id = ? AND mileage = ? 
            ORDER BY record_date DESC, id DESC LIMIT 1
        ''', (vehicle_id, max_status))
        rs = cursor.fetchone()
        if rs:
            if rs[0] is not None:
                curr_fuel_pct = rs[0]
            if rs[1] is not None:
                curr_bat_pct = rs[1]

    # 2. 若补能加油记录里程为最新
    elif max_fuel >= highest_m and max_fuel > 0:
        cursor.execute('''
            SELECT fuel_percent, battery_percent FROM fuel_records 
            WHERE vehicle_id = ? AND mileage = ? 
            ORDER BY record_date DESC, id DESC LIMIT 1
        ''', (vehicle_id, max_fuel))
        rf = cursor.fetchone()
        if rf:
            curr_fuel_pct = rf[0]
            if rf[1] is not None:
                curr_bat_pct = rf[1]

    # 3. 若补能加电记录里程为最新
    elif max_elec >= highest_m and max_elec > 0:
        cursor.execute('''
            SELECT end_soc, fuel_percent FROM electricity_records 
            WHERE vehicle_id = ? AND mileage = ? 
            ORDER BY record_date DESC, id DESC LIMIT 1
        ''', (vehicle_id, max_elec))
        re = cursor.fetchone()
        if re:
            curr_bat_pct = re[0]
            if re[1] is not None:
                curr_fuel_pct = re[1]

    if curr_fuel_pct is None:
        cursor.execute('''
            SELECT fuel_percent FROM fuel_records 
            WHERE vehicle_id = ? AND fuel_percent IS NOT NULL 
            ORDER BY mileage DESC, record_date DESC, id DESC LIMIT 1
        ''', (vehicle_id,))
        row_f = cursor.fetchone()
        curr_fuel_pct = row_f[0] if row_f else (vehicle.get('initial_fuel_percent') or 0.0)

    if curr_bat_pct is None:
        cursor.execute('''
            SELECT end_soc FROM electricity_records 
            WHERE vehicle_id = ? AND end_soc IS NOT NULL 
            ORDER BY mileage DESC, record_date DESC, id DESC LIMIT 1
        ''', (vehicle_id,))
        row_e = cursor.fetchone()
        curr_bat_pct = row_e[0] if row_e else (vehicle.get('initial_battery_percent') or 0.0)

    now = datetime.now().strftime('%Y-%m-%d %H:%M:%S')
    cursor.execute('''
        UPDATE vehicles 
        SET current_mileage = ?, current_fuel_percent = ?, current_battery_percent = ?, updated_at = ? 
        WHERE id = ?
    ''', (highest_m, curr_fuel_pct, curr_bat_pct, now, vehicle_id))
    conn.commit()
    conn.close()

sync_vehicle_current_mileage = sync_vehicle_state

# ----------------- 油耗记录 -----------------

def get_fuel_records(vehicle_id: Optional[int] = None) -> List[Dict[str, Any]]:
    conn = get_db_connection()
    cursor = conn.cursor()
    if vehicle_id:
        cursor.execute('''
            SELECT f.*, v.name as vehicle_name, v.plate_number
            FROM fuel_records f
            JOIN vehicles v ON f.vehicle_id = v.id
            WHERE f.vehicle_id = ?
            ORDER BY f.mileage DESC, f.record_date DESC
        ''', (vehicle_id,))
    else:
        cursor.execute('''
            SELECT f.*, v.name as vehicle_name, v.plate_number
            FROM fuel_records f
            JOIN vehicles v ON f.vehicle_id = v.id
            ORDER BY f.record_date DESC
        ''')
    rows = cursor.fetchall()
    records = [dict(row) for row in rows]
    conn.close()
    return records

def add_fuel_record(data: Dict[str, Any]) -> int:
    conn = get_db_connection()
    cursor = conn.cursor()
    now = datetime.now().strftime('%Y-%m-%d %H:%M:%S')

    vehicle_id = int(data['vehicle_id'])
    mileage = float(data['mileage'])
    fuel_amount = round(float(data['fuel_amount']), 2)
    unit_price = round(float(data['unit_price']), 3)
    total_price = round(float(data['total_price']), 2)

    cursor.execute('''
        INSERT INTO fuel_records (
            vehicle_id, mileage, fuel_amount, unit_price, total_price,
            fuel_type, is_full, start_fuel_percent, fuel_percent, battery_percent, station, record_date, notes, created_at
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
    ''', (
        vehicle_id,
        mileage,
        fuel_amount,
        unit_price,
        total_price,
        data.get('fuel_type', '92#'),
        int(data.get('is_full', 1)),
        float(data['start_fuel_percent']) if data.get('start_fuel_percent') is not None and data.get('start_fuel_percent') != '' else None,
        float(data.get('fuel_percent', 100) or 100),
        float(data['battery_percent']) if data.get('battery_percent') is not None and data.get('battery_percent') != '' else None,
        data.get('station', ''),
        data.get('record_date', datetime.now().strftime('%Y-%m-%d %H:%M')),
        data.get('notes', ''),
        now
    ))
    rec_id = cursor.lastrowid
    conn.commit()
    conn.close()

    sync_vehicle_current_mileage(vehicle_id)
    return rec_id

def get_fuel_record_by_id(record_id: int) -> Optional[Dict[str, Any]]:
    conn = get_db_connection()
    cursor = conn.cursor()
    cursor.execute('SELECT * FROM fuel_records WHERE id = ?', (record_id,))
    row = cursor.fetchone()
    conn.close()
    return dict(row) if row else None

def update_fuel_record(record_id: int, data: Dict[str, Any]) -> bool:
    conn = get_db_connection()
    cursor = conn.cursor()
    cursor.execute('SELECT vehicle_id FROM fuel_records WHERE id = ?', (record_id,))
    row = cursor.fetchone()
    if not row:
        conn.close()
        return False
    vehicle_id = row['vehicle_id']

    mileage = float(data['mileage'])
    fuel_amount = round(float(data['fuel_amount']), 2)
    unit_price = round(float(data['unit_price']), 3)
    total_price = round(float(data['total_price']), 2)
    fuel_type = data.get('fuel_type', '92#')
    is_full = int(data.get('is_full', 1))
    start_fuel_percent = float(data['start_fuel_percent']) if data.get('start_fuel_percent') is not None and data.get('start_fuel_percent') != '' else None
    fuel_percent = float(data.get('fuel_percent', 100) or 100)
    battery_percent = float(data['battery_percent']) if data.get('battery_percent') is not None and data.get('battery_percent') != '' else None
    station = data.get('station', '')
    record_date = data.get('record_date', '')
    notes = data.get('notes', '')

    cursor.execute('''
        UPDATE fuel_records SET
            mileage = ?, fuel_amount = ?, unit_price = ?, total_price = ?,
            fuel_type = ?, is_full = ?, start_fuel_percent = ?, fuel_percent = ?, battery_percent = ?, station = ?,
            record_date = ?, notes = ?
        WHERE id = ?
    ''', (
        mileage, fuel_amount, unit_price, total_price,
        fuel_type, is_full, start_fuel_percent, fuel_percent, battery_percent, station,
        record_date, notes, record_id
    ))
    updated = cursor.rowcount > 0
    conn.commit()
    conn.close()
    sync_vehicle_current_mileage(vehicle_id)
    return updated

def delete_fuel_record(record_id: int) -> bool:
    conn = get_db_connection()
    cursor = conn.cursor()
    cursor.execute('SELECT vehicle_id FROM fuel_records WHERE id = ?', (record_id,))
    row = cursor.fetchone()
    if not row:
        conn.close()
        return False
    vehicle_id = row['vehicle_id']
    cursor.execute('DELETE FROM fuel_records WHERE id = ?', (record_id,))
    conn.commit()
    conn.close()
    sync_vehicle_current_mileage(vehicle_id)
    return True

# ----------------- 电耗记录 -----------------

def get_electricity_records(vehicle_id: Optional[int] = None) -> List[Dict[str, Any]]:
    conn = get_db_connection()
    cursor = conn.cursor()
    if vehicle_id:
        cursor.execute('''
            SELECT e.*, v.name as vehicle_name, v.plate_number
            FROM electricity_records e
            JOIN vehicles v ON e.vehicle_id = v.id
            WHERE e.vehicle_id = ?
            ORDER BY e.mileage DESC, e.record_date DESC
        ''', (vehicle_id,))
    else:
        cursor.execute('''
            SELECT e.*, v.name as vehicle_name, v.plate_number
            FROM electricity_records e
            JOIN vehicles v ON e.vehicle_id = v.id
            ORDER BY e.record_date DESC
        ''')
    rows = cursor.fetchall()
    records = [dict(row) for row in rows]
    conn.close()
    return records

def add_electricity_record(data: Dict[str, Any]) -> int:
    conn = get_db_connection()
    cursor = conn.cursor()
    now = datetime.now().strftime('%Y-%m-%d %H:%M:%S')

    vehicle_id = int(data['vehicle_id'])
    mileage = float(data['mileage'])
    electricity_amount = round(float(data['electricity_amount']), 2)
    unit_price = round(float(data['unit_price']), 3)
    total_price = round(float(data['total_price']), 2)

    fuel_pct = None
    if data.get('fuel_percent') is not None and data.get('fuel_percent') != '':
        try:
            fuel_pct = float(data.get('fuel_percent'))
        except (ValueError, TypeError):
            fuel_pct = None

    is_calibrated = 1 if (data.get('is_calibrated') in (1, '1', True, 'true')) else 0
    calibrated_capacity = None
    if is_calibrated:
        try:
            if data.get('calibrated_capacity') is not None and data.get('calibrated_capacity') != '':
                calibrated_capacity = round(float(data['calibrated_capacity']), 2)
            elif float(data.get('end_soc', 0)) > float(data.get('start_soc', 0)):
                delta_soc = float(data['end_soc']) - float(data['start_soc'])
                calibrated_capacity = round((electricity_amount / delta_soc) * 100.0, 2)
        except (ValueError, TypeError, ZeroDivisionError):
            calibrated_capacity = None

    cursor.execute('''
        INSERT INTO electricity_records (
            vehicle_id, mileage, electricity_amount, unit_price, total_price,
            charging_type, start_soc, end_soc, fuel_percent, station,
            is_calibrated, calibrated_capacity, record_date, notes, created_at
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
    ''', (
        vehicle_id,
        mileage,
        electricity_amount,
        unit_price,
        total_price,
        data.get('charging_type', '快充'),
        float(data.get('start_soc')) if data.get('start_soc') is not None and data.get('start_soc') != '' else None,
        float(data.get('end_soc')) if data.get('end_soc') is not None and data.get('end_soc') != '' else None,
        fuel_pct,
        data.get('station', ''),
        is_calibrated,
        calibrated_capacity,
        data.get('record_date', datetime.now().strftime('%Y-%m-%d %H:%M')),
        data.get('notes', ''),
        now
    ))
    rec_id = cursor.lastrowid


    conn.commit()
    conn.close()

    sync_vehicle_current_mileage(vehicle_id)
    return rec_id

def get_electricity_record_by_id(record_id: int) -> Optional[Dict[str, Any]]:
    conn = get_db_connection()
    cursor = conn.cursor()
    cursor.execute('SELECT * FROM electricity_records WHERE id = ?', (record_id,))
    row = cursor.fetchone()
    conn.close()
    return dict(row) if row else None

def update_electricity_record(record_id: int, data: Dict[str, Any]) -> bool:
    conn = get_db_connection()
    cursor = conn.cursor()
    cursor.execute('SELECT vehicle_id FROM electricity_records WHERE id = ?', (record_id,))
    row = cursor.fetchone()
    if not row:
        conn.close()
        return False
    vehicle_id = row['vehicle_id']

    mileage = float(data['mileage'])
    electricity_amount = round(float(data['electricity_amount']), 2)
    unit_price = round(float(data['unit_price']), 3)
    total_price = round(float(data['total_price']), 2)
    charging_type = data.get('charging_type', '快充')
    start_soc = float(data.get('start_soc')) if data.get('start_soc') is not None and data.get('start_soc') != '' else None
    end_soc = float(data.get('end_soc')) if data.get('end_soc') is not None and data.get('end_soc') != '' else None
    fuel_pct = None
    if data.get('fuel_percent') is not None and data.get('fuel_percent') != '':
        try:
            fuel_pct = float(data.get('fuel_percent'))
        except (ValueError, TypeError):
            fuel_pct = None
    station = data.get('station', '')
    record_date = data.get('record_date', '')
    notes = data.get('notes', '')

    is_calibrated = 1 if (data.get('is_calibrated') in (1, '1', True, 'true')) else 0
    calibrated_capacity = None
    if is_calibrated:
        try:
            if data.get('calibrated_capacity') is not None and data.get('calibrated_capacity') != '':
                calibrated_capacity = round(float(data['calibrated_capacity']), 2)
            elif end_soc is not None and start_soc is not None and end_soc > start_soc:
                delta_soc = end_soc - start_soc
                calibrated_capacity = round((electricity_amount / delta_soc) * 100.0, 2)
        except (ValueError, TypeError, ZeroDivisionError):
            calibrated_capacity = None

    cursor.execute('''
        UPDATE electricity_records SET
            mileage = ?, electricity_amount = ?, unit_price = ?, total_price = ?,
            charging_type = ?, start_soc = ?, end_soc = ?, fuel_percent = ?,
            station = ?, is_calibrated = ?, calibrated_capacity = ?, record_date = ?, notes = ?
        WHERE id = ?
    ''', (
        mileage, electricity_amount, unit_price, total_price,
        charging_type, start_soc, end_soc, fuel_pct,
        station, is_calibrated, calibrated_capacity, record_date, notes, record_id
    ))
    updated = cursor.rowcount > 0

    conn.commit()
    conn.close()
    sync_vehicle_current_mileage(vehicle_id)
    return updated

def delete_electricity_record(record_id: int) -> bool:
    conn = get_db_connection()
    cursor = conn.cursor()
    cursor.execute('SELECT vehicle_id FROM electricity_records WHERE id = ?', (record_id,))
    row = cursor.fetchone()
    if not row:
        conn.close()
        return False
    vehicle_id = row['vehicle_id']
    cursor.execute('DELETE FROM electricity_records WHERE id = ?', (record_id,))
    conn.commit()
    conn.close()
    sync_vehicle_current_mileage(vehicle_id)
    return True

# ----------------- 维护保养记录 -----------------

def get_maintenance_records(vehicle_id: Optional[int] = None) -> List[Dict[str, Any]]:
    conn = get_db_connection()
    cursor = conn.cursor()
    if vehicle_id:
        cursor.execute('''
            SELECT m.*, v.name as vehicle_name, v.plate_number
            FROM maintenance_records m
            JOIN vehicles v ON m.vehicle_id = v.id
            WHERE m.vehicle_id = ?
            ORDER BY m.record_date DESC, m.id DESC
        ''', (vehicle_id,))
    else:
        cursor.execute('''
            SELECT m.*, v.name as vehicle_name, v.plate_number
            FROM maintenance_records m
            JOIN vehicles v ON m.vehicle_id = v.id
            ORDER BY m.record_date DESC, m.id DESC
        ''')
    rows = cursor.fetchall()
    records = [dict(row) for row in rows]
    conn.close()
    return records

def get_maintenance_record_by_id(record_id: int) -> Optional[Dict[str, Any]]:
    conn = get_db_connection()
    cursor = conn.cursor()
    cursor.execute('SELECT * FROM maintenance_records WHERE id = ?', (record_id,))
    row = cursor.fetchone()
    conn.close()
    return dict(row) if row else None

def update_maintenance_record(record_id: int, data: Dict[str, Any]) -> bool:
    conn = get_db_connection()
    cursor = conn.cursor()
    cursor.execute('SELECT vehicle_id FROM maintenance_records WHERE id = ?', (record_id,))
    row = cursor.fetchone()
    if not row:
        conn.close()
        return False
    vehicle_id = row['vehicle_id']

    mileage = float(data.get('mileage', 0) or 0)
    item_type = data.get('item_type', '费用支出')
    cost_val = data.get('cost')
    cost = float(cost_val) if cost_val is not None and cost_val != '' else 0.0
    provider = data.get('provider', '')
    record_date = data.get('record_date', '')
    next_m = float(data.get('next_mileage_reminder')) if data.get('next_mileage_reminder') else None
    next_d = data.get('next_date_reminder', '')
    notes = data.get('notes', '')

    cursor.execute('''
        UPDATE maintenance_records SET
            mileage = ?, item_type = ?, cost = ?, provider = ?,
            record_date = ?, next_mileage_reminder = ?, next_date_reminder = ?, notes = ?
        WHERE id = ?
    ''', (
        mileage, item_type, cost, provider,
        record_date, next_m, next_d, notes, record_id
    ))
    updated = cursor.rowcount > 0
    conn.commit()
    conn.close()
    sync_vehicle_current_mileage(vehicle_id)
    return updated

def add_maintenance_record(data: Dict[str, Any]) -> int:
    conn = get_db_connection()
    cursor = conn.cursor()
    now = datetime.now().strftime('%Y-%m-%d %H:%M:%S')

    vehicle_id = int(data['vehicle_id'])
    mileage = float(data.get('mileage', 0) or 0)

    cursor.execute('''
        INSERT INTO maintenance_records (
            vehicle_id, mileage, item_type, cost, provider,
            record_date, next_mileage_reminder, next_date_reminder, notes, created_at
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
    ''', (
        vehicle_id,
        mileage,
        data.get('item_type', '常规保养'),
        float(data.get('cost')) if data.get('cost') is not None and data.get('cost') != '' else 0.0,
        data.get('provider', ''),
        data.get('record_date', datetime.now().strftime('%Y-%m-%d')),
        float(data.get('next_mileage_reminder')) if data.get('next_mileage_reminder') else None,
        data.get('next_date_reminder', ''),
        data.get('notes', ''),
        now
    ))
    rec_id = cursor.lastrowid
    conn.commit()
    conn.close()

    sync_vehicle_current_mileage(vehicle_id)
    return rec_id

def delete_maintenance_record(record_id: int) -> bool:
    conn = get_db_connection()
    cursor = conn.cursor()
    cursor.execute('SELECT vehicle_id FROM maintenance_records WHERE id = ?', (record_id,))
    row = cursor.fetchone()
    if not row:
        conn.close()
        return False
    vehicle_id = row['vehicle_id']
    cursor.execute('DELETE FROM maintenance_records WHERE id = ?', (record_id,))
    conn.commit()
    conn.close()
    sync_vehicle_current_mileage(vehicle_id)
    return True

# ----------------- 车辆当前状态/表显快照记录 -----------------

def get_status_records(vehicle_id: Optional[int] = None) -> List[Dict[str, Any]]:
    conn = get_db_connection()
    cursor = conn.cursor()
    if vehicle_id:
        cursor.execute('''
            SELECT s.*, v.name as vehicle_name, v.plate_number
            FROM status_records s
            JOIN vehicles v ON s.vehicle_id = v.id
            WHERE s.vehicle_id = ?
            ORDER BY s.mileage DESC, s.record_date DESC, s.id DESC
        ''', (vehicle_id,))
    else:
        cursor.execute('''
            SELECT s.*, v.name as vehicle_name, v.plate_number
            FROM status_records s
            JOIN vehicles v ON s.vehicle_id = v.id
            ORDER BY s.mileage DESC, s.record_date DESC, s.id DESC
        ''')
    rows = cursor.fetchall()
    records = [dict(row) for row in rows]
    conn.close()
    return records

def get_status_record_by_id(record_id: int) -> Optional[Dict[str, Any]]:
    conn = get_db_connection()
    cursor = conn.cursor()
    cursor.execute('SELECT * FROM status_records WHERE id = ?', (record_id,))
    row = cursor.fetchone()
    conn.close()
    return dict(row) if row else None

def add_status_record(data: Dict[str, Any]) -> int:
    conn = get_db_connection()
    cursor = conn.cursor()
    now = datetime.now().strftime('%Y-%m-%d %H:%M:%S')

    vehicle_id = int(data['vehicle_id'])
    mileage = float(data['mileage'])
    bat_pct = float(data['battery_percent']) if data.get('battery_percent') is not None and data.get('battery_percent') != '' else None
    fuel_pct = float(data['fuel_percent']) if data.get('fuel_percent') is not None and data.get('fuel_percent') != '' else None
    rec_date = data.get('record_date') or datetime.now().strftime('%Y-%m-%d %H:%M')
    notes = data.get('notes', '')

    cursor.execute('''
        INSERT INTO status_records (
            vehicle_id, mileage, battery_percent, fuel_percent, record_date, notes, created_at
        ) VALUES (?, ?, ?, ?, ?, ?, ?)
    ''', (vehicle_id, mileage, bat_pct, fuel_pct, rec_date, notes, now))
    rec_id = cursor.lastrowid
    conn.commit()
    conn.close()

    update_vehicle(vehicle_id, {
        "current_mileage": mileage,
        "current_battery_percent": bat_pct,
        "current_fuel_percent": fuel_pct
    })
    return rec_id

def update_status_record(record_id: int, data: Dict[str, Any]) -> bool:
    rec = get_status_record_by_id(record_id)
    if not rec:
        return False
    conn = get_db_connection()
    cursor = conn.cursor()

    vehicle_id = int(data.get('vehicle_id', rec['vehicle_id']))
    mileage = float(data['mileage']) if data.get('mileage') is not None else rec['mileage']
    bat_pct = float(data['battery_percent']) if data.get('battery_percent') is not None and data.get('battery_percent') != '' else (rec['battery_percent'] if data.get('battery_percent') is None else None)
    fuel_pct = float(data['fuel_percent']) if data.get('fuel_percent') is not None and data.get('fuel_percent') != '' else (rec['fuel_percent'] if data.get('fuel_percent') is None else None)
    rec_date = data.get('record_date', rec['record_date'])
    notes = data.get('notes', rec['notes'])

    cursor.execute('''
        UPDATE status_records SET
            vehicle_id = ?, mileage = ?, battery_percent = ?, fuel_percent = ?,
            record_date = ?, notes = ?
        WHERE id = ?
    ''', (vehicle_id, mileage, bat_pct, fuel_pct, rec_date, notes, record_id))
    updated = cursor.rowcount > 0
    conn.commit()
    conn.close()

    sync_vehicle_state(vehicle_id)
    return updated

def delete_status_record(record_id: int) -> bool:
    rec = get_status_record_by_id(record_id)
    if not rec:
        return False
    vehicle_id = rec['vehicle_id']
    conn = get_db_connection()
    cursor = conn.cursor()
    cursor.execute('DELETE FROM status_records WHERE id = ?', (record_id,))
    deleted = cursor.rowcount > 0
    conn.commit()
    conn.close()

    sync_vehicle_state(vehicle_id)
    return deleted

def parse_date_safely(date_str: str) -> Optional[datetime]:
    if not date_str:
        return None
    for fmt in ('%Y-%m-%d %H:%M', '%Y-%m-%d %H:%M:%S', '%Y-%m-%d'):
        try:
            return datetime.strptime(date_str.strip(), fmt)
        except ValueError:
            pass
    return None

# ----------------- 能耗统计与仪表板分析 -----------------

def get_vehicle_statistics(vehicle_id: int) -> Dict[str, Any]:
    vehicle = get_vehicle_by_id(vehicle_id)
    if not vehicle:
        return {}

    conn = get_db_connection()
    cursor = conn.cursor()

    # 1. 油耗聚合
    cursor.execute('''
        SELECT
            COUNT(*) as count,
            COALESCE(SUM(fuel_amount), 0) as total_fuel,
            COALESCE(SUM(total_price), 0) as total_fuel_cost,
            MIN(mileage) as min_fuel_mileage,
            MAX(mileage) as max_fuel_mileage
        FROM fuel_records WHERE vehicle_id = ?
    ''', (vehicle_id,))
    fuel_stat = dict(cursor.fetchone())

    # 2. 电耗聚合
    cursor.execute('''
        SELECT
            COUNT(*) as count,
            COALESCE(SUM(electricity_amount), 0) as total_elec,
            COALESCE(SUM(total_price), 0) as total_elec_cost,
            MIN(mileage) as min_elec_mileage,
            MAX(mileage) as max_elec_mileage
        FROM electricity_records WHERE vehicle_id = ?
    ''', (vehicle_id,))
    elec_stat = dict(cursor.fetchone())

    # 3. 维护费用聚合
    cursor.execute('''
        SELECT
            COUNT(*) as count,
            COALESCE(SUM(cost), 0) as total_maintenance_cost
        FROM maintenance_records WHERE vehicle_id = ?
    ''', (vehicle_id,))
    maint_stat = dict(cursor.fetchone())

    # 4. 日均开销计算：统计自购车日期 (purchase_date) 或最早记录日期到今天的天数
    today = datetime.now().date()
    start_date = None
    if vehicle.get('purchase_date'):
        pd = parse_date_safely(vehicle['purchase_date'])
        if pd:
            start_date = pd.date()

    if not start_date:
        cursor.execute('SELECT MIN(record_date) as d FROM fuel_records WHERE vehicle_id = ?', (vehicle_id,))
        r1 = cursor.fetchone()
        cursor.execute('SELECT MIN(record_date) as d FROM electricity_records WHERE vehicle_id = ?', (vehicle_id,))
        r2 = cursor.fetchone()
        cursor.execute('SELECT MIN(record_date) as d FROM maintenance_records WHERE vehicle_id = ?', (vehicle_id,))
        r3 = cursor.fetchone()

        valid_dates = []
        for r in (r1, r2, r3):
            if r and r['d']:
                pd = parse_date_safely(r['d'])
                if pd:
                    valid_dates.append(pd.date())
        if valid_dates:
            start_date = min(valid_dates)
        elif vehicle.get('created_at'):
            pd = parse_date_safely(vehicle['created_at'])
            if pd:
                start_date = pd.date()

    # 5. 按月费用统计聚合 (横轴为月份，统计燃油、电力、其他支出及总费用)
    cursor.execute('''
        SELECT
            month,
            ROUND(SUM(fuel_cost), 2) as fuel_cost,
            ROUND(SUM(elec_cost), 2) as elec_cost,
            ROUND(SUM(other_cost), 2) as other_cost,
            ROUND(SUM(fuel_cost + elec_cost + other_cost), 2) as total_cost
        FROM (
            SELECT SUBSTR(record_date, 1, 7) as month, total_price as fuel_cost, 0 as elec_cost, 0 as other_cost
            FROM fuel_records WHERE vehicle_id = ?
            UNION ALL
            SELECT SUBSTR(record_date, 1, 7) as month, 0 as fuel_cost, total_price as elec_cost, 0 as other_cost
            FROM electricity_records WHERE vehicle_id = ?
            UNION ALL
            SELECT SUBSTR(record_date, 1, 7) as month, 0 as fuel_cost, 0 as elec_cost, cost as other_cost
            FROM maintenance_records WHERE vehicle_id = ?
        )
        WHERE month IS NOT NULL AND month != ''
        GROUP BY month
        ORDER BY month ASC
    ''', (vehicle_id, vehicle_id, vehicle_id))
    monthly_expenses = [dict(r) for r in cursor.fetchall()]

    conn.close()

    total_cost = fuel_stat['total_fuel_cost'] + elec_stat['total_elec_cost'] + maint_stat['total_maintenance_cost']
    total_energy_cost = fuel_stat['total_fuel_cost'] + elec_stat['total_elec_cost']

    initial_m = vehicle['initial_mileage']
    current_m = vehicle['current_mileage']
    total_driven_km = max(0.0, current_m - initial_m)

    # 累计百公里能耗计算：行程为最新补能里程减初始里程，能耗为本次补能前所有能耗总和
    ea = get_energy_interval_analysis(vehicle_id)
    ea_summary = ea.get('summary', {})
    avg_fuel_per_hundred = ea_summary.get('fuel', {}).get('overall_avg', 0.0)
    avg_elec_per_hundred = ea_summary.get('electricity', {}).get('overall_avg', 0.0)
    avg_equiv_elec_per_hundred = ea_summary.get('electricity', {}).get('equivalent_overall_avg', 0.0)
    total_equiv_elec_consumed = ea_summary.get('electricity', {}).get('equivalent_total_consumed', 0.0)

    # 每公里总费用 & 能耗费用
    cost_per_km = round(total_cost / total_driven_km, 2) if total_driven_km > 0 else 0.0
    energy_cost_per_km = round(total_energy_cost / total_driven_km, 2) if total_driven_km > 0 else 0.0

    # 平均每天累计能耗与维护总开销费用及平均每天里程
    days_owned = max(1, (today - start_date).days) if start_date else 1
    cost_per_day = round(total_cost / days_owned, 2) if days_owned > 0 else 0.0
    km_per_day = round(total_driven_km / days_owned, 2) if days_owned > 0 else 0.0

    # 概览看板新增指标：每公里能耗成本 (以最新补能终极里程与FIFO实耗金额核算)
    fifo_energy_cost = ea_summary.get('fifo_energy_cost', 0.0)
    fifo_cost_per_km = ea_summary.get('fifo_cost_per_km', 0.0)
    fifo_cost_per_hundred = ea_summary.get('fifo_cost_per_hundred', 0.0)
    fifo_driven_km = ea_summary.get('fifo_driven_km', 0.0)
    fifo_fuel_cost = ea_summary.get('fifo_fuel_cost', 0.0)
    fifo_elec_cost = ea_summary.get('fifo_elec_cost', 0.0)
    latest_replenish_mileage = ea_summary.get('latest_replenish_mileage', current_m)

    # 平均每天能耗成本 (总购能费用日均与FIFO实耗日均)
    energy_cost_per_day = round(total_energy_cost / days_owned, 2) if days_owned > 0 else 0.0
    fifo_energy_cost_per_day = round(fifo_energy_cost / days_owned, 2) if days_owned > 0 else 0.0

    return {
        "vehicle": vehicle,
        "total_driven_km": round(total_driven_km, 1),
        "total_cost": round(total_cost, 2),
        "total_energy_cost": round(total_energy_cost, 2),
        "cost_per_km": cost_per_km,
        "energy_cost_per_km": energy_cost_per_km,
        "energy_cost_per_km_fifo": fifo_cost_per_km,
        "fifo_cost_per_km": fifo_cost_per_km,
        "energy_cost_per_hundred_fifo": fifo_cost_per_hundred,
        "fifo_energy_cost": fifo_energy_cost,
        "fifo_driven_km": fifo_driven_km,
        "fifo_fuel_cost": fifo_fuel_cost,
        "fifo_elec_cost": fifo_elec_cost,
        "latest_replenish_mileage": latest_replenish_mileage,
        "cost_per_day": cost_per_day,
        "km_per_day": km_per_day,
        "energy_cost_per_day": energy_cost_per_day,
        "fifo_energy_cost_per_day": fifo_energy_cost_per_day,
        "days_owned": days_owned,
        "monthly_expenses": monthly_expenses,
        "fuel": {
            "record_count": fuel_stat['count'],
            "total_amount": round(fuel_stat['total_fuel'], 2),
            "total_consumed": ea_summary.get('fuel', {}).get('total_consumed', 0.0),
            "driven_km": round(total_driven_km, 1),
            "total_cost": round(fuel_stat['total_fuel_cost'], 2),
            "avg_per_hundred": avg_fuel_per_hundred
        },
        "electricity": {
            "record_count": elec_stat['count'],
            "total_amount": round(elec_stat['total_elec'], 2),
            "total_consumed": ea_summary.get('electricity', {}).get('total_consumed', 0.0),
            "equivalent_total_consumed": total_equiv_elec_consumed,
            "driven_km": round(total_driven_km, 1),
            "total_cost": round(elec_stat['total_elec_cost'], 2),
            "avg_per_hundred": avg_elec_per_hundred,
            "equivalent_avg_per_hundred": avg_equiv_elec_per_hundred
        },
        "maintenance": {
            "record_count": maint_stat['count'],
            "total_cost": round(maint_stat['total_maintenance_cost'], 2)
        }
    }

# ----------------- 先进先出 (FIFO) 实耗金额核算辅助函数 -----------------

def compute_fifo_cost(consumed_amount: float, records: list, amount_key: str, price_key: str) -> float:
    rem = consumed_amount
    total_cost = 0.0
    for r in records:
        if rem <= 0:
            break
        amt = float(r.get(amount_key) or 0)
        price = float(r.get(price_key) or 0)
        if amt <= 0:
            continue
        take = min(rem, amt)
        total_cost += take * price
        rem -= take
    if rem > 0 and len(records) > 0:
        first_price = float(records[0].get(price_key) or 0)
        total_cost += rem * first_price
    return round(total_cost, 2)

# ----------------- 每两次补能之间的能耗与全局能耗分析 -----------------

def get_energy_interval_analysis(vehicle_id: int) -> Dict[str, Any]:
    """
    计算每两次补能（加油或充电）之间的区间能耗数据，以及全车总能耗统计
    """
    vehicle = get_vehicle_by_id(vehicle_id)
    if not vehicle:
        return {}

    conn = get_db_connection()
    cursor = conn.cursor()

    # 1. 查询所有加油记录（按里程与时间升序）
    cursor.execute('''
        SELECT * FROM fuel_records
        WHERE vehicle_id = ?
        ORDER BY mileage ASC, record_date ASC, id ASC
    ''', (vehicle_id,))
    fuel_rows = [dict(row) for row in cursor.fetchall()]

    # 2. 查询所有充电记录（按里程与时间升序）
    cursor.execute('''
        SELECT * FROM electricity_records
        WHERE vehicle_id = ?
        ORDER BY mileage ASC, record_date ASC, id ASC
    ''', (vehicle_id,))
    elec_rows = [dict(row) for row in cursor.fetchall()]

    conn.close()

    initial_mileage = float(vehicle['initial_mileage'] or 0)
    current_mileage = float(vehicle['current_mileage'] or initial_mileage)
    total_driven_km = max(0.0, current_mileage - initial_mileage)
    fuel_tank_cap = float(vehicle.get('fuel_tank_capacity') or 0)
    battery_cap = float(vehicle.get('battery_capacity') or 0)
    init_fuel_pct = float(vehicle.get('initial_fuel_percent') if vehicle.get('initial_fuel_percent') is not None else 100)
    init_battery_pct = float(vehicle.get('initial_battery_percent') if vehicle.get('initial_battery_percent') is not None else 100)
    if vehicle.get('power_type') == 'gas' and init_fuel_pct <= 0:
        init_fuel_pct = 100.0

    # --- 统一全量补能时序流 (合并燃油与充电，严格按时间与里程时序流动核算双能耗) ---
    unified_records = []
    for r in fuel_rows:
        unified_records.append({
            '_type': 'fuel',
            'data': r,
            'mileage': float(r['mileage']),
            'date': parse_date_safely(r['record_date'])
        })
    for r in elec_rows:
        unified_records.append({
            '_type': 'electricity',
            'data': r,
            'mileage': float(r['mileage']),
            'date': parse_date_safely(r['record_date'])
        })
    unified_records.sort(key=lambda x: (x['mileage'], x['data']['record_date'] or '', x['data']['id']))

    prev_m = initial_mileage
    prev_date = None
    prev_fuel_end = init_fuel_pct
    prev_bat_end = init_battery_pct
    cum_fuel_consumed = 0.0
    cum_elec_consumed = 0.0

    active_bat_cap = battery_cap
    fuel_order = 0
    elec_order = 0

    fuel_intervals = []
    elec_intervals = []
    all_intervals = []

    all_seen_fuels = []
    all_seen_elecs = []

    for item in unified_records:
        r = item['data']
        curr_m = item['mileage']
        curr_date = item['date']
        delta_m = max(0.0, curr_m - prev_m)
        days_diff = max(0, (curr_date - prev_date).days) if prev_date and curr_date else None
        drive_bat_cap = active_bat_cap

        if item['_type'] == 'fuel':
            all_seen_fuels.append(r)
        else:
            all_seen_elecs.append(r)

        if item['_type'] == 'fuel':
            fuel_order += 1
            amount = float(r['fuel_amount'])
            cost = float(r['total_price'])

            # 油量：加前初始值与加后结束值
            curr_end_fuel_pct = float(r['fuel_percent'] if r.get('fuel_percent') is not None else 100.0)
            if r.get('start_fuel_percent') is not None and r['start_fuel_percent'] != '':
                curr_start_fuel_pct = float(r['start_fuel_percent'])
            elif fuel_tank_cap > 0:
                curr_start_fuel_pct = max(0.0, curr_end_fuel_pct - (amount / fuel_tank_cap) * 100.0)
            else:
                curr_start_fuel_pct = prev_fuel_end

            # 电量：加油不改变电量，初始值与结束值均为当前记录电量（若未记则继承上次结束值）
            curr_bat_val = float(r['battery_percent']) if (r.get('battery_percent') is not None and r['battery_percent'] != '') else None
            curr_start_bat_pct = curr_bat_val if curr_bat_val is not None else prev_bat_end
            curr_end_bat_pct = curr_start_bat_pct

        else: # electricity
            elec_order += 1
            amount = float(r['electricity_amount'])
            cost = float(r['total_price'])

            # 电量：充前初始值与充后结束值
            curr_end_bat_pct = float(r['end_soc'] if r.get('end_soc') is not None else 100.0)
            if r.get('start_soc') is not None and r['start_soc'] != '':
                curr_start_bat_pct = float(r['start_soc'])
            elif active_bat_cap > 0:
                curr_start_bat_pct = max(0.0, curr_end_bat_pct - (amount / active_bat_cap) * 100.0)
            else:
                curr_start_bat_pct = prev_bat_end

            # 油量：充电不改变油量，初始值与结束值均为当前记录油量（若未记则继承上次结束值）
            curr_fuel_val = float(r['fuel_percent']) if (r.get('fuel_percent') is not None and r['fuel_percent'] != '') else None
            curr_start_fuel_pct = curr_fuel_val if curr_fuel_val is not None else prev_fuel_end
            curr_end_fuel_pct = curr_start_fuel_pct

        # 核心算法：单次能耗 = 上次补能结束值 - 本次补能初始值
        delta_fuel_pct = max(0.0, prev_fuel_end - curr_start_fuel_pct)
        delta_bat_pct = max(0.0, prev_bat_end - curr_start_bat_pct)

        consumed_fuel = round((delta_fuel_pct / 100.0) * fuel_tank_cap, 2) if fuel_tank_cap > 0 else 0.0
        consumed_elec = round((delta_bat_pct / 100.0) * drive_bat_cap, 2) if drive_bat_cap > 0 else 0.0

        # 电池矫正：若本次充电设置了电池矫正，从本次充电结束时刻起，更新生效电池容量，只影响后续区间
        is_cal = 1 if r.get('is_calibrated') in (1, '1', True, 'true') else 0
        cal_cap = None
        if is_cal and r.get('calibrated_capacity') is not None and r.get('calibrated_capacity') != '':
            try:
                cal_cap = round(float(r['calibrated_capacity']), 2)
                if cal_cap > 0:
                    active_bat_cap = cal_cap
            except (ValueError, TypeError):
                cal_cap = None
        # 等效电耗分析：将油耗按 1L/100km 折合为 3.3 kWh/100km (1L 油 = 3.3 kWh 电)
        consumed_equiv_elec = round(consumed_elec + consumed_fuel * 3.3, 2)

        cum_fuel_consumed += consumed_fuel
        cum_elec_consumed += consumed_elec
        cum_equiv_elec_consumed = round(cum_elec_consumed + cum_fuel_consumed * 3.3, 2)

        cum_m = max(0.0, curr_m - initial_mileage)
        cum_fuel_cons = round((cum_fuel_consumed / cum_m) * 100, 2) if cum_m > 0 else 0.0
        cum_elec_cons = round((cum_elec_consumed / cum_m) * 100, 2) if cum_m > 0 else 0.0
        cum_equiv_elec_cons = round((cum_equiv_elec_consumed / cum_m) * 100, 2) if cum_m > 0 else 0.0

        interval_fuel_cons = round((consumed_fuel / delta_m) * 100, 2) if delta_m > 0 else 0.0
        interval_elec_cons = round((consumed_elec / delta_m) * 100, 2) if delta_m > 0 else 0.0
        interval_equiv_elec_cons = round((consumed_equiv_elec / delta_m) * 100, 2) if delta_m > 0 else 0.0
        interval_cost_km = round(cost / delta_m, 2) if delta_m > 0 else 0.0

        # 核心算法：先进先出 (FIFO) 结算补能前实际所消耗电费与油费
        fifo_fuel_cost = compute_fifo_cost(cum_fuel_consumed, all_seen_fuels, 'fuel_amount', 'unit_price')
        fifo_elec_cost = compute_fifo_cost(cum_elec_consumed, all_seen_elecs, 'electricity_amount', 'unit_price')
        fifo_energy_cost = round(fifo_fuel_cost + fifo_elec_cost, 2)
        fifo_cost_per_km = round(fifo_energy_cost / cum_m, 2) if cum_m > 0 else 0.0
        fifo_cost_per_hundred = round((fifo_energy_cost / cum_m) * 100, 2) if cum_m > 0 else 0.0

        interval_item = {
            "order": fuel_order if item['_type'] == 'fuel' else elec_order,
            "type": item['_type'],
            "id": r['id'],
            "record_date": r['record_date'],
            "mileage": curr_m,
            "prev_mileage": prev_m,
            "delta_mileage": round(delta_m, 1),
            "amount": amount,
            "consumed_amount": consumed_fuel if item['_type'] == 'fuel' else consumed_elec,
            "interval_fuel_consumed": consumed_fuel,
            "interval_fuel_consumption": interval_fuel_cons,
            "interval_elec_consumed": consumed_elec,
            "interval_elec_consumption": interval_elec_cons,
            "interval_equivalent_elec_consumed": consumed_equiv_elec,
            "interval_equivalent_elec_consumption": interval_equiv_elec_cons,
            "fifo_fuel_cost": fifo_fuel_cost,
            "fifo_elec_cost": fifo_elec_cost,
            "fifo_energy_cost": fifo_energy_cost,
            "fifo_cost_per_km": fifo_cost_per_km,
            "fifo_cost_per_hundred": fifo_cost_per_hundred,
            "prev_fuel_percent": round(prev_fuel_end, 1),
            "curr_start_fuel_percent": round(curr_start_fuel_pct, 1),
            "curr_end_fuel_percent": round(curr_end_fuel_pct, 1),
            "prev_battery_percent": round(prev_bat_end, 1),
            "curr_start_battery_percent": round(curr_start_bat_pct, 1),
            "curr_end_battery_percent": round(curr_end_bat_pct, 1),
            "delta_fuel_percent": round(delta_fuel_pct, 1),
            "delta_battery_percent": round(delta_bat_pct, 1),
            "prev_percent": round(prev_fuel_end if item['_type'] == 'fuel' else prev_bat_end, 1),
            "curr_start_percent": round(curr_start_fuel_pct if item['_type'] == 'fuel' else curr_start_bat_pct, 1),
            "curr_end_percent": round(curr_end_fuel_pct if item['_type'] == 'fuel' else curr_end_bat_pct, 1),
            "fuel_percent": round(curr_end_fuel_pct, 1),
            "battery_percent": round(curr_end_bat_pct, 1),
            "cumulative_mileage": round(cum_m, 1),
            "cumulative_consumed_amount": round(cum_fuel_consumed if item['_type'] == 'fuel' else cum_elec_consumed, 2),
            "cumulative_consumption": cum_fuel_cons if item['_type'] == 'fuel' else cum_elec_cons,
            "cumulative_fuel_consumed": round(cum_fuel_consumed, 2),
            "cumulative_fuel_consumption": cum_fuel_cons,
            "cumulative_elec_consumed": round(cum_elec_consumed, 2),
            "cumulative_elec_consumption": cum_elec_cons,
            "cumulative_equivalent_elec_consumed": cum_equiv_elec_consumed,
            "cumulative_equivalent_elec_consumption": cum_equiv_elec_cons,
            "unit": "L" if item['_type'] == 'fuel' else "kWh",
            "unit_price": float(r['unit_price']),
            "total_price": cost,
            "fuel_type": r.get('fuel_type') or '',
            "charging_type": r.get('charging_type') or '',
            "is_full": r.get('is_full', 1),
            "start_soc": r.get('start_soc'),
            "end_soc": r.get('end_soc'),
            "station": r.get('station') or '',
            "notes": r.get('notes') or '',
            "is_calibrated": is_cal if item['_type'] == 'electricity' else 0,
            "calibrated_capacity": cal_cap if item['_type'] == 'electricity' else None,
            "active_battery_capacity": active_bat_cap,
            "drive_battery_capacity": drive_bat_cap,
            "interval_consumption": interval_fuel_cons if item['_type'] == 'fuel' else interval_elec_cons,
            "cost_per_km": interval_cost_km,
            "days_since_prev": days_diff
        }

        if item['_type'] == 'fuel':
            fuel_intervals.append(interval_item)
        else:
            elec_intervals.append(interval_item)
        all_intervals.append(interval_item)

        prev_m = curr_m
        prev_date = curr_date
        prev_fuel_end = curr_end_fuel_pct
        prev_bat_end = curr_end_bat_pct

    # 检查从最后一次补能记录到车辆当前最新状态的行驶消耗 (在途/未补能状态)
    ongoing_delta_m = round(max(0.0, current_mileage - prev_m), 1)

    v_curr_fuel = vehicle.get('current_fuel_percent')
    curr_fuel_pct = float(v_curr_fuel) if v_curr_fuel is not None and v_curr_fuel != '' else prev_fuel_end

    v_curr_bat = vehicle.get('current_battery_percent')
    curr_bat_pct = float(v_curr_bat) if v_curr_bat is not None and v_curr_bat != '' else prev_bat_end

    delta_fuel_pct = max(0.0, prev_fuel_end - curr_fuel_pct)
    delta_bat_pct = max(0.0, prev_bat_end - curr_bat_pct)

    ongoing_fuel_consumed = round((delta_fuel_pct / 100.0) * fuel_tank_cap, 2) if fuel_tank_cap > 0 else 0.0
    ongoing_elec_consumed = round((delta_bat_pct / 100.0) * active_bat_cap, 2) if active_bat_cap > 0 else 0.0
    ongoing_equiv_elec_consumed = round(ongoing_elec_consumed + ongoing_fuel_consumed * 3.3, 2)

    cum_fuel_consumed += ongoing_fuel_consumed
    cum_elec_consumed += ongoing_elec_consumed
    cum_equiv_elec_consumed = round(cum_elec_consumed + cum_fuel_consumed * 3.3, 2)

    # 重新结算包含在途消耗在内的 FIFO 成本
    fifo_fuel_cost = compute_fifo_cost(cum_fuel_consumed, all_seen_fuels, 'fuel_amount', 'unit_price')
    fifo_elec_cost = compute_fifo_cost(cum_elec_consumed, all_seen_elecs, 'electricity_amount', 'unit_price')
    fifo_energy_cost = round(fifo_fuel_cost + fifo_elec_cost, 2)

    # 若车辆在最后一次补能后有额外行驶或电量/油量有消耗，则构建“当前行驶状态”区间项
    ongoing_item = None
    if ongoing_delta_m > 0 or ongoing_fuel_consumed > 0 or ongoing_elec_consumed > 0:
        ongoing_item = {
            "order": len(all_intervals) + 1,
            "type": "ongoing",
            "id": "current_status",
            "record_date": vehicle.get('updated_at') or "当前最新状态",
            "mileage": current_mileage,
            "prev_mileage": prev_m,
            "delta_mileage": ongoing_delta_m,
            "amount": 0,
            "consumed_amount": ongoing_elec_consumed if vehicle.get('power_type') == 'ev' else (ongoing_fuel_consumed if vehicle.get('power_type') == 'gas' else ongoing_equiv_elec_consumed),
            "interval_fuel_consumed": ongoing_fuel_consumed,
            "interval_fuel_consumption": round((ongoing_fuel_consumed / ongoing_delta_m) * 100, 2) if ongoing_delta_m > 0 else 0.0,
            "interval_elec_consumed": ongoing_elec_consumed,
            "interval_elec_consumption": round((ongoing_elec_consumed / ongoing_delta_m) * 100, 2) if ongoing_delta_m > 0 else 0.0,
            "interval_equivalent_elec_consumed": ongoing_equiv_elec_consumed,
            "interval_equivalent_elec_consumption": round((ongoing_equiv_elec_consumed / ongoing_delta_m) * 100, 2) if ongoing_delta_m > 0 else 0.0,
            "fifo_fuel_cost": fifo_fuel_cost,
            "fifo_elec_cost": fifo_elec_cost,
            "fifo_energy_cost": fifo_energy_cost,
            "fifo_cost_per_km": round(fifo_energy_cost / total_driven_km, 2) if total_driven_km > 0 else 0.0,
            "fifo_cost_per_hundred": round((fifo_energy_cost / total_driven_km) * 100, 2) if total_driven_km > 0 else 0.0,
            "prev_fuel_percent": round(prev_fuel_end, 1),
            "curr_start_fuel_percent": round(prev_fuel_end, 1),
            "curr_end_fuel_percent": round(curr_fuel_pct, 1),
            "prev_battery_percent": round(prev_bat_end, 1),
            "curr_start_battery_percent": round(prev_bat_end, 1),
            "curr_end_battery_percent": round(curr_bat_pct, 1),
            "fuel_percent": round(curr_fuel_pct, 1),
            "battery_percent": round(curr_bat_pct, 1),
            "cumulative_mileage": round(total_driven_km, 1),
            "cumulative_consumed_amount": round(cum_fuel_consumed if vehicle.get('power_type') == 'gas' else cum_elec_consumed, 2),
            "cumulative_consumption": round((cum_fuel_consumed / total_driven_km) * 100, 2) if vehicle.get('power_type') == 'gas' else round((cum_elec_consumed / total_driven_km) * 100, 2),
            "cumulative_fuel_consumed": round(cum_fuel_consumed, 2),
            "cumulative_fuel_consumption": round((cum_fuel_consumed / total_driven_km) * 100, 2) if total_driven_km > 0 else 0.0,
            "cumulative_elec_consumed": round(cum_elec_consumed, 2),
            "cumulative_elec_consumption": round((cum_elec_consumed / total_driven_km) * 100, 2) if total_driven_km > 0 else 0.0,
            "cumulative_equivalent_elec_consumed": cum_equiv_elec_consumed,
            "cumulative_equivalent_elec_consumption": round((cum_equiv_elec_consumed / total_driven_km) * 100, 2) if total_driven_km > 0 else 0.0,
            "unit": "L" if vehicle.get('power_type') == 'gas' else "kWh",
            "unit_price": 0,
            "total_price": 0,
            "fuel_type": "",
            "charging_type": "",
            "station": "当前在途行驶",
            "is_calibrated": 0,
            "calibrated_capacity": None,
            "active_battery_capacity": active_bat_cap,
            "drive_battery_capacity": active_bat_cap,
            "interval_consumption": round((ongoing_fuel_consumed / ongoing_delta_m) * 100, 2) if vehicle.get('power_type') == 'gas' else round((ongoing_elec_consumed / ongoing_delta_m) * 100, 2) if ongoing_delta_m > 0 else 0.0,
            "cost_per_km": 0,
            "days_since_prev": 0,
            "is_ongoing": True
        }
        all_intervals.append(ongoing_item)

    # --- 全量补能时间轴 (倒序展示最新在前) ---
    all_combined = all_intervals
    all_combined_sorted = sorted(all_intervals, key=lambda x: (x['mileage'], str(x.get('record_date', ''))), reverse=True)

    # 统计全局指标
    total_fuel_amount = round(sum(r['fuel_amount'] for r in fuel_rows), 2)
    total_fuel_cost = round(sum(r['total_price'] for r in fuel_rows), 2)

    # 燃油与电能累计能耗：总里程均以车辆当前总行驶里程 (当前总里程 - 初始里程) 为分母
    fuel_driven_km = total_driven_km
    total_fuel_consumed = round(cum_fuel_consumed, 2)
    overall_fuel_avg = round((total_fuel_consumed / fuel_driven_km) * 100, 2) if fuel_driven_km > 0 and total_fuel_consumed > 0 else 0.0

    total_elec_amount = round(sum(r['electricity_amount'] for r in elec_rows), 2)
    total_elec_cost = round(sum(r['total_price'] for r in elec_rows), 2)

    # 电耗累计能耗：总里程均以车辆当前总行驶里程为分母
    elec_driven_km = total_driven_km
    total_elec_consumed = round(cum_elec_consumed, 2)
    overall_elec_avg = round((total_elec_consumed / elec_driven_km) * 100, 2) if elec_driven_km > 0 and total_elec_consumed > 0 else 0.0

    # 等效电耗累计能耗 (油耗按 1L/100km 折合 3.3 kWh/100km，即 1L 油 = 3.3 kWh 电)
    total_equiv_elec_consumed = round(total_elec_consumed + total_fuel_consumed * 3.3, 2)
    overall_equiv_elec_avg = round((total_equiv_elec_consumed / total_driven_km) * 100, 2) if total_driven_km > 0 and total_equiv_elec_consumed > 0 else 0.0

    total_energy_cost = round(total_fuel_cost + total_elec_cost, 2)
    overall_energy_cost_per_km = round(total_energy_cost / total_driven_km, 2) if total_driven_km > 0 else 0.0

    # 极值统计 (最佳/最高能耗)
    valid_fuel_cons = [x['interval_consumption'] for x in fuel_intervals if x['interval_consumption'] > 0]
    best_fuel_cons = min(valid_fuel_cons) if valid_fuel_cons else 0.0
    worst_fuel_cons = max(valid_fuel_cons) if valid_fuel_cons else 0.0

    valid_elec_cons = [x['interval_consumption'] for x in elec_intervals if x['interval_consumption'] > 0]
    best_elec_cons = min(valid_elec_cons) if valid_elec_cons else 0.0
    worst_elec_cons = max(valid_elec_cons) if valid_elec_cons else 0.0

    valid_equiv_cons = [x.get('interval_equivalent_elec_consumption', 0) for x in all_combined if x.get('interval_equivalent_elec_consumption', 0) > 0]
    best_equiv_cons = min(valid_equiv_cons) if valid_equiv_cons else 0.0
    worst_equiv_cons = max(valid_equiv_cons) if valid_equiv_cons else 0.0

    # 平均每次补能行驶里程与单次花费
    all_deltas = [x['delta_mileage'] for x in all_combined if x['delta_mileage'] > 0]
    avg_interval_km = round(sum(all_deltas) / len(all_deltas), 1) if all_deltas else 0.0

    all_costs = [x['total_price'] for x in all_combined]
    avg_interval_cost = round(sum(all_costs) / len(all_costs), 2) if all_costs else 0.0

    # FIFO 终极指标：以当前购车行驶总里程为基准进行核算
    last_item = all_intervals[-1] if all_intervals else None
    fifo_driven_km = round(total_driven_km, 1) if total_driven_km > 0 else (last_item["cumulative_mileage"] if last_item else 0.0)
    fifo_cost_per_km = round(fifo_energy_cost / fifo_driven_km, 2) if fifo_driven_km > 0 else 0.0
    fifo_cost_per_hundred = round((fifo_energy_cost / fifo_driven_km) * 100, 2) if fifo_driven_km > 0 else 0.0
    latest_replenish_mileage = float(vehicle.get("current_mileage") or (last_item["mileage"] if last_item else initial_mileage))

    return {
        "vehicle": vehicle,
        "summary": {
            "total_driven_km": round(total_driven_km, 1),
            "total_energy_cost": total_energy_cost,
            "overall_energy_cost_per_km": overall_energy_cost_per_km,
            "fifo_energy_cost": fifo_energy_cost,
            "fifo_cost_per_km": fifo_cost_per_km,
            "fifo_cost_per_hundred": fifo_cost_per_hundred,
            "fifo_driven_km": fifo_driven_km,
            "fifo_fuel_cost": fifo_fuel_cost,
            "fifo_elec_cost": fifo_elec_cost,
            "latest_replenish_mileage": latest_replenish_mileage,
            "total_replenish_count": len(all_combined),
            "avg_interval_km": avg_interval_km,
            "avg_interval_cost": avg_interval_cost,
            "fuel": {
                "count": len(fuel_rows),
                "total_amount": total_fuel_amount,
                "total_consumed": total_fuel_consumed,
                "driven_km": round(fuel_driven_km, 1),
                "total_cost": total_fuel_cost,
                "overall_avg": overall_fuel_avg,
                "best_consumption": best_fuel_cons,
                "worst_consumption": worst_fuel_cons
            },
            "electricity": {
                "count": len(elec_rows),
                "total_amount": total_elec_amount,
                "total_consumed": total_elec_consumed,
                "driven_km": round(elec_driven_km, 1),
                "total_cost": total_elec_cost,
                "overall_avg": overall_elec_avg,
                "nominal_battery_capacity": battery_cap,
                "current_battery_capacity": active_bat_cap,
                "equivalent_total_consumed": total_equiv_elec_consumed,
                "equivalent_overall_avg": overall_equiv_elec_avg,
                "best_consumption": best_elec_cons,
                "worst_consumption": worst_elec_cons
            },
            "equivalent_electricity": {
                "total_consumed": total_equiv_elec_consumed,
                "overall_avg": overall_equiv_elec_avg,
                "driven_km": round(total_driven_km, 1),
                "best_consumption": best_equiv_cons,
                "worst_consumption": worst_equiv_cons,
                "formula": "1L/100km = 3.3 kWh/100km"
            }
        },
        "fuel_intervals": fuel_intervals,
        "elec_intervals": elec_intervals,
        "recent_intervals": all_combined_sorted,
        "ongoing_interval": ongoing_item
    }

