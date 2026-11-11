import AppKit
import Foundation
import Observation
import Photos

/// 工作区主视图路由模式
public enum WorkspaceMode: String, Codable, Hashable, Sendable {
    case grid
    case quickReview
}

/// 年份倒序分组结构（同年内按拍摄时间升序排列，FR-3.1）
public struct YearSection: Identifiable, Hashable, Sendable {
    public var id: Int { year }
    public let year: Int
    public let monthDay: MonthDay
    public let assets: [AssetItem]
    public let weekdayTitle: String

    public init(
        year: Int,
        monthDay: MonthDay,
        assets: [AssetItem],
        weekdayTitle: String? = nil
    ) {
        self.year = year
        self.monthDay = monthDay
        self.assets = assets
        self.weekdayTitle = weekdayTitle ?? Self.computeWeekdayTitle(year: year, monthDay: monthDay)
    }

    /// 组头展示标题，如 "2026年8月13日 · 周四 (12 项)"
    public var headerTitle: String {
        if weekdayTitle.isEmpty {
            return "\(year)年\(monthDay.month)月\(monthDay.day)日 (\(assets.count) 项)"
        }
        return "\(year)年\(monthDay.month)月\(monthDay.day)日 · \(weekdayTitle) (\(assets.count) 项)"
    }

    /// 当年所有未收藏（`!isFavorite`）资产的 ID 集合（供【全选本年未收藏项】使用）
    public var unfavoritedAssetIDs: Set<String> {
        Set(assets.filter { !$0.isFavorite }.map(\.id))
    }

    public static func computeWeekdayTitle(
        year: Int,
        monthDay: MonthDay,
        calendar: Calendar = .current
    ) -> String {
        var comps = DateComponents()
        comps.year = year
        comps.month = monthDay.month
        comps.day = monthDay.day
        guard let date = calendar.date(from: comps) else { return "" }
        let weekday = calendar.component(.weekday, from: date)
        let symbols = ["周日", "周一", "周二", "周三", "周四", "周五", "周六"]
        guard (1...7).contains(weekday) else { return "" }
        return symbols[weekday - 1]
    }
}

/// 当前选中“月-日”切片的状态、年份分组、多维筛选、多选交互、快审游标与收藏联动 ViewModel（FR-2 ~ FR-4, FR-7.1）
@MainActor
@Observable
public final class DaySliceViewModel {
    // MARK: - Observable State

    /// 当前选中的月-日切片（冷启动默认今日）
    public var selectedMonthDay: MonthDay {
        didSet {
            if selectedMonthDay != oldValue {
                handleMonthDayChanged()
            }
        }
    }

    /// 当前激活的顶部多维过滤胶囊
    public var activeFilter: MediaFilter = .all {
        didSet {
            if activeFilter != oldValue {
                if activeFilter == .largeFiles {
                    preloadAllDayFileSizes()
                }
                rebuildFilteredAndGroupedAssets()
            }
        }
    }

    /// `activeFilter` 的同义访问器
    public var mediaFilter: MediaFilter {
        get { activeFilter }
        set { activeFilter = newValue }
    }

    /// 主工作区模式（年份分组网格 vs 空格大图快审）
    public var workspaceMode: WorkspaceMode = .grid

    /// 右侧检视栏（相簿归属 + EXIF）是否显示（快捷键 `I` 切换）
    public var isInspectorVisible: Bool = true

    /// 当前选中的资产 ID 集合（支持单选、⌘多选、Shift连选、橡皮筋框选）
    public var selectedAssetIDs: Set<String> = [] {
        didSet {
            if selectedAssetIDs != oldValue {
                onSelectionChanged?(selectedAssetIDs)
                refreshPrimaryExifDetail()
            }
        }
    }

    /// 上一次单击/锚定选中的资产 ID（用于 Shift 区间连选）
    public var lastClickedAssetID: String?

    /// 大图快审模式下的当前焦点资产 ID
    public var reviewCursorAssetID: String? {
        didSet {
            if reviewCursorAssetID != oldValue {
                refreshPrimaryExifDetail()
                preheatAdjacentReviewAssets()
            }
        }
    }

    /// 大图快审按 `L` 键触发 Live Photo 播放的脉冲计数器
    public private(set) var livePhotoPlayTriggerCount: Int = 0

