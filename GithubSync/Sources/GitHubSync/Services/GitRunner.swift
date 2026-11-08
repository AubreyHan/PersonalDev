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
    case timedOut(command: String, timeout: TimeInterval)
    case custom(String)

    public var errorDescription: String? {
        switch self {
        case .commandFailed(let command, let exitCode, let stderr):
            let msg = stderr.trimmingCharacters(in: .whitespacesAndNewlines)
            if exitCode == 9 || exitCode == 137 {
                let hint = msg.isEmpty ? "macOS 代码签名异常 (SIGKILL/Invalid Page): 仓库内存在损坏或未正确签名的 Mach-O 二进制文件，系统内核在 Git 内存映射读取时强制终止了进程" : msg
                return "Git命令执行失败 (\(command), code \(exitCode)): \(hint)"
            }
            if msg.contains("GH013") {
                if msg.contains("PUSH PROTECTION") || msg.contains("Push cannot contain secrets") {
                    return "GitHub 安全拦截 (GH013: 发现敏感凭据/Token): 推送分支中包含被 GitHub 拦截的 Secret 密钥，请在提交历史中移除或至 GitHub 安全页面解除拦截。"
                } else if msg.contains("Protected branch") || msg.contains("protected") {
                    return "GitHub 规则拦截 (GH013: 受保护分支): 目标分支受保护，禁止直接推送。"
                }
            }
            return "Git命令执行失败 (\(command), code \(exitCode)): \(msg)"
        case .timedOut(let command, let timeout):
            return "Git命令执行超时 (\(command), 超过 \(Int(timeout)) 秒)"
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

    public func run(args: [String], at directory: URL, timeout: TimeInterval = 120.0, maxRetries: Int = 2) async throws -> String {
        var lastResult: GitResult?
        let isNetworkOp = ["fetch", "push", "pull", "clone", "ls-remote"].contains(args.first ?? "")
        let attempts = isNetworkOp ? (1 + maxRetries) : 1

        for attempt in 1...attempts {
            let result = try await execute(args: args, at: directory, timeout: timeout)
            if result.isSuccess {
                return result.stdout
            }
            lastResult = result
            if attempt < attempts {
                let delay = UInt64(attempt * 2) * 1_000_000_000
                AppLogger.shared.warn("Git 网络操作重试 [\(args.joined(separator: " "))]，将在 \(attempt * 2) 秒后进行第 \(attempt) 次重试...", tag: "Git")
                try? await Task.sleep(nanoseconds: delay)
            }
        }

        let cmdStr = args.joined(separator: " ")
        var stderr = lastResult?.stderr ?? ""
        if (lastResult?.exitCode == 9 || lastResult?.exitCode == 137) && stderr.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty {
            stderr = "macOS 代码签名校验异常 (SIGKILL / Code Signature Invalid): 仓库中存在未正确签名的 Mach-O 可执行文件，系统内核在 Git 内存映射读取时强制终止了进程。"
        }
        AppLogger.shared.error("Git 执行失败 [\(cmdStr)]: \(stderr.trimmingCharacters(in: .whitespacesAndNewlines))", tag: "Git")
        throw GitError.commandFailed(command: cmdStr, exitCode: lastResult?.exitCode ?? 1, stderr: stderr)
    }

    public func execute(args: [String], at directory: URL, timeout: TimeInterval = 120.0) async throws -> GitResult {
        return try await withCheckedThrowingContinuation { continuation in
            DispatchQueue.global(qos: .userInitiated).async {
                let process = Process()
                process.executableURL = URL(fileURLWithPath: self.gitPath)
                process.arguments = args
                process.currentDirectoryURL = directory

                // 继承并扩充系统环境变量，确保 ssh / 钥匙串可用，并禁用终端交互输入卡死
                var env = ProcessInfo.processInfo.environment
                let extraPaths = "/opt/homebrew/bin:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin"
                if let currentPath = env["PATH"] {
                    env["PATH"] = "\(extraPaths):\(currentPath)"
                } else {
                    env["PATH"] = extraPaths
                }
                if env["HOME"] == nil {
                    env["HOME"] = NSHomeDirectory()
                }
                if env["USER"] == nil {
                    env["USER"] = NSUserName()
                }
                env["GIT_TERMINAL_PROMPT"] = "0"
                env["GIT_MERGE_AUTOEDIT"] = "no"
                env["GIT_EDITOR"] = "true"
                let home = env["HOME"] ?? NSHomeDirectory()
                let githubKey = "\(home)/.ssh/github"
                var sshCmd = "ssh -o BatchMode=yes -o StrictHostKeyChecking=accept-new -o ConnectTimeout=15 -o ServerAliveInterval=15 -o ServerAliveCountMax=3"
                if FileManager.default.fileExists(atPath: githubKey) {
                    sshCmd += " -i \(githubKey) -o IdentitiesOnly=yes"
                }
                env["GIT_SSH_COMMAND"] = sshCmd
                process.environment = env

                let stdoutPipe = Pipe()
                let stderrPipe = Pipe()
                process.standardOutput = stdoutPipe
                process.standardError = stderrPipe

                // 超时监控与状态保护锁
                let lock = NSLock()
                var isCompleted = false
                var didTimeOut = false

                let timer = DispatchSource.makeTimerSource(queue: DispatchQueue.global(qos: .utility))
                timer.schedule(deadline: .now() + timeout)
                timer.setEventHandler {
                    lock.lock()
                    defer { lock.unlock() }
                    if !isCompleted {
                        didTimeOut = true
                        if process.isRunning {
                            process.terminate()
                        }
                    }
                }
                timer.resume()

                do {
                    try process.run()

                    // 并发排空 stdout 与 stderr 管道，防止超过 64KB 导致管道写满产生死锁
                    var stdoutData = Data()
                    var stderrData = Data()
                    let readGroup = DispatchGroup()

                    readGroup.enter()
                    DispatchQueue.global(qos: .userInitiated).async {
                        stdoutData = stdoutPipe.fileHandleForReading.readDataToEndOfFile()
                        readGroup.leave()
                    }

                    readGroup.enter()
                    DispatchQueue.global(qos: .userInitiated).async {
                        stderrData = stderrPipe.fileHandleForReading.readDataToEndOfFile()
                        readGroup.leave()
                    }

                    readGroup.wait()
                    process.waitUntilExit()

                    timer.cancel()
                    lock.lock()
                    isCompleted = true
                    let timedOut = didTimeOut
                    lock.unlock()

                    if timedOut {
                        let cmdStr = args.joined(separator: " ")
                        AppLogger.shared.error("Git 执行超时 (超过 \(Int(timeout)) 秒) [\(cmdStr)]", tag: "Git")
                        continuation.resume(throwing: GitError.timedOut(command: cmdStr, timeout: timeout))
                        return
                    }

                    let stdoutStr = String(data: stdoutData, encoding: .utf8) ?? ""
                    let stderrStr = String(data: stderrData, encoding: .utf8) ?? ""

                    let res = GitResult(exitCode: process.terminationStatus, stdout: stdoutStr, stderr: stderrStr)
                    continuation.resume(returning: res)
                } catch {
                    timer.cancel()
                    lock.lock()
                    isCompleted = true
                    let timedOut = didTimeOut
                    lock.unlock()

                    if timedOut {
                        let cmdStr = args.joined(separator: " ")
                        AppLogger.shared.error("Git 执行超时 (超过 \(Int(timeout)) 秒) [\(cmdStr)]", tag: "Git")
                        continuation.resume(throwing: GitError.timedOut(command: cmdStr, timeout: timeout))
                    } else {
                        continuation.resume(throwing: error)
                    }
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
