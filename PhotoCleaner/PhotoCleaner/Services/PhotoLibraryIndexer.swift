import Foundation
import Observation
import Photos

@MainActor
@Observable
public final class PhotoLibraryIndexer: NSObject, PHPhotoLibraryChangeObserver {
    public static let shared = PhotoLibraryIndexer()

    /// 366 个月-日桶：MonthDay -> [PHAsset]（桶内按 creationDate 升序排列）
    public private(set) var assetsByMonthDay: [MonthDay: [PHAsset]] = [:]

    /// localIdentifier -> PHAsset O(1) 查找表
    public private(set) var assetsByID: [String: PHAsset] = [:]

    /// 当前全库有效资产 ID 集合（用于 EC-5 校验外部已删除资产）
    public private(set) var allValidAssetIDs: Set<String> = []

    /// 是否包含系统“已隐藏”相簿中的资产（FR-1.4 默认 false）
    public private(set) var includeHiddenAssets: Bool = false

    /// 是否正在执行索引构建
    public private(set) var isIndexing: Bool = false

    /// 是否已完成首次冷启动索引
    public private(set) var hasCompletedInitialIndex: Bool = false

    /// 索引变更版本号（每次重建或增量更新自增，便于 ViewModel 响应）
    public private(set) var indexRevision: Int = 0

    /// 外部图库变更回调（主线程触发）
    public var onLibraryDidChange: (@MainActor () -> Void)?

    private var changeListeners: [UUID: @MainActor () -> Void] = [:]
    private var lastFetchResult: PHFetchResult<PHAsset>?
    private var isObserverRegistered: Bool = false

    public override init() {
        super.init()
        var initialBuckets: [MonthDay: [PHAsset]] = [:]
        initialBuckets.reserveCapacity(366)
        for day in MonthDay.all366Days {
            initialBuckets[day] = []
        }
        self.assetsByMonthDay = initialBuckets
    }

    deinit {
        if isObserverRegistered {
            PHPhotoLibrary.shared().unregisterChangeObserver(self)
        }
    }

    // MARK: - Observer Registration

    public func registerChangeObserverIfNeeded() {
        guard !isObserverRegistered else { return }
        guard PHPhotoLibrary.authorizationStatus(for: .readWrite) == .authorized ||
              PHPhotoLibrary.authorizationStatus(for: .readWrite) == .limited else {
            return
        }
        PHPhotoLibrary.shared().register(self)
        isObserverRegistered = true
    }

    @discardableResult
    public func addChangeListener(_ listener: @escaping @MainActor () -> Void) -> UUID {
        let id = UUID()
        changeListeners[id] = listener
        return id
    }

    public func removeChangeListener(_ id: UUID) {
        changeListeners.removeValue(forKey: id)
    }

    // MARK: - Full & Incremental Indexing

    public func updateIncludeHiddenAssets(_ includeHidden: Bool) async {
        guard includeHidden != self.includeHiddenAssets || !hasCompletedInitialIndex else { return }
        self.includeHiddenAssets = includeHidden
        await buildIndex(includeHiddenAssets: includeHidden)
    }

    public func buildIndex(includeHiddenAssets: Bool? = nil) async {
        if let includeHiddenAssets {
            self.includeHiddenAssets = includeHiddenAssets
        }
        let status = PHPhotoLibrary.authorizationStatus(for: .readWrite)
        guard status == .authorized || status == .limited else {
            return
        }

        registerChangeObserverIfNeeded()
        isIndexing = true
        let includeHidden = self.includeHiddenAssets

        let snapshot = await Task.detached(priority: .userInitiated) { () -> IndexSnapshot in
            Self.scanPhotoLibrary(includeHiddenAssets: includeHidden)
        }.value

        applySnapshot(snapshot)
        isIndexing = false
        hasCompletedInitialIndex = true
    }

