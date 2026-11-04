/**
 * 汽车维护与能耗记录核心组装器与启动引导 (app.js)
 * 架构说明：第一阶段模块化重构后，核心业务逻辑按高内聚原则拆分为 6 个独立子模块：
 *   - static/modules/auth.js          (AppModules.Auth: 用户认证、密码哈希与会话管理)
 *   - static/modules/sync_ui.js       (AppModules.SyncUI: PWA 离线缓存、云端同步状态与乐观锁冲突处理 UI)
 *   - static/modules/tri_calc.js      (AppModules.TriCalc: 油电三项联动计算、电池容量校准与常用费用标签)
 *   - static/modules/stations.js      (AppModules.Stations: 双地图并发检索 page_size、getStationBrandKey 品牌保护、VERIFIED_CHARGING_STATIONS [2f543338cfd7cce19a00508d 理想超充站] 与 30m 空间去重)
 *   - static/modules/records.js       (AppModules.Records: 车辆档案、概览看板、油/电/维保/状态快照 CRUD 与历史明细)
 *   - static/modules/energy_charts.js (AppModules.EnergyCharts: 区间能耗、FIFO 实耗成本、SVG 趋势图与多维费用图表)
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
  }

};

// 模块化自动装配 (将 AppModules 各业务子模块混入 App 核心单例)
(function assembleAppModules(global) {
  const mods = global.AppModules || {};
  const moduleOrder = ["Auth", "SyncUI", "TriCalc", "Stations", "Records", "EnergyCharts"];
  for (const modName of moduleOrder) {
    if (mods[modName]) {
      Object.assign(App, mods[modName]);
    }
  }
})(typeof window !== "undefined" ? window : globalThis);

// 挂载至全局 window 对象
if (typeof window !== "undefined") {
  window.App = App;
}

// 启动应用
if (typeof document !== "undefined") {
  document.addEventListener("DOMContentLoaded", () => {
    App.init();
  });
}
