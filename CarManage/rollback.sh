#!/bin/bash
# ==============================================================================
# 我的车管家 (Car Care Tracker) 版本安全回滚与状态恢复脚本
# 基准稳定版 Tag: v1.0-stable-before-phase1 (Branch: backup/v1-before-phase1)
# ==============================================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

BACKUP_TAG="v1.0-stable-before-phase1"
BACKUP_BRANCH="backup/v1-before-phase1"

case "${1:-status}" in
  status)
    echo "=================================================="
    echo "🛡️  我的车管家 · 版本保护与回滚中心"
    echo "=================================================="
    echo "📌 当前 HEAD 提交: $(git log -1 --oneline)"
    echo "🔒 基准稳定版备份: $BACKUP_TAG ($(git rev-parse --short "$BACKUP_TAG" 2>/dev/null || echo '未找到'))"
    echo ""
    echo "📊 距基准稳定版变更统计:"
    git diff --stat "$BACKUP_TAG" || true
    echo ""
    echo "💡 如需一键无损回滚至优化前版本，请执行:"
    echo "   ./rollback.sh restore"
    ;;
  restore)
    echo "⚠️  正在将代码库无损回滚至第一阶段优化前稳定版本 ($BACKUP_TAG)..."
    git checkout "$BACKUP_TAG" -- .
    # 清理新增的模块化目录（如有）
    rm -rf static/modules
    echo "✅ 已成功回滚至 $BACKUP_TAG 稳定版本！"
    echo "🧪 正在运行自动化测试验证..."
    python3 -m unittest discover -v
    ;;
  *)
    echo "用法: ./rollback.sh [status|restore]"
    exit 1
    ;;
esac
