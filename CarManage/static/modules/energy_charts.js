/**
 * 我的车管家 · 前端模块化子系统 (energy_charts.js)
 * 职责：区间能耗与 FIFO 实耗成本分析引擎、SVG 趋势图与多维费用图表模块
 */
(function (global) {
  global.AppModules = global.AppModules || {};
  global.AppModules.EnergyCharts = {
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
})(typeof window !== "undefined" ? window : globalThis);
