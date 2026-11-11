import AppKit
import CoreGraphics
import Foundation

func renderIcon(size: Int) -> NSImage {
    let cgSize = CGSize(width: size, height: size)
    let image = NSImage(size: cgSize)
    image.lockFocus()
    guard let ctx = NSGraphicsContext.current?.cgContext else {
        image.unlockFocus()
        return image
    }

    let scale = CGFloat(size) / 1024.0
    ctx.saveGState()
    ctx.scaleBy(x: scale, y: scale)

    // 1. macOS squircle background with drop shadow
    let iconRect = CGRect(x: 100, y: 100, width: 824, height: 824)
    let cornerRadius: CGFloat = 185.0
    let squirclePath = CGPath(roundedRect: iconRect, cornerWidth: cornerRadius, cornerHeight: cornerRadius, transform: nil)

    ctx.saveGState()
    ctx.setShadow(offset: CGSize(width: 0, height: -20), blur: 36, color: NSColor(calibratedWhite: 0, alpha: 0.45).cgColor)
    ctx.addPath(squirclePath)
    ctx.setFillColor(NSColor(calibratedRed: 0.05, green: 0.10, blue: 0.18, alpha: 1.0).cgColor)
    ctx.fillPath()
    ctx.restoreGState()

    // Clip to squircle
    ctx.saveGState()
    ctx.addPath(squirclePath)
    ctx.clip()

    // Deep Garmin sport navy-blue gradient
    let colorSpace = CGColorSpaceCreateDeviceRGB()
    let bgColors = [
        NSColor(calibratedRed: 0.04, green: 0.09, blue: 0.18, alpha: 1.0).cgColor,
        NSColor(calibratedRed: 0.06, green: 0.18, blue: 0.32, alpha: 1.0).cgColor,
        NSColor(calibratedRed: 0.03, green: 0.28, blue: 0.44, alpha: 1.0).cgColor
    ] as CFArray
    let bgLocations: [CGFloat] = [0.0, 0.55, 1.0]
    if let bgGradient = CGGradient(colorsSpace: colorSpace, colors: bgColors, locations: bgLocations) {
        ctx.drawLinearGradient(
            bgGradient,
            start: CGPoint(x: 150, y: 900),
            end: CGPoint(x: 880, y: 120),
            options: []
        )
    }

    // Subtle radial glow in center-top
    let glowColors = [
        NSColor(calibratedRed: 0.0, green: 0.78, blue: 1.0, alpha: 0.25).cgColor,
        NSColor(calibratedRed: 0.0, green: 0.78, blue: 1.0, alpha: 0.0).cgColor
    ] as CFArray
    if let glowGrad = CGGradient(colorsSpace: colorSpace, colors: glowColors, locations: [0.0, 1.0]) {
        ctx.drawRadialGradient(
            glowGrad,
            startCenter: CGPoint(x: 512, y: 600),
            startRadius: 0,
            endCenter: CGPoint(x: 512, y: 600),
            endRadius: 420,
            options: [.drawsBeforeStartLocation]
        )
    }

    // 2. Subtle topographic / radar rings
    ctx.setStrokeColor(NSColor(calibratedRed: 0.20, green: 0.65, blue: 0.90, alpha: 0.10).cgColor)
    ctx.setLineWidth(3.0)
    for r in stride(from: 140.0, through: 440.0, by: 75.0) {
        let ringRect = CGRect(x: 512 - r, y: 540 - r, width: r * 2, height: r * 2)
        ctx.strokeEllipse(in: ringRect)
    }

    // 3. GPS Activity Route Path (winding athletic track from bottom-left to top-right)
    let routePath = CGMutablePath()
    let p0 = CGPoint(x: 245, y: 400)
    let p2 = CGPoint(x: 560, y: 480)
    let p3 = CGPoint(x: 755, y: 715)

    routePath.move(to: p0)
    routePath.addCurve(
        to: p2,
        control1: CGPoint(x: 295, y: 660),
        control2: CGPoint(x: 455, y: 660)
    )
    routePath.addCurve(
        to: p3,
        control1: CGPoint(x: 645, y: 340),
        control2: CGPoint(x: 710, y: 560)
    )

    // Outer glow of GPS track
    ctx.saveGState()
    ctx.setShadow(offset: .zero, blur: 28, color: NSColor(calibratedRed: 0.0, green: 0.90, blue: 0.75, alpha: 0.75).cgColor)
    ctx.addPath(routePath)
    ctx.setStrokeColor(NSColor(calibratedRed: 0.0, green: 0.88, blue: 0.85, alpha: 0.45).cgColor)
    ctx.setLineWidth(38)
    ctx.setLineCap(.round)
    ctx.setLineJoin(.round)
    ctx.strokePath()
    ctx.restoreGState()

    // Gradient stroke along the GPS route
    ctx.saveGState()
    ctx.addPath(routePath)
    ctx.setLineWidth(24)
    ctx.setLineCap(.round)
    ctx.setLineJoin(.round)
    ctx.replacePathWithStrokedPath()
    ctx.clip()

    let routeColors = [
        NSColor(calibratedRed: 0.0, green: 0.68, blue: 1.0, alpha: 1.0).cgColor,
        NSColor(calibratedRed: 0.0, green: 0.95, blue: 0.65, alpha: 1.0).cgColor
    ] as CFArray
    if let routeGrad = CGGradient(colorsSpace: colorSpace, colors: routeColors, locations: [0.0, 1.0]) {
        ctx.drawLinearGradient(routeGrad, start: p0, end: p3, options: [])
    }
    ctx.restoreGState()

    // Start & End GPS nodes
    for (idx, pt) in [p0, p3].enumerated() {
        let nodeRadius: CGFloat = idx == 0 ? 18 : 24
        ctx.saveGState()
        ctx.setFillColor(NSColor.white.cgColor)
        ctx.fillEllipse(in: CGRect(x: pt.x - nodeRadius, y: pt.y - nodeRadius, width: nodeRadius * 2, height: nodeRadius * 2))
        let innerR = nodeRadius * 0.55
        ctx.setFillColor(
            idx == 0
                ? NSColor(calibratedRed: 0.0, green: 0.68, blue: 1.0, alpha: 1.0).cgColor
                : NSColor(calibratedRed: 0.0, green: 0.90, blue: 0.60, alpha: 1.0).cgColor
        )
        ctx.fillEllipse(in: CGRect(x: pt.x - innerR, y: pt.y - innerR, width: innerR * 2, height: innerR * 2))
        ctx.restoreGState()
    }

    // 4. Garmin-style triangle delta emblem at top-left
    let triPath = CGMutablePath()
    triPath.move(to: CGPoint(x: 275, y: 785))
    triPath.addLine(to: CGPoint(x: 225, y: 700))
    triPath.addLine(to: CGPoint(x: 325, y: 700))
    triPath.closeSubpath()
    ctx.setFillColor(NSColor(calibratedRed: 0.0, green: 0.78, blue: 1.0, alpha: 0.92).cgColor)
    ctx.addPath(triPath)
    ctx.fillPath()

    // 5. Prominent Download Badge Circle + Arrow at lower center
    let badgeCenter = CGPoint(x: 512, y: 315)
    let badgeRadius: CGFloat = 135

    ctx.saveGState()
    ctx.setShadow(offset: CGSize(width: 0, height: -10), blur: 24, color: NSColor(calibratedWhite: 0, alpha: 0.45).cgColor)
    let badgeRect = CGRect(
        x: badgeCenter.x - badgeRadius,
        y: badgeCenter.y - badgeRadius,
        width: badgeRadius * 2,
        height: badgeRadius * 2
    )
    ctx.setFillColor(NSColor(calibratedRed: 0.05, green: 0.14, blue: 0.25, alpha: 0.96).cgColor)
    ctx.fillEllipse(in: badgeRect)
    ctx.restoreGState()

    // Glowing ring around download badge
    ctx.setStrokeColor(NSColor(calibratedRed: 0.0, green: 0.85, blue: 0.95, alpha: 0.85).cgColor)
    ctx.setLineWidth(8)
    ctx.strokeEllipse(in: badgeRect)

    // Crisp Download Arrow inside badge
    ctx.saveGState()
    ctx.setStrokeColor(NSColor.white.cgColor)
    ctx.setLineWidth(24)
    ctx.setLineCap(.round)
    ctx.setLineJoin(.round)

    // Vertical shaft
    ctx.move(to: CGPoint(x: badgeCenter.x, y: badgeCenter.y + 62))
    ctx.addLine(to: CGPoint(x: badgeCenter.x, y: badgeCenter.y - 28))
    // Arrowhead
    ctx.move(to: CGPoint(x: badgeCenter.x - 46, y: badgeCenter.y + 12))
    ctx.addLine(to: CGPoint(x: badgeCenter.x, y: badgeCenter.y - 34))
    ctx.addLine(to: CGPoint(x: badgeCenter.x + 46, y: badgeCenter.y + 12))
    // Bottom tray bar
    ctx.move(to: CGPoint(x: badgeCenter.x - 54, y: badgeCenter.y - 64))
    ctx.addLine(to: CGPoint(x: badgeCenter.x + 54, y: badgeCenter.y - 64))
    ctx.strokePath()
    ctx.restoreGState()

    // 6. Inner subtle glass border on squircle
    ctx.addPath(squirclePath)
    ctx.setStrokeColor(NSColor(calibratedWhite: 1.0, alpha: 0.18).cgColor)
    ctx.setLineWidth(6)
    ctx.strokePath()

    ctx.restoreGState()
    ctx.restoreGState()
    image.unlockFocus()
    return image
}

