import Foundation
import Observation

/// 右侧检视栏“相簿归属面板”状态与三态交互 ViewModel（FR-6.1 ~ FR-6.4, SPEC §2.1, EC-7）
@MainActor
@Observable
public final class AlbumMembershipViewModel {
    // MARK: - Observable State

    /// 当前选中的资产 ID 集合
    public var selectedAssetIDs: Set<String> = [] {
        didSet {
            if selectedAssetIDs != oldValue {
                recomputeRows()
            }
        }
    }

    /// 顶部实时相簿路径模糊搜索词
    public var searchQuery: String = "" {
        didSet {
            if searchQuery != oldValue {
                recomputeRows()
            }
        }
    }

    /// 内联新建相簿输入框标题
    public var newAlbumTitle: String = ""

    /// 内联新建相簿可选父文件夹 ID（nil 表示顶层）
    public var selectedParentFolderID: String? = nil

    /// 【所属相簿】区块行列表（`allSelectedInAlbum` 在前，`partiallySelectedInAlbum` 在后，同级按 `breadcrumbPath` 升序）
    public private(set) var belongingRows: [AlbumMembershipRow] = []

    /// 【未加入相簿】区块行列表（`noneSelectedInAlbum`，按 `breadcrumbPath` 升序）
    public private(set) var notJoinedRows: [AlbumMembershipRow] = []

    /// 是否正在执行 PhotoKit 相簿变更事务
    public private(set) var isMutating: Bool = false

    // MARK: - Dependencies & Callbacks

    public let albumGraphService: AlbumGraphService
    public let emptySweeper: EmptyCollectionSweeper
    public let stateStore: LocalStateStore

    public var onToast: ((String, ToastKind) -> Void)?
    public var onTriggerEmptyAlbumSweep: (@MainActor () async -> Int)?
    public var onMembershipChanged: (@MainActor () -> Void)?

    /// 可选自定义切换执行器（便于单元测试拦截验证）
    public var customToggleExecutor: (@MainActor (AlbumMembershipRow, AlbumGraphService.MembershipToggleAction) async throws -> Int)?

    public init(
        albumGraphService: AlbumGraphService = .shared,
        emptySweeper: EmptyCollectionSweeper = .shared,
        stateStore: LocalStateStore = .shared
    ) {
        self.albumGraphService = albumGraphService
        self.emptySweeper = emptySweeper
        self.stateStore = stateStore
        recomputeRows()
    }

    // MARK: - Computed Helpers

    public var hasSelection: Bool {
        !selectedAssetIDs.isEmpty
    }

    public var selectedCount: Int {
        selectedAssetIDs.count
    }

    public var belongingCount: Int {
        belongingRows.count
    }

    public var notJoinedCount: Int {
        notJoinedRows.count
    }

    public var availableFolders: [UserFolderNode] {
        albumGraphService.folders
    }

    public var allAlbums: [UserAlbumNode] {
        albumGraphService.albums
    }

    /// 当搜索词非空且不存在完全同名相簿时，允许一键回车新建该搜索词名称的相簿
    public var canQuickCreateFromSearchQuery: Bool {
        let trimmed = searchQuery.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmed.isEmpty, !selectedAssetIDs.isEmpty else { return false }
        return albumGraphService.findExistingAlbum(title: trimmed, parentFolderID: selectedParentFolderID) == nil
    }

    // MARK: - Selection & Graph Sync

    public func updateSelection(_ assetIDs: Set<String>) {
        self.selectedAssetIDs = assetIDs
    }

    public func refreshFromGraph() {
        recomputeRows()
    }

    public func recomputeRows() {
        let computed = albumGraphService.computeMembershipRows(
            for: selectedAssetIDs,
            searchQuery: searchQuery
        )
        self.belongingRows = computed.belonging
        self.notJoinedRows = computed.notJoined
    }

    public func albumCount(forAssetID assetID: String) -> Int {
        albumGraphService.albumCount(forAssetID: assetID)
    }

    // MARK: - Toggle Membership & Empty Album Sweep (SPEC §2.1 & FR-6.4)