    /// 当前月-日下的全部存量资产（按拍摄时间升序）
    public private(set) var allDayAssets: [AssetItem] = []

    /// 经过 `activeFilter` 过滤后的资产列表
    public private(set) var filteredAssets: [AssetItem] = []

    /// 按年份倒序分组、同年内按时间升序排列的分组列表（FR-3.1）
    public private(set) var yearSections: [YearSection] = []

    /// 当前选中月-日的进度摘要（totalCount, newUnreviewedCount, status）
    public private(set) var currentDaySummary: DaySliceSummary

    /// 当前选中月-日已持久化的已审阅资产 ID 集合
    public private(set) var reviewedAssetIDsForCurrentDay: Set<String> = []

    /// 当前焦点/主选资产的 EXIF 元数据详情
    public private(set) var currentExifDetail: AssetExifDetail?

    /// 当前大图快审资产的 iCloud 下载进度（0.0...1.0，本地已就绪时为 nil）
    public var iCloudDownloadProgress: Double?

    // MARK: - Dependencies & Callbacks

    public let indexer: PhotoLibraryIndexer
    public let mediaLoadService: MediaLoadService
    public let albumGraphService: AlbumGraphService
    public let stateStore: LocalStateStore
    public let trashBasketVM: TrashBasketViewModel
    public let similarityVM: SimilarityCompareViewModel
    public let albumMembershipVM: AlbumMembershipViewModel

    public var onSelectionChanged: ((Set<String>) -> Void)?
    public var onDayProgressChanged: (() -> Void)?
    public var onToast: ((String, ToastKind) -> Void)?

    /// 可选自定义收藏切换执行器（供单元测试拦截验证，无需系统 PhotoKit 权限）
    public var customFavoriteExecutor: (@MainActor (String, Bool) async throws -> Void)?

    /// 是否处于单元测试注入资产模式
    private var isUsingTestingAssets: Bool = false

    public init(
        initialMonthDay: MonthDay = MonthDay.today(),
        indexer: PhotoLibraryIndexer = .shared,
        mediaLoadService: MediaLoadService = .shared,
        albumGraphService: AlbumGraphService = .shared,
        stateStore: LocalStateStore = .shared,
        trashBasketVM: TrashBasketViewModel,
        similarityVM: SimilarityCompareViewModel,
        albumMembershipVM: AlbumMembershipViewModel
    ) {
        self.selectedMonthDay = initialMonthDay
        self.indexer = indexer
        self.mediaLoadService = mediaLoadService
        self.albumGraphService = albumGraphService
        self.stateStore = stateStore
        self.trashBasketVM = trashBasketVM
        self.similarityVM = similarityVM
        self.albumMembershipVM = albumMembershipVM
        self.currentDaySummary = DaySliceSummary(
            monthDay: initialMonthDay,
            totalCount: 0,
            newUnreviewedCount: 0,
            status: .empty
        )
    }

    // MARK: - Computed Properties

    public var isQuickReviewActive: Bool {
        get { workspaceMode == .quickReview }
        set { workspaceMode = newValue ? .quickReview : .grid }
    }

    public var currentDayStatus: DayReviewStatus {
        currentDaySummary.status
    }

    public var totalDayAssetCount: Int {
        allDayAssets.count
    }

    public var filteredAssetCount: Int {
        filteredAssets.count
    }

    /// 顶部过滤栏展示的胶囊选项（仅当状态为 `.hasNew` 时展示 `.newOnly`，FR-3.3）
    public var availableFilters: [MediaFilter] {
        MediaFilter.allCases.filter { filter in
            if filter == .newOnly {
                return currentDayStatus == .hasNew
            }
            return true
        }
    }

    /// 当前快审焦点或主选中资产
    public var activeReviewAsset: AssetItem? {
        if let cursorID = reviewCursorAssetID,
           let match = filteredAssets.first(where: { $0.id == cursorID }) ?? allDayAssets.first(where: { $0.id == cursorID }) {
            return match
        }
        if selectedAssetIDs.count == 1,
           let onlyID = selectedAssetIDs.first,
           let match = filteredAssets.first(where: { $0.id == onlyID }) ?? allDayAssets.first(where: { $0.id == onlyID }) {
            return match
        }
        return filteredAssets.first
    }

