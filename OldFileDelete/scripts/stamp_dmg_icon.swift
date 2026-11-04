import Foundation
import AppKit

let repoRoot = URL(fileURLWithPath: #file)
    .deletingLastPathComponent()
    .deletingLastPathComponent()

let dmgDir = repoRoot.appendingPathComponent("src-tauri/target/release/bundle/dmg").path
let iconPath = repoRoot.appendingPathComponent("src-tauri/icons/icon.icns").path

guard let icon = NSImage(contentsOfFile: iconPath) else {
    print("Cannot find icon at \(iconPath)")
    exit(1)
}

let fm = FileManager.default
if let files = try? fm.contentsOfDirectory(atPath: dmgDir) {
    for f in files where f.hasSuffix(".dmg") {
        let fullPath = "\(dmgDir)/\(f)"
        let ok = NSWorkspace.shared.setIcon(icon, forFile: fullPath, options: [])
        print("Stamped icon onto \(f): \(ok)")
    }
}