func savePNG(_ image: NSImage, to url: URL, pixelSize: Int) throws {
    guard let rep = NSBitmapImageRep(
        bitmapDataPlanes: nil,
        pixelsWide: pixelSize,
        pixelsHigh: pixelSize,
        bitsPerSample: 8,
        samplesPerPixel: 4,
        hasAlpha: true,
        isPlanar: false,
        colorSpaceName: .deviceRGB,
        bytesPerRow: 0,
        bitsPerPixel: 0
    ) else {
        throw NSError(domain: "IconGen", code: 1)
    }
    rep.size = NSSize(width: pixelSize, height: pixelSize)
    NSGraphicsContext.saveGraphicsState()
    NSGraphicsContext.current = NSGraphicsContext(bitmapImageRep: rep)
    image.draw(in: NSRect(x: 0, y: 0, width: pixelSize, height: pixelSize))
    NSGraphicsContext.restoreGraphicsState()

    guard let pngData = rep.representation(using: .png, properties: [:]) else {
        throw NSError(domain: "IconGen", code: 2)
    }
    try pngData.write(to: url)
}

let outputDir = URL(fileURLWithPath: CommandLine.arguments.count > 1 ? CommandLine.arguments[1] : "./AppIcon.iconset")
try FileManager.default.createDirectory(at: outputDir, withIntermediateDirectories: true)

let specs: [(String, Int)] = [
    ("icon_16x16.png", 16),
    ("icon_16x16@2x.png", 32),
    ("icon_32x32.png", 32),
    ("icon_32x32@2x.png", 64),
    ("icon_128x128.png", 128),
    ("icon_128x128@2x.png", 256),
    ("icon_256x256.png", 256),
    ("icon_256x256@2x.png", 512),
    ("icon_512x512.png", 512),
    ("icon_512x512@2x.png", 1024)
]

let masterImage = renderIcon(size: 1024)
for (name, px) in specs {
    let fileURL = outputDir.appendingPathComponent(name)
    try savePNG(masterImage, to: fileURL, pixelSize: px)
}
print("Generated iconset at \(outputDir.path)")
