import Foundation
import SwiftUI
import Combine

@MainActor
public final class AppState: ObservableObject {
    public static let shared = AppState()

    @Published public var watchedFolders: [WatchedFolder] = []
    @Published public var selectedFolderId: String? = nil // nil 表示“全部文件夹”
    @Published public var repos: [GitRepo] = []
    @Published public var settings: AppSettings = .default

    @Published public var searchText: String = ""
    @Published public var showSettings: Bool = false
    @Published public var showLogsModal: Bool = false
    @Published public var showPermissionModal: Bool = false
    @Published public var alertMessage: String? = nil
    @Published public var showAlert: Bool = false

    private let storage = StorageManager.shared
    private let discovery = DiscoveryEngine.shared
    private let watcher = FSEventsWatcher.shared
    private let scheduler = Scheduler.shared
    private let gitRunner = GitRunner.shared

    private init() {
        self.settings = storage.data.settings
        self.watchedFolders = storage.data.watchedFolders

        watcher.delegate = self
        scheduler.delegate = self

        // 监听定时轮询通知
        NotificationCenter.default.addObserver(
            self,
            selector: #selector(handleScheduledPoll),
            name: .scheduledPollRequested,
            object: nil
        )

        // 首次运行检查磁盘权限引导
        if !settings.firstRunCompleted {
            self.showPermissionModal = true
        }

        // 初始化加载与扫描
        Task {
            AppLogger.shared.info("GitHub Auto Sync 应用启动，已配置 \(self.watchedFolders.count) 个监控目录", tag: "App")
            await self.reloadAllWatchedFolders()
            self.scheduler.startPolling(intervalMinutes: self.settings.remotePollIntervalMinutes)
        }
    }

    // MARK: - 过滤计算属性
    public var filteredRepos: [GitRepo] {
        var list = repos

        if let selectedId = selectedFolderId {
            list = list.filter { $0.folderId == selectedId }
        }

        let query = searchText.trimmingCharacters(in: .whitespacesAndNewlines).lowercased()
        if !query.isEmpty {
            list = list.filter {
                $0.name.lowercased().contains(query) ||
                $0.path.lowercased().contains(query) ||
                $0.currentBranch.lowercased().contains(query)
            }
        }

        return list.sorted { $0.name.localizedStandardCompare($1.name) == .orderedAscending }
    }

    // MARK: - 文件夹管理
    public func promptAddFolder() {
        let panel = NSOpenPanel()
        panel.canChooseFiles = false
        panel.canChooseDirectories = true
        panel.allowsMultipleSelection = false
        panel.canCreateDirectories = false
        panel.prompt = "选择监控文件夹"

        if panel.runModal() == .OK, let selectedURL = panel.url {
            addFolder(path: selectedURL.path)
        }
    }

    public func addFolder(path: String) {
        do {
            let (canonical, name) = try discovery.validateNewFolder(path: path, existingFolders: watchedFolders)
            let folder = WatchedFolder(name: name, path: canonical)
            storage.addFolder(folder)
            watchedFolders = storage.data.watchedFolders
            AppLogger.shared.info("成功添加监控文件夹【\(name)】: \(canonical)", tag: "Workspace")

            Task {
                await scanFolder(folder)
                updateWatcherPaths()
            }
        } catch {
            showError(error.localizedDescription)
        }
    }

    public func removeFolder(id: String) {
        guard watchedFolders.contains(where: { $0.id == id }) else { return }

        // 停止该目录下所有 Repo 调度
        let reposInFolder = repos.filter { $0.folderId == id }
        for repo in reposInFolder {
            scheduler.cancelRepo(repo.id)
            storage.removeRepoConfig(path: repo.path)
        }

        // 从内存中移除
        repos.removeAll { $0.folderId == id }
        storage.removeFolder(id: id)
        watchedFolders = storage.data.watchedFolders

        if selectedFolderId == id {
            selectedFolderId = nil
        }

        updateWatcherPaths()
    }

    // MARK: - 扫描与更新
    public func reloadAllWatchedFolders() async {
        var allRepos: [GitRepo] = []
        for folder in watchedFolders {
            let found = await discovery.scanRepos(in: folder)
            storage.updateFolderRepoCount(id: folder.id, count: found.count)
            allRepos.append(contentsOf: found)
        }
        self.repos = allRepos
        self.watchedFolders = storage.data.watchedFolders
        updateWatcherPaths()
    }

    private func scanFolder(_ folder: WatchedFolder) async {
        let found = await discovery.scanRepos(in: folder)
        storage.updateFolderRepoCount(id: folder.id, count: found.count)
        self.watchedFolders = storage.data.watchedFolders

        // 增量合并到现有 repos
        var updated = repos.filter { $0.folderId != folder.id }
        updated.append(contentsOf: found)
        self.repos = updated
    }

    private func updateWatcherPaths() {
        let paths = watchedFolders.map { $0.path }
        watcher.updateWatchedPaths(paths)
    }

