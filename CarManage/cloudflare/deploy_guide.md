# Cloudflare D1 数据库与 Worker 极速部署指南

本项目后端已经完全适配 **Cloudflare D1 分布式 SQL 数据库**、**Cloudflare Workers** 与 **Cloudflare Pages**。
您可以选择以下任一种方式将服务发布至云端，实现 **iPhone / iPad / Mac 永久多端实时同步**。

> 完整且图文并茂的部署文档请参阅根目录下的：[**`DEPLOY_CLOUDFLARE.md`**](../DEPLOY_CLOUDFLARE.md)

---

## 方式一：使用 Cloudflare 网页控制台部署 (最推荐，无需安装环境)

### 第一步：创建 Cloudflare D1 数据库
1. 登录 [Cloudflare 控制台](https://dash.cloudflare.com/)。
2. 在左侧侧边栏进入 **Workers 和 Pages** ➔ **D1 SQL 数据库**。
3. 点击 **创建数据库 (Create database)**：
   - 数据库名称填：`car_tracker_d1`。
   - 点击 **创建**。
4. 点击刚创建的数据库，进入 **控制台 (Console)** 标签页：
   - 打开项目目录下的 `cloudflare/schema.sql`，复制全部 SQL 内容；
   - 粘贴到 Cloudflare D1 的控制台输入框中，点击 **执行 (Execute)**，数据表即初始化完成！

### 第二步：创建 Cloudflare Worker 服务
1. 在左侧侧边栏进入 **Workers 和 Pages** ➔ **概述 (Overview)**。
2. 点击 **创建 (Create)** ➔ **创建 Worker (Create Worker)**：
   - 名称可设为：`car-tracker-api`；
   - 点击 **部署 (Deploy)**。
3. 部署后点击 **编辑代码 (Edit code)**：
   - 清空编辑器原有代码，将项目目录下 `cloudflare/worker.js` 的完整内容粘贴进去；
   - 点击右上角 **保存并部署 (Save and deploy)**。

### 第三步：将 D1 数据库绑定到 Worker
1. 返回该 Worker 的管理页面，进入 **设置 (Settings)** ➔ **绑定 (Bindings)**（或 **变量与机密**）。
2. 在 **D1 数据库绑定 (D1 Database Bindings)** 区域点击 **添加绑定 (Add binding)**：
   - **变量名称 (Variable name)**：必须填写 `DB`（大写两字母）；
   - **D1 数据库 (D1 database)**：选择第一步创建的 `car_tracker_d1`；
   - 点击 **保存并部署 (Save and deploy)**。
3. 记下您的 Worker 访问网址，形如：`https://car-tracker-api.<your-subdomain>.workers.dev`。

### 第四步：部署前端静态网站至 Cloudflare Pages
1. 在左侧进入 **Workers 和 Pages** ➔ **创建 (Create)** ➔ **Pages** ➔ **直接上传 (Upload assets)**。
2. 项目名称命名为 `car-care-app`。
3. 将项目中的 `static/` 文件夹整体拖入网页上传区域，点击 **部署站点 (Deploy site)**。
4. 获得永久访问网址（例如：`https://car-care-app.pages.dev`）。

---

## 方式二：使用自动化脚本或 Wrangler 命令行部署

### 快速一键脚本：
```bash
cd /Users/aubreyhan/Tools/git_repo/PersonalDev/CarManage
./cloudflare/deploy.sh
```

### 手动命令行分布执行：
```bash
cd /Users/aubreyhan/Tools/git_repo/PersonalDev/CarManage/cloudflare

# 1. 登录 Cloudflare 账号
npx wrangler login

# 2. 创建 D1 数据库
npx wrangler d1 create car_tracker_d1
# 复制命令行输出中的 database_id 填入 wrangler.toml 中的 database_id

# 3. 初始化数据表
npx wrangler d1 execute car_tracker_d1 --remote --file=schema.sql

# 4. 发布部署 Worker API
npx wrangler deploy

# 5. 部署前端静态站点
npx wrangler pages deploy ../static --project-name=car-care-app
```

---

## 如何在应用中使用 Cloudflare D1 云端同步？

无论您是本地打开，还是在 iPhone / iPad 上通过 Safari 添加到主屏幕：
1. 打开应用右上角的 **☁️ 云端同步设置**（点击同步状态胶囊）。
2. 在弹出窗口中，将您的 Cloudflare Worker 网址（如 `https://car-tracker-api.xxx.workers.dev`）填入并保存。
3. 点击 **立即同步**，全量数据即可秒级同步至 Cloudflare D1！
4. 随后在 iPhone、iPad、Mac 任意设备上均自动在后台静默双向同步，即使断网离线也能正常记录！
