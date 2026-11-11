import Foundation
import XCTest
@testable import PhotoCleaner

@MainActor
final class FavoriteProtectionTests: XCTestCase {
    private var tempDirectoryURL: URL!
    private var stateStore: LocalStateStore!
    private var trashBasketVM: TrashBasketViewModel!
    private var similarityVM: SimilarityCompareViewModel!
    private var albumMembershipVM: AlbumMembershipViewModel!
    private var daySliceVM: DaySliceViewModel!

    override func setUp() async throws {
        try await super.setUp()
        tempDirectoryURL = FileManager.default.temporaryDirectory
            .appendingPathComponent("PhotoCleanerTests-Fav-\(UUID().uuidString)", isDirectory: true)
        stateStore = LocalStateStore(directoryURL: tempDirectoryURL)
        trashBasketVM = TrashBasketViewModel(stateStore: stateStore)
        similarityVM = SimilarityCompareViewModel(trashBasketVM: trashBasketVM)
        albumMembershipVM = AlbumMembershipViewModel(stateStore: stateStore)
        daySliceVM = DaySliceViewModel(
            initialMonthDay: MonthDay(month: 8, day: 13),
            stateStore: stateStore,
            trashBasketVM: trashBasketVM,
            similarityVM: similarityVM,
            albumMembershipVM: albumMembershipVM
        )
    }

    override func tearDown() async throws {
        if let tempDirectoryURL {
            try? FileManager.default.removeItem(at: tempDirectoryURL)
        }
        try await super.tearDown()
    }

    func testFavoriteLockSkipsOnSingleBatchAndSimilarityDelete() {
        var capturedToasts: [(String, ToastKind)] = []
        trashBasketVM.onToast = { message, kind in
            capturedToasts.append((message, kind))
        }

        let favAsset1 = AssetItem(id: "fav-1", isFavorite: true, fileSizeByteCount: 5_000_000)
        let favAsset2 = AssetItem(id: "fav-2", isFavorite: true, fileSizeByteCount: 6_000_000)
        let normalAsset1 = AssetItem(id: "norm-1", isFavorite: false, fileSizeByteCount: 10_000_000)
        let normalAsset2 = AssetItem(id: "norm-2", isFavorite: false, fileSizeByteCount: 20_000_000)

        // 1. 单删收藏照片 -> 0 张入篮，跳过 1 张并触发精确 Toast
        let singleResult = trashBasketVM.addAsset(favAsset1)
        XCTAssertTrue(singleResult.addedAssetIDs.isEmpty)
        XCTAssertEqual(singleResult.skippedFavoriteAssetIDs, ["fav-1"])
        XCTAssertTrue(trashBasketVM.isEmpty)
        XCTAssertEqual(
            capturedToasts.last?.0,
            "已自动跳过 1 张收藏 ❤️ 照片；如需删除请先按 F 取消收藏"
        )

        // 2. 批量框选混有 2 张收藏 + 1 张普通 -> 仅普通入篮，跳过 2 张收藏
        let batchResult = trashBasketVM.addAssets([favAsset1, favAsset2, normalAsset1])
        XCTAssertEqual(batchResult.addedAssetIDs, ["norm-1"])
        XCTAssertEqual(batchResult.skippedFavoriteAssetIDs, ["fav-1", "fav-2"])
        XCTAssertEqual(trashBasketVM.trashedAssetIDs, ["norm-1"])
        XCTAssertEqual(
            capturedToasts.last?.0,
            "已自动跳过 2 张收藏 ❤️ 照片；如需删除请先按 F 取消收藏"
        )

        // 3. 相似组对比视图“保留已选，其余移入待删篮”：未保留项中包含 favAsset1 与 normalAsset2
        similarityVM.openCompare(assets: [normalAsset1, favAsset1, normalAsset2])
        similarityVM.selectOnlyKept(assetID: "norm-1")
        let compareResult = similarityVM.keepSelectedAndTrashOthers()

        XCTAssertEqual(compareResult.addedAssetIDs, ["norm-2"])
        XCTAssertEqual(compareResult.skippedFavoriteAssetIDs, ["fav-1"])
        XCTAssertEqual(trashBasketVM.trashedAssetIDs, ["norm-1", "norm-2"])
        XCTAssertEqual(trashBasketVM.estimatedFreedBytes, 30_000_000)
    }

    func testReadOnlySharedLibraryAssetProtection() {
        var lastToast: String?
        trashBasketVM.onToast = { message, _ in
            lastToast = message
        }

        let readOnlyAsset = AssetItem(id: "shared-readonly", isFavorite: false, canDelete: false)
        let result = trashBasketVM.addAsset(readOnlyAsset)

        XCTAssertTrue(result.addedAssetIDs.isEmpty)
        XCTAssertEqual(result.skippedReadOnlyAssetIDs, ["shared-readonly"])
        XCTAssertEqual(lastToast, "该共享图库项目不支持由当前账号删除")
    }

