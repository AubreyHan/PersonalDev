import XCTest
@testable import PhotoCleaner

final class MonthDayProgressTests: XCTestCase {

    // MARK: - 1. 366 天全量生成与闰日 02-29 验证 (EC-1)

    func testAll366DaysGenerationContainsLeapDayAndIsSorted() {
        let days = MonthDay.all366Days
        XCTAssertEqual(days.count, 366, "全年固定切片必须为 366 天（含 02-29）")

        let leapDay = MonthDay(month: 2, day: 29)
        XCTAssertTrue(days.contains(leapDay), "必须永久包含闰日 02-29 (EC-1)")
        XCTAssertEqual(leapDay.storageKey, "02-29")
        XCTAssertEqual(leapDay.displayTitle, "2月29日")

        XCTAssertEqual(days.first, MonthDay(month: 1, day: 1))
        XCTAssertEqual(days.last, MonthDay(month: 12, day: 31))

        // 验证严格递增排序与唯一性
        let uniqueKeys = Set(days.map(\.storageKey))
        XCTAssertEqual(uniqueKeys.count, 366)
        for i in 1..<days.count {
            XCTAssertLessThan(days[i - 1], days[i])
        }
    }

    func testMonthDayStorageKeyParsingAndValidation() {
        let aug13 = MonthDay(storageKey: "08-13")
        XCTAssertEqual(aug13, MonthDay(month: 8, day: 13))

        let feb29 = MonthDay(storageKey: "02-29")
        XCTAssertEqual(feb29, MonthDay(month: 2, day: 29))

        XCTAssertNil(MonthDay(storageKey: "02-30"), "2月30日应为非法日期")
        XCTAssertNil(MonthDay(storageKey: "04-31"), "4月31日应为非法日期")
        XCTAssertNil(MonthDay(storageKey: "13-01"), "13月应为非法月份")
        XCTAssertNil(MonthDay(storageKey: "00-10"), "0月应为非法月份")
        XCTAssertNil(MonthDay(storageKey: "invalid"))
    }

    // MARK: - 2. 四态进度流转验证 (FR-2.3 / AC-1 / AC-2)

    func testEmptyStateWhenNoCurrentAssets() {
        let status1 = MonthDay.computeStatus(currentIDs: [], reviewedIDs: [])
        XCTAssertEqual(status1, .empty)

        // 即使历史 reviewedIDs 非空，只要当前该日无任何资产，状态仍为 empty
        let status2 = MonthDay.computeStatus(currentIDs: [], reviewedIDs: ["asset-1", "asset-2"])
        XCTAssertEqual(status2, .empty)
    }

    func testPendingStateWhenNeverReviewed() {
        let current: Set<String> = ["2024-A", "2025-B"]
        let status = MonthDay.computeStatus(currentIDs: current, reviewedIDs: [])
        XCTAssertEqual(status, .pending)

        let summary = MonthDay.computeSummary(
            monthDay: MonthDay(month: 8, day: 13),
            currentIDs: current,
            reviewedIDs: []
        )
        XCTAssertEqual(summary.status, .pending)
        XCTAssertEqual(summary.totalCount, 2)
        XCTAssertEqual(summary.newUnreviewedCount, 2)
    }

    func testCompletedAndHasNewTransitions() {
        let aug13 = MonthDay(month: 8, day: 13)
        let initialAssets: Set<String> = ["2023-01", "2024-01", "2025-01"]

        // 1. 标记完成当日整理：currentIDs 全部进入 reviewedIDs -> completed
        let reviewed = initialAssets
        let completedSummary = MonthDay.computeSummary(
            monthDay: aug13,
            currentIDs: initialAssets,
            reviewedIDs: reviewed
        )
        XCTAssertEqual(completedSummary.status, .completed)
        XCTAssertEqual(completedSummary.totalCount, 3)
        XCTAssertEqual(completedSummary.newUnreviewedCount, 0)

        // 2. 隔年同一天新增 1 张照片 "2026-01" -> 自动变为 hasNew (AC-2)
        var nextYearAssets = initialAssets
        nextYearAssets.insert("2026-01")
        let hasNewSummary = MonthDay.computeSummary(
            monthDay: aug13,
            currentIDs: nextYearAssets,
            reviewedIDs: reviewed
        )
        XCTAssertEqual(hasNewSummary.status, .hasNew)
        XCTAssertEqual(hasNewSummary.totalCount, 4)
        XCTAssertEqual(hasNewSummary.newUnreviewedCount, 1)

        // 3. 若将该新增废片删除，存量资产恢复为 reviewed 子集 -> 自动恢复 completed
        nextYearAssets.remove("2026-01")
        nextYearAssets.remove("2023-01") // 即使旧照片也被删掉一张，其余全已审，仍保持 completed
        let restoredSummary = MonthDay.computeSummary(
            monthDay: aug13,
            currentIDs: nextYearAssets,
            reviewedIDs: reviewed
        )
        XCTAssertEqual(restoredSummary.status, .completed)
        XCTAssertEqual(restoredSummary.totalCount, 2)
        XCTAssertEqual(restoredSummary.newUnreviewedCount, 0)
    }

