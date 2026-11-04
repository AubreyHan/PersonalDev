/**
 * Cloudflare Worker + D1 数据库 RESTful API 服务端 (worker.js)
 * 具备全球 CDN 加速、0 维护成本、永久免费无服务器架构
 */

import { ASSETS } from './static_assets.js';

export default {
  async fetch(request, env, ctx) {
    const url = new URL(request.url);
    const path = url.pathname;
    const method = request.method;

    // 1. 处理跨域预检请求 (CORS)
    if (method === 'OPTIONS') {
      return new Response(null, {
        headers: {
          'Access-Control-Allow-Origin': '*',
          'Access-Control-Allow-Methods': 'GET, POST, PUT, DELETE, OPTIONS',
          'Access-Control-Allow-Headers': 'Content-Type, Authorization',
          'Access-Control-Max-Age': '86400',
        },
      });
    }

    // 2. 静态页面与前端资源托管 (PWA / iPhone / iPad 全平台网页访问)
    if (!path.startsWith('/api/')) {
      const normalizedPath = '/' + path.replace(/^\/(?:static\/)?/, '');
      const assetKey = path === '/' ? '/' : (ASSETS[path] ? path : normalizedPath);
      const asset = ASSETS[assetKey] || ASSETS['/' + path.replace(/^\/+/, '')];
      if (asset) {
        let cacheControl = 'public, max-age=3600';
        if (path === '/' || path.endsWith('.html') || path.endsWith('sw.js') || path === '/sw.js') {
          cacheControl = 'no-cache, no-store, must-revalidate';
        }
        if (asset.base64) {
          const binary = Uint8Array.from(atob(asset.base64), c => c.charCodeAt(0));
          return new Response(binary, {
            headers: {
              'Content-Type': asset.contentType,
              'Cache-Control': 'public, max-age=86400',
            }
          });
        }
        return new Response(asset.content, {
          headers: {
            'Content-Type': asset.contentType,
            'Cache-Control': cacheControl,
          }
        });
      }
    }

    // CORS 响应头辅助函数
    const corsHeaders = {
      'Content-Type': 'application/json; charset=utf-8',
      'Access-Control-Allow-Origin': '*',
      'Cache-Control': 'no-cache, no-store, must-revalidate',
    };

    const json = (data, status = 200) => {
      return new Response(JSON.stringify(data), { status, headers: corsHeaders });
    };

    const err = (message, status = 400) => {
      return json({ error: true, message }, status);
    };

    const AUTH_SALT_DEFAULT = 'car_tracker_salt_2026';
    const AUTH_TOKEN_KEY = 'car_tracker_auth_token_secret_key_2026';

    const hashPassword = async (password, salt = AUTH_SALT_DEFAULT) => {
      const enc = new TextEncoder();
      const data = enc.encode(password + salt);
      const hashBuffer = await crypto.subtle.digest('SHA-256', data);
      const hashArray = Array.from(new Uint8Array(hashBuffer));
      return hashArray.map(b => b.toString(16).padStart(2, '0')).join('');
    };

    const makeToken = async (username, passwordHash) => {
      const enc = new TextEncoder();
      const data = enc.encode(username + ':' + passwordHash + ':' + AUTH_TOKEN_KEY);
      const hashBuffer = await crypto.subtle.digest('SHA-256', data);
      const hashArray = Array.from(new Uint8Array(hashBuffer));
      return hashArray.map(b => b.toString(16).padStart(2, '0')).join('');
    };

    const syncVehicleState = async (vid) => {
      if (!vid) return;
      const v = await env.DB.prepare('SELECT * FROM vehicles WHERE id = ?').bind(vid).first();
      if (!v) return;
      const initM = parseFloat(v.initial_mileage || 0);
      const initFuel = v.initial_fuel_percent !== null && v.initial_fuel_percent !== undefined ? parseFloat(v.initial_fuel_percent) : 0;
      const initBat = v.initial_battery_percent !== null && v.initial_battery_percent !== undefined ? parseFloat(v.initial_battery_percent) : 100;

      // 权威获取最后一次录入的状态：合并 status_records, fuel_records, electricity_records 按时间降序, id降序
      // 字段统一化：
      // status: type='status', mileage, fuel_percent, battery_percent, record_date, id
      // fuel: type='fuel', mileage, fuel_percent, battery_percent, record_date, id
      // elec: type='elec', mileage, fuel_percent, battery_percent=end_soc, record_date, id
      let latestRecord = null;
      let allRecords = [];
      try {
        const query = "SELECT \x27status\x27 as rec_type, id, mileage, fuel_percent, battery_percent, record_date FROM status_records WHERE vehicle_id = ? UNION ALL SELECT \x27fuel\x27 as rec_type, id, mileage, fuel_percent, battery_percent, record_date FROM fuel_records WHERE vehicle_id = ? UNION ALL SELECT \x27elec\x27 as rec_type, id, mileage, fuel_percent, end_soc as battery_percent, record_date FROM electricity_records WHERE vehicle_id = ? ORDER BY record_date DESC, id DESC";
        const res = await env.DB.prepare(query).bind(vid, vid, vid).all();
        allRecords = res?.results || [];
      } catch (err) {
        // 容错：若 status_records 暂未建表或字段缺失
        try {
          const fallbackQuery = "SELECT \x27fuel\x27 as rec_type, id, mileage, fuel_percent, battery_percent, record_date FROM fuel_records WHERE vehicle_id = ? UNION ALL SELECT \x27elec\x27 as rec_type, id, mileage, fuel_percent, end_soc as battery_percent, record_date FROM electricity_records WHERE vehicle_id = ? ORDER BY record_date DESC, id DESC";
          const res = await env.DB.prepare(fallbackQuery).bind(vid, vid).all();
          allRecords = res?.results || [];
        } catch (e2) {}
      }

      let currMileage = parseFloat(v.current_mileage || initM);
      let currFuelPct = v.current_fuel_percent;
      let currBatPct = v.current_battery_percent;

      if (allRecords.length > 0) {
        latestRecord = allRecords[0];
        if (latestRecord.mileage !== null && latestRecord.mileage !== undefined) {
          currMileage = parseFloat(latestRecord.mileage) || currMileage;
        }

        // 1. 若最后一次为 status_records：直接采用录入数据
        if (latestRecord.rec_type === 'status') {
          if (latestRecord.fuel_percent !== null && latestRecord.fuel_percent !== undefined) currFuelPct = latestRecord.fuel_percent;
          if (latestRecord.battery_percent !== null && latestRecord.battery_percent !== undefined) currBatPct = latestRecord.battery_percent;
        }
        // 2. 若最后一次为 fuel_records：总里程为录入里程，油量为加油结束百分比
        else if (latestRecord.rec_type === 'fuel') {
          if (latestRecord.fuel_percent !== null && latestRecord.fuel_percent !== undefined) currFuelPct = latestRecord.fuel_percent;
          if (latestRecord.battery_percent !== null && latestRecord.battery_percent !== undefined) currBatPct = latestRecord.battery_percent;
        }
        // 3. 若最后一次为 electricity_records：总里程为录入里程，电量为充电结束百分比 (end_soc)
        else if (latestRecord.rec_type === 'elec') {
          if (latestRecord.battery_percent !== null && latestRecord.battery_percent !== undefined) currBatPct = latestRecord.battery_percent;
          if (latestRecord.fuel_percent !== null && latestRecord.fuel_percent !== undefined) currFuelPct = latestRecord.fuel_percent;
        }

        // 回溯寻找未指定的电量
        if (currBatPct === null || currBatPct === undefined) {
          for (const r of allRecords) {
            if (r.battery_percent !== null && r.battery_percent !== undefined) {
              currBatPct = r.battery_percent;
              break;
            }
          }
        }
        // 回溯寻找未指定的油量
        if (currFuelPct === null || currFuelPct === undefined) {
          for (const r of allRecords) {
            if (r.fuel_percent !== null && r.fuel_percent !== undefined) {
              currFuelPct = r.fuel_percent;
              break;
            }
          }
        }
      }

      if (currFuelPct === null || currFuelPct === undefined) currFuelPct = initFuel;
      if (currBatPct === null || currBatPct === undefined) currBatPct = initBat;

      const now = new Date().toISOString().replace('T', ' ').slice(0, 19);
      await env.DB.prepare('UPDATE vehicles SET current_mileage = ?, current_fuel_percent = ?, current_battery_percent = ?, updated_at = ? WHERE id = ?')
        .bind(currMileage, currFuelPct, currBatPct, now, vid).run();
    };

    // 兼容函数别名，确保所有业务接口调用平滑
    const syncVehicleMileage = syncVehicleState;

    // 百度地图周边搜索代理 (解决前端跨域与受限网络兜底)
    if (path === '/api/baidu-search' && method === 'GET') {
      const qTerm = url.searchParams.get('query') || '充电站';
      const lat = url.searchParams.get('lat') || '';
      const lng = url.searchParams.get('lng') || '';
      const radius = url.searchParams.get('radius') || '15000';
      const pageSize = url.searchParams.get('page_size') || '20';
      const ak = url.searchParams.get('ak') || '3YJcotkPHfGjr1vN50trOKrHoWUAwWCS';
      if (!lat || !lng) {
        return err('缺少 lat/lng 坐标参数', 400);
      }

      // 构造百度地图 Place API 请求
      // 经验证：Baidu Place API 免费配额要求 query 在前、output=json 在后，参数严格 URL 编码
      const targetUrls = [
        `https://api.map.baidu.com/place/v2/search?query=${encodeURIComponent(qTerm)}&location=${lat},${lng}&radius=${radius}&output=json&page_size=${pageSize}&ak=${ak}&coord_type=2&ret_coordtype=gcj02ll`,
        `http://api.map.baidu.com/place/v2/search?query=${encodeURIComponent(qTerm)}&location=${lat},${lng}&radius=${radius}&output=json&page_size=${pageSize}&ak=${ak}&coord_type=2&ret_coordtype=gcj02ll`
      ];

      for (const targetUrl of targetUrls) {
        try {
          const res = await fetch(targetUrl, {
            headers: {
              'User-Agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36'
            }
          });
          const text = await res.text();
          if (text.includes('"status":0') || text.includes('"status": 0')) {
            const data = JSON.parse(text);
            return json(data);
          }
        } catch (e) {}
      }

      return err('请求百度地图接口失败或无响应', 502);
    }

    // 检查 D1 绑定
    if (!env.DB) {
      return err('Cloudflare D1 数据库绑定未配置，请检查 wrangler.toml 中的 [[d1_databases]] binding = "DB"', 500);
    }

    try {
      // 数据库平滑迁移检查 (添加 current_fuel_percent, current_battery_percent, status_records)
      try {
        await env.DB.prepare('ALTER TABLE vehicles ADD COLUMN current_fuel_percent REAL').run();
      } catch (e) {}
      try {
        await env.DB.prepare('ALTER TABLE vehicles ADD COLUMN current_battery_percent REAL').run();
      } catch (e) {}
      try {
        await env.DB.prepare(`
          CREATE TABLE IF NOT EXISTS status_records (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            vehicle_id INTEGER NOT NULL,
            mileage REAL NOT NULL,
            battery_percent REAL DEFAULT NULL,
            fuel_percent REAL DEFAULT NULL,
            record_date TEXT NOT NULL,
            notes TEXT DEFAULT '',
            created_at TEXT NOT NULL
          )
        `).run();
      } catch (e) {}

      // 用户认证表与初始账号 AubreyHan 自动初始化
      try {
        await env.DB.prepare(`
          CREATE TABLE IF NOT EXISTS users (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            username TEXT UNIQUE NOT NULL,
            password_hash TEXT NOT NULL,
            salt TEXT NOT NULL,
            created_at TEXT NOT NULL,
            updated_at TEXT NOT NULL
          )
        `).run();

        const existingUser = await env.DB.prepare('SELECT id FROM users WHERE username = ?').bind('AubreyHan').first();
        if (!existingUser) {
          const salt = AUTH_SALT_DEFAULT;
          const pwdHash = await hashPassword('P@ss=w0rd', salt);
          const now = new Date().toISOString().replace('T', ' ').slice(0, 19);
          await env.DB.prepare(`
            INSERT INTO users (username, password_hash, salt, created_at, updated_at)
            VALUES (?, ?, ?, ?, ?)
          `).bind('AubreyHan', pwdHash, salt, now, now).run();
        }
      } catch (e) {
        console.warn('初始化 users 表异常:', e);
      }

      // ---------------- 用户认证接口 (Auth) ----------------
      if (path === '/api/auth/login' && method === 'POST') {
        const body = await request.json();
        const username = (body.username || '').trim();
        const password = body.password || '';
        if (!username || !password) return err('用户名和密码不能为空');

        const user = await env.DB.prepare('SELECT * FROM users WHERE username = ?').bind(username).first();
        if (!user) return err('用户名或密码错误', 401);

        const salt = user.salt || AUTH_SALT_DEFAULT;
        const testHash = await hashPassword(password, salt);
        if (testHash === user.password_hash) {
          const token = await makeToken(user.username, user.password_hash);
          return json({ success: true, token, user: { username: user.username } });
        }
        return err('用户名或密码错误', 401);
      }

      if (path === '/api/auth/change-password' && method === 'POST') {
        const body = await request.json();
        const username = (body.username || '').trim();
        const oldPassword = body.old_password || '';
        const newPassword = body.new_password || '';
        if (!username || !oldPassword || !newPassword) return err('请完整填写原密码与新密码');
        if (newPassword.length < 4) return err('新密码长度不能少于4位');

        const user = await env.DB.prepare('SELECT * FROM users WHERE username = ?').bind(username).first();
        if (!user) return err('用户不存在', 404);

        const salt = user.salt || AUTH_SALT_DEFAULT;
        const testHash = await hashPassword(oldPassword, salt);
        if (testHash !== user.password_hash) return err('原密码错误', 400);

        const newHash = await hashPassword(newPassword, salt);
        const now = new Date().toISOString().replace('T', ' ').slice(0, 19);
        await env.DB.prepare('UPDATE users SET password_hash = ?, updated_at = ? WHERE id = ?').bind(newHash, now, user.id).run();
        const newToken = await makeToken(username, newHash);
        return json({ success: true, token: newToken, message: '密码修改成功' });
      }

      if (path === '/api/auth/verify' && method === 'POST') {
        const body = await request.json();
        const token = body.token || '';
        if (!token) return err('未提供认证凭据', 401);

        const { results: users } = await env.DB.prepare('SELECT username, password_hash FROM users').all();
        for (const u of (users || [])) {
          const expected = await makeToken(u.username, u.password_hash);
          if (expected === token) {
            return json({ success: true, authenticated: true, user: { username: u.username } });
          }
        }
        return err('登录态已失效或无效', 401);
      }

      // ---------------- 车辆接口 (Vehicles) ----------------
      if (path === '/api/vehicles' && method === 'GET') {
        const { results } = await env.DB.prepare(
          'SELECT * FROM vehicles ORDER BY id DESC'
        ).all();
        return json(results);
      }

      if (path === '/api/vehicles' && method === 'POST') {
        const body = await request.json();
        if (!body.name || !body.brand) return err('车名与品牌为必填项');

        const now = new Date().toISOString().replace('T', ' ').slice(0, 19);
        const initM = parseFloat(body.initial_mileage || 0);
        const currM = body.current_mileage !== undefined && body.current_mileage !== null ? parseFloat(body.current_mileage) : initM;
        const initFuel = parseFloat(body.initial_fuel_percent || 0);
        const initBat = parseFloat(body.initial_battery_percent || 0);
        const currFuel = body.current_fuel_percent !== undefined && body.current_fuel_percent !== null ? parseFloat(body.current_fuel_percent) : initFuel;
        const currBat = body.current_battery_percent !== undefined && body.current_battery_percent !== null ? parseFloat(body.current_battery_percent) : initBat;

        const res = await env.DB.prepare(`
          INSERT INTO vehicles (
            name, brand, model, power_type, displacement,
            fuel_tank_capacity, battery_capacity, plate_number,
            initial_mileage, current_mileage, purchase_date, notes,
            initial_fuel_percent, initial_battery_percent,
            current_fuel_percent, current_battery_percent,
            created_at, updated_at
          ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        `).bind(
          body.name,
          body.brand,
          body.model || '',
          body.power_type || 'gas',
          body.displacement || '',
          parseFloat(body.fuel_tank_capacity || 0),
          parseFloat(body.battery_capacity || 0),
          body.plate_number || '',
          initM,
          currM,
          body.purchase_date || '',
          body.notes || '',
          initFuel,
          initBat,
          currFuel,
          currBat,
          now,
          now
        ).run();

        const newId = res.meta.last_row_id;
        const newVehicle = await env.DB.prepare('SELECT * FROM vehicles WHERE id = ?').bind(newId).first();
        return json(newVehicle, 201);
      }

      if (path.startsWith('/api/vehicles/') && method === 'PUT') {
        const id = parseInt(path.split('/')[3]);
        const body = await request.json();
        const now = new Date().toISOString().replace('T', ' ').slice(0, 19);

        const sets = [
          'name = ?', 'brand = ?', 'model = ?', 'power_type = ?', 'displacement = ?',
          'fuel_tank_capacity = ?', 'battery_capacity = ?', 'plate_number = ?',
          'initial_mileage = ?', 'purchase_date = ?', 'notes = ?',
          'initial_fuel_percent = ?', 'initial_battery_percent = ?', 'updated_at = ?'
        ];
        const vals = [
          body.name,
          body.brand,
          body.model || '',
          body.power_type || 'gas',
          body.displacement || '',
          parseFloat(body.fuel_tank_capacity || 0),
          parseFloat(body.battery_capacity || 0),
          body.plate_number || '',
          parseFloat(body.initial_mileage || 0),
          body.purchase_date || '',
          body.notes || '',
          parseFloat(body.initial_fuel_percent || 0),
          parseFloat(body.initial_battery_percent || 0),
          now
        ];

        if (body.current_mileage !== undefined && body.current_mileage !== null && body.current_mileage !== '') {
          sets.push('current_mileage = ?');
          vals.push(parseFloat(body.current_mileage));
        }
        if (body.current_fuel_percent !== undefined && body.current_fuel_percent !== null && body.current_fuel_percent !== '') {
          sets.push('current_fuel_percent = ?');
          vals.push(parseFloat(body.current_fuel_percent));
        }
        if (body.current_battery_percent !== undefined && body.current_battery_percent !== null && body.current_battery_percent !== '') {
          sets.push('current_battery_percent = ?');
          vals.push(parseFloat(body.current_battery_percent));
        }
        vals.push(id);

        await env.DB.prepare(`UPDATE vehicles SET ${sets.join(', ')} WHERE id = ?`).bind(...vals).run();
        await syncVehicleMileage(id);

        const updated = await env.DB.prepare('SELECT * FROM vehicles WHERE id = ?').bind(id).first();
        return json({ success: true, vehicle: updated });
      }

      if (path.startsWith('/api/vehicles/') && method === 'DELETE') {
        const id = parseInt(path.split('/')[3]);
        await env.DB.prepare('DELETE FROM vehicles WHERE id = ?').bind(id).run();
        return json({ success: true });
      }

      // ---------------- 油耗记录 (Fuel Records) ----------------
      if (path === '/api/records/fuel' && method === 'GET') {
        const vid = url.searchParams.get('vehicle_id');
        let query = 'SELECT f.*, v.name as vehicle_name, v.plate_number FROM fuel_records f JOIN vehicles v ON f.vehicle_id = v.id';
        let stmt;
        if (vid) {
          query += ' WHERE f.vehicle_id = ? ORDER BY f.mileage DESC, f.record_date DESC';
          stmt = env.DB.prepare(query).bind(parseInt(vid));
        } else {
          query += ' ORDER BY f.record_date DESC';
          stmt = env.DB.prepare(query);
        }
        const { results } = await stmt.all();
        return json(results);
      }

      if (path === '/api/records/fuel' && method === 'POST') {
        const b = await request.json();
        const now = new Date().toISOString().replace('T', ' ').slice(0, 19);
        const vid = parseInt(b.vehicle_id);
        const m = parseFloat(b.mileage);

        const res = await env.DB.prepare(`
          INSERT INTO fuel_records (
            vehicle_id, mileage, fuel_amount, unit_price, total_price,
            fuel_type, is_full, start_fuel_percent, fuel_percent, battery_percent, station, record_date, notes, created_at
          ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        `).bind(
          vid,
          m,
          parseFloat(b.fuel_amount),
          parseFloat(b.unit_price),
          parseFloat(b.total_price),
          b.fuel_type || '92#',
          parseInt(b.is_full ?? 1),
          b.start_fuel_percent !== undefined && b.start_fuel_percent !== null && b.start_fuel_percent !== '' ? parseFloat(b.start_fuel_percent) : null,
          b.fuel_percent !== undefined && b.fuel_percent !== null && b.fuel_percent !== '' ? parseFloat(b.fuel_percent) : 100,
          b.battery_percent !== undefined && b.battery_percent !== null && b.battery_percent !== '' ? parseFloat(b.battery_percent) : null,
          b.station || '',
          b.record_date || now.slice(0, 16),
          b.notes || '',
          now
        ).run();

        // 刷新车辆当前最新最高里程
        await env.DB.prepare(`
          UPDATE vehicles SET current_mileage = MAX(current_mileage, ?), updated_at = ? WHERE id = ?
        `).bind(m, now, vid).run();

        return json({ success: true, id: res.meta.last_row_id }, 201);
      }

      if (path.startsWith('/api/records/fuel/') && method === 'PUT') {
        const id = parseInt(path.split('/')[4]);
        const b = await request.json();
        const rec = await env.DB.prepare('SELECT vehicle_id FROM fuel_records WHERE id = ?').bind(id).first();
        if (!rec) return err('记录不存在', 404);
        const vid = rec.vehicle_id;

        await env.DB.prepare(`
          UPDATE fuel_records SET
            mileage = ?, fuel_amount = ?, unit_price = ?, total_price = ?,
            fuel_type = ?, is_full = ?, start_fuel_percent = ?, fuel_percent = ?, battery_percent = ?, station = ?,
            record_date = ?, notes = ?
          WHERE id = ?
        `).bind(
          parseFloat(b.mileage),
          parseFloat(b.fuel_amount),
          parseFloat(b.unit_price),
          parseFloat(b.total_price),
          b.fuel_type || '92#',
          parseInt(b.is_full ?? 1),
          b.start_fuel_percent !== undefined && b.start_fuel_percent !== null && b.start_fuel_percent !== '' ? parseFloat(b.start_fuel_percent) : null,
          b.fuel_percent !== undefined && b.fuel_percent !== null && b.fuel_percent !== '' ? parseFloat(b.fuel_percent) : 100,
          b.battery_percent !== undefined && b.battery_percent !== null && b.battery_percent !== '' ? parseFloat(b.battery_percent) : null,
          b.station || '',
          b.record_date || '',
          b.notes || '',
          id
        ).run();

        await syncVehicleMileage(vid);
        const updated = await env.DB.prepare('SELECT * FROM fuel_records WHERE id = ?').bind(id).first();
        return json({ success: true, record: updated });
      }

      if (path.startsWith('/api/records/fuel/') && method === 'DELETE') {
        const id = parseInt(path.split('/')[4]);
        const rec = await env.DB.prepare('SELECT vehicle_id FROM fuel_records WHERE id = ?').bind(id).first();
        await env.DB.prepare('DELETE FROM fuel_records WHERE id = ?').bind(id).run();
        if (rec) await syncVehicleMileage(rec.vehicle_id);
        return json({ success: true });
      }

      // ---------------- 电耗记录 (Electricity Records) ----------------
      if (path === '/api/records/electricity' && method === 'GET') {
        const vid = url.searchParams.get('vehicle_id');
        let query = 'SELECT e.*, v.name as vehicle_name, v.plate_number FROM electricity_records e JOIN vehicles v ON e.vehicle_id = v.id';
        let stmt;
        if (vid) {
          query += ' WHERE e.vehicle_id = ? ORDER BY e.mileage DESC, e.record_date DESC';
          stmt = env.DB.prepare(query).bind(parseInt(vid));
        } else {
          query += ' ORDER BY e.record_date DESC';
          stmt = env.DB.prepare(query);
        }
        const { results } = await stmt.all();
        return json(results);
      }

      if (path === '/api/records/electricity' && method === 'POST') {
        const b = await request.json();
        const now = new Date().toISOString().replace('T', ' ').slice(0, 19);
        const vid = parseInt(b.vehicle_id);
        const m = parseFloat(b.mileage);
        const amount = parseFloat(b.electricity_amount);
        const startSoc = b.start_soc !== null && b.start_soc !== '' ? parseFloat(b.start_soc) : null;
        const endSoc = b.end_soc !== null && b.end_soc !== '' ? parseFloat(b.end_soc) : null;

        const isCalibrated = (b.is_calibrated === 1 || b.is_calibrated === '1' || b.is_calibrated === true) ? 1 : 0;
        let calibratedCapacity = null;
        if (isCalibrated) {
          if (b.calibrated_capacity !== null && b.calibrated_capacity !== undefined && b.calibrated_capacity !== '') {
            calibratedCapacity = Math.round(parseFloat(b.calibrated_capacity) * 100) / 100;
          } else if (endSoc !== null && startSoc !== null && endSoc > startSoc) {
            const deltaSoc = endSoc - startSoc;
            calibratedCapacity = Math.round((amount / deltaSoc) * 10000) / 100;
          }
        }

        const res = await env.DB.prepare(`
          INSERT INTO electricity_records (
            vehicle_id, mileage, electricity_amount, unit_price, total_price,
            charging_type, start_soc, end_soc, fuel_percent, station,
            is_calibrated, calibrated_capacity, record_date, notes, created_at
          ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        `).bind(
          vid,
          m,
          amount,
          parseFloat(b.unit_price),
          parseFloat(b.total_price),
          b.charging_type || '快充',
          startSoc,
          endSoc,
          b.fuel_percent !== undefined && b.fuel_percent !== null && b.fuel_percent !== '' ? parseFloat(b.fuel_percent) : null,
          b.station || '',
          isCalibrated,
          calibratedCapacity,
          b.record_date || now.slice(0, 16),
          b.notes || '',
          now
        ).run();

        // 刷新最高里程
        await env.DB.prepare(`
          UPDATE vehicles SET current_mileage = MAX(current_mileage, ?), updated_at = ? WHERE id = ?
        `).bind(m, now, vid).run();

        return json({ success: true, id: res.meta.last_row_id }, 201);
      }

      if (path.startsWith('/api/records/electricity/') && method === 'PUT') {
        const id = parseInt(path.split('/')[4]);
        const b = await request.json();
        const rec = await env.DB.prepare('SELECT vehicle_id FROM electricity_records WHERE id = ?').bind(id).first();
        if (!rec) return err('记录不存在', 404);
        const vid = rec.vehicle_id;
        const amount = parseFloat(b.electricity_amount);
        const startSoc = b.start_soc !== null && b.start_soc !== '' ? parseFloat(b.start_soc) : null;
        const endSoc = b.end_soc !== null && b.end_soc !== '' ? parseFloat(b.end_soc) : null;

        const isCalibrated = (b.is_calibrated === 1 || b.is_calibrated === '1' || b.is_calibrated === true) ? 1 : 0;
        let calibratedCapacity = null;
        if (isCalibrated) {
          if (b.calibrated_capacity !== null && b.calibrated_capacity !== undefined && b.calibrated_capacity !== '') {
            calibratedCapacity = Math.round(parseFloat(b.calibrated_capacity) * 100) / 100;
          } else if (endSoc !== null && startSoc !== null && endSoc > startSoc) {
            const deltaSoc = endSoc - startSoc;
            calibratedCapacity = Math.round((amount / deltaSoc) * 10000) / 100;
          }
        }

        await env.DB.prepare(`
          UPDATE electricity_records SET
            mileage = ?, electricity_amount = ?, unit_price = ?, total_price = ?,
            charging_type = ?, start_soc = ?, end_soc = ?, fuel_percent = ?, station = ?,
            is_calibrated = ?, calibrated_capacity = ?, record_date = ?, notes = ?
          WHERE id = ?
        `).bind(
          parseFloat(b.mileage),
          amount,
          parseFloat(b.unit_price),
          parseFloat(b.total_price),
          b.charging_type || '快充',
          startSoc,
          endSoc,
          b.fuel_percent !== undefined && b.fuel_percent !== null && b.fuel_percent !== '' ? parseFloat(b.fuel_percent) : null,
          b.station || '',
          isCalibrated,
          calibratedCapacity,
          b.record_date || '',
          b.notes || '',
          id
        ).run();

        await syncVehicleMileage(vid);
        const updated = await env.DB.prepare('SELECT * FROM electricity_records WHERE id = ?').bind(id).first();
        return json({ success: true, record: updated });
      }

      if (path.startsWith('/api/records/electricity/') && method === 'DELETE') {
        const id = parseInt(path.split('/')[4]);
        const rec = await env.DB.prepare('SELECT vehicle_id FROM electricity_records WHERE id = ?').bind(id).first();
        await env.DB.prepare('DELETE FROM electricity_records WHERE id = ?').bind(id).run();
        if (rec) await syncVehicleMileage(rec.vehicle_id);
        return json({ success: true });
      }

      // ---------------- 维护记录 (Maintenance Records) ----------------
      if (path === '/api/records/maintenance' && method === 'GET') {
        const vid = url.searchParams.get('vehicle_id');
        let query = 'SELECT m.*, v.name as vehicle_name, v.plate_number FROM maintenance_records m JOIN vehicles v ON m.vehicle_id = v.id';
        let stmt;
        if (vid) {
          query += ' WHERE m.vehicle_id = ? ORDER BY m.record_date DESC';
          stmt = env.DB.prepare(query).bind(parseInt(vid));
        } else {
          query += ' ORDER BY m.record_date DESC';
          stmt = env.DB.prepare(query);
        }
        const { results } = await stmt.all();
        return json(results);
      }

      if (path === '/api/records/maintenance' && method === 'POST') {
        const b = await request.json();
        const now = new Date().toISOString().replace('T', ' ').slice(0, 19);
        const vid = parseInt(b.vehicle_id);

        const res = await env.DB.prepare(`
          INSERT INTO maintenance_records (
            vehicle_id, mileage, item_type, cost, provider,
            record_date, next_mileage_reminder, next_date_reminder, notes, created_at
          ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        `).bind(
          vid,
          parseFloat(b.mileage || 0),
          b.item_type || '常规保养',
          parseFloat(b.cost !== undefined && b.cost !== null && b.cost !== '' ? b.cost : 0),
          b.provider || '',
          b.record_date || now.slice(0, 16),
          b.next_mileage_reminder ? parseFloat(b.next_mileage_reminder) : null,
          b.next_date_reminder || '',
          b.notes || '',
          now
        ).run();

        return json({ success: true, id: res.meta.last_row_id }, 201);
      }

      if (path.startsWith('/api/records/maintenance/') && method === 'PUT') {
        const id = parseInt(path.split('/')[4]);
        const b = await request.json();
        const rec = await env.DB.prepare('SELECT vehicle_id FROM maintenance_records WHERE id = ?').bind(id).first();
        if (!rec) return err('记录不存在', 404);
        const vid = rec.vehicle_id;

        await env.DB.prepare(`
          UPDATE maintenance_records SET
            mileage = ?, item_type = ?, cost = ?, provider = ?,
            record_date = ?, next_mileage_reminder = ?, next_date_reminder = ?, notes = ?
          WHERE id = ?
        `).bind(
          parseFloat(b.mileage || 0),
          b.item_type || '常规保养',
          parseFloat(b.cost !== undefined && b.cost !== null && b.cost !== '' ? b.cost : 0),
          b.provider || '',
          b.record_date || '',
          b.next_mileage_reminder ? parseFloat(b.next_mileage_reminder) : null,
          b.next_date_reminder || '',
          b.notes || '',
          id
        ).run();

        await syncVehicleMileage(vid);
        const updated = await env.DB.prepare('SELECT * FROM maintenance_records WHERE id = ?').bind(id).first();
        return json({ success: true, record: updated });
      }

      if (path.startsWith('/api/records/maintenance/') && method === 'DELETE') {
        const id = parseInt(path.split('/')[4]);
        const rec = await env.DB.prepare('SELECT vehicle_id FROM maintenance_records WHERE id = ?').bind(id).first();
        await env.DB.prepare('DELETE FROM maintenance_records WHERE id = ?').bind(id).run();
        if (rec) await syncVehicleMileage(rec.vehicle_id);
        return json({ success: true });
      }

      // ---------------- 状态记录 (Status Records) ----------------
      if (path === '/api/records/status' && method === 'GET') {
        const vid = url.searchParams.get('vehicle_id');
        let query = 'SELECT s.*, v.name as vehicle_name, v.plate_number FROM status_records s JOIN vehicles v ON s.vehicle_id = v.id';
        let stmt;
        if (vid) {
          query += ' WHERE s.vehicle_id = ? ORDER BY s.mileage DESC, s.record_date DESC, s.id DESC';
          stmt = env.DB.prepare(query).bind(parseInt(vid));
        } else {
          query += ' ORDER BY s.mileage DESC, s.record_date DESC, s.id DESC';
          stmt = env.DB.prepare(query);
        }
        const { results } = await stmt.all();
        return json(results);
      }

      if (path === '/api/records/status' && method === 'POST') {
        const b = await request.json();
        const now = new Date().toISOString().replace('T', ' ').slice(0, 19);
        const vid = parseInt(b.vehicle_id);
        const m = parseFloat(b.mileage);
        const batPct = b.battery_percent !== undefined && b.battery_percent !== null && b.battery_percent !== '' ? parseFloat(b.battery_percent) : null;
        const fuelPct = b.fuel_percent !== undefined && b.fuel_percent !== null && b.fuel_percent !== '' ? parseFloat(b.fuel_percent) : null;

        const res = await env.DB.prepare(`
          INSERT INTO status_records (
            vehicle_id, mileage, battery_percent, fuel_percent, record_date, notes, created_at
          ) VALUES (?, ?, ?, ?, ?, ?, ?)
        `).bind(
          vid,
          m,
          batPct,
          fuelPct,
          b.record_date || now.slice(0, 16),
          b.notes || '',
          now
        ).run();

        await syncVehicleMileage(vid);
        return json({ success: true, id: res.meta.last_row_id }, 201);
      }

      if (path.startsWith('/api/records/status/') && method === 'PUT') {
        const id = parseInt(path.split('/')[4]);
        const b = await request.json();
        const rec = await env.DB.prepare('SELECT * FROM status_records WHERE id = ?').bind(id).first();
        if (!rec) return err('记录不存在', 404);
        const vid = rec.vehicle_id;
        const m = b.mileage !== undefined && b.mileage !== null ? parseFloat(b.mileage) : rec.mileage;
        const batPct = b.battery_percent !== undefined && b.battery_percent !== null && b.battery_percent !== '' ? parseFloat(b.battery_percent) : (b.battery_percent === null ? null : rec.battery_percent);
        const fuelPct = b.fuel_percent !== undefined && b.fuel_percent !== null && b.fuel_percent !== '' ? parseFloat(b.fuel_percent) : (b.fuel_percent === null ? null : rec.fuel_percent);

        await env.DB.prepare(`
          UPDATE status_records SET
            mileage = ?, battery_percent = ?, fuel_percent = ?, record_date = ?, notes = ?
          WHERE id = ?
        `).bind(
          m,
          batPct,
          fuelPct,
          b.record_date || rec.record_date,
          b.notes !== undefined ? b.notes : rec.notes,
          id
        ).run();

        await syncVehicleMileage(vid);
        const updated = await env.DB.prepare('SELECT * FROM status_records WHERE id = ?').bind(id).first();
        return json({ success: true, record: updated });
      }

      if (path.startsWith('/api/records/status/') && method === 'DELETE') {
        const id = parseInt(path.split('/')[4]);
        const rec = await env.DB.prepare('SELECT vehicle_id FROM status_records WHERE id = ?').bind(id).first();
        await env.DB.prepare('DELETE FROM status_records WHERE id = ?').bind(id).run();
        if (rec) await syncVehicleMileage(rec.vehicle_id);
        return json({ success: true });
      }

      // 内部辅助函数：先进先出 (FIFO) 实耗金额核算
      const computeFifoCost = (consumedAmount, records, amountKey, priceKey) => {
        let rem = consumedAmount;
        let totalCost = 0.0;
        for (const r of records) {
          if (rem <= 0) break;
          const amt = parseFloat(r[amountKey]) || 0;
          const price = parseFloat(r[priceKey]) || 0;
          if (amt <= 0) continue;
          const take = Math.min(rem, amt);
          totalCost += take * price;
          rem -= take;
        }
        if (rem > 0 && records.length > 0) {
          const firstPrice = parseFloat(records[0][priceKey]) || 0;
          totalCost += rem * firstPrice;
        }
        return Number(totalCost.toFixed(2));
      };

      // 内部辅助函数：计算全量能耗时序与区间分析
      const getEnergyAnalysisData = async (targetVid, targetVehicle = null) => {
        let v = targetVehicle;
        if (!v) {
          v = await env.DB.prepare('SELECT * FROM vehicles WHERE id = ?').bind(targetVid).first();
        }
        if (!v) return null;

        const { results: fuelRows } = await env.DB.prepare(
          'SELECT * FROM fuel_records WHERE vehicle_id = ? ORDER BY mileage ASC, record_date ASC, id ASC'
        ).bind(targetVid).all();

        const { results: elecRows } = await env.DB.prepare(
          'SELECT * FROM electricity_records WHERE vehicle_id = ? ORDER BY mileage ASC, record_date ASC, id ASC'
        ).bind(targetVid).all();

        const initM = parseFloat(v.initial_mileage || 0);
        const maxFuelM = fuelRows.length ? Math.max(...fuelRows.map(r => parseFloat(r.mileage) || 0)) : 0;
        const maxElecM = elecRows.length ? Math.max(...elecRows.map(r => parseFloat(r.mileage) || 0)) : 0;
        const currM = Math.max(parseFloat(v.current_mileage || initM), maxFuelM, maxElecM);
        const totalDriven = Math.max(0, currM - initM);

        const fuelTankCap = parseFloat(v.fuel_tank_capacity || 0);
        const batteryCap = parseFloat(v.battery_capacity || 0);
        const initFuelPct = parseFloat(v.initial_fuel_percent !== undefined && v.initial_fuel_percent !== null ? v.initial_fuel_percent : 100);
        const initBatteryPct = parseFloat(v.initial_battery_percent !== undefined && v.initial_battery_percent !== null ? v.initial_battery_percent : 100);

        // 统一全量补能时序流 (严格按时间与里程时序流动核算双能耗)
        const unifiedRecords = [];
        fuelRows.forEach(r => {
          unifiedRecords.push({
            _type: 'fuel',
            data: r,
            mileage: parseFloat(r.mileage) || 0,
            date: r.record_date ? new Date(r.record_date.replace(/-/g, '/')) : null
          });
        });
        elecRows.forEach(r => {
          unifiedRecords.push({
            _type: 'electricity',
            data: r,
            mileage: parseFloat(r.mileage) || 0,
            date: r.record_date ? new Date(r.record_date.replace(/-/g, '/')) : null
          });
        });
        unifiedRecords.sort((a, b) => (a.mileage - b.mileage) || ((a.date || 0) - (b.date || 0)) || (a.data.id - b.data.id));

        let prevM = initM;
        let prevDate = null;
        let prevFuelEnd = initFuelPct;
        let prevBatEnd = initBatteryPct;
        let cumFuelConsumed = 0.0;
        let cumElecConsumed = 0.0;
        let activeBatCap = batteryCap;

        let fuelOrder = 0;
        let elecOrder = 0;

        const fuelIntervals = [];
        const elecIntervals = [];
        const allIntervals = [];
        const allSeenFuels = [];
        const allSeenElecs = [];

        unifiedRecords.forEach(item => {
          const r = item.data;
          const itemM = item.mileage;
          const currDate = item.date;
          const deltaM = Math.max(0, itemM - prevM);
          const daysDiff = (prevDate && currDate) ? Math.max(0, Math.floor((currDate - prevDate) / (1000 * 3600 * 24))) : null;
          const driveBatCap = activeBatCap;

          let currStartFuelPct, currEndFuelPct, currStartBatPct, currEndBatPct;
          const amount = parseFloat(item._type === 'fuel' ? r.fuel_amount : r.electricity_amount) || 0;
          const cost = parseFloat(r.total_price) || 0;

          if (item._type === 'fuel') {
            fuelOrder += 1;
            allSeenFuels.push(r);
            currEndFuelPct = parseFloat(r.fuel_percent !== undefined && r.fuel_percent !== null ? r.fuel_percent : 100);
            if (r.start_fuel_percent !== undefined && r.start_fuel_percent !== null && r.start_fuel_percent !== '') {
              currStartFuelPct = parseFloat(r.start_fuel_percent);
            } else if (fuelTankCap > 0) {
              currStartFuelPct = Math.max(0, currEndFuelPct - (amount / fuelTankCap) * 100);
            } else {
              currStartFuelPct = prevFuelEnd;
            }
            const currBatVal = (r.battery_percent !== undefined && r.battery_percent !== null && r.battery_percent !== '') ? parseFloat(r.battery_percent) : null;
            currStartBatPct = currBatVal !== null ? currBatVal : prevBatEnd;
            currEndBatPct = currStartBatPct;
          } else {
            elecOrder += 1;
            allSeenElecs.push(r);
            currEndBatPct = parseFloat(r.end_soc !== undefined && r.end_soc !== null ? r.end_soc : 100);
            if (r.start_soc !== undefined && r.start_soc !== null && r.start_soc !== '') {
              currStartBatPct = parseFloat(r.start_soc);
            } else if (activeBatCap > 0) {
              currStartBatPct = Math.max(0, currEndBatPct - (amount / activeBatCap) * 100);
            } else {
              currStartBatPct = prevBatEnd;
            }
            const currFuelVal = (r.fuel_percent !== undefined && r.fuel_percent !== null && r.fuel_percent !== '') ? parseFloat(r.fuel_percent) : null;
            currStartFuelPct = currFuelVal !== null ? currFuelVal : prevFuelEnd;
            currEndFuelPct = currStartFuelPct;
          }

          const deltaFuelPct = Math.max(0, prevFuelEnd - currStartFuelPct);
          const deltaBatPct = Math.max(0, prevBatEnd - currStartBatPct);

          const consumedFuel = fuelTankCap > 0 ? Number(((deltaFuelPct / 100.0) * fuelTankCap).toFixed(2)) : 0;
          const consumedElec = driveBatCap > 0 ? Number(((deltaBatPct / 100.0) * driveBatCap).toFixed(2)) : 0;

          // 电池矫正：若本次充电设置了电池矫正，从本次充电结束时刻起，更新生效电池容量，只影响后续区间
          let isCal = (r.is_calibrated === 1 || r.is_calibrated === '1' || r.is_calibrated === true) ? 1 : 0;
          let calCap = null;
          if (isCal && r.calibrated_capacity !== null && r.calibrated_capacity !== undefined && r.calibrated_capacity !== '') {
            calCap = Math.round(parseFloat(r.calibrated_capacity) * 100) / 100;
            if (calCap > 0) {
              activeBatCap = calCap;
            }
          }
          const consumedEquivElec = Number((consumedElec + consumedFuel * 3.3).toFixed(2));

          cumFuelConsumed += consumedFuel;
          cumElecConsumed += consumedElec;
          const cumEquivElecConsumed = Number((cumElecConsumed + cumFuelConsumed * 3.3).toFixed(2));

          const cumM = Math.max(0, itemM - initM);
          const cumFuelCons = cumM > 0 ? Number(((cumFuelConsumed / cumM) * 100).toFixed(2)) : 0;
          const cumElecCons = cumM > 0 ? Number(((cumElecConsumed / cumM) * 100).toFixed(2)) : 0;
          const cumEquivElecCons = cumM > 0 ? Number(((cumEquivElecConsumed / cumM) * 100).toFixed(2)) : 0;

          const intervalFuelCons = deltaM > 0 ? Number(((consumedFuel / deltaM) * 100).toFixed(2)) : 0;
          const intervalElecCons = deltaM > 0 ? Number(((consumedElec / deltaM) * 100).toFixed(2)) : 0;
          const intervalEquivElecCons = deltaM > 0 ? Number(((consumedEquivElec / deltaM) * 100).toFixed(2)) : 0;
          const costKm = deltaM > 0 ? Number((cost / deltaM).toFixed(2)) : 0;

          // 核心算法：先进先出 (FIFO) 结算补能前实际所消耗电费与油费
          const fifoFuelCost = computeFifoCost(cumFuelConsumed, allSeenFuels, 'fuel_amount', 'unit_price');
          const fifoElecCost = computeFifoCost(cumElecConsumed, allSeenElecs, 'electricity_amount', 'unit_price');
          const fifoEnergyCost = Number((fifoFuelCost + fifoElecCost).toFixed(2));
          const fifoCostPerKm = cumM > 0 ? Number((fifoEnergyCost / cumM).toFixed(2)) : 0;
          const fifoCostPerHundred = cumM > 0 ? Number(((fifoEnergyCost / cumM) * 100).toFixed(2)) : 0;

          const intervalItem = {
            order: item._type === 'fuel' ? fuelOrder : elecOrder,
            type: item._type,
            id: r.id,
            record_date: r.record_date,
            mileage: itemM,
            prev_mileage: prevM,
            delta_mileage: Number(deltaM.toFixed(1)),
            amount: amount,
            consumed_amount: item._type === 'fuel' ? consumedFuel : consumedElec,
            interval_fuel_consumed: consumedFuel,
            interval_fuel_consumption: intervalFuelCons,
            interval_elec_consumed: consumedElec,
            interval_elec_consumption: intervalElecCons,
            interval_equivalent_elec_consumed: consumedEquivElec,
            interval_equivalent_elec_consumption: intervalEquivElecCons,
            fifo_fuel_cost: fifoFuelCost,
            fifo_elec_cost: fifoElecCost,
            fifo_energy_cost: fifoEnergyCost,
            fifo_cost_per_km: fifoCostPerKm,
            fifo_cost_per_hundred: fifoCostPerHundred,
            prev_fuel_percent: Number(prevFuelEnd.toFixed(1)),
            curr_start_fuel_percent: Number(currStartFuelPct.toFixed(1)),
            curr_end_fuel_percent: Number(currEndFuelPct.toFixed(1)),
            prev_battery_percent: Number(prevBatEnd.toFixed(1)),
            curr_start_battery_percent: Number(currStartBatPct.toFixed(1)),
            curr_end_battery_percent: Number(currEndBatPct.toFixed(1)),
            delta_fuel_percent: Number(deltaFuelPct.toFixed(1)),
            delta_battery_percent: Number(deltaBatPct.toFixed(1)),
            prev_percent: Number((item._type === 'fuel' ? prevFuelEnd : prevBatEnd).toFixed(1)),
            curr_start_percent: Number((item._type === 'fuel' ? currStartFuelPct : currStartBatPct).toFixed(1)),
            curr_end_percent: Number((item._type === 'fuel' ? currEndFuelPct : currEndBatPct).toFixed(1)),
            fuel_percent: Number(currEndFuelPct.toFixed(1)),
            battery_percent: Number(currEndBatPct.toFixed(1)),
            cumulative_mileage: Number(cumM.toFixed(1)),
            cumulative_consumed_amount: Number((item._type === 'fuel' ? cumFuelConsumed : cumElecConsumed).toFixed(2)),
            cumulative_consumption: item._type === 'fuel' ? cumFuelCons : cumElecCons,
            cumulative_fuel_consumed: Number(cumFuelConsumed.toFixed(2)),
            cumulative_fuel_consumption: cumFuelCons,
            cumulative_elec_consumed: Number(cumElecConsumed.toFixed(2)),
            cumulative_elec_consumption: cumElecCons,
            cumulative_equivalent_elec_consumed: cumEquivElecConsumed,
            cumulative_equivalent_elec_consumption: cumEquivElecCons,
            unit: item._type === 'fuel' ? 'L' : 'kWh',
            unit_price: r.unit_price,
            total_price: r.total_price,
            fuel_type: r.fuel_type,
            charging_type: r.charging_type,
            station: r.station || '',
            is_calibrated: item._type === 'electricity' ? isCal : 0,
            calibrated_capacity: item._type === 'electricity' ? calCap : null,
            active_battery_capacity: activeBatCap,
            drive_battery_capacity: driveBatCap,
            interval_consumption: item._type === 'fuel' ? intervalFuelCons : intervalElecCons,
            cost_per_km: costKm,
            days_since_prev: daysDiff
          };

          if (item._type === 'fuel') {
            fuelIntervals.push(intervalItem);
          } else {
            elecIntervals.push(intervalItem);
          }
          allIntervals.push(intervalItem);

          prevM = itemM;
          prevDate = currDate;
          prevFuelEnd = currEndFuelPct;
          prevBatEnd = currEndBatPct;
        });

        // 检查从最后一次补能记录到车辆当前最新状态的行驶消耗 (在途/未补能状态)
        const ongoingDeltaM = Number(Math.max(0, currM - prevM).toFixed(1));
        const currFuelPct = v.current_fuel_percent !== undefined && v.current_fuel_percent !== null && v.current_fuel_percent !== '' ? parseFloat(v.current_fuel_percent) : prevFuelEnd;
        const currBatPct = v.current_battery_percent !== undefined && v.current_battery_percent !== null && v.current_battery_percent !== '' ? parseFloat(v.current_battery_percent) : prevBatEnd;

        const deltaFuelPct = Math.max(0, prevFuelEnd - currFuelPct);
        const deltaBatPct = Math.max(0, prevBatEnd - currBatPct);

        const ongoingFuelConsumed = fuelTankCap > 0 ? Number(((deltaFuelPct / 100) * fuelTankCap).toFixed(2)) : 0.0;
        const ongoingElecConsumed = activeBatCap > 0 ? Number(((deltaBatPct / 100) * activeBatCap).toFixed(2)) : 0.0;
        const ongoingEquivElecConsumed = Number((ongoingElecConsumed + ongoingFuelConsumed * 3.3).toFixed(2));

        cumFuelConsumed += ongoingFuelConsumed;
        cumElecConsumed += ongoingElecConsumed;
        const totalEquivElecConsumed = Number((cumElecConsumed + cumFuelConsumed * 3.3).toFixed(2));

        const fifoFuelCost = computeFifoCost(cumFuelConsumed, allSeenFuels, 'fuel_amount', 'unit_price');
        const fifoElecCost = computeFifoCost(cumElecConsumed, allSeenElecs, 'electricity_amount', 'unit_price');
        const fifoEnergyCost = Number((fifoFuelCost + fifoElecCost).toFixed(2));

        let ongoingItem = null;
        if (ongoingDeltaM > 0 || ongoingFuelConsumed > 0 || ongoingElecConsumed > 0) {
          ongoingItem = {
            order: allIntervals.length + 1,
            type: 'ongoing',
            id: 'current_status',
            record_date: v.updated_at || '当前最新状态',
            mileage: currM,
            prev_mileage: prevM,
            delta_mileage: ongoingDeltaM,
            amount: 0,
            consumed_amount: v.power_type === 'ev' ? ongoingElecConsumed : (v.power_type === 'gas' ? ongoingFuelConsumed : ongoingEquivElecConsumed),
            interval_fuel_consumed: ongoingFuelConsumed,
            interval_fuel_consumption: ongoingDeltaM > 0 ? Number(((ongoingFuelConsumed / ongoingDeltaM) * 100).toFixed(2)) : 0.0,
            interval_elec_consumed: ongoingElecConsumed,
            interval_elec_consumption: ongoingDeltaM > 0 ? Number(((ongoingElecConsumed / ongoingDeltaM) * 100).toFixed(2)) : 0.0,
            interval_equivalent_elec_consumed: ongoingEquivElecConsumed,
            interval_equivalent_elec_consumption: ongoingDeltaM > 0 ? Number(((ongoingEquivElecConsumed / ongoingDeltaM) * 100).toFixed(2)) : 0.0,
            fifo_fuel_cost: fifoFuelCost,
            fifo_elec_cost: fifoElecCost,
            fifo_energy_cost: fifoEnergyCost,
            fifo_cost_per_km: totalDriven > 0 ? Number((fifoEnergyCost / totalDriven).toFixed(2)) : 0.0,
            fifo_cost_per_hundred: totalDriven > 0 ? Number(((fifoEnergyCost / totalDriven) * 100).toFixed(2)) : 0.0,
            prev_fuel_percent: Number(prevFuelEnd.toFixed(1)),
            curr_start_fuel_percent: Number(prevFuelEnd.toFixed(1)),
            curr_end_fuel_percent: Number(currFuelPct.toFixed(1)),
            prev_battery_percent: Number(prevBatEnd.toFixed(1)),
            curr_start_battery_percent: Number(prevBatEnd.toFixed(1)),
            curr_end_battery_percent: Number(currBatPct.toFixed(1)),
            fuel_percent: Number(currFuelPct.toFixed(1)),
            battery_percent: Number(currBatPct.toFixed(1)),
            cumulative_mileage: Number(totalDriven.toFixed(1)),
            cumulative_consumed_amount: Number((v.power_type === 'gas' ? cumFuelConsumed : cumElecConsumed).toFixed(2)),
            cumulative_consumption: totalDriven > 0 ? Number((( (v.power_type === 'gas' ? cumFuelConsumed : cumElecConsumed) / totalDriven) * 100).toFixed(2)) : 0.0,
            cumulative_fuel_consumed: Number(cumFuelConsumed.toFixed(2)),
            cumulative_fuel_consumption: totalDriven > 0 ? Number(((cumFuelConsumed / totalDriven) * 100).toFixed(2)) : 0.0,
            cumulative_elec_consumed: Number(cumElecConsumed.toFixed(2)),
            cumulative_elec_consumption: totalDriven > 0 ? Number(((cumElecConsumed / totalDriven) * 100).toFixed(2)) : 0.0,
            cumulative_equivalent_elec_consumed: totalEquivElecConsumed,
            cumulative_equivalent_elec_consumption: totalDriven > 0 ? Number(((totalEquivElecConsumed / totalDriven) * 100).toFixed(2)) : 0.0,
            unit: v.power_type === 'gas' ? 'L' : 'kWh',
            unit_price: 0,
            total_price: 0,
            fuel_type: '',
            charging_type: '',
            station: '当前在途行驶',
            is_calibrated: 0,
            cost_per_km: 0,
            days_since_prev: 0,
            is_ongoing: true
          };
          allIntervals.push(ongoingItem);
        }

        // 燃油与电能累计百公里能耗：分母均严格使用车辆行驶总里程 (当前总里程 - 初始里程)
        const totalFuelAmount = Number(fuelRows.reduce((acc, r) => acc + (parseFloat(r.fuel_amount) || 0), 0).toFixed(2));
        const totalFuelCost = Number(fuelRows.reduce((acc, r) => acc + (parseFloat(r.total_price) || 0), 0).toFixed(2));
        const fuelDrivenKm = totalDriven;
        const totalFuelConsumed = Number(cumFuelConsumed.toFixed(2));
        const overallFuelAvg = (fuelDrivenKm > 0 && totalFuelConsumed > 0) ? Number(((totalFuelConsumed / fuelDrivenKm) * 100).toFixed(2)) : 0;

        const totalElecAmount = Number(elecRows.reduce((acc, r) => acc + (parseFloat(r.electricity_amount) || 0), 0).toFixed(2));
        const totalElecCost = Number(elecRows.reduce((acc, r) => acc + (parseFloat(r.total_price) || 0), 0).toFixed(2));
        const elecDrivenKm = totalDriven;
        const totalElecConsumed = Number(cumElecConsumed.toFixed(2));
        const overallElecAvg = (elecDrivenKm > 0 && totalElecConsumed > 0) ? Number(((totalElecConsumed / elecDrivenKm) * 100).toFixed(2)) : 0;

        const overallEquivElecAvg = (totalDriven > 0 && totalEquivElecConsumed > 0) ? Number(((totalEquivElecConsumed / totalDriven) * 100).toFixed(2)) : 0;

        const totalEnergyCost = Number((totalFuelCost + totalElecCost).toFixed(2));
        const costPerKm = totalDriven > 0 ? Number((totalEnergyCost / totalDriven).toFixed(2)) : 0;

        const validFuel = fuelIntervals.filter(x => x.interval_consumption > 0).map(x => x.interval_consumption);
        const validElec = elecIntervals.filter(x => x.interval_consumption > 0).map(x => x.interval_consumption);
        const validEquiv = allIntervals.map(x => x.interval_equivalent_elec_consumption).filter(c => c > 0);
        const bestEquivCons = validEquiv.length ? Math.min(...validEquiv) : 0;
        const worstEquivCons = validEquiv.length ? Math.max(...validEquiv) : 0;

        const allCombined = [...allIntervals].sort((a, b) => b.mileage - a.mileage);
        const allDeltas = allCombined.filter(x => x.delta_mileage > 0).map(x => x.delta_mileage);
        const avgIntervalKm = allDeltas.length > 0 ? Number((allDeltas.reduce((a, b) => a + b, 0) / allDeltas.length).toFixed(1)) : 0;
        const avgCost = allCombined.length > 0 ? Number((totalEnergyCost / allCombined.length).toFixed(2)) : 0;

        // FIFO 终极指标：以当前购车行驶总里程为基准进行核算
        const lastItem = allIntervals.length > 0 ? allIntervals[allIntervals.length - 1] : null;
        const fifoDrivenKm = totalDriven > 0 ? totalDriven : (lastItem ? lastItem.cumulative_mileage : 0.0);
        const fifoCostPerKm = fifoDrivenKm > 0 ? Number((fifoEnergyCost / fifoDrivenKm).toFixed(2)) : 0.0;
        const fifoCostPerHundred = fifoDrivenKm > 0 ? Number(((fifoEnergyCost / fifoDrivenKm) * 100).toFixed(2)) : 0.0;
        const latestReplenishMileage = (parseFloat(v.current_mileage) || (lastItem ? lastItem.mileage : initM));

        return {
          vehicle: v,
          summary: {
            total_driven_km: totalDriven,
            total_energy_cost: totalEnergyCost,
            overall_energy_cost_per_km: costPerKm,
            fifo_energy_cost: fifoEnergyCost,
            fifo_cost_per_km: fifoCostPerKm,
            fifo_cost_per_hundred: fifoCostPerHundred,
            fifo_driven_km: fifoDrivenKm,
            fifo_fuel_cost: fifoFuelCost,
            fifo_elec_cost: fifoElecCost,
            latest_replenish_mileage: latestReplenishMileage,
            total_replenish_count: allCombined.length,
            avg_interval_km: avgIntervalKm,
            avg_interval_cost: avgCost,
            fuel: {
              count: fuelRows.length,
              total_amount: totalFuelAmount,
              total_consumed: totalFuelConsumed,
              driven_km: Number(fuelDrivenKm.toFixed(1)),
              total_cost: totalFuelCost,
              overall_avg: overallFuelAvg,
              best_consumption: validFuel.length ? Math.min(...validFuel) : 0,
              worst_consumption: validFuel.length ? Math.max(...validFuel) : 0,
            },
            electricity: {
              count: elecRows.length,
              total_amount: totalElecAmount,
              total_consumed: totalElecConsumed,
              driven_km: Number(elecDrivenKm.toFixed(1)),
              total_cost: totalElecCost,
              overall_avg: overallElecAvg,
              nominal_battery_capacity: batteryCap,
              current_battery_capacity: activeBatCap,
              equivalent_total_consumed: totalEquivElecConsumed,
              equivalent_overall_avg: overallEquivElecAvg,
              best_consumption: validElec.length ? Math.min(...validElec) : 0,
              worst_consumption: validElec.length ? Math.max(...validElec) : 0,
            },
            equivalent_electricity: {
              total_consumed: totalEquivElecConsumed,
              overall_avg: overallEquivElecAvg,
              driven_km: totalDriven,
              best_consumption: bestEquivCons,
              worst_consumption: worstEquivCons,
              formula: "1L/100km = 3.3 kWh/100km"
            }
          },
          fuel_intervals: fuelIntervals,
          elec_intervals: elecIntervals,
          recent_intervals: allCombined,
          ongoing_interval: ongoingItem
        };
      };

      // ---------------- 独立能耗分析接口 (Energy Interval Analysis) ----------------
      if (path === '/api/energy-analysis' && method === 'GET') {
        const vid = parseInt(url.searchParams.get('vehicle_id'));
        if (!vid) return err('缺少 vehicle_id 参数');

        const analysis = await getEnergyAnalysisData(vid);
        if (!analysis) return err('车辆不存在', 404);
        return json(analysis);
      }

      // ---------------- 汇总统计接口 (Statistics) ----------------
      if (path === '/api/statistics' && method === 'GET') {
        const vid = parseInt(url.searchParams.get('vehicle_id'));
        if (!vid) return err('缺少 vehicle_id 参数');

        const vehicle = await env.DB.prepare('SELECT * FROM vehicles WHERE id = ?').bind(vid).first();
        if (!vehicle) return err('车辆不存在', 404);

        const fuel = await env.DB.prepare('SELECT COUNT(*) as count, COALESCE(SUM(fuel_amount), 0) as total_fuel, COALESCE(SUM(total_price), 0) as total_fuel_cost FROM fuel_records WHERE vehicle_id = ?').bind(vid).first();
        const elec = await env.DB.prepare('SELECT COUNT(*) as count, COALESCE(SUM(electricity_amount), 0) as total_elec, COALESCE(SUM(total_price), 0) as total_elec_cost FROM electricity_records WHERE vehicle_id = ?').bind(vid).first();
        const maint = await env.DB.prepare('SELECT COUNT(*) as count, COALESCE(SUM(cost), 0) as total_maint_cost FROM maintenance_records WHERE vehicle_id = ?').bind(vid).first();

        const ea = await getEnergyAnalysisData(vid, vehicle);
        const driven = ea ? ea.summary.total_driven_km : Math.max(0, (vehicle.current_mileage || 0) - (vehicle.initial_mileage || 0));
        const totalCost = (fuel.total_fuel_cost || 0) + (elec.total_elec_cost || 0) + (maint.total_maint_cost || 0);

        let daysOwned = 1;
        const purchaseDateStr = vehicle.purchase_date || vehicle.created_at;
        if (purchaseDateStr) {
          const pd = new Date(purchaseDateStr.replace(/-/g, '/'));
          if (!isNaN(pd.getTime())) {
            const now = new Date();
            const today = new Date(now.getFullYear(), now.getMonth(), now.getDate());
            const startDay = new Date(pd.getFullYear(), pd.getMonth(), pd.getDate());
            daysOwned = Math.max(1, Math.floor((today - startDay) / (1000 * 3600 * 24)));
          }
        }
        const costPerDay = Number((totalCost / daysOwned).toFixed(2));
        const kmPerDay = driven > 0 && daysOwned > 0 ? Number((driven / daysOwned).toFixed(2)) : 0.0;

        const avgFuelPerHundred = ea ? ea.summary.fuel.overall_avg : 0;
        const avgElecPerHundred = ea ? ea.summary.electricity.overall_avg : 0;
        const avgEquivElecPerHundred = ea ? ea.summary.electricity.equivalent_overall_avg : 0;
        const totalEquivElecConsumed = ea ? ea.summary.electricity.equivalent_total_consumed : 0;

        // 按月费用统计聚合 (横轴为月份，统计燃油、电力、其他支出及总费用)
        const monthlyRows = await env.DB.prepare(`
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
        `).bind(vid, vid, vid).all();
        const rawMonthly = monthlyRows && monthlyRows.results ? monthlyRows.results : [];

        // 查询按月、按类别的维保支出明细
        const maintBreakdownRows = await env.DB.prepare(`
          SELECT
            SUBSTR(record_date, 1, 7) as month,
            COALESCE(NULLIF(TRIM(item_type), ''), '其他') as item_type,
            ROUND(SUM(cost), 2) as cat_cost
          FROM maintenance_records
          WHERE vehicle_id = ? AND record_date IS NOT NULL AND record_date != ''
          GROUP BY month, item_type
        `).bind(vid).all();
        const breakdownMap = {};
        if (maintBreakdownRows && maintBreakdownRows.results) {
          maintBreakdownRows.results.forEach(r => {
            if (!breakdownMap[r.month]) breakdownMap[r.month] = {};
            breakdownMap[r.month][r.item_type] = Number(r.cat_cost) || 0;
          });
        }

        const monthlyExpenses = rawMonthly.map(m => ({
          ...m,
          other_breakdown: breakdownMap[m.month] || {}
        }));

        const fifoEnergyCost = ea ? ea.summary.fifo_energy_cost : 0.0;
        const fifoCostPerKm = ea ? (ea.summary.fifo_cost_per_km !== undefined ? ea.summary.fifo_cost_per_km : 0) : 0.0;
        const fifoCostPerHundred = ea ? ea.summary.fifo_cost_per_hundred : 0.0;
        const fifoDrivenKm = ea ? ea.summary.fifo_driven_km : 0.0;
        const fifoFuelCost = ea ? ea.summary.fifo_fuel_cost : 0.0;
        const fifoElecCost = ea ? ea.summary.fifo_elec_cost : 0.0;
        const latestReplenishMileage = ea ? ea.summary.latest_replenish_mileage : (vehicle.current_mileage || 0);

        const totalEnergyCost = Number(((fuel.total_fuel_cost || 0) + (elec.total_elec_cost || 0)).toFixed(2));
        const energyCostPerDay = daysOwned > 0 ? Number((totalEnergyCost / daysOwned).toFixed(2)) : 0.0;
        const fifoEnergyCostPerDay = daysOwned > 0 ? Number((fifoEnergyCost / daysOwned).toFixed(2)) : 0.0;

        return json({
          vehicle,
          total_driven_km: driven,
          total_cost: Number(totalCost.toFixed(2)),
          total_energy_cost: totalEnergyCost,
          cost_per_km: driven > 0 ? Number((totalCost / driven).toFixed(2)) : 0,
          energy_cost_per_km: driven > 0 ? Number((totalEnergyCost / driven).toFixed(2)) : 0,
          energy_cost_per_km_fifo: fifoCostPerKm,
          fifo_cost_per_km: fifoCostPerKm,
          energy_cost_per_hundred_fifo: fifoCostPerHundred,
          fifo_energy_cost: fifoEnergyCost,
          fifo_driven_km: fifoDrivenKm,
          fifo_fuel_cost: fifoFuelCost,
          fifo_elec_cost: fifoElecCost,
          latest_replenish_mileage: latestReplenishMileage,
          cost_per_day: costPerDay,
          km_per_day: kmPerDay,
          energy_cost_per_day: energyCostPerDay,
          fifo_energy_cost_per_day: fifoEnergyCostPerDay,
          days_owned: daysOwned,
          monthly_expenses: monthlyExpenses,
          fuel: {
            record_count: fuel.count,
            total_amount: Number((fuel.total_fuel || 0).toFixed(2)),
            total_consumed: ea ? ea.summary.fuel.total_consumed : 0,
            driven_km: driven,
            total_cost: Number((fuel.total_fuel_cost || 0).toFixed(2)),
            avg_per_hundred: avgFuelPerHundred,
            avg_unit_price: fuel.total_fuel > 0 ? Number(((fuel.total_fuel_cost || 0) / fuel.total_fuel).toFixed(2)) : 0.0,
            avg_cost_per_time: fuel.count > 0 ? Number(((fuel.total_fuel_cost || 0) / fuel.count).toFixed(2)) : 0.0
          },
          electricity: {
            record_count: elec.count,
            total_amount: Number((elec.total_elec || 0).toFixed(2)),
            total_consumed: ea ? ea.summary.electricity.total_consumed : 0,
            equivalent_total_consumed: totalEquivElecConsumed,
            driven_km: driven,
            total_cost: Number((elec.total_elec_cost || 0).toFixed(2)),
            avg_per_hundred: avgElecPerHundred,
            equivalent_avg_per_hundred: avgEquivElecPerHundred,
            avg_unit_price: elec.total_elec > 0 ? Number(((elec.total_elec_cost || 0) / elec.total_elec).toFixed(2)) : 0.0,
            avg_cost_per_time: elec.count > 0 ? Number(((elec.total_elec_cost || 0) / elec.count).toFixed(2)) : 0.0
          },
          maintenance: {
            record_count: maint.count,
            total_cost: Number((maint.total_maint_cost || 0).toFixed(2))
          }
        });
      }

      return err('接口路由不存在', 404);
    } catch (e) {
      return err(`服务器内部异常: ${e.message}`, 500);
    }
  }
};
