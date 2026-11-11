import Foundation
import Observation
import Photos

/// 非阻塞轻提示类型
public enum ToastKind: String, Codable, Hashable, Sendable {
    case info
    case success
    case warning
    case error

    public var systemImage: String {
        switch self {
        case .info: return "info.circle.fill"
        case .success: return "checkmark.circle.fill"
        case .warning: return "heart.slash.fill"
        case .error: return "exclamationmark.triangle.fill"
        }
    }
}

/// 全局非阻塞 Toast 消息结构
public struct ToastMessage: Identifiable, Hashable, Sendable {
    public let id: UUID
    public let message: String
    public let kind: ToastKind
    public let createdAt: Date
    public let durationSeconds: TimeInterval

    public init(
        id: UUID = UUID(),
        message: String,
        kind: ToastKind = .info,
        createdAt: Date = Date(),
        durationSeconds: TimeInterval = 3.2
    ) {
        self.id = id
        self.message = message
        self.kind = kind
        self.createdAt = createdAt
        self.durationSeconds = durationSeconds
    }
}

/// 全局待删暂存篮状态与编排 ViewModel（FR-7.1 ~ FR-7.4, EC-4, EC-5）
@MainActor
@Observable
public final class TrashBasketViewModel {
    public static let maxUndoSteps: Int = 50

    public struct AddToBasketResult: Equatable, Sendable {
        public let addedAssetIDs: [String]
        public let skippedFavoriteAssetIDs: [String]
        public let skippedReadOnlyAssetIDs: [String]

        public var addedCount: Int { addedAssetIDs.count }
        public var skippedFavoriteCount: Int { skippedFavoriteAssetIDs.count }
        public var skippedReadOnlyCount: Int { skippedReadOnlyAssetIDs.count }

        public init(
            addedAssetIDs: [String] = [],
            skippedFavoriteAssetIDs: [String] = [],
            skippedReadOnlyAssetIDs: [String] = []
        ) {
            self.addedAssetIDs = addedAssetIDs
            self.skippedFavoriteAssetIDs = skippedFavoriteAssetIDs
            self.skippedReadOnlyAssetIDs = skippedReadOnlyAssetIDs
        }
    }

    // MARK: - Observable State

    /// 当前暂存篮中待删资产的 localIdentifier 有序列表（持久化于 LocalStateStore）
    public private(set) var trashedAssetIDs: [String] = []

    /// O(1) 判断资产是否在待删篮中
    public private(set) var trashedAssetIDSet: Set<String> = []

    /// 暂存篮抽屉是否展开
    public var isDrawerPresented: Bool = false

    /// 是否正在执行系统删除提交
    public private(set) var isCommitting: Bool = false

    /// 内存已知资产体积映射（结合 MediaLoadService 与 AssetItem 注入体积）
    public private(set) var knownFileSizes: [String: Int64] = [:]

    /// 内存已知 AssetItem 快照（供抽屉预览及无 PhotoKit 单元测试使用）
    public private(set) var cachedAssetItemsByID: [String: AssetItem] = [:]

    /// 最多 50 步的撤销历史栈（保存每次变更前的 trashedAssetIDs 快照）
    private var undoStack: [[String]] = []

    // MARK: - Dependencies & Hooks

    public let stateStore: LocalStateStore
    public let indexer: PhotoLibraryIndexer
    public let mediaLoadService: MediaLoadService
    public let emptySweeper: EmptyCollectionSweeper
    public let albumGraphService: AlbumGraphService

    /// 触发非阻塞 Toast 的回调
    public var onToast: ((String, ToastKind) -> Void)?

    /// 提交删除成功后联动触发空相簿清理的回调（返回清理的空相簿+空文件夹总数）
    public var onTriggerEmptyAlbumSweep: (@MainActor () async -> Int)?

    /// 提交删除完成后的通知回调：(deletedAssetCount, freedBytes, deletedEmptyCollectionCount)
    public var onDeletionCommitted: (@MainActor (Int, Int64, Int) -> Void)?

    /// 可选自定义删除执行器（供单元测试拦截验证，不调用系统 PHPhotoLibrary 弹窗）
    public var customDeleteExecutor: (@MainActor ([String]) async throws -> Void)?

    public init(
        stateStore: LocalStateStore = .shared,
        indexer: PhotoLibraryIndexer = .shared,
        mediaLoadService: MediaLoadService = .shared,
        emptySweeper: EmptyCollectionSweeper = .shared,
        albumGraphService: AlbumGraphService = .shared
    ) {
        self.stateStore = stateStore
        self.indexer = indexer
        self.mediaLoadService = mediaLoadService
        self.emptySweeper = emptySweeper
        self.albumGraphService = albumGraphService

        let persisted = stateStore.load()
        var uniqueIDs: [String] = []
        var seen = Set<String>()
        for id in persisted.trashBasketAssetIDs where !seen.contains(id) {
            seen.insert(id)
            uniqueIDs.append(id)
        }
        self.trashedAssetIDs = uniqueIDs
        self.trashedAssetIDSet = seen
    }

