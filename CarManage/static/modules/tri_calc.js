/**
 * 我的车管家 · 前端模块化子系统 (tri_calc.js)
 * 职责：油电三项智能联动计算、电池容量校准与常用费用标签模块
 */
(function (global) {
  global.AppModules = global.AppModules || {};
  global.AppModules.TriCalc = {
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
  }
  };
})(typeof window !== "undefined" ? window : globalThis);
