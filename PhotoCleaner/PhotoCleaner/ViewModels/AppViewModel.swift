import Foundation
import Observation
import Photos

/// 全局根协调器 ViewModel（管理 366 天统计看板、子 ViewModel 联动、冷启动空相簿清理、Toast 队列与快捷键路由）
@MainActor
@Observable
public final class AppViewModel {
    // MARK: - Services

    public let permissionService: PhotoPermissionService
    public let indexer: PhotoLibraryIndexer
    public let mediaLoadService: MediaLoadService
    public let albumGraphService: AlbumGraphService
    public let emptySweeper: EmptyCollectionSweeper
    public let similarityEngine: DaySimilarityEngine
    public let stateStore: LocalStateStore

    // MARK: - Child ViewModels

    public let trashBasketVM: TrashBasketViewModel
    public let albumMembershipVM: AlbumMembershipViewModel
    public let similarityVM: SimilarityCompareViewModel
    public let daySliceVM: DaySliceViewModel

    // MARK: - 366-Day Summaries & Lifetime Stats

    /// 全年 366 个月-日切片的进度摘要（含 02-29）
    public private(set) var daySummaries: [DaySliceSummary] = []

    /// MonthDay -> DaySliceSummary O(1) 查找表
    public private(set) var daySummariesByMonthDay: [MonthDay: DaySliceSummary] = [:]

    /// 历史累计统计数据
    public private(set) var lifetimeStats: LifetimeStats

    /// 用户偏好设置
    public var preferences: UserPreferences {
        didSet {
            if preferences != oldValue {
                handlePreferencesChanged(from: oldValue, to: preferences)
            }
        }
    }

    // MARK: - Toast Notification Queue

    /// 待展示或正在展示的轻提示队列
    public private(set) var toastQueue: [ToastMessage] = []

    /// 当前顶部浮层展示的 Toast
    public var activeToast: ToastMessage? {
        toastQueue.last
    }

    /// 是否已完成首次冷启动加载与首次空相簿自检
    public private(set) var hasPerformedColdStartLoad: Bool = false

    /// 是否正在执行冷启动索引
    public private(set) var isBootstrapping: Bool = false

    private var changeListenerID: UUID?
    private var toastDismissTasks: [UUID: Task<Void, Never>] = [:]

    // MARK: - Initialization

    public init(
        initialMonthDay: MonthDay = MonthDay.today(),
        permissionService: PhotoPermissionService = .shared,
        indexer: PhotoLibraryIndexer = .shared,
        mediaLoadService: MediaLoadService = .shared,
        albumGraphService: AlbumGraphService = .shared,
        emptySweeper: EmptyCollectionSweeper = .shared,
        similarityEngine: DaySimilarityEngine = .shared,
        stateStore: LocalStateStore = .shared
    ) {
        self.permissionService = permissionService
        self.indexer = indexer
        self.mediaLoadService = mediaLoadService
        self.albumGraphService = albumGraphService
        self.emptySweeper = emptySweeper
        self.similarityEngine = similarityEngine
        self.stateStore = stateStore

        let persisted = stateStore.load()
        self.lifetimeStats = persisted.lifetimeStats
        self.preferences = persisted.preferences

        let basketVM = TrashBasketViewModel(
            stateStore: stateStore,
            indexer: indexer,
            mediaLoadService: mediaLoadService,
            emptySweeper: emptySweeper,
            albumGraphService: albumGraphService
        )
        let albumVM = AlbumMembershipViewModel(
            albumGraphService: albumGraphService,
            emptySweeper: emptySweeper,
            stateStore: stateStore
        )
        let simVM = SimilarityCompareViewModel(
            similarityEngine: similarityEngine,
            trashBasketVM: basketVM,
            indexer: indexer,
            mediaLoadService: mediaLoadService
        )
        let sliceVM = DaySliceViewModel(
            initialMonthDay: initialMonthDay,
            indexer: indexer,
            mediaLoadService: mediaLoadService,
            albumGraphService: albumGraphService,
            stateStore: stateStore,
            trashBasketVM: basketVM,
            similarityVM: simVM,
            albumMembershipVM: albumVM
        )

        self.trashBasketVM = basketVM
        self.albumMembershipVM = albumVM
        self.similarityVM = simVM
        self.daySliceVM = sliceVM

        recomputeAllDaySummaries()
        wireChildCallbacks()
    }

