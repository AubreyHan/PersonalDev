import Foundation
import XCTest
@testable import PhotoCleaner

@MainActor
final class AlbumMembershipStateTests: XCTestCase {
    private var tempDirectoryURL: URL!
    private var stateStore: LocalStateStore!
    private var graphService: AlbumGraphService!
    private var sweeper: EmptyCollectionSweeper!
    private var viewModel: AlbumMembershipViewModel!

    override func setUp() async throws {
        try await super.setUp()
        tempDirectoryURL = FileManager.default.temporaryDirectory
            .appendingPathComponent("PhotoCleanerTests-Album-\(UUID().uuidString)", isDirectory: true)
        stateStore = LocalStateStore(directoryURL: tempDirectoryURL)
        graphService = AlbumGraphService()
        sweeper = EmptyCollectionSweeper()
        viewModel = AlbumMembershipViewModel(
            albumGraphService: graphService,
            emptySweeper: sweeper,
            stateStore: stateStore
        )
    }

    override func tearDown() async throws {
        if let tempDirectoryURL {
            try? FileManager.default.removeItem(at: tempDirectoryURL)
        }
        try await super.tearDown()
    }

    func testSingleSelectionThreeStateCalculationAndSorting() {
        let albums = [
            UserAlbumNode(id: "album-japan", title: "2025日本", breadcrumbPath: "旅行 / 2025日本", parentFolderID: "folder-travel", assetCount: 2),
            UserAlbumNode(id: "album-family", title: "家庭聚会", breadcrumbPath: "家庭 / 家庭聚会", parentFolderID: "folder-family", assetCount: 1),
            UserAlbumNode(id: "album-work", title: "工作资料", breadcrumbPath: "工作资料", parentFolderID: nil, assetCount: 1)
        ]
        let albumToAssets: [String: Set<String>] = [
            "album-japan": ["asset-1", "asset-2"],
            "album-family": ["asset-1"],
            "album-work": ["asset-3"]
        ]
        graphService.replaceGraphForTesting(albums: albums, albumToAssets: albumToAssets)

        viewModel.updateSelection(["asset-1"])

        XCTAssertEqual(viewModel.belongingRows.count, 2)
        XCTAssertEqual(viewModel.notJoinedRows.count, 1)

        // 按 breadcrumbPath 升序排列："家庭 / 家庭聚会" 在 "旅行 / 2025日本" 之前
        XCTAssertEqual(viewModel.belongingRows[0].album.id, "album-family")
        XCTAssertEqual(viewModel.belongingRows[0].relation, .allSelectedInAlbum)
        XCTAssertEqual(viewModel.belongingRows[1].album.id, "album-japan")
        XCTAssertEqual(viewModel.belongingRows[1].relation, .allSelectedInAlbum)

        XCTAssertEqual(viewModel.notJoinedRows[0].album.id, "album-work")
        XCTAssertEqual(viewModel.notJoinedRows[0].relation, .noneSelectedInAlbum)
    }

    func testMultiSelectionAllPartialAndNoneOrderingAndSearchFilter() {
        let albums = [
            UserAlbumNode(id: "album-b-partial", title: "B相簿", breadcrumbPath: "分类 / B相簿", assetCount: 1),
            UserAlbumNode(id: "album-c-all", title: "C相簿", breadcrumbPath: "分类 / C相簿", assetCount: 3),
            UserAlbumNode(id: "album-a-all", title: "A相簿", breadcrumbPath: "分类 / A相簿", assetCount: 2),
            UserAlbumNode(id: "album-d-none", title: "D相簿", breadcrumbPath: "其他 / D相簿", assetCount: 5)
        ]
        let albumToAssets: [String: Set<String>] = [
            "album-a-all": ["a1", "a2"],
            "album-c-all": ["a1", "a2", "a3"],
            "album-b-partial": ["a1"],
            "album-d-none": ["a9"]
        ]
        graphService.replaceGraphForTesting(albums: albums, albumToAssets: albumToAssets)

        // 选中 a1 与 a2：
        // - album-a-all 与 album-c-all 为 allSelectedInAlbum [✓]（应排在所属相簿最前）
        // - album-b-partial 为 partiallySelectedInAlbum [-]（1/2，排在 allSelectedInAlbum 之后）
        // - album-d-none 为 noneSelectedInAlbum [ ]（排在未加入相簿）
        viewModel.updateSelection(["a1", "a2"])

        XCTAssertEqual(viewModel.belongingRows.map(\.album.id), ["album-a-all", "album-c-all", "album-b-partial"])
        XCTAssertEqual(viewModel.belongingRows[0].relation, .allSelectedInAlbum)
        XCTAssertEqual(viewModel.belongingRows[1].relation, .allSelectedInAlbum)
        XCTAssertEqual(viewModel.belongingRows[2].relation, .partiallySelectedInAlbum)
        XCTAssertEqual(viewModel.belongingRows[2].matchedSelectedCount, 1)
        XCTAssertEqual(viewModel.belongingRows[2].totalSelectedCount, 2)

        XCTAssertEqual(viewModel.notJoinedRows.map(\.album.id), ["album-d-none"])
        XCTAssertEqual(viewModel.notJoinedRows[0].relation, .noneSelectedInAlbum)

        // 测试大小写不敏感路径搜索过滤
        viewModel.searchQuery = "b相簿"
        XCTAssertEqual(viewModel.belongingRows.map(\.album.id), ["album-b-partial"])
        XCTAssertTrue(viewModel.notJoinedRows.isEmpty)

        viewModel.searchQuery = "其他"
        XCTAssertTrue(viewModel.belongingRows.isEmpty)
        XCTAssertEqual(viewModel.notJoinedRows.map(\.album.id), ["album-d-none"])
    }

