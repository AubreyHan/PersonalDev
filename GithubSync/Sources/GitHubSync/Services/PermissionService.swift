import Foundation
import AppKit

public final class PermissionService {
    public static let shared = PermissionService()

    private init() {}

    /// 检测用户主目录读写权限
    public func checkDiskPermission() -> Bool {
        let testDir = FileManager.default.homeDirectoryForCurrentUser.appendingPathComponent(".github_sync_perm_test")
        do {
            try "test".write(to: testDir, atomically: true, encoding: .utf8)
            try FileManager.default.removeItem(at: testDir)
            return true
        } catch {
            return false
        }
    }

    /// 打开 macOS 系统偏好设置的“隐私与安全性 -> 完全磁盘访问权限”
    public func openPrivacySettings() {
        if let url = URL(string: "x-apple.systempreferences:com.apple.preference.security?Privacy_AllFiles") {
            NSWorkspace.shared.open(url)
        }
    }
}