    // MARK: - Computed Properties

    public var count: Int {
        trashedAssetIDs.count
    }

    public var isEmpty: Bool {
        trashedAssetIDs.isEmpty
    }

    public var canUndo: Bool {
        !undoStack.isEmpty
    }

    public var undoDepth: Int {
        undoStack.count
    }

    /// 预计释放磁盘字节数（汇总暂存篮内所有资产体积）
    public var estimatedFreedBytes: Int64 {
        var total: Int64 = 0
        for id in trashedAssetIDs {
            if let size = knownFileSizes[id] ?? mediaLoadService.cachedFileSize(for: id) ?? cachedAssetItemsByID[id]?.fileSizeByteCount {
                total += max(0, size)
            }
        }
        return total
    }

    /// 格式化后的预计释放空间字符串（如 "1.86 GB"）
    public var formattedEstimatedFreedBytes: String {
        ByteFormatter.string(fromByteCount: estimatedFreedBytes)
    }

    /// 底部暂存篮状态文案（如 "待删 24 项 · 预计释放 1.86 GB"）
    public var statusSummaryText: String {
        "待删 \(count) 项 · 预计释放 \(formattedEstimatedFreedBytes)"
    }

    /// 获取当前暂存篮内所有资产的 `AssetItem` 列表（用于 `TrashBasketDrawerView` 展示）
    public var trashedAssetItems: [AssetItem] {
        trashedAssetIDs.compactMap { id in
            if let phAsset = indexer.asset(for: id) {
                let size = knownFileSizes[id] ?? mediaLoadService.cachedFileSize(for: id) ?? cachedAssetItemsByID[id]?.fileSizeByteCount
                let localAvail = mediaLoadService.localAvailabilityCache[id] ?? true
                return AssetItem(
                    phAsset: phAsset,
                    isLocallyAvailable: localAvail,
                    fileSizeByteCount: size
                )
            }
            return cachedAssetItemsByID[id]
        }
    }

    // MARK: - Queries

    public func isTrashed(_ assetID: String) -> Bool {
        trashedAssetIDSet.contains(assetID)
    }

    public func isInBasket(_ assetID: String) -> Bool {
        trashedAssetIDSet.contains(assetID)
    }

    public func registerKnownAssetItems(_ items: [AssetItem]) {
        for item in items {
            cachedAssetItemsByID[item.id] = item
            if let bytes = item.fileSizeByteCount {
                knownFileSizes[item.id] = bytes
            }
        }
    }

    // MARK: - Add to Trash Basket with Favorite Lock (FR-7.1) & Permission Check (EC-4)

    @discardableResult
    public func addAsset(_ item: AssetItem) -> AddToBasketResult {
        addAssets([item])
    }

    /// 将 1 或 N 个资产加入待删暂存篮：
    /// - 自动过滤 `isFavorite == true` 的收藏照片并弹出非阻塞 Toast（FR-7.1）
    /// - 自动过滤 `!canDelete` 的共享图库只读项目并弹出非阻塞 Toast（EC-4）
    @discardableResult
    public func addAssets(_ items: [AssetItem]) -> AddToBasketResult {
        guard !items.isEmpty else {
            return AddToBasketResult()
        }

        registerKnownAssetItems(items)

        var addedIDs: [String] = []
        var skippedFavorites: [String] = []
        var skippedReadOnly: [String] = []

        for item in items {
            if item.isFavorite {
                skippedFavorites.append(item.id)
                continue
            }
            if !item.canDelete {
                skippedReadOnly.append(item.id)
                continue
            }
            if !trashedAssetIDSet.contains(item.id) && !addedIDs.contains(item.id) {
                addedIDs.append(item.id)
            }
        }

        if !addedIDs.isEmpty {
            pushUndoSnapshot()
            trashedAssetIDs.append(contentsOf: addedIDs)
            trashedAssetIDSet.formUnion(addedIDs)
            persistBasket()
            preloadFileSizesIfNeeded(for: addedIDs)
        }

        if !skippedFavorites.isEmpty {
            onToast?(
                "已自动跳过 \(skippedFavorites.count) 张收藏 ❤️ 照片；如需删除请先按 F 取消收藏",
                .warning
            )
        }

        if !skippedReadOnly.isEmpty {
            onToast?(
                "该共享图库项目不支持由当前账号删除",
                .warning
            )
        }

        return AddToBasketResult(
            addedAssetIDs: addedIDs,
            skippedFavoriteAssetIDs: skippedFavorites,
            skippedReadOnlyAssetIDs: skippedReadOnly
        )
    }