    public func rebuildIndexSynchronously(includeHiddenAssets: Bool? = nil) {
        if let includeHiddenAssets {
            self.includeHiddenAssets = includeHiddenAssets
        }
        let status = PHPhotoLibrary.authorizationStatus(for: .readWrite)
        guard status == .authorized || status == .limited else {
            return
        }
        registerChangeObserverIfNeeded()
        let snapshot = Self.scanPhotoLibrary(includeHiddenAssets: self.includeHiddenAssets)
        applySnapshot(snapshot)
        hasCompletedInitialIndex = true
    }

    private struct IndexSnapshot: @unchecked Sendable {
        let fetchResult: PHFetchResult<PHAsset>
        let buckets: [MonthDay: [PHAsset]]
        let byID: [String: PHAsset]
        let allIDs: Set<String>
    }

    nonisolated private static func scanPhotoLibrary(includeHiddenAssets: Bool) -> IndexSnapshot {
        let options = PHFetchOptions()
        options.includeHiddenAssets = includeHiddenAssets
        options.sortDescriptors = [NSSortDescriptor(key: "creationDate", ascending: true)]

        let fetchResult = PHAsset.fetchAssets(with: options)
        let count = fetchResult.count

        var buckets: [MonthDay: [PHAsset]] = [:]
        buckets.reserveCapacity(366)
        for day in MonthDay.all366Days {
            buckets[day] = []
        }

        var byID: [String: PHAsset] = [:]
        byID.reserveCapacity(count)
        var allIDs = Set<String>()
        allIDs.reserveCapacity(count)

        let calendar = Calendar.current

        fetchResult.enumerateObjects { asset, _, _ in
            let id = asset.localIdentifier
            byID[id] = asset
            allIDs.insert(id)

            let date = asset.creationDate ?? asset.modificationDate ?? Date.distantPast
            let components = calendar.dateComponents([.month, .day], from: date)
            if let month = components.month, let day = components.day {
                let monthDay = MonthDay(month: month, day: day)
                buckets[monthDay, default: []].append(asset)
            }
        }

        return IndexSnapshot(
            fetchResult: fetchResult,
            buckets: buckets,
            byID: byID,
            allIDs: allIDs
        )
    }

    private func applySnapshot(_ snapshot: IndexSnapshot) {
        self.lastFetchResult = snapshot.fetchResult
        self.assetsByMonthDay = snapshot.buckets
        self.assetsByID = snapshot.byID
        self.allValidAssetIDs = snapshot.allIDs
        self.indexRevision &+= 1
        notifyListeners()
    }

    private func notifyListeners() {
        onLibraryDidChange?()
        for listener in changeListeners.values {
            listener()
        }
    }

    // MARK: - PHPhotoLibraryChangeObserver

    nonisolated public func photoLibraryDidChange(_ changeInstance: PHChange) {
        Task { @MainActor [weak self] in
            guard let self else { return }
            if let lastFetch = self.lastFetchResult,
               let details = changeInstance.changeDetails(for: lastFetch) {
                let updatedFetchResult = details.fetchResultAfterChanges
                let snapshot = await Task.detached(priority: .userInitiated) {
                    Self.bucketFetchResult(updatedFetchResult)
                }.value
                self.applySnapshot(snapshot)
            } else {
                await self.buildIndex()
            }
        }
    }

    nonisolated private static func bucketFetchResult(_ fetchResult: PHFetchResult<PHAsset>) -> IndexSnapshot {
        let count = fetchResult.count
        var buckets: [MonthDay: [PHAsset]] = [:]
        buckets.reserveCapacity(366)
        for day in MonthDay.all366Days {
            buckets[day] = []
        }

        var byID: [String: PHAsset] = [:]
        byID.reserveCapacity(count)
        var allIDs = Set<String>()
        allIDs.reserveCapacity(count)

        let calendar = Calendar.current

        fetchResult.enumerateObjects { asset, _, _ in
            let id = asset.localIdentifier
            byID[id] = asset
            allIDs.insert(id)

            let date = asset.creationDate ?? asset.modificationDate ?? Date.distantPast
            let components = calendar.dateComponents([.month, .day], from: date)
            if let month = components.month, let day = components.day {
                let monthDay = MonthDay(month: month, day: day)
                buckets[monthDay, default: []].append(asset)
            }
        }

        return IndexSnapshot(
            fetchResult: fetchResult,
            buckets: buckets,
            byID: byID,
            allIDs: allIDs
        )
    }