    // MARK: - 3. LocalStateStore 原子持久化往返测试 (SPEC §1.4)

    func testLocalStateStoreAtomicPersistenceRoundTrip() throws {
        let tempDir = FileManager.default.temporaryDirectory
            .appendingPathComponent("PhotoCleanerTests-\(UUID().uuidString)", isDirectory: true)
        defer {
            try? FileManager.default.removeItem(at: tempDir)
        }

        let store = LocalStateStore(directoryURL: tempDir)

        // 初始空状态读取
        let initial = store.load()
        XCTAssertEqual(initial.schemaVersion, 1)
        XCTAssertTrue(initial.reviewedAssetIDsByDay.isEmpty)
        XCTAssertTrue(initial.trashBasketAssetIDs.isEmpty)
        XCTAssertFalse(initial.preferences.includeHiddenAssets)
        XCTAssertTrue(initial.preferences.autoDeleteEmptyAlbums)
        XCTAssertTrue(initial.preferences.autoAdvanceAfterMarkDelete)
        XCTAssertEqual(initial.preferences.similaritySensitivity, .standard)

        // 标记 08-13 與 02-29 已整理
        let aug13 = MonthDay(month: 8, day: 13)
        let feb29 = MonthDay(month: 2, day: 29)
        try store.markDayCompleted(aug13, assetIDs: ["A1", "A2"])
        try store.markDayCompleted(feb29, assetIDs: ["L1"])

        // 更新暂存篮与累计清理统计
        try store.updateTrashBasket(["T1", "T2"])
        try store.recordDeletion(deletedCount: 15, freedBytes: 125_829_120, emptyAlbumsDeleted: 2)
        try store.updatePreferences(
            UserPreferences(
                includeHiddenAssets: true,
                autoDeleteEmptyAlbums: true,
                autoAdvanceAfterMarkDelete: false,
                similaritySensitivity: .strict
            )
        )

        // 重新实例化一个新的 LocalStateStore 验证磁盘 JSON 持久化
        let reloadedStore = LocalStateStore(directoryURL: tempDir)
        let reloaded = reloadedStore.load()

        XCTAssertEqual(reloaded.reviewedAssetIDs(for: aug13), ["A1", "A2"])
        XCTAssertEqual(reloaded.reviewedAssetIDs(for: feb29), ["L1"])
        XCTAssertEqual(reloaded.trashBasketAssetIDs, ["T1", "T2"])
        XCTAssertEqual(reloaded.lifetimeStats.totalDeletedAssetCount, 15)
        XCTAssertEqual(reloaded.lifetimeStats.totalFreedBytes, 125_829_120)
        XCTAssertEqual(reloaded.lifetimeStats.totalEmptyAlbumsDeleted, 2)
        XCTAssertTrue(reloaded.preferences.includeHiddenAssets)
        XCTAssertFalse(reloaded.preferences.autoAdvanceAfterMarkDelete)
        XCTAssertEqual(reloaded.preferences.similaritySensitivity, .strict)

        // 验证重置单日进度
        try reloadedStore.resetDayProgress(aug13)
        XCTAssertTrue(reloadedStore.load().reviewedAssetIDs(for: aug13).isEmpty)
        XCTAssertEqual(reloadedStore.load().reviewedAssetIDs(for: feb29), ["L1"])
    }

    func testByteFormatterOutputs() {
        let formatted = ByteFormatter.string(fromByteCount: 25_000_000)
        XCTAssertFalse(formatted.isEmpty)
        XCTAssertEqual(ByteFormatter.format(nil, placeholder: "未知"), "未知")
    }
}