    /// 点击相簿行切换归属关系：
    /// - 若为 `.noneSelectedInAlbum` 或 `.partiallySelectedInAlbum`：补齐加入差集 `S \ M_A`
    /// - 若为 `.allSelectedInAlbum`：将全部选中项 `S` 从相簿移出；若移出后该相簿变空且开启了 `autoDeleteEmptyAlbums`，即刻触发 `EmptyCollectionSweeper`！
    public func toggleMembership(for row: AlbumMembershipRow) async {
        guard !selectedAssetIDs.isEmpty, !isMutating else { return }

        let existingMembers = albumGraphService.assetIDs(inAlbumID: row.album.id)
        let action = AlbumGraphService.computeToggleAction(
            relation: row.relation,
            selectedAssetIDs: selectedAssetIDs,
            existingAlbumAssetIDs: existingMembers
        )

        isMutating = true
        defer { isMutating = false }

        do {
            switch action {
            case .add(let deltaIDs):
                guard !deltaIDs.isEmpty else { return }
                if let customToggleExecutor {
                    _ = try await customToggleExecutor(row, action)
                } else {
                    try await albumGraphService.addAssets(deltaIDs, toAlbum: row.album.id)
                }
                recomputeRows()
                onMembershipChanged?()

            case .remove(let removeIDs):
                guard !removeIDs.isEmpty else { return }
                let remainingCount: Int
                if let customToggleExecutor {
                    remainingCount = try await customToggleExecutor(row, action)
                } else {
                    remainingCount = try await albumGraphService.removeAssets(removeIDs, fromAlbum: row.album.id)
                }
                recomputeRows()
                onMembershipChanged?()

                // FR-6.4: 本应用内移出相簿成员后，若相簿变空且开启了自动清理，即刻触发空相簿与空父文件夹清理
                let prefs = stateStore.load().preferences
                if remainingCount == 0 && prefs.autoDeleteEmptyAlbums {
                    let sweptCount: Int
                    if let onTriggerEmptyAlbumSweep {
                        sweptCount = await onTriggerEmptyAlbumSweep()
                    } else {
                        let sweepResult = await emptySweeper.sweepEmptyAlbumsAndFolders(
                            albumGraphService: albumGraphService
                        )
                        sweptCount = sweepResult.totalDeletedCount
                        if sweptCount > 0 {
                            _ = try? stateStore.recordEmptyAlbumsDeleted(sweptCount)
                        }
                    }
                    recomputeRows()
                    if sweptCount > 0 {
                        onToast?("相簿「\(row.album.breadcrumbPath)」已清空并自动删除", .info)
                    }
                }
            }
        } catch {
            onToast?("调整相簿归属失败：\(error.localizedDescription)", .error)
        }
    }

    // MARK: - Create Album and Join (FR-6.3 & EC-7)

    /// 面板内一键新建相簿并将当前选中资产归入其中（若同层级已有同名相簿则直接归入现有相簿，EC-7）
    @discardableResult
    public func createAlbumAndJoin(
        title: String? = nil,
        parentFolderID: String? = nil
    ) async -> AlbumGraphService.CreateAlbumResult? {
        let rawTitle = title ?? newAlbumTitle
        let trimmedTitle = rawTitle.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmedTitle.isEmpty else {
            onToast?("请输入新相簿名称", .warning)
            return nil
        }

        let resolvedParentID = parentFolderID ?? selectedParentFolderID
        isMutating = true
        defer { isMutating = false }

        do {
            let result = try await albumGraphService.createAlbumAndAddAssets(
                title: trimmedTitle,
                parentFolderID: resolvedParentID,
                assetIDs: selectedAssetIDs
            )
            if title == nil {
                newAlbumTitle = ""
            }
            recomputeRows()
            onMembershipChanged?()

            if result.joinedExistingAlbum {
                onToast?("同名相簿已存在，将直接归入现有相簿", .info)
            } else {
                onToast?("已新建相簿「\(trimmedTitle)」并加入 \(selectedAssetIDs.count) 项", .success)
            }
            return result
        } catch {
            onToast?("新建相簿失败：\(error.localizedDescription)", .error)
            return nil
        }
    }

    // MARK: - Pure Static Helpers for Unit Testing

    public nonisolated static func computeRows(
        selectedAssetIDs: Set<String>,
        albums: [UserAlbumNode],
        albumToAssets: [String: Set<String>],
        searchQuery: String = ""
    ) -> (belonging: [AlbumMembershipRow], notJoined: [AlbumMembershipRow]) {
        AlbumGraphService.computeMembershipRows(
            selectedAssetIDs: selectedAssetIDs,
            albums: albums,
            albumToAssets: albumToAssets,
            searchQuery: searchQuery
        )
    }

    public nonisolated static func computeToggleAction(
        relation: AlbumMembershipRelation,
        selectedAssetIDs: Set<String>,
        existingAlbumAssetIDs: Set<String>
    ) -> AlbumGraphService.MembershipToggleAction {
        AlbumGraphService.computeToggleAction(
            relation: relation,
            selectedAssetIDs: selectedAssetIDs,
            existingAlbumAssetIDs: existingAlbumAssetIDs
        )
    }
}
