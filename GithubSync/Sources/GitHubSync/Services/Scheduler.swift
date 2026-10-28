import Foundation

@MainActor
public protocol SchedulerDelegate: AnyObject {
    func schedulerDidUpdateRepoStatus(repoId: String, status: SyncStatus, lastSyncTime: Date?, errorMessage: String?)
    func schedulerDidUpdateRepoDetails(repoId: String, currentBranch: String, uncommittedChanges: Int)
}

public final class Scheduler: @unchecked Sendable {
    public static let shared = Scheduler()

    public weak var delegate: SchedulerDelegate?

    private var debounceWorkItems: [String: DispatchWorkItem] = [:]
    private var activeSyncTasks: Set<String> = []
    private var pollingTimer: Timer?
    private let queue = DispatchQueue(label: "com.aubreyhan.GitHubSync.scheduler", qos: .userInitiated)

    private init() {}

    // MARK: - 启动与停止
    public func startPolling(intervalMinutes: Int) {
        stopPolling()
        let interval = max(1, intervalMinutes) * 60
        DispatchQueue.main.async { [weak self] in
            guard let self = self else { return }
            self.pollingTimer = Timer.scheduledTimer(withTimeInterval: TimeInterval(interval), repeats: true) { [weak self] _ in
                self?.triggerScheduledPoll()
            }
        }
    }

    public func stopPolling() {
        DispatchQueue.main.async {
            self.pollingTimer?.invalidate()
            self.pollingTimer = nil
        }
    }

    public func stopAll() {
        queue.sync {
            for (_, item) in debounceWorkItems {
                item.cancel()
            }
            debounceWorkItems.removeAll()
        }
        stopPolling()
    }

    // MARK: - 本地文件变动防抖 (5秒)
    public func notifyFileChanged(in repo: GitRepo, debounceSeconds: Int = 5) {
        guard repo.syncMode != .stopped else { return }

        queue.async { [weak self] in
            guard let self = self else { return }
            // 取消已有的防抖计时
            if let existing = self.debounceWorkItems[repo.id] {
                existing.cancel()
            }

            let workItem = DispatchWorkItem { [weak self] in
                guard let self = self else { return }
                AppLogger.shared.info("[\(repo.name)] 5秒防抖结束，发起自动同步", tag: "Debouncer")
                Task {
                    await self.syncRepo(repo)
                }
            }

            self.debounceWorkItems[repo.id] = workItem
            let delay = Double(max(1, debounceSeconds))
            AppLogger.shared.info("[\(repo.name)] 捕获本地变动，启动 \(debounceSeconds) 秒防抖计时器", tag: "Debouncer")
            self.queue.asyncAfter(deadline: .now() + delay, execute: workItem)
        }
    }

    // MARK: - 定时轮询触发
    private func triggerScheduledPoll() {
        // 从 AppState 触发，由外部或委托调度
        NotificationCenter.default.post(name: .scheduledPollRequested, object: nil)
    }

    // MARK: - 执行仓库同步
    public func syncRepo(_ repo: GitRepo) async {
        let repoId = repo.id

        // 防止同仓库并发重入
        var alreadyRunning = false
        queue.sync {
            if activeSyncTasks.contains(repoId) {
                alreadyRunning = true
            } else {
                activeSyncTasks.insert(repoId)
            }
        }
        if alreadyRunning { return }

        defer {
            queue.sync {
                _ = activeSyncTasks.remove(repoId)
            }
        }

        // 通知状态变为 syncing
        DispatchQueue.main.async {
            self.delegate?.schedulerDidUpdateRepoStatus(repoId: repoId, status: .syncing, lastSyncTime: repo.lastSyncTime, errorMessage: nil)
        }

        do {
            try await SyncEngine.shared.performSync(for: repo)

            let repoURL = URL(fileURLWithPath: repo.path)
            let newBranch = await GitRunner.shared.getCurrentBranch(at: repoURL)
            let uncommitted = await GitRunner.shared.getUncommittedChangesCount(at: repoURL)
            let now = Date()

            // 持久化成功时间
            StorageManager.shared.saveRepoConfig(path: repo.path, mode: repo.syncMode, lastSyncTime: now)

            DispatchQueue.main.async {
                self.delegate?.schedulerDidUpdateRepoStatus(repoId: repoId, status: .success, lastSyncTime: now, errorMessage: nil)
                self.delegate?.schedulerDidUpdateRepoDetails(repoId: repoId, currentBranch: newBranch, uncommittedChanges: uncommitted)
            }
        } catch {
            let errorMsg = error.localizedDescription
            DispatchQueue.main.async {
                self.delegate?.schedulerDidUpdateRepoStatus(repoId: repoId, status: .error, lastSyncTime: repo.lastSyncTime, errorMessage: errorMsg)
            }
        }
    }

    // MARK: - 取消单个 Repo 调度
    public func cancelRepo(_ repoId: String) {
        queue.sync {
            if let item = debounceWorkItems.removeValue(forKey: repoId) {
                item.cancel()
            }
        }
    }
}

public extension Notification.Name {
    static let scheduledPollRequested = Notification.Name("com.aubreyhan.GitHubSync.scheduledPollRequested")
}