    // MARK: - Queries & Helpers

    public func assets(for monthDay: MonthDay) -> [PHAsset] {
        assetsByMonthDay[monthDay] ?? []
    }

    public func assetIDs(for monthDay: MonthDay) -> Set<String> {
        let list = assetsByMonthDay[monthDay] ?? []
        var set = Set<String>()
        set.reserveCapacity(list.count)
        for asset in list {
            set.insert(asset.localIdentifier)
        }
        return set
    }

    public func asset(for id: String) -> PHAsset? {
        assetsByID[id]
    }

    public func assets(for ids: [String]) -> [PHAsset] {
        ids.compactMap { assetsByID[$0] }
    }

    public func assetItems(for monthDay: MonthDay, fileSizeCache: [String: Int64] = [:]) -> [AssetItem] {
        let phAssets = assets(for: monthDay)
        return phAssets.map { phAsset in
            AssetItem(
                phAsset: phAsset,
                fileSizeByteCount: fileSizeCache[phAsset.localIdentifier]
            )
        }
    }

    public func computeAllDaySummaries(reviewedAssetIDsByDay: [String: Set<String>]) -> [DaySliceSummary] {
        MonthDay.all366Days.map { day in
            let currentAssets = assetsByMonthDay[day] ?? []
            var currentIDs = Set<String>()
            currentIDs.reserveCapacity(currentAssets.count)
            for asset in currentAssets {
                currentIDs.insert(asset.localIdentifier)
            }
            let reviewedIDs = reviewedAssetIDsByDay[day.storageKey] ?? []
            let status = MonthDay.computeStatus(currentIDs: currentIDs, reviewedIDs: reviewedIDs)
            let newCount: Int
            switch status {
            case .empty:
                newCount = 0
            case .pending:
                newCount = currentIDs.count
            case .completed:
                newCount = 0
            case .hasNew:
                newCount = currentIDs.subtracting(reviewedIDs).count
            }
            return DaySliceSummary(
                monthDay: day,
                totalCount: currentIDs.count,
                newUnreviewedCount: newCount,
                status: status
            )
        }
    }

    // MARK: - Native PhotoKit Asset Mutations (Favorite & Delete)

    /// 切换或设置系统相册原生“收藏 ❤️”属性（FR-4.4）
    @discardableResult
    public func toggleFavorite(for assetID: String) async throws -> Bool {
        guard let phAsset = assetsByID[assetID] ?? PHAsset.fetchAssets(withLocalIdentifiers: [assetID], options: nil).firstObject else {
            return false
        }
        let targetFavorite = !phAsset.isFavorite
        try await setFavorite(targetFavorite, for: phAsset)
        return targetFavorite
    }

    public func setFavorite(_ isFavorite: Bool, for assetID: String) async throws {
        guard let phAsset = assetsByID[assetID] ?? PHAsset.fetchAssets(withLocalIdentifiers: [assetID], options: nil).firstObject else {
            return
        }
        try await setFavorite(isFavorite, for: phAsset)
    }

    public func setFavorite(_ isFavorite: Bool, for asset: PHAsset) async throws {
        try await PHPhotoLibrary.shared().performChanges {
            let request = PHAssetChangeRequest(for: asset)
            request.isFavorite = isFavorite
        }
        await buildIndex()
    }

    /// 批量将指定资产移入系统“最近删除”（FR-7.3，单次确认弹窗）
    public func deleteAssets(withIDs assetIDs: [String]) async throws {
        guard !assetIDs.isEmpty else { return }
        let fetchResult = PHAsset.fetchAssets(withLocalIdentifiers: assetIDs, options: nil)
        guard fetchResult.count > 0 else { return }

        try await PHPhotoLibrary.shared().performChanges {
            PHAssetChangeRequest.deleteAssets(fetchResult)
        }
        await buildIndex()
    }
}
