/**
 * 我的车管家 本地 IndexedDB 离线存储与云端双向同步引擎 (db.js)
 */

const CarDB = {
  dbName: 'CarTrackerLocalDB',
  version: 4,
  db: null,

  entityStoreMap: {
    vehicles: 'vehicles',
    fuel: 'fuel_records',
    electricity: 'electricity_records',
    maintenance: 'maintenance_records',
    status: 'status_records'
  },

  async init() {
    return new Promise((resolve, reject) => {
      const request = indexedDB.open(this.dbName, this.version);

      request.onupgradeneeded = (e) => {
        const db = e.target.result;
        console.log('[IndexedDB] 升级/初始化本地数据库存储 (v4)');

        if (!db.objectStoreNames.contains('vehicles')) {
          db.createObjectStore('vehicles', { keyPath: 'id' });
        }
        if (!db.objectStoreNames.contains('fuel_records')) {
          const s = db.createObjectStore('fuel_records', { keyPath: 'id' });
          s.createIndex('vehicle_id', 'vehicle_id', { unique: false });
        }
        if (!db.objectStoreNames.contains('electricity_records')) {
          const s = db.createObjectStore('electricity_records', { keyPath: 'id' });
          s.createIndex('vehicle_id', 'vehicle_id', { unique: false });
        }
        if (!db.objectStoreNames.contains('maintenance_records')) {
          const s = db.createObjectStore('maintenance_records', { keyPath: 'id' });
          s.createIndex('vehicle_id', 'vehicle_id', { unique: false });
        }
        if (!db.objectStoreNames.contains('status_records')) {
          const s = db.createObjectStore('status_records', { keyPath: 'id' });
          s.createIndex('vehicle_id', 'vehicle_id', { unique: false });
        }
        if (!db.objectStoreNames.contains('sync_queue')) {
          db.createObjectStore('sync_queue', { keyPath: 'qid', autoIncrement: true });
        }
        if (!db.objectStoreNames.contains('sync_conflicts')) {
          const cf = db.createObjectStore('sync_conflicts', { keyPath: 'cid', autoIncrement: true });
          cf.createIndex('occurred_at', 'occurred_at', { unique: false });
        }
        if (!db.objectStoreNames.contains('settings')) {
          db.createObjectStore('settings', { keyPath: 'key' });
        }
        if (!db.objectStoreNames.contains('station_cache')) {
          const sc = db.createObjectStore('station_cache', { keyPath: 'id' });
          sc.createIndex('type', 'type', { unique: false });
          sc.createIndex('updated_at', 'updated_at', { unique: false });
        }
      };

      request.onsuccess = (e) => {
        this.db = e.target.result;
        resolve(this.db);
      };

      request.onerror = (e) => {
        console.error('[IndexedDB] 初始化失败:', e);
        reject(e);
      };
    });
  },

  // 通用事务操作封装
  async getAll(storeName) {
    return new Promise((resolve, reject) => {
      const tx = this.db.transaction(storeName, 'readonly');
      const store = tx.objectStore(storeName);
      const req = store.getAll();
      req.onsuccess = () => resolve(req.result || []);
      req.onerror = () => reject(req.error);
    });
  },

  async get(storeName, id) {
    return new Promise((resolve, reject) => {
      const tx = this.db.transaction(storeName, 'readonly');
      const store = tx.objectStore(storeName);
      const req = store.get(id);
      req.onsuccess = () => {
        if (req.result !== undefined) {
          resolve(req.result);
        } else if (typeof id === 'string' && !isNaN(Number(id)) && id.trim() !== '') {
          const numReq = store.get(Number(id));
          numReq.onsuccess = () => resolve(numReq.result !== undefined ? numReq.result : null);
          numReq.onerror = () => reject(numReq.error);
        } else if (typeof id === 'number') {
          const strReq = store.get(String(id));
          strReq.onsuccess = () => resolve(strReq.result !== undefined ? strReq.result : null);
          strReq.onerror = () => reject(strReq.error);
        } else {
          resolve(null);
        }
      };
      req.onerror = () => reject(req.error);
    });
  },

  async put(storeName, item) {
    return new Promise((resolve, reject) => {
      const tx = this.db.transaction(storeName, 'readwrite');
      const store = tx.objectStore(storeName);
      const req = store.put(item);
      req.onsuccess = () => resolve(req.result);
      req.onerror = () => reject(req.error);
    });
  },

  async putMany(storeName, items) {
    return new Promise((resolve, reject) => {
      const tx = this.db.transaction(storeName, 'readwrite');
      const store = tx.objectStore(storeName);
      items.forEach(item => store.put(item));
      tx.oncomplete = () => resolve();
      tx.onerror = () => reject(tx.error);
    });
  },

  async delete(storeName, id) {
    return new Promise((resolve, reject) => {
      const tx = this.db.transaction(storeName, 'readwrite');
      const store = tx.objectStore(storeName);
      const req = store.delete(id);
      req.onsuccess = () => resolve(true);
      req.onerror = () => reject(req.error);
    });
  },

  async clear(storeName) {
    return new Promise((resolve, reject) => {
      const tx = this.db.transaction(storeName, 'readwrite');
      const store = tx.objectStore(storeName);
      const req = store.clear();
      req.onsuccess = () => resolve(true);
      req.onerror = () => reject(req.error);
    });
  },

  // 计算球面直线距离 (Haversine 公式，单位：米)
  calcDistance(lat1, lon1, lat2, lon2) {
    if (lat1 == null || lon1 == null || lat2 == null || lon2 == null) return null;
    const p1Lat = parseFloat(lat1);
    const p1Lon = parseFloat(lon1);
    const p2Lat = parseFloat(lat2);
    const p2Lon = parseFloat(lon2);
    if (isNaN(p1Lat) || isNaN(p1Lon) || isNaN(p2Lat) || isNaN(p2Lon)) return null;

    const R = 6371000; // 地球平均半径(米)
    const rad = Math.PI / 180;
    const dLat = (p2Lat - p1Lat) * rad;
    const dLon = (p2Lon - p1Lon) * rad;
    const a = Math.sin(dLat / 2) * Math.sin(dLat / 2) +
              Math.cos(p1Lat * rad) * Math.cos(p2Lat * rad) *
              Math.sin(dLon / 2) * Math.sin(dLon / 2);
    const c = 2 * Math.atan2(Math.sqrt(a), Math.sqrt(1 - a));
    return Math.round(R * c);
  },

  // 保存搜索或历史站点至本地 IndexedDB 离线缓存 (增量合并，轻量级存储)
  async saveStationCache(stations, type = 'fuel') {
    if (!this.db || !Array.isArray(stations) || stations.length === 0) return;
    try {
      const tx = this.db.transaction('station_cache', 'readwrite');
      const store = tx.objectStore('station_cache');
      const now = Date.now();
      for (const st of stations) {
        if (!st || !st.name) continue;
        const cleanName = String(st.name).trim();
        if (!cleanName) continue;

        let lng = null, lat = null;
        if (typeof st.location === 'string' && st.location.includes(',')) {
          const parts = st.location.split(',');
          lng = parseFloat(parts[0]);
          lat = parseFloat(parts[1]);
        } else if (st.lng != null && st.lat != null) {
          lng = parseFloat(st.lng);
          lat = parseFloat(st.lat);
        }

        const id = st.id ? String(st.id) : (`${type}_` + cleanName.replace(/[\s\(\)（）]/g, ''));
        const record = {
          id: id,
          name: cleanName,
          address: typeof st.address === 'string' ? st.address : '',
          district: typeof st.district === 'string' ? st.district : '',
          type: type,
          lng: !isNaN(lng) ? lng : null,
          lat: !isNaN(lat) ? lat : null,
          location: (!isNaN(lng) && !isNaN(lat)) ? `${lng},${lat}` : (typeof st.location === 'string' ? st.location : ''),
          typecode: String(st.typecode || st.type || ''),
          source: st.source || 'cache',
          updated_at: now
        };
        store.put(record);
      }
      return new Promise((resolve) => {
        tx.oncomplete = () => resolve(true);
        tx.onerror = () => resolve(false);
      });
    } catch (e) {
      console.warn('保存离线站点缓存异常:', e);
    }
  },

  // 离线检索站点：支持按关键字模糊搜索、按 GPS 距离排序与范围过滤
  async queryStationCache({ type = 'fuel', keyword = '', lng = null, lat = null, maxRadius = 15000, limit = 25 }) {
    if (!this.db) return [];
    try {
      const tx = this.db.transaction(['station_cache', 'fuel_records', 'electricity_records'], 'readonly');
      const cacheStore = tx.objectStore('station_cache');

      const cachedList = await new Promise((resolve) => {
        const req = cacheStore.getAll();
        req.onsuccess = () => resolve(req.result || []);
        req.onerror = () => resolve([]);
      });

      // 提取历史记录中的真实站点补充至离线候选
      const historyStations = [];
      const historySet = new Set();
      if (type === 'fuel') {
        const fuelStore = tx.objectStore('fuel_records');
        const fuelRecs = await new Promise((resolve) => {
          const req = fuelStore.getAll();
          req.onsuccess = () => resolve(req.result || []);
          req.onerror = () => resolve([]);
        });
        for (const r of fuelRecs) {
          const sName = (r.station_name || '').trim();
          if (sName && !historySet.has(sName)) {
            historySet.add(sName);
            historyStations.push({
              id: 'hist_' + sName,
              name: sName,
              address: '历史使用过的加油站',
              type: 'fuel',
              source: 'history'
            });
          }
        }
      } else {
        const elecStore = tx.objectStore('electricity_records');
        const elecRecs = await new Promise((resolve) => {
          const req = elecStore.getAll();
          req.onsuccess = () => resolve(req.result || []);
          req.onerror = () => resolve([]);
        });
        for (const r of elecRecs) {
          const sName = (r.charging_station || '').trim();
          if (sName && !historySet.has(sName)) {
            historySet.add(sName);
            historyStations.push({
              id: 'hist_' + sName,
              name: sName,
              address: '历史使用过的充电站',
              type: 'elec',
              source: 'history'
            });
          }
        }
      }

      // 合并本地已缓存站点与历史记录
      const allStations = [...cachedList.filter(s => s.type === type), ...historyStations];
      const seenNames = new Set();
      const uniqueStations = [];
      for (const st of allStations) {
        if (!seenNames.has(st.name)) {
          seenNames.add(st.name);
          uniqueStations.push(st);
        }
      }

      const kw = (keyword || '').trim().toLowerCase();
      const centerLng = lng != null ? parseFloat(lng) : null;
      const centerLat = lat != null ? parseFloat(lat) : null;

      let results = uniqueStations.map(st => {
        let dist = null;
        if (centerLat != null && centerLng != null && st.lat != null && st.lng != null) {
          dist = this.calcDistance(centerLat, centerLng, st.lat, st.lng);
        }
        return {
          ...st,
          distance: dist !== null ? dist : (st.distance != null ? st.distance : null),
          isOffline: true
        };
      });

      // 关键词过滤
      if (kw) {
        results = results.filter(st => {
          const name = (st.name || '').toLowerCase();
          const addr = (st.address || '').toLowerCase();
          const dist = (st.district || '').toLowerCase();
          return name.includes(kw) || addr.includes(kw) || dist.includes(kw);
        });
      }

      // 按距离排序与过滤
      if (centerLat != null && centerLng != null) {
        const withDist = results.filter(s => s.distance !== null && (!maxRadius || s.distance <= maxRadius));
        const withoutDist = results.filter(s => s.distance === null);
        withDist.sort((a, b) => a.distance - b.distance);
        results = [...withDist, ...withoutDist];
      }

      return results.slice(0, limit);
    } catch (e) {
      console.warn('检索离线站点异常:', e);
      return [];
    }
  },

  // 待同步队列 (Outbox) 操作 —— 支持离线操作智能合并 (Queue Coalescing) 与乐观锁基准版本捕获
  async enqueueSync(action, entity, data) {
    const nowIso = new Date().toISOString();
    const payload = data ? { ...data, _client_updated_at: nowIso } : {};
    const targetId = payload.id !== undefined ? payload.id : payload._tempId;

    // 若为 UPDATE 且未显式指定 _base_version，尝试从本地 Store 读取基准 sync_version
    if (action === 'UPDATE' && payload._base_version === undefined && targetId !== undefined) {
      const storeName = this.entityStoreMap[entity];
      if (storeName) {
        try {
          const existingLocal = await this.get(storeName, targetId);
          if (existingLocal && existingLocal.sync_version !== undefined) {
            payload._base_version = parseInt(existingLocal.sync_version) || 1;
          } else {
            payload._base_version = 1;
          }
        } catch (e) {
          payload._base_version = 1;
        }
      } else {
        payload._base_version = 1;
      }
      payload._check_conflict = true;
    }

    // 检查当前待同步队列，执行同实体同 ID 离线操作智能合并
    if (targetId !== undefined && targetId !== null) {
      const queue = await this.getSyncQueue();
      const matched = queue.find(q => q.entity === entity && q.data && (q.data.id == targetId || q.data._tempId == targetId));
      if (matched) {
        if (matched.action === 'CREATE' && action === 'UPDATE') {
          // 离线新建后又在离线状态下修改：直接合并进原 CREATE 项
          const mergedData = { ...matched.data, ...payload };
          delete mergedData._check_conflict;
          delete mergedData._base_version;
          matched.data = mergedData;
          matched.updated_at = nowIso;
          await this.put('sync_queue', matched);
          CarSync.notifyQueueChanged();
          return;
        }
        if (matched.action === 'CREATE' && action === 'DELETE') {
          // 离线新建后尚未同步即删除：直接从待同步队列抵消移除，节省网络开销
          await this.delete('sync_queue', matched.qid);
          CarSync.notifyQueueChanged();
          return;
        }
        if (matched.action === 'UPDATE' && action === 'UPDATE') {
          // 离线多次修改同一条记录：合并最新字段，但保留最初捕获的 _base_version 基准版本号
          const origBaseVer = matched.data._base_version || payload._base_version || 1;
          matched.data = {
            ...matched.data,
            ...payload,
            _base_version: origBaseVer,
            _check_conflict: true
          };
          matched.updated_at = nowIso;
          await this.put('sync_queue', matched);
          CarSync.notifyQueueChanged();
          return;
        }
        if (matched.action === 'UPDATE' && action === 'DELETE') {
          // 离线修改后又决定删除：将原 UPDATE 升级为 DELETE
          matched.action = 'DELETE';
          matched.data = { id: targetId };
          matched.updated_at = nowIso;
          await this.put('sync_queue', matched);
          CarSync.notifyQueueChanged();
          return;
        }
      }
    }

    const queueItem = {
      action, // 'CREATE', 'UPDATE', 'DELETE'
      entity, // 'vehicles', 'fuel', 'electricity', 'maintenance', 'status'
      data: payload,
      created_at: nowIso,
      updated_at: nowIso
    };
    await this.put('sync_queue', queueItem);
    CarSync.notifyQueueChanged();
  },

  async getSyncQueue() {
    return await this.getAll('sync_queue');
  },

  async removeSyncQueueItem(qid) {
    await this.delete('sync_queue', qid);
    CarSync.notifyQueueChanged();
  },

  async clearSyncQueue() {
    await this.clear('sync_queue');
    CarSync.notifyQueueChanged();
  },

  // 同步冲突日志仓库 (sync_conflicts) 操作
  async recordConflict(conflictItem) {
    if (!this.db || !this.db.objectStoreNames.contains('sync_conflicts')) return null;
    const entry = {
      ...conflictItem,
      occurred_at: conflictItem.occurred_at || new Date().toISOString()
    };
    return await this.put('sync_conflicts', entry);
  },

  async getConflicts() {
    if (!this.db || !this.db.objectStoreNames.contains('sync_conflicts')) return [];
    const list = await this.getAll('sync_conflicts');
    return list.sort((a, b) => (b.occurred_at || '').localeCompare(a.occurred_at || ''));
  },

  async removeConflict(cid) {
    if (!this.db || !this.db.objectStoreNames.contains('sync_conflicts')) return;
    await this.delete('sync_conflicts', cid);
  },

  async clearConflicts() {
    if (!this.db || !this.db.objectStoreNames.contains('sync_conflicts')) return;
    await this.clear('sync_conflicts');
  },

  // 设置存储 (如 Cloudflare Worker 基础 URL)
  async getSetting(key, defaultVal = '') {
    return new Promise((resolve) => {
      const tx = this.db.transaction('settings', 'readonly');
      const store = tx.objectStore('settings');
      const req = store.get(key);
      req.onsuccess = () => resolve(req.result ? req.result.value : defaultVal);
      req.onerror = () => resolve(defaultVal);
    });
  },

  async setSetting(key, value) {
    return await this.put('settings', { key, value });
  }
};

