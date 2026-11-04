#!/bin/bash

# 获取当前脚本所在目录
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"
cd "$DIR"

PORT=8088

# 检查端口是否被占用，如被占用自动递增
while lsof -i :$PORT >/dev/null 2>&1; do
  echo "端口 $PORT 已被占用，尝试端口 $((PORT+1))..."
  PORT=$((PORT+1))
done

echo "================================================="
echo "   🚗 正在启动汽车维护与能耗记录服务 (Car Care)   "
echo "================================================="
echo "本地访问地址: http://127.0.0.1:$PORT"
echo "按 Ctrl+C 停止服务"
echo "================================================="

# 尝试自动在默认浏览器中打开页面
if [[ "$OSTYPE" == "darwin"* ]]; then
  (sleep 1 && open "http://127.0.0.1:$PORT") &
elif [[ "$OSTYPE" == "linux-gnu"* ]]; then
  (sleep 1 && xdg-open "http://127.0.0.1:$PORT" 2>/dev/null) &
fi

python3 server.py $PORT
