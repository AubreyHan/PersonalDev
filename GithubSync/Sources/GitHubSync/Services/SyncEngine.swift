import Foundation

public final class SyncEngine {
    public static let shared = SyncEngine()

    private let gitRunner = GitRunner.shared
    private let largeFileHandler = LargeFileHandler.shared
    private let commitFormatter = CommitFormatter.shared

    private init() {}

    /// 执行同步
    public func performSync(for repo: GitRepo) async throws {
        let repoURL = URL(fileURLWithPath: repo.path)
        let branch = await gitRunner.getCurrentBranch(at: repoURL)
        AppLogger.shared.sync("开始同步 [\(repo.name)] (模式: \(repo.syncMode.badgeTitle), 分支: \(branch))", tag: "SyncEngine")

        switch repo.syncMode {
        case .stopped:
            return

        case .localFirst:
            try await executeLocalFirst(at: repoURL, branch: branch)

        case .remoteFirst:
            try await executeRemoteFirst(at: repoURL, branch: branch)

        case .bidirectionalNewest:
            try await executeBidirectionalNewest(at: repoURL, branch: branch)
        }
        AppLogger.shared.sync("[\(repo.name)] 同步完成", tag: "SyncEngine")
    }

    // MARK: - 模式一：本地优先 (local_first)
    private func executeLocalFirst(at repoURL: URL, branch: String) async throws {
        // 1. 检查是否有未提交变更
        let statusLines = try await gitRunner.getStatusPorcelain(at: repoURL)
        if !statusLines.isEmpty {
            // 处理大文件 (>=100MB)
            try await largeFileHandler.handleLargeFiles(at: repoURL, statusLines: statusLines)

            // 重新获取剔除大文件后的状态
            let filteredStatus = try await gitRunner.getStatusPorcelain(at: repoURL)
            if !filteredStatus.isEmpty {
                let commitMessage = commitFormatter.format(statusLines: filteredStatus)
                _ = try await gitRunner.run(args: ["add", "-A"], at: repoURL)
                _ = try await gitRunner.run(args: ["commit", "-m", commitMessage], at: repoURL)
            }
        }

        // 2. 尝试常规推送
        let pushResult = try await gitRunner.execute(args: ["push", "origin", branch], at: repoURL)
        if !pushResult.isSuccess {
            // 若被远端拒绝，使用 --force-with-lease 强推拉齐
            _ = try await gitRunner.run(args: ["push", "--force-with-lease", "origin", branch], at: repoURL)
        }
    }

    // MARK: - 模式二：远端优先 (remote_first)
    private func executeRemoteFirst(at repoURL: URL, branch: String) async throws {
        // 1. 获取远端最新更新
        _ = try await gitRunner.run(args: ["fetch", "origin", branch], at: repoURL)

        // 2. 丢弃本地所有未提交草稿与未跟踪文件
        _ = try? await gitRunner.run(args: ["reset", "--hard", "HEAD"], at: repoURL)
        _ = try? await gitRunner.run(args: ["clean", "-fd"], at: repoURL)

        // 3. 强制对齐到远端分支
        _ = try await gitRunner.run(args: ["reset", "--hard", "origin/\(branch)"], at: repoURL)
    }

