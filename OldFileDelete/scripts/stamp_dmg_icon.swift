import Foundation
import AppKit

let repoRoot = URL(fileURLWithPath: #file)
    .deletingLastPathComponent()
    .deletingLastPathComponent()

let dmgDir = repoRoot.appendingPathComponent("src-tauri/target/release/bundle/dmg").path
let macosDir = repoRoot.appendingPathComponent("src-tauri/target/release/bundle/macos").path
let releaseDir = repoRoot.appendingPathComponent("release").path
let iconPath = repoRoot.appendingPathComponent("src-tauri/icons/icon.icns").path

guard let icon = NSImage(contentsOfFile: iconPath) else {
    print("Cannot find icon at \(iconPath)")
    exit(1)
}

let fm = FileManager.default

// Ensure release directory exists
try? fm.createDirectory(atPath: releaseDir, withIntermediateDirectories: true, attributes: nil)

// Stamp icon on original DMGs
if let files = try? fm.contentsOfDirectory(atPath: dmgDir) {
    for f in files where f.hasSuffix(".dmg") {
        let fullPath = "\(dmgDir)/\(f)"
        let ok = NSWorkspace.shared.setIcon(icon, forFile: fullPath, options: [])
        print("Stamped icon onto \(f): \(ok)")
        
        // Copy DMG to release directory
        let destDmg = "\(releaseDir)/\(f)"
        try? fm.removeItem(atPath: destDmg)
        do {
            try fm.copyItem(atPath: fullPath, toPath: destDmg)
            let destOk = NSWorkspace.shared.setIcon(icon, forFile: destDmg, options: [])
            print("Copied \(f) to release/ (stamped: \(destOk))")
        } catch {
            print("Failed to copy \(f) to release: \(error)")
        }
    }
}

// Copy FilePurger.app to release directory
let srcApp = "\(macosDir)/FilePurger.app"
let destApp = "\(releaseDir)/FilePurger.app"
if fm.fileExists(atPath: srcApp) {
    let p = Process()
    p.executableURL = URL(fileURLWithPath: "/usr/bin/ditto")
    p.arguments = [srcApp, destApp]
    try? p.run()
    p.waitUntilExit()
    
    // Ad-hoc sign for local execution
    let cs = Process()
    cs.executableURL = URL(fileURLWithPath: "/usr/bin/codesign")
    cs.arguments = ["--force", "--deep", "-s", "-", destApp]
    try? cs.run()
    cs.waitUntilExit()
    
    print("Copied and ad-hoc signed FilePurger.app in release/")
}

