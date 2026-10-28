import Foundation
import ServiceManagement

public final class AutoStartManager {
    public static let shared = AutoStartManager()

    private init() {}

    /// 获取当前开机自启状态
    public var isEnabled: Bool {
        if #available(macOS 13.0, *) {
            return SMAppService.mainApp.status == .enabled
        } else {
            return checkLegacyLaunchAgent()
        }
    }

    /// 设置开机自启
    public func setEnabled(_ enabled: Bool) -> Result<Void, Error> {
        if #available(macOS 13.0, *) {
            do {
                if enabled {
                    if SMAppService.mainApp.status != .enabled {
                        try SMAppService.mainApp.register()
                    }
                } else {
                    if SMAppService.mainApp.status == .enabled {
                        try SMAppService.mainApp.unregister()
                    }
                }
                return .success(())
            } catch {
                return .failure(error)
            }
        } else {
            return setLegacyLaunchAgent(enabled: enabled)
        }
    }

    // MARK: - Legacy LaunchAgent 降级支持
    private var launchAgentPlistURL: URL {
        let home = FileManager.default.homeDirectoryForCurrentUser
        return home.appendingPathComponent("Library/LaunchAgents/com.aubreyhan.GitHubSync.plist")
    }

    private func checkLegacyLaunchAgent() -> Bool {
        return FileManager.default.fileExists(atPath: launchAgentPlistURL.path)
    }

    private func setLegacyLaunchAgent(enabled: Bool) -> Result<Void, Error> {
        let fileURL = launchAgentPlistURL
        if enabled {
            let bundlePath = Bundle.main.bundlePath
            let executablePath = Bundle.main.executablePath ?? "\(bundlePath)/Contents/MacOS/GitHubSync"

            let plistContent = """
            <?xml version="1.0" encoding="UTF-8"?>
            <!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
            <plist version="1.0">
            <dict>
                <key>Label</key>
                <string>com.aubreyhan.GitHubSync</string>
                <key>ProgramArguments</key>
                <array>
                    <string>\(executablePath)</string>
                </array>
                <key>RunAtLoad</key>
                <true/>
                <key>KeepAlive</key>
                <false/>
            </dict>
            </plist>
            """
            do {
                let dir = fileURL.deletingLastPathComponent()
                try FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
                try plistContent.write(to: fileURL, atomically: true, encoding: .utf8)
                return .success(())
            } catch {
                return .failure(error)
            }
        } else {
            try? FileManager.default.removeItem(at: fileURL)
            return .success(())
        }
    }

    /// 彻底清理注销自启项
    public func disableCompletely() {
        if #available(macOS 13.0, *) {
            try? SMAppService.mainApp.unregister()
        }
        try? FileManager.default.removeItem(at: launchAgentPlistURL)
    }
}
