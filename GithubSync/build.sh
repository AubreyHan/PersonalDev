#!/usr/bin/env bash
set -e

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$PROJECT_DIR"

echo "===> [1/4] 编译 Release 模式二进制文件..."
swift build -c release

BIN_PATH="$PROJECT_DIR/.build/release/GitHubSync"
if [ ! -f "$BIN_PATH" ]; then
    echo "错误: 未找到编译后的二进制文件: $BIN_PATH"
    exit 1
fi

DIST_DIR="$PROJECT_DIR/dist"
APP_NAME="GitHubSync.app"
APP_BUNDLE="$DIST_DIR/$APP_NAME"

echo "===> [2/4] 构建 macOS Application Bundle: $APP_BUNDLE..."
rm -rf "$APP_BUNDLE"
mkdir -p "$APP_BUNDLE/Contents/MacOS"
mkdir -p "$APP_BUNDLE/Contents/Resources"

# 复制二进制
cp "$BIN_PATH" "$APP_BUNDLE/Contents/MacOS/GitHubSync"
chmod +x "$APP_BUNDLE/Contents/MacOS/GitHubSync"

# 复制 Info.plist 与图标
cp "$PROJECT_DIR/Resources/Info.plist" "$APP_BUNDLE/Contents/Info.plist"
if [ -f "$PROJECT_DIR/Resources/AppIcon.icns" ]; then
    cp "$PROJECT_DIR/Resources/AppIcon.icns" "$APP_BUNDLE/Contents/Resources/AppIcon.icns"
fi

echo "===> [3/4] 执行 ad-hoc 代码签名..."
codesign --force --deep --sign - "$APP_BUNDLE"

echo "===> [4/4] 验证应用包结构..."
ls -la "$APP_BUNDLE/Contents"

echo ""
echo "✅ 应用构建完成！生成的应用位于:"
echo "   $APP_BUNDLE"
echo ""
echo "如需生成安装包 DMG，可运行:"
echo "   ./package_dmg.sh"
echo "如需直接运行测试，可执行:"
echo "   open \"$APP_BUNDLE\""
