import CoreGraphics
import Foundation
import Observation

/// 当日相似/连拍组状态与同屏并排对比 ViewModel（FR-5.1 ~ FR-5.3, SPEC §2.3）
@MainActor
@Observable
public final class SimilarityCompareViewModel {
    // MARK: - Per-Day Similarity Groups State

    /// 当前选中 MonthDay 下的全部相似/连拍组（按年份倒序、组内时间升序）
    public private(set) var similarityGroups: [SimilarityGroup] = []

    /// assetID -> SimilarityGroup O(1) 映射（供网格卡片高亮框查询）
    public private(set) var assetToGroupMap: [String: SimilarityGroup] = [:]

    /// 属于任意相似组的全部 assetID 集合（供 `.similarGroups` 过滤胶囊使用）
    public private(set) var allGroupedAssetIDs: Set<String> = []

    /// 是否正在后台执行 Vision 相似图特征分析
    public private(set) var isAnalyzing: Bool = false

    // MARK: - Compare Sheet State

    /// 同屏并排对比弹窗是否打开
    public var isComparePresented: Bool = false

    /// 当前正在对比的相似组（若为用户手动多选按 `C` 发起对比则为 nil）
    public private(set) var activeGroup: SimilarityGroup?

    /// 当前参与并排对比的资产 ID 列表（2 ~ N 张）
    public var comparingAssetIDs: [String] = []

    /// 当前参与并排对比的完整 `AssetItem` 列表
    public var comparingAssets: [AssetItem] = []

    /// 用户在对比视图中勾选“保留 (Keep)”的资产 ID 集合
    public var keptAssetIDs: Set<String> = []

    // MARK: - Synced Zoom & Pan State (FR-5.3)

    /// 多图同步缩放倍率（1.0 ... 5.0）
    public var sharedZoomScale: CGFloat = 1.0

    /// 多图同步平移偏移量
    public var sharedPanOffset: CGSize = .zero

    // MARK: - Dependencies

    public let similarityEngine: DaySimilarityEngine
    public let trashBasketVM: TrashBasketViewModel
    public let indexer: PhotoLibraryIndexer
    public let mediaLoadService: MediaLoadService

    public var onToast: ((String, ToastKind) -> Void)?

    /// 内存缓存当天资产字典，便于通过 ID 快速解析 AssetItem
    private var dayAssetsByID: [String: AssetItem] = [:]

    public init(
        similarityEngine: DaySimilarityEngine = .shared,
        trashBasketVM: TrashBasketViewModel,
        indexer: PhotoLibraryIndexer = .shared,
        mediaLoadService: MediaLoadService = .shared
    ) {
        self.similarityEngine = similarityEngine
        self.trashBasketVM = trashBasketVM
        self.indexer = indexer
        self.mediaLoadService = mediaLoadService
    }

    // MARK: - Group Queries for Grid Layout (FR-5.2)

    /// 获取某资产所属的相似组（若不属于任何相似组则返回 nil）
    public func group(forAssetID assetID: String) -> SimilarityGroup? {
        assetToGroupMap[assetID]
    }

    /// 判断某资产是否为其所属相似组的第一张（用于在首张卡片上方绘制【相似组 · N 张 | 对比此组】胶囊）
    public func isFirstAssetInGroup(_ assetID: String) -> Bool {
        guard let group = assetToGroupMap[assetID] else { return false }
        return group.assetIDs.first == assetID
    }

    /// 获取某年份下的所有相似组
    public func groups(forYear year: Int) -> [SimilarityGroup] {
        similarityGroups.filter { $0.year == year }
    }

    // MARK: - Per-Day Analysis Lifecycle

    /// 切换日期或调整灵敏度时异步运行单日相似聚类
    public func analyzeDay(
        monthDay: MonthDay,
        assets: [AssetItem],
        sensitivity: SimilaritySensitivity
    ) async {
        updateDayAssetLookup(assets)

        guard sensitivity != .off, assets.count >= 2 else {
            await similarityEngine.cancelCurrentTask()
            applyGroups([])
            isAnalyzing = false
            return
        }

        isAnalyzing = true
        let groups = await similarityEngine.analyze(
            monthDay: monthDay,
            assets: assets,
            sensitivity: sensitivity
        )
        applyGroups(groups)
        isAnalyzing = false
    }

    public func clearGroups() {
        applyGroups([])
        isAnalyzing = false
    }

    /// 供单元测试直接设置相似组与当天资产映射
    public func setSimilarityGroupsForTesting(
        _ groups: [SimilarityGroup],
        assets: [AssetItem] = []
    ) {
        if !assets.isEmpty {
            updateDayAssetLookup(assets)
        }
        applyGroups(groups)
    }

    private func updateDayAssetLookup(_ assets: [AssetItem]) {
        var map: [String: AssetItem] = [:]
        map.reserveCapacity(assets.count)
        for item in assets {
            map[item.id] = item
        }
        self.dayAssetsByID = map
        trashBasketVM.registerKnownAssetItems(assets)
    }

    private func applyGroups(_ groups: [SimilarityGroup]) {
        self.similarityGroups = groups
        var byAsset: [String: SimilarityGroup] = [:]
        var allIDs = Set<String>()
        for group in groups {
            for id in group.assetIDs {
                byAsset[id] = group
                allIDs.insert(id)
            }
        }
        self.assetToGroupMap = byAsset
        self.allGroupedAssetIDs = allIDs
    }

    // MARK: - Open / Dismiss Compare Sheet (FR-5.3)

    /// 点击相似组胶囊【对比此组】进入并排对比视图
    public func openCompare(for group: SimilarityGroup, allDayAssets: [AssetItem] = []) {
        if !allDayAssets.isEmpty {
            updateDayAssetLookup(allDayAssets)
        }
        self.activeGroup = group
        let resolvedAssets = resolveAssetItems(for: group.assetIDs)
        presentCompare(with: resolvedAssets, assetIDs: group.assetIDs)
    }