    private func wireChildCallbacks() {
        let toastHandler: (String, ToastKind) -> Void = { [weak self] message, kind in
            self?.showToast(message, kind: kind)
        }
        trashBasketVM.onToast = toastHandler
        albumMembershipVM.onToast = toastHandler
        similarityVM.onToast = toastHandler
        daySliceVM.onToast = toastHandler

        daySliceVM.onDayProgressChanged = { [weak self] in
            self?.recomputeAllDaySummaries()
        }

        albumMembershipVM.onMembershipChanged = { [weak self] in
            self?.daySliceVM.rebuildFilteredAndGroupedAssets()
        }

        albumMembershipVM.onTriggerEmptyAlbumSweep = { [weak self] in
            guard let self else { return 0 }
            return await self.runEmptyCollectionSweep()
        }

        trashBasketVM.onTriggerEmptyAlbumSweep = { [weak self] in
            guard let self else { return 0 }
            await self.albumGraphService.reloadGraph(includeHiddenAssets: self.preferences.includeHiddenAssets)
            return await self.runEmptyCollectionSweep()
        }

        trashBasketVM.onDeletionCommitted = { [weak self] _, _, _ in
            guard let self else { return }
            self.lifetimeStats = self.stateStore.load().lifetimeStats
            self.recomputeAllDaySummaries()
            self.daySliceVM.reloadCurrentDayAssets()
            self.albumMembershipVM.recomputeRows()
        }

        // 监听外部 Photos.app 图库变更（FR-1.3 & EC-5）
        // 注意：外部变更绝不自动触发 EmptyCollectionSweeper（FR-6.4 防误删新建空相簿）
        changeListenerID = indexer.addChangeListener { [weak self] in
            guard let self else { return }
            Task { @MainActor in
                await self.handleExternalLibraryChange()
            }
        }
    }

    // MARK: - Convenience Forwarders & Progress Counts (FR-2.5)

    public var selectedMonthDay: MonthDay {
        get { daySliceVM.selectedMonthDay }
        set { daySliceVM.selectMonthDay(newValue) }
    }

    public var isAuthorized: Bool {
        permissionService.isAuthorized
    }

    public var authorizationStatus: PHAuthorizationStatus {
        permissionService.authorizationStatus
    }

    /// 已完成整理的天数（`.completed`）
    public var completedDaysCount: Int {
        daySummaries.reduce(0) { $0 + ($1.status == .completed ? 1 : 0) }
    }

    /// 有照片的总天数（非 `.empty`）
    public var nonEmptyDaysCount: Int {
        daySummaries.reduce(0) { $0 + ($1.status != .empty ? 1 : 0) }
    }

    /// 待整理天数（`.pending` + `.hasNew`）
    public var pendingDaysCount: Int {
        daySummaries.reduce(0) { $0 + (($1.status == .pending || $1.status == .hasNew) ? 1 : 0) }
    }

    /// 有新增待审天数（`.hasNew`）
    public var hasNewDaysCount: Int {
        daySummaries.reduce(0) { $0 + ($1.status == .hasNew ? 1 : 0) }
    }

    /// 366 天整体完成进度（0.0 ... 1.0）
    public var completionProgress: Double {
        guard nonEmptyDaysCount > 0 else { return 0.0 }
        return Double(completedDaysCount) / Double(nonEmptyDaysCount)
    }