    /// 当前选中的 `AssetItem` 数组（按当前过滤视图顺序）
    public var selectedAssets: [AssetItem] {
        filteredAssets.filter { selectedAssetIDs.contains($0.id) }
    }

    /// 右侧 EXIF 面板展示的目标资产（快审模式下为游标项，网格模式下为主选中项）
    public var primarySelectedAsset: AssetItem? {
        if workspaceMode == .quickReview, let reviewAsset = activeReviewAsset {
            return reviewAsset
        }
        if let anchor = lastClickedAssetID,
           selectedAssetIDs.contains(anchor),
           let item = allDayAssets.first(where: { $0.id == anchor }) {
            return item
        }
        if let firstSelectedID = selectedAssetIDs.first,
           let item = allDayAssets.first(where: { $0.id == firstSelectedID }) {
            return item
        }
        return nil
    }

    /// 判断某资产在 `.hasNew` 日期下是否属于“新增未审”项（FR-3.2, AC-2）
    public func isNewUnreviewed(_ assetID: String) -> Bool {
        guard currentDayStatus == .hasNew else { return false }
        return !reviewedAssetIDsForCurrentDay.contains(assetID)
    }

    /// 计算某个过滤胶囊对应的资产数量
    public func filterCount(for filter: MediaFilter) -> Int {
        applyFilter(filter, to: allDayAssets).count
    }

    // MARK: - Reload & Filtering Pipeline

    public func selectMonthDay(_ monthDay: MonthDay) {
        if selectedMonthDay != monthDay {
            selectedMonthDay = monthDay
        } else {
            reloadCurrentDayAssets()
        }
    }

    private func handleMonthDayChanged() {
        workspaceMode = .grid
        reviewCursorAssetID = nil
        lastClickedAssetID = nil
        selectedAssetIDs = []
        if activeFilter == .newOnly {
            activeFilter = .all
        }
        reloadCurrentDayAssets(triggerSimilarityAnalysis: true)
    }

    public func reloadCurrentDayAssets(triggerSimilarityAnalysis: Bool = true) {
        let persisted = stateStore.load()
        let reviewed = persisted.reviewedAssetIDs(for: selectedMonthDay)
        self.reviewedAssetIDsForCurrentDay = reviewed

        if !isUsingTestingAssets {
            let phAssets = indexer.assets(for: selectedMonthDay)
            self.allDayAssets = phAssets.map { phAsset in
                let id = phAsset.localIdentifier
                return AssetItem(
                    phAsset: phAsset,
                    isLocallyAvailable: mediaLoadService.localAvailabilityCache[id] ?? true,
                    fileSizeByteCount: mediaLoadService.cachedFileSize(for: id) ?? trashBasketVM.knownFileSizes[id]
                )
            }
        }

        trashBasketVM.registerKnownAssetItems(allDayAssets)

        let currentIDs = Set(allDayAssets.map(\.id))
        self.currentDaySummary = MonthDay.computeSummary(
            monthDay: selectedMonthDay,
            currentIDs: currentIDs,
            reviewedIDs: reviewed
        )

        if activeFilter == .newOnly && currentDaySummary.status != .hasNew {
            activeFilter = .all
        }

        // 修剪已不存在于当天的选中项
        let validSelection = selectedAssetIDs.intersection(currentIDs)
        if validSelection != selectedAssetIDs {
            selectedAssetIDs = validSelection
        }
        albumMembershipVM.updateSelection(selectedAssetIDs)

        rebuildFilteredAndGroupedAssets()

        if triggerSimilarityAnalysis && !isUsingTestingAssets {
            let daySnapshot = selectedMonthDay
            let assetsSnapshot = allDayAssets
            let sensitivity = persisted.preferences.similaritySensitivity
            Task { @MainActor [weak self] in
                guard let self else { return }
                await self.similarityVM.analyzeDay(
                    monthDay: daySnapshot,
                    assets: assetsSnapshot,
                    sensitivity: sensitivity
                )
                if self.selectedMonthDay == daySnapshot {
                    self.rebuildFilteredAndGroupedAssets()
                }
            }
        }
    }