/**
 * 云端双向同步引擎 (支持 Cloudflare D1 / Worker 及本地后端)
 */
const CarSync = {
  status: 'synced', // 'synced' (实时同步), 'offline' (离线模式), 'syncing' (同步中), 'error' (同步异常)
  pendingCount: 0,
  lastError: null,
  syncListeners: [],
  syncTimer: null,

  async init() {
    await CarDB.init();
    await this.updatePendingCount();

    // 监听网络状态
    window.addEventListener('online', () => {
      console.log('[CarSync] 恢复网络连接，触发自动后台静默同步');
      this.sync();
    });

    window.addEventListener('offline', () => {
      console.log('[CarSync] 网络断开，转入离线优先模式');
      this.setStatus('offline');
    });

    // 初始状态判断
    if (!navigator.onLine) {
      this.setStatus('offline');
    } else {
      this.sync();
    }

    // 周期性同步 (每 3 分钟自动检查一次)
    setInterval(() => {
      if (navigator.onLine && this.status !== 'syncing') {
        this.sync(true); // silent
      }
    }, 180000);
  },

  onStatusChange(fn) {
    this.syncListeners.push(fn);
  },

  setStatus(newStatus) {
    this.status = newStatus;
    this.notifyListeners();
  },

  async updatePendingCount() {
    const queue = await CarDB.getSyncQueue();
    this.pendingCount = queue.length;
    if (!navigator.onLine) {
      this.status = 'offline';
    } else if (this.pendingCount > 0 && this.status !== 'syncing') {
      this.status = 'offline'; // 提示有待同步数据
    }
    this.notifyListeners();
  },

  notifyListeners() {
    this.syncListeners.forEach(fn => fn(this.status, this.pendingCount));
  },

  async notifyQueueChanged() {
    await this.updatePendingCount();
  },

  DEFAULT_WORKER_URL: 'https://car-tracker-api.aubreyhan.workers.dev',

  // 获取目标 API 根路径 (支持配置 Cloudflare Worker 域名)
  async getApiBaseUrl() {
    let customUrl = await CarDB.getSetting('cloudflare_worker_url', '');
    if (customUrl && customUrl.trim().length > 0) {
      customUrl = customUrl.trim();
      if (!customUrl.startsWith('http://') && !customUrl.startsWith('https://')) {
        customUrl = 'https://' + customUrl;
      }
      if (customUrl.endsWith('.worker')) {
        customUrl = customUrl + 's.dev';
      } else if (customUrl.endsWith('.workers')) {
        customUrl = customUrl + '.dev';
      }
      return customUrl.replace(/\/+$/, '');
    }
    // 若当前环境已在 workers.dev 域名运行，使用当前 origin
    if (typeof window !== 'undefined' && window.location && window.location.origin) {
      if (window.location.hostname.includes('workers.dev')) {
        return window.location.origin;
      }
      // 本地单机开发环境且未手动指定时，走本地相对路由
      if (window.location.hostname === 'localhost' || window.location.hostname === '127.0.0.1') {
        return '';
      }
    }
    // 手机端 / 离线 PWA / 独立 App 模式下，默认直连已部署的 Worker 云端服务
    return this.DEFAULT_WORKER_URL;
  },

  // 执行核心双向同步
  async sync(silent = false, isManual = false) {
    if (!isManual && typeof navigator !== 'undefined' && navigator.onLine === false) {
      this.setStatus('offline');
      return;
    }

    this.setStatus('syncing');
    try {
      const baseUrl = await this.getApiBaseUrl();

      // 第一步：上报待同步队列 (Push pending changes)
      const queue = await CarDB.getSyncQueue();
      let pushFailed = false;
      if (queue.length > 0) {
        console.log(`[CarSync] 正在将 ${queue.length} 笔离线数据推送到云端...`);
        for (const item of queue) {
          try {
            await this.pushItem(baseUrl, item);
            await CarDB.removeSyncQueueItem(item.qid);
          } catch (pushErr) {
            console.warn('[CarSync] 单笔数据上传异常，保留在待同步队列:', pushErr);
            this.lastError = pushErr.message || '数据上传异常';
            pushFailed = true;
            break;
          }
        }
      }

      if (pushFailed) {
        await this.updatePendingCount();
        this.setStatus('error');
        return;
      }

      // 第二步：从云端拉取全量最新数据 (Pull latest data)
      console.log('[CarSync] 正在从云端拉取最新记录...');
      const [vRes, fRes, eRes, mRes, sRes] = await Promise.all([
        fetch(`${baseUrl}/api/vehicles`),
        fetch(`${baseUrl}/api/records/fuel`),
        fetch(`${baseUrl}/api/records/electricity`),
        fetch(`${baseUrl}/api/records/maintenance`),
        fetch(`${baseUrl}/api/records/status`).catch(() => ({ ok: false }))
      ]);

      if (vRes.ok && fRes.ok && eRes.ok && mRes.ok) {
        const vehicles = await vRes.json();
        const fuels = await fRes.json();
        const elecs = await eRes.json();
        const maints = await mRes.json();
        const statuses = sRes.ok ? await sRes.json() : [];

        // 写入本地 IndexedDB
        await CarDB.clear('vehicles');
        await CarDB.putMany('vehicles', vehicles);

        await CarDB.clear('fuel_records');
        await CarDB.putMany('fuel_records', fuels);

        await CarDB.clear('electricity_records');
        await CarDB.putMany('electricity_records', elecs);

        await CarDB.clear('maintenance_records');
        await CarDB.putMany('maintenance_records', maints);

        await CarDB.clear('status_records');
        if (statuses && statuses.length > 0) {
          await CarDB.putMany('status_records', statuses);
        }

        // 若队列中仍有尚未推送成功的本地变更，重放保护到本地 IndexedDB，防止断网半途失败导致本地修改丢失
        await this.applyPendingQueueToLocalStores();

        console.log('[CarSync] 云端数据双向同步完成！');
        this.lastError = null;
      }

      await this.updatePendingCount();
      this.setStatus(this.pendingCount > 0 ? 'offline' : 'synced');

      if (window.App) {
        if (!silent) {
          await window.App.loadVehicles();
        }
        if (window.App.extractStationsFromHistory) {
          await window.App.extractStationsFromHistory();
        } else {
          if (window.App.extractFuelStationsHistory) await window.App.extractFuelStationsHistory();
          if (window.App.extractElecStationsHistory) await window.App.extractElecStationsHistory();
        }
        if (window.App.extractExpenseItemsFromHistory) {
          await window.App.extractExpenseItemsFromHistory();
          window.App.renderExpenseItemsPills();
        }
        if (window.App.currentTab === 'overview') window.App.loadOverview();
        else if (window.App.currentTab === 'energy') window.App.loadEnergyAnalysis();
        else if (window.App.currentTab === 'history') window.App.loadHistory();
        else if (['fuel', 'electricity', 'maintenance'].includes(window.App.currentTab)) {
          window.App.syncActiveVehicleToForms();
        }
      }
    } catch (err) {
      console.error('[CarSync] 同步发生网络错误:', err);
      this.lastError = err.message || '网络连接超时或无法连接云端';
      this.setStatus('error');
    }
  },

  async clearQueue() {
    await CarDB.clearSyncQueue();
    this.lastError = null;
    await this.updatePendingCount();
    this.setStatus('synced');
  },

  // 将尚未同步成功的本地挂起队列重放保护至本地 IndexedDB
  async applyPendingQueueToLocalStores() {
    const queue = await CarDB.getSyncQueue();
    if (!queue || queue.length === 0) return;
    for (const item of queue) {
      const storeName = CarDB.entityStoreMap[item.entity];
      if (!storeName || !item.data) continue;
      try {
        if (item.action === 'DELETE' && item.data.id !== undefined) {
          await CarDB.delete(storeName, item.data.id);
        } else if (item.action === 'CREATE' || item.action === 'UPDATE') {
          const existing = item.data.id !== undefined ? await CarDB.get(storeName, item.data.id) : null;
          await CarDB.put(storeName, { ...(existing || {}), ...item.data });
        }
      } catch (e) {
        console.warn('[CarSync] 本地挂起项重放保护忽略:', e);
      }
    }
  },

  // 手动或自动裁决多端并发冲突：strategy = 'use_local' (强制采用本地版本) | 'use_server' (恢复为云端版本)
  async resolveConflict(cid, strategy = 'use_local') {
    const conflicts = await CarDB.getConflicts();
    const conflict = conflicts.find(c => c.cid == cid);
    if (!conflict) return false;

    const baseUrl = await this.getApiBaseUrl();
    let endpoint = '';
    if (conflict.entity === 'vehicles') endpoint = '/api/vehicles';
    else if (conflict.entity === 'fuel') endpoint = '/api/records/fuel';
    else if (conflict.entity === 'electricity') endpoint = '/api/records/electricity';
    else if (conflict.entity === 'maintenance') endpoint = '/api/records/maintenance';
    else if (conflict.entity === 'status') endpoint = '/api/records/status';

    const targetPayload = strategy === 'use_server'
      ? { ...conflict.server_data, _force_overwrite: true }
      : { ...conflict.local_data, _force_overwrite: true };

    const res = await fetch(`${baseUrl}${endpoint}/${conflict.record_id}`, {
      method: 'PUT',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(targetPayload)
    });

    if (res.ok) {
      await CarDB.removeConflict(cid);
      await this.sync(false, true);
      return true;
    }
    return false;
  },

  async pushItem(baseUrl, item) {
    const { action, entity, data } = item;
    let endpoint = '';
    if (entity === 'vehicles') endpoint = '/api/vehicles';
    else if (entity === 'fuel') endpoint = '/api/records/fuel';
    else if (entity === 'electricity') endpoint = '/api/records/electricity';
    else if (entity === 'maintenance') endpoint = '/api/records/maintenance';
    else if (entity === 'status') endpoint = '/api/records/status';

    let method = 'POST';
    let url = `${baseUrl}${endpoint}`;

    if (action === 'DELETE') {
      method = 'DELETE';
      url = `${baseUrl}${endpoint}/${data.id}`;
      const res = await fetch(url, { method });
      if (!res.ok && res.status !== 404) throw new Error(`删除失败: ${res.status}`);
      return;
    }

    if (action === 'UPDATE') {
      method = 'PUT';
      url = `${baseUrl}${endpoint}/${data.id}`;
    }

    const res = await fetch(url, {
      method,
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(data)
    });

    // 捕获 HTTP 409 乐观锁并发冲突：自动归档双端完整快照并按最新变更闭环完成同步
    if (res.status === 409) {
      let conflictInfo = {};
      try {
        conflictInfo = await res.json();
      } catch (e) {}
      console.warn('[CarSync] 检测到多端并发修改冲突 (HTTP 409)，正在归档冲突快照并执行智能合并:', conflictInfo);
      await CarDB.recordConflict({
        entity,
        record_id: data.id,
        local_data: data,
        server_data: conflictInfo.server_record || {},
        client_base_version: conflictInfo.client_base_version || data._base_version || 1,
        server_version: conflictInfo.server_version || 2,
        resolution: 'auto_lww_with_backup'
      });

      // 以强制合并方式提交最新改动，同时冲突快照已安全保存在 sync_conflicts 供用户随时一键回退至云端版本
      const forceRes = await fetch(url, {
        method: 'PUT',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ ...data, _force_overwrite: true })
      });
      if (!forceRes.ok && forceRes.status !== 404) {
        throw new Error(`冲突合并提交失败: ${forceRes.status}`);
      }
      return;
    }

    if (!res.ok) {
      if (res.status === 404 || res.status === 400) {
        console.warn(`[CarSync] 远端数据不存在或参数错误 (HTTP ${res.status})，跳过该项以避免阻塞待传队列:`, item);
        return;
      }
      throw new Error(`上报异常: ${res.status}`);
    }

    if (action === 'CREATE' && entity === 'vehicles') {
      try {
        const newV = await res.json();
        if (newV && newV.id && data._tempId) {
          const remainingQueue = await CarDB.getSyncQueue();
          for (const q of remainingQueue) {
            if (q.data && q.data.vehicle_id === data._tempId) {
              q.data.vehicle_id = newV.id;
              await CarDB.put('sync_queue', q);
            }
          }
          const savedVid = localStorage.getItem('car_tracker_active_vid');
          if (savedVid == data._tempId) {
            localStorage.setItem('car_tracker_active_vid', newV.id);
          }
        }
      } catch (parseErr) {
        // 静默忽略响应解析异常
      }
    }
  }
};

// 挂载至全局 window 对象供各页面及 App 模块调用
if (typeof window !== 'undefined') {
  window.CarDB = CarDB;
  window.CarSync = CarSync;
}
