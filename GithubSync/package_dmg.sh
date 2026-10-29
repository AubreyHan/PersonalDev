#!/usr/bin/env bash
set -e

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$PROJECT_DIR"

APP_PATH="$PROJECT_DIR/dist/GitHubSync.app"
ICON_PATH="$PROJECT_DIR/Resources/AppIcon.icns"
VERSION="1.0.0"
DMG_NAME="GitHubSync-${VERSION}.dmg"
DMG_PATH="$PROJECT_DIR/dist/${DMG_NAME}"
VOL_NAME="GitHub Auto Sync"
STAGING_DIR="$PROJECT_DIR/dist/dmg_staging"

echo "===> [1/4] 检查 App 构建产物..."
if [ ! -d "$APP_PATH" ]; then
    echo "未找到应用包，正在执行一键构建..."
    ./build.sh
fi

echo "===> [2/4] 准备 DMG 制作暂存区..."
rm -rf "$STAGING_DIR"
rm -f "$DMG_PATH"
mkdir -p "$STAGING_DIR"

# 复制应用包
echo "正在复制 GitHubSync.app..."
cp -R "$APP_PATH" "$STAGING_DIR/"

# 创建 Applications 软链接
echo "创建 /Applications 快捷安装链接..."
ln -s /Applications "$STAGING_DIR/Applications"

# 设置 DMG 卷图标
if [ -f "$ICON_PATH" ]; then
    echo "注入卷宗图标 (.VolumeIcon.icns)..."
    cp "$ICON_PATH" "$STAGING_DIR/.VolumeIcon.icns"
    # 设置目录自定义图标属性 (Custom Icon Flag)
    SetFile -a C "$STAGING_DIR"
    # 隐藏 .VolumeIcon.icns 文件，避免在窗口中显示
    SetFile -a V "$STAGING_DIR/.VolumeIcon.icns"
fi

echo "===> [3/4] 使用 hdiutil 打包压缩生成 DMG..."
hdiutil create \
    -volname "$VOL_NAME" \
    -srcfolder "$STAGING_DIR" \
    -ov \
    -format UDZO \
    "$DMG_PATH"

rm -rf "$STAGING_DIR"

echo "===> [4/4] 为 DMG 文件本身赋予与 App 一致的高清图标..."
swift - <<EOF
import AppKit

let iconURL = URL(fileURLWithPath: "$ICON_PATH")
let targetPath = "$DMG_PATH"

if let icon = NSImage(contentsOf: iconURL) {
    let ok = NSWorkspace.shared.setIcon(icon, forFile: targetPath, options: [])
    if ok {
        print("✅ 成功为 DMG 文件设置自定义浅色 AppIcon 图标！")
    } else {
        print("⚠️ 设置文件图标返回 false")
    }
} else {
    print("⚠️ 无法加载图标文件: \(iconURL.path)")
}
EOF

echo ""
echo "=========================================================="
echo "🎉 安装包制作完成！"
echo "   文件路径: $DMG_PATH"
echo "   文件大小: $(du -sh "$DMG_PATH" | cut -f1)"
echo "   卷宗名称: $VOL_NAME"
echo "   图标状态: 与 App 一致的高清浅色图标已嵌入卷宗与文件本身"
echo "=========================================================="
