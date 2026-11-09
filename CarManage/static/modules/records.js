/**
 * 我的车管家 · 前端模块化子系统 (records.js)
 * 职责：车辆档案管理、概览控制台、油/电/维保/状态快照 CRUD 与历史明细模块
 */
(function (global) {
  global.AppModules = global.AppModules || {};
  global.AppModules.Records = {
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

  // 判断当前是否处于录入页面或弹窗编辑状态（禁止后台自动刷新或重置输入内容）
  isUserInEntryOrModal() {
    if (['fuel', 'electricity', 'maintenance'].includes(this.currentTab)) {
      return true;
    }
    const modalIds = ['record-edit-modal', 'current-status-modal', 'vehicle-modal'];
    for (const id of modalIds) {
      const el = document.getElementById(id);
      if (el && !el.classList.contains('hidden')) {
        return true;
      }
    }
    return this.hasUnsavedEntryInput();
  },

  // 检查录入表单是否已有用户输入的内容（保护已填内容不被重置）
  hasUnsavedEntryInput(tab = null) {
    const checkFuel = !tab || tab === 'fuel';
    const checkElec = !tab || tab === 'electricity';
    const checkMaint = !tab || tab === 'maintenance';

    const hasVal = (id) => {
      const el = document.getElementById(id);
      return Boolean(el && String(el.value || '').trim() !== '');
    };
    const isEdited = (id) => {
      const el = document.getElementById(id);
      return Boolean(el && el.dataset && el.dataset.isDefault === 'false' && String(el.value || '').trim() !== '');
    };

    if (checkFuel) {
      if (
        hasVal('fuel-amount') ||
        hasVal('fuel-unit-price') ||
        hasVal('fuel-total-price') ||
        hasVal('fuel-station') ||
        hasVal('fuel-notes') ||
        isEdited('fuel-mileage') ||
        isEdited('fuel-pre-percent') ||
        isEdited('fuel-post-percent') ||
        isEdited('fuel-battery-percent')
      ) {
        return true;
      }
    }

    if (checkElec) {
      if (
        hasVal('elec-amount') ||
        hasVal('elec-unit-price') ||
        hasVal('elec-total-price') ||
        hasVal('elec-station') ||
        hasVal('elec-notes') ||
        isEdited('elec-mileage') ||
        isEdited('elec-start-soc') ||
        isEdited('elec-end-soc') ||
        isEdited('elec-fuel-percent')
      ) {
        return true;
      }
    }

    if (checkMaint) {
      if (hasVal('maint-item-type') || hasVal('maint-cost') || hasVal('maint-notes')) {
        return true;
      }
    }

    return false;
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

    const prevActiveVid = this.activeVehicleId;
    const savedVid = localStorage.getItem('car_tracker_active_vid');
    const foundSaved = this.vehicles.find(v => v.id == savedVid);
    this.activeVehicleId = foundSaved ? foundSaved.id : this.vehicles[0].id;
    const vidChanged = prevActiveVid !== this.activeVehicleId;

    this.vehicles.forEach(v => {
      const opt = document.createElement('option');
      opt.value = v.id;
      opt.textContent = `🚗 ${v.plate_number || v.name} · ${v.brand} ${v.model}`;
      if (v.id === this.activeVehicleId) opt.selected = true;
      select.appendChild(opt);
    });

    // 若用户正处于录入界面且当前激活车辆未变化，禁止重置表单输入内容
    this.syncActiveVehicleToForms(vidChanged);
    this.syncVehiclePowerUI();
    if (!['fuel', 'electricity', 'maintenance'].includes(this.currentTab)) {
      this.loadOverview();
    }
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
    this.syncActiveVehicleToForms(true);
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

  async syncActiveVehicleToForms(forceReset = false) {
    const v = this.getActiveVehicle();
    if (!v) return;

    const vehicleChanged = this._lastSyncedFormVehicleId !== v.id;
    const shouldForce = Boolean(forceReset || vehicleChanged);
    this._lastSyncedFormVehicleId = v.id;

    const fuelDirty = !shouldForce && this.hasUnsavedEntryInput('fuel');
    const elecDirty = !shouldForce && this.hasUnsavedEntryInput('electricity');

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

    const bindDefaultTracker = (el) => {
      if (el && !el.dataset.hasListener) {
        el.addEventListener('input', () => {
          el.dataset.isDefault = 'false';
        });
        el.dataset.hasListener = 'true';
      }
    };

    const fuelMileage = document.getElementById('fuel-mileage');
    if (fuelMileage) {
      fuelMileage.placeholder = '当前基准 ' + this.formatThousands(benchmarkMileage);
      if (shouldForce || ((!fuelMileage.value || fuelMileage.dataset.isDefault === 'true') && !fuelDirty)) {
        fuelMileage.value = benchmarkMileage;
        fuelMileage.dataset.isDefault = 'true';
      }
      bindDefaultTracker(fuelMileage);
    }

    const elecMileage = document.getElementById('elec-mileage');
    if (elecMileage) {
      elecMileage.placeholder = '当前基准 ' + this.formatThousands(benchmarkMileage);
      if (shouldForce || ((!elecMileage.value || elecMileage.dataset.isDefault === 'true') && !elecDirty)) {
        elecMileage.value = benchmarkMileage;
        elecMileage.dataset.isDefault = 'true';
      }
      bindDefaultTracker(elecMileage);
    }

    // 1. 油耗表单：加前起步油量（默认为最后一次油量）并自动计算加后油量（仅在未修改或强制重置时更新）
    const fuelPreInput = document.getElementById('fuel-pre-percent');
    const fuelPostInput = document.getElementById('fuel-post-percent');
    bindDefaultTracker(fuelPreInput);
    bindDefaultTracker(fuelPostInput);
    if (fuelPreInput && (shouldForce || (!fuelDirty && (!fuelPreInput.value || fuelPreInput.dataset.isDefault !== 'false')))) {
      const lastFuel = await this.getLastKnownFuelPercent(v.id);
      fuelPreInput.value = lastFuel;
      fuelPreInput.dataset.isDefault = 'true';
      if (shouldForce && fuelPostInput) {
        fuelPostInput.dataset.isDefault = 'true';
      }
      this.calcFuelPostPercent();
    }

    // 1.1 油耗表单：当前电量输入框（纯燃油车自动隐藏，混动/增程车默认带出最后一次电量）
    const fuelBatBox = document.getElementById('fuel-battery-percent-box');
    const fuelBatInput = document.getElementById('fuel-battery-percent');
    bindDefaultTracker(fuelBatInput);
    if (fuelBatBox) {
      if (v.power_type === 'gas') {
        fuelBatBox.classList.add('hidden');
      } else {
        fuelBatBox.classList.remove('hidden');
        if (fuelBatInput && (shouldForce || (!fuelDirty && (!fuelBatInput.value || fuelBatInput.dataset.isDefault !== 'false')))) {
          const lastSoc = await this.getLastKnownBatterySoc(v.id);
          fuelBatInput.value = (lastSoc !== null && lastSoc !== undefined) ? lastSoc : '';
          fuelBatInput.dataset.isDefault = 'true';
        }
      }
    }

    // 1.2 油耗表单：油品标号（默认使用上次录入的值，若用户正在填写油耗表单则不覆盖）
    const fuelTypeInput = document.getElementById('fuel-type');
    if (fuelTypeInput && (shouldForce || (!fuelDirty && !fuelTypeInput.value))) {
      const lastFuelType = await this.getLastKnownFuelType(v.id);
      this.setFuelType(lastFuelType);
    }

    // 2. 电耗表单：起始电量（默认为最后一次电量）并自动推算结束电量（仅在未修改或强制重置时更新）
    const elecStartInput = document.getElementById('elec-start-soc');
    const elecEndInput = document.getElementById('elec-end-soc');
    bindDefaultTracker(elecStartInput);
    bindDefaultTracker(elecEndInput);
    if (elecStartInput && (shouldForce || (!elecDirty && (!elecStartInput.value || elecStartInput.dataset.isDefault !== 'false')))) {
      const lastSoc = await this.getLastKnownBatterySoc(v.id);
      elecStartInput.value = lastSoc;
      elecStartInput.dataset.isDefault = 'true';
      if (shouldForce && elecEndInput) {
        elecEndInput.dataset.isDefault = 'true';
      }
      this.calcElecEndSoc();
    }

    // 3. 电耗表单：油量百分比输入框（纯电车自动隐藏，混动/增程车默认带出最后一次油量）
    const elecFuelBox = document.getElementById('elec-fuel-box');
    const elecFuelInput = document.getElementById('elec-fuel-percent');
    bindDefaultTracker(elecFuelInput);
    if (elecFuelBox) {
      if (v.power_type === 'ev') {
        elecFuelBox.classList.add('hidden');
      } else {
        elecFuelBox.classList.remove('hidden');
        if (elecFuelInput && (shouldForce || (!elecDirty && (!elecFuelInput.value || elecFuelInput.dataset.isDefault !== 'false')))) {
          const lastFuel = await this.getLastKnownFuelPercent(v.id);
          elecFuelInput.value = lastFuel;
          elecFuelInput.dataset.isDefault = 'true';
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
    const fuelMileageEl = document.getElementById('fuel-mileage');
    if (fuelMileageEl) {
      fuelMileageEl.value = '';
      fuelMileageEl.dataset.isDefault = 'true';
    }
    const fuelPreEl = document.getElementById('fuel-pre-percent');
    if (fuelPreEl) fuelPreEl.dataset.isDefault = 'true';
    const fuelPostEl = document.getElementById('fuel-post-percent');
    if (fuelPostEl) fuelPostEl.dataset.isDefault = 'true';
    const fuelBatEl = document.getElementById('fuel-battery-percent');
    if (fuelBatEl) fuelBatEl.dataset.isDefault = 'true';
    document.getElementById('fuel-station').value = '';
    const clearBtn = document.getElementById('fuel-station-clear-btn');
    if (clearBtn) clearBtn.classList.add('hidden');
    this.closeFuelStationDropdown();
    await this.extractFuelStationsHistory();
    this.renderFuelStationPills();
    document.getElementById('fuel-notes').value = '';
    await this.syncActiveVehicleToForms(true);

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
    const elecMileageEl = document.getElementById('elec-mileage');
    if (elecMileageEl) {
      elecMileageEl.value = '';
      elecMileageEl.dataset.isDefault = 'true';
    }
    const elecStartEl = document.getElementById('elec-start-soc');
    if (elecStartEl) {
      elecStartEl.value = '';
      elecStartEl.dataset.isDefault = 'true';
    }
    const elecEndEl = document.getElementById('elec-end-soc');
    if (elecEndEl) {
      elecEndEl.value = '';
      elecEndEl.dataset.isDefault = 'true';
    }
    const elecFuelEl = document.getElementById('elec-fuel-percent');
    if (elecFuelEl) elecFuelEl.dataset.isDefault = 'true';
    document.getElementById('elec-station').value = '';
    const clearBtn = document.getElementById('elec-station-clear-btn');
    if (clearBtn) clearBtn.classList.add('hidden');
    this.closeElecStationDropdown();
    await this.extractElecStationsHistory();
    this.renderElecStationPills();
    document.getElementById('elec-notes').value = '';
    const calToggle = document.getElementById('elec-battery-calibrate-toggle');
    if (calToggle) calToggle.checked = false;
    this.toggleBatteryCalibration(false);
    this.updateSocBar();
    await this.syncActiveVehicleToForms(true);

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
  }
  };
})(typeof window !== "undefined" ? window : globalThis);
