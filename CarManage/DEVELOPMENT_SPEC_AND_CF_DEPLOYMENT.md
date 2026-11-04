# 《我的车管家》完整开发需求规格说明书与 Cloudflare 自动化部署指南

> **适用说明**：本指南为《我的车管家》（Car Maintenance & Energy Manager）的全景需求规格说明书（PRD）与自动化云原生部署手册。任何开发人员或 AI 辅助编程智能体（Vibe Coding Agent）凭借本文档，即可在本地与云端完整复现该系统，并实现基于 Cloudflare Workers + D1 的零成本极速自动化部署。
>
> ⚠️ **核心原则声明**：**新建或初始化部署时，数据库必须完全为空，绝不允许包含任何假数据、测试数据或预设种子数据！用户初次使用时为绝对纯净的新建档案流程。**

---

## 目录
1. [项目全景与设计哲学](#一项目全景与设计哲学)
2. [业务需求与功能规格说明 (PRD)](#二业务需求与功能规格说明-prd)
   - [2.1 多车辆档案管理](#21-多车辆档案管理)
   - [2.2 油耗录入与智能推导](#22-油耗录入与智能推导)
   - [2.3 电耗录入与智能推导](#23-电耗录入与智能推导)
   - [2.4 维保与日常费用支出](#24-维保与日常费用支出)
   - [2.5 能耗计算引擎核心算法与公式](#25-能耗计算引擎核心算法与公式)
   - [2.6 概览控制台 (Dashboard)](#26-概览控制台-dashboard)
   - [2.7 能耗分析面板 (Energy Analysis)](#27-能耗分析面板-energy-analysis)
   - [2.8 离线优先 PWA 与双向数据同步机制](#28-离线优先-pwa-与双向数据同步机制)
3. [系统技术架构与交互流](#三系统技术架构与交互流)
4. [数据库表结构定义 (D1 SQL Schema · 纯净空表)](#四数据库表结构定义-d1-sql-schema--纯净空表)
5. [RESTful API 接口规范](#五restful-api-接口规范)
6. [Cloudflare 边缘全自动化部署实现](#六cloudflare-边缘全自动化部署实现)
   - [6.1 单 Worker 全栈架构设计](#61-单-worker-全栈架构设计)
   - [6.2 零依赖 Python 自动化部署脚本 (`deploy_cf.py`)](#62-零依赖-python-自动化部署脚本-deploy_cfpy)
   - [6.3 传统 Wrangler CLI 部署方式](#63-传统-wrangler-cli-部署方式)
7. [Vibe Coding 完整实操指令模板 (Prompt for AI)](#七vibe-coding-完整实操指令模板-prompt-for-ai)

---

## 一、项目全景与设计哲学

### 1.1 软件定位
《我的车管家》是一款专为私家车主打造的**全动力类型、离线优先、极致轻量、零服务器维护成本**的现代化车辆能耗与养车费用管理系统。

### 1.2 核心设计哲学
1. **多动力车型全覆盖**：原生完美支持 **燃油车 (Gas)**、**纯电动 (EV)**、**插电混动 (PHEV)**、**增程式 (EREV)** 以及 **油电混动 (Hybrid)**，尤其解决混动/增程车型“油电混加、能耗割裂、难以统一衡量”的行业痛点。
2. **离线优先 (Offline-First)**：地下车库、偏远山区等无信号弱网环境下，PWA 原生秒开，所有录入写入本地 IndexedDB，一旦网络恢复后台静默双向无感同步至云端。
3. **极简高效录入**：油耗与电耗录入提供“单价、量数、总价任意两项自算第三项”，省去用户心算与复杂操作。
4. **科学严谨的能耗核算**：
   - 累计能耗严格以**实际行驶总里程（最新里程 - 初始里程）**为唯一定义分母。
   - 建立业界公认的**等效电耗换算标准（1L 油 = 3.3 kWh 电）**，为多能源车提供全局横向对比视角。
5. **极速零成本 Serverless 架构**：全系统由单个 Cloudflare Worker + Cloudflare D1 分布式数据库托管，享有全球 CDN 毫秒级加速、免费 HTTPS 证书、以及永久免费的基础额度（每天 10 万次请求 + 500 万行读取）。

---

## 二、业务需求与功能规格说明 (PRD)

### 2.1 多车辆档案管理
- **功能目标**：支持单个用户管理多台不同类型的车辆，可随时平滑切换当前操作车辆。
- **字段要求**：
  - `name` (车名/昵称，如“我的通勤车”)
  - `brand` (品牌，如“理想”、“比亚迪”、“特斯拉”)
  - `model` (型号，如“L7 Max”、“汉 EV”)
  - `power_type` (动力类型：`gas` 燃油, `ev` 纯电, `phev` 插电混动, `erev` 增程, `hybrid` 油电混动)
  - `plate_number` (车牌号，可选)
  - `displacement` (排量，如 1.5T / 2.0L，可选)
  - `fuel_tank_capacity` (油箱容量，单位：L)
  - `battery_capacity` (动力电池容量，单位：kWh)
  - `initial_mileage` (提车/接车初始里程，单位：km，默认 0)
  - `current_mileage` (当前最新表显里程，单位：km)
  - `initial_fuel_percent` (提车初始油量百分比，默认 0~100%)
  - `initial_battery_percent` (提车初始电量百分比，默认 0~100%)
  - `purchase_date` (购车日期)
  - `notes` (备注)
- **里程联动规则**：`current_mileage` 由系统自动根据该车名下所有加油记录、充电记录、维保记录中出现的最大里程与 `initial_mileage` 自动取大值驱动更新。

---

### 2.2 油耗录入与智能推导
- **任意两项自算第三项**：用户在【加油量 (L)】、【单价 (元/L)】、【总价 (元)】三项中任意输入两项，前端立即实时演算补齐第三项。
- **油品标号智能记忆**：油品标号（如 `92#`、`95#`、`98#`、`0# 柴油` 等）默认自动带出该车辆上次录入的油品作为初始默认值；若该车辆暂无加油记录，自动继承用户最近使用的本地记忆或兜底 `92#`，并在用户切换快捷按钮或手动输入时同步高亮与记忆。
- **加油站多模态智能选择（三种方式）**：
  1. **历史加油站（使用次数越多越靠前）**：自动聚合统计所有历史加油记录中的油站使用频次，频次越高排序越靠前，以药丸按钮形式展示（带有 `(X次)` 标记），点击按钮直接填入并激活高亮。
  2. **高德地图定位与 POI 模糊搜索（固化 Key: 9885ffe2a89aff7e151b50fa4801c9a9，无需用户配置）**：
     - **GPS 定位周边油站**：点击【📍 定位附近油站】获取设备当前经纬度，调用高德周边搜索接口（`place/around`），全面囊括所有品牌油站（中石油/中石化/海湾/中化/壳牌/加德士/民营油站等），按距离由近到远严格排列并浮现下拉菜单，展示距离与品牌徽标，点击即选。
     - **输入框任意地标 POI 模糊搜索与周边油站联动**：
       - 输入框不限制仅搜索加油站，可输入任意地标、商圈、大厦、路名（如“万象天地”、“科技园”、“人民广场”等）。
       - 联想下拉菜单中：若匹配项为加油站，展示品牌徽标并支持点击直接选定；若为普通地标/商圈，展示“📍 地标”标签及【⚡ 查周边油站 ➔】快捷入口。
       - 点击地标或“查周边油站”后，系统以该 POI 的坐标（若坐标为空则自动调用 API 补全）为中心调用周边油站检索，自动将该地点周边全部加油站按从近到远严格排序展示，支持【← 返回地标列表】与一键选择。
  3. **手工输入**：输入框支持自由编辑任何油站名称，支持一键清空与回车存入常用列表；提交保存后自动计入历史频次库。
- **业务明细字段**：
  - 表显里程 (km，必须大于或等于初始里程)
  - 加油量 (L)、单价 (元/L)、总价 (元)
  - 油品规格：`92#`、`95#`、`98#`、`0# 柴油` 等
  - 是否加满：勾选状态（1: 加满, 0: 未加满）
  - 起步油量与加后油量百分比（如加满则加后油量自动联动为 100%）
  - 加油时电量 SOC (%)（混动/增程式车型专用，记录加油时刻的伴随电量）
  - 加油站名称、记录时间（年月日时分）、备注说明

---

### 2.3 电耗录入与智能推导
- **任意两项自算第三项**：用户在【充电度数 (kWh)】、【单价 (元/度)】、【总价 (元)】中输入任意两项，实时自算补齐第三项。
- **业务明细字段**：
  - 表显里程 (km)
  - 充入电量 (kWh)、单价 (元/度)、总费用 (元)
  - 充电方式：`快充`、`慢充`、`家用桩`、`超充`
  - 充电起始 SOC (%) 与 结束 SOC (%)（可自动结合电池容量辅助换算）
  - 充电时剩余油量百分比 (%)（混动/增程式车型专用）
  - 充电站名称、记录时间、备注说明

---

### 2.4 维保与日常费用支出
- **费用类别划分**：
  - 常规保养（机油机滤、火花塞、刹车油等）
  - 车辆维修（机械故障、钣金喷漆等）
  - 固定开支（车辆保险、车船税、年检费）
  - 日常费用（停车费、洗车美容、路桥通行费、违章罚款、车辆改装/车载用品等）
- **预警提醒机制**：
  - 支持录入【下次建议保养里程 (km)】与【下次建议保养日期】。
  - 控制台首页自动对比当前最新里程与今日日期，产生距离保养到期剩余天数/公里的动态预警提示。

---

### 2.5 能耗计算引擎核心算法与公式

系统能耗引擎分为**“车辆全生命周期累计能耗”**和**“每次补能区间行程能耗”**两大维度。

#### 1. 累计能耗计算标准（关键定义）
> **原则**：无论是纯电能耗还是燃油能耗，分母均严格统一为**车辆行驶总里程**，分子为车辆生命周期内的累计物理消耗总量。

$$\text{行驶总里程 } S_{\text{total}} = \text{最新表显总里程 } M_{\text{current}} - \text{提车初始里程 } M_{\text{initial}}$$

- **累计百公里油耗 (L/100km)**：
  $$\text{Overall Fuel Consumption} = \left( \frac{\sum \text{Fuel Consumed (L)}}{S_{\text{total}}} \right) \times 100$$
- **累计百公里电耗 (kWh/100km)**：
  $$\text{Overall Electricity Consumption} = \left( \frac{\sum \text{Electricity Consumed (kWh)}}{S_{\text{total}}} \right) \times 100$$

#### 2. 等效电耗换算模型 (Equivalent Electricity)
针对增程式、插电混动车，将燃油消耗以热值与驱动做功折算：
$$\text{换算基准}：1 \text{ 升燃油} = 3.3 \text{ 度电 (kWh)}$$
$$\text{即：} 1 \text{ L/100km} \Longleftrightarrow 3.3 \text{ kWh/100km}$$

- **累计百公里等效电耗 (kWh/100km)**：
  $$\text{Overall Equivalent Electricity} = \left( \frac{\sum \text{Elec (kWh)} + \sum \text{Fuel (L)} \times 3.3}{S_{\text{total}}} \right) \times 100$$

#### 3. 每次补能区间行程能耗 (Interval Consumption)
按补能时间或里程正序排列，第 $i$ 次补能与第 $i-1$ 次补能构成一个独立区间：
- **区间行驶里程**：$\Delta M = M_i - M_{i-1}$（第 1 次补能的 $M_{0}$ 对应车辆提车初始里程）
- **区间能耗核算**：
  - 考虑油量/电量百分比（如上次补能结束时的剩余容量与本次补能开始时的剩余容量之差）折算实际被消耗掉的能源量。
  - 区间百公里油耗：$\text{Interval Fuel} = \left( \frac{\text{区间消耗油量}}{\Delta M} \right) \times 100$
  - 区间百公里电耗：$\text{Interval Elec} = \left( \frac{\text{区间消耗电量}}{\Delta M} \right) \times 100$
  - 区间百公里等效电耗：$\text{Interval Equiv} = \text{Interval Elec} + (\text{Interval Fuel} \times 3.3)$
- **每公里费用成本**：$\text{Cost Per KM} = \frac{\text{本次补能总花费}}{\Delta M}$

#### 4. 历史极值统计
- **历史最省能耗**：所有有效区间中，百公里等效电耗的最小值，记录当时区间行程（如 `+420km (1200➔1620km)`）。
- **历史最高能耗**：所有有效区间中，百公里等效电耗的最大峰值。

#### 5. 每公里能耗成本（先进先出 FIFO 实耗结算）
> **设计背景**：车辆补能（加油/充电）时，每次充入的能源单价不尽相同；车辆行驶消耗的是“此前充入但尚未被耗尽”的能源。因此，真实反映能耗开销必须以**先进先出 (FIFO - First-In-First-Out)** 方式，从最早入库批次顺次结算实际烧掉或用掉的能源金额，未消耗的补能批次不提前计入。

1. **更新时机**：每次有补能（加油或充电）记录新增、修改或删除时实时更新。
2. **终极里程数 (Terminal Driven Distance)**：
   $$S_{\text{fifo}} = \text{最新一次补能时刻的表显里程 } M_{\text{latest}} - \text{提车初始里程 } M_{\text{initial}}$$
3. **实耗能源总金额 (FIFO Consumed Energy Cost)**：
   - 补能前实际被消耗的累计燃油总量 $V_{\text{consumed}}$ 与累计电能总量 $E_{\text{consumed}}$；
   - 分别在历史加油记录队列与充电记录队列中，从第 1 笔补能按时间正序依次扣除消耗量，乘以对应各笔记录的 `unit_price`（单价）：
     - 例如：共加了 4 次电，第 1 次单价 1.2 元加了 50 度，第 2 次单价 1.0 元加了 35 度，第 3 次单价 1.7 元加了 52 度。第 4 次补能时若之前累计共用了 82 度，则实耗电费为 $1.2 \times 50 + 1.0 \times 32 = 92$ 元；因第 2 次尚未用完（仅用 32/35 度），第 3 次加电暂未实际消耗，故不计入本次成本。
   - 实耗总金额：$\text{Cost}_{\text{fifo}} = \text{FIFO实耗燃油金额} + \text{FIFO实耗电力金额}$。
4. **每公里能耗成本公式**：
   $$\text{Energy Cost Per KM (FIFO)} = \frac{\text{Cost}_{\text{fifo}}}{S_{\text{fifo}}} \quad (\text{元 / km})$$
   *(若 $S_{\text{fifo}} \le 0$，则显示为 0.00)*

---

### 2.6 概览控制台 (Dashboard)
1. **顶部核心 KPI 指标卡**：
   - 当前累计里程与自购车行驶里程 ($S_{\text{total}}$)
   - **🛣️ 平均每天里程**：自购车以来的平均每日行驶里程（自购车行驶总里程 / 购车天数，单位：km/天，保留两位小数）
   - **⚡⛽ 平均每日能耗**：自购车以来的平均每日能耗费用支出（总能耗费用 / 购车天数，单位：元/天，保留两位小数），副文本展示先进先出 (FIFO) 实耗能源的日均成本（实耗能耗金额 / 购车天数）。
   - 累计能耗与维护总开销（附带每公里均摊与日均支出）
   - 百公里平均油耗（加油次数）
   - 百公里平均电耗（充电次数）
   - 综合等效电耗（等效总电量）
   - **⚡⛽ 每公里能耗成本**：根据最新补能时刻先进先出 (FIFO) 结算的实耗油电总金额除以终极里程计算（单位：元/km），鼠标悬停展示实耗油费与实耗电费明细。
2. **详细能耗与开销分卡片**：
   - ⛽ 燃油消耗总览：累计加油升数、累计加油总额、**FIFO 实耗油费**、加油记录笔数。
   - ⚡ 电力补能总览：累计充入度数、累计充电总额、综合等效电耗、**FIFO 实耗电费**、充电记录笔数。
   - 💰 其它费用支出：累计总额、支出笔数。
2. **按月费用统计多色堆叠柱状图卡片**：
   - **年份选择下拉框 (`#monthly-year-select`)**：
     - 自动提取账单中所有发生年份与当前年份（降序排列，如 `2026年`、`2025年`）。
     - 默认选中最新年份，切换时图表即时响应。
   - **自然年 12 个月份展示**：
     - 横轴固定标定为所选年份的 `1月` 至 `12月`。
     - 单条多色堆叠柱体，纵向区分三种开支：
       - 燃油费用（琥珀橙黄 `#f59e0b`）
       - 电力费用（翡翠翠绿 `#10b981`）
       - 其他支出（靛青深蓝 `#6366f1`）
     - **色块保底高度机制**：只要当月该类别金额大于 0，保底渲染至少 `6px`，彻底杜绝大额维保将几百元油电开销压缩为看不见的细线。
     - **柱顶总金额**：有费用的月份，柱顶清晰标注文案并**保留两位小数**（如 `¥65890.23`）；无消费月份以浅灰底槽占位，柱顶隐藏金额以保持页面整洁。
   - **交互 Tooltip**：鼠标悬停或轻触柱子，居中弹出深色毛玻璃卡片，显示 `YYYY年MM月` 各分类具体数值及总计。
3. **最近动态记录瀑布流**：
   - 时间线倒序汇聚展示最近录入的加油、充电、维保与开支流水，并提供单笔记录的【编辑】与【删除】弹窗操作。

---

### 2.7 能耗分析面板 (Energy Analysis)
1. **顶部能耗 4 大极值与均值卡**：
   - 综合百公里电耗、综合百公里油耗、历史最省能耗（百公里等效）、历史最高能耗（百公里等效）。
2. **每次补能百公里能耗分析柱状图**：
   - **年份选择下拉框 (`#energy-year-select`)**：
     - 包含已有年份列表及「全部年份」选项。
     - 选中某年份时：仅展示该年份内的行程补能柱体；起点标定为该年首程的上一阶段里程（如 `2026年初起点 5200 km`）；横向参考基准线自动切换为该年份的「**年度均值**」。
     - 选中全部年份时：展示全周期历史柱体，起点标定为提车初始里程，基准线展示「**累计均值**」。
   - **分类选项卡 (`#energy-chart-tabs`)**：
     - 针对混动/增程车提供快捷切换：`全部能耗`、`⛽ 仅看油耗`、`⚡ 仅看电耗`、`🔋 等效电耗`。
   - **柱体结构与注释**：
     - 柱顶：百公里能耗数值（保留 2 位小数）。
     - 底部第一行：本次行程增量（如 `+450km`）。
     - 底部第二行：里程起止跨度（如 `5200➔5650`）。
     - 底部第三行：序号与日期（如 `#3 · 09-18`）。
3. **补能区间明细流水台账**：
   - 卡片式展示每两次行程间的详细耗量、每公里成本、比对累计均值的节油/节电浮动率（🟢 优于均值 / 🟡 高于均值）。

---

### 2.8 离线优先 PWA 与双向数据同步机制
1. **Service Worker 离线应用壳 (App Shell)**：
   - 预缓存 `index.html`、`style.css`、`app.js`、`db.js`、`manifest.json`、高清 App 图标以及 Tailwind CSS 依赖。
   - 实现“断网秒开”，即使在无信号的地下车库也可顺畅打开 App 并完整操作。
2. **前端本地 IndexedDB 存储体系 (`db.js`)**：
   - 维护 4 个核心实体表：`vehicles`、`fuel_records`、`electricity_records`、`maintenance_records`。
   - 维护 1 个同步追踪表：`sync_queue`，用于保存离线状态下产生的增删改操作（`action: create/update/delete`）。
3. **双向同步引擎**：
   - 具备联网状态检测（`navigator.onLine` 与网络事件监听）。
   - 网络恢复时，自动静默把本地 `sync_queue` 提交至 `/api/sync`，并拉取云端 D1 数据库最新变更，本地无损平滑合并。

---

## 三、系统技术架构与交互流

```
                     ┌─────────────────────────────────────────────────────────┐
                     │          客户端：iOS / iPad / Android / Mac 浏览器       │
                     │          (离线优先 PWA / 原生添加到主屏幕全屏运行)          │
                     └────────────────────────────┬────────────────────────────┘
                                                  │
                       静态资源请求 (HTML/JS/CSS)   │   REST API 交互与增量同步
                                                  ▼
                     ┌─────────────────────────────────────────────────────────┐
                     │            Cloudflare Worker (边缘计算节点)             │
                     │  - 单 Worker 架构，内嵌打包后的静态前端资产 (static_assets.js) │
                     │  - 动态路由分发：非 /api/ 请求直出静态页面；/api/* 执行接口逻辑 │
                     │  - 能耗与月度统计纯边缘端原生计算引擎                        │
                     └────────────────────────────┬────────────────────────────┘
                                                  │
                                                  ▼ 绑定变量: env.DB
                     ┌─────────────────────────────────────────────────────────┐
                     │         Cloudflare D1 (分布式 Serverless SQLite)        │
                     │  - vehicles (车辆档案)                                  │
                     │  - fuel_records (加油明细)                              │
                     │  - electricity_records (充电明细)                       │
                     │  - maintenance_records (维保支出)                       │
                     │  - 初始部署状态：严格为空库 (0条业务记录)                     │
                     └─────────────────────────────────────────────────────────┘
```

---

## 四、数据库表结构定义 (D1 SQL Schema · 纯净空表)

保存为 `cloudflare/schema.sql`。**切记：此文件仅包含建表结构与索引，绝不包含任何 INSERT 记录！**

```sql
-- ==========================================================
-- 《我的车管家》Cloudflare D1 数据库初始化结构 (纯净空库)
-- ==========================================================

-- 1. 车辆信息表
CREATE TABLE IF NOT EXISTS vehicles (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL,                     -- 车名 / 昵称 (如: 我的通勤车)
    brand TEXT NOT NULL,                    -- 品牌 (如: 比亚迪, 特斯拉, 理想, 丰田)
    model TEXT NOT NULL,                    -- 型号 (如: 汉EV, 秦PLUS, Model Y, L7)
    power_type TEXT NOT NULL DEFAULT 'gas', -- 动力类型: gas, ev, phev, erev, hybrid
    displacement TEXT,                      -- 排量 (如: 1.5T, 2.0L)
    fuel_tank_capacity REAL DEFAULT 0,      -- 油箱容量 (L)
    battery_capacity REAL DEFAULT 0,        -- 动力电池容量 (kWh)
    plate_number TEXT,                      -- 车牌号
    initial_mileage REAL NOT NULL DEFAULT 0,-- 提车初始里程 (km)
    current_mileage REAL NOT NULL DEFAULT 0,-- 当前最新里程 (km)
    initial_fuel_percent REAL DEFAULT 0,    -- 提车初始油量百分比 (%)
    initial_battery_percent REAL DEFAULT 0, -- 提车初始电量百分比 (%)
    purchase_date TEXT,                     -- 购车日期
    notes TEXT,                             -- 备注
    created_at TEXT NOT NULL,               -- 创建时间 (YYYY-MM-DD HH:MM:SS)
    updated_at TEXT NOT NULL                -- 更新时间 (YYYY-MM-DD HH:MM:SS)
);

-- 2. 油耗记录表
CREATE TABLE IF NOT EXISTS fuel_records (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    vehicle_id INTEGER NOT NULL,            -- 所属车辆 ID
    mileage REAL NOT NULL,                  -- 表显里程 (km)
    fuel_amount REAL NOT NULL,              -- 加油量 (L)
    unit_price REAL NOT NULL,               -- 单价 (元/L)
    total_price REAL NOT NULL,              -- 总花费金额 (元)
    fuel_type TEXT NOT NULL DEFAULT '92#',  -- 油品 (92#, 95#, 98#, 0#柴油)
    is_full INTEGER DEFAULT 1,              -- 是否加满 (1: 是, 0: 否)
    start_fuel_percent REAL,                -- 加油前初始油量 (%)
    fuel_percent REAL DEFAULT 100,          -- 加油后油量百分比 (%)
    battery_percent REAL,                   -- 加油时伴随剩余电量 (%) (混动/增程专用)
    station TEXT,                           -- 加油站名
    record_date TEXT NOT NULL,              -- 记录时间
    notes TEXT,                             -- 备注
    created_at TEXT NOT NULL,
    FOREIGN KEY (vehicle_id) REFERENCES vehicles (id) ON DELETE CASCADE
);
CREATE INDEX IF NOT EXISTS idx_fuel_vehicle_mileage ON fuel_records (vehicle_id, mileage ASC);

-- 3. 电耗记录表
CREATE TABLE IF NOT EXISTS electricity_records (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    vehicle_id INTEGER NOT NULL,            -- 所属车辆 ID
    mileage REAL NOT NULL,                  -- 表显里程 (km)
    electricity_amount REAL NOT NULL,       -- 充入电量 (kWh)
    unit_price REAL NOT NULL,               -- 单价 (元/度)
    total_price REAL NOT NULL,              -- 总费用 (元)
    charging_type TEXT NOT NULL DEFAULT '快充', -- 充电方式 (快充, 慢充, 家充)
    start_soc REAL,                         -- 起始电量 (%)
    end_soc REAL,                           -- 结束电量 (%)
    fuel_percent REAL,                      -- 充电时伴随剩余油量 (%) (混动/增程专用)
    station TEXT,                           -- 充电站名
    record_date TEXT NOT NULL,              -- 记录时间
    notes TEXT,                             -- 备注
    created_at TEXT NOT NULL,
    FOREIGN KEY (vehicle_id) REFERENCES vehicles (id) ON DELETE CASCADE
);
CREATE INDEX IF NOT EXISTS idx_elec_vehicle_mileage ON electricity_records (vehicle_id, mileage ASC);

-- 4. 维护与综合费用支出表
CREATE TABLE IF NOT EXISTS maintenance_records (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    vehicle_id INTEGER NOT NULL,            -- 所属车辆 ID
    mileage REAL NOT NULL,                  -- 发生时表显里程 (km)
    item_type TEXT NOT NULL,                -- 费用类型 (保养, 维修, 保险, 年检, 停车, 洗车, 路桥等)
    cost REAL NOT NULL DEFAULT 0,           -- 金额 (元)
    provider TEXT,                          -- 服务商 / 4S 店
    record_date TEXT NOT NULL,              -- 发生日期
    next_mileage_reminder REAL,             -- 下次建议保养里程 (km)
    next_date_reminder TEXT,                -- 下次建议保养日期 (YYYY-MM-DD)
    notes TEXT,                             -- 备注
    created_at TEXT NOT NULL,
    FOREIGN KEY (vehicle_id) REFERENCES vehicles (id) ON DELETE CASCADE
);
CREATE INDEX IF NOT EXISTS idx_maint_vehicle_date ON maintenance_records (vehicle_id, record_date DESC);
```

---

## 五、RESTful API 接口规范

所有接口均支持 CORS 跨域，并返回统一的 JSON 格式。

| 方法 | 请求路径 | 作用说明 | 关键入参 / 说明 |
| :--- | :--- | :--- | :--- |
| `GET` | `/api/vehicles` | 获取所有车辆列表 | 返回车辆数组（按 ID 倒序） |
| `POST` | `/api/vehicles` | 新增车辆 | `name, brand, model, power_type, initial_mileage, fuel_tank_capacity, battery_capacity...` |
| `PUT` | `/api/vehicles/:id` | 修改车辆信息 | 传入待修改的属性字段 |
| `DELETE`| `/api/vehicles/:id` | 删除指定车辆 | 级联删除该车辆下所有加油、电耗、维保明细 |
| `GET` | `/api/fuel-records?vehicle_id=X` | 获取车辆加油记录 | 按里程升序排列 |
| `POST` | `/api/fuel-records` | 新增加油记录 | `mileage, fuel_amount, unit_price, total_price, is_full...` |
| `PUT` | `/api/fuel-records/:id` | 修改单笔加油明细 | 支持按 ID 编辑 |
| `DELETE`| `/api/fuel-records/:id` | 删除单笔加油明细 | 支持按 ID 删除 |
| `GET` | `/api/electricity-records?vehicle_id=X` | 获取车辆充电记录 | 按里程升序排列 |
| `POST` | `/api/electricity-records` | 新增充电记录 | `mileage, electricity_amount, unit_price, total_price...` |
| `PUT` | `/api/electricity-records/:id`| 修改单笔充电明细 | 支持按 ID 编辑 |
| `DELETE`| `/api/electricity-records/:id`| 删除单笔充电明细 | 支持按 ID 删除 |
| `GET` | `/api/maintenance-records?vehicle_id=X`| 获取维保与花费明细 | 按发生日期倒序排列 |
| `POST` | `/api/maintenance-records` | 新增维保支出明细 | `mileage, item_type, cost, provider, record_date...` |
| `PUT` | `/api/maintenance-records/:id`| 修改单笔维保开支 | 支持按 ID 编辑 |
| `DELETE`| `/api/maintenance-records/:id`| 删除单笔维保开支 | 支持按 ID 删除 |
| `GET` | `/api/stats/overview?vehicle_id=X` | 获取首页概览统计 | 返回总里程、总开销、每公里花费、最近瀑布流流水、以及按月费用数组 `monthly_expenses: [{ month: "2026-09", fuel_cost, elec_cost, other_cost, total_cost }]` |
| `GET` | `/api/stats/energy-analysis?vehicle_id=X` | 获取能耗分析全量数据 | 返回综合百公里电耗、综合百公里油耗、历史最省与最高等效电耗、按时间排序的区间补能数据集合 `recent_intervals` |
| `POST` | `/api/sync` | 离线双向批量同步 | 接收客户端批量变更操作队列，事务提交到 D1，返回云端最新数据快照 |

---

## 六、Cloudflare 边缘全自动化部署实现

### 6.1 单 Worker 全栈架构设计
本项目采用业界领先的**全栈单 Worker 交付架构**：
- 前端代码（`index.html`、`app.js`、`db.js`、`sw.js`、`manifest.json`、PNG 图标）在部署前被打包脚本转化为单一的 JavaScript 模块（`static_assets.js`），文本直接以 UTF-8 字符串存储，图片转化为 Base64 编码。
- `worker.js` 在收到非 `/api/` 路由请求时，直接从内存中极速输出对应的前端文件与正确 MIME 类型（支持 PWA 离线 Service Worker、Tailwind 缓存头）；收到 `/api/*` 请求时直连 D1 数据库执行 SQL。
- **优势**：仅需一个 Worker 即可拥有独立的完整 Web 站点和 API 网关，无需同时维护 Pages 和 Workers 两个独立服务，配置与同步最简单、最鲁棒。

---

### 6.2 零依赖 Python 自动化部署脚本 (`deploy_cf.py`)

本地无需安装任何 Node.js、npm 或 Wrangler，使用 macOS / Linux / Windows 自带的原生 Python 3（仅使用标准库 `urllib`、`json`、`base64`）即可实现 10 秒全自动打包部署与空表初始化。

```python
#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
《我的车管家》· Cloudflare 零依赖自动化部署工具
功能：
1. 自动打包前端静态资源为 ES Module (static_assets.js)
2. 自动检查或在 Cloudflare 创建 D1 数据库 (car_tracker_d1)
3. 自动在 D1 执行纯净建表脚本 (schema.sql，严格空库)
4. 自动组装并上传部署 Cloudflare Worker (绑定 D1 数据库)
5. 自动激活 workers.dev 全球公网访问子域名
"""

import os
import sys
import json
import urllib.request
import urllib.error
import base64

CF_API_BASE = "https://api.cloudflare.com/client/v4"

def make_request(url, method="GET", headers=None, data=None):
    if headers is None:
        headers = {}
    if "User-Agent" not in headers:
        headers["User-Agent"] = "CarTrackerDeployer/1.0"
    req = urllib.request.Request(url, data=data, headers=headers, method=method)
    try:
        with urllib.request.urlopen(req) as resp:
            body = resp.read().decode("utf-8")
            return resp.status, json.loads(body)
    except urllib.error.HTTPError as e:
        body = e.read().decode("utf-8")
        try:
            parsed = json.loads(body)
        except Exception:
            parsed = {"raw": body}
        return e.code, parsed

def build_static_assets(root_dir):
    static_dir = os.path.join(root_dir, "static")
    cf_dir = os.path.join(root_dir, "cloudflare")
    os.makedirs(cf_dir, exist_ok=True)
    
    assets = {}
    files_map = {
        "/": ("index.html", "text/html; charset=utf-8"),
        "/index.html": ("index.html", "text/html; charset=utf-8"),
        "/style.css": ("style.css", "text/css; charset=utf-8"),
        "/app.js": ("app.js", "application/javascript; charset=utf-8"),
        "/db.js": ("db.js", "application/javascript; charset=utf-8"),
        "/sw.js": ("sw.js", "application/javascript; charset=utf-8"),
        "/manifest.json": ("manifest.json", "application/manifest+json; charset=utf-8"),
    }
    for route, (filename, mime) in files_map.items():
        p = os.path.join(static_dir, filename)
        if os.path.exists(p):
            with open(p, "r", encoding="utf-8") as f:
                assets[route] = {"content": f.read(), "contentType": mime}

    img_map = {
        "/icon-192.png": ("icon-192.png", "image/png"),
        "/icon-512.png": ("icon-512.png", "image/png"),
    }
    for route, (filename, mime) in img_map.items():
        p = os.path.join(static_dir, filename)
        if os.path.exists(p):
            with open(p, "rb") as f:
                assets[route] = {"base64": base64.b64encode(f.read()).decode("ascii"), "contentType": mime}

    out_path = os.path.join(cf_dir, "static_assets.js")
    with open(out_path, "w", encoding="utf-8") as f:
        f.write("export const ASSETS = " + json.dumps(assets, ensure_ascii=False) + ";\n")
    print(f"📦 1. 前端静态资源打包完成: {out_path} ({os.path.getsize(out_path)} 字节)")
    return out_path

def deploy(account_id: str, api_token: str):
    root_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    headers = {
        "Authorization": f"Bearer {api_token}",
        "Content-Type": "application/json"
    }

    print("=" * 60)
    print("🚀 开始通过 Cloudflare REST API 自动化构建与边缘部署")
    print("=" * 60)

    # 1. 验证 Token 权限
    print("🔍 2. 正在校验 Cloudflare API Token 有效性...")
    status, res = make_request(f"{CF_API_BASE}/user/tokens/verify", headers=headers)
    if status != 200 or not res.get("success"):
        print(f"❌ API Token 验证失败 ({status}): {res}")
        return False
    print("✅ API Token 验证通过！")

    # 2. 检查或创建 D1 数据库 (car_tracker_d1)
    db_name = "car_tracker_d1"
    print(f"📦 3. 检查 Cloudflare D1 数据库 '{db_name}'...")
    status, res = make_request(f"{CF_API_BASE}/accounts/{account_id}/d1/database", headers=headers)
    db_id = None
    if status == 200 and res.get("success"):
        for db in res.get("result", []):
            if db.get("name") == db_name:
                db_id = db.get("uuid")
                print(f"✅ 找到现有 D1 数据库，ID: {db_id}")
                break

    if not db_id:
        print(f"⚡ 创建全新 D1 数据库: {db_name}...")
        create_payload = json.dumps({"name": db_name}).encode("utf-8")
        status, res = make_request(f"{CF_API_BASE}/accounts/{account_id}/d1/database", method="POST", headers=headers, data=create_payload)
        if status not in (200, 201) or not res.get("success"):
            print(f"❌ 创建 D1 数据库失败: {res}")
            return False
        db_id = res["result"]["uuid"]
        print(f"✅ D1 数据库创建成功！ID: {db_id}")

    # 3. 初始化纯净空表结构 (schema.sql)
    schema_path = os.path.join(root_dir, "cloudflare", "schema.sql")
    print(f"🛠️ 4. 执行 D1 纯净建表脚本 (不写入任何初始假数据): {schema_path}...")
    with open(schema_path, "r", encoding="utf-8") as f:
        schema_sql = f.read()

    status, res = make_request(
        f"{CF_API_BASE}/accounts/{account_id}/d1/database/{db_id}/query",
        method="POST",
        headers=headers,
        data=json.dumps({"sql": schema_sql}).encode("utf-8")
    )
    if status != 200 or not res.get("success"):
        print(f"❌ 初始化 D1 表结构失败: {res}")
        return False
    print("✅ 纯净数据库初始化成功（数据表就绪，0 业务数据）！")

    # 4. 打包前端静态资源
    build_static_assets(root_dir)

    # 5. 上传并发布 Worker (绑定 D1)
    worker_name = "car-tracker-api"
    worker_js_path = os.path.join(root_dir, "cloudflare", "worker.js")
    static_assets_path = os.path.join(root_dir, "cloudflare", "static_assets.js")
    print(f"⚡ 5. 正在发布 Worker 云函数 '{worker_name}' 并绑定 D1...")

    with open(worker_js_path, "r", encoding="utf-8") as f:
        worker_code = f.read()
    with open(static_assets_path, "r", encoding="utf-8") as f:
        static_code = f.read()

    metadata = {
        "main_module": "worker.js",
        "bindings": [
            {
                "type": "d1",
                "name": "DB",
                "id": db_id
            }
        ],
        "compatibility_date": "2024-09-01"
    }

    boundary = "----CloudflareWorkerBoundaryXYZ12345"
    body_parts = []
    # 模块元信息
    body_parts.append(f"--{boundary}\r\n".encode("utf-8"))
    body_parts.append(b'Content-Disposition: form-data; name="metadata"; filename="metadata.json"\r\n')
    body_parts.append(b'Content-Type: application/json\r\n\r\n')
    body_parts.append(json.dumps(metadata).encode("utf-8"))
    body_parts.append(b"\r\n")

    # 主脚本 worker.js
    body_parts.append(f"--{boundary}\r\n".encode("utf-8"))
    body_parts.append(b'Content-Disposition: form-data; name="worker.js"; filename="worker.js"\r\n')
    body_parts.append(b'Content-Type: application/javascript+module\r\n\r\n')
    body_parts.append(worker_code.encode("utf-8"))
    body_parts.append(b"\r\n")

    # 静态资源 static_assets.js
    body_parts.append(f"--{boundary}\r\n".encode("utf-8"))
    body_parts.append(b'Content-Disposition: form-data; name="static_assets.js"; filename="static_assets.js"\r\n')
    body_parts.append(b'Content-Type: application/javascript+module\r\n\r\n')
    body_parts.append(static_code.encode("utf-8"))
    body_parts.append(b"\r\n")

    body_parts.append(f"--{boundary}--\r\n".encode("utf-8"))
    multipart_body = b"".join(body_parts)

    upload_headers = {
        "Authorization": f"Bearer {api_token}",
        "Content-Type": f"multipart/form-data; boundary={boundary}"
    }

    status, res = make_request(
        f"{CF_API_BASE}/accounts/{account_id}/workers/scripts/{worker_name}",
        method="PUT",
        headers=upload_headers,
        data=multipart_body
    )
    if status not in (200, 201) or not res.get("success"):
        print(f"❌ 上传 Worker 失败: {res}")
        return False
    print("✅ Worker 脚本上传并绑定 D1 成功！")

    # 6. 激活 workers.dev 路由
    print("🌐 6. 正在激活 Worker 公网访问域名 (workers.dev)...")
    subdomain_headers = {"Authorization": f"Bearer {api_token}", "Content-Type": "application/json"}
    status, res = make_request(
        f"{CF_API_BASE}/accounts/{account_id}/workers/scripts/{worker_name}/subdomain",
        method="POST",
        headers=subdomain_headers,
        data=json.dumps({"enabled": True}).encode("utf-8")
    )
    
    # 获取用户前缀
    _, sub_res = make_request(f"{CF_API_BASE}/accounts/{account_id}/workers/subdomain", headers=subdomain_headers)
    subdomain = sub_res.get("result", {}).get("subdomain", "<你的子域名>")
    live_url = f"https://{worker_name}.{subdomain}.workers.dev"

    print("=" * 60)
    print("🎉 部署圆满成功！全新空库已就绪，公网即开即用！")
    print(f"🔗 线上访问网址: {live_url}")
    print("=" * 60)
    return True

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("使用说明: python3 deploy_cf.py <CF_ACCOUNT_ID> <CF_API_TOKEN>")
        sys.exit(1)
    deploy(sys.argv[1], sys.argv[2])
```

#### 运行方式：
```bash
python3 cloudflare/deploy_cf_api.py <ACCOUNT_ID> <API_TOKEN>
```
*仅需执行一条命令，10 秒内即可完成从静态资源打包、新建空数据库、执行建表到全球部署的全流程。*

---

### 6.3 传统 Wrangler CLI 部署方式

如果你偏好使用官方 Wrangler 工具链：

1. **安装并登录**：
   ```bash
   npm install -g wrangler
   wrangler login
   ```
2. **创建 D1 数据库**：
   ```bash
   wrangler d1 create car_tracker_d1
   ```
3. **初始化空表结构**：
   ```bash
   wrangler d1 execute car_tracker_d1 --remote --file=cloudflare/schema.sql
   ```
4. **编译静态资源并部署 Worker**：
   ```bash
   # 打包静态资源为 static_assets.js
   python3 -c "import deploy_cf_api; deploy_cf_api.build_static_assets('.')"
   # 发布到 Cloudflare Workers
   wrangler deploy
   ```

---

## 七、Vibe Coding 完整实操指令模板 (Prompt for AI)

> **💡 使用指南**：如果其他开发人员或 AI 智能体需要使用 Cursor、Claude 3.7、ChatGPT 或 Antigravity 从零复刻或二次开发本项目，可**完整复制以下框内的提示词（Prompt）**直接作为任务输入：

````markdown
你是一名全栈工程师与 Vibe Coding 专家。请为我构建一个名为《我的车管家》(Car Maintenance & Energy Manager) 的全动力类型私家车能耗与养车费用管理系统，并支持一键自动化部署至 Cloudflare Workers + D1。

【系统核心原则】
1. 初始数据库为空：严禁写入任何模拟数据、测试假数据或预置数据。用户初次使用必须是干净的“无车辆记录”状态。
2. 架构模式：采用单 Worker 架构。Worker 既作为 RESTful API 网关直连 Cloudflare D1 (绑定变量名严格为 DB)，又作为静态资产托管容器（将 HTML、JS、CSS 以模块形式内嵌到 static_assets.js），一个 URL 即可同时承载完整前端 PWA 与后端服务。
3. 离线优先：原生纯原生 JS (ES6+) + Tailwind CSS + IndexedDB 本地缓存 + Service Worker，支持离线记账和联网无缝双向增量同步。

【功能模块规格】
1. 车辆管理：支持燃油车(gas)、纯电(ev)、插电混动(phev)、增程式(erev)、油电混动(hybrid)。支持初始里程、初始油量%、初始电量%、油箱容积、电池度数。
2. 记账录入：
   - 油耗录入：加油量(L)、单价(元/L)、总价(元)输入任意两项自算第三项；支持是否加满、起止油量%、加油时剩余电量%。
   - 电耗录入：充电量(kWh)、单价(元/度)、总价(元)输入任意两项自算第三项；支持快慢充、起止SOC%、充电时剩余油量%。
   - 维保支出：保养、维修、保险、年检、停车、洗车等分类，支持下次保养里程/日期预警。
3. 能耗计算核心引擎：
   - 累计百公里能耗：无论油耗还是电耗，分母严格统一为车辆行驶总里程 (最新里程 - 提车初始里程)。
   - 等效电耗折算：油耗按 1L = 3.3 kWh 折合为电耗。
   - 区间能耗：相邻补能计算单次行程百公里能耗、每公里费用。
   - 历史最省/最高能耗：从每次行程百公里等效电耗中取极值。
4. 概览控制台 (Overview)：
   - 4 大 KPI 卡片（总里程、总能耗开支、每公里成本、待办维保）。
   - 按月费用统计堆叠柱状图：
     - 包含年份选择下拉框（自动提取账单年份及当前年，默认选中最新年份）。
     - 固定展开所选年份的 1月~12月，单柱多色堆叠（燃油=琥珀黄、电力=翡翠绿、其他=靛青蓝），金额大于0保底高度6px，柱顶显示保留两位小数的总金额，无消费月份仅显示底槽隐藏数值。悬浮展示明细。
   - 最近记录瀑布流（支持编辑与删除）。
5. 能耗分析面板 (Energy Analysis)：
   - 4 大指标卡（综合百公里电耗、综合百公里油耗、历史最省能耗、历史最高能耗）。
   - 每次补能百公里能耗分析柱状图：
     - 包含年份选择下拉框（支持具体年份或全部年份）。
     - 支持切换查看全部能耗、仅看油耗、仅看电耗、等效电耗。
     - 柱状图展示每次行程间能耗，底部标注增量里程(+X km)和起止跨度(X➔Y km)。
     - 选中具体年份时，图表起点为年度首程起点，基准虚线显示“年度均值”；选中全部年份时显示提车初始起点与“累计均值”。
   - 每次补能区间流水台账。

【代码与交付物】
1. 前端静态代码：`static/index.html`, `static/app.js`, `static/db.js`, `static/sw.js`, `static/manifest.json`。
2. 数据库结构：`cloudflare/schema.sql`（仅建表与索引，绝对无测试数据）。
3. 云端云函数：`cloudflare/worker.js`（处理 API + 静态资产直出）。
4. 零依赖部署脚本：`cloudflare/deploy_cf_api.py`（纯 Python 实现，接收 CF_ACCOUNT_ID 和 CF_API_TOKEN，全自动执行打包、D1建表、Worker发布）。

请根据以上全景需求，直接开始实现！
````
