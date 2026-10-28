import Foundation
import Combine

public enum LogLevel: String, Codable, CaseIterable, Sendable {
    case info = "INFO"
    case sync = "SYNC"
    case warn = "WARN"
    case error = "ERROR"

    public var displayIcon: String {
        switch self {
        case .info: return "info.circle"
        case .sync: return "arrow.triangle.2.circlepath"
        case .warn: return "exclamationmark.triangle"
        case .error: return "xmark.octagon"
        }
    }
}

public struct LogEntry: Identifiable, Sendable {
    public let id = UUID()
    public let timestamp: Date
    public let level: LogLevel
    public let tag: String
    public let message: String

    public var formattedText: String {
        let df = AppLogger.dateFormatter.string(from: timestamp)
        return "[\(df)] [\(level.rawValue)] [\(tag)] \(message)"
    }
}

public final class AppLogger: @unchecked Sendable {
    public static let shared = AppLogger()

    public static let maxFileSize: UInt64 = 1024 * 1024 // 1MB 上限
    public static let pruneTargetSize: UInt64 = 700 * 1024 // 触发 FIFO 缩减后的目标大小 (~700KB)

    fileprivate static let dateFormatter: DateFormatter = {
        let df = DateFormatter()
        df.dateFormat = "yyyy-MM-dd HH:mm:ss"
        return df
    }()

    public let logFileURL: URL
    private let fileManager = FileManager.default
    private let queue = DispatchQueue(label: "com.aubreyhan.GitHubSync.logger", qos: .utility)

    // 提供给 UI 的最近日志发布者
    public let newLogPublisher = PassthroughSubject<LogEntry, Never>()

    private init() {
        self.logFileURL = Self.determineLogFileURL()

        // 启动时确保存档大小不超过 1MB
        queue.async {
            self.enforceSizeLimit()
        }
    }

    /// 定位日志文件存储路径：优先存放在 app Bundle 内部的 Contents/Logs 下，随 App 删除时一同彻底删除
    private static func determineLogFileURL() -> URL {
        let fm = FileManager.default
        let bundleURL = Bundle.main.bundleURL

        // 1. 若当前运行在标准 .app 目录下
        if bundleURL.pathExtension == "app" {
            let appLogsDir = bundleURL.appendingPathComponent("Contents/Logs", isDirectory: true)
            do {
                try fm.createDirectory(at: appLogsDir, withIntermediateDirectories: true)
                let testFile = appLogsDir.appendingPathComponent(".perm_test")
                try "ok".write(to: testFile, atomically: true, encoding: .utf8)
                try fm.removeItem(at: testFile)
                return appLogsDir.appendingPathComponent("app.log")
            } catch {
                // 若该目录只读（极罕见），降级至应用支持目录
            }
        } else {
            // 2. 检查当前可执行文件同级目录是否存在 GitHubSync.app
            let distAppLogs = bundleURL.deletingLastPathComponent().appendingPathComponent("GitHubSync.app/Contents/Logs", isDirectory: true)
            if (try? fm.createDirectory(at: distAppLogs, withIntermediateDirectories: true)) != nil {
                return distAppLogs.appendingPathComponent("app.log")
            }

            // 3. 命令行本地调试开发模式：保存在同级 Logs 目录
            let devLogsDir = bundleURL.deletingLastPathComponent().appendingPathComponent("Logs", isDirectory: true)
            if (try? fm.createDirectory(at: devLogsDir, withIntermediateDirectories: true)) != nil {
                return devLogsDir.appendingPathComponent("app.log")
            }
        }

        // 兜底方案
        let baseDir = fm.urls(for: .applicationSupportDirectory, in: .userDomainMask).first!
        let fallbackDir = baseDir.appendingPathComponent("com.aubreyhan.GitHubSync/Logs", isDirectory: true)
        try? fm.createDirectory(at: fallbackDir, withIntermediateDirectories: true)
        return fallbackDir.appendingPathComponent("app.log")
    }

    // MARK: - 写入日志接口
    public func info(_ message: String, tag: String = "App") {
        log(level: .info, tag: tag, message: message)
    }

    public func sync(_ message: String, tag: String = "Sync") {
        log(level: .sync, tag: tag, message: message)
    }

    public func warn(_ message: String, tag: String = "Warn") {
        log(level: .warn, tag: tag, message: message)
    }

    public func error(_ message: String, tag: String = "Error") {
        log(level: .error, tag: tag, message: message)
    }

    private func log(level: LogLevel, tag: String, message: String) {
        let entry = LogEntry(timestamp: Date(), level: level, tag: tag, message: message)
        let line = entry.formattedText + "\n"

        queue.async {
            self.appendLine(line)
            self.newLogPublisher.send(entry)
        }
    }

    // MARK: - FIFO 写入与容量限制管理 (保持在 1MB 以下)
    private func appendLine(_ line: String) {
        guard let data = line.data(using: .utf8) else { return }

        // 检查写入后是否会超出 1MB 容量，若超出先执行 FIFO 修剪
        let currentSize = (try? fileManager.attributesOfItem(atPath: logFileURL.path)[.size] as? UInt64) ?? 0
        if currentSize + UInt64(data.count) >= Self.maxFileSize {
            pruneFIFO()
        }

        if !fileManager.fileExists(atPath: logFileURL.path) {
            try? data.write(to: logFileURL, options: .atomic)
        } else if let handle = try? FileHandle(forWritingTo: logFileURL) {
            defer { try? handle.close() }
            _ = try? handle.seekToEnd()
            try? handle.write(contentsOf: data)
        }
    }

    /// FIFO 修剪：丢弃头部最旧的日志行，保留最新的约 700KB 内容，确保总大小始终严格在 1MB 以下
    private func pruneFIFO() {
        guard let fullData = try? Data(contentsOf: logFileURL),
              fullData.count > Int(Self.pruneTargetSize) else {
            return
        }

        let bytesToKeep = Int(Self.pruneTargetSize)
        let startIndex = fullData.count - bytesToKeep
        var subData = fullData.subdata(in: startIndex..<fullData.count)

        // 寻找换行符 \n，对齐到完整的行首，避免首行被截断为残缺字符
        if let newlineIndex = subData.firstIndex(of: UInt8(ascii: "\n")) {
            let alignedStart = newlineIndex + 1
            if alignedStart < subData.count {
                subData = subData.subdata(in: alignedStart..<subData.count)
            }
        }

        let header = "[SYSTEM] --- 触发 FIFO 自动清理（保持容量在 1MB 以内），旧日志已淘汰 ---\n".data(using: .utf8) ?? Data()
        var finalData = Data()
        finalData.append(header)
        finalData.append(subData)

        try? finalData.write(to: logFileURL, options: .atomic)
    }

    private func enforceSizeLimit() {
        let currentSize = (try? fileManager.attributesOfItem(atPath: logFileURL.path)[.size] as? UInt64) ?? 0
        if currentSize >= Self.maxFileSize {
            pruneFIFO()
        }
    }

    // MARK: - 读取与清空
    public func readAllLogs() -> String {
        return (try? String(contentsOf: logFileURL, encoding: .utf8)) ?? ""
    }

    public func getCurrentFileSize() -> UInt64 {
        return (try? fileManager.attributesOfItem(atPath: logFileURL.path)[.size] as? UInt64) ?? 0
    }

    public func clear() {
        queue.async {
            try? "".write(to: self.logFileURL, atomically: true, encoding: .utf8)
            self.info("日志已手动清空", tag: "System")
        }
    }
}