    func testToggleDeltaComputationAndEmptySweepTriggerOnRemove() async {
        let selected: Set<String> = ["a1", "a2", "a3"]
        let existingPartial: Set<String> = ["a1"]

        // 1. partiallySelectedInAlbum -> 仅补齐差集 S \ M_A = {"a2", "a3"}
        let partialAction = AlbumMembershipViewModel.computeToggleAction(
            relation: .partiallySelectedInAlbum,
            selectedAssetIDs: selected,
            existingAlbumAssetIDs: existingPartial
        )
        XCTAssertEqual(partialAction, .add(assetIDs: ["a2", "a3"]))

        // 2. noneSelectedInAlbum -> 全部加入 {"a1", "a2", "a3"}
        let noneAction = AlbumMembershipViewModel.computeToggleAction(
            relation: .noneSelectedInAlbum,
            selectedAssetIDs: selected,
            existingAlbumAssetIDs: []
        )
        XCTAssertEqual(noneAction, .add(assetIDs: ["a1", "a2", "a3"]))

        // 3. allSelectedInAlbum -> 全部移出 {"a1", "a2", "a3"}
        let allAction = AlbumMembershipViewModel.computeToggleAction(
            relation: .allSelectedInAlbum,
            selectedAssetIDs: selected,
            existingAlbumAssetIDs: ["a1", "a2", "a3", "a4"]
        )
        XCTAssertEqual(allAction, .remove(assetIDs: ["a1", "a2", "a3"]))

        // 4. 验证将相簿中最后一张照片移出时立即触发 onTriggerEmptyAlbumSweep (FR-6.4)
        let singleAlbum = UserAlbumNode(id: "album-last", title: "测试空相簿", breadcrumbPath: "测试空相簿", assetCount: 1)
        graphService.replaceGraphForTesting(albums: [singleAlbum], albumToAssets: ["album-last": ["a1"]])
        viewModel.updateSelection(["a1"])

        var didTriggerSweep = false
        viewModel.customToggleExecutor = { [weak self] row, action in
            XCTAssertEqual(action, .remove(assetIDs: ["a1"]))
            self?.graphService.replaceGraphForTesting(
                albums: [UserAlbumNode(id: row.album.id, title: row.album.title, assetCount: 0)],
                albumToAssets: [row.album.id: []]
            )
            return 0 // 移出后剩余 0 张
        }
        viewModel.onTriggerEmptyAlbumSweep = {
            didTriggerSweep = true
            return 1
        }

        if let firstRow = viewModel.belongingRows.first {
            await viewModel.toggleMembership(for: firstRow)
        }
        XCTAssertTrue(didTriggerSweep, "当相簿移出资产后剩余 0 张且开启自动清理时，必须立即触发 EmptyCollectionSweeper")
    }

