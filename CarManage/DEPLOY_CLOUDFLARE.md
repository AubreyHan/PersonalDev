# 🚀 我的车管家 · Cloudflare 完整部署与多端同步指南

> 💡 **开发者提示**：如需系统级全景开发需求说明书、纯净空库建表规范及一键自动化 Python 部署脚本，请查阅完整技术手册：[DEVELOPMENT_SPEC_AND_CF_DEPLOYMENT.md](DEVELOPMENT_SPEC_AND_CF_DEPLOYMENT.md)。

本项目采用 **Serverless 现代化云原生无服务器架构**，完全支持部署至 **Cloudflare 全球边缘网络**。
部署后具备：**0 服务器月租成本、全球 CDN 极速访问、自带免费 HTTPS 证书、D1 分布式 SQL 数据库持久化，以及 iPhone / iPad / Mac 多设备实时双向同步**。

---

## 🏗️ 架构拓扑概览

```
[ iPhone / iPad / Mac 设备 ] (Safari / Chrome / PWA 桌面端)
             │
             ├── 静态资源 (HTML / JS / CSS / 图标)
             │   └── 托管于: Cloudflare Pages (例如: https://my-car.pages.dev)
             │
             └── RESTful API 动态请求 & 数据双向同步
                 └── 运行于: Cloudflare Workers (例如: https://car-tracker-api.xxx.workers.dev)
                          │
                          └── 持久存储: Cloudflare D1 分布式 SQL 数据库 (表结构: schema.sql)
```

---

## 🌟 方案一：Cloudflare 网页控制台可视化部署（最推荐，零命令行）

无需安装 Node.js 或任何本地开发环境，仅需在浏览器控制台点选 3~5 分钟即可完成。