    // MARK: - 模式三：双向同步（新覆盖旧 - bidirectional_newest）
    private func executeBidirectionalNewest(at repoURL: URL, branch: String) async throws {
        // 1. 本地变动提交
        let statusLines = try await gitRunner.getStatusPorcelain(at: repoURL)
        if !statusLines.isEmpty {
            try await largeFileHandler.handleLargeFiles(at: repoURL, statusLines: statusLines)
            let filteredStatus = try await gitRunner.getStatusPorcelain(at: repoURL)
            if !filteredStatus.isEmpty {
                let msg = commitFormatter.format(statusLines: filteredStatus)
                _ = try await gitRunner.run(args: ["add", "-A"], at: repoURL)
                _ = try await gitRunner.run(args: ["commit", "-m", msg], at: repoURL)
            }
        }

        // 2. 拉取远端信息
        _ = try await gitRunner.run(args: ["fetch", "origin", branch], at: repoURL)

        // 3. 比对 Ahead / Behind
        let revCountOutput = try await gitRunner.run(
            args: ["rev-list", "--left-right", "--count", "HEAD...origin/\(branch)"],
            at: repoURL
        )

        let parts = revCountOutput.trimmingCharacters(in: .whitespacesAndNewlines)
            .split(whereSeparator: { $0.isWhitespace })
            .compactMap { Int($0) }

        guard parts.count == 2 else { return }
        let ahead = parts[0]
        let behind = parts[1]

        if ahead > 0 && behind == 0 {
            // 本地领先，直接推送
            _ = try await gitRunner.run(args: ["push", "origin", branch], at: repoURL)
        } else if ahead == 0 && behind > 0 {
            // 远端领先，快进合并
            _ = try await gitRunner.run(args: ["merge", "--ff-only", "origin/\(branch)"], at: repoURL)
        } else if ahead > 0 && behind > 0 {
            // 两侧均有提交（冲突分叉），进行智能合并
            let mergeRes = try await gitRunner.execute(
                args: ["merge", "--allow-unrelated-histories", "origin/\(branch)"],
                at: repoURL
            )

            let gitDir = repoURL.appendingPathComponent(".git")
            let isMerging = FileManager.default.fileExists(atPath: gitDir.appendingPathComponent("MERGE_HEAD").path)

            if !mergeRes.isSuccess || isMerging {
                // 存在冲突，获取冲突文件清单
                let conflictFilesStr = (try? await gitRunner.run(
                    args: ["diff", "--name-only", "--diff-filter=U"],
                    at: repoURL
                )) ?? ""
                let conflictFiles = conflictFilesStr.components(separatedBy: .newlines)
                    .map { $0.trimmingCharacters(in: .whitespaces) }
                    .filter { !$0.isEmpty }

                for file in conflictFiles {
                    let fileURL = repoURL.appendingPathComponent(file)
                    let localMtime = (try? FileManager.default.attributesOfItem(atPath: fileURL.path)[.modificationDate] as? Date)?.timeIntervalSince1970 ?? 0

                    // 远端提交时间戳
                    let remoteTimeStr = (try? await gitRunner.run(
                        args: ["log", "-1", "--format=%ct", "origin/\(branch)", "--", file],
                        at: repoURL
                    ))?.trimmingCharacters(in: .whitespacesAndNewlines) ?? "0"
                    let remoteCommitTime = TimeInterval(remoteTimeStr) ?? 0

                    if localMtime >= remoteCommitTime {
                        // 本地更新，保留本地版本
                        _ = try? await gitRunner.run(args: ["checkout", "--ours", file], at: repoURL)
                    } else {
                        // 远端更新，保留远端版本
                        _ = try? await gitRunner.run(args: ["checkout", "--theirs", file], at: repoURL)
                    }
                }

                _ = try? await gitRunner.run(args: ["add", "-A"], at: repoURL)

                // 若处于 MERGE 状态或有未暂存变更，执行 commit，避免空提交抛错
                let statusAfterResolve = (try? await gitRunner.getStatusPorcelain(at: repoURL)) ?? []
                let stillMerging = FileManager.default.fileExists(atPath: gitDir.appendingPathComponent("MERGE_HEAD").path)
                if !statusAfterResolve.isEmpty || stillMerging {
                    _ = try await gitRunner.run(
                        args: ["commit", "-m", "auto-sync: resolve conflict by newer timestamp", "--allow-empty"],
                        at: repoURL
                    )
                }
            }

            // 推送合并结果
            _ = try await gitRunner.run(args: ["push", "origin", branch], at: repoURL)
        }
    }
}