    public var totalDeletedAssetCount: Int {
        lifetimeStats.totalDeletedAssetCount
    }

    public var totalFreedBytes: Int64 {
        lifetimeStats.totalFreedBytes
    }

    public var totalEmptyAlbumsDeleted: Int {
        lifetimeStats.totalEmptyAlbumsDeleted
    }

    public var formattedTotalFreedBytes: String {
        ByteFormatter.string(fromByteCount: totalFreedBytes)
    }

    public func summary(for monthDay: MonthDay) -> DaySliceSummary {
        daySummariesByMonthDay[monthDay] ?? DaySliceSummary(
            monthDay: monthDay,
            totalCount: 0,
            newUnreviewedCount: 0,
            status: .empty
        )
    }

    public func summaries(forMonth month: Int) -> [DaySliceSummary] {
        MonthDay.days(forMonth: month).map { summary(for: $0) }
    }

    // MARK: - Cold Start Bootstrap & Empty Album Sweep (FR-1, FR-2.1, FR-6.4)

    public func performInitialLoadIfNeeded() async {
        guard !hasPerformedColdStartLoad, !isBootstrapping else { return }
        await bootstrap()
    }

    public func bootstrap() async {
        guard !isBootstrapping else { return }
        isBootstrapping = true
        defer { isBootstrapping = false }

        var status = permissionService.refreshStatus()
        if status == .notDetermined {
            status = await permissionService.requestAuthorization()
        }
        guard status == .authorized || status == .limited else {
            return
        }

        let includeHidden = preferences.includeHiddenAssets
        await indexer.buildIndex(includeHiddenAssets: includeHidden)
        await albumGraphService.reloadGraph(includeHiddenAssets: includeHidden)

        // EC-5: 启动时校验并剔除已在外部被删除的暂存篮资产 ID
        _ = trashBasketVM.pruneInvalidAssets(validAssetIDs: indexer.allValidAssetIDs)
        trashBasketVM.preloadFileSizesIfNeeded()

        recomputeAllDaySummaries()
        daySliceVM.reloadCurrentDayAssets()
        albumMembershipVM.recomputeRows()

        hasPerformedColdStartLoad = true

        // FR-6.4 时机 1：冷启动完成首次图库与相簿索引后，若开启了自动清理则即刻扫描并清理历史空普通相簿与空文件夹
        if preferences.autoDeleteEmptyAlbums {
            let deletedEmptyCount = await runEmptyCollectionSweep()
            if deletedEmptyCount > 0 {
                showToast("启动自检已自动清理 \(deletedEmptyCount) 个空相簿/空文件夹", kind: .info)
            }
        }
    }

    @discardableResult
    public func runEmptyCollectionSweep() async -> Int {
        guard preferences.autoDeleteEmptyAlbums else { return 0 }
        let result = await emptySweeper.sweepEmptyAlbumsAndFolders(albumGraphService: albumGraphService)
        let deletedCount = result.totalDeletedCount
        if deletedCount > 0 {
            if let updated = try? stateStore.recordEmptyAlbumsDeleted(deletedCount) {
                self.lifetimeStats = updated.lifetimeStats
            }
            albumMembershipVM.recomputeRows()
        }
        return deletedCount
    }

    private func handleExternalLibraryChange() async {
        await albumGraphService.reloadGraph(includeHiddenAssets: preferences.includeHiddenAssets)
        _ = trashBasketVM.pruneInvalidAssets(validAssetIDs: indexer.allValidAssetIDs)
        recomputeAllDaySummaries()
        daySliceVM.reloadCurrentDayAssets(triggerSimilarityAnalysis: false)
        albumMembershipVM.recomputeRows()
    }

    public func recomputeAllDaySummaries() {
        let persisted = stateStore.load()
        self.lifetimeStats = persisted.lifetimeStats
        let computed = indexer.computeAllDaySummaries(reviewedAssetIDsByDay: persisted.reviewedAssetIDsByDay)
        self.daySummaries = computed
        self.daySummariesByMonthDay = Dictionary(uniqueKeysWithValues: computed.map { ($0.monthDay, $0) })
    }

