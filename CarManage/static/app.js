/**
 * 汽车维护与能耗记录核心交互逻辑 (App.js)
 */

const App = {
  vehicles: [],
  activeVehicleId: null,
  currentTab: 'overview',
  historyFilter: 'all',
  energyAnalysisData: null,
  energyIntervalFilter: 'all',
  energyChartTab: 'both',

  // 常用费用支出项目（默认为空，用户输入后才增加）
  expenseItems: [],

  // 常用加油站列表与充电站列表
  fuelStations: [],
  fuelStationsWithCount: [],
  elecStations: [],

  // 地图检索 API 固化配置与状态 (高德 + 百度双地图并发检索，无需客户配置)
  amapApiKey: '9885ffe2a89aff7e151b50fa4801c9a9',
  baiduApiKey: '3YJcotkPHfGjr1vN50trOKrHoWUAwWCS',
  userCoords: null,
  isLocatingFuel: false,
  isLocatingElec: false,
  isLocatingFuelEdit: false,
  isLocatingElecEdit: false,
  nearbyFuelStationsCache: null,
  nearbyElecStationsCache: null,
  fuelSearchDebounceTimer: null,
  elecSearchDebounceTimer: null,
  lastPoiSearchResults: null,
  lastPoiKeyword: '',
  lastElecPoiSearchResults: null,
  lastElecPoiKeyword: '',
  lastEditFuelPoiSearchResults: null,
  lastEditFuelPoiKeyword: '',
  lastEditElecPoiSearchResults: null,
  lastEditElecPoiKeyword: '',

  // 联动三项计算器状态跟踪
  triCalc: {
    fuel: {
      userEdited: [], // 记录用户手动修改的字段，例如 ['amount', 'price']
      calculated: null // 当前被自动算出的字段，例如 'total'
    },
    elec: {
      userEdited: [],
      calculated: null
    }
  },

  // 用户认证与账号状态
  currentUser: null,
  authToken: null,

  // 初始化入口
  async init() {
    this.bindEvents();
    this.initDateTimeInputs();
    this.initExpenseItems();
    this.registerServiceWorker();
    await this.initSyncEngine();
    await this.initStations();
    this.initAuth();
    if (this.currentUser) {
      await this.loadVehicles();
      this.preloadNearbyFuelStations();
      this.preloadNearbyElecStations();
    }
  },

  initAuth() {
    const savedUser = localStorage.getItem('car_tracker_auth_user');
    const savedToken = localStorage.getItem('car_tracker_auth_token');
    if (savedUser && savedToken) {
      this.currentUser = savedUser;
      this.authToken = savedToken;
      this.updateUserUI(savedUser);
      this.closeLoginModal();
      this.verifyAuthTokenSilent();
    } else {
      this.showLoginModal();
    }
  },

  updateUserUI(username) {
    const nameEl = document.getElementById('user-profile-name');
    const modalNameEl = document.getElementById('user-modal-name');
    if (nameEl) nameEl.textContent = username || 'AubreyHan';
    if (modalNameEl) modalNameEl.textContent = username || 'AubreyHan';
  },

  showLoginModal() {
    const modal = document.getElementById('login-modal');
    if (modal) modal.classList.remove('hidden');
    const userInp = document.getElementById('login-username');
    if (userInp && !userInp.value) userInp.value = 'AubreyHan';
  },

  closeLoginModal() {
    const modal = document.getElementById('login-modal');
    if (modal) modal.classList.add('hidden');
    const errBox = document.getElementById('login-error-msg');
    if (errBox) errBox.classList.add('hidden');
  },

  openUserModal() {
    if (!this.currentUser) {
      this.showLoginModal();
      return;
    }
    const modal = document.getElementById('user-modal');
    if (modal) modal.classList.remove('hidden');
    this.updateUserUI(this.currentUser);
  },

  closeUserModal() {
    const modal = document.getElementById('user-modal');
    if (modal) modal.classList.add('hidden');
  },

  togglePasswordVisibility(inputId, btn) {
    const input = document.getElementById(inputId);
    if (!input) return;
    if (input.type === 'password') {
      input.type = 'text';
      if (btn) btn.textContent = '🙈';
    } else {
      input.type = 'password';
      if (btn) btn.textContent = '👁️';
    }
  },

  async handleLogin(e) {
    if (e && e.preventDefault) e.preventDefault();
    const userInp = document.getElementById('login-username');
    const pwdInp = document.getElementById('login-password');
    const errBox = document.getElementById('login-error-msg');
    const submitBtn = document.getElementById('btn-login-submit');

    const username = userInp ? userInp.value.trim() : '';
    const password = pwdInp ? pwdInp.value : '';

    if (!username || !password) {
      if (errBox) {
        errBox.textContent = '请输入用户名和密码';
        errBox.classList.remove('hidden');
      }
      return;
    }

    if (errBox) errBox.classList.add('hidden');
    if (submitBtn) {
      submitBtn.disabled = true;
      submitBtn.textContent = '正在登录...';
    }

    try {
      const baseUrl = window.CarSync ? await CarSync.getApiBaseUrl() : '';
      let loginSuccess = false;
      let token = '';

      if (navigator.onLine && baseUrl) {
        try {
          const res = await fetch(`${baseUrl}/api/auth/login`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ username, password })
          });
          const data = await res.json();
          if (res.ok && data.success) {
            loginSuccess = true;
            token = data.token;
          } else {
            throw new Error(data.message || '用户名或密码错误');
          }
        } catch (netErr) {
          if (this.verifyOfflineAuth(username, password)) {
            loginSuccess = true;
            token = 'offline_token_' + Date.now();
          } else {
            throw netErr;
          }
        }
      } else {
        if (this.verifyOfflineAuth(username, password)) {
          loginSuccess = true;
          token = 'offline_token_' + Date.now();
        } else {
          throw new Error('用户名或密码错误 (离线)');
        }
      }

      if (loginSuccess) {
        this.currentUser = username;
        this.authToken = token;
        localStorage.setItem('car_tracker_auth_user', username);
        localStorage.setItem('car_tracker_auth_token', token);
        this.updateUserUI(username);
        this.closeLoginModal();
        this.showToast(`🎉 欢迎回来，${username}！登录状态已保存。`, 'success');
        if (pwdInp) pwdInp.value = '';
        await this.loadVehicles();
        this.preloadNearbyFuelStations();
        this.preloadNearbyElecStations();
      }
    } catch (err) {
      if (errBox) {
        errBox.textContent = err.message || '用户名或密码错误';
        errBox.classList.remove('hidden');
      }
      this.showToast(err.message || '登录失败', 'error');
    } finally {
      if (submitBtn) {
        submitBtn.disabled = false;
        submitBtn.textContent = '立即登录';
      }
    }
  },

  verifyOfflineAuth(username, password) {
    if (username === 'AubreyHan') {
      const customLocalPwd = localStorage.getItem('car_tracker_offline_pwd');
      if (customLocalPwd) {
        return password === customLocalPwd;
      }
      return password === 'P@ss=w0rd';
    }
    return false;
  },

  async handleChangePassword(e) {
    if (e && e.preventDefault) e.preventDefault();
    const oldInp = document.getElementById('change-pwd-old');
    const newInp = document.getElementById('change-pwd-new');
    const confirmInp = document.getElementById('change-pwd-confirm');
    const btn = document.getElementById('btn-change-pwd-submit');

    const oldPwd = oldInp ? oldInp.value : '';
    const newPwd = newInp ? newInp.value : '';
    const confirmPwd = confirmInp ? confirmInp.value : '';

    if (!oldPwd || !newPwd || !confirmPwd) {
      this.showToast('请完整输入各项密码', 'error');
      return;
    }
    if (newPwd !== confirmPwd) {
      this.showToast('两次输入的新密码不一致，请仔细核对', 'error');
      return;
    }
    if (newPwd.length < 4) {
      this.showToast('新密码长度不能少于4位字符', 'error');
      return;
    }

    if (btn) {
      btn.disabled = true;
      btn.textContent = '正在修改...';
    }

    try {
      const baseUrl = window.CarSync ? await CarSync.getApiBaseUrl() : '';
      let changed = false;

      if (navigator.onLine && baseUrl) {
        const res = await fetch(`${baseUrl}/api/auth/change-password`, {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({
            username: this.currentUser,
            old_password: oldPwd,
            new_password: newPwd
          })
        });
        const data = await res.json();
        if (res.ok && data.success) {
          changed = true;
          if (data.token) {
            this.authToken = data.token;
            localStorage.setItem('car_tracker_auth_token', data.token);
          }
        } else {
          throw new Error(data.message || '修改密码失败');
        }
      } else {
        if (this.verifyOfflineAuth(this.currentUser, oldPwd)) {
          changed = true;
        } else {
          throw new Error('原密码错误 (离线)');
        }
      }

      if (changed) {
        localStorage.setItem('car_tracker_offline_pwd', newPwd);
        this.showToast('✅ 密码修改成功！登录状态持续保持中。', 'success');
        if (oldInp) oldInp.value = '';
        if (newInp) newInp.value = '';
        if (confirmInp) confirmInp.value = '';
        this.closeUserModal();
      }
    } catch (err) {
      this.showToast(err.message || '修改密码失败', 'error');
    } finally {
      if (btn) {
        btn.disabled = false;
        btn.textContent = '确认修改密码';
      }
    }
  },

  async verifyAuthTokenSilent() {
    if (!this.authToken || !navigator.onLine) return;
    try {
      const baseUrl = window.CarSync ? await CarSync.getApiBaseUrl() : '';
      if (!baseUrl) return;
      const res = await fetch(`${baseUrl}/api/auth/verify`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ token: this.authToken })
      });
      if (res.status === 401) {
        console.warn('登录凭据已在远端失效或密码已被重置，请重新登录');
        this.logout(false);
      }
    } catch (e) {
      // 离线或弱网静默忽略
    }
  },

  logout(showToast = true) {
    if (showToast && !confirm('确认退出当前登录状态吗？退出后需要重新输入密码登录。')) {
      return;
    }
    localStorage.removeItem('car_tracker_auth_user');
    localStorage.removeItem('car_tracker_auth_token');
    this.currentUser = null;
    this.authToken = null;
    this.closeUserModal();
    this.showLoginModal();
    if (showToast) {
      this.showToast('已安全退出登录', 'info');
    }
  },

  registerServiceWorker() {
    if ('serviceWorker' in navigator) {
      let refreshing = false;
      navigator.serviceWorker.addEventListener('controllerchange', () => {
        if (!refreshing) {
          refreshing = true;
          console.log('[PWA] 检测到 Service Worker 控制器更新，自动重载页面以激活最新功能');
          window.location.reload();
        }
      });

      window.addEventListener('load', () => {
        navigator.serviceWorker.register('./sw.js')
          .then(reg => {
            console.log('[PWA] Service Worker 注册就绪，范围:', reg.scope);
            if (reg.update) {
              reg.update().catch(() => {});
            }
          })
          .catch(err => console.warn('[PWA] Service Worker 注册受限:', err));
      });
    }
  },

  async initSyncEngine() {
    if (window.CarSync) {
      CarSync.onStatusChange((status, pending) => {
        this.updateSyncBadge(status, pending);
      });
      await CarSync.init();
    }
  },

  updateSyncBadge(status, pendingCount) {
    const btn = document.getElementById('sync-status-btn');
    const dot = document.getElementById('sync-status-dot');
    const text = document.getElementById('sync-status-text');
    const modalStatus = document.getElementById('modal-network-status');
    const modalPending = document.getElementById('modal-pending-count');

    if (modalPending) modalPending.textContent = `${pendingCount} 笔待上传`;

    if (status === 'syncing') {
      if (btn) btn.className = 'inline-flex items-center space-x-1.5 px-2.5 py-1.5 rounded-lg border text-xs font-medium bg-blue-50 text-blue-700 border-blue-200 shadow-sm';
      if (dot) dot.className = 'w-2 h-2 rounded-full bg-blue-500 animate-pulse';
      if (text) text.textContent = '同步中...';
      if (modalStatus) modalStatus.innerHTML = '<span class="w-2 h-2 rounded-full bg-blue-500 animate-pulse"></span> 正在与云端双向同步';
    } else if (status === 'offline') {
      if (btn) btn.className = 'inline-flex items-center space-x-1.5 px-2.5 py-1.5 rounded-lg border text-xs font-medium bg-amber-50 text-amber-700 border-amber-200 shadow-sm';
      if (dot) dot.className = 'w-2 h-2 rounded-full bg-amber-500';
      if (text) text.textContent = pendingCount > 0 ? `离线 · ${pendingCount}笔待传` : '离线优先';
      if (modalStatus) modalStatus.innerHTML = '<span class="w-2 h-2 rounded-full bg-amber-500"></span> 离线优先模式 (本地已存，联网自动推送)';
    } else if (status === 'error') {
      if (btn) btn.className = 'inline-flex items-center space-x-1.5 px-2.5 py-1.5 rounded-lg border text-xs font-medium bg-rose-50 text-rose-700 border-rose-200 shadow-sm';
      if (dot) dot.className = 'w-2 h-2 rounded-full bg-rose-500';
      if (text) text.textContent = '云端未连接';
      const errMsg = window.CarSync && CarSync.lastError ? ` (${CarSync.lastError})` : '';
      if (modalStatus) modalStatus.innerHTML = `<span class="w-2 h-2 rounded-full bg-rose-500"></span> 暂未连接云端${errMsg}，本地离线数据可用`;
    } else {
      if (btn) btn.className = 'inline-flex items-center space-x-1.5 px-2.5 py-1.5 rounded-lg border text-xs font-medium bg-emerald-50 text-emerald-700 border-emerald-200 shadow-sm';
      if (dot) dot.className = 'w-2 h-2 rounded-full bg-emerald-500';
      if (text) text.textContent = '已同步';
      if (modalStatus) modalStatus.innerHTML = '<span class="w-2 h-2 rounded-full bg-emerald-500"></span> 在线就绪 · 数据已全量同步';
    }
  },

  async openSyncModal() {
    const modal = document.getElementById('sync-modal');
    if (modal) modal.classList.remove('hidden');
    const input = document.getElementById('cf-worker-url');
    if (input && window.CarDB) {
      const saved = await CarDB.getSetting('cloudflare_worker_url', '');
      input.value = saved || (window.CarSync ? CarSync.DEFAULT_WORKER_URL : 'https://car-tracker-api.aubreyhan.workers.dev');
    }
    if (window.CarSync) CarSync.updatePendingCount();
  },

  async fillDefaultWorkerUrl() {
    const input = document.getElementById('cf-worker-url');
    if (input) {
      input.value = window.CarSync ? CarSync.DEFAULT_WORKER_URL : 'https://car-tracker-api.aubreyhan.workers.dev';
      await this.saveCloudflareUrl();
    }
  },

  closeSyncModal() {
    const modal = document.getElementById('sync-modal');
    if (modal) modal.classList.add('hidden');
  },

  async saveCloudflareUrl() {
    const input = document.getElementById('cf-worker-url');
    if (!input || !window.CarDB) return;
    let url = input.value.trim();
    if (url) {
      if (!url.startsWith('http://') && !url.startsWith('https://')) {
        url = 'https://' + url;
      }
      if (url.endsWith('.worker')) {
        url = url + 's.dev';
      } else if (url.endsWith('.workers')) {
        url = url + '.dev';
      }
      url = url.replace(/\/+$/, '');
      input.value = url;
    }
    await CarDB.setSetting('cloudflare_worker_url', url);
    this.showToast('Cloudflare Worker 地址已保存！', 'success');
    if (window.CarSync) CarSync.sync(false, true);
  },

  async testCloudflareConnection() {
    const input = document.getElementById('cf-worker-url');
    let url = input ? input.value.trim().replace(/\/+$/, '') : '';
    if (!url) {
      this.showToast('请先输入 Cloudflare Worker 完整地址', 'error');
      return;
    }
    if (!url.startsWith('http://') && !url.startsWith('https://')) {
      url = 'https://' + url;
    }
    if (url.endsWith('.worker')) {
      url = url + 's.dev';
    } else if (url.endsWith('.workers')) {
      url = url + '.dev';
    }
    if (input) input.value = url;
    this.showToast('正在测试云端连接...', 'info');
    try {
      const res = await fetch(`${url}/api/vehicles`, { mode: 'cors' });
      if (res.ok) {
        this.showToast('✅ Cloudflare D1 云端服务连接成功！', 'success');
      } else {
        this.showToast(`连接响应异常: HTTP ${res.status}`, 'error');
      }
    } catch (e) {
      this.showToast(`连接失败: ${e.message}`, 'error');
    }
  },

  async triggerManualSync() {
    if (window.CarSync) {
      this.showToast('正在连接云端双向同步...', 'info');
      await CarSync.sync(false, true);
      if (CarSync.status === 'synced') {
        this.showToast('✅ 云端双向同步完成！', 'success');
      } else if (CarSync.status === 'error') {
        this.showToast(`❌ 同步失败: ${CarSync.lastError || '无法连接云端'}`, 'error');
      }
    }
  },

  async clearPendingQueue() {
    if (!confirm('确认清空本地待上传队列吗？（如遇队列卡死无法同步时推荐使用）')) return;
    if (window.CarSync) {
      await CarSync.clearQueue();
      this.showToast('本地待上传队列已清空！', 'success');
      if (window.CarSync) CarSync.sync(false, true);
    }
  },


  // 绑定事件与三项联动监听器
  bindEvents() {
    // 监听油耗输入框联动
    const fuelAmount = document.getElementById('fuel-amount');
    const fuelPrice = document.getElementById('fuel-unit-price');
    const fuelTotal = document.getElementById('fuel-total-price');

    if (fuelAmount && fuelPrice && fuelTotal) {
      ['input', 'change'].forEach(evt => {
        fuelAmount.addEventListener(evt, () => this.handleTriInput('fuel', 'amount'));
        fuelPrice.addEventListener(evt, () => this.handleTriInput('fuel', 'price'));
        fuelTotal.addEventListener(evt, () => this.handleTriInput('fuel', 'total'));
      });
    }

    // 监听电耗输入框联动
    const elecAmount = document.getElementById('elec-amount');
    const elecPrice = document.getElementById('elec-unit-price');
    const elecTotal = document.getElementById('elec-total-price');

    if (elecAmount && elecPrice && elecTotal) {
      ['input', 'change'].forEach(evt => {
        elecAmount.addEventListener(evt, () => this.handleTriInput('elec', 'amount'));
        elecPrice.addEventListener(evt, () => this.handleTriInput('elec', 'price'));
        elecTotal.addEventListener(evt, () => this.handleTriInput('elec', 'total'));
      });
    }

    // 监听油耗油品标号手动输入
    const fuelTypeInput = document.getElementById('fuel-type');
    if (fuelTypeInput) {
      fuelTypeInput.addEventListener('input', () => {
        const val = fuelTypeInput.value.trim();
        this.updateFuelPillHighlight(val);
        if (val) {
          if (this.activeVehicleId) {
            localStorage.setItem(`car_last_fuel_type_${this.activeVehicleId}`, val);
          }
          localStorage.setItem('car_last_fuel_type_common', val);
        }
      });
    }

    // 通用油站/电站输入框自动定位与POI联想绑定器
    const setupStationInput = (inputId, clearBtnId, dropdownId, locateFn, searchFn, cacheKey, renderDropdownFn, closeDropdownFn, addCustomFn) => {
      const input = document.getElementById(inputId);
      const clearBtn = document.getElementById(clearBtnId);
      if (!input) return;

      input.addEventListener('input', () => {
        const val = input.value.trim();
        if (clearBtn) clearBtn.classList.toggle('hidden', !val);
        this.renderFuelStationPills();
        this.renderElecStationPills();

        const timerKey = `${inputId}DebounceTimer`;
        if (this[timerKey]) clearTimeout(this[timerKey]);
        this[timerKey] = setTimeout(() => {
          if (val) {
            searchFn(val);
          } else {
            const cache = this[cacheKey];
            if (cache && cache.length > 0) {
              renderDropdownFn(cache, 'nearby', '', '', this.userCoords?.source || 'gps', this.userCoords?.city || '');
            } else {
              locateFn(true);
            }
          }
        }, 250);
      });

      const handleActivate = () => {
        const val = input.value.trim();
        const dropdown = document.getElementById(dropdownId);
        const isOpen = dropdown && !dropdown.classList.contains('hidden');

        if (val) {
          if (!isOpen) {
            searchFn(val);
          }
        } else {
          // 输入框为空时，点击/获取焦点直接弹出推荐列表 (秒级展示缓存或自动定位)
          if (!isOpen) {
            const cache = this[cacheKey];
            if (cache && cache.length > 0) {
              renderDropdownFn(cache, 'nearby', '', '', this.userCoords?.source || 'gps', this.userCoords?.city || '');
            } else {
              locateFn(true);
            }
          }
        }
      };

      input.addEventListener('focus', handleActivate);
      input.addEventListener('click', handleActivate);

      if (addCustomFn) {
        input.addEventListener('keydown', (e) => {
          if (e.key === 'Enter') {
            e.preventDefault();
            closeDropdownFn();
            addCustomFn();
          }
        });
      }
    };

    // 1. 油耗录入油站输入框联动
    setupStationInput(
      'fuel-station', 'fuel-station-clear-btn', 'fuel-station-dropdown',
      (auto) => this.locateNearbyFuelStations(auto),
      (val) => this.searchFuelStationPOI(val, 'fuel'),
      'nearbyFuelStationsCache',
      (c, m, k, t, s, city) => this.renderFuelStationDropdown(c, m, k, t, s, city),
      () => this.closeFuelStationDropdown(),
      () => this.addCustomFuelStation()
    );

    // 2. 电耗录入充电站输入框联动
    setupStationInput(
      'elec-station', 'elec-station-clear-btn', 'elec-station-dropdown',
      (auto) => this.locateNearbyElecStations(auto),
      (val) => this.searchElecStationPOI(val, 'elec'),
      'nearbyElecStationsCache',
      (c, m, k, t, s, city) => this.renderElecStationDropdown(c, m, k, t, s, city),
      () => this.closeElecStationDropdown(),
      () => this.addCustomElecStation()
    );

    // 3. 历史编辑油耗油站输入框联动
    setupStationInput(
      'edit-fuel-station', 'edit-fuel-station-clear-btn', 'edit-fuel-station-dropdown',
      (auto) => this.locateNearbyFuelStationsForEdit(auto),
      (val) => this.searchFuelStationPOI(val, 'edit'),
      'nearbyFuelStationsCache',
      (c, m, k, t, s, city) => this.renderEditFuelStationDropdown(c, m, k, t, s, city),
      () => this.closeEditFuelStationDropdown(),
      null
    );

    // 4. 历史编辑电耗充电站输入框联动
    setupStationInput(
      'edit-elec-station', 'edit-elec-station-clear-btn', 'edit-elec-station-dropdown',
      (auto) => this.locateNearbyElecStationsForEdit(auto),
      (val) => this.searchElecStationPOI(val, 'edit'),
      'nearbyElecStationsCache',
      (c, m, k, t, s, city) => this.renderEditElecStationDropdown(c, m, k, t, s, city),
      () => this.closeEditElecStationDropdown(),
      null
    );

    // 点击页面外部区域时自动关闭对应油站/电站下拉菜单
    document.addEventListener('click', (e) => {
      const dropdownConfigs = [
        { d: 'fuel-station-dropdown', i: 'fuel-station', b: 'btn-fuel-nearby-stations', close: () => this.closeFuelStationDropdown() },
        { d: 'elec-station-dropdown', i: 'elec-station', b: 'btn-elec-nearby-stations', close: () => this.closeElecStationDropdown() },
        { d: 'edit-fuel-station-dropdown', i: 'edit-fuel-station', b: 'btn-edit-fuel-nearby-stations', close: () => this.closeEditFuelStationDropdown() },
        { d: 'edit-elec-station-dropdown', i: 'edit-elec-station', b: 'btn-edit-elec-nearby-stations', close: () => this.closeEditElecStationDropdown() }
      ];
      dropdownConfigs.forEach(({ d, i, b, close }) => {
        const dropdown = document.getElementById(d);
        const input = document.getElementById(i);
        const locateBtn = document.getElementById(b);
        if (dropdown && !dropdown.classList.contains('hidden')) {
          if (!dropdown.contains(e.target) && e.target !== input && (!locateBtn || !locateBtn.contains(e.target))) {
            close();
          }
        }
      });
    });
  },

  initDateTimeInputs() {
    const now = new Date();
    const offset = now.getTimezoneOffset() * 60000;
    const localISOTime = (new Date(now - offset)).toISOString().slice(0, 16);
    const localDateOnly = (new Date(now - offset)).toISOString().slice(0, 10);

    const fuelDate = document.getElementById('fuel-date');
    if (fuelDate && !fuelDate.value) fuelDate.value = localISOTime;

    const elecDate = document.getElementById('elec-date');
    if (elecDate && !elecDate.value) elecDate.value = localISOTime;

    const maintDate = document.getElementById('maint-date');
    if (maintDate && !maintDate.value) maintDate.value = localISOTime;
  },

  // 高精度格式化辅助函数：保留最多 maxDecimals 位小数，并移除末尾冗余的 0
  formatPrecision(num, maxDecimals = 5) {
    if (num === null || num === undefined || isNaN(num)) return '';
    const fixed = Number(num).toFixed(maxDecimals);
    const cleaned = fixed.replace(/\.?0+$/, '');
    return cleaned === '-0' ? '0' : cleaned;
  },

  // 千位计数法格式化函数 (千分位加逗号，支持指定小数位数或自适应小数)
  formatThousands(val, decimals = null) {
    if (val === null || val === undefined || val === '') return '';
    const n = Number(val);
    if (isNaN(n)) return String(val);
    if (decimals !== null && decimals !== undefined) {
      const fixedStr = n.toFixed(decimals);
      const parts = fixedStr.split('.');
      parts[0] = parts[0].replace(/\B(?=(\d{3})+(?!\d))/g, ',');
      return parts.join('.');
    }
    // 未指定小数位时，若包含小数则保留有效小数部分并格式化整数部分
    const str = String(val);
    const parts = str.split('.');
    parts[0] = parts[0].replace(/\B(?=(\d{3})+(?!\d))/g, ',');
    return parts.join('.');
  },

  // 核心：实付总价 = 数量 × 单价 智能核算与联动机制
  // 优化规则：修改总价和加电量/加油量时，自动调整单价，而不是调整总价和电量/加油量
  handleTriInput(type, fieldKey) {
    const state = this.triCalc[type];
    const prefix = type === 'fuel' ? 'fuel' : 'elec';
    const unit = type === 'fuel' ? 'L' : '度';
    const priceUnit = type === 'fuel' ? '元/L' : '元/度';
    const colorClass = type === 'fuel' ? 'amber' : 'emerald';

    const inputAmount = document.getElementById(`${prefix}-amount`);
    const inputPrice = document.getElementById(`${prefix}-unit-price`);
    const inputTotal = document.getElementById(`${prefix}-total-price`);

    const tagAmount = document.getElementById(`${prefix}-amount-tag`);
    const tagPrice = document.getElementById(`${prefix}-price-tag`);
    const tagTotal = document.getElementById(`${prefix}-total-tag`);
    const preview = document.getElementById(`${prefix}-formula-preview`);

    if (!inputAmount || !inputPrice || !inputTotal) return;

    // 用户正在手动编辑 fieldKey
    state.userEdited = state.userEdited.filter(k => k !== fieldKey);
    state.userEdited.push(fieldKey);

    if (state.calculated === fieldKey) {
      state.calculated = null;
    }

    const valAmount = parseFloat(inputAmount.value);
    const valPrice = parseFloat(inputPrice.value);
    const valTotal = parseFloat(inputTotal.value);

    const hasAmount = !isNaN(valAmount) && valAmount > 0;
    const hasPrice = !isNaN(valPrice) && valPrice > 0;
    const hasTotal = !isNaN(valTotal) && valTotal >= 0;

    // 清除所有的自动计算高亮标签
    [inputAmount, inputPrice, inputTotal].forEach(el => el && el.classList.remove('tri-calculated'));
    [tagAmount, tagPrice, tagTotal].forEach(el => el && el.classList.add('hidden'));

    if (fieldKey === 'total') {
      // 优化规则 1: 用户修改总价时，自动调整单价，而不是调整加电量/加油量
      if (hasAmount && hasTotal) {
        const calcPrice = this.formatPrecision(valTotal / valAmount, 5);
        inputPrice.value = calcPrice;
        inputPrice.classList.add('tri-calculated');
        if (tagPrice) tagPrice.classList.remove('hidden');
        state.calculated = 'price';
        if (preview) {
          preview.innerHTML = `<span class="text-${colorClass}-800 font-bold">✨ 实时核算：</span><span class="font-semibold text-slate-800"><strong class="text-${colorClass}-900">${valTotal} 元</strong> = ${valAmount} ${unit} × <strong class="text-${colorClass}-900">${calcPrice} ${priceUnit}</strong></span>`;
        }
      } else if (!hasTotal && state.calculated === 'price') {
        inputPrice.value = '';
        state.calculated = null;
      }
    } else if (fieldKey === 'amount') {
      // 优化规则 2: 用户修改加电量/加油量时，自动调整单价，而不是调整总价
      if (hasTotal && hasAmount) {
        const calcPrice = this.formatPrecision(valTotal / valAmount, 5);
        inputPrice.value = calcPrice;
        inputPrice.classList.add('tri-calculated');
        if (tagPrice) tagPrice.classList.remove('hidden');
        state.calculated = 'price';
        if (preview) {
          preview.innerHTML = `<span class="text-${colorClass}-800 font-bold">✨ 实时核算：</span><span class="font-semibold text-slate-800"><strong class="text-${colorClass}-900">${valTotal} 元</strong> = ${valAmount} ${unit} × <strong class="text-${colorClass}-900">${calcPrice} ${priceUnit}</strong></span>`;
        }
      } else if (!hasTotal && hasPrice && hasAmount) {
        // 若总价未填写但已有单价，顺推总价
        const calcTotal = this.formatPrecision(valAmount * valPrice, 5);
        inputTotal.value = calcTotal;
        inputTotal.classList.add('tri-calculated');
        if (tagTotal) tagTotal.classList.remove('hidden');
        state.calculated = 'total';
        if (preview) {
          preview.innerHTML = `<span class="text-${colorClass}-800 font-bold">✨ 实时核算：</span><span class="font-semibold text-slate-800"><strong class="text-${colorClass}-900">${calcTotal} 元</strong> = ${valAmount} ${unit} × ${valPrice} ${priceUnit}</span>`;
        }
      } else if (!hasAmount && state.calculated === 'price') {
        inputPrice.value = '';
        state.calculated = null;
      }
    } else if (fieldKey === 'price') {
      // 优化规则 3: 用户手动修改单价时，按 实付总价 = 数量 × 单价 调整总价
      if (hasAmount && hasPrice) {
        const calcTotal = this.formatPrecision(valAmount * valPrice, 5);
        inputTotal.value = calcTotal;
        inputTotal.classList.add('tri-calculated');
        if (tagTotal) tagTotal.classList.remove('hidden');
        state.calculated = 'total';
        if (preview) {
          preview.innerHTML = `<span class="text-${colorClass}-800 font-bold">✨ 实时核算：</span><span class="font-semibold text-slate-800"><strong class="text-${colorClass}-900">${calcTotal} 元</strong> = ${valAmount} ${unit} × ${valPrice} ${priceUnit}</span>`;
        }
      }
    }

    if (type === 'fuel') this.calcFuelPostPercent();
    if (type === 'elec') this.calcElecEndSoc();

    if (preview && !hasAmount && !hasTotal && !hasPrice) {
      preview.innerHTML = `<span>输入实付总价与${type === 'fuel' ? '加油量' : '加电量'}自动计算单价；亦支持修改单价调整实付总价。</span>`;
    }
  },

  onFuelAmountChange() {
    this.handleTriInput('fuel', 'amount');
    this.calcFuelPostPercent();
  },

  onElecAmountChange() {
    this.handleTriInput('elec', 'amount');
    this.calcElecEndSoc();
  },

  // 核心需求2: 自动计算录入后的油量百分比
  calcFuelPostPercent() {
    const isFullRadio = document.querySelector('input[name="fuel-is-full"]:checked');
    const isFull = isFullRadio ? isFullRadio.value === '1' : true;
    const postInput = document.getElementById('fuel-post-percent');
    const hint = document.getElementById('fuel-post-calc-hint');
    const bar = document.getElementById('fuel-percent-bar');
    const barText = document.getElementById('fuel-percent-bar-text');

    let postPercent = 100;
    let hintText = '加满自动锁定 100%';

    if (!isFull) {
      const pre = parseFloat(document.getElementById('fuel-pre-percent')?.value) || 0;
      const amount = parseFloat(document.getElementById('fuel-amount')?.value) || 0;
      const v = this.getActiveVehicle();

      if (v && v.fuel_tank_capacity > 0 && amount > 0) {
        const addedPct = (amount / v.fuel_tank_capacity) * 100;
        postPercent = Math.min(100, Math.round((pre + addedPct) * 10) / 10);
        hintText = `加前 ${pre}% + 加油 ${amount}L (+${addedPct.toFixed(1)}%) ➔ 自算为 ${postPercent}%`;
      } else {
        postPercent = Math.min(100, pre);
        hintText = v?.fuel_tank_capacity ? `加前 ${pre}% (输入加油量即刻推算)` : `请在车辆档案中补充油箱容量以精准推算`;
      }
    }

    if (postInput) postInput.value = postPercent;
    if (hint) hint.textContent = hintText;
    if (bar) bar.style.width = `${Math.max(0, Math.min(100, postPercent))}%`;
    if (barText) barText.textContent = `加后油量: ${postPercent}%`;
  },

  // 核心需求1: 根据充电的加电量和起始电量自动计算结束电量，无需手动输入 (若开启电池校准则手动输入并推算电池容量)
  calcElecEndSoc() {
    const isCalibrated = !!document.getElementById('elec-battery-calibrate-toggle')?.checked;
    if (isCalibrated) {
      this.calcBatteryCalibration(false);
      return;
    }
    const startSoc = parseFloat(document.getElementById('elec-start-soc')?.value) || 0;
    const amount = parseFloat(document.getElementById('elec-amount')?.value) || 0;
    const endSocInput = document.getElementById('elec-end-soc');
    const v = this.getActiveVehicle();

    let endSoc = startSoc;
    if (v && v.battery_capacity > 0 && amount > 0) {
      const addedPct = (amount / v.battery_capacity) * 100;
      endSoc = Math.min(100, Math.round((startSoc + addedPct) * 10) / 10);
    } else {
      endSoc = Math.min(100, startSoc);
    }

    if (endSocInput) endSocInput.value = endSoc;
    this.updateSocBar();
  },

  // 电池校准开关切换 (支持主录入表单与编辑弹窗)
  toggleBatteryCalibration(isEdit = false) {
    const prefix = isEdit ? 'edit-elec' : 'elec';
    const toggle = document.getElementById(`${prefix}-battery-calibrate-toggle`);
    const isCalibrated = !!toggle?.checked;
    const endSocInput = document.getElementById(`${prefix}-end-soc`);
    const resultBox = document.getElementById(`${prefix}-calibrate-result-box`);

    if (isCalibrated) {
      if (endSocInput) {
        endSocInput.removeAttribute('readonly');
        endSocInput.classList.remove('cursor-not-allowed', 'bg-slate-50');
        endSocInput.classList.add('bg-white', 'border-emerald-500');
        endSocInput.placeholder = '手动输入完成SOC%';
      }
      if (resultBox) resultBox.classList.remove('hidden');
      this.calcBatteryCalibration(isEdit);
    } else {
      if (endSocInput) {
        if (!isEdit) {
          endSocInput.setAttribute('readonly', 'true');
          endSocInput.classList.add('cursor-not-allowed', 'bg-slate-50');
          endSocInput.classList.remove('bg-white', 'border-emerald-500');
          endSocInput.placeholder = '自动计算';
        }
      }
      if (resultBox) resultBox.classList.add('hidden');
      if (isEdit) {
        this.calcEditElecEndSoc();
      } else {
        this.calcElecEndSoc();
      }
    }
  },

  // 计算电池校准新容量 (加电量 / (结束SOC - 起始SOC) * 100)
  calcBatteryCalibration(isEdit = false) {
    const prefix = isEdit ? 'edit-elec' : 'elec';
    const toggle = document.getElementById(`${prefix}-battery-calibrate-toggle`);
    if (!toggle || !toggle.checked) return;

    const startSoc = parseFloat(document.getElementById(`${prefix}-start-soc`)?.value) || 0;
    const endSoc = parseFloat(document.getElementById(`${prefix}-end-soc`)?.value) || 0;
    const amount = parseFloat(document.getElementById(`${prefix}-amount`)?.value) || 0;
    const v = this.getActiveVehicle();

    const capValEl = document.getElementById(`${prefix}-calibrated-capacity-val`);
    const currCapEl = document.getElementById(`${prefix}-current-capacity-val`);
    const deltaSocEl = document.getElementById(`${prefix}-calibrate-delta-soc`);

    const currentCap = v ? parseFloat(v.battery_capacity || 0) : 0;
    if (currCapEl) currCapEl.textContent = currentCap > 0 ? currentCap.toFixed(1) : '--';

    const deltaSoc = endSoc - startSoc;
    if (deltaSocEl) deltaSocEl.textContent = `ΔSOC: ${deltaSoc > 0 ? '+' + deltaSoc.toFixed(1) : deltaSoc.toFixed(1)}%`;

    if (deltaSoc > 0 && amount > 0) {
      const calibratedCap = Math.round((amount / deltaSoc) * 10000) / 100;
      if (capValEl) {
        capValEl.textContent = `${calibratedCap.toFixed(2)} kWh`;
        capValEl.className = 'text-base font-extrabold text-emerald-600';
      }
    } else {
      if (capValEl) {
        capValEl.textContent = deltaSoc <= 0 ? '需结束电量 > 起始电量' : '请输入加电量';
        capValEl.className = 'text-xs font-semibold text-amber-600';
      }
    }

    if (!isEdit) {
      this.updateSocBar();
    }
  },

  onElecEndSocInput(isEdit = false) {
    const prefix = isEdit ? 'edit-elec' : 'elec';
    const toggle = document.getElementById(`${prefix}-battery-calibrate-toggle`);
    if (toggle && toggle.checked) {
      this.calcBatteryCalibration(isEdit);
    } else {
      if (!isEdit) this.updateSocBar();
    }
  },

  // ================= 权威最新车辆状态与能耗推算中心 =================
  // 获取车辆最新状态：统一来自全量记录（status_records / fuel_records / electricity_records）中最后一次录入
  // 规则：
  // 1. 若最后一次为 status_records：显示录入的里程、油量百分比、电量百分比；
  // 2. 若最后一次为 fuel_records：显示录入的总里程、加油结束时的百分比 (fuel_percent)，电量取该记录当时电量或回溯最新已知电量；
  // 3. 若最后一次为 electricity_records：显示录入的总里程、充电结束时的百分比 (end_soc)，油量取该记录当时油量或回溯最新已知油量；
  // 4. 若无任何记录：兜底取车辆档案初始值。
  async getLatestVehicleStatus(vehicleId) {
    const v = this.vehicles.find(item => item.id == vehicleId) || (this.activeVehicleId == vehicleId ? this.getActiveVehicle() : null);

    const parseSafeTime = (d) => {
      if (!d) return 0;
      if (typeof d === 'number') return d;
      const str = String(d).trim().replace(/-/g, '/');
      const t = new Date(str).getTime();
      return isNaN(t) ? 0 : t;
    };

    let fuels = [];
    let elecs = [];
    let statuses = [];

    // 1. 本地 IndexedDB 离线优先读取
    if (window.CarDB && CarDB.db) {
      try {
        const [allF, allE, allS] = await Promise.all([
          CarDB.getAll('fuel_records'),
          CarDB.getAll('electricity_records'),
          CarDB.getAll('status_records')
        ]);
        fuels = (allF || []).filter(r => r.vehicle_id == vehicleId);
        elecs = (allE || []).filter(r => r.vehicle_id == vehicleId);
        statuses = (allS || []).filter(r => r.vehicle_id == vehicleId);
      } catch (err) {
        console.warn('本地获取车辆状态记录异常:', err);
      }
    }

    // 2. 若本地无记录且网络通畅，尝试从服务端拉取
    if (fuels.length === 0 && elecs.length === 0 && statuses.length === 0 && navigator.onLine) {
      try {
        const baseUrl = window.CarSync ? await CarSync.getApiBaseUrl() : '';
        const [fRes, eRes, sRes] = await Promise.all([
          fetch(baseUrl + '/api/records/fuel?vehicle_id=' + vehicleId).catch(() => null),
          fetch(baseUrl + '/api/records/electricity?vehicle_id=' + vehicleId).catch(() => null),
          fetch(baseUrl + '/api/records/status?vehicle_id=' + vehicleId).catch(() => null)
        ]);
        if (fRes && fRes.ok) fuels = await fRes.json();
        if (eRes && eRes.ok) elecs = await eRes.json();
        if (sRes && sRes.ok) statuses = await sRes.json();
      } catch (netErr) {
        console.warn('网络获取最新车辆状态异常:', netErr);
      }
    }

    // 格式化打标并全量归集
    const fMapped = (fuels || []).map(r => ({
      ...r,
      _recType: 'fuel',
      _time: parseSafeTime(r.record_date || r.created_at),
      _mileage: parseFloat(r.mileage || 0)
    }));
    const eMapped = (elecs || []).map(r => ({
      ...r,
      _recType: 'elec',
      _time: parseSafeTime(r.record_date || r.created_at),
      _mileage: parseFloat(r.mileage || 0)
    }));
    const sMapped = (statuses || []).map(r => ({
      ...r,
      _recType: 'status',
      _time: parseSafeTime(r.record_date || r.created_at),
      _mileage: parseFloat(r.mileage || 0)
    }));

    // 统一排序规则：时间优先降序；时间相同按ID降序；ID相同按里程降序
    const allRecords = [...fMapped, ...eMapped, ...sMapped].sort((a, b) => {
      if (b._time !== a._time) return b._time - a._time;
      const idA = typeof a.id === 'number' ? a.id : parseFloat(a.id) || 0;
      const idB = typeof b.id === 'number' ? b.id : parseFloat(b.id) || 0;
      if (idB !== idA) return idB - idA;
      return b._mileage - a._mileage;
    });

    const initM = parseFloat(v?.initial_mileage || 0);
    const initBat = v?.initial_battery_percent !== null && v?.initial_battery_percent !== undefined ? parseFloat(v.initial_battery_percent) : 100;
    const initFuel = v?.initial_fuel_percent !== null && v?.initial_fuel_percent !== undefined ? parseFloat(v.initial_fuel_percent) : 0;

    // 若无任何历史打卡/补能记录，直接兜底初始状态
    if (allRecords.length === 0) {
      return {
        mileage: parseFloat(v?.current_mileage || initM),
        battery_percent: (v?.current_battery_percent !== null && v?.current_battery_percent !== undefined) ? parseFloat(v.current_battery_percent) : initBat,
        fuel_percent: (v?.current_fuel_percent !== null && v?.current_fuel_percent !== undefined) ? parseFloat(v.current_fuel_percent) : initFuel,
        latestRecord: null,
        type: 'initial'
      };
    }

    // 绝对权威：第一条记录就是全量时间线上的“最后一次录入”
    const latestRec = allRecords[0];
    let mileage = latestRec._mileage > 0 ? latestRec._mileage : parseFloat(v?.current_mileage || initM);
    let batteryPercent = null;
    let fuelPercent = null;

    if (latestRec._recType === 'status') {
      // 1. 如果最后一次是录入当前状态：显示录入数据
      if (latestRec.battery_percent !== null && latestRec.battery_percent !== undefined && !isNaN(latestRec.battery_percent)) {
        batteryPercent = parseFloat(latestRec.battery_percent);
      }
      if (latestRec.fuel_percent !== null && latestRec.fuel_percent !== undefined && !isNaN(latestRec.fuel_percent)) {
        fuelPercent = parseFloat(latestRec.fuel_percent);
      }
    } else if (latestRec._recType === 'fuel') {
      // 2. 如果最后一次录入是油耗：显示录入总里程和加油结束时的百分比 (fuel_percent)
      if (latestRec.fuel_percent !== null && latestRec.fuel_percent !== undefined && !isNaN(latestRec.fuel_percent)) {
        fuelPercent = parseFloat(latestRec.fuel_percent);
      }
      // 该加油记录如果有随手记录当时的电量，优先使用
      if (latestRec.battery_percent !== null && latestRec.battery_percent !== undefined && !isNaN(latestRec.battery_percent)) {
        batteryPercent = parseFloat(latestRec.battery_percent);
      }
    } else if (latestRec._recType === 'elec') {
      // 3. 如果最后一次录入是电耗：显示录入总里程和充电结束时的百分比 (end_soc)
      if (latestRec.end_soc !== null && latestRec.end_soc !== undefined && !isNaN(latestRec.end_soc)) {
        batteryPercent = parseFloat(latestRec.end_soc);
      }
      // 该充电记录如果有随手记录当时的油量，优先使用
      if (latestRec.fuel_percent !== null && latestRec.fuel_percent !== undefined && !isNaN(latestRec.fuel_percent)) {
        fuelPercent = parseFloat(latestRec.fuel_percent);
      }
    }

    // 若对应电量尚未确定（如最后一次加油未记电量），沿时间线往前寻找最近一次已知电量
    if (batteryPercent === null || isNaN(batteryPercent)) {
      for (let i = 0; i < allRecords.length; i++) {
        const r = allRecords[i];
        if (r._recType === 'status' && r.battery_percent !== null && r.battery_percent !== undefined && !isNaN(r.battery_percent)) {
          batteryPercent = parseFloat(r.battery_percent);
          break;
        } else if (r._recType === 'elec' && r.end_soc !== null && r.end_soc !== undefined && !isNaN(r.end_soc)) {
          batteryPercent = parseFloat(r.end_soc);
          break;
        } else if (r._recType === 'fuel' && r.battery_percent !== null && r.battery_percent !== undefined && !isNaN(r.battery_percent)) {
          batteryPercent = parseFloat(r.battery_percent);
          break;
        }
      }
      if (batteryPercent === null || isNaN(batteryPercent)) {
        batteryPercent = (v?.current_battery_percent !== null && v?.current_battery_percent !== undefined) ? parseFloat(v.current_battery_percent) : initBat;
      }
    }

    // 若对应油量尚未确定（如最后一次充电未记油量），沿时间线往前寻找最近一次已知油量
    if (fuelPercent === null || isNaN(fuelPercent)) {
      for (let i = 0; i < allRecords.length; i++) {
        const r = allRecords[i];
        if (r._recType === 'status' && r.fuel_percent !== null && r.fuel_percent !== undefined && !isNaN(r.fuel_percent)) {
          fuelPercent = parseFloat(r.fuel_percent);
          break;
        } else if (r._recType === 'fuel' && r.fuel_percent !== null && r.fuel_percent !== undefined && !isNaN(r.fuel_percent)) {
          fuelPercent = parseFloat(r.fuel_percent);
          break;
        } else if (r._recType === 'elec' && r.fuel_percent !== null && r.fuel_percent !== undefined && !isNaN(r.fuel_percent)) {
          fuelPercent = parseFloat(r.fuel_percent);
          break;
        }
      }
      if (fuelPercent === null || isNaN(fuelPercent)) {
        fuelPercent = (v?.current_fuel_percent !== null && v?.current_fuel_percent !== undefined) ? parseFloat(v.current_fuel_percent) : initFuel;
      }
    }

    return {
      mileage,
      battery_percent: batteryPercent,
      fuel_percent: fuelPercent,
      latestRecord: latestRec,
      type: latestRec._recType
    };
  },

  // 查询该车辆最后一次记录的油量百分比（若无记录则取初始油量）
  async getLastKnownFuelPercent(vehicleId) {
    const status = await this.getLatestVehicleStatus(vehicleId);
    return (status && status.fuel_percent !== null && status.fuel_percent !== undefined && !isNaN(status.fuel_percent))
      ? status.fuel_percent
      : 0;
  },

  // 查询该车辆最后一次记录的电量 SOC（若无记录则取初始电量）
  async getLastKnownBatterySoc(vehicleId) {
    const status = await this.getLatestVehicleStatus(vehicleId);
    return (status && status.battery_percent !== null && status.battery_percent !== undefined && !isNaN(status.battery_percent))
      ? status.battery_percent
      : 100;
  },

  // 查询该车辆最后一次记录的油品标号（若无记录则优先读取本地记忆，兜底为 92#）
  async getLastKnownFuelType(vehicleId) {
    let candidateType = null;

    const parseSafeTime = (d) => {
      if (!d) return 0;
      if (typeof d === 'number') return d;
      const str = String(d).trim().replace(/-/g, '/');
      const t = new Date(str).getTime();
      return isNaN(t) ? 0 : t;
    };

    const sortRecordsDesc = (list) => {
      return list.sort((a, b) => {
        const ma = parseFloat(a.mileage || 0);
        const mb = parseFloat(b.mileage || 0);
        if (mb !== ma) return mb - ma;
        const da = parseSafeTime(a.record_date || a.created_at);
        const db = parseSafeTime(b.record_date || b.created_at);
        if (db !== da) return db - da;
        return (b.id || 0) - (a.id || 0);
      });
    };

    if (window.CarDB && CarDB.db) {
      try {
        const fuels = (await CarDB.getAll('fuel_records'))
          .filter(r => r.vehicle_id == vehicleId && r.fuel_type && String(r.fuel_type).trim() !== '');

        const allRecords = sortRecordsDesc(fuels);
        if (allRecords.length > 0 && allRecords[0].fuel_type) {
          candidateType = allRecords[0].fuel_type.trim();
        }
      } catch (err) {
        console.warn('获取最后一次油品标号失败:', err);
      }
    }

    // 如果本地暂未命中但网络通畅，尝试拉取云端记录
    if (!candidateType && navigator.onLine) {
      try {
        const baseUrl = window.CarSync ? await CarSync.getApiBaseUrl() : '';
        const fRes = await fetch(`${baseUrl}/api/records/fuel?vehicle_id=${vehicleId}`);
        if (fRes.ok) {
          const fData = await fRes.json();
          const fuels = (Array.isArray(fData) ? fData : []).filter(r => r.fuel_type && String(r.fuel_type).trim() !== '');
          const allRecords = sortRecordsDesc(fuels);
          if (allRecords.length > 0 && allRecords[0].fuel_type) {
            candidateType = allRecords[0].fuel_type.trim();
          }
        }
      } catch (netErr) {
        console.warn('网络获取最后一次油品标号异常:', netErr);
      }
    }

    if (candidateType) {
      return candidateType;
    }

    // 优先读取该车辆的本地记忆，其次读取通用记忆，最后兜底 92#
    return localStorage.getItem(`car_last_fuel_type_${vehicleId}`) ||
           localStorage.getItem('car_last_fuel_type_common') ||
           '92#';
  },

  resetTriCalc(type) {
    const prefix = type === 'fuel' ? 'fuel' : 'elec';
    document.getElementById(`${prefix}-amount`).value = '';
    document.getElementById(`${prefix}-unit-price`).value = '';
    document.getElementById(`${prefix}-total-price`).value = '';

    ['amount', 'price', 'total'].forEach(k => {
      const input = document.getElementById(`${prefix}-${k === 'price' ? 'unit-price' : k + '-price'}`) || document.getElementById(`${prefix}-amount`);
      const tag = document.getElementById(`${prefix}-${k}-tag`);
      if (input) input.classList.remove('tri-calculated');
      if (tag) tag.classList.add('hidden');
    });

    const preview = document.getElementById(`${prefix}-formula-preview`);
    if (preview) {
      preview.innerHTML = `<span>输入实付总价与${type === 'fuel' ? '加油量' : '加电量'}自动计算单价；亦支持修改单价调整实付总价。</span>`;
    }

    this.triCalc[type].userEdited = [];
    this.triCalc[type].calculated = null;

    if (type === 'fuel') {
      const v = this.getActiveVehicle();
      if (v) {
        this.getLastKnownFuelPercent(v.id).then(lastFuel => {
          const preInput = document.getElementById('fuel-pre-percent');
          if (preInput) preInput.value = lastFuel;
          this.calcFuelPostPercent();
        });
        this.getLastKnownFuelType(v.id).then(lastFuelType => {
          this.setFuelType(lastFuelType);
        });
      } else {
        this.calcFuelPostPercent();
      }
    }
    if (type === 'elec') {
      const v = this.getActiveVehicle();
      if (v) {
        this.getLastKnownBatterySoc(v.id).then(lastSoc => {
          const startInput = document.getElementById('elec-start-soc');
          if (startInput) startInput.value = lastSoc;
          this.calcElecEndSoc();
        });
      } else {
        this.calcElecEndSoc();
      }
    }

    this.showToast('已重置计算数值', 'info');
  },

  // 快捷油品设置与记忆
  setFuelType(typeStr) {
    if (!typeStr) return;
    const input = document.getElementById('fuel-type');
    if (input) input.value = typeStr;
    this.updateFuelPillHighlight(typeStr);

    if (this.activeVehicleId) {
      localStorage.setItem(`car_last_fuel_type_${this.activeVehicleId}`, typeStr);
    }
    localStorage.setItem('car_last_fuel_type_common', typeStr);
  },

  // 更新油品药丸按钮高亮状态
  updateFuelPillHighlight(typeStr) {
    const container = document.getElementById('fuel-pills');
    if (!container) return;
    const cleanTarget = (typeStr || '').replace(/\s*(汽油|柴油)/g, '').trim();
    Array.from(container.children).forEach(btn => {
      const btnVal = btn.getAttribute('onclick') || '';
      const match = btnVal.match(/App\.setFuelType\(['"]([^'"]+)['"]\)/);
      const pillValue = match ? match[1] : btn.innerText.replace(/\s*(汽油|柴油)/g, '').trim();
      const btnText = btn.innerText.trim();

      if (pillValue === typeStr || pillValue === cleanTarget || btnText === typeStr || (cleanTarget && btnText.startsWith(cleanTarget))) {
        btn.classList.add('active');
      } else {
        btn.classList.remove('active');
      }
    });
  },

  // 快捷充电类型设置
  setChargingType(typeStr) {
    document.getElementById('elec-charging-type').value = typeStr;
    const container = document.getElementById('elec-pills');
    Array.from(container.children).forEach(btn => {
      if (btn.innerText.includes(typeStr)) {
        btn.classList.add('active');
      } else {
        btn.classList.remove('active');
      }
    });
  },

  // 初始化常用费用支出标签（离线优先，用户输入后增加，并从历史记录/多端同步数据中自动聚合）
  async initExpenseItems() {
    try {
      const storedV2 = localStorage.getItem('car_expense_items_v2');
      if (storedV2 !== null) {
        this.expenseItems = JSON.parse(storedV2);
      } else {
        this.expenseItems = [];
        localStorage.removeItem('car_expense_items');
        localStorage.setItem('car_expense_items_v2', JSON.stringify([]));
      }
    } catch (e) {
      this.expenseItems = [];
    }

    // 从已有历史支出记录和本地DB中提取常用类别，实现多端同步
    await this.extractExpenseItemsFromHistory();
    this.renderExpenseItemsPills();

    // 支出项目输入框默认为空，并监听用户输入与回车
    const itemInput = document.getElementById('maint-item-type');
    if (itemInput) {
      itemInput.value = '';
      itemInput.addEventListener('input', () => {
        this.updateExpensePillsHighlight(itemInput.value.trim());
      });
      itemInput.addEventListener('keydown', (e) => {
        if (e.key === 'Enter') {
          e.preventDefault();
          this.addCustomExpenseItem();
        }
      });
    }

    const editInput = document.getElementById('edit-maint-item-type');
    if (editInput) {
      editInput.addEventListener('input', () => {
        this.renderEditExpenseItemsPills();
      });
    }
  },

  // 扫描所有支出记录，统计各类别真实使用频次并与本地常用列表双向合并，实现多端数据同步
  async extractExpenseItemsFromHistory() {
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

      // 1. 扫描本地 IndexedDB maintenance_records
      if (window.CarDB && CarDB.db) {
        const maints = await CarDB.getAll('maintenance_records');
        (maints || []).forEach(r => {
          const item = (r.item_type || '').trim();
          if (item) {
            countMap[item] = (countMap[item] || 0) + 1;
            const time = parseTime(r.record_date || r.created_at);
            if (!lastUsedMap[item] || time > lastUsedMap[item]) {
              lastUsedMap[item] = time;
            }
          }
        });
      }

      // 2. 将用户本地已经添加但尚未产生记录的项目也纳入（频次计为0，避免被冲掉）
      (this.expenseItems || []).forEach(item => {
        const s = (item || '').trim();
        if (s && countMap[s] === undefined) {
          countMap[s] = 0;
          lastUsedMap[s] = 0;
        }
      });

      // 3. 按使用频次倒序排序，频次相同按最近使用时间倒序
      const sortedItems = Object.keys(countMap);
      sortedItems.sort((a, b) => {
        if (countMap[b] !== countMap[a]) {
          return countMap[b] - countMap[a];
        }
        return (lastUsedMap[b] || 0) - (lastUsedMap[a] || 0);
      });

      this.expenseItems = sortedItems;
      try {
        localStorage.setItem('car_expense_items_v2', JSON.stringify(this.expenseItems));
      } catch (e) {}
    } catch (err) {
      console.warn('提取常用费用类别失败:', err);
    }
  },

  // 动态渲染费用支出胶囊标签列表及自动补全 datalist
  renderExpenseItemsPills() {
    // 渲染录入表单的 datalist
    const datalist = document.getElementById('maint-items-datalist');
    if (datalist) {
      datalist.innerHTML = (this.expenseItems || []).map(item => `<option value="${item.replace(/"/g, '&quot;')}"></option>`).join('');
    }
    // 渲染编辑弹窗的 datalist
    const editDatalist = document.getElementById('edit-maint-items-datalist');
    if (editDatalist) {
      editDatalist.innerHTML = datalist ? datalist.innerHTML : '';
    }

    const container = document.getElementById('maint-items-pills');
    if (container) {
      if (!this.expenseItems || this.expenseItems.length === 0) {
        container.innerHTML = '';
        container.classList.add('hidden');
      } else {
        container.classList.remove('hidden');
        const currentVal = document.getElementById('maint-item-type')?.value?.trim() || '';
        container.innerHTML = this.expenseItems.map(item => {
          const isActive = item === currentVal;
          const safeItem = item.replace(/"/g, '&quot;').replace(/'/g, "\\'");
          return `
            <span class="inline-flex items-center gap-1.5 px-3 py-1 rounded-full text-xs font-medium border transition ${
              isActive 
                ? 'bg-indigo-100 text-indigo-800 border-indigo-300 shadow-sm font-semibold' 
                : 'bg-slate-100 text-slate-700 hover:bg-slate-200 border-slate-200'
            }">
              <button type="button" onclick="App.setMaintItem('${safeItem}')" class="focus:outline-none">
                ${item}
              </button>
              <button type="button" onclick="App.removeExpenseItem('${safeItem}', event)" class="text-slate-400 hover:text-rose-600 rounded-full w-3.5 h-3.5 flex items-center justify-center text-[13px] leading-none transition" title="从常用类别移除">
                ×
              </button>
            </span>
          `;
        }).join('');
      }
    }

    this.renderEditExpenseItemsPills();
  },

  // 渲染编辑记录弹窗内的常用支出类别胶囊
  renderEditExpenseItemsPills() {
    const container = document.getElementById('edit-maint-items-pills');
    if (!container) return;
    if (!this.expenseItems || this.expenseItems.length === 0) {
      container.innerHTML = '';
      container.classList.add('hidden');
      return;
    }
    container.classList.remove('hidden');
    const currentVal = document.getElementById('edit-maint-item-type')?.value?.trim() || '';
    container.innerHTML = this.expenseItems.map(item => {
      const isActive = item === currentVal;
      const safeItem = item.replace(/"/g, '&quot;').replace(/'/g, "\\'");
      return `
        <button type="button" onclick="App.setEditMaintItem('${safeItem}')" class="px-2.5 py-1 rounded-full text-xs font-medium border transition ${
          isActive 
            ? 'bg-indigo-100 text-indigo-800 border-indigo-300 shadow-sm font-semibold' 
            : 'bg-slate-100 text-slate-600 hover:bg-slate-200 border-slate-200'
        }">
          ${item}
        </button>
      `;
    }).join('');
  },

  // 编辑弹窗点击常用支出类别快速填入
  setEditMaintItem(itemStr) {
    const input = document.getElementById('edit-maint-item-type');
    if (!input) return;
    input.value = itemStr;
    this.renderEditExpenseItemsPills();
  },

  // 用户点击支出项目标签快速填入
  setMaintItem(itemStr) {
    const input = document.getElementById('maint-item-type');
    if (!input) return;
    input.value = itemStr;
    this.renderExpenseItemsPills();
  },

  // 更新标签高亮状态
  updateExpensePillsHighlight(currentVal) {
    this.renderExpenseItemsPills();
  },

  // 用户主动点击“+ 添加项目”或在输入框按回车
  addCustomExpenseItem() {
    const input = document.getElementById('maint-item-type');
    if (!input) return;
    const val = input.value.trim();
    if (!val) {
      this.showToast('请先输入项目名称', 'info');
      input.focus();
      return;
    }
    this.addExpenseItem(val);
    this.setMaintItem(val);
    this.showToast(`已添加支出项目 “${val}”`, 'success');
  },

  // 将新输入的支出项目持久化保存至列表
  addExpenseItem(itemStr) {
    const trimmed = (itemStr || '').trim();
    if (!trimmed) return;
    if (!this.expenseItems.includes(trimmed)) {
      this.expenseItems.unshift(trimmed); // 新增项目排在最前
      try {
        localStorage.setItem('car_expense_items_v2', JSON.stringify(this.expenseItems));
      } catch (e) {
        console.warn('保存自定义支出项目失败:', e);
      }
      this.renderExpenseItemsPills();
    }
  },

  // 删除指定的支出项目标签
  removeExpenseItem(itemStr, e) {
    if (e) e.stopPropagation();
    this.expenseItems = this.expenseItems.filter(item => item !== itemStr);
    try {
      localStorage.setItem('car_expense_items_v2', JSON.stringify(this.expenseItems));
    } catch (err) {
      console.warn('更新支出项目失败:', err);
    }
    const input = document.getElementById('maint-item-type');
    if (input && input.value.trim() === itemStr) {
      input.value = '';
    }
    this.renderExpenseItemsPills();
  },

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
  },

  // 电池 SOC 进度条动态渲染
  updateSocBar() {
    const start = parseFloat(document.getElementById('elec-start-soc').value) || 0;
    const end = parseFloat(document.getElementById('elec-end-soc').value) || 0;
    const bar = document.getElementById('soc-preview-bar');
    if (end > start) {
      bar.style.marginLeft = `${start}%`;
      bar.style.width = `${Math.min(100 - start, end - start)}%`;
    } else {
      bar.style.marginLeft = '0%';
      bar.style.width = `${Math.min(100, end || start)}%`;
    }
  },

  // 标签页切换
  async switchTab(tabKey) {
    const v = this.getActiveVehicle();
    if (v) {
      if (tabKey === 'electricity' && v.power_type === 'gas') {
        this.showToast('当前车辆为纯燃油车，无电耗录入功能', 'info');
        tabKey = 'fuel';
      } else if (tabKey === 'fuel' && v.power_type === 'ev') {
        this.showToast('当前车辆为纯电动车，无油耗录入功能', 'info');
        tabKey = 'electricity';
      }
    }

    this.currentTab = tabKey;

    // 隐藏所有 tab 内容
    document.querySelectorAll('.tab-pane').forEach(el => el.classList.add('hidden'));

    // 移除所有导航 active 状态
    document.querySelectorAll('.nav-tab-btn').forEach(btn => btn.classList.remove('active'));
    document.querySelectorAll('.mobile-nav-item').forEach(btn => btn.classList.remove('active'));

    // 显示目标 tab
    const targetPane = document.getElementById(`tab-${tabKey}`);
    if (targetPane) targetPane.classList.remove('hidden');

    // 激活对应按钮
    const deskBtn = document.getElementById(`tab-btn-${tabKey}`);
    if (deskBtn) deskBtn.classList.add('active');

    const mobBtn = document.getElementById(`m-tab-${tabKey}`);
    if (mobBtn) mobBtn.classList.add('active');

    // 刷新该页面数据
    if (tabKey === 'overview') {
      await this.loadOverview();
    } else if (tabKey === 'energy') {
      await this.loadEnergyAnalysis();
    } else if (tabKey === 'history') {
      await this.loadHistory();
    } else if (tabKey === 'vehicles') {
      this.renderVehiclesList();
    } else if (tabKey === 'fuel' || tabKey === 'electricity' || tabKey === 'maintenance') {
      await this.syncActiveVehicleToForms();
      if (tabKey === 'fuel') {
        this.preloadNearbyFuelStations();
      } else if (tabKey === 'electricity') {
        this.preloadNearbyElecStations();
      }
    }
    window.scrollTo({ top: 0, behavior: 'smooth' });
  },

  // 获取所有车辆 (本地 IndexedDB 离线秒读 + 云端最新拉取)
  async loadVehicles() {
    // 自动识别并彻底清理测试车辆 (满足：每次生成新版app时删除自动生成的测试车辆)
    const isTestCar = (v) => {
      if (!v) return false;
      const testPlates = ['沪AD12345', '京A88888', '粤B12345', '粤BD88888'];
      const testNames = ['蔚来ES6', '理想L8', '宝马3系日常代步', '特斯拉家庭出行'];
      return testPlates.includes(v.plate_number) || testNames.includes(v.name);
    };

    try {
      // 1. 离线优先：先从本地 IndexedDB 毫秒级呈现，并自动清退旧版本残留测试车辆
      if (window.CarDB && CarDB.db) {
        let localList = await CarDB.getAll('vehicles');
        const tests = localList.filter(isTestCar);
        for (const t of tests) {
          await CarDB.delete('vehicles', t.id);
        }
        localList = localList.filter(v => !isTestCar(v));

        if (localList && localList.length > 0) {
          this.vehicles = localList;
          this.populateVehicleUI();
        }
      }

      // 2. 如果在线，从云端拉取最新数据并同步剔除测试车辆
      if (navigator.onLine) {
        const baseUrl = window.CarSync ? await CarSync.getApiBaseUrl() : '';
        const res = await fetch(`${baseUrl}/api/vehicles`);
        if (res.ok) {
          const serverVehicles = await res.json();
          this.vehicles = serverVehicles.filter(v => !isTestCar(v));
          if (window.CarDB && CarDB.db) {
            await CarDB.clear('vehicles');
            await CarDB.putMany('vehicles', this.vehicles);
          }
          this.populateVehicleUI();
        }
      }
    } catch (err) {
      console.warn('获取云端车辆失败，保持本地数据:', err);
    }
  },

  populateVehicleUI() {
    const select = document.getElementById('vehicle-select');
    if (!select) return;
    select.innerHTML = '';

    if (this.vehicles.length === 0) {
      select.innerHTML = '<option value="">暂无车辆</option>';
      document.getElementById('no-vehicle-notice').classList.remove('hidden');
      document.getElementById('tab-overview').classList.add('hidden');
      this.renderVehiclesList();
      return;
    }

    document.getElementById('no-vehicle-notice').classList.add('hidden');

    const savedVid = localStorage.getItem('car_tracker_active_vid');
    const foundSaved = this.vehicles.find(v => v.id == savedVid);
    this.activeVehicleId = foundSaved ? foundSaved.id : this.vehicles[0].id;

    this.vehicles.forEach(v => {
      const opt = document.createElement('option');
      opt.value = v.id;
      opt.textContent = `🚗 ${v.plate_number || v.name} · ${v.brand} ${v.model}`;
      if (v.id === this.activeVehicleId) opt.selected = true;
      select.appendChild(opt);
    });

    this.syncActiveVehicleToForms();
    this.syncVehiclePowerUI();
    this.loadOverview();
    this.renderVehiclesList();
  },

  // 根据当前激活车辆的动力类型，智能控制油耗/电耗相关入口与看板卡片
  syncVehiclePowerUI() {
    const v = this.getActiveVehicle();
    if (!v) return;

    const isGas = v.power_type === 'gas';
    const isEv = v.power_type === 'ev';

    // 1. 桌面端顶部导航：纯燃油隐藏电耗，纯电隐藏油耗
    const deskFuel = document.getElementById('tab-btn-fuel');
    if (deskFuel) deskFuel.classList.toggle('hidden', isEv);

    const deskElec = document.getElementById('tab-btn-electricity');
    if (deskElec) deskElec.classList.toggle('hidden', isGas);

    // 2. 移动端底部导航
    const mobFuel = document.getElementById('m-tab-fuel');
    if (mobFuel) mobFuel.classList.toggle('hidden', isEv);

    const mobElec = document.getElementById('m-tab-electricity');
    if (mobElec) mobElec.classList.toggle('hidden', isGas);

    // 3. 首页大横幅快捷录入按钮
    const heroBtnFuel = document.getElementById('hero-btn-fuel');
    if (heroBtnFuel) heroBtnFuel.classList.toggle('hidden', isEv);

    const heroBtnElec = document.getElementById('hero-btn-elec');
    if (heroBtnElec) heroBtnElec.classList.toggle('hidden', isGas);

    // 4. 首页大横幅能耗指标
    const heroMetricFuel = document.getElementById('hero-metric-fuel');
    if (heroMetricFuel) heroMetricFuel.classList.toggle('hidden', isEv);

    const heroMetricElec = document.getElementById('hero-metric-elec');
    if (heroMetricElec) heroMetricElec.classList.toggle('hidden', isGas);

    const heroMetricsGrid = document.getElementById('hero-metrics-grid');
    if (heroMetricsGrid) {
      if (isGas || isEv) {
        heroMetricsGrid.className = 'grid grid-cols-1 sm:grid-cols-3 gap-4 mt-6 pt-6 border-t border-slate-700/60 relative z-10';
      } else {
        heroMetricsGrid.className = 'grid grid-cols-2 sm:grid-cols-4 gap-4 mt-6 pt-6 border-t border-slate-700/60 relative z-10';
      }
    }

    // 5. 首页分项总览卡片
    const cardFuel = document.getElementById('stat-card-fuel');
    if (cardFuel) cardFuel.classList.toggle('hidden', isEv);

    const cardElec = document.getElementById('stat-card-elec');
    if (cardElec) cardElec.classList.toggle('hidden', isGas);

    const statCardsGrid = document.getElementById('overview-stat-cards');
    if (statCardsGrid) {
      if (isGas || isEv) {
        statCardsGrid.className = 'grid grid-cols-1 md:grid-cols-2 gap-6';
      } else {
        statCardsGrid.className = 'grid grid-cols-1 md:grid-cols-3 gap-6';
      }
    }

    // 6. 能耗分析页面的区间过滤胶囊
    const eiFuel = document.getElementById('ei-filter-fuel');
    if (eiFuel) eiFuel.classList.toggle('hidden', isEv);

    const eiElec = document.getElementById('ei-filter-elec');
    if (eiElec) eiElec.classList.toggle('hidden', isGas);

    // 7. 历史明细页面的分类过滤胶囊
    const histFuel = document.getElementById('hist-filter-fuel');
    if (histFuel) histFuel.classList.toggle('hidden', isEv);

    const histElec = document.getElementById('hist-filter-elec');
    if (histElec) histElec.classList.toggle('hidden', isGas);
  },

  onVehicleChange(vid) {
    if (!vid) return;
    this.activeVehicleId = parseInt(vid);
    localStorage.setItem('car_tracker_active_vid', this.activeVehicleId);
    this.syncActiveVehicleToForms();
    this.syncVehiclePowerUI();

    const v = this.getActiveVehicle();
    if (v) {
      if (this.currentTab === 'electricity' && v.power_type === 'gas') {
        this.currentTab = 'overview';
      } else if (this.currentTab === 'fuel' && v.power_type === 'ev') {
        this.currentTab = 'overview';
      }
    }

    if (this.currentTab === 'overview') {
      this.loadOverview();
    } else if (this.currentTab === 'energy') {
      this.loadEnergyAnalysis();
    } else if (this.currentTab === 'history') {
      this.loadHistory();
    }
    this.showToast('已切换当前车辆', 'info');
  },

  getActiveVehicle() {
    return this.vehicles.find(v => v.id === this.activeVehicleId) || null;
  },

  async syncActiveVehicleToForms() {
    const v = this.getActiveVehicle();
    if (!v) return;

    const label = `${v.plate_number || v.name} · ${v.brand} ${v.model}`;

    const fuelCar = document.getElementById('fuel-car-display');
    if (fuelCar) fuelCar.value = label;

    const elecCar = document.getElementById('elec-car-display');
    if (elecCar) elecCar.value = label;

    const maintCar = document.getElementById('maint-car-display');
    if (maintCar) maintCar.value = label;

    // 同步上次里程展示与建议初始值（权威取最后一次录入的状态）
    const latestStatus = await this.getLatestVehicleStatus(v.id);
    const benchmarkMileage = (latestStatus && latestStatus.mileage !== undefined && latestStatus.mileage !== null)
      ? latestStatus.mileage
      : (v.current_mileage || v.initial_mileage || 0);

    const fuelPrev = document.getElementById('fuel-prev-mileage');
    if (fuelPrev) fuelPrev.textContent = this.formatThousands(benchmarkMileage);

    const elecPrev = document.getElementById('elec-prev-mileage');
    if (elecPrev) elecPrev.textContent = this.formatThousands(benchmarkMileage);

    const fuelMileage = document.getElementById('fuel-mileage');
    if (fuelMileage) {
      fuelMileage.placeholder = '当前基准 ' + this.formatThousands(benchmarkMileage);
      if (!fuelMileage.value || fuelMileage.dataset.isDefault === 'true') {
        fuelMileage.value = benchmarkMileage;
        fuelMileage.dataset.isDefault = 'true';
      }
      if (!fuelMileage.dataset.hasListener) {
        fuelMileage.addEventListener('input', () => {
          fuelMileage.dataset.isDefault = 'false';
        });
        fuelMileage.dataset.hasListener = 'true';
      }
    }

    const elecMileage = document.getElementById('elec-mileage');
    if (elecMileage) {
      elecMileage.placeholder = '当前基准 ' + this.formatThousands(benchmarkMileage);
      if (!elecMileage.value || elecMileage.dataset.isDefault === 'true') {
        elecMileage.value = benchmarkMileage;
        elecMileage.dataset.isDefault = 'true';
      }
      if (!elecMileage.dataset.hasListener) {
        elecMileage.addEventListener('input', () => {
          elecMileage.dataset.isDefault = 'false';
        });
        elecMileage.dataset.hasListener = 'true';
      }
    }

    // 1. 油耗表单：加前起步油量（默认为最后一次油量）并自动计算加后油量
    const fuelPreInput = document.getElementById('fuel-pre-percent');
    if (fuelPreInput) {
      const lastFuel = await this.getLastKnownFuelPercent(v.id);
      fuelPreInput.value = lastFuel;
      this.calcFuelPostPercent();
    }

    // 1.1 油耗表单：当前电量输入框（纯燃油车自动隐藏，混动/增程车默认带出最后一次电量）
    const fuelBatBox = document.getElementById('fuel-battery-percent-box');
    const fuelBatInput = document.getElementById('fuel-battery-percent');
    if (fuelBatBox) {
      if (v.power_type === 'gas') {
        fuelBatBox.classList.add('hidden');
      } else {
        fuelBatBox.classList.remove('hidden');
        if (fuelBatInput) {
          const lastSoc = await this.getLastKnownBatterySoc(v.id);
          fuelBatInput.value = (lastSoc !== null && lastSoc !== undefined) ? lastSoc : '';
        }
      }
    }

    // 1.2 油耗表单：油品标号（默认使用上次录入的值）
    const fuelTypeInput = document.getElementById('fuel-type');
    if (fuelTypeInput) {
      const lastFuelType = await this.getLastKnownFuelType(v.id);
      this.setFuelType(lastFuelType);
    }

    // 2. 电耗表单：起始电量（默认为最后一次电量）并自动推算结束电量
    const elecStartInput = document.getElementById('elec-start-soc');
    if (elecStartInput) {
      const lastSoc = await this.getLastKnownBatterySoc(v.id);
      elecStartInput.value = lastSoc;
      this.calcElecEndSoc();
    }

    // 3. 电耗表单：油量百分比输入框（纯电车自动隐藏，混动/增程车默认带出最后一次油量）
    const elecFuelBox = document.getElementById('elec-fuel-box');
    const elecFuelInput = document.getElementById('elec-fuel-percent');
    if (elecFuelBox) {
      if (v.power_type === 'ev') {
        elecFuelBox.classList.add('hidden');
      } else {
        elecFuelBox.classList.remove('hidden');
        if (elecFuelInput) {
          const lastFuel = await this.getLastKnownFuelPercent(v.id);
          elecFuelInput.value = lastFuel;
        }
      }
    }

    await this.extractStationsFromHistory();
  },

  // 加载并渲染看板概览
  async loadOverview() {
    if (!this.activeVehicleId) return;
    let stats = null;
    const baseUrl = window.CarSync ? await CarSync.getApiBaseUrl() : '';

    if (navigator.onLine) {
      try {
        const res = await fetch(`${baseUrl}/api/statistics?vehicle_id=${this.activeVehicleId}`);
        if (res.ok) {
          stats = await res.json();
        }
      } catch (err) {
        console.warn('云端统计加载异常，转由本地实时计算:', err);
      }
    }

    if (!stats || !stats.vehicle) {
      stats = await this.computeLocalStatistics(this.activeVehicleId);
    }

    if (!stats || !stats.vehicle) return;

    const v = stats.vehicle;

    // 顶部横幅
    const plateText = v.plate_number || v.name || '未登记车牌';
    document.getElementById('hero-name').textContent = plateText;
    document.getElementById('hero-model').textContent = `${v.brand} ${v.model} · ${this.getPowerTypeName(v.power_type)} ${v.displacement ? '· 排量 ' + v.displacement : ''}`;
    const heroPlate = document.getElementById('hero-plate');
    if (heroPlate) heroPlate.textContent = plateText;

    const badge = document.getElementById('hero-badge');
    badge.className = `vehicle-badge badge-${v.power_type}`;
    badge.textContent = this.getPowerTypeName(v.power_type);

    // 当前状态指标（里程、电量百分比、油量百分比）
    // 权威统一数据源：来自最后一次录入（当前状态/油耗/电耗）
    const latestStatus = await this.getLatestVehicleStatus(v.id);
    const currMileage = (latestStatus && latestStatus.mileage !== undefined && latestStatus.mileage !== null)
      ? latestStatus.mileage
      : (v.current_mileage || v.initial_mileage || 0);
    const currDrivenKm = Math.max(0, currMileage - parseFloat(v.initial_mileage || 0));

    const heroCurrentMileage = document.getElementById('hero-current-mileage');
    if (heroCurrentMileage) heroCurrentMileage.textContent = this.formatThousands(currMileage);
    const heroDrivenKm = document.getElementById('hero-driven-km');
    if (heroDrivenKm) heroDrivenKm.textContent = this.formatThousands(currDrivenKm);

    // 动态解析与展示当前电量百分比及油量百分比
    const isGasVehicle = v.power_type === 'gas';
    const isEvVehicle = v.power_type === 'ev';

    const heroStatusBatBox = document.getElementById('hero-status-battery-box');
    const heroStatusBat = document.getElementById('hero-status-battery');
    if (heroStatusBatBox) {
      if (isGasVehicle) {
        heroStatusBatBox.classList.add('hidden');
      } else {
        heroStatusBatBox.classList.remove('hidden');
        const currentSoc = (latestStatus && latestStatus.battery_percent !== null && latestStatus.battery_percent !== undefined)
          ? latestStatus.battery_percent
          : await this.getLastKnownBatterySoc(v.id);
        if (heroStatusBat) {
          heroStatusBat.textContent = (currentSoc !== null && currentSoc !== undefined && !isNaN(currentSoc))
            ? (Math.round(currentSoc) + '%')
            : '--%';
        }
      }
    }

    const heroStatusFuelBox = document.getElementById('hero-status-fuel-box');
    const heroStatusFuel = document.getElementById('hero-status-fuel');
    if (heroStatusFuelBox) {
      if (isEvVehicle) {
        heroStatusFuelBox.classList.add('hidden');
      } else {
        heroStatusFuelBox.classList.remove('hidden');
        const currentFuel = (latestStatus && latestStatus.fuel_percent !== null && latestStatus.fuel_percent !== undefined)
          ? latestStatus.fuel_percent
          : await this.getLastKnownFuelPercent(v.id);
        if (heroStatusFuel) {
          heroStatusFuel.textContent = (currentFuel !== null && currentFuel !== undefined && !isNaN(currentFuel))
            ? (Math.round(currentFuel) + '%')
            : '--%';
        }
      }
    }
    document.getElementById('hero-total-cost').textContent = this.formatThousands(stats.total_cost, 2);
    document.getElementById('hero-cost-per-km').textContent = this.formatThousands(stats.cost_per_km, 2);

    document.getElementById('hero-avg-fuel').textContent = this.formatThousands(stats.fuel.avg_per_hundred, 2);
    document.getElementById('hero-fuel-records-count').textContent = this.formatThousands(stats.fuel.record_count);

    document.getElementById('hero-avg-elec').textContent = this.formatThousands(stats.electricity.avg_per_hundred, 2);
    document.getElementById('hero-elec-records-count').textContent = this.formatThousands(stats.electricity.record_count);

    const equivElec = stats.electricity?.equivalent_avg_per_hundred || 0;
    const equivTotal = stats.electricity?.equivalent_total_consumed || 0;
    const heroEquivMetric = document.getElementById('hero-metric-equiv');
    const heroAvgEquivElec = document.getElementById('hero-avg-equiv-elec');
    const heroEquivTotalConsumed = document.getElementById('hero-equiv-total-consumed');
    const statEquivAvg = document.getElementById('stat-equiv-avg');
    const statElecEquivBox = document.getElementById('stat-elec-equiv-box');

    if (heroAvgEquivElec) heroAvgEquivElec.textContent = this.formatThousands(equivElec, 2);
    if (heroEquivTotalConsumed) heroEquivTotalConsumed.textContent = this.formatThousands(equivTotal, 1);
    if (statEquivAvg) statEquivAvg.textContent = this.formatThousands(equivElec, 2);

    const isDualPower = v.power_type === 'erev' || v.power_type === 'phev' || v.power_type === 'hybrid';
    if (heroEquivMetric) {
      if (isDualPower || stats.fuel?.record_count > 0) {
        heroEquivMetric.classList.remove('hidden');
      } else {
        heroEquivMetric.classList.add('hidden');
      }
    }
    if (statElecEquivBox) {
      if (isDualPower || stats.fuel?.record_count > 0) {
        statElecEquivBox.classList.remove('hidden');
      } else {
        statElecEquivBox.classList.add('hidden');
      }
    }

    const heroCostPerDay = document.getElementById('hero-cost-per-day');
    if (heroCostPerDay) {
      heroCostPerDay.textContent = this.formatThousands(stats.cost_per_day !== undefined ? stats.cost_per_day : 0, 2);
    }
    const heroCostSubtext = document.getElementById('hero-cost-subtext');
    if (heroCostSubtext && stats.days_owned) {
      heroCostSubtext.title = `累计统计 ${this.formatThousands(stats.days_owned)} 天，总支出 ¥${this.formatThousands(stats.total_cost, 2)}`;
    }

    // 平均每天里程
    const drivenKm = stats.total_driven_km || 0;
    const daysOwned = stats.days_owned || 1;
    const kmPerDay = stats.km_per_day !== undefined ? stats.km_per_day : (daysOwned > 0 ? (drivenKm / daysOwned) : 0);

    const heroKmPerDay = document.getElementById('hero-km-per-day');
    if (heroKmPerDay) heroKmPerDay.textContent = this.formatThousands(kmPerDay, 2);

    const heroDaysOwned = document.getElementById('hero-days-owned');
    if (heroDaysOwned) heroDaysOwned.textContent = this.formatThousands(daysOwned);

    const heroDaysOwnedSubtext = document.getElementById('hero-days-owned-subtext');
    if (heroDaysOwnedSubtext) {
      heroDaysOwnedSubtext.title = `自购车/登记至今已行驶 ${this.formatThousands(drivenKm, 1)} km，历时 ${this.formatThousands(daysOwned)} 天`;
    }

    // 概览看板新增指标：每公里能耗成本 (先进先出 FIFO 结算)
    const fifoCostKm = stats.energy_cost_per_km_fifo !== undefined ? stats.energy_cost_per_km_fifo : (stats.fifo_cost_per_km !== undefined ? stats.fifo_cost_per_km : ((stats.fifo_driven_km > 0 && stats.fifo_energy_cost) ? (stats.fifo_energy_cost / stats.fifo_driven_km) : 0));
    const fifoEnergyCost = stats.fifo_energy_cost !== undefined ? stats.fifo_energy_cost : 0;
    const fifoDrivenKm = stats.fifo_driven_km !== undefined ? stats.fifo_driven_km : 0;
    const fifoFuelCost = stats.fifo_fuel_cost !== undefined ? stats.fifo_fuel_cost : 0;
    const fifoElecCost = stats.fifo_elec_cost !== undefined ? stats.fifo_elec_cost : 0;

    // 平均每日能耗成本 (总支出日均与 FIFO 实耗日均)
    const totalEnergyCost = stats.total_energy_cost !== undefined ? stats.total_energy_cost : ((stats.fuel?.total_cost || 0) + (stats.electricity?.total_cost || 0));
    const energyCostPerDay = stats.energy_cost_per_day !== undefined ? stats.energy_cost_per_day : (daysOwned > 0 ? (totalEnergyCost / daysOwned) : 0);
    const fifoEnergyCostPerDay = stats.fifo_energy_cost_per_day !== undefined ? stats.fifo_energy_cost_per_day : (daysOwned > 0 ? (fifoEnergyCost / daysOwned) : 0);

    const heroEnergyCostPerDay = document.getElementById('hero-energy-cost-per-day');
    if (heroEnergyCostPerDay) heroEnergyCostPerDay.textContent = this.formatThousands(energyCostPerDay, 2);

    const heroFifoEnergyCostPerDay = document.getElementById('hero-fifo-energy-cost-per-day');
    if (heroFifoEnergyCostPerDay) heroFifoEnergyCostPerDay.textContent = this.formatThousands(fifoEnergyCostPerDay, 2);

    const heroMetricEnergyCostPerDay = document.getElementById('hero-metric-energy-cost-per-day');
    if (heroMetricEnergyCostPerDay) {
      heroMetricEnergyCostPerDay.title = `自购车以来的平均每日能耗费用支出：总能耗费用支出 ¥${this.formatThousands(totalEnergyCost, 2)}，购车 ${this.formatThousands(daysOwned)} 天，平均每日支出 ¥${this.formatThousands(energyCostPerDay, 2)}/天；先进先出 (FIFO) 实耗能源 ¥${this.formatThousands(fifoEnergyCost, 2)}，实耗日均 ¥${this.formatThousands(fifoEnergyCostPerDay, 2)}/天`;
    }

    const heroEnergyCost100 = document.getElementById('hero-energy-cost-per-hundred');
    const heroFifoEnergyCost = document.getElementById('hero-fifo-energy-cost');
    const heroFifoDrivenKm = document.getElementById('hero-fifo-driven-km');
    const heroMetricEnergyCost100 = document.getElementById('hero-metric-energy-cost-100');

    if (heroEnergyCost100) heroEnergyCost100.textContent = this.formatThousands(fifoCostKm, 2);
    if (heroFifoEnergyCost) heroFifoEnergyCost.textContent = this.formatThousands(fifoEnergyCost, 2);
    if (heroFifoDrivenKm) heroFifoDrivenKm.textContent = this.formatThousands(fifoDrivenKm, 1);
    if (heroMetricEnergyCost100) {
      heroMetricEnergyCost100.title = `先进先出 (FIFO) 核算：实耗油费 ¥${this.formatThousands(fifoFuelCost, 2)} + 实耗电费 ¥${this.formatThousands(fifoElecCost, 2)} = 实耗总能耗 ¥${this.formatThousands(fifoEnergyCost, 2)}，终极里程 ${this.formatThousands(fifoDrivenKm, 1)} km，每公里成本 ¥${this.formatThousands(fifoCostKm, 2)}/km`;
    }

    const statFuelFifoCost = document.getElementById('stat-fuel-fifo-cost');
    if (statFuelFifoCost) statFuelFifoCost.textContent = this.formatThousands(fifoFuelCost, 2);

    const statElecFifoCost = document.getElementById('stat-elec-fifo-cost');
    if (statElecFifoCost) statElecFifoCost.textContent = this.formatThousands(fifoElecCost, 2);

    // 计算平均加油成本与充电成本 (单价与次均)
    const fuelAmount = parseFloat(stats.fuel?.total_amount || 0);
    const fuelCost = parseFloat(stats.fuel?.total_cost !== undefined ? stats.fuel.total_cost : (stats.fuel?.total_fuel_cost || 0));
    const fuelCount = parseInt(stats.fuel?.record_count || 0);
    const avgFuelPrice = stats.fuel?.avg_unit_price !== undefined ? stats.fuel.avg_unit_price : (fuelAmount > 0 ? (fuelCost / fuelAmount) : 0);
    const avgFuelPerTime = stats.fuel?.avg_cost_per_time !== undefined ? stats.fuel.avg_cost_per_time : (fuelCount > 0 ? (fuelCost / fuelCount) : 0);

    const elecAmount = parseFloat(stats.electricity?.total_amount || 0);
    const elecCost = parseFloat(stats.electricity?.total_cost !== undefined ? stats.electricity.total_cost : (stats.electricity?.total_elec_cost || 0));
    const elecCount = parseInt(stats.electricity?.record_count || 0);
    const avgElecPrice = stats.electricity?.avg_unit_price !== undefined ? stats.electricity.avg_unit_price : (elecAmount > 0 ? (elecCost / elecAmount) : 0);
    const avgElecPerTime = stats.electricity?.avg_cost_per_time !== undefined ? stats.electricity.avg_cost_per_time : (elecCount > 0 ? (elecCost / elecCount) : 0);

    const heroAvgFuelPerTime = document.getElementById('hero-avg-fuel-cost-per-time');
    if (heroAvgFuelPerTime) heroAvgFuelPerTime.textContent = this.formatThousands(avgFuelPerTime, 2);

    const heroAvgElecPerTime = document.getElementById('hero-avg-elec-cost-per-time');
    if (heroAvgElecPerTime) heroAvgElecPerTime.textContent = this.formatThousands(avgElecPerTime, 2);

    // 细分统计卡片
    document.getElementById('stat-total-fuel').textContent = this.formatThousands(fuelAmount, 1);
    document.getElementById('stat-fuel-cost').textContent = this.formatThousands(fuelCost, 2);
    document.getElementById('stat-fuel-count').textContent = this.formatThousands(fuelCount);

    const statFuelAvgPrice = document.getElementById('stat-fuel-avg-price');
    if (statFuelAvgPrice) statFuelAvgPrice.textContent = this.formatThousands(avgFuelPrice, 2);
    const statFuelAvgPerTime = document.getElementById('stat-fuel-avg-per-time');
    if (statFuelAvgPerTime) statFuelAvgPerTime.textContent = this.formatThousands(avgFuelPerTime, 2);

    document.getElementById('stat-total-elec').textContent = this.formatThousands(elecAmount, 1);
    document.getElementById('stat-elec-cost').textContent = this.formatThousands(elecCost, 2);
    document.getElementById('stat-elec-count').textContent = this.formatThousands(elecCount);

    const statElecAvgPrice = document.getElementById('stat-elec-avg-price');
    if (statElecAvgPrice) statElecAvgPrice.textContent = this.formatThousands(avgElecPrice, 2);
    const statElecAvgPerTime = document.getElementById('stat-elec-avg-per-time');
    if (statElecAvgPerTime) statElecAvgPerTime.textContent = this.formatThousands(avgElecPerTime, 2);

    document.getElementById('stat-maint-cost').textContent = this.formatThousands(stats.maintenance.total_cost, 2);
    document.getElementById('stat-maint-count').textContent = this.formatThousands(stats.maintenance.record_count);

    // 从云端或本地补充各月维保支出的具体分类明细 (other_breakdown)
    let overviewMaints = [];
    if (navigator.onLine && baseUrl) {
      try {
        const mRes = await fetch(`${baseUrl}/api/records/maintenance?vehicle_id=${this.activeVehicleId}`);
        if (mRes.ok) {
          const list = await mRes.json();
          if (Array.isArray(list)) overviewMaints = list;
        }
      } catch (e) {
        console.warn('获取维保明细用于月度细分异常:', e);
      }
    }
    if (overviewMaints.length === 0 && window.CarDB && CarDB.db) {
      const allM = await CarDB.getAll('maintenance_records');
      overviewMaints = (allM || []).filter(r => r.vehicle_id == this.activeVehicleId);
    }
    const maintMonthMap = {};
    overviewMaints.forEach(r => {
      const d = r.record_date || r.maintenance_date;
      if (!d) return;
      const m = String(d).slice(0, 7);
      const item = (r.item_type || '其他').trim() || '其他';
      const cost = parseFloat(r.cost) || 0;
      if (!maintMonthMap[m]) maintMonthMap[m] = {};
      maintMonthMap[m][item] = parseFloat(((maintMonthMap[m][item] || 0) + cost).toFixed(2));
    });

    const monthlyExpenses = (stats.monthly_expenses || []).map(m => {
      const bd = maintMonthMap[m.month] || m.other_breakdown || {};
      return { ...m, other_breakdown: bd };
    });

    // 渲染按月查看费用堆叠柱状图
    this.renderMonthlyCostChart(monthlyExpenses);

    // 渲染各项总费用统计柱状图
    this.renderTotalCostChart(stats, overviewMaints);

    this.loadRecentActivities();
  },

  // 本地 IndexedDB 离线实时统计计算引擎
  async computeLocalStatistics(vehicleId) {
    if (!window.CarDB || !CarDB.db) return null;
    const vehicles = await CarDB.getAll('vehicles');
    const v = vehicles.find(item => item.id == vehicleId);
    if (!v) return null;

    const fuels = (await CarDB.getAll('fuel_records')).filter(r => r.vehicle_id == vehicleId);
    const elecs = (await CarDB.getAll('electricity_records')).filter(r => r.vehicle_id == vehicleId);
    const maints = (await CarDB.getAll('maintenance_records')).filter(r => r.vehicle_id == vehicleId);

    const initialMileage = parseFloat(v.initial_mileage || 0);
    const currentMileage = parseFloat(v.current_mileage || initialMileage);
    const totalDrivenKm = Math.max(0, currentMileage - initialMileage);

    const totalFuelAmount = fuels.reduce((acc, r) => acc + (parseFloat(r.fuel_amount) || 0), 0);
    const totalFuelCost = fuels.reduce((acc, r) => acc + (parseFloat(r.total_price) || 0), 0);

    const totalElecAmount = elecs.reduce((acc, r) => acc + (parseFloat(r.electricity_amount) || 0), 0);
    const totalElecCost = elecs.reduce((acc, r) => acc + (parseFloat(r.total_price) || 0), 0);

    // 累计百公里能耗计算：行程为最新补能里程减初始里程，能耗为所有能耗总和
    const ea = await this.computeLocalEnergyAnalysis(vehicleId);
    const avgFuel = ea?.summary?.fuel?.overall_avg || 0;
    const avgElec = ea?.summary?.electricity?.overall_avg || 0;

    const totalMaintCost = maints.reduce((acc, r) => acc + (parseFloat(r.cost) || 0), 0);
    const totalEnergyCost = totalFuelCost + totalElecCost;
    const totalCost = totalEnergyCost + totalMaintCost;
    const costPerKm = totalDrivenKm > 0 ? totalCost / totalDrivenKm : 0;
    const energyCostPerKm = totalDrivenKm > 0 ? totalEnergyCost / totalDrivenKm : 0;

    // 计算平均每天累计能耗与维护总开销费用
    let startDate = null;
    if (v.purchase_date) {
      const parts = v.purchase_date.split(' ')[0].split('-');
      if (parts.length === 3) {
        startDate = new Date(parseInt(parts[0]), parseInt(parts[1]) - 1, parseInt(parts[2]));
      }
    }
    if (!startDate) {
      const allDates = [
        ...fuels.map(r => r.record_date),
        ...elecs.map(r => r.record_date),
        ...maints.map(r => r.maintenance_date || r.record_date)
      ].filter(Boolean);
      if (allDates.length > 0) {
        allDates.sort();
        const parts = allDates[0].split(' ')[0].split('-');
        if (parts.length === 3) {
          startDate = new Date(parseInt(parts[0]), parseInt(parts[1]) - 1, parseInt(parts[2]));
        }
      } else if (v.created_at) {
        const parts = v.created_at.split(' ')[0].split('-');
        if (parts.length === 3) {
          startDate = new Date(parseInt(parts[0]), parseInt(parts[1]) - 1, parseInt(parts[2]));
        }
      }
    }

    const now = new Date();
    const today = new Date(now.getFullYear(), now.getMonth(), now.getDate());
    let daysOwned = 1;
    if (startDate) {
      const diffMs = today.getTime() - startDate.getTime();
      daysOwned = Math.max(1, Math.floor(diffMs / (1000 * 3600 * 24)));
    }
    const costPerDay = parseFloat((totalCost / daysOwned).toFixed(2));

    // 按月费用统计聚合 (燃油、电力、其他支出及总费用，细分支出类别明细)
    const monthlyMap = {};
    const addExpense = (dateStr, fuelC, elecC, otherC, itemType) => {
      if (!dateStr) return;
      const m = String(dateStr).slice(0, 7);
      if (!monthlyMap[m]) {
        monthlyMap[m] = { month: m, fuel_cost: 0, elec_cost: 0, other_cost: 0, total_cost: 0, other_breakdown: {} };
      }
      monthlyMap[m].fuel_cost = parseFloat((monthlyMap[m].fuel_cost + fuelC).toFixed(2));
      monthlyMap[m].elec_cost = parseFloat((monthlyMap[m].elec_cost + elecC).toFixed(2));
      monthlyMap[m].other_cost = parseFloat((monthlyMap[m].other_cost + otherC).toFixed(2));
      monthlyMap[m].total_cost = parseFloat((monthlyMap[m].fuel_cost + monthlyMap[m].elec_cost + monthlyMap[m].other_cost).toFixed(2));
      if (otherC !== 0) {
        const cat = (itemType || '其他').trim() || '其他';
        monthlyMap[m].other_breakdown[cat] = parseFloat(((monthlyMap[m].other_breakdown[cat] || 0) + otherC).toFixed(2));
      }
    };

    fuels.forEach(r => addExpense(r.record_date, parseFloat(r.total_price) || 0, 0, 0));
    elecs.forEach(r => addExpense(r.record_date, 0, parseFloat(r.total_price) || 0, 0));
    maints.forEach(r => addExpense(r.record_date || r.maintenance_date, 0, 0, parseFloat(r.cost) || 0, r.item_type));

    const monthlyExpenses = Object.keys(monthlyMap).sort().map(k => monthlyMap[k]);

    const fifoEnergyCost = ea?.summary?.fifo_energy_cost || 0.0;
    const fifoCostPerKm = ea?.summary?.fifo_cost_per_km !== undefined ? ea.summary.fifo_cost_per_km : 0.0;
    const fifoCostPerHundred = ea?.summary?.fifo_cost_per_hundred || 0.0;
    const fifoDrivenKm = ea?.summary?.fifo_driven_km || 0.0;
    const fifoFuelCost = ea?.summary?.fifo_fuel_cost || 0.0;
    const fifoElecCost = ea?.summary?.fifo_elec_cost || 0.0;
    const latestReplenishMileage = ea?.summary?.latest_replenish_mileage || currentMileage;

    const kmPerDay = totalDrivenKm > 0 && daysOwned > 0 ? parseFloat((totalDrivenKm / daysOwned).toFixed(2)) : 0.0;
    const energyCostPerDay = daysOwned > 0 ? parseFloat((totalEnergyCost / daysOwned).toFixed(2)) : 0.0;
    const fifoEnergyCostPerDay = daysOwned > 0 ? parseFloat((fifoEnergyCost / daysOwned).toFixed(2)) : 0.0;

    return {
      vehicle: v,
      total_driven_km: totalDrivenKm,
      total_cost: totalCost,
      total_energy_cost: totalEnergyCost,
      cost_per_km: costPerKm,
      energy_cost_per_km: energyCostPerKm,
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
        record_count: fuels.length,
        total_amount: totalFuelAmount,
        total_consumed: ea?.summary?.fuel?.total_consumed || 0,
        driven_km: totalDrivenKm,
        total_cost: totalFuelCost,
        avg_per_hundred: avgFuel,
        avg_unit_price: totalFuelAmount > 0 ? parseFloat((totalFuelCost / totalFuelAmount).toFixed(2)) : 0.0,
        avg_cost_per_time: fuels.length > 0 ? parseFloat((totalFuelCost / fuels.length).toFixed(2)) : 0.0
      },
      electricity: {
        record_count: elecs.length,
        total_amount: totalElecAmount,
        total_consumed: ea?.summary?.electricity?.total_consumed || 0,
        equivalent_total_consumed: ea?.summary?.electricity?.equivalent_total_consumed || 0,
        driven_km: totalDrivenKm,
        total_cost: totalElecCost,
        avg_per_hundred: avgElec,
        equivalent_avg_per_hundred: ea?.summary?.electricity?.equivalent_overall_avg || 0,
        avg_unit_price: totalElecAmount > 0 ? parseFloat((totalElecCost / totalElecAmount).toFixed(2)) : 0.0,
        avg_cost_per_time: elecs.length > 0 ? parseFloat((totalElecCost / elecs.length).toFixed(2)) : 0.0
      },
      maintenance: {
        record_count: maints.length,
        total_cost: totalMaintCost
      }
    };
  },

  // 加载最近活动记录 (离线优先)
  async loadRecentActivities() {
    if (!this.activeVehicleId) return;
    const listEl = document.getElementById('recent-activities-list');
    listEl.innerHTML = '<div class="text-sm text-slate-400 py-2">加载动态中...</div>';

    let fuels = [];
    let elecs = [];
    let maints = [];
    const baseUrl = window.CarSync ? await CarSync.getApiBaseUrl() : '';

    if (navigator.onLine) {
      try {
        const [fuelRes, elecRes, maintRes] = await Promise.all([
          fetch(`${baseUrl}/api/records/fuel?vehicle_id=${this.activeVehicleId}`),
          fetch(`${baseUrl}/api/records/electricity?vehicle_id=${this.activeVehicleId}`),
          fetch(`${baseUrl}/api/records/maintenance?vehicle_id=${this.activeVehicleId}`)
        ]);
        if (fuelRes.ok && elecRes.ok && maintRes.ok) {
          fuels = await fuelRes.json();
          elecs = await elecRes.json();
          maints = await maintRes.json();
        }
      } catch (err) {
        console.warn('云端记录加载异常，从本地 IndexedDB 加载:', err);
      }
    }

    if (fuels.length === 0 && elecs.length === 0 && maints.length === 0 && window.CarDB && CarDB.db) {
      const allF = await CarDB.getAll('fuel_records');
      const allE = await CarDB.getAll('electricity_records');
      const allM = await CarDB.getAll('maintenance_records');
      fuels = allF.filter(r => r.vehicle_id == this.activeVehicleId);
      elecs = allE.filter(r => r.vehicle_id == this.activeVehicleId);
      maints = allM.filter(r => r.vehicle_id == this.activeVehicleId);
    }

    const fMapped = fuels.map(r => ({ ...r, _type: 'fuel', _time: r.record_date }));
    const eMapped = elecs.map(r => ({ ...r, _type: 'electricity', _time: r.record_date }));
    const mMapped = maints.map(r => ({ ...r, _type: 'maintenance', _time: r.record_date }));

    const all = [...fMapped, ...eMapped, ...mMapped].sort((a, b) => (b._time > a._time ? 1 : -1)).slice(0, 5);

    if (all.length === 0) {
      listEl.innerHTML = `
        <div class="text-center py-6 text-slate-400 text-sm">
          本车辆暂无能耗或维保记录，点击上方按钮立即开始记一笔吧！
        </div>
      `;
      return;
    }

    listEl.innerHTML = all.map(item => {
      if (item._type === 'fuel') {
        return `
          <div class="flex items-center justify-between p-3 rounded-xl bg-slate-50 hover:bg-slate-100 transition border border-slate-100">
            <div class="flex items-center space-x-3">
              <div class="w-9 h-9 rounded-lg bg-amber-100 text-amber-600 flex items-center justify-center font-bold text-sm">⛽</div>
              <div>
                <div class="text-sm font-semibold text-slate-900">加油 ${this.formatThousands(item.fuel_amount)} L · ${item.fuel_type}</div>
                <div class="text-xs text-slate-400">${item.record_date} · 里程: ${this.formatThousands(item.mileage)} km · 单价: ¥${this.formatThousands(item.unit_price)}/L</div>
              </div>
            </div>
            <div class="text-right">
              <span class="text-sm font-bold text-slate-900">¥${this.formatThousands(item.total_price, 2)}</span>
              <span class="block text-[10px] text-amber-600 font-medium">${item.is_full ? '已加满' : '未加满'}</span>
            </div>
          </div>
        `;
      } else if (item._type === 'electricity') {
        return `
          <div class="flex items-center justify-between p-3 rounded-xl bg-slate-50 hover:bg-slate-100 transition border border-slate-100">
            <div class="flex items-center space-x-3">
              <div class="w-9 h-9 rounded-lg bg-emerald-100 text-emerald-600 flex items-center justify-center font-bold text-sm">⚡</div>
                <div class="text-sm font-semibold text-slate-900 flex items-center gap-1.5 flex-wrap">
                  <span>充电 ${this.formatThousands(item.electricity_amount)} 度 · ${item.charging_type}</span>
                  ${item.is_calibrated ? `<span class="text-[10px] font-bold text-teal-700 bg-teal-100/70 border border-teal-200/80 px-1.5 py-0.2 rounded">🔋 校准: ${this.formatThousands(item.calibrated_capacity)} kWh</span>` : ''}
                </div>
                <div class="text-xs text-slate-400">${item.record_date} · 里程: ${this.formatThousands(item.mileage)} km · 单价: ¥${this.formatThousands(item.unit_price)}/度</div>
              </div>
            </div>
            <div class="text-right">
              <span class="text-sm font-bold text-slate-900">¥${this.formatThousands(item.total_price, 2)}</span>
              <span class="block text-[10px] text-emerald-600 font-medium">${item.station || '电桩充电'}</span>
            </div>
          </div>
        `;
      } else {
        return `
          <div class="flex items-center justify-between p-3 rounded-xl bg-slate-50 hover:bg-slate-100 transition border border-slate-100">
            <div class="flex items-center space-x-3">
              <div class="w-9 h-9 rounded-lg bg-indigo-100 text-indigo-600 flex items-center justify-center font-bold text-sm">💰</div>
              <div>
                <div class="text-sm font-semibold text-slate-900">${item.item_type}</div>
                <div class="text-xs text-slate-400">${item.record_date}${item.notes ? ' · ' + item.notes : ''}</div>
              </div>
            </div>
            <div class="text-right">
              <span class="text-sm font-bold ${item.cost < 0 ? 'text-emerald-600' : 'text-slate-900'}">${item.cost < 0 ? '-¥' + this.formatThousands(Math.abs(item.cost), 2) : '¥' + this.formatThousands(item.cost, 2)}</span>
              <span class="block text-[10px] ${item.cost < 0 ? 'text-emerald-600 font-semibold' : 'text-indigo-600'} font-medium">${item.cost < 0 ? '退款/冲减' : '支出'}</span>
            </div>
          </div>
        `;
      }
    }).join('');
  },

  // 提交油耗记录
  async submitFuelRecord(e) {
    e.preventDefault();
    if (!this.activeVehicleId) {
      this.showToast('请先选择或录入车辆', 'error');
      return;
    }

    const mileage = parseFloat(document.getElementById('fuel-mileage').value);
    const amount = parseFloat(document.getElementById('fuel-amount').value);
    const totalPrice = parseFloat(document.getElementById('fuel-total-price').value);
    let unitPrice = parseFloat(document.getElementById('fuel-unit-price').value);

    // 容错补全：若填写了总价和升数，自动补充单价
    if (isNaN(unitPrice) && !isNaN(totalPrice) && !isNaN(amount) && amount > 0) {
      unitPrice = parseFloat((totalPrice / amount).toFixed(3));
      document.getElementById('fuel-unit-price').value = unitPrice;
    }

    const fuelType = document.getElementById('fuel-type').value.trim();
    const isFull = document.querySelector('input[name="fuel-is-full"]:checked').value;
    const station = document.getElementById('fuel-station').value.trim();
    const recordDate = document.getElementById('fuel-date').value.replace('T', ' ');
    const notes = document.getElementById('fuel-notes').value.trim();

    if (isNaN(amount) || isNaN(unitPrice) || isNaN(totalPrice)) {
      this.showToast('实付总价、加油量和单价需全部就绪（实付总价 = 加油量 × 单价）', 'error');
      return;
    }

    const v = this.getActiveVehicle();
    if (v && mileage < v.current_mileage) {
      if (!confirm(`录入里程 (${this.formatThousands(mileage)} km) 小于车辆已知里程 (${this.formatThousands(v.current_mileage)} km)，是否仍确认保存？`)) {
        return;
      }
    }

    const preFuelPercent = parseFloat(document.getElementById('fuel-pre-percent')?.value);
    const postFuelPercent = parseFloat(document.getElementById('fuel-post-percent')?.value) || 100;
    const fuelBatPercentVal = document.getElementById('fuel-battery-percent')?.value;
    const fuelBatPercent = (fuelBatPercentVal !== undefined && fuelBatPercentVal !== null && fuelBatPercentVal !== '' && !isNaN(parseFloat(fuelBatPercentVal))) ? parseFloat(fuelBatPercentVal) : null;

    const payload = {
      vehicle_id: this.activeVehicleId,
      mileage,
      fuel_amount: amount,
      unit_price: unitPrice,
      total_price: totalPrice,
      fuel_type: fuelType,
      is_full: parseInt(isFull),
      start_fuel_percent: !isNaN(preFuelPercent) ? preFuelPercent : null,
      fuel_percent: postFuelPercent,
      battery_percent: fuelBatPercent,
      station,
      record_date: recordDate,
      notes
    };

    // 离线优先：存入本地 IndexedDB
    const tempId = Date.now();
    const localRecord = { id: tempId, ...payload, created_at: new Date().toISOString() };

    if (window.CarDB && CarDB.db) {
      await CarDB.put('fuel_records', localRecord);
      if (v && mileage >= (v.current_mileage || 0)) {
        v.current_mileage = mileage;
        v.current_fuel_percent = postFuelPercent;
        if (fuelBatPercent !== null && !isNaN(fuelBatPercent)) v.current_battery_percent = fuelBatPercent;
        await CarDB.put('vehicles', v);
      }
      await CarDB.enqueueSync('CREATE', 'fuel', payload);
    }

    if (station) {
      this.addFuelStation(station);
    }

    // 记忆本次油品标号供下次默认使用
    if (fuelType) {
      if (this.activeVehicleId) {
        localStorage.setItem(`car_last_fuel_type_${this.activeVehicleId}`, fuelType);
      }
      localStorage.setItem('car_last_fuel_type_common', fuelType);
    }

    this.showToast('✅ 油耗记录已保存！(本地已缓存)', 'success');
    this.resetTriCalc('fuel');
    document.getElementById('fuel-mileage').value = '';
    document.getElementById('fuel-station').value = '';
    const clearBtn = document.getElementById('fuel-station-clear-btn');
    if (clearBtn) clearBtn.classList.add('hidden');
    this.closeFuelStationDropdown();
    await this.extractFuelStationsHistory();
    this.renderFuelStationPills();
    document.getElementById('fuel-notes').value = '';

    // 若联网则触发静默同步
    if (navigator.onLine && window.CarSync) {
      CarSync.sync(true);
    }

    await this.loadVehicles();
    this.switchTab('overview');
  },

  // 提交电耗记录
  async submitElectricityRecord(e) {
    e.preventDefault();
    if (!this.activeVehicleId) {
      this.showToast('请先选择或录入车辆', 'error');
      return;
    }

    const mileage = parseFloat(document.getElementById('elec-mileage').value);
    const amount = parseFloat(document.getElementById('elec-amount').value);
    const totalPrice = parseFloat(document.getElementById('elec-total-price').value);
    let unitPrice = parseFloat(document.getElementById('elec-unit-price').value);

    // 容错补全：若填写了总价和度数，自动补充单价
    if (isNaN(unitPrice) && !isNaN(totalPrice) && !isNaN(amount) && amount > 0) {
      unitPrice = parseFloat((totalPrice / amount).toFixed(3));
      document.getElementById('elec-unit-price').value = unitPrice;
    }

    const chargingType = document.getElementById('elec-charging-type').value.trim();
    const startSoc = document.getElementById('elec-start-soc').value;
    const endSoc = document.getElementById('elec-end-soc').value;
    const station = document.getElementById('elec-station').value.trim();
    const recordDate = document.getElementById('elec-date').value.replace('T', ' ');
    const notes = document.getElementById('elec-notes').value.trim();

    if (isNaN(amount) || isNaN(unitPrice) || isNaN(totalPrice)) {
      this.showToast('实付总价、加电量和单价需全部就绪（实付总价 = 加电量 × 单价）', 'error');
      return;
    }

    const v = this.getActiveVehicle();
    if (v && mileage < v.current_mileage) {
      if (!confirm(`录入里程 (${this.formatThousands(mileage)} km) 小于车辆已知里程 (${this.formatThousands(v.current_mileage)} km)，是否仍确认保存？`)) {
        return;
      }
    }

    let fuelPercent = null;
    if (v && v.power_type !== 'ev') {
      const fpVal = document.getElementById('elec-fuel-percent')?.value;
      if (fpVal !== undefined && fpVal !== '') {
        fuelPercent = parseFloat(fpVal);
      }
    }

    const isCalibrated = document.getElementById('elec-battery-calibrate-toggle')?.checked ? 1 : 0;
    let calibratedCapacity = null;
    if (isCalibrated) {
      const parsedEndSoc = parseFloat(endSoc);
      const parsedStartSoc = parseFloat(startSoc);
      if (!isNaN(parsedEndSoc) && !isNaN(parsedStartSoc) && parsedEndSoc > parsedStartSoc) {
        calibratedCapacity = Math.round((amount / (parsedEndSoc - parsedStartSoc)) * 10000) / 100;
      }
    }

    const payload = {
      vehicle_id: this.activeVehicleId,
      mileage,
      electricity_amount: amount,
      unit_price: unitPrice,
      total_price: totalPrice,
      charging_type: chargingType,
      start_soc: startSoc !== '' ? parseFloat(startSoc) : null,
      end_soc: endSoc !== '' ? parseFloat(endSoc) : null,
      fuel_percent: fuelPercent,
      station,
      is_calibrated: isCalibrated,
      calibrated_capacity: calibratedCapacity,
      record_date: recordDate,
      notes
    };

    // 离线优先：存入本地 IndexedDB
    const tempId = Date.now();
    const localRecord = { id: tempId, ...payload, created_at: new Date().toISOString() };

    if (window.CarDB && CarDB.db) {
      await CarDB.put('electricity_records', localRecord);
      if (v && mileage >= (v.current_mileage || 0)) {
        v.current_mileage = mileage;
        if (endSoc !== null && endSoc !== undefined && !isNaN(endSoc)) v.current_battery_percent = endSoc;
        if (fuelPercent !== null && fuelPercent !== undefined && !isNaN(fuelPercent)) v.current_fuel_percent = fuelPercent;
        await CarDB.put('vehicles', v);
      }
      await CarDB.enqueueSync('CREATE', 'electricity', payload);
    }

    if (station) {
      this.addElecStation(station);
    }

    this.showToast('⚡ 电耗记录已保存！(本地已缓存)', 'success');
    this.resetTriCalc('elec');
    document.getElementById('elec-mileage').value = '';
    document.getElementById('elec-station').value = '';
    const clearBtn = document.getElementById('elec-station-clear-btn');
    if (clearBtn) clearBtn.classList.add('hidden');
    this.closeElecStationDropdown();
    await this.extractElecStationsHistory();
    this.renderElecStationPills();
    document.getElementById('elec-notes').value = '';
    document.getElementById('elec-start-soc').value = '';
    document.getElementById('elec-end-soc').value = '';
    const calToggle = document.getElementById('elec-battery-calibrate-toggle');
    if (calToggle) calToggle.checked = false;
    this.toggleBatteryCalibration(false);
    this.updateSocBar();

    if (navigator.onLine && window.CarSync) {
      CarSync.sync(true);
    }

    await this.loadVehicles();
    this.switchTab('overview');
  },

  // 切换费用支出输入框的正负符号（适配移动端数字键盘无负号的问题）
  toggleCostSign(inputId) {
    const input = document.getElementById(inputId);
    if (!input) return;
    let val = input.value.trim();
    if (!val || val === '-') {
      input.value = val === '-' ? '' : '-';
    } else if (val.startsWith('-')) {
      input.value = val.slice(1);
    } else {
      input.value = '-' + val;
    }
    this.updateCostSignButton(inputId);
    input.focus();
    input.dispatchEvent(new Event('input', { bubbles: true }));
  },

  // 动态更新正负切换按钮的展示状态（负数呈现绿色高亮）
  updateCostSignButton(inputId) {
    const input = document.getElementById(inputId);
    const btn = document.getElementById(`btn-toggle-sign-${inputId}`);
    if (!input || !btn) return;
    const val = input.value.trim();
    if (val.startsWith('-')) {
      btn.className = 'absolute right-1.5 px-2.5 py-1.5 text-xs font-bold rounded-lg border transition shadow-sm bg-emerald-100 hover:bg-emerald-200 text-emerald-800 border-emerald-300 active:scale-95';
      btn.innerHTML = '➖ 负数冲减';
    } else {
      btn.className = 'absolute right-1.5 px-2.5 py-1.5 text-xs font-bold rounded-lg border transition shadow-sm bg-slate-100 hover:bg-slate-200 text-slate-700 border-slate-200 active:scale-95';
      btn.innerHTML = '± 正/负切换';
    }
  },

  // 提交费用支出记录
  async submitMaintenanceRecord(e) {
    e.preventDefault();
    if (!this.activeVehicleId) {
      this.showToast('请先选择或录入车辆', 'error');
      return;
    }

    const itemInput = document.getElementById('maint-item-type');
    const itemType = itemInput ? itemInput.value.trim() : '';
    if (!itemType) {
      this.showToast('请填写或选择支出项目', 'error');
      if (itemInput) itemInput.focus();
      return;
    }

    const costInput = document.getElementById('maint-cost');
    const rawCost = costInput ? costInput.value.trim() : '';
    if (rawCost === '' || rawCost === '-' || isNaN(parseFloat(rawCost))) {
      this.showToast('请输入有效的支出金额 (支持正数或负数冲减)', 'error');
      if (costInput) costInput.focus();
      return;
    }
    const cost = parseFloat(rawCost);
    const rawMaintDate = document.getElementById('maint-date').value;
    const recordDate = rawMaintDate ? rawMaintDate.replace('T', ' ') : new Date(Date.now() - new Date().getTimezoneOffset() * 60000).toISOString().slice(0, 16).replace('T', ' ');
    const notes = document.getElementById('maint-notes') ? document.getElementById('maint-notes').value.trim() : '';

    // 新项目自动加入常用列表并持久化保存
    this.addExpenseItem(itemType);

    // 获取当前车辆当前里程作为记录关联里程
    const activeCar = this.vehicles.find(v => v.id === this.activeVehicleId);
    const mileage = activeCar ? (parseFloat(activeCar.current_mileage) || 0) : 0;

    const payload = {
      vehicle_id: this.activeVehicleId,
      mileage,
      item_type: itemType,
      cost,
      provider: '',
      record_date: recordDate,
      next_mileage_reminder: null,
      next_date_reminder: '',
      notes
    };

    // 离线优先：存入本地 IndexedDB
    const tempId = Date.now();
    const localRecord = { id: tempId, ...payload, created_at: new Date().toISOString() };

    if (window.CarDB && CarDB.db) {
      await CarDB.put('maintenance_records', localRecord);
      await CarDB.enqueueSync('CREATE', 'maintenance', payload);
    }

    this.showToast('💰 费用支出记录已保存！(本地已缓存)', 'success');
    document.getElementById('maint-form').reset();
    this.updateCostSignButton('maint-cost');
    if (itemInput) itemInput.value = '';
    this.renderExpenseItemsPills();
    this.initDateTimeInputs();
    this.syncActiveVehicleToForms();

    if (navigator.onLine && window.CarSync) {
      CarSync.sync(true);
    }

    await this.loadVehicles();
    this.switchTab('overview');
  },


  // 历史明细加载与渲染 (离线优先)
  async loadHistory() {
    if (!this.activeVehicleId) return;
    const container = document.getElementById('history-container');
    container.innerHTML = '<div class="text-center py-8 text-slate-400 text-sm">正在加载明细数据...</div>';

    let fuels = [];
    let elecs = [];
    let maints = [];
    let statuses = [];
    const baseUrl = window.CarSync ? await CarSync.getApiBaseUrl() : '';

    if (navigator.onLine) {
      try {
        const [fuelRes, elecRes, maintRes, statusRes] = await Promise.all([
          fetch(`${baseUrl}/api/records/fuel?vehicle_id=${this.activeVehicleId}`).catch(() => null),
          fetch(`${baseUrl}/api/records/electricity?vehicle_id=${this.activeVehicleId}`).catch(() => null),
          fetch(`${baseUrl}/api/records/maintenance?vehicle_id=${this.activeVehicleId}`).catch(() => null),
          fetch(`${baseUrl}/api/records/status?vehicle_id=${this.activeVehicleId}`).catch(() => null)
        ]);

        if (fuelRes && fuelRes.ok) {
          const resFuels = await fuelRes.json();
          if (Array.isArray(resFuels)) fuels = resFuels;
        }
        if (elecRes && elecRes.ok) {
          const resElecs = await elecRes.json();
          if (Array.isArray(resElecs)) elecs = resElecs;
        }
        if (maintRes && maintRes.ok) {
          const resMaints = await maintRes.json();
          if (Array.isArray(resMaints)) maints = resMaints;
        }
        if (statusRes && statusRes.ok) {
          const resStatuses = await statusRes.json();
          if (Array.isArray(resStatuses)) statuses = resStatuses;
        }
      } catch (err) {
        console.warn('云端历史加载异常，转由本地 IndexedDB 呈现:', err);
      }
    }

    // 本地 IndexedDB 兜底与合并 (若云端未返回或返回空，或存在离线新录入数据，从本地同步合入)
    if (window.CarDB && CarDB.db) {
      const allF = await CarDB.getAll('fuel_records');
      const allE = await CarDB.getAll('electricity_records');
      const allM = await CarDB.getAll('maintenance_records');
      const allS = await CarDB.getAll('status_records');

      const localF = allF.filter(r => r.vehicle_id == this.activeVehicleId);
      const localE = allE.filter(r => r.vehicle_id == this.activeVehicleId);
      const localM = allM.filter(r => r.vehicle_id == this.activeVehicleId);
      const localS = (allS || []).filter(r => r.vehicle_id == this.activeVehicleId);

      if (fuels.length === 0) fuels = localF;
      else {
        // 合并本地可能存在但尚未上报的记录
        const ids = new Set(fuels.map(r => String(r.id)));
        localF.forEach(r => { if (!ids.has(String(r.id))) fuels.push(r); });
      }

      if (elecs.length === 0) elecs = localE;
      else {
        const ids = new Set(elecs.map(r => String(r.id)));
        localE.forEach(r => { if (!ids.has(String(r.id))) elecs.push(r); });
      }

      if (maints.length === 0) maints = localM;
      else {
        const ids = new Set(maints.map(r => String(r.id)));
        localM.forEach(r => { if (!ids.has(String(r.id))) maints.push(r); });
      }

      if (statuses.length === 0) statuses = localS;
      else {
        const ids = new Set(statuses.map(r => String(r.id)));
        localS.forEach(r => { if (!ids.has(String(r.id))) statuses.push(r); });
      }
    }

    fuels.forEach(r => {
      const s = (r.station || '').trim();
      if (s) this.addFuelStation(s);
    });
    elecs.forEach(r => {
      const s = (r.station || '').trim();
      if (s) this.addElecStation(s);
    });

    const fMapped = fuels.map(r => ({ ...r, _category: 'fuel', _time: r.record_date }));
    const eMapped = elecs.map(r => ({ ...r, _category: 'elec', _time: r.record_date }));
    const mMapped = maints.map(r => ({ ...r, _category: 'maint', _time: r.record_date }));
    const sMapped = statuses.map(r => ({ ...r, _category: 'status', _time: r.record_date }));

    let records = [...fMapped, ...eMapped, ...mMapped, ...sMapped].sort((a, b) => (b._time > a._time ? 1 : -1));

    if (this.historyFilter !== 'all') {
      records = records.filter(r => r._category === this.historyFilter);
    }

    if (records.length === 0) {
      container.innerHTML = `
        <div class="text-center py-12 text-slate-400 text-sm">
          <p class="text-base font-semibold text-slate-600 mb-1">暂无相关历史记录</p>
          <p>请点击顶部标签录入油耗、电耗或保养数据</p>
        </div>
      `;
      return;
    }

    container.innerHTML = `
      <table class="w-full text-left text-sm border-collapse">
        <thead>
          <tr class="border-b border-slate-200 text-slate-400 text-xs uppercase bg-slate-50/50">
            <th class="py-3 px-3">类型</th>
            <th class="py-3 px-3">时间 / 里程</th>
            <th class="py-3 px-3">详情内容</th>
            <th class="py-3 px-3">单价 / 规格</th>
            <th class="py-3 px-3 text-right">花费总额</th>
            <th class="py-3 px-3 text-center">操作</th>
          </tr>
        </thead>
        <tbody class="divide-y divide-slate-100">
          ${records.map(rec => {
            if (rec._category === 'status') {
              const details = [];
              if (rec.battery_percent !== null && rec.battery_percent !== undefined) {
                details.push(`⚡ 电量 ${rec.battery_percent}%`);
              }
              if (rec.fuel_percent !== null && rec.fuel_percent !== undefined) {
                details.push(`⛽ 油量 ${rec.fuel_percent}%`);
              }
              const detailText = details.length > 0 ? details.join(' · ') : '状态打卡';

              return `
                <tr class="hover:bg-blue-50/30 transition">
                  <td class="py-3 px-3">
                    <span class="inline-flex items-center px-2 py-0.5 rounded text-xs font-semibold bg-blue-100 text-blue-800">📊 状态</span>
                  </td>
                  <td class="py-3 px-3">
                    <div class="font-medium text-slate-900">${rec.record_date}</div>
                    <div class="text-xs text-slate-400 font-mono">${this.formatThousands(rec.mileage)} km</div>
                  </td>
                  <td class="py-3 px-3">
                    <div class="font-semibold text-slate-800">${detailText}</div>
                    <div class="text-xs text-slate-400">当前表显快照</div>
                  </td>
                  <td class="py-3 px-3">
                    <div class="text-xs text-slate-500">${rec.notes || '-'}</div>
                  </td>
                  <td class="py-3 px-3 text-right">
                    <span class="text-xs text-slate-400 font-medium">--</span>
                  </td>
                  <td class="py-3 px-3 text-center">
                    <div class="flex items-center justify-center space-x-1">
                      <button onclick="App.openRecordEditModal('status', '${rec.id}')" class="text-slate-400 hover:text-blue-600 p-1 transition" title="编辑记录">
                        <svg class="w-4 h-4 inline" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M11 5H6a2 2 0 00-2 2v11a2 2 0 002 2h11a2 2 0 002-2v-5m-1.414-9.414a2 2 0 112.828 2.828L11.828 15H9v-2.828l8.586-8.586z"></path></svg>
                      </button>
                      <button onclick="App.deleteRecord('status', '${rec.id}')" class="text-slate-400 hover:text-rose-600 p-1 transition" title="删除记录">
                        <svg class="w-4 h-4 inline" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M19 7l-.867 12.142A2 2 0 0116.138 21H7.862a2 2 0 01-1.995-1.858L5 7m5 4v6m4-6v6m1-10V4a1 1 0 00-1-1h-4a1 1 0 00-1 1v3M4 7h16"></path></svg>
                      </button>
                    </div>
                  </td>
                </tr>
              `;
            } else if (rec._category === 'fuel') {
              return `
                <tr class="hover:bg-amber-50/30 transition">
                  <td class="py-3 px-3">
                    <span class="inline-flex items-center px-2 py-0.5 rounded text-xs font-semibold bg-amber-100 text-amber-800">⛽ 加油</span>
                  </td>
                  <td class="py-3 px-3">
                    <div class="font-medium text-slate-900">${rec.record_date}</div>
                    <div class="text-xs text-slate-400 font-mono">${this.formatThousands(rec.mileage)} km</div>
                  </td>
                  <td class="py-3 px-3">
                    <div class="font-semibold text-slate-800">${this.formatThousands(rec.fuel_amount)} 升 · ${rec.fuel_type}</div>
                    <div class="text-xs text-slate-400">${rec.station || '未填油站'} ${rec.is_full ? '(加满)' : ''}${(rec.battery_percent !== null && rec.battery_percent !== undefined) ? ` · ⚡${rec.battery_percent}%` : ''}</div>
                  </td>
                  <td class="py-3 px-3">
                    <div class="text-xs font-mono text-slate-600">¥${this.formatThousands(rec.unit_price)} / 升</div>
                    ${rec.notes ? `<div class="text-[11px] text-slate-400 truncate max-w-xs">${rec.notes}</div>` : ''}
                  </td>
                  <td class="py-3 px-3 text-right">
                    <span class="text-base font-bold text-amber-600">¥${this.formatThousands(rec.total_price, 2)}</span>
                  </td>
                  <td class="py-3 px-3 text-center">
                    <div class="flex items-center justify-center space-x-1">
                      <button onclick="App.openRecordEditModal('fuel', '${rec.id}')" class="text-slate-400 hover:text-blue-600 p-1 transition" title="编辑记录">
                        <svg class="w-4 h-4 inline" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M11 5H6a2 2 0 00-2 2v11a2 2 0 002 2h11a2 2 0 002-2v-5m-1.414-9.414a2 2 0 112.828 2.828L11.828 15H9v-2.828l8.586-8.586z"></path></svg>
                      </button>
                      <button onclick="App.deleteRecord('fuel', '${rec.id}')" class="text-slate-400 hover:text-rose-600 p-1 transition" title="删除记录">
                        <svg class="w-4 h-4 inline" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M19 7l-.867 12.142A2 2 0 0116.138 21H7.862a2 2 0 01-1.995-1.858L5 7m5 4v6m4-6v6m1-10V4a1 1 0 00-1-1h-4a1 1 0 00-1 1v3M4 7h16"></path></svg>
                      </button>
                    </div>
                  </td>
                </tr>
              `;
            } else if (rec._category === 'elec') {
              return `
                <tr class="hover:bg-emerald-50/30 transition">
                  <td class="py-3 px-3">
                    <span class="inline-flex items-center px-2 py-0.5 rounded text-xs font-semibold bg-emerald-100 text-emerald-800">⚡ 充电</span>
                  </td>
                  <td class="py-3 px-3">
                    <div class="font-medium text-slate-900">${rec.record_date}</div>
                    <div class="text-xs text-slate-400 font-mono">${this.formatThousands(rec.mileage)} km</div>
                  </td>
                  <td class="py-3 px-3">
                    <div class="font-semibold text-slate-800 flex items-center gap-1.5 flex-wrap">
                      <span>${this.formatThousands(rec.electricity_amount)} 度 · ${rec.charging_type}</span>
                      ${rec.is_calibrated ? `<span class="inline-flex items-center px-1.5 py-0.2 rounded text-[10px] font-bold bg-teal-100 text-teal-800 border border-teal-200">🔋 矫正容量: ${this.formatThousands(rec.calibrated_capacity)} kWh</span>` : ''}
                    </div>
                    <div class="text-xs text-slate-400">${rec.station || '公共/家充'} ${rec.start_soc !== null && rec.end_soc !== null ? `(${rec.start_soc}% ➔ ${rec.end_soc}%)` : ''}</div>
                  </td>
                  <td class="py-3 px-3">
                    <div class="text-xs font-mono text-slate-600">¥${this.formatThousands(rec.unit_price)} / 度</div>
                    ${rec.notes ? `<div class="text-[11px] text-slate-400 truncate max-w-xs">${rec.notes}</div>` : ''}
                  </td>
                  <td class="py-3 px-3 text-right">
                    <span class="text-base font-bold text-emerald-600">¥${this.formatThousands(rec.total_price, 2)}</span>
                  </td>
                  <td class="py-3 px-3 text-center">
                    <div class="flex items-center justify-center space-x-1">
                      <button onclick="App.openRecordEditModal('elec', '${rec.id}')" class="text-slate-400 hover:text-blue-600 p-1 transition" title="编辑记录">
                        <svg class="w-4 h-4 inline" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M11 5H6a2 2 0 00-2 2v11a2 2 0 002 2h11a2 2 0 002-2v-5m-1.414-9.414a2 2 0 112.828 2.828L11.828 15H9v-2.828l8.586-8.586z"></path></svg>
                      </button>
                      <button onclick="App.deleteRecord('electricity', '${rec.id}')" class="text-slate-400 hover:text-rose-600 p-1 transition" title="删除记录">
                        <svg class="w-4 h-4 inline" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M19 7l-.867 12.142A2 2 0 0116.138 21H7.862a2 2 0 01-1.995-1.858L5 7m5 4v6m4-6v6m1-10V4a1 1 0 00-1-1h-4a1 1 0 00-1 1v3M4 7h16"></path></svg>
                      </button>
                    </div>
                  </td>
                </tr>
              `;
            } else {
              return `
                <tr class="hover:bg-indigo-50/30 transition">
                  <td class="py-3 px-3">
                    <span class="inline-flex items-center px-2 py-0.5 rounded text-xs font-semibold bg-indigo-100 text-indigo-800">💰 支出</span>
                  </td>
                  <td class="py-3 px-3">
                    <div class="font-medium text-slate-900">${rec.record_date}</div>
                    ${rec.mileage ? `<div class="text-xs text-slate-400 font-mono">${this.formatThousands(rec.mileage)} km</div>` : ''}
                  </td>
                  <td class="py-3 px-3">
                    <div class="font-semibold text-slate-800">${rec.item_type}</div>
                    ${rec.provider ? `<div class="text-xs text-slate-400">${rec.provider}</div>` : ''}
                  </td>
                  <td class="py-3 px-3">
                    <div class="text-xs text-slate-600">${rec.notes || '-'}</div>
                  </td>
                  <td class="py-3 px-3 text-right">
                    <span class="text-base font-bold ${rec.cost < 0 ? 'text-emerald-600' : 'text-indigo-600'}">${rec.cost < 0 ? '-¥' + this.formatThousands(Math.abs(rec.cost), 2) : '¥' + this.formatThousands(rec.cost, 2)}</span>
                  </td>
                  <td class="py-3 px-3 text-center">
                    <div class="flex items-center justify-center space-x-1">
                      <button onclick="App.openRecordEditModal('maint', '${rec.id}')" class="text-slate-400 hover:text-blue-600 p-1 transition" title="编辑记录">
                        <svg class="w-4 h-4 inline" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M11 5H6a2 2 0 00-2 2v11a2 2 0 002 2h11a2 2 0 002-2v-5m-1.414-9.414a2 2 0 112.828 2.828L11.828 15H9v-2.828l8.586-8.586z"></path></svg>
                      </button>
                      <button onclick="App.deleteRecord('maintenance', '${rec.id}')" class="text-slate-400 hover:text-rose-600 p-1 transition" title="删除记录">
                        <svg class="w-4 h-4 inline" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M19 7l-.867 12.142A2 2 0 0116.138 21H7.862a2 2 0 01-1.995-1.858L5 7m5 4v6m4-6v6m1-10V4a1 1 0 00-1-1h-4a1 1 0 00-1 1v3M4 7h16"></path></svg>
                      </button>
                    </div>
                  </td>
                </tr>
              `;
            }
          }).join('')}
        </tbody>
      </table>
    `;
  },

  filterHistory(type) {
    this.historyFilter = type;
    ['all', 'status', 'fuel', 'elec', 'maint'].forEach(t => {
      const btn = document.getElementById(`hist-filter-${t}`);
      if (btn) {
        if (t === type) btn.classList.add('active');
        else btn.classList.remove('active');
      }
    });
    this.loadHistory();
  },

  async deleteRecord(category, id) {
    if (!confirm('确认删除本条记录吗？删除后相关统计将自动重算。')) return;
    const numId = parseInt(id);

    let storeName = 'fuel_records';
    let syncCat = 'fuel';
    if (category === 'electricity' || category === 'elec') {
      storeName = 'electricity_records';
      syncCat = 'electricity';
    } else if (category === 'maintenance' || category === 'maint') {
      storeName = 'maintenance_records';
      syncCat = 'maintenance';
    } else if (category === 'status') {
      storeName = 'status_records';
      syncCat = 'status';
    } else {
      syncCat = 'fuel';
    }

    // 1. 离线优先：从本地 IndexedDB 立即移除
    if (window.CarDB && CarDB.db) {
      await CarDB.delete(storeName, numId);
      await CarDB.enqueueSync('DELETE', syncCat, { id: numId });

      // 同步本地车辆最高里程
      if (this.activeVehicleId) {
        const vehicles = await CarDB.getAll('vehicles');
        const v = vehicles.find(item => item.id == this.activeVehicleId);
        if (v) {
          const allF = (await CarDB.getAll('fuel_records')).filter(r => r.vehicle_id == v.id);
          const allE = (await CarDB.getAll('electricity_records')).filter(r => r.vehicle_id == v.id);
          const allM = (await CarDB.getAll('maintenance_records')).filter(r => r.vehicle_id == v.id);
          const allS = (await CarDB.getAll('status_records')).filter(r => r.vehicle_id == v.id);
          const allMileages = [
            parseFloat(v.initial_mileage || 0),
            parseFloat(v.current_mileage || 0),
            ...allF.map(r => parseFloat(r.mileage) || 0),
            ...allE.map(r => parseFloat(r.mileage) || 0),
            ...allM.map(r => parseFloat(r.mileage) || 0),
            ...allS.map(r => parseFloat(r.mileage) || 0)
          ];
          v.current_mileage = Math.max(...allMileages);
          await CarDB.put('vehicles', v);
        }
      }
    }

    this.showToast('记录已删除！(本地已生效)', 'info');

    // 2. 若联网则触发静默同步
    if (navigator.onLine && window.CarSync) {
      CarSync.sync(true);
    }

    await this.loadVehicles();
    this.loadHistory();
    await this.extractStationsFromHistory();
  },

  // 快捷油品设置 (编辑弹窗)
  setEditFuelType(typeStr) {
    const el = document.getElementById('edit-fuel-type');
    if (el) el.value = typeStr;
    const container = document.getElementById('edit-fuel-pills');
    if (container) {
      Array.from(container.children).forEach(btn => {
        if (btn.innerText.includes(typeStr)) {
          btn.classList.add('active');
        } else {
          btn.classList.remove('active');
        }
      });
    }
  },

  // 快捷充电类型设置 (编辑弹窗)
  setEditChargingType(typeStr) {
    const el = document.getElementById('edit-elec-charging-type');
    if (el) el.value = typeStr;
    const container = document.getElementById('edit-elec-pills');
    if (container) {
      Array.from(container.children).forEach(btn => {
        if (btn.innerText.includes(typeStr)) {
          btn.classList.add('active');
        } else {
          btn.classList.remove('active');
        }
      });
    }
  },

  // 编辑弹窗三项联动计算 (实付总价 = 数量 × 单价)
  // 优化规则：修改总价或加电量/加油量时，自动调整单价，而不是调整总价和电量/加油量
  handleEditTriInput(type, changedField) {
    const prefix = type === 'fuel' ? 'edit-fuel' : 'edit-elec';
    const amountEl = document.getElementById(`${prefix}-amount`);
    const priceEl = document.getElementById(`${prefix}-unit-price`);
    const totalEl = document.getElementById(`${prefix}-total-price`);
    if (!amountEl || !priceEl || !totalEl) return;

    const amount = parseFloat(amountEl.value);
    const price = parseFloat(priceEl.value);
    const total = parseFloat(totalEl.value);

    const hasAmount = !isNaN(amount) && amount > 0;
    const hasPrice = !isNaN(price) && price > 0;
    const hasTotal = !isNaN(total) && total >= 0;

    if (changedField === 'total') {
      // 优化规则 1: 修改总价时，自动调整单价 (单价 = 总价 / 数量)，不修改加电量/加油量
      if (hasAmount && hasTotal) {
        priceEl.value = this.formatPrecision(total / amount, 5);
      }
    } else if (changedField === 'amount') {
      // 优化规则 2: 修改加电量/加油量时，自动调整单价 (单价 = 总价 / 数量)，不修改总价
      if (hasTotal && hasAmount) {
        priceEl.value = this.formatPrecision(total / amount, 5);
      } else if (!hasTotal && hasPrice && hasAmount) {
        // 若未填总价但已填单价，顺推总价
        totalEl.value = this.formatPrecision(amount * price, 5);
      }
    } else if (changedField === 'price') {
      // 优化规则 3: 手动修改单价时，按 实付总价 = 数量 × 单价 调整总价
      if (hasAmount && hasPrice) {
        totalEl.value = this.formatPrecision(amount * price, 5);
      }
    }

    if (type === 'fuel') this.calcEditFuelPostPercent();
    if (type === 'elec') this.calcEditElecEndSoc();
  },

  // 编辑弹窗油量推算
  calcEditFuelPostPercent() {
    const isFullRadio = document.querySelector('input[name="edit-fuel-is-full"]:checked');
    const isFull = isFullRadio ? isFullRadio.value === '1' : true;
    const postInput = document.getElementById('edit-fuel-percent');
    if (postInput && isFull) {
      postInput.value = 100;
    }
  },

  // 编辑弹窗电量推算
  calcEditElecEndSoc() {
    const isCalibrated = !!document.getElementById('edit-elec-battery-calibrate-toggle')?.checked;
    if (isCalibrated) {
      this.calcBatteryCalibration(true);
      return;
    }
    const vid = parseInt(document.getElementById('edit-vehicle-id')?.value);
    const v = this.vehicles.find(item => item.id == vid) || this.getActiveVehicle();
    const batteryCap = v ? parseFloat(v.battery_capacity || 0) : 0;
    const startSoc = parseFloat(document.getElementById('edit-elec-start-soc')?.value || 0);
    const amount = parseFloat(document.getElementById('edit-elec-amount')?.value || 0);
    const endSocEl = document.getElementById('edit-elec-end-soc');
    if (!endSocEl) return;

    if (batteryCap > 0 && amount > 0) {
      const calculatedEnd = Math.min(100, Math.round(startSoc + (amount / batteryCap) * 100));
      endSocEl.value = calculatedEnd;
    }
  },

  // 打开记录编辑弹窗 (油耗 / 电耗 / 费用支出)
  async openRecordEditModal(category, id) {
    try {
      const numId = parseInt(id);
      let storeName = 'fuel_records';
      let normCat = 'fuel';
      if (category === 'electricity' || category === 'elec') {
        storeName = 'electricity_records';
        normCat = 'elec';
      } else if (category === 'maintenance' || category === 'maint') {
        storeName = 'maintenance_records';
        normCat = 'maint';
      } else if (category === 'status') {
        storeName = 'status_records';
        normCat = 'status';
      }

      // 1. 获取记录数据 (多重安全兜底读取)
      let rec = null;
      if (window.CarDB && CarDB.db) {
        rec = await CarDB.get(storeName, numId);
        if (!rec) {
          rec = await CarDB.get(storeName, id);
        }
        if (!rec) {
          const all = await CarDB.getAll(storeName);
          rec = all.find(r => r.id == id || r.id == numId);
        }
      }

      if (!rec) {
        const baseUrl = window.CarSync ? await CarSync.getApiBaseUrl() : '';
        const apiEndpoint = normCat === 'elec' ? 'electricity' : normCat === 'maint' ? 'maintenance' : normCat === 'status' ? 'status' : 'fuel';
        try {
          const res = await fetch(`${baseUrl}/api/records/${apiEndpoint}?vehicle_id=${this.activeVehicleId || ''}`);
          if (res.ok) {
            const list = await res.json();
            rec = list.find(r => r.id == numId || r.id == id);
          }
        } catch (e) {
          console.warn('从API获取记录异常:', e);
        }
      }

      if (!rec) {
        this.showToast('无法读取该记录数据', 'error');
        return;
      }

      // 2. 填充隐藏字段与车辆信息
      document.getElementById('edit-record-id').value = rec.id || numId;
      document.getElementById('edit-record-category').value = normCat;
      document.getElementById('edit-vehicle-id').value = rec.vehicle_id;

      const v = this.vehicles.find(item => item.id == rec.vehicle_id) || this.getActiveVehicle();
      document.getElementById('edit-car-display').value = v ? (v.plate_number || v.name) : '当前车辆';

      // 3. UI 元素切换
      const titleEl = document.getElementById('record-edit-title');
      const iconEl = document.getElementById('record-edit-icon');
      const fuelBox = document.getElementById('edit-fuel-fields');
      const elecBox = document.getElementById('edit-elec-fields');
      const maintBox = document.getElementById('edit-maint-fields');
      const statusBox = document.getElementById('edit-status-fields');
      const dtInput = document.getElementById('edit-date');
      const mileageInput = document.getElementById('edit-mileage');
      const mileageRequired = document.getElementById('edit-mileage-required');
      const notesInput = document.getElementById('edit-notes');

      if (notesInput) notesInput.value = rec.notes || '';
      if (mileageInput) mileageInput.value = rec.mileage !== undefined && rec.mileage !== null ? rec.mileage : '';

      if (normCat === 'fuel') {
        if (titleEl) titleEl.textContent = '编辑油耗记录';
        if (iconEl) {
          iconEl.textContent = '⛽';
          iconEl.className = 'w-8 h-8 rounded-lg bg-amber-100 text-amber-600 flex items-center justify-center font-bold text-base';
        }
        if (fuelBox) fuelBox.classList.remove('hidden');
        if (elecBox) elecBox.classList.add('hidden');
        if (maintBox) maintBox.classList.add('hidden');
        if (statusBox) statusBox.classList.add('hidden');

        if (dtInput) {
          dtInput.classList.remove('hidden');
          dtInput.required = true;
          dtInput.value = (rec.record_date || '').replace(' ', 'T').slice(0, 16);
        }

        if (mileageRequired) mileageRequired.classList.remove('hidden');
        if (mileageInput) mileageInput.required = true;

        const editFuelAmount = document.getElementById('edit-fuel-amount');
        if (editFuelAmount) editFuelAmount.value = rec.fuel_amount ?? '';
        const editFuelUnitPrice = document.getElementById('edit-fuel-unit-price');
        if (editFuelUnitPrice) editFuelUnitPrice.value = rec.unit_price ?? '';
        const editFuelTotalPrice = document.getElementById('edit-fuel-total-price');
        if (editFuelTotalPrice) editFuelTotalPrice.value = rec.total_price ?? '';
        this.setEditFuelType(rec.fuel_type || '92#');

        const isFullRadios = document.querySelectorAll('input[name="edit-fuel-is-full"]');
        const isFullVal = (rec.is_full === 1 || rec.is_full === true || rec.is_full === '1') ? '1' : '0';
        isFullRadios.forEach(r => {
          r.checked = (r.value === isFullVal);
        });
        const editFuelStartInput = document.getElementById('edit-fuel-start-percent');
        if (editFuelStartInput) {
          editFuelStartInput.value = rec.start_fuel_percent !== undefined && rec.start_fuel_percent !== null ? rec.start_fuel_percent : '';
        }
        const editFuelPercent = document.getElementById('edit-fuel-percent');
        if (editFuelPercent) editFuelPercent.value = rec.fuel_percent !== undefined && rec.fuel_percent !== null ? rec.fuel_percent : 100;
        const editFuelBatBox = document.getElementById('edit-fuel-battery-box');
        if (v && v.power_type === 'gas') {
          if (editFuelBatBox) editFuelBatBox.classList.add('hidden');
        } else {
          if (editFuelBatBox) editFuelBatBox.classList.remove('hidden');
          const editFuelBatInput = document.getElementById('edit-fuel-battery-percent');
          if (editFuelBatInput) {
            editFuelBatInput.value = (rec.battery_percent !== null && rec.battery_percent !== undefined) ? rec.battery_percent : '';
          }
        }
        const editFuelStation = document.getElementById('edit-fuel-station');
        if (editFuelStation) editFuelStation.value = rec.station || '';
        const fuelClear = document.getElementById('edit-fuel-station-clear-btn');
        if (fuelClear) fuelClear.classList.toggle('hidden', !rec.station);
        this.renderEditFuelStationPills();
        this.closeEditFuelStationDropdown();

      } else if (normCat === 'elec') {
        if (titleEl) titleEl.textContent = '编辑电耗记录';
        if (iconEl) {
          iconEl.textContent = '⚡';
          iconEl.className = 'w-8 h-8 rounded-lg bg-emerald-100 text-emerald-600 flex items-center justify-center font-bold text-base';
        }
        if (fuelBox) fuelBox.classList.add('hidden');
        if (elecBox) elecBox.classList.remove('hidden');
        if (maintBox) maintBox.classList.add('hidden');
        if (statusBox) statusBox.classList.add('hidden');

        if (dtInput) {
          dtInput.classList.remove('hidden');
          dtInput.required = true;
          dtInput.value = (rec.record_date || '').replace(' ', 'T').slice(0, 16);
        }

        if (mileageRequired) mileageRequired.classList.remove('hidden');
        if (mileageInput) mileageInput.required = true;

        const editElecAmount = document.getElementById('edit-elec-amount');
        if (editElecAmount) editElecAmount.value = rec.electricity_amount ?? '';
        const editElecUnitPrice = document.getElementById('edit-elec-unit-price');
        if (editElecUnitPrice) editElecUnitPrice.value = rec.unit_price ?? '';
        const editElecTotalPrice = document.getElementById('edit-elec-total-price');
        if (editElecTotalPrice) editElecTotalPrice.value = rec.total_price ?? '';
        this.setEditChargingType(rec.charging_type || '快充 (DC直流)');

        const editElecStartSoc = document.getElementById('edit-elec-start-soc');
        if (editElecStartSoc) editElecStartSoc.value = rec.start_soc !== null && rec.start_soc !== undefined ? rec.start_soc : '';
        const editElecEndSoc = document.getElementById('edit-elec-end-soc');
        if (editElecEndSoc) editElecEndSoc.value = rec.end_soc !== null && rec.end_soc !== undefined ? rec.end_soc : '';

        const elecFuelBox = document.getElementById('edit-elec-fuel-box');
        if (v && v.power_type === 'ev') {
          if (elecFuelBox) elecFuelBox.classList.add('hidden');
        } else {
          if (elecFuelBox) elecFuelBox.classList.remove('hidden');
          const editElecFuelPercent = document.getElementById('edit-elec-fuel-percent');
          if (editElecFuelPercent) editElecFuelPercent.value = rec.fuel_percent !== null && rec.fuel_percent !== undefined ? rec.fuel_percent : '';
        }
        const editElecStation = document.getElementById('edit-elec-station');
        if (editElecStation) editElecStation.value = rec.station || '';
        const elecClear = document.getElementById('edit-elec-station-clear-btn');
        if (elecClear) elecClear.classList.toggle('hidden', !rec.station);
        this.renderEditElecStationPills();
        this.closeEditElecStationDropdown();

        const editCalToggle = document.getElementById('edit-elec-battery-calibrate-toggle');
        const isCal = (rec.is_calibrated === 1 || rec.is_calibrated === '1' || rec.is_calibrated === true);
        if (editCalToggle) {
          editCalToggle.checked = isCal;
        }
        this.toggleBatteryCalibration(true);

      } else if (normCat === 'status') {
        if (titleEl) titleEl.textContent = '编辑状态记录';
        if (iconEl) {
          iconEl.textContent = '📊';
          iconEl.className = 'w-8 h-8 rounded-lg bg-blue-100 text-blue-600 flex items-center justify-center font-bold text-base';
        }
        if (fuelBox) fuelBox.classList.add('hidden');
        if (elecBox) elecBox.classList.add('hidden');
        if (maintBox) maintBox.classList.add('hidden');
        if (statusBox) statusBox.classList.remove('hidden');

        if (dtInput) {
          dtInput.classList.remove('hidden');
          dtInput.required = true;
          dtInput.value = (rec.record_date || '').replace(' ', 'T').slice(0, 16);
        }

        if (mileageRequired) mileageRequired.classList.remove('hidden');
        if (mileageInput) mileageInput.required = true;

        const editStatusBat = document.getElementById('edit-status-battery');
        const editStatusFuel = document.getElementById('edit-status-fuel');
        const editStatusBatBox = document.getElementById('edit-status-battery-box');
        const editStatusFuelBox = document.getElementById('edit-status-fuel-box');

        if (editStatusBat) editStatusBat.value = (rec.battery_percent !== null && rec.battery_percent !== undefined) ? rec.battery_percent : '';
        if (editStatusFuel) editStatusFuel.value = (rec.fuel_percent !== null && rec.fuel_percent !== undefined) ? rec.fuel_percent : '';

        const pType = v ? (v.power_type || 'gas') : 'gas';
        if (editStatusBatBox) {
          if (pType === 'gas') editStatusBatBox.classList.add('hidden');
          else editStatusBatBox.classList.remove('hidden');
        }
        if (editStatusFuelBox) {
          if (pType === 'ev') editStatusFuelBox.classList.add('hidden');
          else editStatusFuelBox.classList.remove('hidden');
        }

      } else { // maint
        if (titleEl) titleEl.textContent = '编辑费用支出记录';
        if (iconEl) {
          iconEl.textContent = '💰';
          iconEl.className = 'w-8 h-8 rounded-lg bg-indigo-100 text-indigo-600 flex items-center justify-center font-bold text-base';
        }
        if (fuelBox) fuelBox.classList.add('hidden');
        if (elecBox) elecBox.classList.add('hidden');
        if (maintBox) maintBox.classList.remove('hidden');
        if (statusBox) statusBox.classList.add('hidden');

        if (dtInput) {
          dtInput.classList.remove('hidden');
          dtInput.required = true;
          dtInput.value = (rec.record_date || '').replace(' ', 'T').slice(0, 16);
        }

        if (mileageRequired) mileageRequired.classList.add('hidden');
        if (mileageInput) mileageInput.required = false;

        const editMaintItemType = document.getElementById('edit-maint-item-type');
        if (editMaintItemType) editMaintItemType.value = rec.item_type || '';
        const editMaintCost = document.getElementById('edit-maint-cost');
        if (editMaintCost) editMaintCost.value = rec.cost ?? '';
        this.updateCostSignButton('edit-maint-cost');
        this.renderEditExpenseItemsPills();
      }

      const modalEl = document.getElementById('record-edit-modal');
      if (modalEl) {
        modalEl.classList.remove('hidden');
      }
    } catch (err) {
      console.error('打开编辑弹窗失败:', err);
      this.showToast('打开编辑弹窗失败: ' + err.message, 'error');
    }
  },

  // 关闭记录编辑弹窗
  closeRecordEditModal() {
    this.closeEditFuelStationDropdown();
    this.closeEditElecStationDropdown();
    const modal = document.getElementById('record-edit-modal');
    if (modal) modal.classList.add('hidden');
  },

  // 提交保存修改记录
  async saveRecordEdit(e) {
    e.preventDefault();
    const id = parseInt(document.getElementById('edit-record-id').value);
    const normCat = document.getElementById('edit-record-category').value;
    const vid = parseInt(document.getElementById('edit-vehicle-id').value);

    let storeName = 'fuel_records';
    let apiCat = 'fuel';
    if (normCat === 'elec') {
      storeName = 'electricity_records';
      apiCat = 'electricity';
    } else if (normCat === 'maint') {
      storeName = 'maintenance_records';
      apiCat = 'maintenance';
    } else if (normCat === 'status') {
      storeName = 'status_records';
      apiCat = 'status';
    }

    let existing = null;
    if (window.CarDB && CarDB.db) {
      existing = await CarDB.get(storeName, id);
    }
    const updated = { ...(existing || {}), id, vehicle_id: vid };

    const notes = document.getElementById('edit-notes').value.trim();
    updated.notes = notes;

    if (normCat === 'fuel') {
      const mileage = parseFloat(document.getElementById('edit-mileage').value);
      const amount = parseFloat(document.getElementById('edit-fuel-amount').value);
      const unitPrice = parseFloat(document.getElementById('edit-fuel-unit-price').value);
      const totalPrice = parseFloat(document.getElementById('edit-fuel-total-price').value);
      const fuelType = document.getElementById('edit-fuel-type').value.trim() || '92#';
      const isFull = parseInt(document.querySelector('input[name="edit-fuel-is-full"]:checked')?.value ?? 1);
      const startFuelVal = document.getElementById('edit-fuel-start-percent')?.value;
      const startFuelPercent = (startFuelVal !== undefined && startFuelVal !== '') ? parseFloat(startFuelVal) : null;
      const fuelPercent = parseFloat(document.getElementById('edit-fuel-percent').value || 100);
      const station = document.getElementById('edit-fuel-station').value.trim();
      const recordDate = document.getElementById('edit-date').value.replace('T', ' ');

      if (isNaN(mileage) || mileage < 0) {
        this.showToast('请输入有效的表显总里程', 'error');
        return;
      }
      if (isNaN(amount) || amount <= 0) {
        this.showToast('请输入有效的加油量', 'error');
        return;
      }
      if (isNaN(totalPrice) || totalPrice < 0) {
        this.showToast('请输入有效的实付总价', 'error');
        return;
      }

      updated.mileage = mileage;
      updated.fuel_amount = amount;
      updated.unit_price = !isNaN(unitPrice) ? unitPrice : parseFloat((totalPrice / amount).toFixed(3));
      updated.total_price = totalPrice;
      updated.fuel_type = fuelType;
      updated.is_full = isFull;
      updated.start_fuel_percent = startFuelPercent;
      updated.fuel_percent = fuelPercent;
      const editFuelBatVal = document.getElementById('edit-fuel-battery-percent')?.value;
      const editFuelBatteryPercent = (editFuelBatVal !== undefined && editFuelBatVal !== '' && !isNaN(parseFloat(editFuelBatVal))) ? parseFloat(editFuelBatVal) : null;
      updated.battery_percent = editFuelBatteryPercent;
      updated.station = station;
      if (station) this.addFuelStation(station);
      updated.record_date = recordDate;

    } else if (normCat === 'elec') {
      const mileage = parseFloat(document.getElementById('edit-mileage').value);
      const amount = parseFloat(document.getElementById('edit-elec-amount').value);
      const unitPrice = parseFloat(document.getElementById('edit-elec-unit-price').value);
      const totalPrice = parseFloat(document.getElementById('edit-elec-total-price').value);
      const chargingType = document.getElementById('edit-elec-charging-type').value.trim() || '快充 (DC直流)';
      const startSocVal = document.getElementById('edit-elec-start-soc').value;
      const startSoc = startSocVal !== '' ? parseFloat(startSocVal) : null;
      const endSocVal = document.getElementById('edit-elec-end-soc').value;
      const endSoc = endSocVal !== '' ? parseFloat(endSocVal) : null;
      const fuelPercentVal = document.getElementById('edit-elec-fuel-percent').value;
      const fuelPercent = fuelPercentVal !== '' ? parseFloat(fuelPercentVal) : null;
      const station = document.getElementById('edit-elec-station').value.trim();
      const recordDate = document.getElementById('edit-date').value.replace('T', ' ');

      if (isNaN(mileage) || mileage < 0) {
        this.showToast('请输入有效的表显总里程', 'error');
        return;
      }
      if (isNaN(amount) || amount <= 0) {
        this.showToast('请输入有效的加电量', 'error');
        return;
      }
      if (isNaN(totalPrice) || totalPrice < 0) {
        this.showToast('请输入有效的实付总价', 'error');
        return;
      }

      const isCalibrated = document.getElementById('edit-elec-battery-calibrate-toggle')?.checked ? 1 : 0;
      let calibratedCapacity = null;
      if (isCalibrated) {
        if (!isNaN(endSoc) && !isNaN(startSoc) && endSoc > startSoc) {
          calibratedCapacity = Math.round((amount / (endSoc - startSoc)) * 10000) / 100;
        }
      }

      updated.mileage = mileage;
      updated.electricity_amount = amount;
      updated.unit_price = !isNaN(unitPrice) ? unitPrice : parseFloat((totalPrice / amount).toFixed(3));
      updated.total_price = totalPrice;
      updated.charging_type = chargingType;
      updated.start_soc = startSoc;
      updated.end_soc = endSoc;
      updated.fuel_percent = fuelPercent;
      updated.station = station;
      updated.is_calibrated = isCalibrated;
      updated.calibrated_capacity = calibratedCapacity;
      if (station) this.addElecStation(station);
      updated.record_date = recordDate;

    } else if (normCat === 'status') {
      const mileage = parseFloat(document.getElementById('edit-mileage').value);
      const recordDate = document.getElementById('edit-date').value.replace('T', ' ');
      const batInput = document.getElementById('edit-status-battery');
      const fuelInput = document.getElementById('edit-status-fuel');

      if (isNaN(mileage) || mileage < 0) {
        this.showToast('请输入有效的里程数', 'error');
        return;
      }

      let batVal = null;
      if (batInput && batInput.value !== '') {
        batVal = parseFloat(batInput.value);
        if (isNaN(batVal) || batVal < 0 || batVal > 100) {
          this.showToast('请输入有效的电量百分比 (0-100)', 'error');
          return;
        }
      }

      let fuelVal = null;
      if (fuelInput && fuelInput.value !== '') {
        fuelVal = parseFloat(fuelInput.value);
        if (isNaN(fuelVal) || fuelVal < 0 || fuelVal > 100) {
          this.showToast('请输入有效的油量百分比 (0-100)', 'error');
          return;
        }
      }

      updated.mileage = mileage;
      updated.battery_percent = batVal;
      updated.fuel_percent = fuelVal;
      updated.record_date = recordDate;

    } else { // maint
      const itemType = document.getElementById('edit-maint-item-type').value.trim();
      const cost = parseFloat(document.getElementById('edit-maint-cost').value);
      const rawDate = (document.getElementById('edit-date')?.value || document.getElementById('edit-date-maint')?.value || '');
      const recordDate = rawDate.replace('T', ' ');
      const mileageVal = document.getElementById('edit-mileage').value;
      const mileage = mileageVal !== '' ? parseFloat(mileageVal) : 0;

      if (!itemType) {
        this.showToast('请输入支出项目', 'error');
        return;
      }
      const rawCost = document.getElementById('edit-maint-cost')?.value.trim();
      if (!rawCost || rawCost === '-' || isNaN(cost)) {
        this.showToast('请输入有效的支出金额 (支持正数或负数冲减)', 'error');
        return;
      }

      updated.item_type = itemType;
      updated.cost = cost;
      updated.record_date = recordDate;
      updated.mileage = mileage;
      this.addExpenseItem(itemType);
    }

    // 1. 离线优先：写入 IndexedDB 并排队等待上报
    if (window.CarDB && CarDB.db) {
      await CarDB.put(storeName, updated);
      await CarDB.enqueueSync('UPDATE', apiCat, updated);

      // 同步本地车辆最高里程
      if (vid) {
        const vehicles = await CarDB.getAll('vehicles');
        const v = vehicles.find(item => item.id == vid);
        if (v) {
          const allF = (await CarDB.getAll('fuel_records')).filter(r => r.vehicle_id == v.id);
          const allE = (await CarDB.getAll('electricity_records')).filter(r => r.vehicle_id == v.id);
          const allM = (await CarDB.getAll('maintenance_records')).filter(r => r.vehicle_id == v.id);
          const allS = (await CarDB.getAll('status_records')).filter(r => r.vehicle_id == v.id);
          const allMileages = [
            parseFloat(v.initial_mileage || 0),
            parseFloat(v.current_mileage || 0),
            ...allF.map(r => parseFloat(r.mileage) || 0),
            ...allE.map(r => parseFloat(r.mileage) || 0),
            ...allM.map(r => parseFloat(r.mileage) || 0),
            ...allS.map(r => parseFloat(r.mileage) || 0)
          ];
          v.current_mileage = Math.max(...allMileages);
          if (normCat === 'status') {
            if (updated.battery_percent !== null && updated.battery_percent !== undefined) {
              v.current_battery_percent = updated.battery_percent;
            }
            if (updated.fuel_percent !== null && updated.fuel_percent !== undefined) {
              v.current_fuel_percent = updated.fuel_percent;
            }
          }
          await CarDB.put('vehicles', v);
        }
      }
    }

    this.closeRecordEditModal();
    this.showToast('记录修改成功！(本地已生效)', 'success');

    // 2. 联网静默同步
    if (navigator.onLine && window.CarSync) {
      CarSync.sync(true);
    }

    // 3. 刷新各页面
    if (normCat === 'fuel') {
      await this.extractFuelStationsHistory();
      this.renderFuelStationPills();
    } else if (normCat === 'elec') {
      await this.extractElecStationsHistory();
      this.renderElecStationPills();
    }
    await this.loadVehicles();
    this.loadHistory();
    this.loadOverview();
    if (document.getElementById('tab-energy') && !document.getElementById('tab-energy').classList.contains('hidden')) {
      this.loadEnergyAnalysis();
    }
  },

  // 车辆列表页面渲染 (满足需求1)
  renderVehiclesList() {
    const grid = document.getElementById('vehicles-grid');
    if (!grid) return;

    if (this.vehicles.length === 0) {
      grid.innerHTML = `
        <div class="col-span-full text-center py-12 bg-white rounded-2xl border border-slate-200">
          <p class="text-slate-500 mb-4">暂无已录入的车辆</p>
          <button onclick="App.openVehicleModal()" class="px-4 py-2 bg-blue-600 text-white rounded-xl text-sm font-medium">录入第一台车</button>
        </div>
      `;
      return;
    }

    grid.innerHTML = this.vehicles.map(v => {
      const isActive = v.id === this.activeVehicleId;
      return `
        <div class="bg-white rounded-2xl border ${isActive ? 'border-blue-500 ring-2 ring-blue-100 shadow-md' : 'border-slate-200 shadow-sm'} p-6 flex flex-col justify-between transition">
          <div>
            <div class="flex items-start justify-between">
              <div>
                <span class="vehicle-badge badge-${v.power_type} mb-1">${this.getPowerTypeName(v.power_type)}</span>
                <h3 class="text-xl font-bold text-slate-900 font-mono tracking-wide">${v.plate_number || v.name}</h3>
                <p class="text-xs text-slate-500 font-medium mt-0.5">${v.brand} ${v.model}</p>
              </div>
              ${isActive ? '<span class="text-xs px-2.5 py-1 bg-blue-50 text-blue-600 font-bold rounded-full">当前活跃</span>' : ''}
            </div>

            <div class="mt-4 pt-4 border-t border-slate-100 space-y-2 text-xs">
              <div class="flex justify-between text-slate-600">
                <span class="text-slate-400">品牌型号:</span>
                <span class="font-medium">${v.brand} ${v.model}</span>
              </div>
              <div class="flex justify-between text-slate-600">
                <span class="text-slate-400">动力参数:</span>
                <span>${v.displacement ? '排量 ' + v.displacement : '纯电动'} ${v.fuel_tank_capacity ? '· 油箱 ' + v.fuel_tank_capacity + 'L' : ''} ${v.battery_capacity ? '· 电池 ' + v.battery_capacity + 'kWh' : ''}</span>
              </div>
              <div class="flex justify-between text-slate-600">
                <span class="text-slate-400">里程信息:</span>
                <span>初始 ${this.formatThousands(v.initial_mileage)} km ➔ 当前 <strong class="text-slate-900 font-semibold">${this.formatThousands(v.current_mileage)}</strong> km</span>
              </div>
              <div class="flex justify-between text-slate-600">
                <span class="text-slate-400">初始余量:</span>
                <span>${v.power_type !== 'ev' ? '初始油量 ' + (v.initial_fuel_percent != null ? v.initial_fuel_percent : 0) + '%' : ''} ${v.power_type !== 'ev' && v.power_type !== 'gas' ? '· ' : ''}${v.power_type !== 'gas' ? '初始电量 ' + (v.initial_battery_percent != null ? v.initial_battery_percent : 0) + '%' : ''}</span>
              </div>
              ${v.purchase_date ? `
                <div class="flex justify-between text-slate-600">
                  <span class="text-slate-400">购车日期:</span>
                  <span>${v.purchase_date}</span>
                </div>
              ` : ''}
            </div>
          </div>

          <div class="mt-6 pt-4 border-t border-slate-100 flex items-center justify-between">
            ${!isActive ? `
              <button onclick="App.onVehicleChange(${v.id})" class="text-xs font-semibold text-blue-600 hover:text-blue-800">
                设为当前车 ➔
              </button>
            ` : '<span class="text-xs text-slate-400">正在使用此车</span>'}

            <div class="space-x-2">
              <button onclick="App.openVehicleModal(${v.id})" class="text-xs px-2.5 py-1 bg-slate-100 hover:bg-slate-200 text-slate-700 rounded-lg transition font-medium">编辑</button>
              <button onclick="App.deleteVehicle(${v.id})" class="text-xs px-2.5 py-1 bg-rose-50 hover:bg-rose-100 text-rose-600 rounded-lg transition font-medium">删除</button>
            </div>
          </div>
        </div>
      `;
    }).join('');
  },

  // 打开车辆编辑/录入弹窗
  openVehicleModal(vehicleId = null) {
    const modal = document.getElementById('vehicle-modal');
    const title = document.getElementById('modal-vehicle-title');
    const form = document.getElementById('vehicle-form');

    form.reset();

    if (vehicleId) {
      const v = this.vehicles.find(item => item.id === vehicleId);
      if (!v) return;
      title.textContent = `编辑车辆 - ${v.plate_number || v.name}`;
      document.getElementById('vehicle-form-id').value = v.id;
      document.getElementById('v-plate').value = v.plate_number || v.name || '';
      document.getElementById('v-brand').value = v.brand;
      document.getElementById('v-model').value = v.model;
      document.getElementById('v-power').value = v.power_type;

      // 解析排量数字与是否带 T
      const rawDisp = (v.displacement || '').trim();
      let dispNum = '';
      let isTurbo = false;
      if (rawDisp) {
        const match = rawDisp.match(/([\d.]+)\s*([tTlL]?)/);
        if (match) {
          dispNum = match[1] || '';
          isTurbo = (match[2] || '').toUpperCase() === 'T';
        } else {
          dispNum = rawDisp;
        }
      }
      document.getElementById('v-displacement-val').value = dispNum;
      document.getElementById('v-displacement-turbo').checked = isTurbo;

      document.getElementById('v-tank').value = v.fuel_tank_capacity || '';
      document.getElementById('v-battery').value = v.battery_capacity || '';
      document.getElementById('v-fuel-percent').value = (v.initial_fuel_percent != null) ? v.initial_fuel_percent : 0;
      document.getElementById('v-battery-percent').value = (v.initial_battery_percent != null) ? v.initial_battery_percent : 0;
      document.getElementById('v-initial-mileage').value = v.initial_mileage;
      document.getElementById('v-current-mileage').value = v.current_mileage || v.initial_mileage || '';
      document.getElementById('v-purchase-date').value = v.purchase_date || '';
      document.getElementById('v-notes').value = v.notes || '';
      this.onPowerTypeChange(v.power_type);
    } else {
      title.textContent = '录入新车辆';
      document.getElementById('vehicle-form-id').value = '';
      document.getElementById('v-plate').value = '';
      document.getElementById('v-displacement-val').value = '';
      document.getElementById('v-displacement-turbo').checked = false;
      document.getElementById('v-tank').value = '';
      document.getElementById('v-battery').value = '';
      document.getElementById('v-fuel-percent').value = 0;
      document.getElementById('v-battery-percent').value = 0;
      document.getElementById('v-initial-mileage').value = '';
      document.getElementById('v-current-mileage').value = '';
      this.onPowerTypeChange('gas');
    }

    modal.classList.remove('hidden');
  },

  closeVehicleModal() {
    document.getElementById('vehicle-modal').classList.add('hidden');
  },

  // 动力类型联动控制：纯燃油不可录电池，纯电不可录排量与油箱
  onPowerTypeChange(type) {
    const dispBox = document.getElementById('v-displacement-box');
    const dispVal = document.getElementById('v-displacement-val');
    const dispTurbo = document.getElementById('v-displacement-turbo');
    const turboLabel = document.getElementById('v-turbo-label');
    const tankBox = document.getElementById('v-tank-box');
    const tankInput = document.getElementById('v-tank');
    const battBox = document.getElementById('v-battery-box');
    const battInput = document.getElementById('v-battery');
    const fuelPctBox = document.getElementById('v-fuel-percent-box');
    const fuelPctInput = document.getElementById('v-fuel-percent');
    const battPctBox = document.getElementById('v-battery-percent-box');
    const battPctInput = document.getElementById('v-battery-percent');

    if (type === 'gas') {
      // 纯燃油车：电池容量与初始电量不可输入
      if (battInput) {
        battInput.disabled = true;
        battInput.value = '';
        battInput.placeholder = '纯燃油车无需电池';
        battInput.classList.add('bg-slate-100', 'text-slate-400', 'cursor-not-allowed');
      }
      if (battBox) battBox.classList.add('opacity-50');

      if (battPctInput) {
        battPctInput.disabled = true;
        battPctInput.value = 0;
        battPctInput.classList.add('bg-slate-100', 'text-slate-400', 'cursor-not-allowed');
      }
      if (battPctBox) battPctBox.classList.add('opacity-50');

      // 排量、油箱与初始油量可正常输入
      if (dispVal) {
        dispVal.disabled = false;
        dispVal.placeholder = '如 2.0';
        dispVal.classList.remove('bg-slate-100', 'text-slate-400', 'cursor-not-allowed');
      }
      if (dispTurbo) dispTurbo.disabled = false;
      if (turboLabel) turboLabel.classList.remove('opacity-40', 'cursor-not-allowed', 'pointer-events-none');
      if (dispBox) dispBox.classList.remove('opacity-50');

      if (tankInput) {
        tankInput.disabled = false;
        tankInput.placeholder = '如 55';
        tankInput.classList.remove('bg-slate-100', 'text-slate-400', 'cursor-not-allowed');
      }
      if (tankBox) tankBox.classList.remove('opacity-50');

      if (fuelPctInput) {
        fuelPctInput.disabled = false;
        fuelPctInput.classList.remove('bg-slate-100', 'text-slate-400', 'cursor-not-allowed');
      }
      if (fuelPctBox) fuelPctBox.classList.remove('opacity-50');

    } else if (type === 'ev') {
      // 纯电动车：排量、油箱容量与初始油量不可输入
      if (dispVal) {
        dispVal.disabled = true;
        dispVal.value = '';
        dispVal.placeholder = '纯电车无排量';
        dispVal.classList.add('bg-slate-100', 'text-slate-400', 'cursor-not-allowed');
      }
      if (dispTurbo) {
        dispTurbo.disabled = true;
        dispTurbo.checked = false;
      }
      if (turboLabel) turboLabel.classList.add('opacity-40', 'cursor-not-allowed', 'pointer-events-none');
      if (dispBox) dispBox.classList.add('opacity-50');

      if (tankInput) {
        tankInput.disabled = true;
        tankInput.value = '';
        tankInput.placeholder = '纯电车无油箱';
        tankInput.classList.add('bg-slate-100', 'text-slate-400', 'cursor-not-allowed');
      }
      if (tankBox) tankBox.classList.add('opacity-50');

      if (fuelPctInput) {
        fuelPctInput.disabled = true;
        fuelPctInput.value = 0;
        fuelPctInput.classList.add('bg-slate-100', 'text-slate-400', 'cursor-not-allowed');
      }
      if (fuelPctBox) fuelPctBox.classList.add('opacity-50');

      // 电池容量与初始电量可输入
      if (battInput) {
        battInput.disabled = false;
        battInput.placeholder = '如 60 或 85.4';
        battInput.classList.remove('bg-slate-100', 'text-slate-400', 'cursor-not-allowed');
      }
      if (battBox) battBox.classList.remove('opacity-50');

      if (battPctInput) {
        battPctInput.disabled = false;
        battPctInput.classList.remove('bg-slate-100', 'text-slate-400', 'cursor-not-allowed');
      }
      if (battPctBox) battPctBox.classList.remove('opacity-50');

    } else {
      // 插电混动 (phev)、增程 (erev)、油电混动 (hybrid)：全部可输入
      if (dispVal) {
        dispVal.disabled = false;
        dispVal.placeholder = '如 1.5 或 2.0';
        dispVal.classList.remove('bg-slate-100', 'text-slate-400', 'cursor-not-allowed');
      }
      if (dispTurbo) dispTurbo.disabled = false;
      if (turboLabel) turboLabel.classList.remove('opacity-40', 'cursor-not-allowed', 'pointer-events-none');
      if (dispBox) dispBox.classList.remove('opacity-50');

      if (tankInput) {
        tankInput.disabled = false;
        tankInput.placeholder = '如 55';
        tankInput.classList.remove('bg-slate-100', 'text-slate-400', 'cursor-not-allowed');
      }
      if (tankBox) tankBox.classList.remove('opacity-50');

      if (fuelPctInput) {
        fuelPctInput.disabled = false;
        fuelPctInput.classList.remove('bg-slate-100', 'text-slate-400', 'cursor-not-allowed');
      }
      if (fuelPctBox) fuelPctBox.classList.remove('opacity-50');

      if (battInput) {
        battInput.disabled = false;
        battInput.placeholder = '如 20 或 40';
        battInput.classList.remove('bg-slate-100', 'text-slate-400', 'cursor-not-allowed');
      }
      if (battBox) battBox.classList.remove('opacity-50');

      if (battPctInput) {
        battPctInput.disabled = false;
        battPctInput.classList.remove('bg-slate-100', 'text-slate-400', 'cursor-not-allowed');
      }
      if (battPctBox) battPctBox.classList.remove('opacity-50');
    }
  },

  // 保存车辆 (离线优先)
  async saveVehicle(e) {
    e.preventDefault();
    const id = document.getElementById('vehicle-form-id').value;
    const plate = document.getElementById('v-plate').value.trim().toUpperCase();
    if (!plate) {
      this.showToast('请输入车牌号', 'error');
      document.getElementById('v-plate').focus();
      return;
    }
    const brand = document.getElementById('v-brand').value.trim();
    const model = document.getElementById('v-model').value.trim();
    const powerType = document.getElementById('v-power').value;

    // 排量处理
    let displacement = '';
    if (powerType !== 'ev') {
      const dispVal = document.getElementById('v-displacement-val').value.trim();
      const isTurbo = document.getElementById('v-displacement-turbo').checked;
      if (dispVal) {
        const num = parseFloat(dispVal);
        if (!isNaN(num) && num > 0) {
          displacement = `${num}${isTurbo ? 'T' : 'L'}`;
        }
      }
    }

    const tank = (powerType === 'ev') ? 0 : (parseFloat(document.getElementById('v-tank').value) || 0);
    const battery = (powerType === 'gas') ? 0 : (parseFloat(document.getElementById('v-battery').value) || 0);
    const initialFuelPercent = (powerType === 'ev') ? 0 : (parseFloat(document.getElementById('v-fuel-percent').value) || 0);
    const initialBatteryPercent = (powerType === 'gas') ? 0 : (parseFloat(document.getElementById('v-battery-percent').value) || 0);
    const initMileage = parseFloat(document.getElementById('v-initial-mileage').value) || 0;
    const purchaseDate = document.getElementById('v-purchase-date').value;
    const notes = document.getElementById('v-notes').value.trim();
    const currMileageInput = document.getElementById('v-current-mileage');
    const currMileageVal = currMileageInput && currMileageInput.value !== '' ? parseFloat(currMileageInput.value) : null;

    const payload = {
      name: plate, // 直接用车牌号替代车名/昵称
      brand,
      model,
      power_type: powerType,
      displacement,
      fuel_tank_capacity: tank,
      battery_capacity: battery,
      initial_fuel_percent: Math.min(100, Math.max(0, initialFuelPercent)),
      initial_battery_percent: Math.min(100, Math.max(0, initialBatteryPercent)),
      plate_number: plate,
      initial_mileage: initMileage,
      purchase_date: purchaseDate,
      notes
    };
    if (currMileageVal !== null && !isNaN(currMileageVal)) {
      payload.current_mileage = Math.max(initMileage, currMileageVal);
    }

    const isEdit = !!id;
    const targetId = isEdit ? parseInt(id) : Date.now();

    // 离线优先：写入本地 IndexedDB
    if (window.CarDB && CarDB.db) {
      if (isEdit) {
        const existing = this.vehicles.find(item => item.id === targetId) || {};
        const finalCurrMileage = currMileageVal !== null && !isNaN(currMileageVal)
          ? Math.max(initMileage, currMileageVal)
          : Math.max(existing.current_mileage || initMileage, initMileage);
        const updated = {
          ...existing,
          ...payload,
          id: targetId,
          current_mileage: finalCurrMileage,
          updated_at: new Date().toISOString()
        };
        await CarDB.put('vehicles', updated);
        await CarDB.enqueueSync('UPDATE', 'vehicles', { id: targetId, ...payload, current_mileage: finalCurrMileage });
      } else {
        payload._tempId = targetId;
        const newVehicle = {
          id: targetId,
          ...payload,
          current_mileage: currMileageVal !== null && !isNaN(currMileageVal) ? Math.max(initMileage, currMileageVal) : initMileage,
          created_at: new Date().toISOString(),
          updated_at: new Date().toISOString()
        };
        await CarDB.put('vehicles', newVehicle);
        await CarDB.enqueueSync('CREATE', 'vehicles', newVehicle);
        this.activeVehicleId = targetId;
        localStorage.setItem('car_tracker_active_vid', targetId);
      }
    }

    this.showToast(`车辆【${plate}】已保存！(本地已生效)`, 'success');
    this.closeVehicleModal();

    // 若联网则触发静默同步
    if (navigator.onLine && window.CarSync) {
      CarSync.sync(true);
    }

    await this.loadVehicles();
  },

  // 删除车辆 (离线优先)
  async deleteVehicle(id) {
    const numId = parseInt(id);
    const v = this.vehicles.find(item => item.id === numId);
    const plateText = v ? (v.plate_number || v.name) : '车辆';
    if (!confirm(`确定要彻底删除车辆【${plateText}】及其所有历史能耗与维保记录吗？此操作不可逆！`)) return;

    if (window.CarDB && CarDB.db) {
      await CarDB.delete('vehicles', numId);
      // 清理本地该车的关联记录
      const allF = await CarDB.getAll('fuel_records');
      for (const r of allF) {
        if (r.vehicle_id === numId) await CarDB.delete('fuel_records', r.id);
      }
      const allE = await CarDB.getAll('electricity_records');
      for (const r of allE) {
        if (r.vehicle_id === numId) await CarDB.delete('electricity_records', r.id);
      }
      const allM = await CarDB.getAll('maintenance_records');
      for (const r of allM) {
        if (r.vehicle_id === numId) await CarDB.delete('maintenance_records', r.id);
      }
      await CarDB.enqueueSync('DELETE', 'vehicles', { id: numId });
    }

    this.showToast(`已删除车辆【${plateText}】`, 'info');

    if (navigator.onLine && window.CarSync) {
      CarSync.sync(true);
    }

    await this.loadVehicles();
    this.switchTab('overview');
  },

  // 打开录入当前状态弹窗 (当前里程、当前电量%、当前油量%)
  async openCurrentStatusModal() {
    if (!this.activeVehicleId) {
      this.showToast('请先选择或录入车辆', 'warning');
      return;
    }
    const v = this.getActiveVehicle();
    if (!v) return;

    const modal = document.getElementById('current-status-modal') || document.getElementById('current-mileage-modal');
    const carNameEl = document.getElementById('curr-status-car-name') || document.getElementById('curr-mileage-car-name');
    const knownMileageEl = document.getElementById('curr-status-known-mileage') || document.getElementById('curr-mileage-known-val');
    const knownBatteryEl = document.getElementById('curr-status-known-battery');
    const knownFuelEl = document.getElementById('curr-status-known-fuel');

    const mileageInput = document.getElementById('quick-current-mileage');
    const batInput = document.getElementById('quick-current-battery');
    const fuelInput = document.getElementById('quick-current-fuel');

    const batBox = document.getElementById('curr-status-battery-box');
    const fuelBox = document.getElementById('curr-status-fuel-box');

    // 根据动力类型智能展示电量/油量输入框
    const powerType = v.power_type || 'gas';
    if (batBox) {
      if (powerType === 'gas') {
        batBox.classList.add('hidden');
      } else {
        batBox.classList.remove('hidden');
      }
    }
    if (fuelBox) {
      if (powerType === 'ev') {
        fuelBox.classList.add('hidden');
      } else {
        fuelBox.classList.remove('hidden');
      }
    }

    if (carNameEl) carNameEl.textContent = `${v.plate_number || v.name} · ${v.brand} ${v.model}`;
    
    // 当前状态基准默认值：权威来自最后一次录入（当前状态/油耗/电耗）
    const latestStatus = await this.getLatestVehicleStatus(v.id);
    const currMileage = (latestStatus && latestStatus.mileage !== undefined && latestStatus.mileage !== null)
      ? latestStatus.mileage
      : (v.current_mileage || v.initial_mileage || 0);
    if (knownMileageEl) knownMileageEl.textContent = this.formatThousands(currMileage);
    if (mileageInput) {
      mileageInput.value = currMileage || '';
      mileageInput.min = v.initial_mileage || 0;
    }

    // 当前电量百分比默认值：最后一次录入的值
    const lastSoc = (latestStatus && latestStatus.battery_percent !== null && latestStatus.battery_percent !== undefined)
      ? latestStatus.battery_percent
      : await this.getLastKnownBatterySoc(v.id);
    if (knownBatteryEl) knownBatteryEl.textContent = (lastSoc !== null && lastSoc !== undefined) ? (Math.round(lastSoc) + '%') : '--';
    if (batInput) {
      batInput.value = (lastSoc !== null && lastSoc !== undefined) ? lastSoc : '';
    }

    // 当前油量百分比默认值：最后一次录入的值
    const lastFuel = (latestStatus && latestStatus.fuel_percent !== null && latestStatus.fuel_percent !== undefined)
      ? latestStatus.fuel_percent
      : await this.getLastKnownFuelPercent(v.id);
    if (knownFuelEl) knownFuelEl.textContent = (lastFuel !== null && lastFuel !== undefined) ? (Math.round(lastFuel) + '%') : '--';
    if (fuelInput) {
      fuelInput.value = (lastFuel !== null && lastFuel !== undefined) ? lastFuel : '';
    }

    // 默认当前时间与备注清空
    const timeInput = document.getElementById('quick-current-status-time');
    if (timeInput) {
      const now = new Date();
      const localISO = new Date(now.getTime() - now.getTimezoneOffset() * 60000).toISOString().slice(0, 16);
      timeInput.value = localISO;
    }
    const notesInput = document.getElementById('quick-current-status-notes');
    if (notesInput) {
      notesInput.value = '';
    }

    if (modal) modal.classList.remove('hidden');
    if (mileageInput) setTimeout(() => mileageInput.focus(), 100);
  },

  closeCurrentStatusModal() {
    const modal = document.getElementById('current-status-modal') || document.getElementById('current-mileage-modal');
    if (modal) modal.classList.add('hidden');
  },

  openCurrentMileageModal() {
    return this.openCurrentStatusModal();
  },

  closeCurrentMileageModal() {
    return this.closeCurrentStatusModal();
  },

  // 保存当前状态 (当前里程、当前电量%、当前油量%)
  async saveCurrentStatus(e) {
    if (e && e.preventDefault) e.preventDefault();
    if (!this.activeVehicleId) {
      this.showToast('请先选择或录入车辆', 'error');
      return;
    }
    const v = this.getActiveVehicle();
    if (!v) return;

    const mileageInput = document.getElementById('quick-current-mileage');
    const batInput = document.getElementById('quick-current-battery');
    const fuelInput = document.getElementById('quick-current-fuel');

    const newMileage = parseFloat(mileageInput.value);
    if (isNaN(newMileage) || newMileage < 0) {
      this.showToast('请输入有效的里程数', 'error');
      return;
    }

    const initM = parseFloat(v.initial_mileage || 0);
    if (newMileage < initM) {
      this.showToast(`当前里程 (${newMileage} km) 不能小于购车初始里程 (${initM} km)`, 'error');
      return;
    }

    const currKnown = parseFloat(v.current_mileage || initM);
    if (newMileage < currKnown) {
      if (!confirm(`输入的里程 (${newMileage} km) 小于目前已知最高里程 (${currKnown} km)，是否仍确认修改？`)) {
        return;
      }
    }

    let newBat = null;
    if (batInput && batInput.value !== '') {
      newBat = parseFloat(batInput.value);
      if (isNaN(newBat) || newBat < 0 || newBat > 100) {
        this.showToast('请输入有效的电量百分比 (0-100)', 'error');
        return;
      }
    } else if (v.current_battery_percent !== undefined && v.current_battery_percent !== null) {
      newBat = parseFloat(v.current_battery_percent);
    }

    let newFuel = null;
    if (fuelInput && fuelInput.value !== '') {
      newFuel = parseFloat(fuelInput.value);
      if (isNaN(newFuel) || newFuel < 0 || newFuel > 100) {
        this.showToast('请输入有效的油量百分比 (0-100)', 'error');
        return;
      }
    } else if (v.current_fuel_percent !== undefined && v.current_fuel_percent !== null) {
      newFuel = parseFloat(v.current_fuel_percent);
    }

    const now = new Date().toISOString();
    const timeVal = document.getElementById('quick-current-status-time')?.value;
    const recordTime = timeVal ? timeVal.replace('T', ' ') : new Date(Date.now() - new Date().getTimezoneOffset() * 60000).toISOString().slice(0, 19).replace('T', ' ');
    const notesVal = document.getElementById('quick-current-status-notes')?.value.trim() || '';

    // 1. 本地 IndexedDB 离线优先更新
    if (window.CarDB && CarDB.db) {
      v.current_mileage = newMileage;
      if (newBat !== null) v.current_battery_percent = newBat;
      if (newFuel !== null) v.current_fuel_percent = newFuel;
      v.updated_at = now;
      await CarDB.put('vehicles', v);
      await CarDB.enqueueSync('UPDATE', 'vehicles', {
        id: v.id,
        name: v.name,
        brand: v.brand,
        model: v.model,
        power_type: v.power_type,
        displacement: v.displacement,
        fuel_tank_capacity: v.fuel_tank_capacity,
        battery_capacity: v.battery_capacity,
        initial_fuel_percent: v.initial_fuel_percent,
        initial_battery_percent: v.initial_battery_percent,
        current_fuel_percent: v.current_fuel_percent,
        current_battery_percent: v.current_battery_percent,
        plate_number: v.plate_number,
        initial_mileage: v.initial_mileage,
        current_mileage: newMileage,
        purchase_date: v.purchase_date,
        notes: v.notes
      });

      // 沉淀一条状态快照记录到 status_records，以便在历史明细中查看和修改
      const statusRecord = {
        id: Date.now(),
        vehicle_id: v.id,
        mileage: newMileage,
        battery_percent: newBat,
        fuel_percent: newFuel,
        record_date: recordTime,
        notes: notesVal,
        created_at: now
      };
      await CarDB.put('status_records', statusRecord);
      await CarDB.enqueueSync('CREATE', 'status', statusRecord);
    }

    this.closeCurrentStatusModal();
    let toastMsg = `车辆当前状态已记录: ${newMileage} km`;
    if (newBat !== null && v.power_type !== 'gas') toastMsg += ` · 电量 ${newBat}%`;
    if (newFuel !== null && v.power_type !== 'ev') toastMsg += ` · 油量 ${newFuel}%`;
    this.showToast(toastMsg, 'success');

    // 2. 联网静默同步
    if (navigator.onLine && window.CarSync) {
      CarSync.sync(true);
    }

    // 3. 刷新视图与表单预填默认值，并联动更新历史明细
    await this.loadVehicles();
    this.loadHistory();
    this.switchTab('overview');
  },

  saveCurrentMileage(e) {
    return this.saveCurrentStatus(e);
  },

  // ================= 能耗分析模块 (离线优先) =================
  async loadEnergyAnalysis() {
    if (!this.activeVehicleId) return;
    const v = this.getActiveVehicle();
    if (v) {
      const titleEl = document.getElementById('energy-car-title');
      if (titleEl) {
        titleEl.textContent = `${v.plate_number || v.name} · ${v.brand} ${v.model}`;
      }
    }

    let analysis = null;
    const baseUrl = window.CarSync ? await CarSync.getApiBaseUrl() : '';

    if (navigator.onLine) {
      try {
        const res = await fetch(`${baseUrl}/api/energy-analysis?vehicle_id=${this.activeVehicleId}`);
        if (res.ok) {
          analysis = await res.json();
        }
      } catch (err) {
        console.warn('云端能耗分析加载异常，切换为本地离线分析引擎:', err);
      }
    }

    if (!analysis || !analysis.summary) {
      analysis = await this.computeLocalEnergyAnalysis(this.activeVehicleId);
    }

    this.energyAnalysisData = analysis;
    if (!this.energyAnalysisData || !this.energyAnalysisData.summary) {
      return;
    }

    this.renderEnergySummary();
    this.renderEnergyChart();
    this.renderEnergyIntervals();
  },

  // 本地 IndexedDB 离线能耗区间与走势分析计算引擎
  async computeLocalEnergyAnalysis(vehicleId) {
    if (!window.CarDB || !CarDB.db) return null;
    const vehicles = await CarDB.getAll('vehicles');
    const v = vehicles.find(item => item.id == vehicleId) || this.getActiveVehicle();
    if (!v) return null;

    let fuelRows = (await CarDB.getAll('fuel_records')).filter(r => r.vehicle_id == vehicleId);
    let elecRows = (await CarDB.getAll('electricity_records')).filter(r => r.vehicle_id == vehicleId);

    // 升序排列计算区间
    fuelRows.sort((a, b) => (parseFloat(a.mileage) - parseFloat(b.mileage)) || (a.record_date > b.record_date ? 1 : -1));
    elecRows.sort((a, b) => (parseFloat(a.mileage) - parseFloat(b.mileage)) || (a.record_date > b.record_date ? 1 : -1));

    const initialMileage = parseFloat(v.initial_mileage || 0);
    const currentMileage = parseFloat(v.current_mileage || initialMileage);
    const totalDrivenKm = Math.max(0, currentMileage - initialMileage);

    const parseDate = (dStr) => {
      if (!dStr) return null;
      const d = new Date(dStr.replace(/-/g, '/'));
      return isNaN(d.getTime()) ? null : d;
    };

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
        date: parseDate(r.record_date)
      });
    });
    elecRows.forEach(r => {
      unifiedRecords.push({
        _type: 'electricity',
        data: r,
        mileage: parseFloat(r.mileage) || 0,
        date: parseDate(r.record_date)
      });
    });
    unifiedRecords.sort((a, b) => (a.mileage - b.mileage) || ((a.date || 0) - (b.date || 0)) || (a.data.id - b.data.id));

    let prevM = initialMileage;
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
      return parseFloat(totalCost.toFixed(2));
    };

    unifiedRecords.forEach(item => {
      const r = item.data;
      const currM = item.mileage;
      const currDate = item.date;
      const deltaM = Math.max(0, currM - prevM);
      const daysDiff = (prevDate && currDate) ? Math.max(0, Math.floor((currDate - prevDate) / (1000 * 3600 * 24))) : null;
      const driveBatCap = activeBatCap;

      let currStartFuelPct, currEndFuelPct, currStartBatPct, currEndBatPct;
      const amount = parseFloat(item._type === 'fuel' ? r.fuel_amount : r.electricity_amount) || 0;
      const cost = parseFloat(r.total_price) || 0;

      if (item._type === 'fuel') {
        fuelOrder += 1;
        allSeenFuels.push(r);
        // 油量：加前初始值与加后结束值
        currEndFuelPct = parseFloat(r.fuel_percent !== undefined && r.fuel_percent !== null ? r.fuel_percent : 100);
        if (r.start_fuel_percent !== undefined && r.start_fuel_percent !== null && r.start_fuel_percent !== '') {
          currStartFuelPct = parseFloat(r.start_fuel_percent);
        } else if (fuelTankCap > 0) {
          currStartFuelPct = Math.max(0, currEndFuelPct - (amount / fuelTankCap) * 100);
        } else {
          currStartFuelPct = prevFuelEnd;
        }

        // 电量：加油不改变电量，初始值与结束值均为当前记录电量（若未记则继承上次结束值）
        const currBatVal = (r.battery_percent !== undefined && r.battery_percent !== null && r.battery_percent !== '') ? parseFloat(r.battery_percent) : null;
        currStartBatPct = currBatVal !== null ? currBatVal : prevBatEnd;
        currEndBatPct = currStartBatPct;
      } else {
        elecOrder += 1;
        allSeenElecs.push(r);
        // 电量：充前初始值与充后结束值
        currEndBatPct = parseFloat(r.end_soc !== undefined && r.end_soc !== null ? r.end_soc : 100);
        if (r.start_soc !== undefined && r.start_soc !== null && r.start_soc !== '') {
          currStartBatPct = parseFloat(r.start_soc);
        } else if (activeBatCap > 0) {
          currStartBatPct = Math.max(0, currEndBatPct - (amount / activeBatCap) * 100);
        } else {
          currStartBatPct = prevBatEnd;
        }

        // 油量：充电不改变油量，初始值与结束值均为当前记录油量（若未记则继承上次结束值）
        const currFuelVal = (r.fuel_percent !== undefined && r.fuel_percent !== null && r.fuel_percent !== '') ? parseFloat(r.fuel_percent) : null;
        currStartFuelPct = currFuelVal !== null ? currFuelVal : prevFuelEnd;
        currEndFuelPct = currStartFuelPct;
      }

      // 核心算法：单次能耗 = 上次补能结束值 - 本次补能初始值
      const deltaFuelPct = Math.max(0, prevFuelEnd - currStartFuelPct);
      const deltaBatPct = Math.max(0, prevBatEnd - currStartBatPct);

      const consumedFuel = fuelTankCap > 0 ? parseFloat(((deltaFuelPct / 100.0) * fuelTankCap).toFixed(2)) : 0.0;
      const consumedElec = driveBatCap > 0 ? parseFloat(((deltaBatPct / 100.0) * driveBatCap).toFixed(2)) : 0.0;

      // 电池校准：若本次记录开启校准，则更新后续电池容量
      let isCal = (r.is_calibrated === 1 || r.is_calibrated === '1' || r.is_calibrated === true) ? 1 : 0;
      let calCap = null;
      if (isCal && r.calibrated_capacity !== null && r.calibrated_capacity !== undefined && r.calibrated_capacity !== '') {
        calCap = Math.round(parseFloat(r.calibrated_capacity) * 100) / 100;
        if (calCap > 0) {
          activeBatCap = calCap;
        }
      }
      const consumedEquivElec = parseFloat((consumedElec + consumedFuel * 3.3).toFixed(2));

      cumFuelConsumed += consumedFuel;
      cumElecConsumed += consumedElec;
      const cumEquivElecConsumed = parseFloat((cumElecConsumed + cumFuelConsumed * 3.3).toFixed(2));

      const cumM = Math.max(0, currM - initialMileage);
      const cumFuelCons = cumM > 0 ? parseFloat(((cumFuelConsumed / cumM) * 100).toFixed(2)) : 0.0;
      const cumElecCons = cumM > 0 ? parseFloat(((cumElecConsumed / cumM) * 100).toFixed(2)) : 0.0;
      const cumEquivElecCons = cumM > 0 ? parseFloat(((cumEquivElecConsumed / cumM) * 100).toFixed(2)) : 0.0;

      const intervalFuelCons = deltaM > 0 ? parseFloat(((consumedFuel / deltaM) * 100).toFixed(2)) : 0.0;
      const intervalElecCons = deltaM > 0 ? parseFloat(((consumedElec / deltaM) * 100).toFixed(2)) : 0.0;
      const intervalEquivElecCons = deltaM > 0 ? parseFloat(((consumedEquivElec / deltaM) * 100).toFixed(2)) : 0.0;
      const intervalCostKm = deltaM > 0 ? parseFloat((cost / deltaM).toFixed(2)) : 0.0;

      // 先进先出 (FIFO) 实耗金额与每公里成本 / 百公里成本
      const fifoFuelCost = computeFifoCost(cumFuelConsumed, allSeenFuels, 'fuel_amount', 'unit_price');
      const fifoElecCost = computeFifoCost(cumElecConsumed, allSeenElecs, 'electricity_amount', 'unit_price');
      const fifoEnergyCost = parseFloat((fifoFuelCost + fifoElecCost).toFixed(2));
      const fifoCostPerKm = cumM > 0 ? parseFloat((fifoEnergyCost / cumM).toFixed(2)) : 0.0;
      const fifoCostPerHundred = cumM > 0 ? parseFloat(((fifoEnergyCost / cumM) * 100).toFixed(2)) : 0.0;

      const intervalItem = {
        order: item._type === 'fuel' ? fuelOrder : elecOrder,
        type: item._type,
        id: r.id,
        record_date: r.record_date,
        mileage: currM,
        prev_mileage: prevM,
        delta_mileage: parseFloat(deltaM.toFixed(1)),
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
        prev_fuel_percent: parseFloat(prevFuelEnd.toFixed(1)),
        curr_start_fuel_percent: parseFloat(currStartFuelPct.toFixed(1)),
        curr_end_fuel_percent: parseFloat(currEndFuelPct.toFixed(1)),
        prev_battery_percent: parseFloat(prevBatEnd.toFixed(1)),
        curr_start_battery_percent: parseFloat(currStartBatPct.toFixed(1)),
        curr_end_battery_percent: parseFloat(currEndBatPct.toFixed(1)),
        delta_fuel_percent: parseFloat(deltaFuelPct.toFixed(1)),
        delta_battery_percent: parseFloat(deltaBatPct.toFixed(1)),
        prev_percent: parseFloat((item._type === 'fuel' ? prevFuelEnd : prevBatEnd).toFixed(1)),
        curr_start_percent: parseFloat((item._type === 'fuel' ? currStartFuelPct : currStartBatPct).toFixed(1)),
        curr_end_percent: parseFloat((item._type === 'fuel' ? currEndFuelPct : currEndBatPct).toFixed(1)),
        fuel_percent: parseFloat(currEndFuelPct.toFixed(1)),
        battery_percent: parseFloat(currEndBatPct.toFixed(1)),
        cumulative_mileage: parseFloat(cumM.toFixed(1)),
        cumulative_consumed_amount: parseFloat((item._type === 'fuel' ? cumFuelConsumed : cumElecConsumed).toFixed(2)),
        cumulative_consumption: item._type === 'fuel' ? cumFuelCons : cumElecCons,
        cumulative_fuel_consumed: parseFloat(cumFuelConsumed.toFixed(2)),
        cumulative_fuel_consumption: cumFuelCons,
        cumulative_elec_consumed: parseFloat(cumElecConsumed.toFixed(2)),
        cumulative_elec_consumption: cumElecCons,
        cumulative_equivalent_elec_consumed: cumEquivElecConsumed,
        cumulative_equivalent_elec_consumption: cumEquivElecCons,
        unit: item._type === 'fuel' ? 'L' : 'kWh',
        unit_price: parseFloat(r.unit_price) || 0,
        total_price: cost,
        fuel_type: r.fuel_type || '',
        charging_type: r.charging_type || '',
        is_full: r.is_full !== undefined ? r.is_full : 1,
        start_soc: r.start_soc !== undefined ? r.start_soc : null,
        end_soc: r.end_soc !== undefined ? r.end_soc : null,
        station: r.station || '',
        notes: r.notes || '',
        is_calibrated: item._type === 'electricity' ? isCal : 0,
        calibrated_capacity: item._type === 'electricity' ? calCap : null,
        active_battery_capacity: activeBatCap,
        drive_battery_capacity: driveBatCap,
        interval_consumption: item._type === 'fuel' ? intervalFuelCons : intervalElecCons,
        cost_per_km: intervalCostKm,
        days_since_prev: daysDiff
      };

      if (item._type === 'fuel') {
        fuelIntervals.push(intervalItem);
      } else {
        elecIntervals.push(intervalItem);
      }
      allIntervals.push(intervalItem);

      prevM = currM;
      prevDate = currDate;
      prevFuelEnd = currEndFuelPct;
      prevBatEnd = currEndBatPct;
    });

    // 检查从最后一次补能记录到车辆当前最新状态的行驶消耗 (在途/未补能状态)
    const ongoingDeltaM = parseFloat(Math.max(0, currentMileage - prevM).toFixed(1));
    const currFuelPct = (v.current_fuel_percent !== undefined && v.current_fuel_percent !== null && v.current_fuel_percent !== '') ? parseFloat(v.current_fuel_percent) : prevFuelEnd;
    const currBatPct = (v.current_battery_percent !== undefined && v.current_battery_percent !== null && v.current_battery_percent !== '') ? parseFloat(v.current_battery_percent) : prevBatEnd;

    const deltaFuelPct = Math.max(0, prevFuelEnd - currFuelPct);
    const deltaBatPct = Math.max(0, prevBatEnd - currBatPct);

    const ongoingFuelConsumed = fuelTankCap > 0 ? parseFloat(((deltaFuelPct / 100.0) * fuelTankCap).toFixed(2)) : 0.0;
    const ongoingElecConsumed = activeBatCap > 0 ? parseFloat(((deltaBatPct / 100.0) * activeBatCap).toFixed(2)) : 0.0;
    const ongoingEquivElecConsumed = parseFloat((ongoingElecConsumed + ongoingFuelConsumed * 3.3).toFixed(2));

    cumFuelConsumed += ongoingFuelConsumed;
    cumElecConsumed += ongoingElecConsumed;
    const totalEquivElecConsumed = parseFloat((cumElecConsumed + cumFuelConsumed * 3.3).toFixed(2));

    const fifoFuelCost = computeFifoCost(cumFuelConsumed, allSeenFuels, 'fuel_amount', 'unit_price');
    const fifoElecCost = computeFifoCost(cumElecConsumed, allSeenElecs, 'electricity_amount', 'unit_price');
    const fifoEnergyCost = parseFloat((fifoFuelCost + fifoElecCost).toFixed(2));

    if (ongoingDeltaM > 0 || ongoingFuelConsumed > 0 || ongoingElecConsumed > 0) {
      const ongoingItem = {
        order: allIntervals.length + 1,
        type: 'ongoing',
        id: 'current_status',
        record_date: v.updated_at || '当前最新状态',
        mileage: currentMileage,
        prev_mileage: prevM,
        delta_mileage: ongoingDeltaM,
        amount: 0,
        consumed_amount: v.power_type === 'ev' ? ongoingElecConsumed : (v.power_type === 'gas' ? ongoingFuelConsumed : ongoingEquivElecConsumed),
        interval_fuel_consumed: ongoingFuelConsumed,
        interval_fuel_consumption: ongoingDeltaM > 0 ? parseFloat(((ongoingFuelConsumed / ongoingDeltaM) * 100).toFixed(2)) : 0.0,
        interval_elec_consumed: ongoingElecConsumed,
        interval_elec_consumption: ongoingDeltaM > 0 ? parseFloat(((ongoingElecConsumed / ongoingDeltaM) * 100).toFixed(2)) : 0.0,
        interval_equivalent_elec_consumed: ongoingEquivElecConsumed,
        interval_equivalent_elec_consumption: ongoingDeltaM > 0 ? parseFloat(((ongoingEquivElecConsumed / ongoingDeltaM) * 100).toFixed(2)) : 0.0,
        fifo_fuel_cost: fifoFuelCost,
        fifo_elec_cost: fifoElecCost,
        fifo_energy_cost: fifoEnergyCost,
        fifo_cost_per_km: totalDrivenKm > 0 ? parseFloat((fifoEnergyCost / totalDrivenKm).toFixed(2)) : 0.0,
        fifo_cost_per_hundred: totalDrivenKm > 0 ? parseFloat(((fifoEnergyCost / totalDrivenKm) * 100).toFixed(2)) : 0.0,
        prev_fuel_percent: parseFloat(prevFuelEnd.toFixed(1)),
        curr_start_fuel_percent: parseFloat(prevFuelEnd.toFixed(1)),
        curr_end_fuel_percent: parseFloat(currFuelPct.toFixed(1)),
        prev_battery_percent: parseFloat(prevBatEnd.toFixed(1)),
        curr_start_battery_percent: parseFloat(prevBatEnd.toFixed(1)),
        curr_end_battery_percent: parseFloat(currBatPct.toFixed(1)),
        fuel_percent: parseFloat(currFuelPct.toFixed(1)),
        battery_percent: parseFloat(currBatPct.toFixed(1)),
        cumulative_mileage: parseFloat(totalDrivenKm.toFixed(1)),
        cumulative_consumed_amount: parseFloat((v.power_type === 'gas' ? cumFuelConsumed : cumElecConsumed).toFixed(2)),
        cumulative_consumption: totalDrivenKm > 0 ? parseFloat((( (v.power_type === 'gas' ? cumFuelConsumed : cumElecConsumed) / totalDrivenKm) * 100).toFixed(2)) : 0.0,
        cumulative_fuel_consumed: parseFloat(cumFuelConsumed.toFixed(2)),
        cumulative_fuel_consumption: totalDrivenKm > 0 ? parseFloat(((cumFuelConsumed / totalDrivenKm) * 100).toFixed(2)) : 0.0,
        cumulative_elec_consumed: parseFloat(cumElecConsumed.toFixed(2)),
        cumulative_elec_consumption: totalDrivenKm > 0 ? parseFloat(((cumElecConsumed / totalDrivenKm) * 100).toFixed(2)) : 0.0,
        cumulative_equivalent_elec_consumed: totalEquivElecConsumed,
        cumulative_equivalent_elec_consumption: totalDrivenKm > 0 ? parseFloat(((totalEquivElecConsumed / totalDrivenKm) * 100).toFixed(2)) : 0.0,
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

    const allCombined = allIntervals;
    const allCombinedSorted = [...allCombined].sort((a, b) => (b.mileage - a.mileage) || (String(b.record_date || '') > String(a.record_date || '') ? 1 : -1));

    // 全局汇总指标 (油耗与电耗总里程均使用车辆当前总行驶里程)
    const totalFuelAmount = parseFloat(fuelRows.reduce((s, r) => s + (parseFloat(r.fuel_amount) || 0), 0).toFixed(2));
    const totalFuelCost = parseFloat(fuelRows.reduce((s, r) => s + (parseFloat(r.total_price) || 0), 0).toFixed(2));
    const fuelDrivenKm = totalDrivenKm;
    const totalFuelConsumed = parseFloat(cumFuelConsumed.toFixed(2));
    const overallFuelAvg = (fuelDrivenKm > 0 && totalFuelConsumed > 0) ? parseFloat(((totalFuelConsumed / fuelDrivenKm) * 100).toFixed(2)) : 0.0;

    const totalElecAmount = parseFloat(elecRows.reduce((s, r) => s + (parseFloat(r.electricity_amount) || 0), 0).toFixed(2));
    const totalElecCost = parseFloat(elecRows.reduce((s, r) => s + (parseFloat(r.total_price) || 0), 0).toFixed(2));
    const elecDrivenKm = totalDrivenKm;
    const totalElecConsumed = parseFloat(cumElecConsumed.toFixed(2));
    const overallElecAvg = (elecDrivenKm > 0 && totalElecConsumed > 0) ? parseFloat(((totalElecConsumed / elecDrivenKm) * 100).toFixed(2)) : 0.0;

    const overallEquivElecAvg = (totalDrivenKm > 0 && totalEquivElecConsumed > 0) ? parseFloat(((totalEquivElecConsumed / totalDrivenKm) * 100).toFixed(2)) : 0.0;

    const totalEnergyCost = parseFloat((totalFuelCost + totalElecCost).toFixed(2));
    const overallEnergyCostPerKm = totalDrivenKm > 0 ? parseFloat((totalEnergyCost / totalDrivenKm).toFixed(2)) : 0.0;

    const validFuelCons = fuelIntervals.map(x => x.interval_consumption).filter(c => c > 0);
    const bestFuelCons = validFuelCons.length ? Math.min(...validFuelCons) : 0.0;
    const worstFuelCons = validFuelCons.length ? Math.max(...validFuelCons) : 0.0;

    const validElecCons = elecIntervals.map(x => x.interval_consumption).filter(c => c > 0);
    const bestElecCons = validElecCons.length ? Math.min(...validElecCons) : 0.0;
    const worstElecCons = validElecCons.length ? Math.max(...validElecCons) : 0.0;

    const validEquivCons = allCombined.map(x => x.interval_equivalent_elec_consumption).filter(c => c > 0);
    const bestEquivCons = validEquivCons.length ? Math.min(...validEquivCons) : 0.0;
    const worstEquivCons = validEquivCons.length ? Math.max(...validEquivCons) : 0.0;

    const allDeltas = allCombined.map(x => x.delta_mileage).filter(d => d > 0);
    const avgIntervalKm = allDeltas.length ? parseFloat((allDeltas.reduce((s, d) => s + d, 0) / allDeltas.length).toFixed(1)) : 0.0;

    const allCosts = allCombined.map(x => x.total_price);
    const avgIntervalCost = allCosts.length ? parseFloat((allCosts.reduce((s, c) => s + c, 0) / allCosts.length).toFixed(2)) : 0.0;

    // FIFO 终极指标：以当前购车行驶总里程为基准进行核算
    const lastItem = allIntervals.length > 0 ? allIntervals[allIntervals.length - 1] : null;
    const fifoDrivenKm = totalDrivenKm > 0 ? parseFloat(totalDrivenKm.toFixed(1)) : (lastItem ? lastItem.cumulative_mileage : 0.0);
    const fifoCostPerKm = fifoDrivenKm > 0 ? parseFloat((fifoEnergyCost / fifoDrivenKm).toFixed(2)) : 0.0;
    const fifoCostPerHundred = fifoDrivenKm > 0 ? parseFloat(((fifoEnergyCost / fifoDrivenKm) * 100).toFixed(2)) : 0.0;
    const latestReplenishMileage = (parseFloat(v.current_mileage) || (lastItem ? lastItem.mileage : initialMileage));

    return {
      vehicle: v,
      summary: {
        total_driven_km: parseFloat(totalDrivenKm.toFixed(1)),
        total_energy_cost: totalEnergyCost,
        overall_energy_cost_per_km: overallEnergyCostPerKm,
        fifo_energy_cost: fifoEnergyCost,
        fifo_cost_per_km: fifoCostPerKm,
        fifo_cost_per_hundred: fifoCostPerHundred,
        fifo_driven_km: fifoDrivenKm,
        fifo_fuel_cost: fifoFuelCost,
        fifo_elec_cost: fifoElecCost,
        latest_replenish_mileage: latestReplenishMileage,
        total_replenish_count: allCombined.length,
        avg_interval_km: avgIntervalKm,
        avg_interval_cost: avgIntervalCost,
        fuel: {
          count: fuelRows.length,
          total_amount: totalFuelAmount,
          total_consumed: totalFuelConsumed,
          driven_km: parseFloat(fuelDrivenKm.toFixed(1)),
          total_cost: totalFuelCost,
          overall_avg: overallFuelAvg,
          best_consumption: bestFuelCons,
          worst_consumption: worstFuelCons
        },
        electricity: {
          count: elecRows.length,
          total_amount: totalElecAmount,
          total_consumed: totalElecConsumed,
          driven_km: parseFloat(elecDrivenKm.toFixed(1)),
          total_cost: totalElecCost,
          overall_avg: overallElecAvg,
          nominal_battery_capacity: batteryCap,
          current_battery_capacity: activeBatCap,
          equivalent_total_consumed: totalEquivElecConsumed,
          equivalent_overall_avg: overallEquivElecAvg,
          best_consumption: bestElecCons,
          worst_consumption: worstElecCons
        },
        equivalent_electricity: {
          total_consumed: totalEquivElecConsumed,
          overall_avg: overallEquivElecAvg,
          driven_km: parseFloat(totalDrivenKm.toFixed(1)),
          best_consumption: bestEquivCons,
          worst_consumption: worstEquivCons,
          formula: '1L/100km = 3.3 kWh/100km'
        }
      },
      fuel_intervals: fuelIntervals,
      elec_intervals: elecIntervals,
      recent_intervals: allCombinedSorted
    };
  },

  renderEnergySummary() {
    if (!this.energyAnalysisData) return;
    const summary = this.energyAnalysisData.summary;

    const setTxt = (id, txt) => {
      const el = document.getElementById(id);
      if (el) el.textContent = txt;
    };

    // 燃油指标 (累计百公里油耗与累计耗油量)
    setTxt('ea-overall-fuel', this.formatThousands(summary.fuel.overall_avg, 2));
    setTxt('ea-fuel-count', this.formatThousands(summary.fuel.count));
    setTxt('ea-fuel-total-amount', this.formatThousands(summary.fuel.total_consumed !== undefined ? summary.fuel.total_consumed : summary.fuel.total_amount, 1));
    setTxt('ea-fuel-total-cost', this.formatThousands(summary.fuel.total_cost, 2));

    // 电力指标 (累计百公里电耗与累计耗电量)
    setTxt('ea-overall-elec', this.formatThousands(summary.electricity.overall_avg, 2));
    setTxt('ea-elec-count', this.formatThousands(summary.electricity.count));
    setTxt('ea-elec-total-amount', this.formatThousands(summary.electricity.total_consumed !== undefined ? summary.electricity.total_consumed : summary.electricity.total_amount, 1));
    setTxt('ea-elec-total-cost', this.formatThousands(summary.electricity.total_cost, 2));

    // 等效电耗指标 (油耗折算 1L=3.3kWh)
    const equivAvg = summary.electricity?.equivalent_overall_avg ?? summary.equivalent_electricity?.overall_avg ?? 0;
    const equivTotal = summary.electricity?.equivalent_total_consumed ?? summary.equivalent_electricity?.total_consumed ?? 0;
    setTxt('ea-overall-equiv', this.formatThousands(equivAvg, 2));
    setTxt('ea-equiv-total-consumed', this.formatThousands(equivTotal, 1));
    setTxt('ea-card-equiv-val', this.formatThousands(equivAvg, 2));
    setTxt('ea-card-equiv-total', this.formatThousands(equivTotal, 1));

    const eaCardEquiv = document.getElementById('ea-card-equiv');
    if (eaCardEquiv) {
      const activeV = this.getActiveVehicle();
      const isDual = activeV && (activeV.power_type === 'erev' || activeV.power_type === 'phev' || activeV.power_type === 'hybrid');
      if (isDual || summary.fuel?.count > 0) {
        eaCardEquiv.classList.remove('hidden');
      } else {
        eaCardEquiv.classList.add('hidden');
      }
    }

    // 能耗总支出与每公里成本
    setTxt('ea-total-energy-cost', this.formatThousands(summary.total_energy_cost, 2));
    setTxt('ea-cost-per-km', this.formatThousands(summary.overall_energy_cost_per_km, 2));

    // 补能节奏均值
    setTxt('ea-avg-interval-km', this.formatThousands(summary.avg_interval_km, 1));
    setTxt('ea-avg-interval-cost', this.formatThousands(summary.avg_interval_cost, 2));

    // 极值分析：历史最省能耗与历史最高能耗（两项数字均从百公里等效电耗获取）
    const intervals = (this.energyAnalysisData.recent_intervals || []).slice();
    const validEquivIntervals = intervals.filter(x => {
      const val = (x.interval_equivalent_elec_consumption !== undefined && x.interval_equivalent_elec_consumption !== null)
        ? x.interval_equivalent_elec_consumption
        : (x.interval_consumption || 0);
      return val > 0;
    });

    let bestItem = null;
    let worstItem = null;
    let bestEquiv = summary.equivalent_electricity?.best_consumption || 0;
    let worstEquiv = summary.equivalent_electricity?.worst_consumption || 0;

    if (validEquivIntervals.length > 0) {
      const getVal = (item) => (item.interval_equivalent_elec_consumption !== undefined && item.interval_equivalent_elec_consumption !== null)
        ? item.interval_equivalent_elec_consumption
        : (item.interval_consumption || 0);

      validEquivIntervals.forEach(x => {
        const val = getVal(x);
        if (!bestItem || val < getVal(bestItem)) {
          bestItem = x;
        }
        if (!worstItem || val > getVal(worstItem)) {
          worstItem = x;
        }
      });
      if (bestItem) {
        bestEquiv = getVal(bestItem);
      }
      if (worstItem) {
        worstEquiv = getVal(worstItem);
      }
    }

    if (bestEquiv > 0) {
      setTxt('ea-best-badge', this.formatThousands(bestEquiv, 2));
      if (bestItem) {
        const fromM = bestItem.prev_mileage !== undefined ? bestItem.prev_mileage : 0;
        const toM = bestItem.mileage;
        const delta = bestItem.delta_mileage || Math.max(0, toM - fromM);
        setTxt('ea-best-description', `区间行程 +${this.formatThousands(delta)}km (${this.formatThousands(fromM)}➔${this.formatThousands(toM)}km)`);
      } else {
        setTxt('ea-best-description', '单次行程百公里等效最省');
      }
    } else {
      setTxt('ea-best-badge', '-');
      setTxt('ea-best-description', '暂无足够区间数据');
    }

    if (worstEquiv > 0) {
      setTxt('ea-worst-badge', this.formatThousands(worstEquiv, 2));
      if (worstItem) {
        const fromM = worstItem.prev_mileage !== undefined ? worstItem.prev_mileage : 0;
        const toM = worstItem.mileage;
        const delta = worstItem.delta_mileage || Math.max(0, toM - fromM);
        setTxt('ea-worst-description', `区间行程 +${this.formatThousands(delta)}km (${this.formatThousands(fromM)}➔${this.formatThousands(toM)}km)`);
      } else {
        setTxt('ea-worst-description', '单次行程百公里等效峰值');
      }
    } else {
      setTxt('ea-worst-badge', '-');
      setTxt('ea-worst-description', '暂无足够区间数据');
    }
  },

  setEnergyChartTab(tab) {
    if (tab === 'equiv') tab = 'equivalent';
    if (tab === 'elec') tab = 'electricity';
    this.energyChartTab = tab;
    ['both', 'fuel', 'elec', 'equiv'].forEach(t => {
      const btn = document.getElementById(`ec-tab-${t}`);
      if (btn) {
        const isActive = (tab === 'both' && t === 'both') ||
                         (tab === 'fuel' && t === 'fuel') ||
                         ((tab === 'electricity' || tab === 'elec') && t === 'elec') ||
                         ((tab === 'equivalent' || tab === 'equiv') && t === 'equiv');
        if (isActive) {
          btn.className = 'px-2.5 py-1 rounded-lg font-medium transition bg-white text-blue-600 shadow-sm whitespace-nowrap';
        } else {
          btn.className = 'px-2.5 py-1 rounded-lg font-medium transition text-slate-600 hover:text-slate-900 whitespace-nowrap';
        }
      }
    });
    this.renderEnergyChart();
  },

  onEnergyYearChange(year) {
    this.selectedEnergyYear = year;
    this.renderEnergyChart();
  },

  // 原生 SVG 柱状图生成器：起点为车辆初始化状态或年度起点，展示每次行程间的百公里能耗
  generateSvgBarChart({ points, title, unit, color, gradientFrom, gradientTo, gradientId, latestVal, avgVal, avgLabel = '均值', initialMileage = 0, startDesc = '' }) {
    if (!points || points.length === 0) {
      return `
        <div class="p-6 rounded-xl border border-slate-200 bg-slate-50/50 text-center text-slate-400 text-xs">
          暂无 ${title} 柱状数据
        </div>
      `;
    }

    const n = points.length;
    const padding = { top: 32, right: 35, bottom: 62, left: 48 };
    const colWidth = Math.max(76, Math.min(110, Math.floor(480 / n)));
    const width = Math.max(540, padding.left + padding.right + n * colWidth);
    const height = 210;

    const plotW = width - padding.left - padding.right;
    const plotH = height - padding.top - padding.bottom;

    const values = points.map(p => p.val);
    let maxVal = Math.max(...values, avgVal || 0);
    if (maxVal <= 0) {
      maxVal = 10;
    } else {
      maxVal = Math.ceil(maxVal * 1.25);
      if (maxVal < 5) maxVal = 5;
    }

    const slotW = plotW / n;
    const barWidth = Math.min(42, Math.max(22, slotW * 0.52));

    // Y 轴刻度与参考水平网格线 (0, 33%, 66%, 100%)
    let gridLines = '';
    const steps = 3;
    for (let step = 0; step <= steps; step++) {
      const val = (maxVal / steps) * step;
      const y = padding.top + plotH - (step / steps) * plotH;
      gridLines += `
        <line x1="${padding.left}" y1="${y}" x2="${width - padding.right}" y2="${y}" stroke="#f1f5f9" stroke-width="1" stroke-dasharray="3 3"/>
        <text x="${padding.left - 6}" y="${y + 3.5}" text-anchor="end" font-size="9" fill="#94a3b8" font-family="monospace">${val >= 10 ? val.toFixed(0) : val.toFixed(1)}</text>
      `;
    }

    // 均值基准横虚线
    let avgLine = '';
    if (avgVal > 0 && avgVal <= maxVal) {
      const avgY = padding.top + plotH - (avgVal / maxVal) * plotH;
      avgLine = `
        <line x1="${padding.left}" y1="${avgY}" x2="${width - padding.right}" y2="${avgY}" stroke="${color}" stroke-width="1.5" stroke-dasharray="4 4" stroke-opacity="0.8"/>
        <text x="${width - padding.right}" y="${avgY - 4}" text-anchor="end" font-size="8.5" font-weight="bold" fill="${color}">${avgLabel} ${avgVal}</text>
      `;
    }

    // X 轴起点标记 (车辆初始化状态或年度起点)
    const initMarkerX = padding.left;
    const initialMarker = `
      <g class="initial-start-indicator">
        <line x1="${initMarkerX}" y1="${padding.top + 5}" x2="${initMarkerX}" y2="${padding.top + plotH}" stroke="#cbd5e1" stroke-width="1.5" stroke-dasharray="2 2"/>
        <circle cx="${initMarkerX}" cy="${padding.top + plotH}" r="3" fill="#64748b"/>
        <text x="${initMarkerX}" y="${padding.top + plotH + 14}" text-anchor="middle" font-size="8" font-weight="bold" fill="#64748b">起点</text>
        <text x="${initMarkerX}" y="${padding.top + plotH + 24}" text-anchor="middle" font-size="7.5" font-family="monospace" fill="#94a3b8">${this.formatThousands(initialMileage)}km</text>
      </g>
    `;

    // 柱子及底部说明
    let barsHtml = '';
    points.forEach((p, i) => {
      const centerX = padding.left + (i + 0.5) * slotW;
      const barX = centerX - barWidth / 2;
      const isZero = p.val <= 0;
      const barH = isZero ? 3 : Math.max(4, (p.val / maxVal) * plotH);
      const barY = padding.top + plotH - barH;

      const fromMileage = p.prev_mileage !== undefined ? p.prev_mileage : (i === 0 ? initialMileage : 0);
      const toMileage = p.mileage;
      const deltaMileage = p.delta_mileage || Math.max(0, toMileage - fromMileage);

      const spanText = (i === 0) ? `初始${this.formatThousands(fromMileage)}➔${this.formatThousands(toMileage)}` : `${this.formatThousands(fromMileage)}➔${this.formatThousands(toMileage)}`;
      const tooltip = `第 ${i + 1} 次行程：${i === 0 ? '初始 ' : ''}${this.formatThousands(fromMileage)}km ➔ ${this.formatThousands(toMileage)}km\n区间行驶: ${this.formatThousands(deltaMileage)} km\n百公里能耗: ${this.formatThousands(p.val, 2)} ${unit}\n实际耗量: ${this.formatThousands(p.consumed_amount ?? 0)} ${unit === 'L/100km' ? 'L' : 'kWh'}\n记录时间: ${p.record_date || '未知'}`;

      barsHtml += `
        <g class="bar-group cursor-pointer">
          <title>${tooltip}</title>
          <!-- 柱体 -->
          <rect x="${barX}" y="${barY}" width="${barWidth}" height="${barH}" rx="4" ry="4"
                fill="${isZero ? '#e2e8f0' : `url(#${gradientId})`}"
                stroke="${isZero ? '#cbd5e1' : color}"
                stroke-width="${isZero ? '1' : '1.5'}"
                opacity="${isZero ? '0.7' : '1'}"
                class="transition-all hover:opacity-85"/>
          <!-- 柱顶数值 -->
          <text x="${centerX}" y="${barY - 5}" text-anchor="middle" font-size="9.5" font-weight="bold"
                fill="${isZero ? '#94a3b8' : color}" font-family="monospace">
            ${this.formatThousands(p.val, 2)}
          </text>
          <!-- 底部行程行驶里程 (+X km) -->
          <text x="${centerX}" y="${padding.top + plotH + 14}" text-anchor="middle" font-size="9.5" font-weight="bold"
                fill="#1e293b" font-family="monospace">
            +${this.formatThousands(deltaMileage)}km
          </text>
          <!-- 底部区间跨度 (X➔Ykm) -->
          <text x="${centerX}" y="${padding.top + plotH + 26}" text-anchor="middle" font-size="8"
                fill="#64748b" font-family="monospace">
            ${spanText}
          </text>
          <!-- 底部序号与日期 (#N · MM-DD) -->
          <text x="${centerX}" y="${padding.top + plotH + 37}" text-anchor="middle" font-size="7.5" fill="#94a3b8">
            #${p.order || (i + 1)}${p.record_date ? ' · ' + p.record_date.slice(5, 10) : ''}
          </text>
        </g>
      `;
    });

    return `
      <div class="p-4 rounded-xl border border-slate-200 bg-white hover:border-slate-300 transition shadow-sm">
        <div class="flex flex-wrap items-center justify-between gap-2 mb-3 pb-2 border-b border-slate-100">
          <div class="flex items-center space-x-2">
            <span class="text-sm font-bold text-slate-800">${title}</span>
            <span class="text-[11px] text-slate-400 font-mono">(${unit})</span>
            <span class="text-[10px] text-blue-700 bg-blue-50 px-2 py-0.5 rounded-full font-medium border border-blue-200/60">
              🚩 起点: ${startDesc || `提车初始 ${this.formatThousands(initialMileage)} km`}
            </span>
          </div>
          <div class="flex items-center space-x-2">
            ${avgVal > 0 ? `<span class="text-[11px] font-mono text-slate-500 bg-slate-50 px-2 py-0.5 rounded border border-slate-200">${avgLabel}: ${avgVal}</span>` : ''}
            <span class="text-[11px] font-mono font-bold px-2 py-0.5 rounded border" style="color: ${color}; background-color: ${color}10; border-color: ${color}30">
              最新: ${latestVal !== undefined ? latestVal : '--'}
            </span>
          </div>
        </div>
        <div class="w-full overflow-x-auto">
          <svg viewBox="0 0 ${width} ${height}" class="w-full h-auto min-w-[340px]" style="max-height: 230px;">
            <defs>
              <linearGradient id="${gradientId}" x1="0%" y1="0%" x2="0%" y2="100%">
                <stop offset="0%" stop-color="${gradientFrom}" stop-opacity="0.95"/>
                <stop offset="100%" stop-color="${gradientTo}" stop-opacity="0.75"/>
              </linearGradient>
            </defs>
            ${gridLines}
            ${initialMarker}
            ${avgLine}
            ${barsHtml}
          </svg>
        </div>
      </div>
    `;
  },

  // 兼容旧接口别名
  generateSvgCurve(args) {
    return this.generateSvgBarChart(args);
  },

  renderEnergyChart() {
    const container = document.getElementById('energy-chart-container');
    if (!container || !this.energyAnalysisData) return;

    const allCombined = [...(this.energyAnalysisData.recent_intervals || [])].sort((a, b) => (a.mileage - b.mileage) || (a.record_date > b.record_date ? 1 : -1));

    if (allCombined.length < 1) {
      container.innerHTML = `
        <div class="py-8 text-center text-slate-400 text-xs">
          <p class="font-medium text-slate-500 mb-1">当前暂无补能记录</p>
          <p>录入补能记录后，系统将自动以车辆初始化状态为起点，计算并呈现每次行程的百公里能耗柱状图</p>
        </div>
      `;
      return;
    }

    // 提取所有有记录的年份与当前年份，降序排列
    const currentYear = new Date().getFullYear().toString();
    const yearSet = new Set();
    allCombined.forEach(x => {
      if (x && x.record_date && x.record_date.length >= 4) {
        yearSet.add(x.record_date.slice(0, 4));
      }
    });
    yearSet.add(currentYear);
    const availableYears = Array.from(yearSet).sort((a, b) => b.localeCompare(a));

    if (!this.selectedEnergyYear || (this.selectedEnergyYear !== 'all' && !availableYears.includes(this.selectedEnergyYear))) {
      this.selectedEnergyYear = availableYears[0];
    }

    // 填充与同步年份下拉选项
    const yearSelect = document.getElementById('energy-year-select');
    if (yearSelect) {
      let selectHtml = availableYears.map(y => `<option value="${y}" ${y === this.selectedEnergyYear ? 'selected' : ''}>${y}年</option>`).join('');
      selectHtml += `<option value="all" ${this.selectedEnergyYear === 'all' ? 'selected' : ''}>全部年份</option>`;
      yearSelect.innerHTML = selectHtml;
    }

    const isYearFilter = this.selectedEnergyYear && this.selectedEnergyYear !== 'all';
    const filteredCombined = isYearFilter
      ? allCombined.filter(x => x.record_date && x.record_date.startsWith(this.selectedEnergyYear))
      : allCombined;

    if (filteredCombined.length === 0) {
      const yearLabel = this.selectedEnergyYear === 'all' ? '全部年份' : `${this.selectedEnergyYear}年`;
      container.innerHTML = `
        <div class="py-10 text-center text-slate-400 text-xs bg-slate-50/50 rounded-xl border border-dashed border-slate-200">
          <p class="font-medium text-slate-500 mb-1">【${yearLabel}】暂无补能记录</p>
          <p>录入该年份的补能数据后，系统将自动以该年度起点计算并呈现每次行程的百公里能耗柱状图</p>
        </div>
      `;
      return;
    }

    const summary = this.energyAnalysisData.summary || {};
    const vehicle = this.energyAnalysisData.vehicle || this.getActiveVehicle() || {};
    const initialMileage = parseFloat(vehicle.initial_mileage || 0);
    const isPureGas = vehicle.power_type === 'gas';
    const isPureEv = vehicle.power_type === 'ev';
    const isDual = !isPureGas && !isPureEv;

    // 起点里程与说明 (当年第一条记录的上一里程作为年度起点，若为全历史则为车辆提车初始里程)
    const firstInterval = filteredCombined[0];
    const chartStartMileage = (firstInterval && firstInterval.prev_mileage !== undefined) ? firstInterval.prev_mileage : initialMileage;
    const startDesc = (!isYearFilter || chartStartMileage === initialMileage) ? `提车初始 ${initialMileage} km` : `${this.selectedEnergyYear}年初起点 ${chartStartMileage} km`;
    const avgLabel = isYearFilter ? '年度均值' : '累计均值';

    // 控制顶部 tab 按钮显示状态
    const tabsContainer = document.getElementById('energy-chart-tabs');
    if (tabsContainer) {
      if (!isDual) {
        tabsContainer.classList.add('hidden');
      } else {
        tabsContainer.classList.remove('hidden');
      }
    }

    // 构造油耗与电耗及综合等效电耗行程柱状图数据集 (起点为车的初始化状态或年度起点)
    const fuelPoints = filteredCombined.map(x => ({
      order: x.order,
      type: x.type,
      mileage: x.mileage,
      prev_mileage: x.prev_mileage !== undefined ? x.prev_mileage : chartStartMileage,
      delta_mileage: x.delta_mileage,
      record_date: x.record_date,
      consumed_amount: x.interval_fuel_consumed !== undefined ? x.interval_fuel_consumed : (x.type === 'fuel' ? x.consumed_amount : 0),
      val: (x.interval_fuel_consumption !== undefined && x.interval_fuel_consumption !== null) ? x.interval_fuel_consumption : (x.type === 'fuel' ? x.interval_consumption : 0.0)
    }));

    const elecPoints = filteredCombined.map(x => ({
      order: x.order,
      type: x.type,
      mileage: x.mileage,
      prev_mileage: x.prev_mileage !== undefined ? x.prev_mileage : chartStartMileage,
      delta_mileage: x.delta_mileage,
      record_date: x.record_date,
      consumed_amount: x.interval_elec_consumed !== undefined ? x.interval_elec_consumed : (x.type !== 'fuel' ? x.consumed_amount : 0),
      val: (x.interval_elec_consumption !== undefined && x.interval_elec_consumption !== null) ? x.interval_elec_consumption : (x.type !== 'fuel' ? x.interval_consumption : 0.0)
    }));

    const equivPoints = filteredCombined.map(x => {
      const elecVal = (x.interval_elec_consumption !== undefined && x.interval_elec_consumption !== null) ? x.interval_elec_consumption : (x.type !== 'fuel' ? x.interval_consumption : 0.0);
      const fuelVal = (x.interval_fuel_consumption !== undefined && x.interval_fuel_consumption !== null) ? x.interval_fuel_consumption : (x.type === 'fuel' ? x.interval_consumption : 0.0);
      const equivVal = (x.interval_equivalent_elec_consumption !== undefined && x.interval_equivalent_elec_consumption !== null)
        ? x.interval_equivalent_elec_consumption
        : Number((elecVal + fuelVal * 3.3).toFixed(2));
      const equivConsumed = (x.interval_equivalent_elec_consumed !== undefined && x.interval_equivalent_elec_consumed !== null)
        ? x.interval_equivalent_elec_consumed
        : Number(((x.interval_elec_consumed || 0) + (x.interval_fuel_consumed || 0) * 3.3).toFixed(2));

      return {
        order: x.order,
        type: x.type,
        mileage: x.mileage,
        prev_mileage: x.prev_mileage !== undefined ? x.prev_mileage : chartStartMileage,
        delta_mileage: x.delta_mileage,
        record_date: x.record_date,
        consumed_amount: equivConsumed,
        val: equivVal
      };
    });

    const validFuelPoints = fuelPoints.filter(p => p.val > 0);
    const validElecPoints = elecPoints.filter(p => p.val > 0);
    const validEquivPoints = equivPoints.filter(p => p.val > 0);

    const latestFuel = validFuelPoints.length ? validFuelPoints[validFuelPoints.length - 1].val : (fuelPoints.length ? fuelPoints[fuelPoints.length - 1].val : 0.0);
    const latestElec = validElecPoints.length ? validElecPoints[validElecPoints.length - 1].val : (elecPoints.length ? elecPoints[elecPoints.length - 1].val : 0.0);
    const latestEquiv = validEquivPoints.length ? validEquivPoints[validEquivPoints.length - 1].val : (equivPoints.length ? equivPoints[equivPoints.length - 1].val : 0.0);

    let avgFuel = 0.0;
    let avgElec = 0.0;
    let avgEquiv = 0.0;

    if (isYearFilter) {
      const yearFuelConsumed = fuelPoints.reduce((s, p) => s + (p.consumed_amount || 0), 0);
      const yearFuelDrivenKm = fuelPoints.reduce((s, p) => s + (p.delta_mileage || 0), 0);
      avgFuel = (yearFuelDrivenKm > 0 && yearFuelConsumed > 0)
        ? Number(((yearFuelConsumed / yearFuelDrivenKm) * 100).toFixed(2))
        : (validFuelPoints.length ? Number((validFuelPoints.reduce((s, p) => s + p.val, 0) / validFuelPoints.length).toFixed(2)) : 0.0);

      const yearElecConsumed = elecPoints.reduce((s, p) => s + (p.consumed_amount || 0), 0);
      const yearElecDrivenKm = elecPoints.reduce((s, p) => s + (p.delta_mileage || 0), 0);
      avgElec = (yearElecDrivenKm > 0 && yearElecConsumed > 0)
        ? Number(((yearElecConsumed / yearElecDrivenKm) * 100).toFixed(2))
        : (validElecPoints.length ? Number((validElecPoints.reduce((s, p) => s + p.val, 0) / validElecPoints.length).toFixed(2)) : 0.0);

      const yearEquivConsumed = equivPoints.reduce((s, p) => s + (p.consumed_amount || 0), 0);
      const yearEquivDrivenKm = equivPoints.reduce((s, p) => s + (p.delta_mileage || 0), 0);
      avgEquiv = (yearEquivDrivenKm > 0 && yearEquivConsumed > 0)
        ? Number(((yearEquivConsumed / yearEquivDrivenKm) * 100).toFixed(2))
        : (validEquivPoints.length ? Number((validEquivPoints.reduce((s, p) => s + p.val, 0) / validEquivPoints.length).toFixed(2)) : 0.0);
    } else {
      avgFuel = summary.fuel?.overall_avg || 0.0;
      avgElec = summary.electricity?.overall_avg || 0.0;
      avgEquiv = summary.electricity?.equivalent_overall_avg ?? summary.equivalent_electricity?.overall_avg ?? 0.0;
    }

    const fuelChartHtml = this.generateSvgBarChart({
      points: fuelPoints,
      title: '⛽ 百公里油耗 (每次行程间能耗)',
      unit: 'L/100km',
      color: '#d97706',
      gradientFrom: '#f59e0b',
      gradientTo: '#d97706',
      gradientId: 'fuelBarGradient',
      latestVal: latestFuel,
      avgVal: avgFuel,
      avgLabel: avgLabel,
      initialMileage: chartStartMileage,
      startDesc: startDesc
    });

    const elecChartHtml = this.generateSvgBarChart({
      points: elecPoints,
      title: '⚡ 百公里电耗 (每次行程间能耗)',
      unit: 'kWh/100km',
      color: '#059669',
      gradientFrom: '#10b981',
      gradientTo: '#059669',
      gradientId: 'elecBarGradient',
      latestVal: latestElec,
      avgVal: avgElec,
      avgLabel: avgLabel,
      initialMileage: chartStartMileage,
      startDesc: startDesc
    });

    const equivChartHtml = this.generateSvgBarChart({
      points: equivPoints,
      title: '🔋 百公里等效电耗 (油耗按 1L=3.3kWh 折算)',
      unit: 'kWh/100km',
      color: '#2563eb',
      gradientFrom: '#3b82f6',
      gradientTo: '#1d4ed8',
      gradientId: 'equivBarGradient',
      latestVal: latestEquiv,
      avgVal: avgEquiv,
      avgLabel: avgLabel,
      initialMileage: chartStartMileage,
      startDesc: startDesc
    });

    const currentTab = this.energyChartTab || 'both';
    const tab = isPureGas ? 'fuel' : (isPureEv ? 'electricity' : currentTab);

    if (tab === 'fuel') {
      container.innerHTML = `<div class="w-full">${fuelChartHtml}</div>`;
    } else if (tab === 'electricity' || tab === 'elec') {
      container.innerHTML = `<div class="w-full">${elecChartHtml}</div>`;
    } else if (tab === 'equivalent' || tab === 'equiv') {
      container.innerHTML = `<div class="w-full">${equivChartHtml}</div>`;
    } else {
      // both: 显示全部（油耗 + 电耗并排，下方附带等效电耗走势）
      container.innerHTML = `
        <div class="grid grid-cols-1 lg:grid-cols-2 gap-4">
          ${fuelChartHtml}
          ${elecChartHtml}
        </div>
        <div class="mt-4">
          ${equivChartHtml}
        </div>
      `;
    }
  },

  renderEnergyIntervals() {
    const container = document.getElementById('energy-intervals-list');
    if (!container || !this.energyAnalysisData) return;

    let list = [];
    if (this.energyIntervalFilter === 'fuel') {
      list = [...(this.energyAnalysisData.fuel_intervals || [])].reverse();
    } else if (this.energyIntervalFilter === 'electricity') {
      list = [...(this.energyAnalysisData.elec_intervals || [])].reverse();
    } else {
      list = this.energyAnalysisData.recent_intervals || [];
    }

    if (list.length === 0) {
      container.innerHTML = `
        <div class="text-center py-10 text-slate-400 text-sm">
          暂无符合条件的补能区间记录，请在上方录入新的油耗或电耗记录。
        </div>
      `;
      return;
    }

    const summary = this.energyAnalysisData.summary || {};

    const activeCar = (this.energyAnalysisData && this.energyAnalysisData.vehicle) || this.getActiveVehicle();
    const isDualFuelElec = activeCar && (activeCar.power_type === 'erev' || activeCar.power_type === 'phev' || activeCar.power_type === 'hybrid');

    container.innerHTML = list.map(item => {
      const isOngoing = item.type === 'ongoing' || item.is_ongoing;
      const isFuel = item.type === 'fuel';
      const overallAvg = isFuel ? summary.fuel?.overall_avg : summary.electricity?.overall_avg;

      let perfBadge = '';
      if (isOngoing) {
        perfBadge = `<span class="px-2 py-0.5 rounded text-[11px] font-bold bg-blue-50 text-blue-700 border border-blue-200">🚗 在途行驶核算</span>`;
      } else if (overallAvg > 0 && item.interval_consumption > 0) {
        const diffPercent = ((item.interval_consumption - overallAvg) / overallAvg) * 100;
        if (diffPercent <= -5) {
          perfBadge = `<span class="px-2 py-0.5 rounded text-[11px] font-bold bg-emerald-50 text-emerald-700 border border-emerald-200">🟢 优于均值 ${Math.abs(diffPercent).toFixed(1)}%</span>`;
        } else if (diffPercent >= 5) {
          perfBadge = `<span class="px-2 py-0.5 rounded text-[11px] font-bold bg-amber-50 text-amber-700 border border-amber-200">🟡 高于均值 ${diffPercent.toFixed(1)}%</span>`;
        } else {
          perfBadge = `<span class="px-2 py-0.5 rounded text-[11px] font-medium bg-slate-100 text-slate-600">⚪ 与均值持平</span>`;
        }
      }

      const fuelCons = (item.interval_fuel_consumption !== undefined && item.interval_fuel_consumption !== null)
        ? item.interval_fuel_consumption
        : (isFuel ? item.interval_consumption : 0);
      const elecCons = (item.interval_elec_consumption !== undefined && item.interval_elec_consumption !== null)
        ? item.interval_elec_consumption
        : (!isFuel ? item.interval_consumption : 0);
      const equivCons = (item.interval_equivalent_elec_consumption !== undefined && item.interval_equivalent_elec_consumption !== null)
        ? item.interval_equivalent_elec_consumption
        : Number((elecCons + fuelCons * 3.3).toFixed(2));

      const fuelConsumed = (item.interval_fuel_consumed !== undefined && item.interval_fuel_consumed !== null)
        ? item.interval_fuel_consumed
        : (isFuel ? (item.consumed_amount ?? item.amount) : 0);
      const elecConsumed = (item.interval_elec_consumed !== undefined && item.interval_elec_consumed !== null)
        ? item.interval_elec_consumed
        : (!isFuel ? (item.consumed_amount ?? item.amount) : 0);
      const equivConsumed = (item.interval_equivalent_elec_consumed !== undefined && item.interval_equivalent_elec_consumed !== null)
        ? item.interval_equivalent_elec_consumed
        : Number(((elecConsumed || 0) + (fuelConsumed || 0) * 3.3).toFixed(2));

      const cumFuelCons = (item.cumulative_fuel_consumption !== undefined && item.cumulative_fuel_consumption !== null)
        ? item.cumulative_fuel_consumption
        : (isFuel ? item.cumulative_consumption : 0);
      const cumElecCons = (item.cumulative_elec_consumption !== undefined && item.cumulative_elec_consumption !== null)
        ? item.cumulative_elec_consumption
        : (!isFuel ? item.cumulative_consumption : 0);
      const cumEquivCons = (item.cumulative_equivalent_elec_consumption !== undefined && item.cumulative_equivalent_elec_consumption !== null)
        ? item.cumulative_equivalent_elec_consumption
        : Number((cumElecCons + cumFuelCons * 3.3).toFixed(2));

      const cumFuelAmt = (item.cumulative_fuel_consumed !== undefined && item.cumulative_fuel_consumed !== null)
        ? item.cumulative_fuel_consumed
        : (isFuel ? item.cumulative_consumed_amount : 0);
      const cumElecAmt = (item.cumulative_elec_consumed !== undefined && item.cumulative_elec_consumed !== null)
        ? item.cumulative_elec_consumed
        : (!isFuel ? item.cumulative_consumed_amount : 0);
      const cumEquivAmt = (item.cumulative_equivalent_elec_consumed !== undefined && item.cumulative_equivalent_elec_consumed !== null)
        ? item.cumulative_equivalent_elec_consumed
        : Number(((cumElecAmt || 0) + (cumFuelAmt || 0) * 3.3).toFixed(2));

      return `
        <div class="p-4 rounded-xl border border-slate-200 hover:border-slate-300 bg-white hover:shadow-sm transition flex flex-col md:flex-row md:items-center justify-between gap-4">
          <div class="flex items-start space-x-3.5">
            <div class="w-10 h-10 rounded-xl ${isOngoing ? 'bg-blue-100 text-blue-600' : (isFuel ? 'bg-amber-100 text-amber-600' : 'bg-emerald-100 text-emerald-600')} flex items-center justify-center font-bold text-lg flex-shrink-0 mt-0.5">
              ${isOngoing ? '🚗' : (isFuel ? '⛽' : '⚡')}
            </div>
            <div>
              <div class="flex items-center space-x-2 flex-wrap">
                <span class="font-bold text-sm text-slate-900">${isOngoing ? '当前行驶状态 (未补能)' : `第 ${item.order} 次${isFuel ? '加油' : '充电'}`}</span>
                <span class="text-xs font-mono text-slate-500">${item.record_date}</span>
                ${!isOngoing ? (item.days_since_prev !== null ? `<span class="text-[11px] px-1.5 py-0.2 rounded bg-slate-100 text-slate-500">距上次 ${item.days_since_prev} 天</span>` : '<span class="text-[11px] px-1.5 py-0.2 rounded bg-blue-50 text-blue-600">首笔记录</span>') : ''}
                ${item.is_calibrated ? `<span class="px-2 py-0.5 rounded text-[11px] font-bold bg-teal-50 text-teal-700 border border-teal-200">🔋 电池校准: ${item.calibrated_capacity || item.active_battery_capacity} kWh</span>` : ''}
                ${perfBadge}
              </div>

              <!-- 里程跨度 -->
              <div class="mt-1.5 flex items-center space-x-2 text-xs text-slate-600 font-mono">
                <span>表显 ${this.formatThousands(item.prev_mileage)} km ➔ ${this.formatThousands(item.mileage)} km</span>
                <span class="font-bold text-slate-900 bg-slate-100 px-2 py-0.5 rounded">区间行驶 ${this.formatThousands(item.delta_mileage)} km</span>
              </div>

              <!-- 油电百分比区间变化与实充/实加 -->
              <div class="mt-1.5 text-xs text-slate-600 flex flex-wrap gap-x-3 gap-y-1">
                ${(isDualFuelElec || isFuel) && item.prev_fuel_percent !== undefined ? `
                  <div class="flex items-center space-x-1 font-mono">
                    <span class="text-amber-800 font-medium">⛽油量:</span>
                    <span>${item.prev_fuel_percent}% ➔ ${item.curr_start_fuel_percent}%</span>
                    <span class="text-amber-700 font-semibold">(耗${item.delta_fuel_percent}%)</span>
                    ${isFuel ? `<span class="text-slate-400">➔ 加后${item.curr_end_fuel_percent}%</span>` : ''}
                  </div>
                ` : ''}
                ${(isDualFuelElec || !isFuel) && item.prev_battery_percent !== undefined ? `
                  <div class="flex items-center space-x-1 font-mono">
                    <span class="text-emerald-800 font-medium">⚡电量:</span>
                    <span>${item.prev_battery_percent}% ➔ ${item.curr_start_battery_percent}%</span>
                    <span class="text-emerald-700 font-semibold">(耗${item.delta_battery_percent}%)</span>
                    ${!isFuel ? `<span class="text-slate-400">➔ 充后${item.curr_end_battery_percent}%</span>` : ''}
                  </div>
                ` : ''}
              </div>

              <!-- 补能规格与地点 -->
              <div class="mt-1 text-xs text-slate-400">
                <span>${item.fuel_type || item.charging_type || ''}</span>
                ${item.station ? `· <span>${item.station}</span>` : ''}
                ${item.notes ? `· <span class="italic text-slate-400">${item.notes}</span>` : ''}
              </div>
            </div>
          </div>

          <!-- 右侧关键数值 -->
          <div class="flex items-center justify-between md:justify-end md:space-x-6 border-t md:border-t-0 pt-3 md:pt-0 border-slate-100">
            ${isDualFuelElec ? `
              <div class="text-left md:text-right">
                <span class="text-xs text-slate-400 block mb-1">区间分段能耗 (双能同步核算)</span>
                <div class="flex items-baseline md:justify-end gap-2.5 flex-wrap">
                  <div class="flex items-baseline gap-1">
                    <span class="text-xs font-bold text-amber-700">⛽ 油耗</span>
                    <span class="text-lg font-extrabold text-amber-600 font-mono">${fuelCons > 0 ? this.formatThousands(fuelCons) : '0.00'}</span>
                    <span class="text-[11px] text-slate-400 font-normal">L/100km</span>
                  </div>
                  <div class="flex items-baseline gap-1">
                    <span class="text-xs font-bold text-emerald-700">⚡ 电耗</span>
                    <span class="text-lg font-extrabold text-emerald-600 font-mono">${elecCons > 0 ? this.formatThousands(elecCons) : '0.00'}</span>
                    <span class="text-[11px] text-slate-400 font-normal">kWh/100km</span>
                  </div>
                  <div class="flex items-baseline gap-1 bg-blue-50 px-1.5 py-0.5 rounded border border-blue-200/80">
                    <span class="text-xs font-bold text-blue-800">🔋 等效</span>
                    <span class="text-lg font-extrabold text-blue-700 font-mono">${equivCons > 0 ? this.formatThousands(equivCons) : '0.00'}</span>
                    <span class="text-[10px] text-blue-500 font-normal">kWh/100km</span>
                  </div>
                </div>
                <div class="text-[11px] text-slate-500 font-mono mt-0.5">
                  <span>耗油 ${this.formatThousands(fuelConsumed)} L · 耗电 ${this.formatThousands(elecConsumed)} kWh · 等效电量 ${this.formatThousands(equivConsumed)} kWh</span>
                </div>
                ${(cumFuelCons > 0 || cumElecCons > 0) ? `
                  <div class="mt-1.5 pt-1.5 border-t border-slate-100 text-[11px] text-slate-600">
                    <div class="flex items-center justify-start md:justify-end gap-2 flex-wrap">
                      <span class="text-slate-400">累计能耗:</span>
                      <span class="font-mono text-amber-700 font-bold">⛽ ${this.formatThousands(cumFuelCons)} L/100km</span>
                      <span class="font-mono text-emerald-700 font-bold">⚡ ${this.formatThousands(cumElecCons)} kWh/100km</span>
                      <span class="font-mono text-blue-700 font-bold bg-blue-50 px-1.5 py-0.2 rounded border border-blue-200/60">🔋 等效 ${this.formatThousands(cumEquivCons)} kWh/100km</span>
                    </div>
                    <div class="text-[10px] text-slate-400 font-mono md:text-right mt-0.5">
                      (累计 ${this.formatThousands(item.cumulative_mileage)} km · 耗油 ${this.formatThousands(cumFuelAmt)} L + 耗电 ${this.formatThousands(cumElecAmt)} kWh · 折合总电量 ${this.formatThousands(cumEquivAmt)} kWh)
                    </div>
                  </div>
                ` : ''}
              </div>
            ` : `
              <div class="text-left md:text-right">
                <span class="text-xs text-slate-400 block">区间百公里能耗</span>
                <span class="text-xl font-extrabold ${isFuel ? 'text-amber-600' : 'text-emerald-600'}">
                  ${item.interval_consumption > 0 ? this.formatThousands(item.interval_consumption) : '--'}
                  <span class="text-xs font-normal text-slate-400">${item.unit}/100km</span>
                </span>
                <span class="block text-[11px] text-slate-500 font-mono">
                  ${item.prev_percent !== undefined && item.prev_percent !== null && item.curr_start_percent !== undefined && item.curr_start_percent !== null
                    ? `${item.prev_percent}% ➔ ${item.curr_start_percent}% (耗${this.formatThousands(item.consumed_amount ?? item.amount)}${item.unit})`
                    : `实际耗能 ${this.formatThousands(item.consumed_amount ?? item.amount)}${item.unit}`}
                </span>
                ${item.cumulative_consumption !== undefined && item.cumulative_consumption > 0 ? `
                  <div class="mt-1.5 pt-1.5 border-t border-slate-100 flex items-center justify-start md:justify-end gap-1.5 text-[11px]">
                    <span class="text-slate-400">累计能耗:</span>
                    <span class="font-bold font-mono ${isFuel ? 'text-amber-700' : 'text-emerald-700'}">${this.formatThousands(item.cumulative_consumption)} ${item.unit}/100km</span>
                    <span class="text-slate-400 text-[10px] font-mono">(累计${this.formatThousands(item.cumulative_mileage)}km·耗${this.formatThousands(item.cumulative_consumed_amount)}${item.unit})</span>
                  </div>
                ` : ''}
              </div>
            `}

            <div class="text-right">
              <span class="text-xs text-slate-400 block">本次补能 · 实付</span>
              <span class="text-sm font-bold text-slate-900">+${this.formatThousands(item.amount)} ${item.unit} · ¥${this.formatThousands(item.total_price, 2)}</span>
              <span class="block text-[11px] text-slate-500 font-mono">¥${this.formatThousands(item.cost_per_km, 2)}/km</span>
            </div>

            ${item.id ? `
              <div class="flex items-center pl-2">
                <button onclick="App.openRecordEditModal('${item.type}', ${item.id})" class="p-1.5 text-slate-400 hover:text-blue-600 hover:bg-blue-50 rounded-lg transition" title="编辑本笔记录">
                  <svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M11 5H6a2 2 0 00-2 2v11a2 2 0 002 2h11a2 2 0 002-2v-5m-1.414-9.414a2 2 0 112.828 2.828L11.828 15H9v-2.828l8.586-8.586z"></path></svg>
                </button>
              </div>
            ` : ''}
          </div>
        </div>
      `;
    }).join('');
  },

  filterEnergyIntervals(type) {
    this.energyIntervalFilter = type;
    ['all', 'fuel', 'elec'].forEach(t => {
      const btn = document.getElementById(`ei-filter-${t}`);
      if (btn) {
        if ((type === 'all' && t === 'all') || (type === 'fuel' && t === 'fuel') || (type === 'electricity' && t === 'elec')) {
          btn.classList.add('active');
        } else {
          btn.classList.remove('active');
        }
      }
    });
    this.renderEnergyIntervals();
  },

  // 维保支出类别高对比度稳定调色体系
  getCategoryColor(catName) {
    const defaultPalette = {
      '维修保养': '#6366f1', // 靛青蓝 (indigo-500)
      '保养': '#6366f1',
      '维修': '#8b5cf6', // 紫罗兰 (purple-500)
      '停车': '#0ea5e9', // 天空蓝 (sky-500)
      '停车费': '#0ea5e9',
      '过桥过路费': '#14b8a6', // 蓝绿湖青 (teal-500)
      '高速费': '#14b8a6',
      'ETC': '#14b8a6',
      '保险': '#a855f7', // 亮紫 (purple-600)
      '车险': '#a855f7',
      '税': '#ec4899', // 玫粉 (pink-500)
      '车船税': '#ec4899',
      '购置税': '#ec4899',
      '违章': '#f43f5e', // 玫瑰红 (rose-500)
      '罚款': '#f43f5e',
      '充电优惠券': '#eab308', // 鹅黄 (yellow-500)
      '洗车': '#06b6d4', // 湖水青 (cyan-500)
      '美容': '#d946ef', // 浅洋红 (fuchsia-500)
      '改装': '#f97316', // 鲜橙 (orange-500)
      '年检': '#84cc16'  // 青柠绿 (lime-500)
    };
    if (defaultPalette[catName]) return defaultPalette[catName];

    // 基于名称哈希从 12 种调色池中稳定选取颜色
    const colorPool = [
      '#6366f1', '#0ea5e9', '#14b8a6', '#8b5cf6',
      '#ec4899', '#f43f5e', '#eab308', '#06b6d4',
      '#d946ef', '#f97316', '#84cc16', '#64748b'
    ];
    let hash = 0;
    const str = String(catName || '其他');
    for (let i = 0; i < str.length; i++) {
      hash = (hash << 5) - hash + str.charCodeAt(i);
      hash |= 0;
    }
    const idx = Math.abs(hash) % colorPool.length;
    return colorPool[idx];
  },

  onMonthlyYearChange(year) {
    this.selectedMonthlyYear = year;
    this.renderMonthlyCostChart(this.allMonthlyExpenses);
  },

  renderMonthlyCostChart(monthlyExpenses = []) {
    const container = document.getElementById('monthly-cost-chart-container');
    if (!container) return;

    if (monthlyExpenses && monthlyExpenses.length > 0) {
      this.allMonthlyExpenses = monthlyExpenses;
    } else if (!this.allMonthlyExpenses) {
      this.allMonthlyExpenses = [];
    }
    const allRecords = this.allMonthlyExpenses;

    // 提取所有有记录的年份与当前年份，降序排列
    const currentYear = new Date().getFullYear().toString();
    const yearSet = new Set();
    allRecords.forEach(m => {
      if (m && m.month && m.month.length >= 4) {
        yearSet.add(m.month.slice(0, 4));
      }
    });
    yearSet.add(currentYear);
    const availableYears = Array.from(yearSet).sort((a, b) => b.localeCompare(a));

    if (!this.selectedMonthlyYear || !availableYears.includes(this.selectedMonthlyYear)) {
      this.selectedMonthlyYear = availableYears[0];
    }

    // 填充或同步年份选择器下拉选项
    const yearSelect = document.getElementById('monthly-year-select');
    if (yearSelect) {
      yearSelect.innerHTML = availableYears.map(y => `<option value="${y}" ${y === this.selectedMonthlyYear ? 'selected' : ''}>${y}年</option>`).join('');
    }

    if (!allRecords || allRecords.length === 0) {
      container.innerHTML = `
        <div class="py-10 text-center text-slate-400 text-xs bg-slate-50/50 rounded-xl border border-dashed border-slate-200">
          <p class="font-medium text-slate-500 mb-1">暂无按月费用记录</p>
          <p>录入加油、充电或维保支出后，系统将自动按月聚合生成费用堆叠柱状图</p>
        </div>
      `;
      return;
    }

    const targetYear = this.selectedMonthlyYear;
    const expenseMap = new Map();
    allRecords.forEach(m => {
      if (m && m.month) {
        expenseMap.set(m.month, m);
      }
    });

    // 构造所选年份 1月 至 12月 的完整月份数据结构
    const yearMonthsData = [];
    for (let m = 1; m <= 12; m++) {
      const mStr = m < 10 ? `0${m}` : `${m}`;
      const key = `${targetYear}-${mStr}`;
      const rec = expenseMap.get(key);
      if (rec) {
        const fuelCost = parseFloat(rec.fuel_cost || 0);
        const elecCost = parseFloat(rec.elec_cost || 0);
        const otherCost = parseFloat(rec.other_cost || 0);
        const totalCost = parseFloat((rec.total_cost !== undefined ? rec.total_cost : (fuelCost + elecCost + otherCost)).toFixed(2));
        const breakdown = rec.other_breakdown || {};
        yearMonthsData.push({
          month: key,
          monthLabel: `${m}月`,
          fullMonth: `${targetYear}年${mStr}月`,
          fuel_cost: fuelCost,
          elec_cost: elecCost,
          other_cost: otherCost,
          total_cost: totalCost,
          other_breakdown: breakdown
        });
      } else {
        yearMonthsData.push({
          month: key,
          monthLabel: `${m}月`,
          fullMonth: `${targetYear}年${mStr}月`,
          fuel_cost: 0,
          elec_cost: 0,
          other_cost: 0,
          total_cost: 0,
          other_breakdown: {}
        });
      }
    }

    // 收集所选年份中实际出现过的其他费用类别并更新动态图例（支持正负支出类别）
    const activeCategories = new Set();
    yearMonthsData.forEach(m => {
      if (m.other_breakdown) {
        Object.entries(m.other_breakdown).forEach(([cat, val]) => {
          if (Math.abs(parseFloat(val)) > 0.001) activeCategories.add(cat);
        });
      }
    });

    const legendContainer = document.getElementById('monthly-chart-legends');
    if (legendContainer) {
      let legendsHtml = `
        <span class="inline-flex items-center gap-1.5 text-slate-600 font-medium">
          <span class="w-2.5 h-2.5 rounded-sm bg-amber-500"></span>燃油
        </span>
        <span class="inline-flex items-center gap-1.5 text-slate-600 font-medium">
          <span class="w-2.5 h-2.5 rounded-sm bg-emerald-500"></span>电力
        </span>
      `;
      if (activeCategories.size > 0) {
        activeCategories.forEach(cat => {
          const color = this.getCategoryColor(cat);
          legendsHtml += `
            <span class="inline-flex items-center gap-1.5 text-slate-600 font-medium">
              <span class="w-2.5 h-2.5 rounded-sm" style="background-color: ${color};"></span>${cat}
            </span>
          `;
        });
      } else {
        legendsHtml += `
          <span class="inline-flex items-center gap-1.5 text-slate-600 font-medium">
            <span class="w-2.5 h-2.5 rounded-sm bg-indigo-500"></span>其他
          </span>
        `;
      }
      legendContainer.innerHTML = legendsHtml;
    }

    // 细分每个月份的正向消费切片 (坐标轴上方) 与负向冲减切片 (坐标轴下方)
    yearMonthsData.forEach(m => {
      const fuelCost = m.fuel_cost;
      const eleCost = m.elec_cost;
      const otherCost = m.other_cost;
      const breakdown = m.other_breakdown || {};

      const posSlices = [];
      const negSlices = [];

      // 1. 燃油费用
      if (fuelCost > 0) {
        posSlices.push({ name: '燃油费用', cost: fuelCost, color: '#f59e0b' });
      } else if (fuelCost < 0) {
        negSlices.push({ name: '燃油退款', cost: fuelCost, color: '#f59e0b' });
      }

      // 2. 电力费用
      if (eleCost > 0) {
        posSlices.push({ name: '电力费用', cost: eleCost, color: '#10b981' });
      } else if (eleCost < 0) {
        negSlices.push({ name: '充电退款', cost: eleCost, color: '#10b981' });
      }

      // 3. 各分类其他支出
      let bdPosSum = 0;
      let bdNegSum = 0;
      Object.entries(breakdown).forEach(([cat, val]) => {
        const c = parseFloat(val) || 0;
        if (c > 0) {
          bdPosSum += c;
          posSlices.push({ name: cat, cost: c, color: this.getCategoryColor(cat) });
        } else if (c < 0) {
          bdNegSum += c;
          negSlices.push({ name: cat, cost: c, color: this.getCategoryColor(cat) });
        }
      });

      // 补充差额到通用“其他”
      const remainPos = parseFloat((Math.max(0, otherCost) - bdPosSum).toFixed(2));
      if (remainPos > 0.01) {
        posSlices.push({ name: '其他费用', cost: remainPos, color: '#6366f1' });
      }
      const remainNeg = parseFloat((Math.min(0, otherCost) - bdNegSum).toFixed(2));
      if (remainNeg < -0.01) {
        negSlices.push({ name: '其他冲减', cost: remainNeg, color: '#6366f1' });
      }

      m.posSlices = posSlices;
      m.posSum = posSlices.reduce((acc, s) => acc + s.cost, 0);
      m.negSlices = negSlices;
      m.negSum = negSlices.reduce((acc, s) => acc + Math.abs(s.cost), 0);
    });

    const maxPositive = Math.max(...yearMonthsData.map(m => m.posSum), 50);
    const maxNegative = Math.max(...yearMonthsData.map(m => m.negSum), 0);
    const hasNegative = maxNegative > 0.01;

    let maxPosVal = Math.ceil(maxPositive * 1.25);
    if (maxPosVal <= 0) maxPosVal = 100;
    if (maxPosVal > 500) {
      maxPosVal = Math.ceil(maxPosVal / 100) * 100;
    } else if (maxPosVal > 100) {
      maxPosVal = Math.ceil(maxPosVal / 50) * 50;
    }

    let maxNegVal = Math.ceil(maxNegative * 1.25);
    if (maxNegVal <= 0) maxNegVal = 50;
    if (maxNegVal > 500) {
      maxNegVal = Math.ceil(maxNegVal / 100) * 100;
    } else if (maxNegVal > 100) {
      maxNegVal = Math.ceil(maxNegVal / 50) * 50;
    }

    const n = 12;
    const width = 720;
    const height = hasNegative ? 285 : 240;
    const padding = hasNegative
      ? { top: 32, right: 28, bottom: 42, left: 60 }
      : { top: 36, right: 28, bottom: 44, left: 60 };

    const plotW = width - padding.left - padding.right;
    const totalPlotH = height - padding.top - padding.bottom;

    const negPlotH = hasNegative ? Math.max(55, Math.min(80, totalPlotH * 0.35)) : 0;
    const posPlotH = totalPlotH - negPlotH;
    const zeroY = padding.top + posPlotH;

    const slotW = plotW / n;
    const barWidth = Math.min(32, Math.max(18, slotW * 0.55));

    const formatTick = (val) => val >= 1000 ? (val / 1000).toFixed(1) + 'k' : val.toFixed(0);

    // Y 轴刻度与横向辅助线
    let gridLines = '';
    const posSteps = 3;
    for (let step = 1; step <= posSteps; step++) {
      const val = (maxPosVal / posSteps) * step;
      const y = zeroY - (step / posSteps) * posPlotH;
      gridLines += `
        <line x1="${padding.left}" y1="${y}" x2="${width - padding.right}" y2="${y}" stroke="#f1f5f9" stroke-width="1" stroke-dasharray="3 3"/>
        <text x="${padding.left - 8}" y="${y + 3.5}" text-anchor="end" font-size="9" fill="#94a3b8" font-family="monospace">¥${formatTick(val)}</text>
      `;
    }

    // 零刻度基准线 (X 轴)
    gridLines += `
      <line x1="${padding.left}" y1="${zeroY}" x2="${width - padding.right}" y2="${zeroY}" stroke="${hasNegative ? '#94a3b8' : '#cbd5e1'}" stroke-width="1.5"/>
      <text x="${padding.left - 8}" y="${zeroY + 3.5}" text-anchor="end" font-size="9" font-weight="${hasNegative ? 'bold' : 'normal'}" fill="${hasNegative ? '#64748b' : '#94a3b8'}" font-family="monospace">¥0</text>
    `;

    // 负消费 Y 轴辅助线 (零轴下方)
    if (hasNegative) {
      const yNeg = zeroY + negPlotH;
      gridLines += `
        <line x1="${padding.left}" y1="${yNeg}" x2="${width - padding.right}" y2="${yNeg}" stroke="#ecfdf5" stroke-width="1" stroke-dasharray="3 3"/>
        <text x="${padding.left - 8}" y="${yNeg + 3.5}" text-anchor="end" font-size="9" font-weight="semibold" fill="#059669" font-family="monospace">-¥${formatTick(maxNegVal)}</text>
      `;
    }

    // 柱体生成
    let barsHtml = '';
    const minSegH = 4;

    yearMonthsData.forEach((m, i) => {
      const centerX = padding.left + (i + 0.5) * slotW;
      const barX = centerX - barWidth / 2;

      const posSum = m.posSum;
      const negSum = m.negSum;
      const totalCost = m.total_cost;
      const breakdown = m.other_breakdown || {};

      let posSlicesSvg = '';
      let totalPosH = 0;
      const clipPosId = `monthBarClipPos_${i}`;

      if (posSum > 0) {
        const rawPosH = Math.max(6, (posSum / maxPosVal) * posPlotH);
        m.posSlices.forEach(s => {
          s.h = Math.max(minSegH, (s.cost / posSum) * rawPosH);
        });
        totalPosH = m.posSlices.reduce((acc, s) => acc + s.h, 0);

        let currY = zeroY;
        m.posSlices.forEach(s => {
          currY -= s.h;
          posSlicesSvg += `
            <rect x="${barX}" y="${currY}" width="${barWidth}" height="${s.h}" fill="${s.color}"/>
            <line x1="${barX}" y1="${currY}" x2="${barX + barWidth}" y2="${currY}" stroke="rgba(255,255,255,0.7)" stroke-width="1"/>
          `;
        });
      }

      let negSlicesSvg = '';
      let totalNegH = 0;
      const clipNegId = `monthBarClipNeg_${i}`;

      if (negSum > 0) {
        const rawNegH = Math.max(6, (negSum / maxNegVal) * negPlotH);
        m.negSlices.forEach(s => {
          s.h = Math.max(minSegH, (Math.abs(s.cost) / negSum) * rawNegH);
        });
        totalNegH = m.negSlices.reduce((acc, s) => acc + s.h, 0);

        let currY = zeroY;
        m.negSlices.forEach(s => {
          negSlicesSvg += `
            <rect x="${barX}" y="${currY}" width="${barWidth}" height="${s.h}" fill="${s.color}"/>
            <line x1="${barX}" y1="${currY + s.h}" x2="${barX + barWidth}" y2="${currY + s.h}" stroke="rgba(255,255,255,0.7)" stroke-width="1"/>
          `;
          currY += s.h;
        });
      }

      let zeroStubSvg = '';
      if (posSum === 0 && negSum === 0) {
        zeroStubSvg = `<rect x="${barX}" y="${zeroY - 1.5}" width="${barWidth}" height="3" fill="#cbd5e1" rx="1.5" ry="1.5"/>`;
      }

      // Tooltip 纯文本备用标题
      let tooltipTitle = `${m.fullMonth} 费用明细\n`;
      if (m.fuel_cost > 0) tooltipTitle += `⛽ 燃油费用: ¥${this.formatThousands(m.fuel_cost, 2)}\n`;
      else if (m.fuel_cost < 0) tooltipTitle += `⛽ 燃油退款: -¥${this.formatThousands(Math.abs(m.fuel_cost), 2)}\n`;

      if (m.elec_cost > 0) tooltipTitle += `⚡ 电力费用: ¥${this.formatThousands(m.elec_cost, 2)}\n`;
      else if (m.elec_cost < 0) tooltipTitle += `⚡ 充电退款: -¥${this.formatThousands(Math.abs(m.elec_cost), 2)}\n`;

      Object.entries(breakdown).forEach(([cat, val]) => {
        const v = parseFloat(val) || 0;
        if (Math.abs(v) > 0.001) {
          const vStr = v < 0 ? `-¥${this.formatThousands(Math.abs(v), 2)}` : `¥${this.formatThousands(v, 2)}`;
          tooltipTitle += `🏷️ ${cat}: ${vStr}\n`;
        }
      });
      tooltipTitle += `📊 当月净计: ${totalCost < 0 ? '-¥' + this.formatThousands(Math.abs(totalCost), 2) : '¥' + this.formatThousands(totalCost, 2)}`;

      const rawDataJson = encodeURIComponent(JSON.stringify(m));
      const hasActivity = posSum > 0 || negSum > 0;
      const monthLabelY = hasNegative ? height - 12 : zeroY + 16;

      barsHtml += `
        <g class="monthly-bar-group cursor-pointer"
           data-month-index="${i}"
           data-month-info="${rawDataJson}"
           onmouseenter="App.handleMonthlyBarHover(event, this)"
           onmousemove="App.moveMonthlyTooltip(event)"
           onmouseleave="App.hideMonthlyTooltip()"
           onclick="App.handleMonthlyBarHover(event, this)">
          <title>${tooltipTitle}</title>

          <!-- 鼠标感应热区 (覆盖整列全高度) -->
          <rect x="${centerX - slotW / 2}" y="${padding.top}" width="${slotW}" height="${height - padding.top - 16}" fill="transparent" class="hover:fill-slate-50/50 transition-colors"/>

          <!-- 0元基线指示块 -->
          ${zeroStubSvg}

          <!-- 正向费用柱体 (坐标轴上方，向上堆叠) -->
          ${totalPosH > 0 ? `
            <clipPath id="${clipPosId}">
              <rect x="${barX}" y="${zeroY - totalPosH}" width="${barWidth}" height="${totalPosH}" rx="4" ry="4"/>
            </clipPath>
            <g clip-path="url(#${clipPosId})">
              ${posSlicesSvg}
            </g>
          ` : ''}

          <!-- 负向冲减柱体 (坐标轴下方，向下堆叠) -->
          ${totalNegH > 0 ? `
            <clipPath id="${clipNegId}">
              <rect x="${barX}" y="${zeroY}" width="${barWidth}" height="${totalNegH}" rx="4" ry="4"/>
            </clipPath>
            <g clip-path="url(#${clipNegId})">
              ${negSlicesSvg}
            </g>
          ` : ''}

          <!-- 柱顶正金额数字 (当月总额 > 0 时标注在正柱顶部) -->
          ${totalCost > 0 ? `
            <text x="${centerX}" y="${zeroY - totalPosH - 4}" text-anchor="middle" font-size="8.5" font-weight="bold"
                  fill="#1e293b" font-family="monospace">
              ¥${this.formatThousands(totalCost, 2)}
            </text>
          ` : ''}

          <!-- 柱底负金额数字 (当月总额 < 0 时标注在负柱底部) -->
          ${totalCost < 0 ? `
            <text x="${centerX}" y="${zeroY + totalNegH + 11}" text-anchor="middle" font-size="8.5" font-weight="bold"
                  fill="#059669" font-family="monospace">
              -¥${this.formatThousands(Math.abs(totalCost), 2)}
            </text>
          ` : ''}

          <!-- X 轴月份标注 (如: 1月, 2月... 12月) -->
          <text x="${centerX}" y="${monthLabelY}" text-anchor="middle" font-size="9.5" font-weight="${hasActivity ? '600' : '500'}"
                fill="${hasActivity ? '#334155' : '#94a3b8'}" font-family="system-ui, sans-serif">
            ${m.monthLabel}
          </text>
        </g>
      `;
    });

    container.innerHTML = `
      <svg viewBox="0 0 ${width} ${height}" class="w-full h-auto min-w-[500px]" style="max-height: ${hasNegative ? '290px' : '250px'};">
        <!-- 网格参考线与零刻度基准线 -->
        ${gridLines}
        <!-- 堆叠柱 -->
        ${barsHtml}
      </svg>
    `;
  },

  handleMonthlyBarHover(evt, elem) {
    if (!elem) return;
    try {
      const raw = elem.getAttribute('data-month-info');
      if (!raw) return;
      const data = JSON.parse(decodeURIComponent(raw));
      this.showMonthlyTooltip(evt, data);
    } catch (e) {
      console.warn('解析月份数据浮窗异常:', e);
    }
  },

  showMonthlyTooltip(evt, monthData) {
    const tooltip = document.getElementById('monthly-chart-tooltip');
    if (!tooltip || !monthData) return;

    const fullMonth = monthData.fullMonth || monthData.month;
    const totalCost = parseFloat(monthData.total_cost || 0);
    const fuelCost = parseFloat(monthData.fuel_cost || 0);
    const elecCost = parseFloat(monthData.elec_cost || 0);
    const breakdown = monthData.other_breakdown || {};

    let itemsHtml = '';
    // 1. 燃油费用
    if (fuelCost > 0) {
      itemsHtml += `
        <div class="flex items-center justify-between gap-4 text-slate-300">
          <span class="flex items-center gap-1.5"><span class="w-2 h-2 rounded-full bg-amber-500"></span>燃油费用:</span>
          <span class="font-mono text-white font-semibold">¥${this.formatThousands(fuelCost, 2)}</span>
        </div>
      `;
    } else if (fuelCost < 0) {
      itemsHtml += `
        <div class="flex items-center justify-between gap-4 text-slate-300">
          <span class="flex items-center gap-1.5"><span class="w-2 h-2 rounded-full bg-amber-500"></span>燃油退款:</span>
          <span class="font-mono text-emerald-400 font-bold">-¥${this.formatThousands(Math.abs(fuelCost), 2)}</span>
        </div>
      `;
    }

    // 2. 电力费用
    if (elecCost > 0) {
      itemsHtml += `
        <div class="flex items-center justify-between gap-4 text-slate-300">
          <span class="flex items-center gap-1.5"><span class="w-2 h-2 rounded-full bg-emerald-500"></span>电力费用:</span>
          <span class="font-mono text-white font-semibold">¥${this.formatThousands(elecCost, 2)}</span>
        </div>
      `;
    } else if (elecCost < 0) {
      itemsHtml += `
        <div class="flex items-center justify-between gap-4 text-slate-300">
          <span class="flex items-center gap-1.5"><span class="w-2 h-2 rounded-full bg-emerald-500"></span>充电退款:</span>
          <span class="font-mono text-emerald-400 font-bold">-¥${this.formatThousands(Math.abs(elecCost), 2)}</span>
        </div>
      `;
    }

    // 3. 各分类其他支出 (全面支持正数与负数冲减)
    const bdEntries = Object.entries(breakdown).filter(([_, val]) => Math.abs(parseFloat(val)) > 0.001);
    let bdSum = 0;
    if (bdEntries.length > 0) {
      bdEntries.forEach(([cat, val]) => {
        const c = parseFloat(val) || 0;
        bdSum += c;
        const color = this.getCategoryColor(cat);
        const isNeg = c < 0;
        const valStr = isNeg ? `-¥${this.formatThousands(Math.abs(c), 2)}` : `¥${this.formatThousands(c, 2)}`;
        itemsHtml += `
          <div class="flex items-center justify-between gap-4 text-slate-300">
            <span class="flex items-center gap-1.5">
              <span class="w-2 h-2 rounded-full" style="background-color: ${color};"></span>
              ${cat}${isNeg ? ' (冲减/退款)' : ''}:
            </span>
            <span class="font-mono ${isNeg ? 'text-emerald-400 font-bold' : 'text-white font-semibold'}">${valStr}</span>
          </div>
        `;
      });
    }

    const otherCost = parseFloat(monthData.other_cost || 0);
    const remainOther = parseFloat((otherCost - bdSum).toFixed(2));
    if (Math.abs(remainOther) > 0.01) {
      const isNeg = remainOther < 0;
      const valStr = isNeg ? `-¥${this.formatThousands(Math.abs(remainOther), 2)}` : `¥${this.formatThousands(remainOther, 2)}`;
      itemsHtml += `
        <div class="flex items-center justify-between gap-4 text-slate-300">
          <span class="flex items-center gap-1.5"><span class="w-2 h-2 rounded-full bg-indigo-500"></span>其他费用${isNeg ? ' (冲减)' : ''}:</span>
          <span class="font-mono ${isNeg ? 'text-emerald-400 font-bold' : 'text-white font-semibold'}">${valStr}</span>
        </div>
      `;
    }

    if (!itemsHtml) {
      itemsHtml = `
        <div class="text-slate-400 py-0.5 text-center">当月无费用发生</div>
      `;
    }

    const totalStr = totalCost < 0 ? `-¥${this.formatThousands(Math.abs(totalCost), 2)}` : `¥${this.formatThousands(totalCost, 2)}`;
    const totalColorClass = totalCost < 0 ? 'text-emerald-400' : 'text-amber-400';

    tooltip.innerHTML = `
      <div class="font-bold text-slate-100 border-b border-slate-700/80 pb-1 mb-1.5 flex items-center justify-between gap-4">
        <span>📅 ${fullMonth}</span>
        <span class="${totalColorClass} font-mono font-extrabold text-sm">${totalStr}</span>
      </div>
      <div class="space-y-1 text-[11px]">
        ${itemsHtml}
      </div>
    `;
    tooltip.classList.remove('hidden');
    this.moveMonthlyTooltip(evt);
  },

  moveMonthlyTooltip(evt) {
    const tooltip = document.getElementById('monthly-chart-tooltip');
    const container = document.getElementById('monthly-cost-chart-container');
    if (!tooltip || !container) return;
    const rect = container.getBoundingClientRect();
    const x = evt.clientX - rect.left;
    const y = evt.clientY - rect.top;
    tooltip.style.left = `${Math.max(90, Math.min(rect.width - 90, x))}px`;
    tooltip.style.top = `${Math.max(10, y - 8)}px`;
  },

  hideMonthlyTooltip() {
    const tooltip = document.getElementById('monthly-chart-tooltip');
    if (tooltip) {
      tooltip.classList.add('hidden');
    }
  },

  // 渲染各项费用累计总开销饼状图 (环形饼图 + 扇区直显金额与百分比 + 丰富图例卡片)
  renderTotalCostChart(stats, maintsList = []) {
    const container = document.getElementById('total-cost-chart-container');
    if (!container) return;

    if (!stats) {
      container.innerHTML = `
        <div class="py-10 text-center text-slate-400 text-xs bg-slate-50/50 rounded-xl border border-dashed border-slate-200">
          <p class="font-medium text-slate-500 mb-1">暂无费用记录</p>
          <p>录入加油、充电或各项费用支出后，系统将自动汇总生成各项费用统计饼状图</p>
        </div>
      `;
      return;
    }

    // 1. 燃油费与电费总额
    const fuelCost = parseFloat(stats.fuel?.total_cost !== undefined ? stats.fuel.total_cost : (stats.fuel?.total_fuel_cost || 0));
    const elecCost = parseFloat(stats.electricity?.total_cost !== undefined ? stats.electricity.total_cost : (stats.electricity?.total_elec_cost || 0));

    // 2. 统计各项维保/费用支出明细累计总额
    const maintCatMap = {};
    let maintSum = 0;
    (maintsList || []).forEach(r => {
      const item = (r.item_type || '其他').trim() || '其他';
      const c = parseFloat(r.cost) || 0;
      if (c !== 0) {
        maintCatMap[item] = parseFloat(((maintCatMap[item] || 0) + c).toFixed(2));
        maintSum += c;
      }
    });

    const totalMaintCostFromStats = parseFloat(stats.maintenance?.total_cost || 0);
    const remainMaint = parseFloat((totalMaintCostFromStats - maintSum).toFixed(2));
    if (remainMaint > 0.01) {
      maintCatMap['其他'] = parseFloat(((maintCatMap['其他'] || 0) + remainMaint).toFixed(2));
    }

    // 3. 构建各项费用项目列表 (Item)
    const items = [];
    if (fuelCost > 0) {
      items.push({
        name: '油费',
        cost: fuelCost,
        color: '#f59e0b',
        icon: '⛽'
      });
    }
    if (elecCost > 0) {
      items.push({
        name: '电费',
        cost: elecCost,
        color: '#10b981',
        icon: '⚡'
      });
    }

    // 维保类别加入列表
    Object.keys(maintCatMap).forEach(cat => {
      items.push({
        name: cat,
        cost: maintCatMap[cat],
        color: this.getCategoryColor(cat),
        icon: '🏷️'
      });
    });

    // 核心要求：按费用金额/百分比从大到小严格降序排列
    items.sort((a, b) => b.cost - a.cost);

    // 累计总费用
    const totalAllCost = items.reduce((acc, it) => acc + it.cost, 0);
    const sumElem = document.getElementById('total-cost-chart-sum');
    if (sumElem) {
      sumElem.textContent = `¥${this.formatThousands(totalAllCost, 2)}`;
    }

    if (items.length === 0 || totalAllCost <= 0.001) {
      container.innerHTML = `
        <div class="py-10 text-center text-slate-400 text-xs bg-slate-50/50 rounded-xl border border-dashed border-slate-200">
          <p class="font-medium text-slate-500 mb-1">暂无费用支出记录</p>
          <p>录入加油、充电或维保支出后，系统将自动汇总各项累计总费用饼图</p>
        </div>
      `;
      return;
    }

    // 4. SVG 环形饼图几何与扇区路径计算
    const cx = 210;
    const cy = 200;
    const rOuter = 135;
    const rInner = 74;
    const svgW = 420;
    const svgH = 400;

    const polarToCartesian = (centerX, centerY, radius, angleInDegrees) => {
      const angleInRadians = (angleInDegrees - 90) * Math.PI / 180.0;
      return {
        x: centerX + (radius * Math.cos(angleInRadians)),
        y: centerY + (radius * Math.sin(angleInRadians))
      };
    };

    const describeDonutSlice = (startAngle, endAngle) => {
      let span = endAngle - startAngle;
      if (span >= 359.999) {
        span = 359.999;
        endAngle = startAngle + span;
      }
      const p1 = polarToCartesian(cx, cy, rOuter, startAngle);
      const p2 = polarToCartesian(cx, cy, rOuter, endAngle);
      const p3 = polarToCartesian(cx, cy, rInner, endAngle);
      const p4 = polarToCartesian(cx, cy, rInner, startAngle);
      const largeArcFlag = span > 180 ? 1 : 0;

      return `M ${p1.x.toFixed(2)} ${p1.y.toFixed(2)} A ${rOuter} ${rOuter} 0 ${largeArcFlag} 1 ${p2.x.toFixed(2)} ${p2.y.toFixed(2)} L ${p3.x.toFixed(2)} ${p3.y.toFixed(2)} A ${rInner} ${rInner} 0 ${largeArcFlag} 0 ${p4.x.toFixed(2)} ${p4.y.toFixed(2)} Z`;
    };

    let currentAngle = 0;
    let slicesSvg = '';
    let directLabelsSvg = '';

    items.forEach((it, idx) => {
      const cost = it.cost;
      const pct = (cost / totalAllCost) * 100;
      const angleSpan = (cost / totalAllCost) * 360;
      const startAngle = currentAngle;
      const endAngle = currentAngle + angleSpan;
      const midAngle = (startAngle + endAngle) / 2;
      currentAngle = endAngle;

      const pathData = describeDonutSlice(startAngle, endAngle);
      const pctFormatted = pct.toFixed(4);
      const rawData = encodeURIComponent(JSON.stringify({
        name: it.name,
        cost: cost,
        pct: pctFormatted,
        color: it.color,
        icon: it.icon,
        totalAllCost: totalAllCost
      }));

      // 扇区图形与交互
      slicesSvg += `
        <path d="${pathData}"
              fill="${it.color}"
              stroke="#ffffff"
              stroke-width="2.5"
              stroke-linejoin="round"
              class="total-pie-slice cursor-pointer transition-transform duration-200 origin-center hover:opacity-90"
              data-info="${rawData}"
              data-item-index="${idx}"
              onmouseenter="App.handleTotalPieHover(event, this)"
              onmousemove="App.moveTotalCostTooltip(event)"
              onmouseleave="App.hideTotalCostTooltip()"
              onclick="App.handleTotalPieHover(event, this)">
          <title>${it.name}: ¥${this.formatThousands(cost, 2)} (${pctFormatted}%)</title>
        </path>
      `;

      // 直接在饼图扇区上标明：大扇区内嵌，较小扇区引线或直接标注
      if (pct >= 8) {
        // 较大扇区：文字直接放置在扇区圆环中间
        const rLabel = (rInner + rOuter) / 2;
        const pos = polarToCartesian(cx, cy, rLabel, midAngle);
        // 若扇区非常大，显示名称+金额+占比；否则显示金额+占比
        const showName = pct >= 14;
        const formattedAmount = `¥${this.formatThousands(cost, 0)}`;

        directLabelsSvg += `
          <g class="pointer-events-none select-none text-center">
            ${showName ? `<text x="${pos.x.toFixed(1)}" y="${(pos.y - 10).toFixed(1)}" text-anchor="middle" font-size="10.5" font-weight="bold" fill="#ffffff" filter="drop-shadow(0px 1px 2px rgba(0,0,0,0.6))">${it.name}</text>` : ''}
            <text x="${pos.x.toFixed(1)}" y="${showName ? (pos.y + 3).toFixed(1) : (pos.y - 3).toFixed(1)}" text-anchor="middle" font-size="11" font-weight="extrabold" font-family="monospace" fill="#ffffff" filter="drop-shadow(0px 1px 2px rgba(0,0,0,0.65))">${formattedAmount}</text>
            <text x="${pos.x.toFixed(1)}" y="${showName ? (pos.y + 16).toFixed(1) : (pos.y + 11).toFixed(1)}" text-anchor="middle" font-size="9" font-weight="600" font-family="monospace" fill="#ffffff" opacity="0.95" filter="drop-shadow(0px 1px 2px rgba(0,0,0,0.6))">${pctFormatted}%</text>
          </g>
        `;
      } else if (pct >= 3.5) {
        // 中小扇区：在环形外侧放置引线和标注
        const pLineStart = polarToCartesian(cx, cy, rOuter + 2, midAngle);
        const pLineEnd = polarToCartesian(cx, cy, rOuter + 14, midAngle);
        const isRightSide = (midAngle >= 0 && midAngle < 180);
        const pText = polarToCartesian(cx, cy, rOuter + 18, midAngle);
        const formattedAmount = `¥${this.formatThousands(cost, 0)}`;

        directLabelsSvg += `
          <g class="pointer-events-none select-none">
            <line x1="${pLineStart.x.toFixed(1)}" y1="${pLineStart.y.toFixed(1)}" x2="${pLineEnd.x.toFixed(1)}" y2="${pLineEnd.y.toFixed(1)}" stroke="${it.color}" stroke-width="1.2" opacity="0.8"/>
            <text x="${pText.x.toFixed(1)}" y="${(pText.y + 3).toFixed(1)}" text-anchor="${isRightSide ? 'start' : 'end'}" font-size="8.5" font-weight="bold" font-family="monospace" fill="#334155">
              ${formattedAmount} (${pctFormatted}%)
            </text>
          </g>
        `;
      }
    });

    // 5. 右侧详细费用类型表格 (按百分比从大到小严格降序，展示排名、类别、金额与4位小数百分比)
    let legendTableRowsHtml = '';
    items.forEach((it, idx) => {
      const cost = it.cost;
      const pctNum = (cost / totalAllCost) * 100;
      const pctStr = pctNum.toFixed(4);
      const rank = idx + 1;
      const rankBadge = rank === 1 ? 'bg-amber-100 text-amber-700 font-bold'
                      : rank === 2 ? 'bg-slate-200 text-slate-700 font-bold'
                      : rank === 3 ? 'bg-amber-50 text-amber-800 font-bold'
                      : 'bg-slate-100 text-slate-500';

      legendTableRowsHtml += `
        <tr class="hover:bg-slate-50/90 transition cursor-pointer group"
            onmouseenter="App.highlightPieSlice(${idx})"
            onmouseleave="App.unhighlightPieSlice()">
          <td class="py-2.5 px-3 text-center whitespace-nowrap">
            <span class="inline-flex items-center justify-center w-5 h-5 rounded-full text-[11px] ${rankBadge}">
              ${rank}
            </span>
          </td>
          <td class="py-2.5 px-3 whitespace-nowrap">
            <div class="flex items-center space-x-2">
              <span class="w-3 h-3 rounded-md flex-shrink-0 shadow-sm" style="background-color: ${it.color};"></span>
              <span class="text-xs font-bold text-slate-800 group-hover:text-blue-600 transition-colors">${it.name}</span>
            </div>
          </td>
          <td class="py-2.5 px-3 text-right whitespace-nowrap">
            <span class="text-xs font-mono font-bold text-slate-900">¥${this.formatThousands(cost, 2)}</span>
          </td>
          <td class="py-2.5 px-3 text-right whitespace-nowrap">
            <div class="flex items-center justify-end space-x-2">
              <span class="text-xs font-mono font-semibold text-emerald-600">${pctStr}%</span>
              <div class="w-12 h-1.5 bg-slate-100 rounded-full overflow-hidden flex-shrink-0 hidden sm:block">
                <div class="h-full rounded-full" style="width: ${Math.min(100, Math.max(2, pctNum))}%; background-color: ${it.color};"></div>
              </div>
            </div>
          </td>
        </tr>
      `;
    });

    // 6. 整合组装并挂载到容器
    container.innerHTML = `
      <div class="flex flex-col lg:flex-row items-center justify-center gap-6 py-2">
        <!-- 左侧: 环形饼状图 (含中心总额与直接数据标注) -->
        <div class="flex-shrink-0 flex items-center justify-center relative">
          <svg viewBox="0 0 ${svgW} ${svgH}" class="w-[320px] sm:w-[380px] h-[320px] sm:h-[380px] max-w-full overflow-visible">
            <!-- 滤镜阴影 -->
            <defs>
              <filter id="pie-shadow" x="-10%" y="-10%" width="120%" height="120%">
                <feDropShadow dx="0" dy="2" stdDeviation="3" flood-opacity="0.08"/>
              </filter>
            </defs>

            <!-- 扇区组 (按百分比由大到小顺时针顺延排列) -->
            <g filter="url(#pie-shadow)">
              ${slicesSvg}
            </g>

            <!-- 中心打底圆环与文字 (累计总金额) -->
            <circle cx="${cx}" cy="${cy}" r="${rInner - 2}" fill="#ffffff" class="filter drop-shadow-sm"/>
            <text x="${cx}" y="${cy - 12}" text-anchor="middle" font-size="11.5" font-weight="600" fill="#64748b">累计总支出</text>
            <text x="${cx}" y="${cy + 10}" text-anchor="middle" font-size="17" font-weight="extrabold" font-family="monospace" fill="#0f172a">¥${this.formatThousands(totalAllCost, 2)}</text>
            <text x="${cx}" y="${cy + 26}" text-anchor="middle" font-size="10" font-weight="500" fill="#94a3b8">${items.length} 个支出类别</text>

            <!-- 扇区直接数字/百分比标注 -->
            ${directLabelsSvg}
          </svg>
        </div>

        <!-- 右侧: 费用类型明细表格 (按百分比严格从大到小降序排列) -->
        <div class="flex-1 w-full max-w-xl lg:max-w-none">
          <div class="overflow-hidden border border-slate-200/80 rounded-xl bg-white shadow-xs">
            <div class="max-h-[380px] overflow-y-auto">
              <table class="w-full text-left border-collapse">
                <thead class="bg-slate-50/90 sticky top-0 z-10 border-b border-slate-200/80 backdrop-blur-xs">
                  <tr class="text-[11px] font-semibold text-slate-500 uppercase tracking-wider">
                    <th scope="col" class="py-2 px-3 text-center w-12">#</th>
                    <th scope="col" class="py-2 px-3">费用类型</th>
                    <th scope="col" class="py-2 px-3 text-right">支出金额</th>
                    <th scope="col" class="py-2 px-3 text-right">占比 (降序)</th>
                  </tr>
                </thead>
                <tbody class="divide-y divide-slate-100">
                  ${legendTableRowsHtml}
                </tbody>
              </table>
            </div>
          </div>
        </div>
      </div>
    `;
  },

  handleTotalPieHover(evt, elem) {
    if (!elem) return;
    try {
      const raw = elem.getAttribute('data-info');
      if (!raw) return;
      const data = JSON.parse(decodeURIComponent(raw));
      this.showTotalCostTooltip(evt, data);
    } catch (e) {
      console.warn('解析总费用浮窗数据异常:', e);
    }
  },

  highlightPieSlice(index) {
    const slices = document.querySelectorAll('.total-pie-slice');
    slices.forEach(slice => {
      const idx = slice.getAttribute('data-item-index');
      if (idx == index) {
        slice.style.transform = 'scale(1.04)';
        slice.style.opacity = '1';
        slice.style.strokeWidth = '3.5';
      } else {
        slice.style.transform = 'scale(1)';
        slice.style.opacity = '0.55';
        slice.style.strokeWidth = '2.5';
      }
    });
  },

  unhighlightPieSlice() {
    const slices = document.querySelectorAll('.total-pie-slice');
    slices.forEach(slice => {
      slice.style.transform = 'scale(1)';
      slice.style.opacity = '1';
      slice.style.strokeWidth = '2.5';
    });
  },

  showTotalCostTooltip(evt, data) {
    const tooltip = document.getElementById('total-cost-chart-tooltip');
    if (!tooltip || !data) return;

    tooltip.innerHTML = `
      <div class="font-bold text-slate-100 border-b border-slate-700/80 pb-1 mb-1.5 flex items-center justify-between gap-4">
        <span class="flex items-center gap-1.5">
          <span class="w-2.5 h-2.5 rounded-sm" style="background-color: ${data.color};"></span>
          <span>${data.name}</span>
        </span>
        <span class="text-amber-400 font-mono font-extrabold text-sm">¥${this.formatThousands(data.cost, 2)}</span>
      </div>
      <div class="space-y-1 text-[11px] text-slate-300">
        <div class="flex items-center justify-between gap-4">
          <span>占累计总支出比例:</span>
          <span class="font-mono text-emerald-400 font-bold">${data.pct}%</span>
        </div>
        <div class="flex items-center justify-between gap-4 text-slate-400 text-[10px]">
          <span>累计总费用基数:</span>
          <span class="font-mono text-slate-300">¥${this.formatThousands(data.totalAllCost, 2)}</span>
        </div>
      </div>
    `;
    tooltip.classList.remove('hidden');
    this.moveTotalCostTooltip(evt);
  },

  moveTotalCostTooltip(evt) {
    const tooltip = document.getElementById('total-cost-chart-tooltip');
    const container = document.getElementById('total-cost-chart-container');
    if (!tooltip || !container) return;
    const rect = container.getBoundingClientRect();
    const x = evt.clientX - rect.left;
    const y = evt.clientY - rect.top;
    tooltip.style.left = `${Math.max(90, Math.min(rect.width - 90, x))}px`;
    tooltip.style.top = `${Math.max(10, y - 8)}px`;
  },

  hideTotalCostTooltip() {
    const tooltip = document.getElementById('total-cost-chart-tooltip');
    if (tooltip) {
      tooltip.classList.add('hidden');
    }
  },

  getPowerTypeName(type) {
    const map = {
      gas: '纯燃油',
      ev: '纯电动',
      phev: '插电混动',
      erev: '增程式',
      hybrid: '油电混动'
    };
    return map[type] || '燃油车';
  },

  showToast(message, type = 'info') {
    const container = document.getElementById('toast-container');
    const toast = document.createElement('div');
    toast.className = `toast toast-${type}`;
    const icon = type === 'success' ? '✅' : (type === 'error' ? '❌' : 'ℹ️');
    toast.innerHTML = `<span>${icon}</span> <span>${message}</span>`;
    container.appendChild(toast);

    setTimeout(() => {
      toast.style.opacity = '0';
      toast.style.transition = 'opacity 0.3s ease';
      setTimeout(() => toast.remove(), 300);
    }, 3200);
  }
};

// 挂载至全局 window 对象
if (typeof window !== 'undefined') {
  window.App = App;
}

// 启动应用
document.addEventListener('DOMContentLoaded', () => {
  App.init();
});