    func testAutoRemovalFromTrashBasketWhenFavorited() async {
        let asset1 = AssetItem(id: "photo-1", year: 2025, monthDay: MonthDay(month: 8, day: 13), isFavorite: false)
        let asset2 = AssetItem(id: "photo-2", year: 2025, monthDay: MonthDay(month: 8, day: 13), isFavorite: false)

        daySliceVM.setDayAssetsForTesting([asset1, asset2], monthDay: MonthDay(month: 8, day: 13))
        daySliceVM.customFavoriteExecutor = { _, _ in }

        // 先将两张普通照片放入待删暂存篮
        _ = trashBasketVM.addAssets([asset1, asset2])
        XCTAssertEqual(trashBasketVM.trashedAssetIDs, ["photo-1", "photo-2"])

        // 对已在暂存篮中的 photo-1 按 F 点亮收藏 ❤️ -> 必须自动从待删暂存篮移出 (FR-7.1)
        await daySliceVM.toggleFavorite(for: "photo-1")

        XCTAssertFalse(trashBasketVM.isTrashed("photo-1"))
        XCTAssertEqual(trashBasketVM.trashedAssetIDs, ["photo-2"])
        XCTAssertEqual(stateStore.load().trashBasketAssetIDs, ["photo-2"])
    }

    func testFiftyStepUndoStackAndPersistence() {
        // 连续执行 55 次添加操作（超过 50 步上限）
        for i in 1...55 {
            let item = AssetItem(id: "item-\(i)", isFavorite: false, fileSizeByteCount: 1024)
            _ = trashBasketVM.addAsset(item)
        }

        XCTAssertEqual(trashBasketVM.count, 55)
        XCTAssertEqual(trashBasketVM.undoDepth, TrashBasketViewModel.maxUndoSteps)

        // 连续撤销 50 次
        for step in 1...50 {
            let didUndo = trashBasketVM.undoLastAction()
            XCTAssertTrue(didUndo, "第 \(step) 步撤销应成功")
        }

        // 55 - 50 = 剩余最早的 5 项，且撤销栈已空
        XCTAssertEqual(trashBasketVM.count, 5)
        XCTAssertEqual(trashBasketVM.trashedAssetIDs, ["item-1", "item-2", "item-3", "item-4", "item-5"])
        XCTAssertFalse(trashBasketVM.canUndo)
        XCTAssertFalse(trashBasketVM.undoLastAction())
        XCTAssertEqual(stateStore.load().trashBasketAssetIDs, ["item-1", "item-2", "item-3", "item-4", "item-5"])
    }

    func testExternalDeletionPruningAndCommitLifecycle() async {
        let a1 = AssetItem(id: "a1", isFavorite: false, fileSizeByteCount: 10_000_000)
        let a2 = AssetItem(id: "a2", isFavorite: false, fileSizeByteCount: 25_000_000)
        let a3 = AssetItem(id: "a3", isFavorite: false, fileSizeByteCount: 40_000_000)
        _ = trashBasketVM.addAssets([a1, a2, a3])

        // EC-5: a2 在外部 Photos.app 被删，仅 a1 与 a3 仍在有效集中
        let pruned = trashBasketVM.pruneInvalidAssets(validAssetIDs: ["a1", "a3", "a99"])
        XCTAssertEqual(pruned, ["a2"])
        XCTAssertEqual(trashBasketVM.trashedAssetIDs, ["a1", "a3"])
        XCTAssertEqual(trashBasketVM.estimatedFreedBytes, 50_000_000)

        // FR-7.3 & FR-6.4: 提交批量删除 -> 单次调用执行器、累加统计并触发空相簿清理
        var committedIDs: [[String]] = []
        var didSweepEmptyAlbums = false

        trashBasketVM.customDeleteExecutor = { ids in
            committedIDs.append(ids)
        }
        trashBasketVM.onTriggerEmptyAlbumSweep = {
            didSweepEmptyAlbums = true
            return 2
        }

        let success = await trashBasketVM.commitDeletions()
        XCTAssertTrue(success)
        XCTAssertEqual(committedIDs, [["a1", "a3"]])
        XCTAssertTrue(trashBasketVM.isEmpty)
        XCTAssertTrue(didSweepEmptyAlbums)

        let persisted = stateStore.load()
        XCTAssertTrue(persisted.trashBasketAssetIDs.isEmpty)
        XCTAssertEqual(persisted.lifetimeStats.totalDeletedAssetCount, 2)
        XCTAssertEqual(persisted.lifetimeStats.totalFreedBytes, 50_000_000)
        XCTAssertEqual(persisted.lifetimeStats.totalEmptyAlbumsDeleted, 2)
    }
}