    /// 供单元测试直接注入当天 `AssetItem` 列表（不依赖系统 PhotoKit）
    public func setDayAssetsForTesting(_ assets: [AssetItem], monthDay: MonthDay? = nil) {
        isUsingTestingAssets = true
        if let monthDay, monthDay != selectedMonthDay {
            self.selectedMonthDay = monthDay
        }
        self.allDayAssets = assets
        trashBasketVM.registerKnownAssetItems(assets)

        let persisted = stateStore.load()
        let reviewed = persisted.reviewedAssetIDs(for: selectedMonthDay)
        self.reviewedAssetIDsForCurrentDay = reviewed
        self.currentDaySummary = MonthDay.computeSummary(
            monthDay: selectedMonthDay,
            currentIDs: Set(assets.map(\.id)),
            reviewedIDs: reviewed
        )
        rebuildFilteredAndGroupedAssets()
    }

    public func rebuildFilteredAndGroupedAssets() {
        let filtered = applyFilter(activeFilter, to: allDayAssets)
        self.filteredAssets = filtered
        self.yearSections = Self.buildYearSections(from: filtered, monthDay: selectedMonthDay)
    }

    private func applyFilter(_ filter: MediaFilter, to items: [AssetItem]) -> [AssetItem] {
        switch filter {
        case .all:
            return items
        case .photosOnly:
            return items.filter(\.isPhoto)
        case .videosOnly:
            return items.filter(\.isVideo)
        case .screenshots:
            return items.filter(\.isScreenshot)
        case .largeFiles:
            return items
                .filter { ($0.fileSizeByteCount ?? mediaLoadService.cachedFileSize(for: $0.id) ?? 0) >= MediaFilter.largeFileThresholdBytes }
                .sorted {
                    let sizeA = $0.fileSizeByteCount ?? mediaLoadService.cachedFileSize(for: $0.id) ?? 0
                    let sizeB = $1.fileSizeByteCount ?? mediaLoadService.cachedFileSize(for: $1.id) ?? 0
                    if sizeA != sizeB { return sizeA > sizeB }
                    return ($0.creationDate ?? .distantPast) < ($1.creationDate ?? .distantPast)
                }
        case .similarGroups:
            let groupedIDs = similarityVM.allGroupedAssetIDs
            return items.filter { groupedIDs.contains($0.id) }
        case .uncategorized:
            return items.filter { albumGraphService.albumCount(forAssetID: $0.id) == 0 }
        case .newOnly:
            guard currentDaySummary.status == .hasNew else { return items }
            return items.filter { !reviewedAssetIDsForCurrentDay.contains($0.id) }
        }
    }

    /// 纯函数：将资产列表按年份倒序（`year` DESC）分组，同年内按 `creationDate` 升序排列（FR-3.1）
    public nonisolated static func buildYearSections(
        from assets: [AssetItem],
        monthDay: MonthDay
    ) -> [YearSection] {
        guard !assets.isEmpty else { return [] }
        let grouped = Dictionary(grouping: assets, by: \.year)
        let sortedYears = grouped.keys.sorted(by: >)

        return sortedYears.compactMap { year in
            guard let yearItems = grouped[year], !yearItems.isEmpty else { return nil }
            let sortedWithinYear = yearItems.sorted { lhs, rhs in
                let dateA = lhs.creationDate ?? .distantPast
                let dateB = rhs.creationDate ?? .distantPast
                if dateA != dateB {
                    return dateA < dateB
                }
                return lhs.id < rhs.id
            }
            return YearSection(
                year: year,
                monthDay: monthDay,
                assets: sortedWithinYear
            )
        }
    }

    // MARK: - Multi-Selection Actions (FR-3.4)

    /// 单击选中单张资产
    public func selectSingle(_ assetID: String) {
        selectedAssetIDs = [assetID]
        lastClickedAssetID = assetID
        reviewCursorAssetID = assetID
        albumMembershipVM.updateSelection(selectedAssetIDs)
    }

    /// ⌘ + 单击：切换某张资产的选中态
    public func toggleSelection(_ assetID: String) {
        var updated = selectedAssetIDs
        if updated.contains(assetID) {
            updated.remove(assetID)
        } else {
            updated.insert(assetID)
            lastClickedAssetID = assetID
        }
        selectedAssetIDs = updated
        albumMembershipVM.updateSelection(selectedAssetIDs)
    }