    // MARK: - Calendar Navigation Actions (FR-2.1, SPEC §2.4)

    public func selectDay(_ monthDay: MonthDay) {
        daySliceVM.selectMonthDay(monthDay)
    }

    /// 【今天】按钮：切回系统当前月-日
    public func selectToday() {
        daySliceVM.selectMonthDay(MonthDay.today())
    }

    /// 【下一待整理日 (`⌘]`)】：向后查找第一个状态为 `.pending` 或 `.hasNew` 的月-日（自动跨年环绕跳过 `.empty` 与 `.completed`）
    @discardableResult
    public func selectNextPendingDay() -> MonthDay? {
        let allDays = MonthDay.all366Days
        guard let currentIndex = allDays.firstIndex(of: daySliceVM.selectedMonthDay) else {
            return nil
        }

        let count = allDays.count
        for offset in 1...count {
            let candidate = allDays[(currentIndex + offset) % count]
            let status = summary(for: candidate).status
            if status == .pending || status == .hasNew {
                daySliceVM.selectMonthDay(candidate)
                return candidate
            }
        }
        showToast("太棒了！全部有照片的日期均已整理完成", kind: .success)
        return nil
    }

    /// 【前一个有照片日 (`⌘[`)】：向前查找第一个非 `.empty` 的月-日
    @discardableResult
    public func selectPreviousNonEmptyDay() -> MonthDay? {
        let allDays = MonthDay.all366Days
        guard let currentIndex = allDays.firstIndex(of: daySliceVM.selectedMonthDay) else {
            return nil
        }

        let count = allDays.count
        for offset in 1...count {
            let idx = (currentIndex - offset + count) % count
            let candidate = allDays[idx]
            if summary(for: candidate).status != .empty {
                daySliceVM.selectMonthDay(candidate)
                return candidate
            }
        }
        return nil
    }

    // MARK: - Preferences & Reset Actions (FR-8.2)

    private func handlePreferencesChanged(from oldValue: UserPreferences, to newValue: UserPreferences) {
        _ = try? stateStore.updatePreferences(newValue)

        if oldValue.includeHiddenAssets != newValue.includeHiddenAssets {
            Task { @MainActor in
                await indexer.updateIncludeHiddenAssets(newValue.includeHiddenAssets)
                await albumGraphService.reloadGraph(includeHiddenAssets: newValue.includeHiddenAssets)
                recomputeAllDaySummaries()
                daySliceVM.reloadCurrentDayAssets()
                albumMembershipVM.recomputeRows()
            }
        } else if oldValue.similaritySensitivity != newValue.similaritySensitivity {
            daySliceVM.reloadCurrentDayAssets(triggerSimilarityAnalysis: true)
        }
    }

    public func updatePreferences(_ mutate: (inout UserPreferences) -> Void) {
        var copy = preferences
        mutate(&copy)
        preferences = copy
    }

    public func resetAllReviewProgress() {
        if let updated = try? stateStore.resetAllReviewProgress() {
            self.lifetimeStats = updated.lifetimeStats
        }
        recomputeAllDaySummaries()
        daySliceVM.reloadCurrentDayAssets(triggerSimilarityAnalysis: false)
        showToast("已清空所有日期的整理进度记录", kind: .info)
    }

    public func resetLifetimeStats() {
        if let updated = try? stateStore.resetLifetimeStats() {
            self.lifetimeStats = updated.lifetimeStats
        }
        showToast("已重置累计清理统计数据", kind: .info)
    }

    // MARK: - Non-Blocking Toast Queue

