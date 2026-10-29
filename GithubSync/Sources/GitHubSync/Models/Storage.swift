import Foundation

public final class StorageManager {
    public static let shared = StorageManager()

    private let fileManager = FileManager.default
    private let appSupportURL: URL
    private let configFileURL: URL

    private(set) public var data: AppStorageData

    private init() {
        let baseDir = fileManager.urls(for: .applicationSupportDirectory, in: .userDomainMask).first!
        appSupportURL = baseDir.appendingPathComponent("com.aubreyhan.GitHubSync", isDirectory: true)
        configFileURL = appSupportURL.appendingPathComponent("config.json")

        self.data = AppStorageData.initial
        ensureDirectoryExists()
        load()
    }

    private func ensureDirectoryExists() {
        if !fileManager.fileExists(atPath: appSupportURL.path) {
            try? fileManager.createDirectory(at: appSupportURL, withIntermediateDirectories: true)
        }
    }

    public func load() {
        guard fileManager.fileExists(atPath: configFileURL.path),
              let fileData = try? Data(contentsOf: configFileURL) else {
            return
        }

        let decoder = JSONDecoder()
        decoder.dateDecodingStrategy = .iso8601
        if let decoded = try? decoder.decode(AppStorageData.self, from: fileData) {
            self.data = decoded
        }
    }

    public func save() {
        ensureDirectoryExists()
        let encoder = JSONEncoder()
        encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
        encoder.dateEncodingStrategy = .iso8601
        if let encoded = try? encoder.encode(self.data) {
            try? encoded.write(to: configFileURL, options: .atomic)
        }
    }

    // MARK: - Folders
    public func addFolder(_ folder: WatchedFolder) {
        if !data.watchedFolders.contains(where: { $0.id == folder.id || $0.path == folder.path }) {
            data.watchedFolders.append(folder)
            save()
        }
    }

    public func removeFolder(id: String) {
        data.watchedFolders.removeAll { $0.id == id }
        save()
    }

    public func updateFolderRepoCount(id: String, count: Int) {
        if let idx = data.watchedFolders.firstIndex(where: { $0.id == id }) {
            data.watchedFolders[idx].repoCount = count
            save()
        }
    }

    // MARK: - Repos
    public func saveRepoConfig(path: String, mode: SyncMode, lastSyncTime: Date?) {
        data.repoConfigs[path] = RepoSavedConfig(syncMode: mode, lastSyncTime: lastSyncTime)
        save()
    }

    public func getRepoConfig(path: String) -> RepoSavedConfig? {
        return data.repoConfigs[path]
    }

    public func removeRepoConfig(path: String) {
        data.repoConfigs.removeValue(forKey: path)
        save()
    }

    // MARK: - Settings
    public func updateSettings(_ settings: AppSettings) {
        data.settings = settings
        save()
    }

    // MARK: - Factory Reset (彻底注销与数据清理)
    public func factoryReset() {
        try? fileManager.removeItem(at: appSupportURL)
        data = AppStorageData.initial
    }
}
