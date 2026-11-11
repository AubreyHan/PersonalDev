import Foundation

/// 本地持久化状态存储服务（~/Library/Application Support/PhotoCleaner/state.json 原子读写）
public final class LocalStateStore: @unchecked Sendable {
    public static let shared = LocalStateStore()

    public let directoryURL: URL
    public let fileName: String

    private let lock = NSLock()
    private let fileManager: FileManager
    private var cachedState: PersistedState?

    public var stateFileURL: URL {
        directoryURL.appendingPathComponent(fileName, isDirectory: false)
    }

    public init(
        directoryURL: URL? = nil,
        fileName: String = "state.json",
        fileManager: FileManager = .default
    ) {
        self.fileManager = fileManager
        self.fileName = fileName
        if let directoryURL {
            self.directoryURL = directoryURL
        } else {
            let appSupport = fileManager.urls(for: .applicationSupportDirectory, in: .userDomainMask).first
                ?? URL(fileURLWithPath: NSHomeDirectory()).appendingPathComponent("Library/Application Support", isDirectory: true)
            self.directoryURL = appSupport.appendingPathComponent("PhotoCleaner", isDirectory: true)
        }
    }

    /// 读取持久化状态（若文件不存在或损坏则回退默认初始状态）
    public func load() -> PersistedState {
        lock.lock()
        defer { lock.unlock() }

        let targetURL = stateFileURL
        guard fileManager.fileExists(atPath: targetURL.path) else {
            let initial = PersistedState()
            cachedState = initial
            return initial
        }

        do {
            let data = try Data(contentsOf: targetURL)
            let decoder = JSONDecoder()
            let decoded = try decoder.decode(PersistedState.self, from: data)
            cachedState = decoded
            return decoded
        } catch {
            let fallback = cachedState ?? PersistedState()
            cachedState = fallback
            return fallback
        }
    }

    /// 通过临时文件写入 + `FileManager.replaceItemAt` 原子替换写入 `state.json`
    @discardableResult
    public func save(_ state: PersistedState) throws -> PersistedState {
        lock.lock()
        defer { lock.unlock() }
        return try saveUnlocked(state)
    }

    /// 原子读取-修改-保存闭包
    @discardableResult
    public func update(_ mutation: (inout PersistedState) -> Void) throws -> PersistedState {
        lock.lock()
        defer { lock.unlock() }

        var state = loadUnlocked()
        mutation(&state)
        return try saveUnlocked(state)
    }

    // MARK: - 便捷业务更新接口

    @discardableResult
    public func markDayCompleted(_ monthDay: MonthDay, assetIDs: Set<String>) throws -> PersistedState {
        try update { state in
            state.addReviewedAssetIDs(assetIDs, for: monthDay)
        }
    }

    @discardableResult
    public func resetDayProgress(_ monthDay: MonthDay) throws -> PersistedState {
        try update { state in
            state.resetReviewedAssetIDs(for: monthDay)
        }
    }

    @discardableResult
    public func resetAllReviewProgress() throws -> PersistedState {
        try update { state in
            state.reviewedAssetIDsByDay.removeAll()
        }
    }

    @discardableResult
    public func resetLifetimeStats() throws -> PersistedState {
        try update { state in
            state.lifetimeStats = LifetimeStats()
        }
    }

    @discardableResult
    public func updateTrashBasket(_ assetIDs: [String]) throws -> PersistedState {
        try update { state in
            state.trashBasketAssetIDs = assetIDs
        }
    }

    @discardableResult
    public func recordDeletion(
        deletedCount: Int,
        freedBytes: Int64,
        emptyAlbumsDeleted: Int = 0
    ) throws -> PersistedState {
        try update { state in
            state.lifetimeStats.totalDeletedAssetCount += max(0, deletedCount)
            state.lifetimeStats.totalFreedBytes += max(0, freedBytes)
            state.lifetimeStats.totalEmptyAlbumsDeleted += max(0, emptyAlbumsDeleted)
        }
    }

    @discardableResult
    public func recordEmptyAlbumsDeleted(_ count: Int) throws -> PersistedState {
        try update { state in
            state.lifetimeStats.totalEmptyAlbumsDeleted += max(0, count)
        }
    }

    @discardableResult
    public func updatePreferences(_ preferences: UserPreferences) throws -> PersistedState {
        try update { state in
            state.preferences = preferences
        }
    }

    // MARK: - Private

    private func loadUnlocked() -> PersistedState {
        if let cachedState {
            return cachedState
        }
        let targetURL = stateFileURL
        guard fileManager.fileExists(atPath: targetURL.path),
              let data = try? Data(contentsOf: targetURL),
              let decoded = try? JSONDecoder().decode(PersistedState.self, from: data) else {
            let initial = PersistedState()
            cachedState = initial
            return initial
        }
        cachedState = decoded
        return decoded
    }

    private func saveUnlocked(_ state: PersistedState) throws -> PersistedState {
        if !fileManager.fileExists(atPath: directoryURL.path) {
            try fileManager.createDirectory(at: directoryURL, withIntermediateDirectories: true)
        }

        let encoder = JSONEncoder()
        encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
        let data = try encoder.encode(state)

        let tempFileName = ".\(fileName).\(UUID().uuidString).tmp"
        let tempURL = directoryURL.appendingPathComponent(tempFileName, isDirectory: false)
        let targetURL = stateFileURL

        try data.write(to: tempURL, options: .atomic)

        if fileManager.fileExists(atPath: targetURL.path) {
            _ = try fileManager.replaceItemAt(
                targetURL,
                withItemAt: tempURL,
                backupItemName: nil,
                options: [.usingNewMetadataOnly]
            )
        } else {
            try fileManager.moveItem(at: tempURL, to: targetURL)
        }

        cachedState = state
        return state
    }
}