    /// 通过资产 ID 列表加入暂存篮
    @discardableResult
    public func addAssetIDs(
        _ assetIDs: [String],
        lookup: ((String) -> AssetItem?)? = nil
    ) -> AddToBasketResult {
        let items: [AssetItem] = assetIDs.compactMap { id in
            if let resolved = lookup?(id) ?? cachedAssetItemsByID[id] {
                return resolved
            }
            if let phAsset = indexer.asset(for: id) {
                return AssetItem(
                    phAsset: phAsset,
                    fileSizeByteCount: knownFileSizes[id] ?? mediaLoadService.cachedFileSize(for: id)
                )
            }
            return nil
        }
        return addAssets(items)
    }

    // MARK: - Toggle / Restore / Clear

    /// 切换单个资产的待删暂存状态（已在篮中则移出，不在则尝试加入并遵守收藏保护）
    /// 返回操作后是否有资产被新加入待删篮
    @discardableResult
    public func toggleAsset(_ item: AssetItem) -> Bool {
        toggleAssets([item])
    }

    /// 批量切换资产的待删暂存状态：
    /// - 若传入的所有资产均已在暂存篮中，则将它们全部移出恢复；
    /// - 否则将尚未在暂存篮中的资产加入暂存篮（遵守收藏 ❤️ 与只读权限过滤）。
    @discardableResult
    public func toggleAssets(_ items: [AssetItem]) -> Bool {
        guard !items.isEmpty else { return false }
        registerKnownAssetItems(items)

        let allAlreadyTrashed = items.allSatisfy { trashedAssetIDSet.contains($0.id) }
        if allAlreadyTrashed {
            let idsToRemove = Set(items.map(\.id))
            removeAssets(withIDs: idsToRemove)
            return false
        } else {
            let notYetTrashed = items.filter { !trashedAssetIDSet.contains($0.id) }
            let result = addAssets(notYetTrashed)
            return !result.addedAssetIDs.isEmpty
        }
    }

    /// 从待删暂存篮中恢复单个资产
    public func restoreAsset(id: String) {
        removeAssets(withIDs: [id])
    }

    public func removeAsset(id: String) {
        removeAssets(withIDs: [id])
    }

    /// 从待删暂存篮中批量移出恢复指定资产
    public func removeAssets(withIDs ids: Set<String>) {
        let matching = ids.intersection(trashedAssetIDSet)
        guard !matching.isEmpty else { return }

        pushUndoSnapshot()
        trashedAssetIDs.removeAll { matching.contains($0) }
        trashedAssetIDSet.subtract(matching)
        persistBasket()
    }

    public func restoreAssets(withIDs ids: [String]) {
        removeAssets(withIDs: Set(ids))
    }

    /// 一键清空待删暂存篮（支持 ⌘Z 撤销）
    public func clearBasket() {
        guard !trashedAssetIDs.isEmpty else { return }
        pushUndoSnapshot()
        trashedAssetIDs.removeAll()
        trashedAssetIDSet.removeAll()
        isDrawerPresented = false
        persistBasket()
    }

    // MARK: - Favorite联动自动移出 (FR-7.1)

    /// 当资产被点亮“收藏 ❤️”（`isFavorite == true`）时，若其处于待删暂存篮中则自动移出（FR-7.1）
    @discardableResult
    public func handleAssetFavoriteChanged(assetID: String, isFavorite: Bool) -> Bool {
        if var cached = cachedAssetItemsByID[assetID] {
            cached.isFavorite = isFavorite
            cachedAssetItemsByID[assetID] = cached
        }
        guard isFavorite, trashedAssetIDSet.contains(assetID) else {
            return false
        }
        pushUndoSnapshot()
        trashedAssetIDs.removeAll { $0 == assetID }
        trashedAssetIDSet.remove(assetID)
        persistBasket()
        onToast?("已点亮收藏 ❤️，该照片已自动从待删暂存篮移出", .info)
        return true
    }

    // MARK: - 50-Step Undo Stack (⌘Z — SPEC §2.4)

    @discardableResult
    public func undo() -> Bool {
        undoLastAction()
    }

    @discardableResult
    public func undoLastAction() -> Bool {
        guard let previousSnapshot = undoStack.popLast() else {
            return false
        }
        trashedAssetIDs = previousSnapshot
        trashedAssetIDSet = Set(previousSnapshot)
        persistBasket()
        onToast?("已撤销上一步待删暂存篮操作", .info)
        return true
    }

    private func pushUndoSnapshot() {
        if undoStack.count >= Self.maxUndoSteps {
            undoStack.removeFirst(undoStack.count - Self.maxUndoSteps + 1)
        }
        undoStack.append(trashedAssetIDs)
    }