### 第一步：注册并登录 Cloudflare
1. 打开并登录 [Cloudflare 控制台](https://dash.cloudflare.com/)（若无账号可免费注册）。

---

### 第二步：创建 Cloudflare D1 数据库（存储车辆与记账明细）
1. 在控制台左侧侧边栏，点击 **Workers 和 Pages** ➔ **D1 SQL 数据库**；
2. 点击右上角 **创建数据库 (Create database)**；
3. 数据库名称填入：`car_tracker_d1`，点击 **创建**；
4. 创建成功后，点击进入该数据库详情页，切换到顶部 **控制台 (Console)** 标签页；
5. 打开本项目中的 [`cloudflare/schema.sql`](cloudflare/schema.sql) 文件，**复制全部 SQL 脚本**；
6. 将 SQL 粘贴到 Cloudflare D1 控制台的查询输入框中，点击右下角 **执行 (Execute)**；
   - 看到成功提示后，`vehicles`、`fuel_records`、`electricity_records`、`maintenance_records` 等数据表即初始化完毕！

---

### 第三步：创建 Cloudflare Worker 云函数（API 与能耗计算服务）
1. 在左侧菜单点击 **Workers 和 Pages** ➔ **概述 (Overview)**；
2. 点击 **创建 (Create)** ➔ 选择 **Worker**；
3. 为 Worker 设置名称（如：`car-tracker-api`），点击右下角 **部署 (Deploy)**；
4. 部署完成后，点击右上角 **编辑代码 (Edit code)** 进入在线代码编辑器；
5. 打开本项目中的 [`cloudflare/worker.js`](cloudflare/worker.js) 文件，**全选并复制全部 JavaScript 代码**；
6. 清空网页编辑器内原有的默认代码，将刚复制的内容**完整粘贴进去**；
7. 点击编辑器右上角的 **保存并部署 (Save and deploy)**。

---

### 第四步：将 D1 数据库绑定至 Worker
1. 返回该 Worker（`car-tracker-api`）的主管理界面；
2. 点击 **设置 (Settings)** 选项卡 ➔ 侧栏选择 **绑定 (Bindings)**（部分版本界面为 **变量与机密**）；
3. 滚动至 **D1 数据库绑定 (D1 Database Bindings)** 区域，点击 **添加绑定 (Add binding)**：
   - **变量名称 (Variable name)**：必须严格填为 `DB`（大写两字母，代码强依赖此变量名）；
   - **D1 数据库 (D1 database)**：下拉菜单选择第二步创建的 `car_tracker_d1`；
4. 点击 **保存并部署 (Save and deploy)**；
5. 复制保存您的 Worker 访问根域名，格式通常为：
   `https://car-tracker-api.<您的前缀>.workers.dev`

---

### 第五步：部署前端静态网站（通过 Cloudflare Pages 获得独立域名）
1. 在左侧菜单点击 **Workers 和 Pages** ➔ 点击 **创建 (Create)**；
2. 切换到 **Pages** 选项卡 ➔ 选择 **直接上传 (Upload assets)**；
3. 设置项目名称（例如：`car-care-app`），点击 **创建项目**；
4. 打开本地电脑的文件管理器，将本项目中的 [`static/`](static/) 文件夹**直接拖入上传区域**；
5. 点击 **部署站点 (Deploy site)**；
6. 约 5 秒后部署完成，Cloudflare 会分配一个永久可用的公网网址（例如：`https://car-care-app.pages.dev`）。

---

### 第六步：在 iPhone / iPad / Mac 上连接云端并添加到桌面
1. 用手机 Safari 浏览器或电脑打开第五步生成的网址（如 `https://car-care-app.pages.dev`）；
2. 点击页面右上角的 **“☁️ 云端未连接”**（或已同步）状态胶囊按钮；
3. 在弹出的「云端同步设置」对话框中：
   - 将第四步复制的 Worker API 网址（如 `https://car-tracker-api.xxxx.workers.dev`）粘贴到 **Cloudflare Worker 域名** 输入框中；
   - 点击 **保存配置**；
   - 点击 **立即同步**；
4. 此时，本地与云端 D1 数据库双向打通，数据立即上传并保持实时同步！
5. **添加到主屏幕（享受原生 App 体验）**：
   - 在 iPhone / iPad Safari 浏览器底栏点击 **“分享”图标（向上箭头的方框）**；
   - 下拉选择 **“添加到主屏幕”** ➔ 点击右上角 **“添加”**；
   - 桌面即刻生成沉浸式图标，全屏无网址栏运行，即便在地下车库等无网状态下亦可离线秒开和记录！

---

## 💻 方案二：Wrangler 命令行自动化部署（适合技术人员）

如果您的电脑已安装 Node.js（v16.17.0+）和 npm，可通过终端实现极速部署：

### 1. 安装 Wrangler 并登录
```bash
cd /Users/aubreyhan/Tools/git_repo/car_maintenance_app/cloudflare

# 登录 Cloudflare 账号 (会自动拉起浏览器授权)
npx wrangler login
```

### 2. 创建 D1 数据库
```bash
npx wrangler d1 create car_tracker_d1
```
> 执行后终端会输出类似于：
> `database_name = "car_tracker_d1"`
> `database_id = "xxxx-xxxx-xxxx-xxxx"`
> 请将该 `database_id` 填入 [`cloudflare/wrangler.toml`](cloudflare/wrangler.toml) 的 `database_id` 字段中。

### 3. 执行建表脚本
```bash
npx wrangler d1 execute car_tracker_d1 --remote --file=schema.sql
```

### 4. 部署后端 Worker API
```bash
npx wrangler deploy
```
> 终端将输出分配的 Worker 访问地址（例如 `https://car-care-tracker.xxx.workers.dev`）。

### 5. 部署前端静态网站至 Pages
```bash
npx wrangler pages deploy ../static --project-name=car-care-app
```

---

## 🛠️ 常见问题解答 (FAQ)

#### Q1: 为什么点击同步提示“数据库未配置”或 500 错误？
- **答**：请检查第三步/第四步中的 D1 绑定变量名称。必须严格为大写的 `DB`，不能写成 `db` 或 `d1`。

#### Q2: 手机离线记账后，联网会自动上传吗？
- **答**：会！应用内置离线优先（Offline First）队列。无信号时数据暂存本地 IndexedDB；一旦检测到网络恢复，后台静默将离线记录自动推送至 Cloudflare D1。

#### Q3: 现有本地 SQLite (`car_tracker.db`) 的数据如何同步到 Cloudflare？
- **答**：直接在已打开本地网页的浏览器中，在“云端同步设置”中填入 Worker 地址并点击“立即同步”，本地既有的所有车辆与历史记录就会全量推送到 Cloudflare D1。

#### Q4: Cloudflare 免费额度够用吗？
- **答**：非常充裕！Cloudflare Worker 每天提供 **100,000 次免费请求**，D1 数据库提供 **500 万次免费行读取/天** 和 **10 万次行写入/天**。个人记账或家庭多车用车场景仅消耗不到免费额度的 0.1%，真正永久 0 费用。