    // MARK: - 策略切换与预检 (Pre-flight Check)
    public func setRepoSyncMode(repo: GitRepo, mode: SyncMode) {
        guard repo.syncMode != mode else { return }

        // 若从 stopped 切换为任意开启状态，执行健康自检
        if repo.syncMode == .stopped && mode != .stopped {
            Task {
                do {
                    let repoURL = URL(fileURLWithPath: repo.path)
                    try await self.gitRunner.runPreflightCheck(at: repoURL)

                    // 预检通过，更新模式并持久化
                    self.applyRepoSyncMode(repoId: repo.id, mode: mode)
                } catch {
                    self.showError("无法开启同步：\(error.localizedDescription)")
                }
            }
        } else {
            applyRepoSyncMode(repoId: repo.id, mode: mode)
        }
    }

    private func applyRepoSyncMode(repoId: String, mode: SyncMode) {
        guard let idx = repos.firstIndex(where: { $0.id == repoId }) else { return }
        repos[idx].syncMode = mode
        if mode == .stopped {
            repos[idx].status = .idle
            scheduler.cancelRepo(repoId)
        }
        storage.saveRepoConfig(path: repos[idx].path, mode: mode, lastSyncTime: repos[idx].lastSyncTime)
        AppLogger.shared.info("仓库 [\(repos[idx].name)] 同步策略切换为: \(mode.displayName)", tag: "Strategy")

        // 当切换为开启同步模式时（双向同步 / 本地优先 / 远端优先），立即自动触发第一次同步
        if mode != .stopped {
            let targetRepo = repos[idx]
            AppLogger.shared.info("[\(targetRepo.name)] 状态改变，自动触发首次立即同步", tag: "Strategy")
            Task {
                await self.scheduler.syncRepo(targetRepo)
            }
        }
    }

    // MARK: - 辅助操作
    public func revealInFinder(path: String) {
        NSWorkspace.shared.selectFile(path, inFileViewerRootedAtPath: "")
    }

    public func openInTerminal(path: String) {
        if let url = URL(string: "x-terminal-emulator://"), NSWorkspace.shared.open(url) {
            return
        }
        let terminalScript = """
        tell application "Terminal"
            activate
            do script "cd '\(path)'"
        end tell
        """
        if let appleScript = NSAppleScript(source: terminalScript) {
            var error: NSDictionary?
            appleScript.executeAndReturnError(&error)
        }
    }

    public func showError(_ message: String) {
        self.alertMessage = message
        self.showAlert = true
    }

    public func completeFirstRun() {
        settings.firstRunCompleted = true
        storage.updateSettings(settings)
        showPermissionModal = false
    }

    public func updateSettings(_ newSettings: AppSettings) {
        self.settings = newSettings
        storage.updateSettings(newSettings)
        scheduler.startPolling(intervalMinutes: newSettings.remotePollIntervalMinutes)
    }

    public func factoryResetAndExit() {
        // 1. 注销系统自启
        AutoStartManager.shared.disableCompletely()
        // 2. 停掉所有后台服务
        watcher.stopWatching()
        scheduler.stopAll()
        // 3. 删除本地配置目录
        storage.factoryReset()
        // 4. 退出进程
        NSApp.terminate(nil)
    }

    @objc private func handleScheduledPoll() {
        let activeRepos = repos.filter { $0.syncMode != .stopped }
        for repo in activeRepos {
            Task {
                await self.scheduler.syncRepo(repo)
            }
        }
    }
}

// MARK: - Watcher 代理扩展
extension AppState: FSEventsWatcherDelegate {
    public func fileWatcherDidDetectRepoStructureChange(in folderPath: String) {
        Task {
            if let folder = self.watchedFolders.first(where: { $0.path == folderPath }) {
                await self.scanFolder(folder)
            }
        }
    }

    public func fileWatcherDidDetectFileChange(at path: String) {
        // 寻找匹配的 repo
        for repo in repos {
            if path.hasPrefix(repo.path + "/") || path == repo.path {
                scheduler.notifyFileChanged(in: repo, debounceSeconds: settings.debounceSeconds)
                break
            }
        }
    }
}

// MARK: - Scheduler 代理扩展
extension AppState: SchedulerDelegate {
    public func schedulerDidUpdateRepoStatus(repoId: String, status: SyncStatus, lastSyncTime: Date?, errorMessage: String?) {
        if let idx = repos.firstIndex(where: { $0.id == repoId }) {
            repos[idx].status = status
            if let lastSyncTime = lastSyncTime {
                repos[idx].lastSyncTime = lastSyncTime
            }
            repos[idx].lastErrorMessage = errorMessage
        }
    }

    public func schedulerDidUpdateRepoDetails(repoId: String, currentBranch: String, uncommittedChanges: Int) {
        if let idx = repos.firstIndex(where: { $0.id == repoId }) {
            repos[idx].currentBranch = currentBranch
            repos[idx].uncommittedChanges = uncommittedChanges
        }
    }
}