    /// Shift + 单击：基于 `lastClickedAssetID` 在当前可见资产序列中执行区间连选
    public func selectRange(to targetAssetID: String, additive: Bool = true) {
        let orderedIDs = yearSections.flatMap { $0.assets.map(\.id) }
        guard let targetIndex = orderedIDs.firstIndex(of: targetAssetID) else {
            selectSingle(targetAssetID)
            return
        }
        guard let anchorID = lastClickedAssetID ?? selectedAssetIDs.first,
              let anchorIndex = orderedIDs.firstIndex(of: anchorID) else {
            selectSingle(targetAssetID)
            return
        }

        let lower = min(anchorIndex, targetIndex)
        let upper = max(anchorIndex, targetIndex)
        let rangeSet = Set(orderedIDs[lower...upper])

        if additive {
            selectedAssetIDs.formUnion(rangeSet)
        } else {
            selectedAssetIDs = rangeSet
        }
        albumMembershipVM.updateSelection(selectedAssetIDs)
    }

    /// 统一处理网格缩略图点击事件（根据修饰键分发单选、⌘多选、Shift连选）
    public func handleGridTap(on assetID: String, modifiers: NSEvent.ModifierFlags = []) {
        if modifiers.contains(.shift) {
            selectRange(to: assetID, additive: modifiers.contains(.command))
        } else if modifiers.contains(.command) {
            toggleSelection(assetID)
        } else {
            selectSingle(assetID)
        }
    }

    /// 鼠标拖拽橡皮筋框选（FR-3.4）
    public func selectRubberBand(assetIDs: Set<String>, additive: Bool = false) {
        if additive {
            selectedAssetIDs.formUnion(assetIDs)
        } else {
            selectedAssetIDs = assetIDs
        }
        albumMembershipVM.updateSelection(selectedAssetIDs)
    }

    /// 年份组头快捷操作：【全选本年未收藏项】（FR-3.1）
    public func selectAllUnfavorited(inYear year: Int) {
        guard let section = yearSections.first(where: { $0.year == year }) else { return }
        let unfavorited = section.unfavoritedAssetIDs
        selectedAssetIDs.formUnion(unfavorited)
        if let first = section.assets.first(where: { !$0.isFavorite }) {
            lastClickedAssetID = first.id
        }
        albumMembershipVM.updateSelection(selectedAssetIDs)
    }

    public func selectAllFiltered() {
        selectedAssetIDs = Set(filteredAssets.map(\.id))
        albumMembershipVM.updateSelection(selectedAssetIDs)
    }

    public func clearSelection() {
        selectedAssetIDs.removeAll()
        lastClickedAssetID = nil
        albumMembershipVM.updateSelection([])
    }

    // MARK: - Workspace Mode & Quick Review Navigation (FR-4.1, FR-4.5)

    public func enterQuickReview(for assetID: String? = nil) {
        let targetID = assetID
            ?? reviewCursorAssetID
            ?? lastClickedAssetID
            ?? selectedAssetIDs.first
            ?? filteredAssets.first?.id
        guard let resolvedID = targetID else { return }

        reviewCursorAssetID = resolvedID
        if !selectedAssetIDs.contains(resolvedID) || selectedAssetIDs.count > 1 {
            selectedAssetIDs = [resolvedID]
            lastClickedAssetID = resolvedID
            albumMembershipVM.updateSelection(selectedAssetIDs)
        }
        workspaceMode = .quickReview
    }

    public func exitQuickReview() {
        if let cursorID = reviewCursorAssetID {
            selectedAssetIDs = [cursorID]
            lastClickedAssetID = cursorID
            albumMembershipVM.updateSelection(selectedAssetIDs)
        }
        workspaceMode = .grid
    }

    public func toggleQuickReview() {
        switch workspaceMode {
        case .grid:
            enterQuickReview()
        case .quickReview:
            exitQuickReview()
        }
    }

    public func toggleInspector() {
        isInspectorVisible.toggle()
    }

    public func triggerLivePhotoPlayback() {
        livePhotoPlayTriggerCount &+= 1
    }

    /// 键盘 `←`：向前移动焦点
    public func moveCursorPrevious() {
        let ordered = orderedVisibleAssets
        guard !ordered.isEmpty else { return }
        let currentID = reviewCursorAssetID ?? lastClickedAssetID ?? selectedAssetIDs.first
        let nextItem: AssetItem
        if let currentID,
           let idx = ordered.firstIndex(where: { $0.id == currentID }) {
            let prevIdx = max(0, idx - 1)
            nextItem = ordered[prevIdx]
        } else {
            nextItem = ordered[0]
        }
        selectSingle(nextItem.id)
    }

