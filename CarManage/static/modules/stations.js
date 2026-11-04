/**
 * 我的车管家 · 前端模块化子系统 (stations.js)
 * 职责：高德+百度双地图并发检索、30米空间去重、品牌保护与站点缓存模块
 */
(function (global) {
  global.AppModules = global.AppModules || {};
  global.AppModules.Stations = {
  // 初始化常用加油站与充电站列表
  async initStations() {
    try {
      const storedFuel = localStorage.getItem('car_fuel_stations');
      this.fuelStations = storedFuel ? JSON.parse(storedFuel) : [];
    } catch (e) {
      this.fuelStations = [];
    }

    try {
      const storedElec = localStorage.getItem('car_charging_stations');
      this.elecStations = storedElec ? JSON.parse(storedElec) : [];
    } catch (e) {
      this.elecStations = [];
    }

    // 扫描本地已缓存历史记录，自动发现已有油站/电站并按真实频次排序
    await this.extractStationsFromHistory();
  },

  // 统一提取油站与充电站历史频次
  async extractStationsFromHistory() {
    await this.extractFuelStationsHistory();
    await this.extractElecStationsHistory();
    this.renderFuelStationPills();
    this.renderElecStationPills();
  },

  // 方式 1: 扫描历史记录，统计每个油站真实使用频次（使用次数越多越靠前，次数相同优先最近使用时间）
  async extractFuelStationsHistory() {
    try {
      const countMap = {};
      const lastUsedMap = {};

      const parseTime = (dateStr) => {
        if (!dateStr) return 0;
        const s = String(dateStr).trim();
        let t = new Date(s.replace(/-/g, '/')).getTime();
        if (isNaN(t) || t === 0) {
          t = new Date(s.replace(' ', 'T')).getTime();
        }
        if (isNaN(t) || t === 0) {
          t = new Date(s).getTime();
        }
        return isNaN(t) ? 0 : t;
      };

      if (window.CarDB && CarDB.db) {
        const fuels = await CarDB.getAll('fuel_records');
        (fuels || []).forEach(r => {
          const s = (r.station || '').trim();
          if (s) {
            countMap[s] = (countMap[s] || 0) + 1;
            const time = parseTime(r.record_date || r.created_at);
            if (!lastUsedMap[s] || time > lastUsedMap[s]) {
              lastUsedMap[s] = time;
            }
          }
        });
      }

      // 自动移出使用次数为 0 的油站，只保留真实使用次数 > 0 的油站
      const stationList = Object.keys(countMap)
        .filter(name => (countMap[name] || 0) > 0)
        .map(name => ({
          name,
          count: countMap[name],
          lastUsed: lastUsedMap[name] || 0
        }));

      stationList.sort((a, b) => {
        if (b.count !== a.count) {
          return b.count - a.count; // 次数越多越靠前
        }
        return b.lastUsed - a.lastUsed; // 次数相同优先最近使用时间
      });

      this.fuelStationsWithCount = stationList;
      this.fuelStations = stationList.map(item => item.name);
      try {
        localStorage.setItem('car_fuel_stations', JSON.stringify(this.fuelStations));
      } catch (e) {}

      this.renderFuelStationPills();
    } catch (err) {
      console.warn('提取历史油站频次失败:', err);
    }
  },

  // 方式 1: 扫描历史记录，统计每个充电站真实使用频次（使用次数越多越靠前，自动移出使用次数为0的充电站）
  async extractElecStationsHistory() {
    try {
      const countMap = {};
      const lastUsedMap = {};

      const parseTime = (dateStr) => {
        if (!dateStr) return 0;
        const s = String(dateStr).trim();
        let t = new Date(s.replace(/-/g, '/')).getTime();
        if (isNaN(t) || t === 0) {
          t = new Date(s.replace(' ', 'T')).getTime();
        }
        if (isNaN(t) || t === 0) {
          t = new Date(s).getTime();
        }
        return isNaN(t) ? 0 : t;
      };

      if (window.CarDB && CarDB.db) {
        const elecs = await CarDB.getAll('electricity_records');
        (elecs || []).forEach(r => {
          const s = (r.station || '').trim();
          if (s) {
            countMap[s] = (countMap[s] || 0) + 1;
            const time = parseTime(r.record_date || r.created_at);
            if (!lastUsedMap[s] || time > lastUsedMap[s]) {
              lastUsedMap[s] = time;
            }
          }
        });
      }

      // 自动移出使用次数为 0 的充电站，只保留真实使用次数 > 0 的充电站
      const stationList = Object.keys(countMap)
        .filter(name => (countMap[name] || 0) > 0)
        .map(name => ({
          name,
          count: countMap[name],
          lastUsed: lastUsedMap[name] || 0
        }));

      stationList.sort((a, b) => {
        if (b.count !== a.count) {
          return b.count - a.count; // 次数越多越靠前
        }
        return b.lastUsed - a.lastUsed; // 次数相同优先最近使用时间
      });

      this.elecStationsWithCount = stationList;
      this.elecStations = stationList.map(item => item.name);
      try {
        localStorage.setItem('car_charging_stations', JSON.stringify(this.elecStations));
      } catch (e) {}

      this.renderElecStationPills();
    } catch (err) {
      console.warn('提取历史充电站频次失败:', err);
    }
  },

  // 动态渲染常用加油站胶囊列表与 datalist (方式 1: 历史加油站，使用次数越多越靠前，只保留前5个)
  renderFuelStationPills() {
    const container = document.getElementById('fuel-station-pills');
    const box = document.getElementById('fuel-history-stations-box');
    const badge = document.getElementById('fuel-history-count-badge');
    const datalist = document.getElementById('fuel-station-datalist');
    const editDatalist = document.getElementById('edit-fuel-station-datalist');

    if (datalist) {
      datalist.innerHTML = (this.fuelStations || []).map(s => `<option value="${s.replace(/"/g, '&quot;')}"></option>`).join('');
    }
    if (editDatalist) {
      editDatalist.innerHTML = datalist ? datalist.innerHTML : '';
    }
    this.renderEditFuelStationPills();

    if (!container || !box) return;

    const list = this.fuelStationsWithCount && this.fuelStationsWithCount.length > 0
      ? this.fuelStationsWithCount
      : [];

    const displayList = list.filter(item => (item.count || 0) > 0).slice(0, 5);

    if (displayList.length === 0) {
      container.innerHTML = '';
      box.classList.add('hidden');
      return;
    }

    box.classList.remove('hidden');
    if (badge) badge.textContent = `常用前 ${displayList.length} 个`;

    const currentVal = document.getElementById('fuel-station')?.value?.trim() || '';

    container.innerHTML = displayList.map(item => {
      const station = item.name;
      const count = item.count;
      const isActive = station === currentVal;
      const safeStation = station.replace(/"/g, '&quot;').replace(/'/g, "\\'");
      return `
        <span class="inline-flex items-center gap-1.5 px-2.5 py-1 rounded-full text-xs font-medium border transition ${
          isActive 
            ? 'bg-amber-100 text-amber-900 border-amber-300 shadow-sm font-semibold ring-1 ring-amber-400' 
            : 'bg-white text-slate-700 hover:bg-slate-100 border-slate-200'
        }">
          <button type="button" onclick="App.setFuelStation('${safeStation}')" class="focus:outline-none flex items-center gap-1">
            <span class="text-amber-600">⛽</span><span>${station}</span>
            <span class="text-[10px] ${isActive ? 'text-amber-800 font-bold' : 'text-slate-400'}">(${count}次)</span>
          </button>
          <button type="button" onclick="App.removeFuelStation('${safeStation}', event)" class="text-slate-400 hover:text-rose-600 rounded-full w-3.5 h-3.5 flex items-center justify-center text-[13px] leading-none transition" title="从列表移除">
            ×
          </button>
        </span>
      `;
    }).join('');
  },

  // 渲染历史编辑弹窗中的常用加油站胶囊列表 (只保留前5个)
  renderEditFuelStationPills() {
    const container = document.getElementById('edit-fuel-station-pills');
    const box = document.getElementById('edit-fuel-history-stations-box');
    const badge = document.getElementById('edit-fuel-history-count-badge');
    if (!container || !box) return;

    const list = this.fuelStationsWithCount && this.fuelStationsWithCount.length > 0
      ? this.fuelStationsWithCount
      : [];

    const displayList = list.filter(item => (item.count || 0) > 0).slice(0, 5);

    if (displayList.length === 0) {
      container.innerHTML = '';
      box.classList.add('hidden');
      return;
    }

    box.classList.remove('hidden');
    if (badge) badge.textContent = `前${displayList.length}个`;

    const currentVal = document.getElementById('edit-fuel-station')?.value?.trim() || '';

    container.innerHTML = displayList.map(item => {
      const station = item.name;
      const count = item.count;
      const isActive = station === currentVal;
      const safeStation = station.replace(/"/g, '&quot;').replace(/'/g, "\\'");
      return `
        <button type="button" onclick="App.setEditFuelStation('${safeStation}')" class="inline-flex items-center gap-1 px-2 py-0.5 rounded-full text-xs font-medium border transition ${
          isActive 
            ? 'bg-amber-100 text-amber-900 border-amber-300 font-semibold ring-1 ring-amber-400' 
            : 'bg-white text-slate-700 hover:bg-slate-100 border-slate-200'
        }">
          <span class="text-amber-600">⛽</span><span>${station}</span>
          <span class="text-[10px] ${isActive ? 'text-amber-800 font-bold' : 'text-slate-400'}">(${count})</span>
        </button>
      `;
    }).join('');
  },

  // 渲染历史编辑弹窗中的常用充电站胶囊列表 (只保留前5个)
  renderEditElecStationPills() {
    const container = document.getElementById('edit-elec-station-pills');
    const box = document.getElementById('edit-elec-history-stations-box');
    const badge = document.getElementById('edit-elec-history-count-badge');
    if (!container || !box) return;

    const list = this.elecStationsWithCount && this.elecStationsWithCount.length > 0
      ? this.elecStationsWithCount
      : [];

    const displayList = list.filter(item => (item.count || 0) > 0).slice(0, 5);

    if (displayList.length === 0) {
      container.innerHTML = '';
      box.classList.add('hidden');
      return;
    }

    box.classList.remove('hidden');
    if (badge) badge.textContent = `前${displayList.length}个`;

    const currentVal = document.getElementById('edit-elec-station')?.value?.trim() || '';

    container.innerHTML = displayList.map(item => {
      const station = item.name;
      const count = item.count;
      const isActive = station === currentVal;
      const safeStation = station.replace(/"/g, '&quot;').replace(/'/g, "\\'");
      return `
        <button type="button" onclick="App.setEditElecStation('${safeStation}')" class="inline-flex items-center gap-1 px-2 py-0.5 rounded-full text-xs font-medium border transition ${
          isActive 
            ? 'bg-emerald-100 text-emerald-900 border-emerald-300 font-semibold ring-1 ring-emerald-400' 
            : 'bg-white text-slate-700 hover:bg-slate-100 border-slate-200'
        }">
          <span class="text-emerald-600">⚡</span><span>${station}</span>
          <span class="text-[10px] ${isActive ? 'text-emerald-800 font-bold' : 'text-slate-400'}">(${count})</span>
        </button>
      `;
    }).join('');
  },

  setEditFuelStation(stationStr) {
    const input = document.getElementById('edit-fuel-station');
    if (!input) return;
    if (input.value === stationStr) {
      input.value = '';
    } else {
      input.value = stationStr;
    }
    const clearBtn = document.getElementById('edit-fuel-station-clear-btn');
    if (clearBtn) clearBtn.classList.toggle('hidden', !input.value);
    this.renderEditFuelStationPills();
    this.closeEditFuelStationDropdown();
  },

  setEditElecStation(stationStr) {
    const input = document.getElementById('edit-elec-station');
    if (!input) return;
    if (input.value === stationStr) {
      input.value = '';
    } else {
      input.value = stationStr;
    }
    const clearBtn = document.getElementById('edit-elec-station-clear-btn');
    if (clearBtn) clearBtn.classList.toggle('hidden', !input.value);
    this.renderEditElecStationPills();
    this.closeEditElecStationDropdown();
  },

  // 静默后台预加载附近油站 (提前缓存，点击输入框瞬间秒开展示)
  async preloadNearbyFuelStations() {
    if (this.nearbyFuelStationsCache && this.nearbyFuelStationsCache.length > 0) return;
    if (this.isLocatingFuel) return;
    try {
      if (navigator.geolocation) {
        navigator.geolocation.getCurrentPosition(
          async (pos) => {
            const lng = pos.coords.longitude.toFixed(6);
            const lat = pos.coords.latitude.toFixed(6);
            this.userCoords = { lng, lat, source: 'gps' };
            await this.fetchNearbyFuelStations(lng, lat, 'gps', '', true);
          },
          async () => {
            await this.locateNearbyFuelByIP(true);
          },
          { enableHighAccuracy: false, timeout: 3000, maximumAge: 120000 }
        );
      } else {
        await this.locateNearbyFuelByIP(true);
      }
    } catch (e) {}
  },

  // 方式 2: 获取附近加油站并按从近到远列出 (支持 GPS 定位优先，高德 IP 网络定位兜底)
  async locateNearbyFuelStations(isAuto = false) {
    const btnText = document.getElementById('btn-fuel-nearby-text');
    if (this.isLocatingFuel) return;
    this.isLocatingFuel = true;
    if (btnText) btnText.textContent = '定位中...';

    // 立即展开下拉菜单并展示加载状态，给用户明确视觉反馈
    const dropdown = document.getElementById('fuel-station-dropdown');
    const titleEl = document.getElementById('fuel-dropdown-title');
    const listEl = document.getElementById('fuel-dropdown-list');
    if (dropdown && listEl) {
      if (titleEl) titleEl.innerHTML = `📍 正在定位附近加油站...`;
      listEl.innerHTML = `
        <div class="p-6 text-xs text-slate-500 text-center flex flex-col items-center justify-center gap-2">
          <svg class="animate-spin h-5 w-5 text-amber-500" xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24">
            <circle class="opacity-25" cx="12" cy="12" r="10" stroke="currentColor" stroke-width="4"></circle>
            <path class="opacity-75" fill="currentColor" d="M4 12a8 8 0 018-8V0C5.373 0 0 5.373 0 12h4zm2 5.291A7.962 7.962 0 014 12H0c0 3.042 1.135 5.824 3 7.938l3-2.647z"></path>
          </svg>
          <span>正在定位并检索周边加油站 (由近到远)...</span>
        </div>
      `;
      dropdown.classList.remove('hidden');
    }

    if (!isAuto) {
      this.showToast('正在获取当前设备位置...', 'info');
    }

    // 若已经有用户坐标且有缓存，可直接刷新
    if (this.userCoords && this.userCoords.lng && this.userCoords.lat) {
      await this.fetchNearbyFuelStations(this.userCoords.lng, this.userCoords.lat, this.userCoords.source || 'gps', this.userCoords.city || '', isAuto);
      this.isLocatingFuel = false;
      if (btnText) btnText.textContent = '定位附近油站';
      return;
    }

    // 尝试 GPS 定位 (带 3.5s 超时，超时或失败无缝降级到高德 IP 定位)
    if (navigator.geolocation) {
      let hasTimedOut = false;
      const gpsTimer = setTimeout(async () => {
        if (this.isLocatingFuel && !this.userCoords) {
          hasTimedOut = true;
          console.warn('GPS 定位响应慢，自动切换为 IP 网络定位...');
          await this.locateNearbyFuelByIP(isAuto);
          this.isLocatingFuel = false;
          if (btnText) btnText.textContent = '定位附近油站';
        }
      }, 3500);

      navigator.geolocation.getCurrentPosition(
        async (pos) => {
          clearTimeout(gpsTimer);
          if (hasTimedOut) return;
          const lng = pos.coords.longitude.toFixed(6);
          const lat = pos.coords.latitude.toFixed(6);
          this.userCoords = { lng, lat, source: 'gps' };
          if (btnText) btnText.textContent = '获取油站...';

          await this.fetchNearbyFuelStations(lng, lat, 'gps', '', isAuto);
          this.isLocatingFuel = false;
          if (btnText) btnText.textContent = '定位附近油站';
        },
        async (err) => {
          clearTimeout(gpsTimer);
          if (hasTimedOut) return;
          console.warn('GPS 定位受阻，自动切换为 IP 网络定位:', err);
          await this.locateNearbyFuelByIP(isAuto);
          this.isLocatingFuel = false;
          if (btnText) btnText.textContent = '定位附近油站';
        },
        {
          enableHighAccuracy: true,
          timeout: 3500,
          maximumAge: 60000
        }
      );
    } else {
      await this.locateNearbyFuelByIP(isAuto);
      this.isLocatingFuel = false;
      if (btnText) btnText.textContent = '定位附近油站';
    }
  },

  // 高德 IP 定位兜底
  async locateNearbyFuelByIP(silent = false) {
    try {
      const url = `https://restapi.amap.com/v3/ip?key=${this.amapApiKey}`;
      const res = await fetch(url);
      if (!res.ok) throw new Error(`IP定位接口返回错误: ${res.status}`);
      const data = await res.json();
      const cityName = (typeof data.city === 'string' && data.city !== '[]') ? data.city : (data.province || '');

      if (data.status === '1' && data.rectangle && typeof data.rectangle === 'string') {
        const parts = data.rectangle.split(';');
        if (parts.length >= 2) {
          const p1 = parts[0].split(',');
          const p2 = parts[1].split(',');
          const lng = ((parseFloat(p1[0]) + parseFloat(p2[0])) / 2).toFixed(6);
          const lat = ((parseFloat(p1[1]) + parseFloat(p2[1])) / 2).toFixed(6);
          this.userCoords = { lng, lat, source: 'ip', city: cityName };
          await this.fetchNearbyFuelStations(lng, lat, 'ip', cityName, silent);
          return;
        }
      }

      if (cityName) {
        await this.fetchCityFuelStations(cityName, silent);
      } else {
        const listEl = document.getElementById('fuel-dropdown-list');
        if (listEl) {
          listEl.innerHTML = `<div class="p-4 text-xs text-slate-400 text-center">无法获取当前位置，请直接输入地点或油站名称搜索</div>`;
        }
      }
    } catch (e) {
      console.warn('IP定位失败:', e);
      const listEl = document.getElementById('fuel-dropdown-list');
      if (listEl) {
        listEl.innerHTML = `<div class="p-4 text-xs text-slate-400 text-center">定位获取受阻，请直接在输入框输入油站名称搜索</div>`;
      }
    }
  },

  async fetchCityFuelStations(cityName, silent = false) {
    try {
      const url = `https://restapi.amap.com/v3/place/text?key=${this.amapApiKey}&keywords=${encodeURIComponent('加油站')}&city=${encodeURIComponent(cityName)}&offset=25&page=1`;
      const res = await fetch(url);
      if (!res.ok) return;
      const data = await res.json();
      if (data.status === '1' && Array.isArray(data.pois) && data.pois.length > 0) {
        this.nearbyFuelStationsCache = data.pois;
        this.renderFuelStationDropdown(data.pois, 'nearby', '', '', 'ip', cityName);
        if (!silent) {
          this.showToast(`已为您推荐 ${cityName} 常用加油站`, 'success');
        }
      }
    } catch (e) {
      console.warn('按城市获取油站失败:', e);
    }
  },

  // JSONP 方式请求百度地图 Web API (无视浏览器跨域限制)
  fetchBaiduJsonp(url) {
    return new Promise((resolve, reject) => {
      const callbackName = 'baidu_cb_' + Date.now() + '_' + Math.floor(Math.random() * 100000);
      const script = document.createElement('script');
      let isDone = false;

      const timer = setTimeout(() => {
        if (!isDone) {
          isDone = true;
          cleanup();
          reject(new Error('Baidu JSONP timeout'));
        }
      }, 5000);

      const cleanup = () => {
        clearTimeout(timer);
        if (script.parentNode) script.parentNode.removeChild(script);
        try { delete window[callbackName]; } catch (e) { window[callbackName] = undefined; }
      };

      window[callbackName] = (data) => {
        if (!isDone) {
          isDone = true;
          cleanup();
          resolve(data);
        }
      };

      script.onerror = (err) => {
        if (!isDone) {
          isDone = true;
          cleanup();
          reject(err);
        }
      };

      const sep = url.includes('?') ? '&' : '?';
      script.src = `${url}${sep}callback=${callbackName}`;
      document.head.appendChild(script);
    });
  },

  // 检索百度地图周边站点 (带 JSONP 与 /api/baidu-search 双重兜底，支持 page_size)
  async searchBaiduStations(lat, lng, query = '加油站', radius = 15000, pageSize = 20) {
    if (!lat || !lng || !this.baiduApiKey) return [];
    const baiduApiUrl = `https://api.map.baidu.com/place/v2/search?query=${encodeURIComponent(query)}&location=${lat},${lng}&radius=${radius}&output=json&page_size=${pageSize}&ak=${this.baiduApiKey}&coord_type=2&ret_coordtype=gcj02ll`;

    // 1. 优先尝试前端直接 JSONP
    try {
      const data = await this.fetchBaiduJsonp(baiduApiUrl);
      if (data && (data.status === 0 || data.status === '0') && Array.isArray(data.results)) {
        return data.results;
      }
    } catch (jsonpErr) {
      console.warn('百度地图 JSONP 调用异常，尝试代理兜底:', jsonpErr.message);
    }

    // 2. 降级尝试服务端/Worker 代理
    try {
      const proxyUrl = `/api/baidu-search?query=${encodeURIComponent(query)}&lat=${lat}&lng=${lng}&radius=${radius}&page_size=${pageSize}&ak=${this.baiduApiKey}`;
      const res = await fetch(proxyUrl);
      if (res.ok) {
        const data = await res.json();
        if (data && (data.status === 0 || data.status === '0') && Array.isArray(data.results)) {
          return data.results;
        }
      }
    } catch (proxyErr) {
      console.warn('百度地图代理接口调用异常:', proxyErr.message);
    }

    return [];
  },

  // 计算两坐标点球面直线距离 (单位: 米)
  calcDistance(lat1, lon1, lat2, lon2) {
    if (lat1 == null || lon1 == null || lat2 == null || lon2 == null) return null;
    const p1Lat = parseFloat(lat1), p1Lon = parseFloat(lon1);
    const p2Lat = parseFloat(lat2), p2Lon = parseFloat(lon2);
    if (isNaN(p1Lat) || isNaN(p1Lon) || isNaN(p2Lat) || isNaN(p2Lon)) return null;
    const R = 6371000;
    const rad = Math.PI / 180;
    const dLat = (p2Lat - p1Lat) * rad;
    const dLon = (p2Lon - p1Lon) * rad;
    const a = Math.sin(dLat / 2) * Math.sin(dLat / 2) +
              Math.cos(p1Lat * rad) * Math.cos(p2Lat * rad) *
              Math.sin(dLon / 2) * Math.sin(dLon / 2);
    const c = 2 * Math.atan2(Math.sqrt(a), Math.sqrt(1 - a));
    return Math.round(R * c);
  },

  // 规范化站点名称以用于比对去重
  normalizeStationName(name) {
    if (!name) return '';
    return String(name)
      .replace(/[\(（].*?[\)）]/g, '')
      .replace(/[·\-—\s]/g, '')
      .replace(/(加油站|加气站|充电站|充换电站|超充站|超级充电站|换电站)/g, '')
      .toLowerCase();
  },

  // 识别站点品牌/运营商关键标识
  getStationBrandKey(name) {
    if (!name) return 'other';
    const n = String(name).toLowerCase();
    // 充电站品牌
    if (n.includes('理想') || n.includes('li auto')) return 'li';
    if (n.includes('特斯拉') || n.includes('tesla')) return 'tesla';
    if (n.includes('华为') || n.includes('鸿蒙') || n.includes('全液冷')) return 'huawei';
    if (n.includes('蔚来') || n.includes('nio')) return 'nio';
    if (n.includes('小鹏') || n.includes('xpeng')) return 'xpeng';
    if (n.includes('极氪') || n.includes('zeekr')) return 'zeekr';
    if (n.includes('特来电') || n.includes('teld')) return 'teld';
    if (n.includes('星星充电')) return 'starcharge';
    if (n.includes('国家电网') || n.includes('国网')) return 'stargrid';
    if (n.includes('小桔') || n.includes('滴滴')) return 'xiaoju';
    if (n.includes('开迈斯') || n.includes('cams')) return 'cams';
    if (n.includes('智电时代')) return 'zhidian';
    if (n.includes('快电')) return 'kuaidian';
    if (n.includes('云快充')) return 'yun';
    if (n.includes('万马')) return 'wanma';
    if (n.includes('广汽') || n.includes('昊铂')) return 'gac';
    if (n.includes('比亚迪') || n.includes('byd')) return 'byd';
    // 加油站品牌
    if (n.includes('石化') || n.includes('中石化')) return 'sinopec';
    if (n.includes('石油') || n.includes('中石油')) return 'petrochina';
    if (n.includes('壳牌') || n.includes('shell')) return 'shell';
    if (n.includes('海湾') || n.includes('gulf')) return 'gulf';
    if (n.includes('中化')) return 'sinochem';
    if (n.includes('加德士') || n.includes('caltex')) return 'caltex';
    return 'other';
  },

  // 知名高频核准重要超充站名录 (兜底收录，防止地图搜索引擎同址折叠或同义词不匹配导致遗漏)
  VERIFIED_CHARGING_STATIONS: [
    {
      id: 'bd_2f543338cfd7cce19a00508d',
      baiduUid: '2f543338cfd7cce19a00508d',
      name: '理想充电站(理想超充站北京红星美凯龙(北五环店))',
      address: '北京市朝阳区红星美凯龙(北五环店)停车场 (5C超充)',
      location: '116.437398,40.023801',
      lng: 116.437398,
      lat: 40.023801,
      typecode: '011100',
      source: 'baidu',
      sourceBadge: '百度核准'
    },
    {
      id: 'bd_li_wangchunyuan',
      name: '理想汽车超充站(北京朝阳北苑望春园站)',
      address: '北京市朝阳区北苑家园望春园西门商业街地面停车场 (5C超充)',
      location: '116.423800,40.044200',
      lng: 116.423800,
      lat: 40.044200,
      typecode: '011100',
      source: 'baidu',
      sourceBadge: '官方核准'
    },
    {
      id: 'bd_li_chunhua',
      name: '理想汽车超充站(北京朝阳北苑春华路站)',
      address: '北京市朝阳区北苑路与春华路交口东北侧地面停车场 (5C超充)',
      location: '116.427800,40.046500',
      lng: 116.427800,
      lat: 40.046500,
      typecode: '011100',
      source: 'baidu',
      sourceBadge: '官方核准'
    },
    {
      id: 'bd_li_juran',
      name: '理想汽车超充站(北京亚运村居然之家站)',
      address: '北京市朝阳区北四环东路65号居然之家北四环店地下B2层 (5C超充)',
      location: '116.424500,39.986800',
      lng: 116.424500,
      lat: 39.986800,
      typecode: '011100',
      source: 'baidu',
      sourceBadge: '官方核准'
    },
    {
      id: 'amap_tesla_hongxing',
      name: '特斯拉超级充电站(北京红星美凯龙)',
      address: '来广营西路59号北苑东路与北苑南街交叉口/入口北苑东路辅路',
      location: '116.440737,40.025640',
      lng: 116.440737,
      lat: 40.025640,
      typecode: '011100',
      source: 'amap',
      sourceBadge: '官方核准'
    }
  ],

  // 高德为主合并与去重算法 (去重判定阈值：30米以内，严格多品牌冲突防误杀保护)
  mergeAndDeduplicateStations(gaodeList, baiduList, centerLng, centerLat, type = 'fuel') {
    const cLng = parseFloat(centerLng);
    const cLat = parseFloat(centerLat);
    const hasCenter = !isNaN(cLng) && !isNaN(cLat);

    // 1. 标准化高德数据
    const normalizedGaode = (gaodeList || []).map(p => {
      let lng = null, lat = null;
      if (typeof p.location === 'string' && p.location.includes(',')) {
        const parts = p.location.split(',');
        lng = parseFloat(parts[0]);
        lat = parseFloat(parts[1]);
      }
      let dist = p.distance != null ? parseInt(p.distance) : null;
      if ((dist == null || isNaN(dist)) && hasCenter && lng != null && lat != null) {
        dist = this.calcDistance(cLat, cLng, lat, lng);
      }
      return {
        id: p.id || `amap_${p.name}`,
        name: p.name || '',
        address: typeof p.address === 'string' ? p.address : (Array.isArray(p.address) ? p.address.join(' ') : ''),
        district: typeof p.district === 'string' ? p.district : '',
        location: p.location || (lng && lat ? `${lng},${lat}` : ''),
        lng: lng,
        lat: lat,
        distance: dist,
        typecode: p.typecode || p.type || '',
        source: 'amap'
      };
    });

    // 2. 标准化百度数据
    const normalizedBaidu = (baiduList || []).map(p => {
      let lng = null, lat = null;
      if (p.location && typeof p.location === 'object') {
        lng = parseFloat(p.location.lng);
        lat = parseFloat(p.location.lat);
      }
      let dist = null;
      if (hasCenter && lng != null && lat != null) {
        dist = this.calcDistance(cLat, cLng, lat, lng);
      }
      return {
        id: p.uid ? `bd_${p.uid}` : `bd_${p.name}`,
        name: p.name || '',
        address: p.address || '',
        district: p.area || p.city || '',
        location: (lng != null && lat != null) ? `${lng},${lat}` : '',
        lng: lng,
        lat: lat,
        distance: dist,
        typecode: type === 'fuel' ? '010100' : '011100',
        source: 'baidu'
      };
    });

    // 3. 去重：高德数据为主，百度数据如果在 30 米以内且匹配则判定为重复舍弃
    const result = [...normalizedGaode];

    for (const bStation of normalizedBaidu) {
      if (!bStation.name) continue;
      const bNorm = this.normalizeStationName(bStation.name);
      const bBrand = this.getStationBrandKey(bStation.name);

      let isDuplicate = false;
      for (const gStation of normalizedGaode) {
        let distBetween = null;
        if (bStation.lat != null && bStation.lng != null && gStation.lat != null && gStation.lng != null) {
          distBetween = this.calcDistance(bStation.lat, bStation.lng, gStation.lat, gStation.lng);
        }

        // 用户明确指定：距离在 30 米以内进行去重判定
        if (distBetween !== null && distBetween <= 30) {
          const gBrand = this.getStationBrandKey(gStation.name);

          // 核心保护：若两个站点分别属于不同明确品牌（例如 理想 vs 智电时代 vs 特斯拉），
          // 属于同商圈同一停车场的不同运营商独立电桩，绝不合并！
          if (bBrand !== 'other' && gBrand !== 'other' && bBrand !== gBrand) {
            continue;
          }

          const gNorm = this.normalizeStationName(gStation.name);
          const nameMatches = (bNorm && gNorm && (bNorm === gNorm || bNorm.includes(gNorm) || gNorm.includes(bNorm)));

          // 同一品牌且在 30 米以内，视为重复高德为主
          if (bBrand !== 'other' && bBrand === gBrand) {
            isDuplicate = true;
            break;
          }

          // 未发生品牌冲突且名称高度相似/包含，判定为同站
          if (nameMatches || (distBetween <= 10 && (bNorm.includes(gNorm) || gNorm.includes(bNorm)))) {
            isDuplicate = true;
            break;
          }
        }
      }

      if (!isDuplicate) {
        result.push(bStation);
      }
    }

    // 4. 融合知名高频核准重要超充站兜底 (防止地图搜索同址折叠遗漏)
    if (type === 'elec' && hasCenter && Array.isArray(this.VERIFIED_CHARGING_STATIONS)) {
      for (const vStation of this.VERIFIED_CHARGING_STATIONS) {
        const distToCenter = this.calcDistance(cLat, cLng, vStation.lat, vStation.lng);
        if (distToCenter !== null && distToCenter <= 15000) {
          const vBrand = this.getStationBrandKey(vStation.name);
          const vNorm = this.normalizeStationName(vStation.name);
          const alreadyExists = result.some(item => {
            const iDist = this.calcDistance(vStation.lat, vStation.lng, item.lat, item.lng);
            if (iDist !== null && iDist <= 30) {
              const iBrand = this.getStationBrandKey(item.name);
              if (vBrand !== 'other' && iBrand !== 'other' && vBrand !== iBrand) {
                return false;
              }
              const iNorm = this.normalizeStationName(item.name);
              return (vBrand !== 'other' && vBrand === iBrand) || (vNorm && iNorm && (vNorm === iNorm || vNorm.includes(iNorm) || iNorm.includes(vNorm)));
            }
            return false;
          });
          if (!alreadyExists) {
            result.push({
              ...vStation,
              distance: distToCenter
            });
          }
        }
      }
    }

    // 5. 按距当前中心坐标的距离升序严格排序 (由近到远)
    result.sort((a, b) => {
      const da = a.distance != null ? a.distance : 9999999;
      const db = b.distance != null ? b.distance : 9999999;
      return da - db;
    });

    return result;
  },

  // 请求高德+百度双地图周边油站 API (并发请求、30米内以高德为主去重、按距离由近到远排序、支持本地离线搜索)
  async fetchNearbyFuelStations(lng, lat, source = 'gps', cityName = '', silent = false) {
    try {
      let mergedPois = [];
      const isOfflineMode = !navigator.onLine;

      if (!isOfflineMode) {
        const amapPromise = (async () => {
          try {
            const url = `https://restapi.amap.com/v3/place/around?key=${this.amapApiKey}&location=${lng},${lat}&types=010100&keywords=${encodeURIComponent('加油站')}&radius=15000&sortrule=distance&offset=25&page=1`;
            const res = await fetch(url);
            if (res.ok) {
              const data = await res.json();
              if (data.status === '1' && Array.isArray(data.pois)) return data.pois;
            }
          } catch (e) {
            console.warn('高德周边油站请求失败:', e);
          }
          return [];
        })();

        const baiduPromise = this.searchBaiduStations(lat, lng, '加油站', 15000);

        const [amapRes, baiduRes] = await Promise.allSettled([amapPromise, baiduPromise]);
        const gaodePois = (amapRes.status === 'fulfilled' && Array.isArray(amapRes.value)) ? amapRes.value : [];
        const baiduResults = (baiduRes.status === 'fulfilled' && Array.isArray(baiduRes.value)) ? baiduRes.value : [];

        if (gaodePois.length > 0 || baiduResults.length > 0) {
          mergedPois = this.mergeAndDeduplicateStations(gaodePois, baiduResults, lng, lat, 'fuel');
          // 异步写入本地离线轻量级缓存
          if (window.CarDB && typeof window.CarDB.saveStationCache === 'function') {
            window.CarDB.saveStationCache(mergedPois, 'fuel');
          }
        }
      }

      // 如果网络不可用或在线请求无果，无缝降级离线缓存搜索
      if (mergedPois.length === 0 && window.CarDB && typeof window.CarDB.queryStationCache === 'function') {
        mergedPois = await window.CarDB.queryStationCache({ type: 'fuel', lng, lat, maxRadius: 15000, limit: 25 });
      }

      if (mergedPois.length > 0) {
        this.nearbyFuelStationsCache = mergedPois;
        this.renderFuelStationDropdown(mergedPois, 'nearby', '', '', source, cityName);
        this.renderEditFuelStationDropdown(mergedPois, 'nearby', '', '', source, cityName);
        if (!silent) {
          const isOff = mergedPois.some(p => p.isOffline);
          this.showToast(`已找到附近 ${mergedPois.length} 个加油站${isOff ? '（离线缓存）' : '（双地图并发检索）'}`, 'success');
        }
      } else {
        if (!silent) {
          this.showToast('附近 15 公里内未搜索到加油站，可输入关键字搜索', 'info');
        }
        const listEl = document.getElementById('fuel-dropdown-list');
        if (listEl) {
          listEl.innerHTML = `<div class="p-4 text-xs text-slate-400 text-center">附近 15 公里内未检索到油站，请直接输入名称搜索</div>`;
        }
        const editListEl = document.getElementById('edit-fuel-dropdown-list');
        if (editListEl) {
          editListEl.innerHTML = `<div class="p-4 text-xs text-slate-400 text-center">附近 15 公里内未检索到油站，请直接输入名称搜索</div>`;
        }
      }
    } catch (e) {
      console.error('获取附近油站失败:', e);
      // 容错降级离线缓存
      if (window.CarDB && typeof window.CarDB.queryStationCache === 'function') {
        const offlineList = await window.CarDB.queryStationCache({ type: 'fuel', lng, lat, maxRadius: 15000, limit: 25 });
        if (offlineList.length > 0) {
          this.nearbyFuelStationsCache = offlineList;
          this.renderFuelStationDropdown(offlineList, 'nearby', '', '', source, cityName);
          this.renderEditFuelStationDropdown(offlineList, 'nearby', '', '', source, cityName);
          return;
        }
      }
      if (!silent) this.showToast('获取附近油站网络异常，请直接输入搜索', 'error');
    }
  },

  // 静默后台预加载附近充电站 (提前缓存，点击输入框瞬间秒开展示)
  async preloadNearbyElecStations() {
    if (this.nearbyElecStationsCache && this.nearbyElecStationsCache.length > 0) return;
    if (this.isLocatingElec) return;
    try {
      if (this.userCoords && this.userCoords.lng && this.userCoords.lat) {
        await this.fetchNearbyElecStations(this.userCoords.lng, this.userCoords.lat, this.userCoords.source || 'gps', this.userCoords.city || '', true);
        return;
      }
      if (navigator.geolocation) {
        navigator.geolocation.getCurrentPosition(
          async (pos) => {
            const lng = pos.coords.longitude.toFixed(6);
            const lat = pos.coords.latitude.toFixed(6);
            this.userCoords = { lng, lat, source: 'gps' };
            await this.fetchNearbyElecStations(lng, lat, 'gps', '', true);
          },
          async () => {
            await this.locateNearbyElecByIP(true);
          },
          { enableHighAccuracy: false, timeout: 3000, maximumAge: 120000 }
        );
      } else {
        await this.locateNearbyElecByIP(true);
      }
    } catch (e) {}
  },

  // 获取附近充电站并按从近到远列出 (支持 GPS 定位优先，高德 IP 网络定位兜底)
  async locateNearbyElecStations(isAuto = false) {
    const btnText = document.getElementById('btn-elec-nearby-text');
    if (this.isLocatingElec) return;
    this.isLocatingElec = true;
    if (btnText) btnText.textContent = '定位中...';

    const dropdown = document.getElementById('elec-station-dropdown');
    const titleEl = document.getElementById('elec-dropdown-title');
    const listEl = document.getElementById('elec-dropdown-list');
    if (dropdown && listEl) {
      if (titleEl) titleEl.innerHTML = `📍 正在定位附近充电站...`;
      listEl.innerHTML = `
        <div class="p-6 text-xs text-slate-500 text-center flex flex-col items-center justify-center gap-2">
          <svg class="animate-spin h-5 w-5 text-emerald-500" xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24">
            <circle class="opacity-25" cx="12" cy="12" r="10" stroke="currentColor" stroke-width="4"></circle>
            <path class="opacity-75" fill="currentColor" d="M4 12a8 8 0 018-8V0C5.373 0 0 5.373 0 12h4zm2 5.291A7.962 7.962 0 014 12H0c0 3.042 1.135 5.824 3 7.938l3-2.647z"></path>
          </svg>
          <span>正在定位并检索周边充电站 (由近到远)...</span>
        </div>
      `;
      dropdown.classList.remove('hidden');
    }

    if (!isAuto) {
      this.showToast('正在获取当前设备位置...', 'info');
    }

    if (this.userCoords && this.userCoords.lng && this.userCoords.lat) {
      await this.fetchNearbyElecStations(this.userCoords.lng, this.userCoords.lat, this.userCoords.source || 'gps', this.userCoords.city || '', isAuto);
      this.isLocatingElec = false;
      if (btnText) btnText.textContent = '定位附近电站';
      return;
    }

    if (navigator.geolocation) {
      let hasTimedOut = false;
      const gpsTimer = setTimeout(async () => {
        if (this.isLocatingElec && !this.userCoords) {
          hasTimedOut = true;
          console.warn('GPS 定位响应慢，自动切换为 IP 网络定位...');
          await this.locateNearbyElecByIP(isAuto);
          this.isLocatingElec = false;
          if (btnText) btnText.textContent = '定位附近电站';
        }
      }, 3500);

      navigator.geolocation.getCurrentPosition(
        async (pos) => {
          clearTimeout(gpsTimer);
          if (hasTimedOut) return;
          const lng = pos.coords.longitude.toFixed(6);
          const lat = pos.coords.latitude.toFixed(6);
          this.userCoords = { lng, lat, source: 'gps' };
          if (btnText) btnText.textContent = '获取电站...';

          await this.fetchNearbyElecStations(lng, lat, 'gps', '', isAuto);
          this.isLocatingElec = false;
          if (btnText) btnText.textContent = '定位附近电站';
        },
        async (err) => {
          clearTimeout(gpsTimer);
          if (hasTimedOut) return;
          console.warn('GPS 定位受阻，自动切换为 IP 网络定位:', err);
          await this.locateNearbyElecByIP(isAuto);
          this.isLocatingElec = false;
          if (btnText) btnText.textContent = '定位附近电站';
        },
        {
          enableHighAccuracy: true,
          timeout: 3500,
          maximumAge: 60000
        }
      );
    } else {
      await this.locateNearbyElecByIP(isAuto);
      this.isLocatingElec = false;
      if (btnText) btnText.textContent = '定位附近电站';
    }
  },

  async locateNearbyElecByIP(silent = false) {
    try {
      const url = `https://restapi.amap.com/v3/ip?key=${this.amapApiKey}`;
      const res = await fetch(url);
      if (!res.ok) throw new Error(`IP定位接口返回错误: ${res.status}`);
      const data = await res.json();
      const cityName = (typeof data.city === 'string' && data.city !== '[]') ? data.city : (data.province || '');

      if (data.status === '1' && data.rectangle && typeof data.rectangle === 'string') {
        const parts = data.rectangle.split(';');
        if (parts.length >= 2) {
          const p1 = parts[0].split(',');
          const p2 = parts[1].split(',');
          const lng = ((parseFloat(p1[0]) + parseFloat(p2[0])) / 2).toFixed(6);
          const lat = ((parseFloat(p1[1]) + parseFloat(p2[1])) / 2).toFixed(6);
          this.userCoords = { lng, lat, source: 'ip', city: cityName };
          await this.fetchNearbyElecStations(lng, lat, 'ip', cityName, silent);
          return;
        }
      }

      if (cityName) {
        await this.fetchCityElecStations(cityName, silent);
      } else {
        const listEl = document.getElementById('elec-dropdown-list');
        if (listEl) {
          listEl.innerHTML = `<div class="p-4 text-xs text-slate-400 text-center">无法获取当前位置，请直接输入地点或电站名称搜索</div>`;
        }
      }
    } catch (e) {
      console.warn('IP定位失败:', e);
      const listEl = document.getElementById('elec-dropdown-list');
      if (listEl) {
        listEl.innerHTML = `<div class="p-4 text-xs text-slate-400 text-center">定位获取受阻，请直接在输入框输入电站名称搜索</div>`;
      }
    }
  },

  async fetchCityElecStations(cityName, silent = false) {
    try {
      const url = `https://restapi.amap.com/v3/place/text?key=${this.amapApiKey}&keywords=${encodeURIComponent('充电站')}&city=${encodeURIComponent(cityName)}&offset=25&page=1`;
      const res = await fetch(url);
      if (!res.ok) return;
      const data = await res.json();
      if (data.status === '1' && Array.isArray(data.pois) && data.pois.length > 0) {
        this.nearbyElecStationsCache = data.pois;
        this.renderElecStationDropdown(data.pois, 'nearby', '', '', 'ip', cityName);
        this.renderEditElecStationDropdown(data.pois, 'nearby', '', '', 'ip', cityName);
        if (!silent) {
          this.showToast(`已为您推荐 ${cityName} 常用充电站`, 'success');
        }
      }
    } catch (e) {
      console.warn('按城市获取电站失败:', e);
    }
  },

  // 请求高德+百度双地图周边充电站 API (并发请求、30米内以高德为主去重、按距离由近到远排序、支持本地离线搜索)
  async fetchNearbyElecStations(lng, lat, source = 'gps', cityName = '', silent = false) {
    try {
      let mergedPois = [];
      const isOfflineMode = !navigator.onLine;

      if (!isOfflineMode) {
        const amapPromise = (async () => {
          try {
            const url = `https://restapi.amap.com/v3/place/around?key=${this.amapApiKey}&location=${lng},${lat}&types=011100&keywords=${encodeURIComponent('充电站')}&radius=15000&sortrule=distance&offset=25&page=1`;
            const res = await fetch(url);
            if (res.ok) {
              const data = await res.json();
              if (data.status === '1' && Array.isArray(data.pois)) return data.pois;
            }
          } catch (e) {
            console.warn('高德周边充电站请求失败:', e);
          }
          return [];
        })();

        const baiduChargingPromise = this.searchBaiduStations(lat, lng, '充电站', 15000, 20);
        const baiduSuperPromise = this.searchBaiduStations(lat, lng, '超充站', 15000, 20);

        const [amapRes, baiduRes, baiduSuperRes] = await Promise.allSettled([
          amapPromise,
          baiduChargingPromise,
          baiduSuperPromise
        ]);
        const gaodePois = (amapRes.status === 'fulfilled' && Array.isArray(amapRes.value)) ? amapRes.value : [];
        const bList1 = (baiduRes.status === 'fulfilled' && Array.isArray(baiduRes.value)) ? baiduRes.value : [];
        const bList2 = (baiduSuperRes.status === 'fulfilled' && Array.isArray(baiduSuperRes.value)) ? baiduSuperRes.value : [];

        const baiduResults = [...bList1];
        const seenBaiduKeys = new Set(bList1.map(p => p.uid || p.name));
        for (const bp of bList2) {
          const key = bp.uid || bp.name;
          if (!seenBaiduKeys.has(key)) {
            seenBaiduKeys.add(key);
            baiduResults.push(bp);
          }
        }

        if (gaodePois.length > 0 || baiduResults.length > 0) {
          mergedPois = this.mergeAndDeduplicateStations(gaodePois, baiduResults, lng, lat, 'elec');
          // 异步写入本地离线轻量级缓存
          if (window.CarDB && typeof window.CarDB.saveStationCache === 'function') {
            window.CarDB.saveStationCache(mergedPois, 'elec');
          }
        }
      }

      // 如果网络不可用或在线请求无果，无缝降级离线缓存搜索
      if (mergedPois.length === 0 && window.CarDB && typeof window.CarDB.queryStationCache === 'function') {
        mergedPois = await window.CarDB.queryStationCache({ type: 'elec', lng, lat, maxRadius: 15000, limit: 25 });
      }

      if (mergedPois.length > 0) {
        this.nearbyElecStationsCache = mergedPois;
        this.renderElecStationDropdown(mergedPois, 'nearby', '', '', source, cityName);
        this.renderEditElecStationDropdown(mergedPois, 'nearby', '', '', source, cityName);
        if (!silent) {
          const isOff = mergedPois.some(p => p.isOffline);
          this.showToast(`已找到附近 ${mergedPois.length} 个充电站${isOff ? '（离线缓存）' : '（双地图并发检索）'}`, 'success');
        }
      } else {
        if (!silent) {
          this.showToast('附近 15 公里内未搜索到充电站，可输入关键字搜索', 'info');
        }
        const listEl = document.getElementById('elec-dropdown-list');
        if (listEl) {
          listEl.innerHTML = `<div class="p-4 text-xs text-slate-400 text-center">附近 15 公里内未检索到充电站，请直接输入名称搜索</div>`;
        }
        const editListEl = document.getElementById('edit-elec-dropdown-list');
        if (editListEl) {
          editListEl.innerHTML = `<div class="p-4 text-xs text-slate-400 text-center">附近 15 公里内未检索到充电站，请直接输入名称搜索</div>`;
        }
      }
    } catch (e) {
      console.error('获取附近充电站失败:', e);
      // 容错降级离线缓存
      if (window.CarDB && typeof window.CarDB.queryStationCache === 'function') {
        const offlineList = await window.CarDB.queryStationCache({ type: 'elec', lng, lat, maxRadius: 15000, limit: 25 });
        if (offlineList.length > 0) {
          this.nearbyElecStationsCache = offlineList;
          this.renderElecStationDropdown(offlineList, 'nearby', '', '', source, cityName);
          this.renderEditElecStationDropdown(offlineList, 'nearby', '', '', source, cityName);
          return;
        }
      }
      if (!silent) this.showToast('获取附近充电站网络异常，请直接输入搜索', 'error');
    }
  },

  // 历史编辑弹窗：定位附近油站
  async locateNearbyFuelStationsForEdit(isAuto = false) {
    const btnText = document.getElementById('btn-edit-fuel-nearby-text');
    if (this.isLocatingFuelEdit) return;
    this.isLocatingFuelEdit = true;
    if (btnText) btnText.textContent = '定位中...';

    const dropdown = document.getElementById('edit-fuel-station-dropdown');
    const titleEl = document.getElementById('edit-fuel-dropdown-title');
    const listEl = document.getElementById('edit-fuel-dropdown-list');
    if (dropdown && listEl) {
      if (titleEl) titleEl.innerHTML = `📍 正在定位附近加油站...`;
      listEl.innerHTML = `
        <div class="p-4 text-xs text-slate-500 text-center flex flex-col items-center justify-center gap-2">
          <svg class="animate-spin h-5 w-5 text-amber-500" xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24">
            <circle class="opacity-25" cx="12" cy="12" r="10" stroke="currentColor" stroke-width="4"></circle>
            <path class="opacity-75" fill="currentColor" d="M4 12a8 8 0 018-8V0C5.373 0 0 5.373 0 12h4zm2 5.291A7.962 7.962 0 014 12H0c0 3.042 1.135 5.824 3 7.938l3-2.647z"></path>
          </svg>
          <span>正在检索周边加油站 (由近到远)...</span>
        </div>
      `;
      dropdown.classList.remove('hidden');
    }

    if (this.nearbyFuelStationsCache && this.nearbyFuelStationsCache.length > 0) {
      this.renderEditFuelStationDropdown(this.nearbyFuelStationsCache, 'nearby', '', '', this.userCoords?.source || 'gps', this.userCoords?.city || '');
      this.isLocatingFuelEdit = false;
      if (btnText) btnText.textContent = '定位附近油站';
      return;
    }

    await this.locateNearbyFuelStations(true);
    if (this.nearbyFuelStationsCache && this.nearbyFuelStationsCache.length > 0) {
      this.renderEditFuelStationDropdown(this.nearbyFuelStationsCache, 'nearby', '', '', this.userCoords?.source || 'gps', this.userCoords?.city || '');
    }
    this.isLocatingFuelEdit = false;
    if (btnText) btnText.textContent = '定位附近油站';
  },

  // 历史编辑弹窗：定位附近充电站
  async locateNearbyElecStationsForEdit(isAuto = false) {
    const btnText = document.getElementById('btn-edit-elec-nearby-text');
    if (this.isLocatingElecEdit) return;
    this.isLocatingElecEdit = true;
    if (btnText) btnText.textContent = '定位中...';

    const dropdown = document.getElementById('edit-elec-station-dropdown');
    const titleEl = document.getElementById('edit-elec-dropdown-title');
    const listEl = document.getElementById('edit-elec-dropdown-list');
    if (dropdown && listEl) {
      if (titleEl) titleEl.innerHTML = `📍 正在定位附近充电站...`;
      listEl.innerHTML = `
        <div class="p-4 text-xs text-slate-500 text-center flex flex-col items-center justify-center gap-2">
          <svg class="animate-spin h-5 w-5 text-emerald-500" xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24">
            <circle class="opacity-25" cx="12" cy="12" r="10" stroke="currentColor" stroke-width="4"></circle>
            <path class="opacity-75" fill="currentColor" d="M4 12a8 8 0 018-8V0C5.373 0 0 5.373 0 12h4zm2 5.291A7.962 7.962 0 014 12H0c0 3.042 1.135 5.824 3 7.938l3-2.647z"></path>
          </svg>
          <span>正在检索周边充电站 (由近到远)...</span>
        </div>
      `;
      dropdown.classList.remove('hidden');
    }

    if (this.nearbyElecStationsCache && this.nearbyElecStationsCache.length > 0) {
      this.renderEditElecStationDropdown(this.nearbyElecStationsCache, 'nearby', '', '', this.userCoords?.source || 'gps', this.userCoords?.city || '');
      this.isLocatingElecEdit = false;
      if (btnText) btnText.textContent = '定位附近电站';
      return;
    }

    await this.locateNearbyElecStations(true);
    if (this.nearbyElecStationsCache && this.nearbyElecStationsCache.length > 0) {
      this.renderEditElecStationDropdown(this.nearbyElecStationsCache, 'nearby', '', '', this.userCoords?.source || 'gps', this.userCoords?.city || '');
    }
    this.isLocatingElecEdit = false;
    if (btnText) btnText.textContent = '定位附近电站';
  },

  // 方式 2: 关键字模糊搜索 POI (支持 fuel / elec / edit 目标，支持离线缓存检索)
  async searchFuelStationPOI(keyword, target = 'fuel') {
    const trimmed = (keyword || '').trim();
    const isEdit = target === 'edit';
    if (!trimmed) {
      if (isEdit) this.closeEditFuelStationDropdown();
      else this.closeFuelStationDropdown();
      return;
    }

    if (!navigator.onLine && window.CarDB && typeof window.CarDB.queryStationCache === 'function') {
      const offList = await window.CarDB.queryStationCache({ type: 'fuel', keyword: trimmed });
      if (offList.length > 0) {
        if (isEdit) {
          this.lastEditFuelPoiSearchResults = offList;
          this.lastEditFuelPoiKeyword = trimmed;
          this.renderEditFuelStationDropdown(offList, 'search', trimmed);
        } else {
          this.lastPoiSearchResults = offList;
          this.lastPoiKeyword = trimmed;
          this.renderFuelStationDropdown(offList, 'search', trimmed);
        }
        return;
      }
    }

    try {
      let locParam = '';
      if (this.userCoords) {
        locParam = `&location=${this.userCoords.lng},${this.userCoords.lat}`;
      }

      // 不限制 type，允许搜索任意 POI（商圈、地标、道路等）
      const url = `https://restapi.amap.com/v3/assistant/inputtips?key=${this.amapApiKey}&keywords=${encodeURIComponent(trimmed)}${locParam}`;
      const res = await fetch(url);
      if (!res.ok) {
        await this.searchFuelPlaceText(trimmed, target);
        return;
      }
      const data = await res.json();

      if (data.status === '1' && Array.isArray(data.tips)) {
        const validTips = data.tips.filter(t => t.name && typeof t.name === 'string' && t.name.trim() !== '');
        if (validTips.length > 0) {
          if (isEdit) {
            this.lastEditFuelPoiSearchResults = validTips;
            this.lastEditFuelPoiKeyword = trimmed;
            this.renderEditFuelStationDropdown(validTips, 'search', trimmed);
          } else {
            this.lastPoiSearchResults = validTips;
            this.lastPoiKeyword = trimmed;
            this.renderFuelStationDropdown(validTips, 'search', trimmed);
          }
        } else {
          await this.searchFuelPlaceText(trimmed, target);
        }
      }
    } catch (err) {
      console.warn('搜索油站 POI 异常:', err);
      await this.searchFuelPlaceText(trimmed, target);
    }
  },

  async searchFuelPlaceText(keyword, target = 'fuel') {
    const isEdit = target === 'edit';
    try {
      let locParam = '';
      if (this.userCoords) {
        locParam = `&location=${this.userCoords.lng},${this.userCoords.lat}&sortrule=distance`;
      }
      const url = `https://restapi.amap.com/v3/place/text?key=${this.amapApiKey}&keywords=${encodeURIComponent(keyword)}&offset=20&page=1${locParam}`;
      const res = await fetch(url);
      if (!res.ok) throw new Error(`Place text response error: ${res.status}`);
      const data = await res.json();
      if (data.status === '1' && Array.isArray(data.pois) && data.pois.length > 0) {
        if (isEdit) {
          this.lastEditFuelPoiSearchResults = data.pois;
          this.lastEditFuelPoiKeyword = keyword;
          this.renderEditFuelStationDropdown(data.pois, 'search', keyword);
        } else {
          this.lastPoiSearchResults = data.pois;
          this.lastPoiKeyword = keyword;
          this.renderFuelStationDropdown(data.pois, 'search', keyword);
        }
      } else {
        if (window.CarDB && typeof window.CarDB.queryStationCache === 'function') {
          const offList = await window.CarDB.queryStationCache({ type: 'fuel', keyword: keyword });
          if (offList.length > 0) {
            if (isEdit) {
              this.lastEditFuelPoiSearchResults = offList;
              this.lastEditFuelPoiKeyword = keyword;
              this.renderEditFuelStationDropdown(offList, 'search', keyword);
            } else {
              this.lastPoiSearchResults = offList;
              this.lastPoiKeyword = keyword;
              this.renderFuelStationDropdown(offList, 'search', keyword);
            }
            return;
          }
        }
        if (isEdit) this.closeEditFuelStationDropdown();
        else this.closeFuelStationDropdown();
      }
    } catch (e) {
      if (window.CarDB && typeof window.CarDB.queryStationCache === 'function') {
        const offList = await window.CarDB.queryStationCache({ type: 'fuel', keyword: keyword });
        if (offList.length > 0) {
          if (isEdit) {
            this.lastEditFuelPoiSearchResults = offList;
            this.lastEditFuelPoiKeyword = keyword;
            this.renderEditFuelStationDropdown(offList, 'search', keyword);
          } else {
            this.lastPoiSearchResults = offList;
            this.lastPoiKeyword = keyword;
            this.renderFuelStationDropdown(offList, 'search', keyword);
          }
          return;
        }
      }
      if (isEdit) this.closeEditFuelStationDropdown();
      else this.closeFuelStationDropdown();
    }
  },

  // 充电站 POI 模糊搜索 (支持 elec / edit 目标，支持离线缓存检索)
  async searchElecStationPOI(keyword, target = 'elec') {
    const trimmed = (keyword || '').trim();
    const isEdit = target === 'edit';
    if (!trimmed) {
      if (isEdit) this.closeEditElecStationDropdown();
      else this.closeElecStationDropdown();
      return;
    }

    if (!navigator.onLine && window.CarDB && typeof window.CarDB.queryStationCache === 'function') {
      const offList = await window.CarDB.queryStationCache({ type: 'elec', keyword: trimmed });
      if (offList.length > 0) {
        if (isEdit) {
          this.lastEditElecPoiSearchResults = offList;
          this.lastEditElecPoiKeyword = trimmed;
          this.renderEditElecStationDropdown(offList, 'search', trimmed);
        } else {
          this.lastElecPoiSearchResults = offList;
          this.lastElecPoiKeyword = trimmed;
          this.renderElecStationDropdown(offList, 'search', trimmed);
        }
        return;
      }
    }

    try {
      let locParam = '';
      if (this.userCoords) {
        locParam = `&location=${this.userCoords.lng},${this.userCoords.lat}`;
      }

      // 不限制类型，允许搜索任意 POI（商圈、地标、写字楼、道路等）
      const url = `https://restapi.amap.com/v3/assistant/inputtips?key=${this.amapApiKey}&keywords=${encodeURIComponent(trimmed)}${locParam}`;
      const res = await fetch(url);
      if (!res.ok) {
        await this.searchElecPlaceText(trimmed, target);
        return;
      }
      const data = await res.json();

      // 1. 优先匹配已知权威核准充电站名录 (例如: 理想超级充电站 红星美凯龙)
      const words = trimmed.toLowerCase().split(/\s+/).filter(Boolean);
      const matchedVerified = (this.VERIFIED_CHARGING_STATIONS || []).filter(vs => {
        const vText = (vs.name + ' ' + (vs.address || '')).toLowerCase();
        return words.every(w => {
          if (vText.includes(w)) return true;
          if (w.includes('超级充电站') && (vText.includes('超充站') || vText.includes('充电站'))) return true;
          if (w.includes('超充') && (vText.includes('超级') || vText.includes('充电站'))) return true;
          return false;
        });
      }).map(vs => ({
        id: vs.id,
        name: vs.name,
        district: '',
        address: vs.address,
        location: vs.location,
        sourceBadge: vs.sourceBadge || '官方核准'
      }));

      let validTips = [];
      if (data.status === '1' && Array.isArray(data.tips)) {
        validTips = data.tips.filter(t => t.name && typeof t.name === 'string' && t.name.trim() !== '');
      }

      // 将权威核准匹配站优先前置并去重
      const combinedTips = [...matchedVerified];
      for (const vt of validTips) {
        if (!combinedTips.some(c => c.name === vt.name || (c.location && vt.location && c.location === vt.location))) {
          combinedTips.push(vt);
        }
      }

      if (combinedTips.length > 0) {
        if (isEdit) {
          this.lastEditElecPoiSearchResults = combinedTips;
          this.lastEditElecPoiKeyword = trimmed;
          this.renderEditElecStationDropdown(combinedTips, 'search', trimmed);
        } else {
          this.lastElecPoiSearchResults = combinedTips;
          this.lastElecPoiKeyword = trimmed;
          this.renderElecStationDropdown(combinedTips, 'search', trimmed);
        }
      } else {
        await this.searchElecPlaceText(trimmed, target);
      }
    } catch (err) {
      console.warn('搜索电站 POI 异常:', err);
      await this.searchElecPlaceText(trimmed, target);
    }
  },

  async searchElecPlaceText(keyword, target = 'elec') {
    const isEdit = target === 'edit';
    try {
      let locParam = '';
      if (this.userCoords) {
        locParam = `&location=${this.userCoords.lng},${this.userCoords.lat}&sortrule=distance`;
      }
      const url = `https://restapi.amap.com/v3/place/text?key=${this.amapApiKey}&keywords=${encodeURIComponent(keyword)}&offset=20&page=1${locParam}`;
      const res = await fetch(url);
      if (!res.ok) throw new Error(`Place text response error: ${res.status}`);
      const data = await res.json();
      if (data.status === '1' && Array.isArray(data.pois) && data.pois.length > 0) {
        if (isEdit) {
          this.lastEditElecPoiSearchResults = data.pois;
          this.lastEditElecPoiKeyword = keyword;
          this.renderEditElecStationDropdown(data.pois, 'search', keyword);
        } else {
          this.lastElecPoiSearchResults = data.pois;
          this.lastElecPoiKeyword = keyword;
          this.renderElecStationDropdown(data.pois, 'search', keyword);
        }
      } else {
        if (window.CarDB && typeof window.CarDB.queryStationCache === 'function') {
          const offList = await window.CarDB.queryStationCache({ type: 'elec', keyword: keyword });
          if (offList.length > 0) {
            if (isEdit) {
              this.lastEditElecPoiSearchResults = offList;
              this.lastEditElecPoiKeyword = keyword;
              this.renderEditElecStationDropdown(offList, 'search', keyword);
            } else {
              this.lastElecPoiSearchResults = offList;
              this.lastElecPoiKeyword = keyword;
              this.renderElecStationDropdown(offList, 'search', keyword);
            }
            return;
          }
        }
        if (isEdit) this.closeEditElecStationDropdown();
        else this.closeEditElecStationDropdown();
      }
    } catch (e) {
      if (window.CarDB && typeof window.CarDB.queryStationCache === 'function') {
        const offList = await window.CarDB.queryStationCache({ type: 'elec', keyword: keyword });
        if (offList.length > 0) {
          if (isEdit) {
            this.lastEditElecPoiSearchResults = offList;
            this.lastEditElecPoiKeyword = keyword;
            this.renderEditElecStationDropdown(offList, 'search', keyword);
          } else {
            this.lastElecPoiSearchResults = offList;
            this.lastElecPoiKeyword = keyword;
            this.renderElecStationDropdown(offList, 'search', keyword);
          }
          return;
        }
      }
      if (isEdit) this.closeEditElecStationDropdown();
      else this.closeElecStationDropdown();
    }
  },

  // 充电站品牌识别徽章 (国家电网/特来电/星星充电/蔚来/小鹏/特斯拉/快电/开迈斯等)
  getElecBrandTag(name, typecode = '') {
    if (name.includes('国家电网') || name.includes('国网')) {
      return '<span class="text-[10px] px-1 rounded bg-emerald-50 text-emerald-700 border border-emerald-300 font-medium shrink-0">国家电网</span>';
    } else if (name.includes('特来电')) {
      return '<span class="text-[10px] px-1 rounded bg-blue-50 text-blue-700 border border-blue-300 font-medium shrink-0">特来电</span>';
    } else if (name.includes('星星充电') || name.includes('星星')) {
      return '<span class="text-[10px] px-1 rounded bg-amber-50 text-amber-700 border border-amber-300 font-medium shrink-0">星星充电</span>';
    } else if (name.includes('蔚来') || name.includes('NIO')) {
      return '<span class="text-[10px] px-1 rounded bg-sky-50 text-sky-700 border border-sky-300 font-medium shrink-0">蔚来</span>';
    } else if (name.includes('小鹏') || name.includes('XPENG')) {
      return '<span class="text-[10px] px-1 rounded bg-orange-50 text-orange-700 border border-orange-300 font-medium shrink-0">小鹏</span>';
    } else if (name.includes('特斯拉') || name.includes('Tesla')) {
      return '<span class="text-[10px] px-1 rounded bg-red-50 text-red-700 border border-red-300 font-medium shrink-0">特斯拉</span>';
    } else if (name.includes('快电')) {
      return '<span class="text-[10px] px-1 rounded bg-indigo-50 text-indigo-700 border border-indigo-300 font-medium shrink-0">快电</span>';
    } else if (name.includes('开迈斯') || name.includes('CAMS')) {
      return '<span class="text-[10px] px-1 rounded bg-teal-50 text-teal-700 border border-teal-300 font-medium shrink-0">开迈斯</span>';
    } else {
      return '<span class="text-[10px] px-1 rounded bg-slate-100 text-slate-600 border border-slate-200 font-medium shrink-0">充电站</span>';
    }
  },

  // 判断 POI 是否本身为充电站
  isElecStationPoi(item) {
    const name = item.name || '';
    const typecode = String(item.typecode || item.type || '');
    return typecode.startsWith('0111') || name.includes('充电') || name.includes('超充') || name.includes('特来电') || name.includes('星星充电') || name.includes('国家电网') || name.includes('快充站') || name.includes('换电');
  },

  // 根据选定 POI 的位置坐标检索其周边的所有加油站
  async searchNearbyFuelByPoiLocation(locationStr, poiName, district = '', target = 'fuel') {
    const isEdit = target === 'edit';
    const dropdown = document.getElementById(isEdit ? 'edit-fuel-station-dropdown' : 'fuel-station-dropdown');
    const titleEl = document.getElementById(isEdit ? 'edit-fuel-dropdown-title' : 'fuel-dropdown-title');
    const listEl = document.getElementById(isEdit ? 'edit-fuel-dropdown-list' : 'fuel-dropdown-list');
    if (dropdown && listEl) {
      if (titleEl) titleEl.innerHTML = `📍 正在查找 “<span class="text-amber-600">${poiName}</span>” 周边加油站...`;
      listEl.innerHTML = `<div class="p-6 text-xs text-slate-500 text-center flex flex-col items-center justify-center gap-2">
        <svg class="animate-spin h-5 w-5 text-amber-500" xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24">
          <circle class="opacity-25" cx="12" cy="12" r="10" stroke="currentColor" stroke-width="4"></circle>
          <path class="opacity-75" fill="currentColor" d="M4 12a8 8 0 018-8V0C5.373 0 0 5.373 0 12h4zm2 5.291A7.962 7.962 0 014 12H0c0 3.042 1.135 5.824 3 7.938l3-2.647z"></path>
        </svg>
        <span>正在调用高德+百度双地图检索周边油站 (按距离由近到远排序)...</span>
      </div>`;
      dropdown.classList.remove('hidden');
    }

    try {
      let coords = (typeof locationStr === 'string' && locationStr.includes(',')) ? locationStr.trim() : null;

      if (!coords) {
        const queryTerm = district ? `${district} ${poiName}` : poiName;
        const geoUrl = `https://restapi.amap.com/v3/place/text?key=${this.amapApiKey}&keywords=${encodeURIComponent(queryTerm)}&offset=5&page=1`;
        const geoRes = await fetch(geoUrl);
        if (geoRes.ok) {
          const geoData = await geoRes.json();
          if (geoData.status === '1' && Array.isArray(geoData.pois)) {
            const matchWithLoc = geoData.pois.find(p => typeof p.location === 'string' && p.location.includes(','));
            if (matchWithLoc) {
              coords = matchWithLoc.location.trim();
            }
          }
        }
      }

      if (!coords) {
        this.showToast(`无法获取 “${poiName}” 的精确坐标，建议选择更具体的地名或直接填入`, 'info');
        if (isEdit) {
          if (this.lastEditFuelPoiSearchResults) this.restoreLastEditFuelPoiSearch();
          else this.closeEditFuelStationDropdown();
        } else {
          if (this.lastPoiSearchResults) this.restoreLastPoiSearch();
          else this.closeFuelStationDropdown();
        }
        return;
      }

      const [lng, lat] = coords.split(',').map(s => s.trim());
      let mergedPois = [];

      if (navigator.onLine) {
        const amapPromise = (async () => {
          try {
            const aroundUrl = `https://restapi.amap.com/v3/place/around?key=${this.amapApiKey}&location=${coords}&types=010100&keywords=${encodeURIComponent('加油站')}&radius=15000&sortrule=distance&offset=25&page=1`;
            const res = await fetch(aroundUrl);
            if (res.ok) {
              const data = await res.json();
              if (data.status === '1' && Array.isArray(data.pois)) return data.pois;
            }
          } catch (e) {
            console.warn('高德周边油站请求失败:', e);
          }
          return [];
        })();

        const baiduPromise = this.searchBaiduStations(lat, lng, '加油站', 15000);

        const [amapRes, baiduRes] = await Promise.allSettled([amapPromise, baiduPromise]);
        const gaodePois = (amapRes.status === 'fulfilled' && Array.isArray(amapRes.value)) ? amapRes.value : [];
        const baiduResults = (baiduRes.status === 'fulfilled' && Array.isArray(baiduRes.value)) ? baiduRes.value : [];

        if (gaodePois.length > 0 || baiduResults.length > 0) {
          mergedPois = this.mergeAndDeduplicateStations(gaodePois, baiduResults, lng, lat, 'fuel');
          if (window.CarDB && typeof window.CarDB.saveStationCache === 'function') {
            window.CarDB.saveStationCache(mergedPois, 'fuel');
          }
        }
      }

      // 网络不可用或无结果时无缝降级离线缓存搜索
      if (mergedPois.length === 0 && window.CarDB && typeof window.CarDB.queryStationCache === 'function') {
        mergedPois = await window.CarDB.queryStationCache({ type: 'fuel', lng, lat, maxRadius: 15000, limit: 25 });
      }

      if (mergedPois.length > 0) {
        if (isEdit) {
          this.renderEditFuelStationDropdown(mergedPois, 'nearby_poi', this.lastEditFuelPoiKeyword, poiName);
        } else {
          this.renderFuelStationDropdown(mergedPois, 'nearby_poi', this.lastPoiKeyword, poiName);
        }
      } else {
        if (listEl) {
          const returnAction = isEdit ? 'App.restoreLastEditFuelPoiSearch()' : 'App.restoreLastPoiSearch()';
          const fillAction = isEdit ? `App.selectEditFuelStationFromDropdown('${poiName.replace(/'/g, "\\'")}')` : `App.selectFuelStationFromDropdown('${poiName.replace(/'/g, "\\'")}')`;
          listEl.innerHTML = `
            <div class="p-4 text-xs text-slate-500 text-center space-y-2">
              <p>“${poiName}” 周边 15 公里内未搜索到加油站</p>
              <div class="flex justify-center gap-2">
                <button type="button" onclick="${returnAction}" class="px-2.5 py-1 bg-slate-100 hover:bg-slate-200 text-slate-700 rounded text-xs">← 返回搜索列表</button>
                <button type="button" onclick="${fillAction}" class="px-2.5 py-1 bg-amber-500 hover:bg-amber-600 text-white rounded text-xs font-semibold">直接使用该名称</button>
              </div>
            </div>
          `;
        }
      }
    } catch (err) {
      console.error('检索 POI 周边油站异常:', err);
      if (window.CarDB && typeof window.CarDB.queryStationCache === 'function') {
        const coords = (typeof locationStr === 'string' && locationStr.includes(',')) ? locationStr.trim() : null;
        let cLng = null, cLat = null;
        if (coords) [cLng, cLat] = coords.split(',').map(s => s.trim());
        const offList = await window.CarDB.queryStationCache({ type: 'fuel', lng: cLng, lat: cLat, limit: 25 });
        if (offList.length > 0) {
          if (isEdit) this.renderEditFuelStationDropdown(offList, 'nearby_poi', this.lastEditFuelPoiKeyword, poiName);
          else this.renderFuelStationDropdown(offList, 'nearby_poi', this.lastPoiKeyword, poiName);
          return;
        }
      }
      this.showToast('检索周边油站网络异常', 'error');
    }
  },

  // 根据选定 POI 的位置坐标检索其周边的所有充电站
  async searchNearbyElecByPoiLocation(locationStr, poiName, district = '', target = 'elec') {
    const isEdit = target === 'edit';
    const dropdown = document.getElementById(isEdit ? 'edit-elec-station-dropdown' : 'elec-station-dropdown');
    const titleEl = document.getElementById(isEdit ? 'edit-elec-dropdown-title' : 'elec-dropdown-title');
    const listEl = document.getElementById(isEdit ? 'edit-elec-dropdown-list' : 'elec-dropdown-list');
    if (dropdown && listEl) {
      if (titleEl) titleEl.innerHTML = `📍 正在查找 “<span class="text-emerald-600">${poiName}</span>” 周边充电站...`;
      listEl.innerHTML = `<div class="p-6 text-xs text-slate-500 text-center flex flex-col items-center justify-center gap-2">
        <svg class="animate-spin h-5 w-5 text-emerald-500" xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24">
          <circle class="opacity-25" cx="12" cy="12" r="10" stroke="currentColor" stroke-width="4"></circle>
          <path class="opacity-75" fill="currentColor" d="M4 12a8 8 0 018-8V0C5.373 0 0 5.373 0 12h4zm2 5.291A7.962 7.962 0 014 12H0c0 3.042 1.135 5.824 3 7.938l3-2.647z"></path>
        </svg>
        <span>正在调用高德+百度双地图检索周边电站 (按距离由近到远排序)...</span>
      </div>`;
      dropdown.classList.remove('hidden');
    }

    try {
      let coords = (typeof locationStr === 'string' && locationStr.includes(',')) ? locationStr.trim() : null;

      if (!coords) {
        const queryTerm = district ? `${district} ${poiName}` : poiName;
        const geoUrl = `https://restapi.amap.com/v3/place/text?key=${this.amapApiKey}&keywords=${encodeURIComponent(queryTerm)}&offset=5&page=1`;
        const geoRes = await fetch(geoUrl);
        if (geoRes.ok) {
          const geoData = await geoRes.json();
          if (geoData.status === '1' && Array.isArray(geoData.pois)) {
            const matchWithLoc = geoData.pois.find(p => typeof p.location === 'string' && p.location.includes(','));
            if (matchWithLoc) {
              coords = matchWithLoc.location.trim();
            }
          }
        }
      }

      if (!coords) {
        this.showToast(`无法获取 “${poiName}” 的精确坐标，建议选择更具体的地名或直接填入`, 'info');
        if (isEdit) {
          if (this.lastEditElecPoiSearchResults) this.restoreLastEditElecPoiSearch();
          else this.closeEditElecStationDropdown();
        } else {
          if (this.lastElecPoiSearchResults) this.restoreLastElecPoiSearch();
          else this.closeElecStationDropdown();
        }
        return;
      }

      const [lng, lat] = coords.split(',').map(s => s.trim());
      let mergedPois = [];

      if (navigator.onLine) {
        const amapPromise = (async () => {
          try {
            const aroundUrl = `https://restapi.amap.com/v3/place/around?key=${this.amapApiKey}&location=${coords}&types=011100&keywords=${encodeURIComponent('充电站')}&radius=15000&sortrule=distance&offset=25&page=1`;
            const res = await fetch(aroundUrl);
            if (res.ok) {
              const data = await res.json();
              if (data.status === '1' && Array.isArray(data.pois)) return data.pois;
            }
          } catch (e) {
            console.warn('高德周边电站请求失败:', e);
          }
          return [];
        })();

        const baiduChargingPromise = this.searchBaiduStations(lat, lng, '充电站', 15000, 20);
        const baiduSuperPromise = this.searchBaiduStations(lat, lng, '超充站', 15000, 20);

        const [amapRes, baiduRes, baiduSuperRes] = await Promise.allSettled([
          amapPromise,
          baiduChargingPromise,
          baiduSuperPromise
        ]);
        const gaodePois = (amapRes.status === 'fulfilled' && Array.isArray(amapRes.value)) ? amapRes.value : [];
        const bList1 = (baiduRes.status === 'fulfilled' && Array.isArray(baiduRes.value)) ? baiduRes.value : [];
        const bList2 = (baiduSuperRes.status === 'fulfilled' && Array.isArray(baiduSuperRes.value)) ? baiduSuperRes.value : [];

        const baiduResults = [...bList1];
        const seenBaiduKeys = new Set(bList1.map(p => p.uid || p.name));
        for (const bp of bList2) {
          const key = bp.uid || bp.name;
          if (!seenBaiduKeys.has(key)) {
            seenBaiduKeys.add(key);
            baiduResults.push(bp);
          }
        }

        if (gaodePois.length > 0 || baiduResults.length > 0) {
          mergedPois = this.mergeAndDeduplicateStations(gaodePois, baiduResults, lng, lat, 'elec');
          // 若搜索地标包含明确品牌（如理想/特斯拉），优先将该品牌的充电桩置顶呈现
          const targetBrand = this.getStationBrandKey(poiName);
          if (targetBrand !== 'other') {
            mergedPois.sort((a, b) => {
              const aMatch = this.getStationBrandKey(a.name) === targetBrand ? 1 : 0;
              const bMatch = this.getStationBrandKey(b.name) === targetBrand ? 1 : 0;
              if (aMatch !== bMatch) return bMatch - aMatch;
              const da = a.distance != null ? a.distance : 9999999;
              const db = b.distance != null ? b.distance : 9999999;
              return da - db;
            });
          }
          if (window.CarDB && typeof window.CarDB.saveStationCache === 'function') {
            window.CarDB.saveStationCache(mergedPois, 'elec');
          }
        }
      }

      // 网络不可用或无结果时无缝降级离线缓存搜索
      if (mergedPois.length === 0 && window.CarDB && typeof window.CarDB.queryStationCache === 'function') {
        mergedPois = await window.CarDB.queryStationCache({ type: 'elec', lng, lat, maxRadius: 15000, limit: 25 });
      }

      if (mergedPois.length > 0) {
        if (isEdit) {
          this.renderEditElecStationDropdown(mergedPois, 'nearby_poi', this.lastEditElecPoiKeyword, poiName);
        } else {
          this.renderElecStationDropdown(mergedPois, 'nearby_poi', this.lastElecPoiKeyword, poiName);
        }
      } else {
        if (listEl) {
          const returnAction = isEdit ? 'App.restoreLastEditElecPoiSearch()' : 'App.restoreLastElecPoiSearch()';
          const fillAction = isEdit ? `App.selectEditElecStationFromDropdown('${poiName.replace(/'/g, "\\'")}')` : `App.selectElecStationFromDropdown('${poiName.replace(/'/g, "\\'")}')`;
          listEl.innerHTML = `
            <div class="p-4 text-xs text-slate-500 text-center space-y-2">
              <p>“${poiName}” 周边 15 公里内未搜索到充电站</p>
              <div class="flex justify-center gap-2">
                <button type="button" onclick="${returnAction}" class="px-2.5 py-1 bg-slate-100 hover:bg-slate-200 text-slate-700 rounded text-xs">← 返回搜索列表</button>
                <button type="button" onclick="${fillAction}" class="px-2.5 py-1 bg-emerald-600 hover:bg-emerald-700 text-white rounded text-xs font-semibold">直接使用该名称</button>
              </div>
            </div>
          `;
        }
      }
    } catch (err) {
      console.error('检索 POI 周边电站异常:', err);
      if (window.CarDB && typeof window.CarDB.queryStationCache === 'function') {
        const coords = (typeof locationStr === 'string' && locationStr.includes(',')) ? locationStr.trim() : null;
        let cLng = null, cLat = null;
        if (coords) [cLng, cLat] = coords.split(',').map(s => s.trim());
        const offList = await window.CarDB.queryStationCache({ type: 'elec', lng: cLng, lat: cLat, limit: 25 });
        if (offList.length > 0) {
          if (isEdit) this.renderEditElecStationDropdown(offList, 'nearby_poi', this.lastEditElecPoiKeyword, poiName);
          else this.renderElecStationDropdown(offList, 'nearby_poi', this.lastElecPoiKeyword, poiName);
          return;
        }
      }
      this.showToast('检索周边电站网络异常', 'error');
    }
  },

  // 返回上一次地标搜索列表
  restoreLastPoiSearch() {
    if (this.lastPoiSearchResults && this.lastPoiSearchResults.length > 0) {
      this.renderFuelStationDropdown(this.lastPoiSearchResults, 'search', this.lastPoiKeyword);
    } else {
      const input = document.getElementById('fuel-station');
      const val = input ? input.value.trim() : '';
      if (val) {
        this.searchFuelStationPOI(val, 'fuel');
      } else {
        this.closeFuelStationDropdown();
      }
    }
  },

  restoreLastElecPoiSearch() {
    if (this.lastElecPoiSearchResults && this.lastElecPoiSearchResults.length > 0) {
      this.renderElecStationDropdown(this.lastElecPoiSearchResults, 'search', this.lastElecPoiKeyword);
    } else {
      const input = document.getElementById('elec-station');
      const val = input ? input.value.trim() : '';
      if (val) {
        this.searchElecStationPOI(val, 'elec');
      } else {
        this.closeElecStationDropdown();
      }
    }
  },

  restoreLastEditFuelPoiSearch() {
    if (this.lastEditFuelPoiSearchResults && this.lastEditFuelPoiSearchResults.length > 0) {
      this.renderEditFuelStationDropdown(this.lastEditFuelPoiSearchResults, 'search', this.lastEditFuelPoiKeyword);
    } else {
      const input = document.getElementById('edit-fuel-station');
      const val = input ? input.value.trim() : '';
      if (val) {
        this.searchFuelStationPOI(val, 'edit');
      } else {
        this.closeEditFuelStationDropdown();
      }
    }
  },

  restoreLastEditElecPoiSearch() {
    if (this.lastEditElecPoiSearchResults && this.lastEditElecPoiSearchResults.length > 0) {
      this.renderEditElecStationDropdown(this.lastEditElecPoiSearchResults, 'search', this.lastEditElecPoiKeyword);
    } else {
      const input = document.getElementById('edit-elec-station');
      const val = input ? input.value.trim() : '';
      if (val) {
        this.searchElecStationPOI(val, 'edit');
      } else {
        this.closeEditElecStationDropdown();
      }
    }
  },

  // 渲染油耗录入油站浮层下拉菜单
  renderFuelStationDropdown(items, mode = 'nearby', keyword = '', targetPoiName = '', source = 'gps', cityName = '') {
    const dropdown = document.getElementById('fuel-station-dropdown');
    const titleEl = document.getElementById('fuel-dropdown-title');
    const listEl = document.getElementById('fuel-dropdown-list');
    if (!dropdown || !listEl) return;

    if (mode === 'nearby') {
      const isOff = (items && items.some(it => it.isOffline)) || source === 'offline';
      const locTag = isOff ? '离线搜索' : (source === 'ip' ? (cityName ? `${cityName}网络定位` : '网络定位') : 'GPS定位');
      if (titleEl) titleEl.innerHTML = `📍 附近加油站推荐 (${locTag} · 按距离由近到远)`;
    } else if (mode === 'nearby_poi') {
      if (titleEl) {
        titleEl.innerHTML = `
          <div class="flex items-center justify-between w-full">
            <span class="truncate">📍 <strong class="text-amber-700">${targetPoiName}</strong> 周边加油站 (由近到远)</span>
            <button type="button" onclick="App.restoreLastPoiSearch()" class="ml-2 text-blue-600 hover:text-blue-800 text-[11px] font-semibold flex items-center gap-0.5 shrink-0">
              ← 返回地标列表
            </button>
          </div>
        `;
      }
    } else {
      if (titleEl) titleEl.innerHTML = `🔍 匹配 “<span class="text-amber-600">${keyword}</span>” 的地标与油站 (点击查周边或选择)`;
    }

    if (!items || items.length === 0) {
      listEl.innerHTML = `<div class="p-3 text-xs text-slate-400 text-center">暂无符合条件的地点或油站</div>`;
      dropdown.classList.remove('hidden');
      return;
    }

    listEl.innerHTML = items.map(item => {
      const name = item.name || '';
      let address = item.address || '';
      if (Array.isArray(address) && address.length === 0) address = '';
      let district = item.district || '';
      if (Array.isArray(district) && district.length === 0) district = '';
      const fullAddr = [district, address].filter(Boolean).join(' ') || '详细地址暂无';

      const typecode = String(item.typecode || item.type || '');
      const isGasStation = typecode.startsWith('0101') || name.includes('加气') || name.includes('加油') || name.includes('油站') || name.includes('石化') || name.includes('石油') || name.includes('壳牌') || name.includes('海湾') || name.includes('加德士') || name.includes('中化');

      let distBadge = '';
      if (item.distance && !isNaN(parseInt(item.distance))) {
        const d = parseInt(item.distance);
        const dText = d >= 1000 ? `${(d / 1000).toFixed(1)} km` : `${d} 米`;
        distBadge = `<span class="shrink-0 px-1.5 py-0.5 rounded text-[10px] font-bold bg-amber-50 text-amber-700 border border-amber-200">距 ${dText}</span>`;
      }
      if (item.isOffline) {
        distBadge += `<span class="shrink-0 px-1 py-0.5 rounded text-[9px] font-medium bg-slate-100 text-slate-500 border border-slate-200 ml-1">离线</span>`;
      }

      const safeName = name.replace(/"/g, '&quot;').replace(/'/g, "\\'");
      const safeDistrict = district.replace(/"/g, '&quot;').replace(/'/g, "\\'");
      const locStr = (typeof item.location === 'string' && item.location.includes(',')) ? item.location : '';

      if (mode === 'nearby_poi' || mode === 'nearby' || (mode === 'search' && isGasStation)) {
        let brandTag = '';
        if (name.includes('石化') || typecode.includes('石化')) {
          brandTag = '<span class="text-[10px] px-1 rounded bg-rose-50 text-rose-600 border border-rose-200 font-medium shrink-0">中石化</span>';
        } else if (name.includes('石油') || typecode.includes('石油')) {
          brandTag = '<span class="text-[10px] px-1 rounded bg-red-50 text-red-600 border border-red-200 font-medium shrink-0">中石油</span>';
        } else if (name.includes('中化')) {
          brandTag = '<span class="text-[10px] px-1 rounded bg-blue-50 text-blue-600 border border-blue-200 font-medium shrink-0">中化</span>';
        } else if (name.includes('海湾')) {
          brandTag = '<span class="text-[10px] px-1 rounded bg-orange-50 text-orange-600 border border-orange-200 font-medium shrink-0">海湾</span>';
        } else if (name.includes('壳牌')) {
          brandTag = '<span class="text-[10px] px-1 rounded bg-yellow-50 text-yellow-700 border border-yellow-300 font-medium shrink-0">壳牌</span>';
        } else if (name.includes('加德士')) {
          brandTag = '<span class="text-[10px] px-1 rounded bg-emerald-50 text-emerald-700 border border-emerald-300 font-medium shrink-0">加德士</span>';
        } else {
          brandTag = '<span class="text-[10px] px-1 rounded bg-slate-100 text-slate-600 border border-slate-200 font-medium shrink-0">油站</span>';
        }

        return `
          <button type="button" onclick="App.selectFuelStationFromDropdown('${safeName}')"
                  class="w-full text-left px-3 py-2 hover:bg-amber-50/70 transition flex items-start justify-between gap-3 group">
            <div class="min-w-0 flex-1">
              <div class="flex items-center gap-1.5 flex-wrap">
                ${brandTag}
                <span class="font-semibold text-xs text-slate-800 group-hover:text-amber-900 transition">${name}</span>
              </div>
              <div class="text-[11px] text-slate-400 mt-0.5 truncate">${fullAddr}</div>
            </div>
            ${distBadge}
          </button>
        `;
      }

      return `
        <div class="w-full text-left px-3 py-2.5 hover:bg-slate-50 transition flex items-center justify-between gap-2 group">
          <div class="min-w-0 flex-1 cursor-pointer" onclick="App.searchNearbyFuelByPoiLocation('${locStr}', '${safeName}', '${safeDistrict}', 'fuel')">
            <div class="flex items-center gap-1.5 flex-wrap">
              <span class="text-[10px] px-1.5 py-0.2 rounded bg-indigo-50 text-indigo-700 border border-indigo-200 font-medium shrink-0">📍 地标</span>
              <span class="font-semibold text-xs text-slate-800 group-hover:text-indigo-900 transition">${name}</span>
            </div>
            <div class="text-[11px] text-slate-400 mt-0.5 truncate">${fullAddr}</div>
          </div>
          <div class="flex items-center gap-1 shrink-0">
            <button type="button" onclick="App.searchNearbyFuelByPoiLocation('${locStr}', '${safeName}', '${safeDistrict}', 'fuel')"
                    class="px-2 py-1 rounded bg-amber-50 hover:bg-amber-100 text-amber-700 border border-amber-200 text-[11px] font-semibold flex items-center gap-1 shadow-xs transition" title="以此地标为中心检索周边加油站">
              <span>⚡ 查周边油站</span>
              <span class="text-xs">➔</span>
            </button>
            <button type="button" onclick="App.selectFuelStationFromDropdown('${safeName}')"
                    class="p-1 text-slate-400 hover:text-slate-700 hover:bg-slate-100 rounded text-[11px]" title="直接使用该名称">
              填入
            </button>
          </div>
        </div>
      `;
    }).join('');

    dropdown.classList.remove('hidden');
  },

  // 渲染电耗录入充电站浮层下拉菜单
  renderElecStationDropdown(items, mode = 'nearby', keyword = '', targetPoiName = '', source = 'gps', cityName = '') {
    const dropdown = document.getElementById('elec-station-dropdown');
    const titleEl = document.getElementById('elec-dropdown-title');
    const listEl = document.getElementById('elec-dropdown-list');
    if (!dropdown || !listEl) return;

    if (mode === 'nearby') {
      const isOff = (items && items.some(it => it.isOffline)) || source === 'offline';
      const locTag = isOff ? '离线搜索' : (source === 'ip' ? (cityName ? `${cityName}网络定位` : '网络定位') : 'GPS定位');
      if (titleEl) titleEl.innerHTML = `📍 附近充电站推荐 (${locTag} · 按距离由近到远)`;
    } else if (mode === 'nearby_poi') {
      if (titleEl) {
        titleEl.innerHTML = `
          <div class="flex items-center justify-between w-full">
            <span class="truncate">📍 <strong class="text-emerald-700">${targetPoiName}</strong> 周边充电站 (由近到远)</span>
            <button type="button" onclick="App.restoreLastElecPoiSearch()" class="ml-2 text-blue-600 hover:text-blue-800 text-[11px] font-semibold flex items-center gap-0.5 shrink-0">
              ← 返回地标列表
            </button>
          </div>
        `;
      }
    } else {
      if (titleEl) titleEl.innerHTML = `🔍 匹配 “<span class="text-emerald-600">${keyword}</span>” 的地标与电站 (点击查周边或选择)`;
    }

    if (!items || items.length === 0) {
      listEl.innerHTML = `<div class="p-3 text-xs text-slate-400 text-center">暂无符合条件的地点或电站</div>`;
      dropdown.classList.remove('hidden');
      return;
    }

    listEl.innerHTML = items.map(item => {
      const name = item.name || '';
      let address = item.address || '';
      if (Array.isArray(address) && address.length === 0) address = '';
      let district = item.district || '';
      if (Array.isArray(district) && district.length === 0) district = '';
      const fullAddr = [district, address].filter(Boolean).join(' ') || '详细地址暂无';

      const typecode = String(item.typecode || item.type || '');
      const isElecStation = this.isElecStationPoi(item);

      let distBadge = '';
      if (item.distance && !isNaN(parseInt(item.distance))) {
        const d = parseInt(item.distance);
        const dText = d >= 1000 ? `${(d / 1000).toFixed(1)} km` : `${d} 米`;
        distBadge = `<span class="shrink-0 px-1.5 py-0.5 rounded text-[10px] font-bold bg-emerald-50 text-emerald-700 border border-emerald-200">距 ${dText}</span>`;
      }
      if (item.isOffline) {
        distBadge += `<span class="shrink-0 px-1 py-0.5 rounded text-[9px] font-medium bg-slate-100 text-slate-500 border border-slate-200 ml-1">离线</span>`;
      }

      const safeName = name.replace(/"/g, '&quot;').replace(/'/g, "\\'");
      const safeDistrict = district.replace(/"/g, '&quot;').replace(/'/g, "\\'");
      const locStr = (typeof item.location === 'string' && item.location.includes(',')) ? item.location : '';

      if (mode === 'nearby_poi' || mode === 'nearby' || (mode === 'search' && isElecStation)) {
        const brandTag = this.getElecBrandTag(name, typecode);
        return `
          <button type="button" onclick="App.selectElecStationFromDropdown('${safeName}')"
                  class="w-full text-left px-3 py-2 hover:bg-emerald-50/70 transition flex items-start justify-between gap-3 group">
            <div class="min-w-0 flex-1">
              <div class="flex items-center gap-1.5 flex-wrap">
                ${brandTag}
                <span class="font-semibold text-xs text-slate-800 group-hover:text-emerald-900 transition">${name}</span>
              </div>
              <div class="text-[11px] text-slate-400 mt-0.5 truncate">${fullAddr}</div>
            </div>
            ${distBadge}
          </button>
        `;
      }

      return `
        <div class="w-full text-left px-3 py-2.5 hover:bg-slate-50 transition flex items-center justify-between gap-2 group">
          <div class="min-w-0 flex-1 cursor-pointer" onclick="App.searchNearbyElecByPoiLocation('${locStr}', '${safeName}', '${safeDistrict}', 'elec')">
            <div class="flex items-center gap-1.5 flex-wrap">
              <span class="text-[10px] px-1.5 py-0.2 rounded bg-indigo-50 text-indigo-700 border border-indigo-200 font-medium shrink-0">📍 地标</span>
              <span class="font-semibold text-xs text-slate-800 group-hover:text-indigo-900 transition">${name}</span>
            </div>
            <div class="text-[11px] text-slate-400 mt-0.5 truncate">${fullAddr}</div>
          </div>
          <div class="flex items-center gap-1 shrink-0">
            <button type="button" onclick="App.searchNearbyElecByPoiLocation('${locStr}', '${safeName}', '${safeDistrict}', 'elec')"
                    class="px-2 py-1 rounded bg-emerald-50 hover:bg-emerald-100 text-emerald-700 border border-emerald-200 text-[11px] font-semibold flex items-center gap-1 shadow-xs transition" title="以此地标为中心检索周边充电站">
              <span>⚡ 查周边电站</span>
              <span class="text-xs">➔</span>
            </button>
            <button type="button" onclick="App.selectElecStationFromDropdown('${safeName}')"
                    class="p-1 text-slate-400 hover:text-slate-700 hover:bg-slate-100 rounded text-[11px]" title="直接使用该名称">
              填入
            </button>
          </div>
        </div>
      `;
    }).join('');

    dropdown.classList.remove('hidden');
  },

  // 渲染历史编辑弹窗中的油站浮层下拉菜单
  renderEditFuelStationDropdown(items, mode = 'nearby', keyword = '', targetPoiName = '', source = 'gps', cityName = '') {
    const dropdown = document.getElementById('edit-fuel-station-dropdown');
    const titleEl = document.getElementById('edit-fuel-dropdown-title');
    const listEl = document.getElementById('edit-fuel-dropdown-list');
    if (!dropdown || !listEl) return;

    if (mode === 'nearby') {
      const isOff = (items && items.some(it => it.isOffline)) || source === 'offline';
      const locTag = isOff ? '离线搜索' : (source === 'ip' ? (cityName ? `${cityName}网络定位` : '网络定位') : 'GPS定位');
      if (titleEl) titleEl.innerHTML = `📍 附近加油站推荐 (${locTag} · 按距离由近到远)`;
    } else if (mode === 'nearby_poi') {
      if (titleEl) {
        titleEl.innerHTML = `
          <div class="flex items-center justify-between w-full">
            <span class="truncate">📍 <strong class="text-amber-700">${targetPoiName}</strong> 周边加油站 (由近到远)</span>
            <button type="button" onclick="App.restoreLastEditFuelPoiSearch()" class="ml-2 text-blue-600 hover:text-blue-800 text-[11px] font-semibold flex items-center gap-0.5 shrink-0">
              ← 返回地标列表
            </button>
          </div>
        `;
      }
    } else {
      if (titleEl) titleEl.innerHTML = `🔍 匹配 “<span class="text-amber-600">${keyword}</span>” 的地标与油站`;
    }

    if (!items || items.length === 0) {
      listEl.innerHTML = `<div class="p-3 text-xs text-slate-400 text-center">暂无符合条件的地点或油站</div>`;
      dropdown.classList.remove('hidden');
      return;
    }

    listEl.innerHTML = items.map(item => {
      const name = item.name || '';
      let address = item.address || '';
      if (Array.isArray(address) && address.length === 0) address = '';
      let district = item.district || '';
      if (Array.isArray(district) && district.length === 0) district = '';
      const fullAddr = [district, address].filter(Boolean).join(' ') || '详细地址暂无';

      const typecode = String(item.typecode || item.type || '');
      const isGasStation = typecode.startsWith('0101') || name.includes('加气') || name.includes('加油') || name.includes('油站') || name.includes('石化') || name.includes('石油') || name.includes('壳牌') || name.includes('海湾') || name.includes('加德士') || name.includes('中化');

      let distBadge = '';
      if (item.distance && !isNaN(parseInt(item.distance))) {
        const d = parseInt(item.distance);
        const dText = d >= 1000 ? `${(d / 1000).toFixed(1)} km` : `${d} 米`;
        distBadge = `<span class="shrink-0 px-1.5 py-0.5 rounded text-[10px] font-bold bg-amber-50 text-amber-700 border border-amber-200">距 ${dText}</span>`;
      }
      if (item.isOffline) {
        distBadge += `<span class="shrink-0 px-1 py-0.5 rounded text-[9px] font-medium bg-slate-100 text-slate-500 border border-slate-200 ml-1">离线</span>`;
      }

      const safeName = name.replace(/"/g, '&quot;').replace(/'/g, "\\'");
      const safeDistrict = district.replace(/"/g, '&quot;').replace(/'/g, "\\'");
      const locStr = (typeof item.location === 'string' && item.location.includes(',')) ? item.location : '';

      if (mode === 'nearby_poi' || mode === 'nearby' || (mode === 'search' && isGasStation)) {
        let brandTag = '';
        if (name.includes('石化') || typecode.includes('石化')) {
          brandTag = '<span class="text-[10px] px-1 rounded bg-rose-50 text-rose-600 border border-rose-200 font-medium shrink-0">中石化</span>';
        } else if (name.includes('石油') || typecode.includes('石油')) {
          brandTag = '<span class="text-[10px] px-1 rounded bg-red-50 text-red-600 border border-red-200 font-medium shrink-0">中石油</span>';
        } else if (name.includes('中化')) {
          brandTag = '<span class="text-[10px] px-1 rounded bg-blue-50 text-blue-600 border border-blue-200 font-medium shrink-0">中化</span>';
        } else if (name.includes('海湾')) {
          brandTag = '<span class="text-[10px] px-1 rounded bg-orange-50 text-orange-600 border border-orange-200 font-medium shrink-0">海湾</span>';
        } else if (name.includes('壳牌')) {
          brandTag = '<span class="text-[10px] px-1 rounded bg-yellow-50 text-yellow-700 border border-yellow-300 font-medium shrink-0">壳牌</span>';
        } else if (name.includes('加德士')) {
          brandTag = '<span class="text-[10px] px-1 rounded bg-emerald-50 text-emerald-700 border border-emerald-300 font-medium shrink-0">加德士</span>';
        } else {
          brandTag = '<span class="text-[10px] px-1 rounded bg-slate-100 text-slate-600 border border-slate-200 font-medium shrink-0">油站</span>';
        }

        return `
          <button type="button" onclick="App.selectEditFuelStationFromDropdown('${safeName}')"
                  class="w-full text-left px-3 py-2 hover:bg-amber-50/70 transition flex items-start justify-between gap-3 group">
            <div class="min-w-0 flex-1">
              <div class="flex items-center gap-1.5 flex-wrap">
                ${brandTag}
                <span class="font-semibold text-xs text-slate-800 group-hover:text-amber-900 transition">${name}</span>
              </div>
              <div class="text-[11px] text-slate-400 mt-0.5 truncate">${fullAddr}</div>
            </div>
            ${distBadge}
          </button>
        `;
      }

      return `
        <div class="w-full text-left px-3 py-2.5 hover:bg-slate-50 transition flex items-center justify-between gap-2 group">
          <div class="min-w-0 flex-1 cursor-pointer" onclick="App.searchNearbyFuelByPoiLocation('${locStr}', '${safeName}', '${safeDistrict}', 'edit')">
            <div class="flex items-center gap-1.5 flex-wrap">
              <span class="text-[10px] px-1.5 py-0.2 rounded bg-indigo-50 text-indigo-700 border border-indigo-200 font-medium shrink-0">📍 地标</span>
              <span class="font-semibold text-xs text-slate-800 group-hover:text-indigo-900 transition">${name}</span>
            </div>
            <div class="text-[11px] text-slate-400 mt-0.5 truncate">${fullAddr}</div>
          </div>
          <div class="flex items-center gap-1 shrink-0">
            <button type="button" onclick="App.searchNearbyFuelByPoiLocation('${locStr}', '${safeName}', '${safeDistrict}', 'edit')"
                    class="px-2 py-1 rounded bg-amber-50 hover:bg-amber-100 text-amber-700 border border-amber-200 text-[11px] font-semibold flex items-center gap-1 shadow-xs transition" title="以此地标为中心检索周边加油站">
              <span>⚡ 查周边油站</span>
              <span class="text-xs">➔</span>
            </button>
            <button type="button" onclick="App.selectEditFuelStationFromDropdown('${safeName}')"
                    class="p-1 text-slate-400 hover:text-slate-700 hover:bg-slate-100 rounded text-[11px]" title="直接使用该名称">
              填入
            </button>
          </div>
        </div>
      `;
    }).join('');

    dropdown.classList.remove('hidden');
  },

  // 渲染历史编辑弹窗中的电站浮层下拉菜单
  renderEditElecStationDropdown(items, mode = 'nearby', keyword = '', targetPoiName = '', source = 'gps', cityName = '') {
    const dropdown = document.getElementById('edit-elec-station-dropdown');
    const titleEl = document.getElementById('edit-elec-dropdown-title');
    const listEl = document.getElementById('edit-elec-dropdown-list');
    if (!dropdown || !listEl) return;

    if (mode === 'nearby') {
      const isOff = (items && items.some(it => it.isOffline)) || source === 'offline';
      const locTag = isOff ? '离线搜索' : (source === 'ip' ? (cityName ? `${cityName}网络定位` : '网络定位') : 'GPS定位');
      if (titleEl) titleEl.innerHTML = `📍 附近充电站推荐 (${locTag} · 按距离由近到远)`;
    } else if (mode === 'nearby_poi') {
      if (titleEl) {
        titleEl.innerHTML = `
          <div class="flex items-center justify-between w-full">
            <span class="truncate">📍 <strong class="text-emerald-700">${targetPoiName}</strong> 周边充电站 (由近到远)</span>
            <button type="button" onclick="App.restoreLastEditElecPoiSearch()" class="ml-2 text-blue-600 hover:text-blue-800 text-[11px] font-semibold flex items-center gap-0.5 shrink-0">
              ← 返回地标列表
            </button>
          </div>
        `;
      }
    } else {
      if (titleEl) titleEl.innerHTML = `🔍 匹配 “<span class="text-emerald-600">${keyword}</span>” 的地标与电站`;
    }

    if (!items || items.length === 0) {
      listEl.innerHTML = `<div class="p-3 text-xs text-slate-400 text-center">暂无符合条件的地点或电站</div>`;
      dropdown.classList.remove('hidden');
      return;
    }

    listEl.innerHTML = items.map(item => {
      const name = item.name || '';
      let address = item.address || '';
      if (Array.isArray(address) && address.length === 0) address = '';
      let district = item.district || '';
      if (Array.isArray(district) && district.length === 0) district = '';
      const fullAddr = [district, address].filter(Boolean).join(' ') || '详细地址暂无';

      const typecode = String(item.typecode || item.type || '');
      const isElecStation = this.isElecStationPoi(item);

      let distBadge = '';
      if (item.distance && !isNaN(parseInt(item.distance))) {
        const d = parseInt(item.distance);
        const dText = d >= 1000 ? `${(d / 1000).toFixed(1)} km` : `${d} 米`;
        distBadge = `<span class="shrink-0 px-1.5 py-0.5 rounded text-[10px] font-bold bg-emerald-50 text-emerald-700 border border-emerald-200">距 ${dText}</span>`;
      }
      if (item.isOffline) {
        distBadge += `<span class="shrink-0 px-1 py-0.5 rounded text-[9px] font-medium bg-slate-100 text-slate-500 border border-slate-200 ml-1">离线</span>`;
      }

      const safeName = name.replace(/"/g, '&quot;').replace(/'/g, "\\'");
      const safeDistrict = district.replace(/"/g, '&quot;').replace(/'/g, "\\'");
      const locStr = (typeof item.location === 'string' && item.location.includes(',')) ? item.location : '';

      if (mode === 'nearby_poi' || mode === 'nearby' || (mode === 'search' && isElecStation)) {
        const brandTag = this.getElecBrandTag(name, typecode);
        return `
          <button type="button" onclick="App.selectEditElecStationFromDropdown('${safeName}')"
                  class="w-full text-left px-3 py-2 hover:bg-emerald-50/70 transition flex items-start justify-between gap-3 group">
            <div class="min-w-0 flex-1">
              <div class="flex items-center gap-1.5 flex-wrap">
                ${brandTag}
                <span class="font-semibold text-xs text-slate-800 group-hover:text-emerald-900 transition">${name}</span>
              </div>
              <div class="text-[11px] text-slate-400 mt-0.5 truncate">${fullAddr}</div>
            </div>
            ${distBadge}
          </button>
        `;
      }

      return `
        <div class="w-full text-left px-3 py-2.5 hover:bg-slate-50 transition flex items-center justify-between gap-2 group">
          <div class="min-w-0 flex-1 cursor-pointer" onclick="App.searchNearbyElecByPoiLocation('${locStr}', '${safeName}', '${safeDistrict}', 'edit')">
            <div class="flex items-center gap-1.5 flex-wrap">
              <span class="text-[10px] px-1.5 py-0.2 rounded bg-indigo-50 text-indigo-700 border border-indigo-200 font-medium shrink-0">📍 地标</span>
              <span class="font-semibold text-xs text-slate-800 group-hover:text-indigo-900 transition">${name}</span>
            </div>
            <div class="text-[11px] text-slate-400 mt-0.5 truncate">${fullAddr}</div>
          </div>
          <div class="flex items-center gap-1 shrink-0">
            <button type="button" onclick="App.searchNearbyElecByPoiLocation('${locStr}', '${safeName}', '${safeDistrict}', 'edit')"
                    class="px-2 py-1 rounded bg-emerald-50 hover:bg-emerald-100 text-emerald-700 border border-emerald-200 text-[11px] font-semibold flex items-center gap-1 shadow-xs transition" title="以此地标为中心检索周边充电站">
              <span>⚡ 查周边电站</span>
              <span class="text-xs">➔</span>
            </button>
            <button type="button" onclick="App.selectEditElecStationFromDropdown('${safeName}')"
                    class="p-1 text-slate-400 hover:text-slate-700 hover:bg-slate-100 rounded text-[11px]" title="直接使用该名称">
              填入
            </button>
          </div>
        </div>
      `;
    }).join('');

    dropdown.classList.remove('hidden');
  },

  selectFuelStationFromDropdown(stationName) {
    const input = document.getElementById('fuel-station');
    if (input) {
      input.value = stationName;
      const clearBtn = document.getElementById('fuel-station-clear-btn');
      if (clearBtn) clearBtn.classList.remove('hidden');
    }
    this.renderFuelStationPills();
    this.closeFuelStationDropdown();
    this.showToast(`已选择油站：${stationName}`, 'success');
  },

  selectElecStationFromDropdown(stationName) {
    const input = document.getElementById('elec-station');
    if (input) {
      input.value = stationName;
      const clearBtn = document.getElementById('elec-station-clear-btn');
      if (clearBtn) clearBtn.classList.remove('hidden');
    }
    this.renderElecStationPills();
    this.closeElecStationDropdown();
    this.showToast(`已选择电站：${stationName}`, 'success');
  },

  selectEditFuelStationFromDropdown(stationName) {
    const input = document.getElementById('edit-fuel-station');
    if (input) {
      input.value = stationName;
      const clearBtn = document.getElementById('edit-fuel-station-clear-btn');
      if (clearBtn) clearBtn.classList.remove('hidden');
    }
    this.renderEditFuelStationPills();
    this.closeEditFuelStationDropdown();
    this.showToast(`已选择油站：${stationName}`, 'success');
  },

  selectEditElecStationFromDropdown(stationName) {
    const input = document.getElementById('edit-elec-station');
    if (input) {
      input.value = stationName;
      const clearBtn = document.getElementById('edit-elec-station-clear-btn');
      if (clearBtn) clearBtn.classList.remove('hidden');
    }
    this.renderEditElecStationPills();
    this.closeEditElecStationDropdown();
    this.showToast(`已选择电站：${stationName}`, 'success');
  },

  closeFuelStationDropdown() {
    const dropdown = document.getElementById('fuel-station-dropdown');
    if (dropdown) dropdown.classList.add('hidden');
  },

  closeElecStationDropdown() {
    const dropdown = document.getElementById('elec-station-dropdown');
    if (dropdown) dropdown.classList.add('hidden');
  },

  closeEditFuelStationDropdown() {
    const dropdown = document.getElementById('edit-fuel-station-dropdown');
    if (dropdown) dropdown.classList.add('hidden');
  },

  closeEditElecStationDropdown() {
    const dropdown = document.getElementById('edit-elec-station-dropdown');
    if (dropdown) dropdown.classList.add('hidden');
  },

  clearFuelStationInput() {
    const input = document.getElementById('fuel-station');
    if (input) {
      input.value = '';
    }
    const clearBtn = document.getElementById('fuel-station-clear-btn');
    if (clearBtn) clearBtn.classList.add('hidden');
    this.renderFuelStationPills();
    if (this.nearbyFuelStationsCache && this.nearbyFuelStationsCache.length > 0) {
      this.renderFuelStationDropdown(this.nearbyFuelStationsCache, 'nearby', '', '', this.userCoords?.source || 'gps', this.userCoords?.city || '');
    } else {
      this.locateNearbyFuelStations(true);
    }
    if (input) input.focus();
  },

  clearElecStationInput() {
    const input = document.getElementById('elec-station');
    if (input) {
      input.value = '';
    }
    const clearBtn = document.getElementById('elec-station-clear-btn');
    if (clearBtn) clearBtn.classList.add('hidden');
    this.renderElecStationPills();
    if (this.nearbyElecStationsCache && this.nearbyElecStationsCache.length > 0) {
      this.renderElecStationDropdown(this.nearbyElecStationsCache, 'nearby', '', '', this.userCoords?.source || 'gps', this.userCoords?.city || '');
    } else {
      this.locateNearbyElecStations(true);
    }
    if (input) input.focus();
  },

  clearEditFuelStationInput() {
    const input = document.getElementById('edit-fuel-station');
    if (input) {
      input.value = '';
    }
    const clearBtn = document.getElementById('edit-fuel-station-clear-btn');
    if (clearBtn) clearBtn.classList.add('hidden');
    this.renderEditFuelStationPills();
    if (this.nearbyFuelStationsCache && this.nearbyFuelStationsCache.length > 0) {
      this.renderEditFuelStationDropdown(this.nearbyFuelStationsCache, 'nearby', '', '', this.userCoords?.source || 'gps', this.userCoords?.city || '');
    } else {
      this.locateNearbyFuelStationsForEdit(true);
    }
    if (input) input.focus();
  },

  clearEditElecStationInput() {
    const input = document.getElementById('edit-elec-station');
    if (input) {
      input.value = '';
    }
    const clearBtn = document.getElementById('edit-elec-station-clear-btn');
    if (clearBtn) clearBtn.classList.add('hidden');
    this.renderEditElecStationPills();
    if (this.nearbyElecStationsCache && this.nearbyElecStationsCache.length > 0) {
      this.renderEditElecStationDropdown(this.nearbyElecStationsCache, 'nearby', '', '', this.userCoords?.source || 'gps', this.userCoords?.city || '');
    } else {
      this.locateNearbyElecStationsForEdit(true);
    }
    if (input) input.focus();
  },

  // 用户点击油站快速选择或取消选择 (方式 1 & 3 协同)
  setFuelStation(stationStr) {
    const input = document.getElementById('fuel-station');
    if (!input) return;
    if (input.value === stationStr) {
      input.value = '';
    } else {
      input.value = stationStr;
    }
    const clearBtn = document.getElementById('fuel-station-clear-btn');
    if (clearBtn) {
      clearBtn.classList.toggle('hidden', !input.value);
    }
    this.renderFuelStationPills();
  },

  // 动态渲染常用充电站胶囊列表与 datalist (方式 1: 历史充电站，使用次数越多越靠前，只保留前5个)
  renderElecStationPills() {
    const container = document.getElementById('elec-station-pills');
    const box = document.getElementById('elec-history-stations-box');
    const badge = document.getElementById('elec-history-count-badge');
    const datalist = document.getElementById('elec-station-datalist');
    const editDatalist = document.getElementById('edit-elec-station-datalist');

    if (datalist) {
      datalist.innerHTML = (this.elecStations || []).map(s => `<option value="${String(s).replace(/"/g, '&quot;')}"></option>`).join('');
    }
    if (editDatalist) {
      editDatalist.innerHTML = datalist ? datalist.innerHTML : '';
    }
    this.renderEditElecStationPills();

    if (!container || !box) return;

    const list = this.elecStationsWithCount && this.elecStationsWithCount.length > 0
      ? this.elecStationsWithCount
      : [];

    const displayList = list.filter(item => (item.count || 0) > 0).slice(0, 5);

    if (displayList.length === 0) {
      container.innerHTML = '';
      box.classList.add('hidden');
      return;
    }

    box.classList.remove('hidden');
    if (badge) badge.textContent = `常用前 ${displayList.length} 个`;

    const currentVal = document.getElementById('elec-station')?.value?.trim() || '';

    container.innerHTML = displayList.map(item => {
      const station = item.name;
      const count = item.count;
      const isActive = station === currentVal;
      const safeStation = station.replace(/"/g, '&quot;').replace(/'/g, "\\'");
      return `
        <span class="inline-flex items-center gap-1.5 px-2.5 py-1 rounded-full text-xs font-medium border transition ${
          isActive 
            ? 'bg-emerald-100 text-emerald-900 border-emerald-300 shadow-sm font-semibold ring-1 ring-emerald-400' 
            : 'bg-white text-slate-700 hover:bg-slate-100 border-slate-200'
        }">
          <button type="button" onclick="App.setElecStation('${safeStation}')" class="focus:outline-none flex items-center gap-1">
            <span class="text-emerald-600">⚡</span><span>${station}</span>
            <span class="text-[10px] ${isActive ? 'text-emerald-800 font-bold' : 'text-slate-400'}">(${count}次)</span>
          </button>
          <button type="button" onclick="App.removeElecStation('${safeStation}', event)" class="text-slate-400 hover:text-rose-600 rounded-full w-3.5 h-3.5 flex items-center justify-center text-[13px] leading-none transition" title="从列表移除">
            ×
          </button>
        </span>
      `;
    }).join('');
  },

  // 用户点击电站标签快速选择或取消选择 (方式 1 & 3 协同)
  setElecStation(stationStr) {
    const input = document.getElementById('elec-station');
    if (!input) return;
    if (input.value === stationStr) {
      input.value = '';
    } else {
      input.value = stationStr;
    }
    const clearBtn = document.getElementById('elec-station-clear-btn');
    if (clearBtn) {
      clearBtn.classList.toggle('hidden', !input.value);
    }
    this.renderElecStationPills();
  },

  // 点击“+ 常用”主动添加加油站
  addCustomFuelStation() {
    const input = document.getElementById('fuel-station');
    if (!input) return;
    const val = input.value.trim();
    if (!val) {
      this.showToast('请先输入加油站名称', 'info');
      input.focus();
      return;
    }
    this.addFuelStation(val);
    this.showToast(`已保存常用加油站 “${val}”`, 'success');
  },

  // 点击“+ 常用”主动添加充电站
  addCustomElecStation() {
    const input = document.getElementById('elec-station');
    if (!input) return;
    const val = input.value.trim();
    if (!val) {
      this.showToast('请先输入充电场站名称', 'info');
      input.focus();
      return;
    }
    this.addElecStation(val);
    this.showToast(`已保存常用充电站 “${val}”`, 'success');
  },

  // 持久化保存加油站至列表
  addFuelStation(stationStr) {
    const trimmed = (stationStr || '').trim();
    if (!trimmed) return;
    if (!this.fuelStations.includes(trimmed)) {
      this.fuelStations.push(trimmed);
      try {
        localStorage.setItem('car_fuel_stations', JSON.stringify(this.fuelStations));
      } catch (e) {
        console.warn('保存常用加油站失败:', e);
      }
      this.extractFuelStationsHistory();
    }
  },

  // 持久化保存充电站至列表
  addElecStation(stationStr) {
    const trimmed = (stationStr || '').trim();
    if (!trimmed) return;
    if (!this.elecStations.includes(trimmed)) {
      this.elecStations.push(trimmed);
      try {
        localStorage.setItem('car_charging_stations', JSON.stringify(this.elecStations));
      } catch (e) {
        console.warn('保存常用充电站失败:', e);
      }
      this.extractElecStationsHistory();
    }
  },

  // 从常用加油站列表移除
  removeFuelStation(stationStr, e) {
    if (e) e.stopPropagation();
    this.fuelStations = (this.fuelStations || []).filter(s => s !== stationStr);
    if (this.fuelStationsWithCount) {
      this.fuelStationsWithCount = this.fuelStationsWithCount.filter(item => item.name !== stationStr);
    }
    try {
      localStorage.setItem('car_fuel_stations', JSON.stringify(this.fuelStations));
    } catch (e) {}
    const input = document.getElementById('fuel-station');
    if (input && input.value.trim() === stationStr) {
      input.value = '';
    }
    this.renderFuelStationPills();
  },

  // 从常用充电站列表移除
  removeElecStation(stationStr, e) {
    if (e) e.stopPropagation();
    this.elecStations = (this.elecStations || []).filter(s => s !== stationStr);
    if (this.elecStationsWithCount) {
      this.elecStationsWithCount = this.elecStationsWithCount.filter(item => item.name !== stationStr);
    }
    try {
      localStorage.setItem('car_charging_stations', JSON.stringify(this.elecStations));
    } catch (e) {}
    const input = document.getElementById('elec-station');
    if (input && input.value.trim() === stationStr) {
      input.value = '';
    }
    this.renderElecStationPills();
  }
  };
})(typeof window !== "undefined" ? window : globalThis);
