import Foundation

/// 磁盘字节数人性化格式化工具
public enum ByteFormatter {
    /// 将字节数格式化为人类可读字符串（如 "24.5 MB"、"1.86 GB"）
    public static func string(fromByteCount byteCount: Int64) -> String {
        let clamped = max(0, byteCount)
        let formatter = ByteCountFormatter()
        formatter.allowedUnits = [.useBytes, .useKB, .useMB, .useGB, .useTB]
        formatter.countStyle = .file
        formatter.includesUnit = true
        formatter.isAdaptive = true
        return formatter.string(fromByteCount: clamped)
    }

    /// `string(fromByteCount:)` 的同义便捷方法
    public static func format(_ byteCount: Int64) -> String {
        string(fromByteCount: byteCount)
    }

    /// 可选字节数格式化，若为 nil 则返回占位符
    public static func format(_ byteCount: Int64?, placeholder: String = "—") -> String {
        guard let byteCount else { return placeholder }
        return string(fromByteCount: byteCount)
    }

    /// 紧凑型体积标签（适合缩略图角标，如 "28 MB"）
    public static func compactString(fromByteCount byteCount: Int64) -> String {
        let clamped = max(0, byteCount)
        let formatter = ByteCountFormatter()
        formatter.allowedUnits = [.useMB, .useGB]
        formatter.countStyle = .file
        formatter.zeroPadsFractionDigits = false
        return formatter.string(fromByteCount: clamped)
    }
}