    /// 键盘 `→`：向后移动焦点
    public func moveCursorNext() {
        let ordered = orderedVisibleAssets
        guard !ordered.isEmpty else { return }
        let currentID = reviewCursorAssetID ?? lastClickedAssetID ?? selectedAssetIDs.first
        let nextItem: AssetItem
        if let currentID,
           let idx = ordered.firstIndex(where: { $0.id == currentID }) {
            let nextIdx = min(ordered.count - 1, idx + 1)
            nextItem = ordered[nextIdx]
        } else {
            nextItem = ordered[0]
        }
        selectSingle(nextItem.id)
    }

    public func selectAndReview(_ assetID: String) {
        selectSingle(assetID)
    }

    private var orderedVisibleAssets: [AssetItem] {
        let list = yearSections.flatMap(\.assets)
        return list.isEmpty ? filteredAssets : list
    }

    // MARK: - Favorite Toggle (`F` Key — FR-4.4 & FR-7.1)

    /// 切换当前选中项（或快审焦点项）在系统相册中的“收藏 ❤️”属性；
    /// 若点亮 `isFavorite == true` 且该项此前在待删暂存篮中，自动将其从待删暂存篮移出（FR-7.1）！
    public func toggleFavoriteForSelection() async {
        let targetIDs: [String]
        if workspaceMode == .quickReview, let cursorID = reviewCursorAssetID {
            targetIDs = [cursorID]
        } else if !selectedAssetIDs.isEmpty {
            targetIDs = Array(selectedAssetIDs)
        } else if let cursorID = reviewCursorAssetID {
            targetIDs = [cursorID]
        } else {
            return
        }

        for id in targetIDs {
            await toggleFavorite(for: id)
        }
    }

    public func toggleFavorite(for assetID: String) async {
        guard let idx = allDayAssets.firstIndex(where: { $0.id == assetID }) else {
            return
        }
        let targetFavorite = !allDayAssets[idx].isFavorite

        do {
            if let customFavoriteExecutor {
                try await customFavoriteExecutor(assetID, targetFavorite)
            } else if !isUsingTestingAssets {
                try await indexer.setFavorite(targetFavorite, for: assetID)
            }

            // 更新本地模型状态
            if let currentIdx = allDayAssets.firstIndex(where: { $0.id == assetID }) {
                allDayAssets[currentIdx].isFavorite = targetFavorite
            }
            rebuildFilteredAndGroupedAssets()
            similarityVM.updateAssetFavoriteState(assetID: assetID, isFavorite: targetFavorite)

            // FR-7.1: 若资产被点亮收藏 ❤️，且原在待删暂存篮中，自动从待删暂存篮移出！
            _ = trashBasketVM.handleAssetFavoriteChanged(assetID: assetID, isFavorite: targetFavorite)
        } catch {
            onToast?("修改收藏状态失败：\(error.localizedDescription)", .error)
        }
    }

    // MARK: - Trash Toggle (`Delete` / `⌫` / `X` — FR-4.5 & FR-7.1)

    /// 将当前选中项（或快审焦点项）加入/移出待删暂存篮；
    /// 若在大图快审模式下标记待删且开启了 `autoAdvanceAfterMarkDelete`，自动跳至下一张。
    public func toggleTrashForSelection() {
        let targets: [AssetItem]
        if workspaceMode == .quickReview, let active = activeReviewAsset {
            targets = [active]
        } else if !selectedAssets.isEmpty {
            targets = selectedAssets
        } else if let active = activeReviewAsset {
            targets = [active]
        } else {
            return
        }

        let orderedBefore = orderedVisibleAssets
        let currentCursorID = reviewCursorAssetID ?? targets.first?.id

        let didAddToBasket = trashBasketVM.toggleAssets(targets)

        if workspaceMode == .quickReview,
           didAddToBasket,
           stateStore.load().preferences.autoAdvanceAfterMarkDelete,
           let currentCursorID,
           let currentIndex = orderedBefore.firstIndex(where: { $0.id == currentCursorID }),
           currentIndex + 1 < orderedBefore.count {
            let nextAsset = orderedBefore[currentIndex + 1]
            selectSingle(nextAsset.id)
        }
    }

