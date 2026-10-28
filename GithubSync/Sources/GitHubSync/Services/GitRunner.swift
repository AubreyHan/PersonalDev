import Foundation

public struct GitResult {
    public let exitCode: Int32
    public let stdout: String
    public let stderr: String

    public var isSuccess: Bool {
        return exitCode == 0
    }
}

public enum GitError: LocalizedError {
    case commandFailed(command: String, exitCode: Int32, stderr: String)
    case custom(String)

    public var errorDescription: String? {
        switch self {
        case .commandFailed(let command, let exitCode, let stderr):
            let msg = stderr.trimmingCharacters(in: .whitespacesAndNewlines)
            return "Git命令执行失败 (\(command), code \(exitCode)): \(msg)"
        case .custom(let str):
            return str
        }
    }
}

public final class GitRunner: @unchecked Sendable {
    public static let shared = GitRunner()

    private let gitPath: String

    private init() {
        if FileManager.default.isExecutableFile(atPath: "/usr/bin/git") {
            self.gitPath = "/usr/bin/git"
        } else if FileManager.default.isExecutableFile(atPath: "/usr/local/bin/git") {
            self.gitPath = "/usr/local/bin/git"
        } else if FileManager.default.isExecutableFile(atPath: "/opt/homebrew/bin/git") {
            self.gitPath = "/opt/homebrew/bin/git"
        } else {
            self.gitPath = "git"
        }
    }

    public func run(args: [String], at directory: URL) async throws -> String {
        let result = try await execute(args: args, at: directory)
        if !result.isSuccess {
            let cmdStr = args.joined(separator: " ")
            AppLogger.shared.error("Git 执行失败 [\(cmdStr)]: \(result.stderr.trimmingCharacters(in: .whitespacesAndNewlines))", tag: "Git")
            throw GitError.commandFailed(command: cmdStr, exitCode: result.exitCode, stderr: result.stderr)
        }
        return result.stdout
    }

    public func execute(args: [String], at directory: URL) async throws -> GitResult {
        return try await withCheckedThrowingContinuation { continuation in
            DispatchQueue.global(qos: .userInitiated).async {
                let process = Process()
                process.executableURL = URL(fileURLWithPath: self.gitPath)
                process.arguments = args
                process.currentDirectoryURL = directory

                // 继承并扩充系统环境变量，确保 ssh / 钥匙串可用
                var env = ProcessInfo.processInfo.environment
                let extraPaths = "/opt/homebrew/bin:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin"
                if let currentPath = env["PATH"] {
                    env["PATH"] = "\(extraPaths):\(currentPath)"
                } else {
                    env["PATH"] = extraPaths
                }
                process.environment = env

                let stdoutPipe = Pipe()
                let stderrPipe = Pipe()
                process.standardOutput = stdoutPipe
                process.standardError = stderrPipe

                do {
                    try process.run()
                    process.waitUntilExit()

                    let stdoutData = stdoutPipe.fileHandleForReading.readDataToEndOfFile()
                    let stderrData = stderrPipe.fileHandleForReading.readDataToEndOfFile()

                    let stdoutStr = String(data: stdoutData, encoding: .utf8) ?? ""
                    let stderrStr = String(data: stderrData, encoding: .utf8) ?? ""

                    let res = GitResult(exitCode: process.terminationStatus, stdout: stdoutStr, stderr: stderrStr)
                    continuation.resume(returning: res)
                } catch {
                    continuation.resume(throwing: error)
                }
            }
        }
    }

    // MARK: - 快捷 Git 操作

    /// 获取当前分支
    public func getCurrentBranch(at directory: URL) async -> String {
        do {
            let branch = try await run(args: ["rev-parse", "--abbrev-ref", "HEAD"], at: directory)
            let trimmed = branch.trimmingCharacters(in: .whitespacesAndNewlines)
            return trimmed.isEmpty ? "main" : trimmed
        } catch {
            return "unknown"
        }
    }

    /// 获取未提交的文件改动列表 (porcelain 格式)
    public func getStatusPorcelain(at directory: URL) async throws -> [String] {
        let output = try await run(args: ["status", "--porcelain"], at: directory)
        return output.components(separatedBy: .newlines).filter { !$0.trimmingCharacters(in: .whitespaces).isEmpty }
    }

    /// 获取未提交的文件数量
    public func getUncommittedChangesCount(at directory: URL) async -> Int {
        do {
            let list = try await getStatusPorcelain(at: directory)
            return list.count
        } catch {
            return 0
        }
    }

    /// 校验仓库是否具有远程追踪分支
    public func hasUpstreamBranch(at directory: URL) async -> Bool {
        do {
            let res = try await run(args: ["rev-parse", "--abbrev-ref", "@{u}"], at: directory)
            return !res.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty
        } catch {
            return false
        }
    }

    /// 执行前置健康预检 (Pre-flight check)
    public func runPreflightCheck(at directory: URL) async throws {
        // 1. 检查是否存在远程源 remote origin
        let remoteOutput: String
        do {
            remoteOutput = try await run(args: ["remote", "get-url", "origin"], at: directory)
        } catch {
            throw GitError.custom("该仓库未配置远程源 (Remote origin 不存在)，无法开启自动同步")
        }

        if remoteOutput.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty {
            throw GitError.custom("该仓库未配置远程源 (Remote origin 不存在)，无法开启自动同步")
        }

        // 2. 检查是否处于冲突中断状态 (例如 rebase-apply, MERGE_HEAD)
        let gitDir = directory.appendingPathComponent(".git")
        let mergeHead = gitDir.appendingPathComponent("MERGE_HEAD")
        let rebaseApply = gitDir.appendingPathComponent("rebase-apply")
        if FileManager.default.fileExists(atPath: mergeHead.path) || FileManager.default.fileExists(atPath: rebaseApply.path) {
            throw GitError.custom("当前工作区处于 Merge / Rebase 冲突中断状态，请先手动处理解决")
        }

        // 3. 校验远端连通性与访问权限
        let lsRemote = try await execute(args: ["ls-remote", "--exit-code", "-h", "origin"], at: directory)
        if !lsRemote.isSuccess {
            let msg = lsRemote.stderr.trimmingCharacters(in: .whitespacesAndNewlines)
            throw GitError.custom("无法连接到远程仓库或缺少访问权限: \(msg)")
        }

        // 4. 检查当前分支是否已建立远程追踪分支 (Upstream)
        let upstreamCheck = try await execute(args: ["rev-parse", "--abbrev-ref", "@{u}"], at: directory)
        if !upstreamCheck.isSuccess {
            throw GitError.custom("当前分支在远端尚无对应追踪分支，请先手动 push 建立关联 (git push -u origin <branch>)")
        }
    }
}
