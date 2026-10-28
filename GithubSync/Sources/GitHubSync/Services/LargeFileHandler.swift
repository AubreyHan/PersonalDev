import Foundation

public final class LargeFileHandler {
    public static let shared = LargeFileHandler()
    public static let maxFileSize: UInt64 = 100 * 1024 * 1024 // 100MB 限制

    private init() {}

    /// 扫描待提交变更中的大文件 (>=100MB)，自动写入 .gitignore 并移出暂存区
    @discardableResult
    public func handleLargeFiles(at repoURL: URL, statusLines: [String]) async throws -> [String] {
        var detectedLargeFiles: [String] = []

        for line in statusLines {
            guard line.count >= 3 else { continue }
            // status porcelain 格式: XY PATH 或 XY "PATH" -> 提取路径
            let pathPart = String(line.dropFirst(3)).trimmingCharacters(in: .whitespaces)
            let cleanPath = pathPart.trimmingCharacters(in: CharacterSet(charactersIn: "\""))

            let fileURL = repoURL.appendingPathComponent(cleanPath)
            if let attrs = try? FileManager.default.attributesOfItem(atPath: fileURL.path),
               let size = attrs[.size] as? UInt64 {
                if size >= Self.maxFileSize {
                    detectedLargeFiles.append(cleanPath)
                }
            }
        }

        if !detectedLargeFiles.isEmpty {
            let gitignoreURL = repoURL.appendingPathComponent(".gitignore")
            var content = (try? String(contentsOf: gitignoreURL, encoding: .utf8)) ?? ""

            // 检查或追加 # large files 标签
            if !content.contains("# large files") {
                if !content.isEmpty && !content.hasSuffix("\n") {
                    content.append("\n")
                }
                content.append("\n# large files\n")
            }

            let lines = content.components(separatedBy: .newlines).map { $0.trimmingCharacters(in: .whitespaces) }
            for path in detectedLargeFiles {
                if !lines.contains(path) {
                    if !content.hasSuffix("\n") {
                        content.append("\n")
                    }
                    content.append("\(path)\n")
                }
            }

            try content.write(to: gitignoreURL, atomically: true, encoding: .utf8)

            // 从暂存区中剔除大文件
            for path in detectedLargeFiles {
                _ = try? await GitRunner.shared.run(args: ["rm", "--cached", path], at: repoURL)
            }
        }

        return detectedLargeFiles
    }
}