    /// 在网格中多选 >= 2 张照片后按快捷键 `C` 发起同屏对比
    public func openCompare(assetIDs: [String], allDayAssets: [AssetItem] = []) {
        if !allDayAssets.isEmpty {
            updateDayAssetLookup(allDayAssets)
        }
        guard assetIDs.count >= 2 else {
            onToast?("请至少选择 2 张照片以进行同屏对比", .info)
            return
        }
        self.activeGroup = nil
        let resolvedAssets = resolveAssetItems(for: assetIDs)
        presentCompare(with: resolvedAssets, assetIDs: assetIDs)
    }

    public func openCompare(assets: [AssetItem]) {
        guard assets.count >= 2 else {
            onToast?("请至少选择 2 张照片以进行同屏对比", .info)
            return
        }
        updateDayAssetLookup(assets)
        self.activeGroup = nil
        presentCompare(with: assets, assetIDs: assets.map(\.id))
    }

    private func presentCompare(with assets: [AssetItem], assetIDs: [String]) {
        self.comparingAssetIDs = assetIDs
        self.comparingAssets = assets
        self.sharedZoomScale = 1.0
        self.sharedPanOffset = .zero

        // 默认预勾选已收藏项，若无已收藏项则默认预勾选第 1 张作为保留项
        let favoritedIDs = Set(assets.filter(\.isFavorite).map(\.id))
        if !favoritedIDs.isEmpty {
            self.keptAssetIDs = favoritedIDs
        } else if let firstID = assetIDs.first {
            self.keptAssetIDs = [firstID]
        } else {
            self.keptAssetIDs = []
        }

        self.isComparePresented = true

        // 异步预热对比资产的精确体积
        let phAssets = assets.compactMap(\.phAsset)
        if !phAssets.isEmpty {
            Task { @MainActor [weak self] in
                guard let self else { return }
                let sizes = await self.mediaLoadService.loadFileSizes(for: phAssets)
                self.comparingAssets = self.comparingAssets.map { item in
                    var updated = item
                    if let bytes = sizes[item.id] {
                        updated.fileSizeByteCount = bytes
                    }
                    return updated
                }
                self.trashBasketVM.registerKnownAssetItems(self.comparingAssets)
            }
        }
    }

    public func closeCompare() {
        isComparePresented = false
    }

    public func dismissCompare() {
        isComparePresented = false
    }

    // MARK: - Keep Selection & Synced Zoom Controls

    public func isKept(_ assetID: String) -> Bool {
        keptAssetIDs.contains(assetID)
    }

    public func toggleKept(assetID: String) {
        if keptAssetIDs.contains(assetID) {
            keptAssetIDs.remove(assetID)
        } else {
            keptAssetIDs.insert(assetID)
        }
    }

    public func selectOnlyKept(assetID: String) {
        keptAssetIDs = [assetID]
    }

    public func resetZoomAndPan() {
        sharedZoomScale = 1.0
        sharedPanOffset = .zero
    }

    public func setZoomScale(_ scale: CGFloat) {
        let clamped = min(max(scale, 1.0), 6.0)
        sharedZoomScale = clamped
        if clamped <= 1.01 {
            sharedPanOffset = .zero
        }
    }

    public func zoomIn() {
        setZoomScale(sharedZoomScale * 1.25)
    }

    public func zoomOut() {
        setZoomScale(sharedZoomScale / 1.25)
    }

    /// 【保留已选，其余移入待删篮】（FR-5.3 & FR-7.1）：
    /// 将当前对比组中未被勾选为保留（`!keptAssetIDs.contains(id)`）的其余照片批量送入 `TrashBasketViewModel`
    /// （`TrashBasketViewModel` 会自动拦截并跳过 `isFavorite == true` 的照片并弹出 Toast）
    @discardableResult
    public func keepSelectedAndTrashOthers() -> TrashBasketViewModel.AddToBasketResult {
        guard !comparingAssetIDs.isEmpty else {
            return TrashBasketViewModel.AddToBasketResult()
        }
        guard !keptAssetIDs.isEmpty else {
            onToast?("请至少勾选 1 张要保留的照片", .warning)
            return TrashBasketViewModel.AddToBasketResult()
        }

        let resolvedAssets = comparingAssets.isEmpty ? resolveAssetItems(for: comparingAssetIDs) : comparingAssets
        let unselectedAssets = resolvedAssets.filter { !keptAssetIDs.contains($0.id) }

        let result = trashBasketVM.addAssets(unselectedAssets)
        isComparePresented = false
        return result
    }

    /// 在对比视图中同步更新某资产的收藏状态
    public func updateAssetFavoriteState(assetID: String, isFavorite: Bool) {
        if var item = dayAssetsByID[assetID] {
            item.isFavorite = isFavorite
            dayAssetsByID[assetID] = item
        }
        if let idx = comparingAssets.firstIndex(where: { $0.id == assetID }) {
            comparingAssets[idx].isFavorite = isFavorite
        }
    }

    private func resolveAssetItems(for ids: [String]) -> [AssetItem] {
        ids.compactMap { id in
            if let item = dayAssetsByID[id] {
                return item
            }
            if let cached = trashBasketVM.cachedAssetItemsByID[id] {
                return cached
            }
            if let phAsset = indexer.asset(for: id) {
                return AssetItem(
                    phAsset: phAsset,
                    isLocallyAvailable: mediaLoadService.localAvailabilityCache[id] ?? true,
                    fileSizeByteCount: mediaLoadService.cachedFileSize(for: id)
                )
            }
            return nil
        }
    }
}