    // MARK: - Day Review Progress Actions (FR-2.4)

    /// 标记当前月-日为“已完成整理”：将当天所有未在待删暂存篮中的存量资产 ID 并入 `reviewedAssetIDsByDay`
    public func markDayCompleted() {
        let nonTrashedCurrentIDs = Set(
            allDayAssets
                .map(\.id)
                .filter { !trashBasketVM.isTrashed($0) }
        )
        let targetReviewedIDs = nonTrashedCurrentIDs.isEmpty
            ? Set(allDayAssets.map(\.id))
            : nonTrashedCurrentIDs

        guard !targetReviewedIDs.isEmpty else { return }

        _ = try? stateStore.markDayCompleted(selectedMonthDay, assetIDs: targetReviewedIDs)
        reloadCurrentDayAssets(triggerSimilarityAnalysis: false)
        onDayProgressChanged?()
        onToast?("已标记「\(selectedMonthDay.displayTitle)」整理完成", .success)
    }

    /// 将当前月-日重置为“未整理”状态
    public func resetDayProgress() {
        _ = try? stateStore.resetDayProgress(selectedMonthDay)
        reloadCurrentDayAssets(triggerSimilarityAnalysis: false)
        onDayProgressChanged?()
        onToast?("已将「\(selectedMonthDay.displayTitle)」重置为待整理", .info)
    }

    // MARK: - EXIF & Cache Preheating

    private func refreshPrimaryExifDetail() {
        guard let asset = primarySelectedAsset else {
            currentExifDetail = nil
            return
        }
        if let phAsset = asset.phAsset {
            if let cached = mediaLoadService.exifDetailCache[phAsset.localIdentifier] {
                currentExifDetail = cached
                return
            }
            let targetID = phAsset.localIdentifier
            Task { @MainActor [weak self] in
                guard let self else { return }
                let detail = await self.mediaLoadService.loadExifDetail(for: phAsset)
                if self.primarySelectedAsset?.id == targetID {
                    self.currentExifDetail = detail
                }
                if let idx = self.allDayAssets.firstIndex(where: { $0.id == targetID }) {
                    self.allDayAssets[idx].fileSizeByteCount = detail.fileSizeByteCount
                    self.allDayAssets[idx].isLocallyAvailable = detail.isLocallyAvailable
                }
            }
        } else {
            currentExifDetail = AssetExifDetail(
                filename: "IMG_\(asset.id).HEIC",
                fileSizeByteCount: asset.fileSizeByteCount ?? 0,
                pixelWidth: asset.pixelWidth,
                pixelHeight: asset.pixelHeight,
                uniformTypeIdentifier: asset.isVideo ? "com.apple.quicktime-movie" : "public.heic",
                captureDate: asset.creationDate,
                isLocallyAvailable: asset.isLocallyAvailable
            )
        }
    }

    private func preloadAllDayFileSizes() {
        let phAssets = allDayAssets.compactMap(\.phAsset)
        guard !phAssets.isEmpty else { return }
        Task { @MainActor [weak self] in
            guard let self else { return }
            let sizes = await self.mediaLoadService.loadFileSizes(for: phAssets)
            for idx in self.allDayAssets.indices {
                let id = self.allDayAssets[idx].id
                if let bytes = sizes[id] {
                    self.allDayAssets[idx].fileSizeByteCount = bytes
                }
            }
            self.rebuildFilteredAndGroupedAssets()
        }
    }

    private func preheatAdjacentReviewAssets() {
        guard workspaceMode == .quickReview,
              let cursorID = reviewCursorAssetID else { return }
        let ordered = orderedVisibleAssets
        guard let idx = ordered.firstIndex(where: { $0.id == cursorID }) else { return }

        var neighbors: [PHAsset] = []
        if idx > 0, let prev = ordered[idx - 1].phAsset {
            neighbors.append(prev)
        }
        if idx + 1 < ordered.count, let next = ordered[idx + 1].phAsset {
            neighbors.append(next)
        }
        if !neighbors.isEmpty {
            mediaLoadService.startCaching(
                assets: neighbors,
                targetSize: CGSize(width: 1280, height: 1280),
                contentMode: .aspectFit
            )
        }
    }
}
