#!/usr/bin/env bash
# ==============================================================================
# 我的车管家 · Cloudflare 一键部署脚本
# 支持自动创建 D1 数据库、导入表结构、部署 Worker 及 Pages 静态站点
# ==============================================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
cd "$SCRIPT_DIR"

# 颜色输出
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m'

echo "=========================================="
echo "🚗 我的车管家 · Cloudflare Serverless 一键部署助手"
echo "========================================================"
echo ""

# 1. 检查 Node.js / npx
if ! command -v npx &> /dev/null; then
    echo "❌ 错误: 未检测到 Node.js / npx 环境。"
    echo "请先安装 Node.js (https://nodejs.org/) 或使用网页控制台部署（详见 DEPLOY_CLOUDFLARE.md）。"
    exit 1
fi

echo "✅ 检测到 Node.js 环境: $(node -v)"
echo "✅ 检测到 npx 环境: $(npx -v)"
echo ""

# 2. 检查 Cloudflare 登录状态
echo "🔍 检查 Cloudflare 授权登录状态..."
if ! npx wrangler whoami &> /dev/null; then
    echo "⚠️ 尚未登录 Cloudflare，正在拉起浏览器登录授权..."
    npx wrangler login
else
    echo "✅ 已登录 Cloudflare 账号。"
fi
echo ""

# 3. 询问是否创建或使用现有 D1 数据库
DB_NAME="car_tracker_d1"
echo "📦 正在检查或创建 D1 数据库: $DB_NAME ..."

# 读取 wrangler.toml 查看是否已有 database_id
HAS_DB_ID=false
if grep -q 'database_id = "[a-zA-Z0-9-]\+"' wrangler.toml 2>/dev/null && ! grep -q 'YOUR_D1_DATABASE_UUID_HERE' wrangler.toml 2>/dev/null; then
    HAS_DB_ID=true
    echo "✅ wrangler.toml 中已配置有效的 D1 database_id。"
else
    echo "🚀 正在创建 Cloudflare D1 数据库 ($DB_NAME)..."
    CREATE_OUTPUT=$(npx wrangler d1 create "$DB_NAME" 2>&1 || true)
    
    # 提取 database_id
    DB_ID=$(echo "$CREATE_OUTPUT" | grep -oE 'database_id = "[a-zA-Z0-9-]+"' | cut -d'"' -f2 || true)
    
    if [ -n "$DB_ID" ]; then
        echo "✅ 成功创建 D1 数据库，ID: $DB_ID"
        # 写入 wrangler.toml
        sed -i.bak "s/database_id = \".*\"/database_id = \"$DB_ID\"/" wrangler.toml
        rm -f wrangler.toml.bak
    else
        echo "⚠️ 数据库可能已存在或需手动指定，尝试获取现有数据库列表..."
        npx wrangler d1 list
        echo "请确认 wrangler.toml 中的 database_id 正确后继续。"
    fi
fi
echo ""

# 4. 执行 D1 数据表初始化
echo "🛠️ 正在初始化/更新 D1 数据库表结构 (schema.sql)..."
npx wrangler d1 execute "$DB_NAME" --remote --file=schema.sql -y
echo "✅ 数据库表结构初始化就绪！"
echo ""

# 5. 部署 Worker 后端 API
echo "🚀 正在部署 Cloudflare Worker 后端 API..."
npx wrangler deploy
echo "✅ Worker 后端 API 部署成功！"
echo ""

# 6. 部署前端静态网站至 Cloudflare Pages
echo "🌐 正在部署前端静态站点至 Cloudflare Pages..."
PAGES_PROJECT="car-care-app"
npx wrangler pages deploy "$ROOT_DIR/static" --project-name="$PAGES_PROJECT"
echo "✅ 前端站点已成功部署至 Cloudflare Pages！"
echo ""

echo "========================================================"
echo "🎉 恭喜！我的车管家 Cloudflare 全套服务部署完成！"
echo "========================================================"
echo "📱 接下来只需 2 步："
echo "1. 在手机 Safari 打开刚刚生成的 Pages 网址；"
echo "2. 点击右上角 [☁️ 云端同步设置]，填入 Worker 域名并点击 [立即同步]；"
echo "3. 点击 Safari 分享按钮 [添加到主屏幕]，即可开启全屏无缝云同步体验！"
echo "========================================================"