    public func showToast(
        _ message: String,
        kind: ToastKind = .info,
        durationSeconds: TimeInterval = 3.2
    ) {
        let toast = ToastMessage(
            message: message,
            kind: kind,
            durationSeconds: durationSeconds
        )
        toastQueue.append(toast)
        if toastQueue.count > 3 {
            let removed = toastQueue.removeFirst()
            toastDismissTasks[removed.id]?.cancel()
            toastDismissTasks.removeValue(forKey: removed.id)
        }

        let toastID = toast.id
        let task = Task { @MainActor [weak self] in
            try? await Task.sleep(nanoseconds: UInt64(max(0.5, durationSeconds) * 1_000_000_000))
            guard !Task.isCancelled else { return }
            self?.dismissToast(id: toastID)
        }
        toastDismissTasks[toastID] = task
    }

    public func dismissToast(id: UUID? = nil) {
        if let id {
            toastDismissTasks[id]?.cancel()
            toastDismissTasks.removeValue(forKey: id)
            toastQueue.removeAll { $0.id == id }
        } else if let last = toastQueue.popLast() {
            toastDismissTasks[last.id]?.cancel()
            toastDismissTasks.removeValue(forKey: last.id)
        }
    }

    // MARK: - Global Keyboard Shortcut Dispatcher (SPEC §2.4)

    /// 供 `KeyboardShortcutMonitor` 统一分发的快捷键处理入口（返回 true 表示已消费该按键）
    @discardableResult
    public func handleShortcut(_ action: ShortcutAction) -> Bool {
        switch action {
        case .toggleQuickReview:
            guard !similarityVM.isComparePresented else { return false }
            daySliceVM.toggleQuickReview()
            return true

        case .exitOverlay:
            if similarityVM.isComparePresented {
                similarityVM.closeCompare()
                return true
            }
            if trashBasketVM.isDrawerPresented {
                trashBasketVM.isDrawerPresented = false
                return true
            }
            if daySliceVM.workspaceMode == .quickReview {
                daySliceVM.exitQuickReview()
                return true
            }
            if !daySliceVM.selectedAssetIDs.isEmpty {
                daySliceVM.clearSelection()
                return true
            }
            return false

        case .movePrevious:
            guard !similarityVM.isComparePresented else { return false }
            daySliceVM.moveCursorPrevious()
            return true

        case .moveNext:
            guard !similarityVM.isComparePresented else { return false }
            daySliceVM.moveCursorNext()
            return true

        case .toggleTrash:
            if similarityVM.isComparePresented {
                _ = similarityVM.keepSelectedAndTrashOthers()
                return true
            }
            daySliceVM.toggleTrashForSelection()
            return true

        case .toggleFavorite:
            Task { @MainActor in
                await daySliceVM.toggleFavoriteForSelection()
            }
            return true

        case .playLivePhoto:
            guard daySliceVM.workspaceMode == .quickReview else { return false }
            daySliceVM.triggerLivePhotoPlayback()
            return true

        case .openCompare:
            let selected = daySliceVM.selectedAssets
            if selected.count >= 2 {
                similarityVM.openCompare(assets: selected)
                return true
            }
            if let single = selected.first ?? daySliceVM.activeReviewAsset,
               let group = similarityVM.group(forAssetID: single.id) {
                similarityVM.openCompare(for: group, allDayAssets: daySliceVM.allDayAssets)
                return true
            }
            showToast("请先选择至少 2 张照片或点击相似组进行对比", kind: .info)
            return true

        case .toggleInspector:
            daySliceVM.toggleInspector()
            return true

        case .undoTrash:
            return trashBasketVM.undoLastAction()

        case .commitDeletions:
            guard !trashBasketVM.isEmpty else { return false }
            Task { @MainActor in
                _ = await trashBasketVM.commitDeletions()
            }
            return true

        case .previousNonEmptyDay:
            _ = selectPreviousNonEmptyDay()
            return true

        case .nextPendingDay:
            _ = selectNextPendingDay()
            return true
        }
    }
}