    func testBottomUpEmptyFolderCascadeAndHiddenPhotoProtection() {
        // 构造 3 层嵌套文件夹结构：
        // RootFolderA
        //   └── SubFolderA1
        //         └── LeafFolderA2
        //               └── AlbumEmpty1 (0 张可见, 0 张隐藏 -> 应被删除)
        // RootFolderB
        //   └── AlbumHiddenOnly (0 张可见, 但有 2 张已隐藏照片 EC-2 -> 绝不能误删!)
        // RootFolderC
        //   └── SubFolderC1 (含空相簿 AlbumEmpty2 -> 应被删)
        //   └── AlbumNonEmpty (含 3 张可见照片 -> 保留，故 RootFolderC 保留)
        let albums = [
            UserAlbumNode(id: "AlbumEmpty1", title: "空相簿1", parentFolderID: "LeafFolderA2", assetCount: 0, canDelete: true),
            UserAlbumNode(id: "AlbumHiddenOnly", title: "含隐藏照相簿", parentFolderID: "RootFolderB", assetCount: 0, canDelete: true),
            UserAlbumNode(id: "AlbumEmpty2", title: "空相簿2", parentFolderID: "SubFolderC1", assetCount: 0, canDelete: true),
            UserAlbumNode(id: "AlbumNonEmpty", title: "非空相簿", parentFolderID: "RootFolderC", assetCount: 3, canDelete: true)
        ]

        let folders = [
            UserFolderNode(id: "RootFolderA", title: "A", parentFolderID: nil, childFolderIDs: ["SubFolderA1"], childAlbumIDs: [], canDelete: true),
            UserFolderNode(id: "SubFolderA1", title: "A1", parentFolderID: "RootFolderA", childFolderIDs: ["LeafFolderA2"], childAlbumIDs: [], canDelete: true),
            UserFolderNode(id: "LeafFolderA2", title: "A2", parentFolderID: "SubFolderA1", childFolderIDs: [], childAlbumIDs: ["AlbumEmpty1"], canDelete: true),
            UserFolderNode(id: "RootFolderB", title: "B", parentFolderID: nil, childFolderIDs: [], childAlbumIDs: ["AlbumHiddenOnly"], canDelete: true),
            UserFolderNode(id: "RootFolderC", title: "C", parentFolderID: nil, childFolderIDs: ["SubFolderC1"], childAlbumIDs: ["AlbumNonEmpty"], canDelete: true),
            UserFolderNode(id: "SubFolderC1", title: "C1", parentFolderID: "RootFolderC", childFolderIDs: [], childAlbumIDs: ["AlbumEmpty2"], canDelete: true)
        ]

        let plan = EmptyCollectionSweeper.resolveBottomUpSweepPlan(
            albums: albums,
            folders: folders,
            hiddenAssetCountsByAlbumID: ["AlbumHiddenOnly": 2] // EC-2: 含有 2 张隐藏照片
        )

        // 验证 Step 1：仅删除真正 0 资产的 AlbumEmpty1 与 AlbumEmpty2；AlbumHiddenOnly 被安全保护！
        XCTAssertEqual(Set(plan.emptyAlbumIDs), ["AlbumEmpty1", "AlbumEmpty2"])
        XCTAssertTrue(plan.remainingAlbums.map(\.id).contains("AlbumHiddenOnly"))
        XCTAssertTrue(plan.remainingAlbums.map(\.id).contains("AlbumNonEmpty"))

        // 验证 Step 2：自底向上按深度轮次递归删除空文件夹
        // Round 1: 最深层的 LeafFolderA2 (depth 2) 与 SubFolderC1 (depth 1) 变空被删
        // Round 2: SubFolderA1 (depth 1) 因 LeafFolderA2 被删而变空被删
        // Round 3: RootFolderA (depth 0) 因 SubFolderA1 被删而变空被删
        // RootFolderB (含 AlbumHiddenOnly) 与 RootFolderC (含 AlbumNonEmpty) 均存活！
        XCTAssertEqual(plan.emptyFolderRounds.count, 3)
        XCTAssertEqual(plan.emptyFolderRounds[0], ["LeafFolderA2", "SubFolderC1"])
        XCTAssertEqual(plan.emptyFolderRounds[1], ["SubFolderA1"])
        XCTAssertEqual(plan.emptyFolderRounds[2], ["RootFolderA"])
        XCTAssertEqual(Set(plan.remainingFolders.map(\.id)), ["RootFolderB", "RootFolderC"])
    }

    func testUserCancelledSessionIgnoreProtection() {
        let albums = [
            UserAlbumNode(id: "AlbumEmptyIgnored", title: "用户取消删除的空相簿", assetCount: 0, canDelete: true)
        ]
        let plan = EmptyCollectionSweeper.resolveBottomUpSweepPlan(
            albums: albums,
            folders: [],
            ignoredAlbumIDs: ["AlbumEmptyIgnored"] // EC-3
        )
        XCTAssertTrue(plan.emptyAlbumIDs.isEmpty, "被记入 sessionIgnoredEmptyAlbumIDs 的相簿在本会话内不得重复触发删除")
    }
}