    // MARK: - External Deletion Pruning (EC-5)

    /// 启动或监听到 `photoLibraryDidChange` 时，与当前全库有效资产 ID 集合求交集，静默剔除外部已删除项（EC-5）
    @discardableResult
    public func pruneInvalidAssets(validAssetIDs: Set<String>) -> [String] {
        guard !trashedAssetIDs.isEmpty else { return [] }
        var removed: [String] = []
        var kept: [String] = []
        kept.reserveCapacity(trashedAssetIDs.count)

        for id in trashedAssetIDs {
            if validAssetIDs.contains(id) {
                kept.append(id)
            } else {
                removed.append(id)
            }
        }

        guard !removed.isEmpty else { return [] }
        trashedAssetIDs = kept
        trashedAssetIDSet = Set(kept)
        // 清理撤销栈中已失效的 ID，避免撤销回外部已删除项
        undoStack = undoStack.map { snapshot in
            snapshot.filter { validAssetIDs.contains($0) }
        }
        persistBasket()
        return removed
    }

    // MARK: - Batch Commit Deletions (⌘Return — FR-7.3 & FR-6.4)

    /// 一次性调用 `PHAssetChangeRequest.deleteAssets` 提交暂存篮内全部资产，成功后更新统计并即刻触发空相簿清理
    @discardableResult
    public func commitDeletions() async -> Bool {
        let idsToDelete = trashedAssetIDs
        guard !idsToDelete.isEmpty, !isCommitting else {
            return false
        }

        isCommitting = true
        defer { isCommitting = false }

        // 确保提交前计算好释放空间
        let phAssetsToDelete = indexer.assets(for: idsToDelete)
        if !phAssetsToDelete.isEmpty {
            let loadedSizes = await mediaLoadService.loadFileSizes(for: phAssetsToDelete)
            for (id, bytes) in loadedSizes {
                knownFileSizes[id] = bytes
            }
        }
        let freedBytes = estimatedFreedBytes
        let deletedCount = idsToDelete.count

        do {
            if let customDeleteExecutor {
                try await customDeleteExecutor(idsToDelete)
            } else {
                try await indexer.deleteAssets(withIDs: idsToDelete)
            }

            // 清空暂存篮与撤销栈并持久化
            trashedAssetIDs.removeAll()
            trashedAssetIDSet.removeAll()
            undoStack.removeAll()
            isDrawerPresented = false
            persistBasket()

            // FR-6.4: 删照成功后即刻检查并清理因此变空的普通相簿与上级文件夹
            var sweptEmptyCount = 0
            let prefs = stateStore.load().preferences
            if prefs.autoDeleteEmptyAlbums {
                if let onTriggerEmptyAlbumSweep {
                    sweptEmptyCount = await onTriggerEmptyAlbumSweep()
                } else {
                    await albumGraphService.reloadGraph(includeHiddenAssets: prefs.includeHiddenAssets)
                    let sweepResult = await emptySweeper.sweepEmptyAlbumsAndFolders(albumGraphService: albumGraphService)
                    sweptEmptyCount = sweepResult.totalDeletedCount
                }
            } else {
                await albumGraphService.reloadGraph(includeHiddenAssets: prefs.includeHiddenAssets)
            }

            _ = try? stateStore.recordDeletion(
                deletedCount: deletedCount,
                freedBytes: freedBytes,
                emptyAlbumsDeleted: sweptEmptyCount
            )

            onDeletionCommitted?(deletedCount, freedBytes, sweptEmptyCount)
            onToast?(
                "已将 \(deletedCount) 项移入“最近删除”，释放 \(ByteFormatter.string(fromByteCount: freedBytes))",
                .success
            )
            return true
        } catch {
            if !EmptyCollectionSweeper.isUserCancelledError(error) {
                onToast?("提交清理未完成：\(error.localizedDescription)", .error)
            }
            return false
        }
    }

    // MARK: - Persistence & File Size Preloading

    private func persistBasket() {
        _ = try? stateStore.updateTrashBasket(trashedAssetIDs)
    }

    public func preloadFileSizesIfNeeded(for assetIDs: [String]? = nil) {
        let targetIDs = assetIDs ?? trashedAssetIDs
        let missingPHAssets = targetIDs.compactMap { id -> PHAsset? in
            guard knownFileSizes[id] == nil, mediaLoadService.cachedFileSize(for: id) == nil else {
                return nil
            }
            return indexer.asset(for: id)
        }
        guard !missingPHAssets.isEmpty else { return }

        Task { @MainActor [weak self] in
            guard let self else { return }
            let sizes = await self.mediaLoadService.loadFileSizes(for: missingPHAssets)
            for (id, size) in sizes {
                self.knownFileSizes[id] = size
            }
        }
    }
}
