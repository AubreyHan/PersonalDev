import Foundation

/// 历史累计清理统计（SPEC §1.4）
public struct LifetimeStats: Codable, Hashable, Sendable {
    public var totalDeletedAssetCount: Int
    public var totalFreedBytes: Int64
    public var totalEmptyAlbumsDeleted: Int

    public init(
        totalDeletedAssetCount: Int = 0,
        totalFreedBytes: Int64 = 0,
        totalEmptyAlbumsDeleted: Int = 0
    ) {
        self.totalDeletedAssetCount = totalDeletedAssetCount
        self.totalFreedBytes = totalFreedBytes
        self.totalEmptyAlbumsDeleted = totalEmptyAlbumsDeleted
    }
}

/// 用户偏好设置（SPEC §1.4 & FR-8.2）
public struct UserPreferences: Codable, Hashable, Sendable {
    public var includeHiddenAssets: Bool
    public var autoDeleteEmptyAlbums: Bool
    public var autoAdvanceAfterMarkDelete: Bool
    public var similaritySensitivity: SimilaritySensitivity

    public init(
        includeHiddenAssets: Bool = false,
        autoDeleteEmptyAlbums: Bool = true,
        autoAdvanceAfterMarkDelete: Bool = true,
        similaritySensitivity: SimilaritySensitivity = .standard
    ) {
        self.includeHiddenAssets = includeHiddenAssets
        self.autoDeleteEmptyAlbums = autoDeleteEmptyAlbums
        self.autoAdvanceAfterMarkDelete = autoAdvanceAfterMarkDelete
        self.similaritySensitivity = similaritySensitivity
    }
}

/// 本地持久化状态模型（对应 ~/Library/Application Support/PhotoCleaner/state.json，SPEC §1.4）
public struct PersistedState: Codable, Hashable, Sendable {
    public static let currentSchemaVersion: Int = 1

    public var schemaVersion: Int
    public var reviewedAssetIDsByDay: [String: Set<String>]
    public var trashBasketAssetIDs: [String]
    public var lifetimeStats: LifetimeStats
    public var preferences: UserPreferences

    public init(
        schemaVersion: Int = PersistedState.currentSchemaVersion,
        reviewedAssetIDsByDay: [String: Set<String>] = [:],
        trashBasketAssetIDs: [String] = [],
        lifetimeStats: LifetimeStats = LifetimeStats(),
        preferences: UserPreferences = UserPreferences()
    ) {
        self.schemaVersion = schemaVersion
        self.reviewedAssetIDsByDay = reviewedAssetIDsByDay
        self.trashBasketAssetIDs = trashBasketAssetIDs
        self.lifetimeStats = lifetimeStats
        self.preferences = preferences
    }

    public func reviewedAssetIDs(for monthDay: MonthDay) -> Set<String> {
        reviewedAssetIDsByDay[monthDay.storageKey] ?? []
    }

    public mutating func setReviewedAssetIDs(_ ids: Set<String>, for monthDay: MonthDay) {
        if ids.isEmpty {
            reviewedAssetIDsByDay.removeValue(forKey: monthDay.storageKey)
        } else {
            reviewedAssetIDsByDay[monthDay.storageKey] = ids
        }
    }

    public mutating func addReviewedAssetIDs(_ ids: Set<String>, for monthDay: MonthDay) {
        guard !ids.isEmpty else { return }
        var existing = reviewedAssetIDsByDay[monthDay.storageKey] ?? []
        existing.formUnion(ids)
        reviewedAssetIDsByDay[monthDay.storageKey] = existing
    }

    public mutating func resetReviewedAssetIDs(for monthDay: MonthDay) {
        reviewedAssetIDsByDay.removeValue(forKey: monthDay.storageKey)
    }
}
