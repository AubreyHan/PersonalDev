import Foundation
import Observation
import Photos

@MainActor
@Observable
public final class AlbumGraphService {
    public static let shared = AlbumGraphService()

    public enum MembershipToggleAction: Equatable, Sendable {
        case add(assetIDs: Set<String>)
        case remove(assetIDs: Set<String>)
    }

    public struct CreateAlbumResult: Equatable, Sendable {
        public let albumID: String
        public let albumNode: UserAlbumNode?
        public let joinedExistingAlbum: Bool

        public init(albumID: String, albumNode: UserAlbumNode?, joinedExistingAlbum: Bool) {
            self.albumID = albumID
            self.albumNode = albumNode
            self.joinedExistingAlbum = joinedExistingAlbum
        }
    }

    /// 按 breadcrumbPath 本地化升序排列的用户普通相簿列表
    public private(set) var albums: [UserAlbumNode] = []

    /// 按 breadcrumbPath 本地化升序排列的用户相簿文件夹列表
    public private(set) var folders: [UserFolderNode] = []

    /// albumID -> UserAlbumNode
    public private(set) var albumsByID: [String: UserAlbumNode] = [:]

    /// folderID -> UserFolderNode
    public private(set) var foldersByID: [String: UserFolderNode] = [:]

    /// 双向 O(1) 索引：albumID -> Set<assetID>
    public private(set) var albumToAssets: [String: Set<String>] = [:]

    /// 双向 O(1) 索引：assetID -> Set<albumID>
    public private(set) var assetToAlbums: [String: Set<String>] = [:]

    /// 图结构修订号（每次刷新自增）
    public private(set) var graphRevision: Int = 0

    public init() {}

    // MARK: - Graph Indexing from PhotoKit

    public func reloadGraph(includeHiddenAssets: Bool = false) async {
        let status = PHPhotoLibrary.authorizationStatus(for: .readWrite)
        guard status == .authorized || status == .limited else { return }

        let snapshot = await Task.detached(priority: .userInitiated) {
            Self.scanUserCollectionsGraph(includeHiddenAssets: includeHiddenAssets)
        }.value

        applySnapshot(snapshot)
    }

    public func reloadGraphSynchronously(includeHiddenAssets: Bool = false) {
        let status = PHPhotoLibrary.authorizationStatus(for: .readWrite)
        guard status == .authorized || status == .limited else { return }
        let snapshot = Self.scanUserCollectionsGraph(includeHiddenAssets: includeHiddenAssets)
        applySnapshot(snapshot)
    }

    /// 供单元测试直接注入内存相簿与文件夹图数据
    public func replaceGraphForTesting(
        albums: [UserAlbumNode],
        folders: [UserFolderNode] = [],
        albumToAssets: [String: Set<String>]
    ) {
        let sortedAlbums = albums.sorted {
            $0.breadcrumbPath.localizedStandardCompare($1.breadcrumbPath) == .orderedAscending
        }
        let sortedFolders = folders.sorted {
            $0.breadcrumbPath.localizedStandardCompare($1.breadcrumbPath) == .orderedAscending
        }
        var reverseMap: [String: Set<String>] = [:]
        for (albumID, assetIDs) in albumToAssets {
            for assetID in assetIDs {
                reverseMap[assetID, default: []].insert(albumID)
            }
        }
        self.albums = sortedAlbums
        self.folders = sortedFolders
        self.albumsByID = Dictionary(uniqueKeysWithValues: sortedAlbums.map { ($0.id, $0) })
        self.foldersByID = Dictionary(uniqueKeysWithValues: sortedFolders.map { ($0.id, $0) })
        self.albumToAssets = albumToAssets
        self.assetToAlbums = reverseMap
        self.graphRevision &+= 1
    }

    private struct GraphSnapshot: Sendable {
        let albums: [UserAlbumNode]
        let folders: [UserFolderNode]
        let albumToAssets: [String: Set<String>]
        let assetToAlbums: [String: Set<String>]
    }

    nonisolated private static func scanUserCollectionsGraph(includeHiddenAssets: Bool) -> GraphSnapshot {
        var collectedAlbums: [UserAlbumNode] = []
        var collectedFolders: [UserFolderNode] = []
        var albumToAssets: [String: Set<String>] = [:]
        var assetToAlbums: [String: Set<String>] = [:]

        let assetFetchOptions = PHFetchOptions()
        assetFetchOptions.includeHiddenAssets = includeHiddenAssets

        func traverse(
            collections: PHFetchResult<PHCollection>,
            parentFolderID: String?,
            parentBreadcrumb: String?
        ) -> (childFolderIDs: [String], childAlbumIDs: [String]) {
            var directFolderIDs: [String] = []
            var directAlbumIDs: [String] = []

            collections.enumerateObjects { collection, _, _ in
                if let folder = collection as? PHCollectionList,
                   folder.collectionListType == .folder,
                   folder.collectionListSubtype == .regularFolder {
                    let title = (folder.localizedTitle ?? "未命名文件夹").trimmingCharacters(in: .whitespacesAndNewlines)
                    let safeTitle = title.isEmpty ? "未命名文件夹" : title
                    let breadcrumb = parentBreadcrumb.map { "\($0) / \(safeTitle)" } ?? safeTitle
                    let folderID = folder.localIdentifier
                    directFolderIDs.append(folderID)

                    let childFetch = PHCollection.fetchCollections(in: folder, options: nil)
                    let (subFolderIDs, subAlbumIDs) = traverse(
                        collections: childFetch,
                        parentFolderID: folderID,
                        parentBreadcrumb: breadcrumb
                    )

                    let folderNode = UserFolderNode(
                        id: folderID,
                        title: safeTitle,
                        breadcrumbPath: breadcrumb,
                        parentFolderID: parentFolderID,
                        childFolderIDs: subFolderIDs,
                        childAlbumIDs: subAlbumIDs,
                        canDelete: folder.canPerform(.delete)
                    )
                    collectedFolders.append(folderNode)
                } else if let album = collection as? PHAssetCollection,
                          album.assetCollectionType == .album,
                          album.assetCollectionSubtype == .albumRegular,
                          album.canPerform(.addContent) || album.canPerform(.removeContent) {
                    let title = (album.localizedTitle ?? "未命名相簿").trimmingCharacters(in: .whitespacesAndNewlines)
                    let safeTitle = title.isEmpty ? "未命名相簿" : title
                    let breadcrumb = parentBreadcrumb.map { "\($0) / \(safeTitle)" } ?? safeTitle
                    let albumID = album.localIdentifier
                    directAlbumIDs.append(albumID)

                    let assetsResult = PHAsset.fetchAssets(in: album, options: assetFetchOptions)
                    var memberIDs = Set<String>()
                    memberIDs.reserveCapacity(assetsResult.count)
                    assetsResult.enumerateObjects { asset, _, _ in
                        let assetID = asset.localIdentifier
                        memberIDs.insert(assetID)
                        assetToAlbums[assetID, default: []].insert(albumID)
                    }

                    albumToAssets[albumID] = memberIDs
                    let albumNode = UserAlbumNode(
                        id: albumID,
                        title: safeTitle,
                        breadcrumbPath: breadcrumb,
                        parentFolderID: parentFolderID,
                        assetCount: memberIDs.count,
                        canDelete: album.canPerform(.delete)
                    )
                    collectedAlbums.append(albumNode)
                }
            }

            return (directFolderIDs, directAlbumIDs)
        }

        let topLevel = PHCollectionList.fetchTopLevelUserCollections(with: nil)
        _ = traverse(collections: topLevel, parentFolderID: nil, parentBreadcrumb: nil)

        collectedAlbums.sort {
            $0.breadcrumbPath.localizedStandardCompare($1.breadcrumbPath) == .orderedAscending
        }
        collectedFolders.sort {
            $0.breadcrumbPath.localizedStandardCompare($1.breadcrumbPath) == .orderedAscending
        }

        return GraphSnapshot(
            albums: collectedAlbums,
            folders: collectedFolders,
            albumToAssets: albumToAssets,
            assetToAlbums: assetToAlbums
        )
    }

    private func applySnapshot(_ snapshot: GraphSnapshot) {
        self.albums = snapshot.albums
        self.folders = snapshot.folders
        self.albumsByID = Dictionary(uniqueKeysWithValues: snapshot.albums.map { ($0.id, $0) })
        self.foldersByID = Dictionary(uniqueKeysWithValues: snapshot.folders.map { ($0.id, $0) })
        self.albumToAssets = snapshot.albumToAssets
        self.assetToAlbums = snapshot.assetToAlbums
        self.graphRevision &+= 1
    }

    // MARK: - O(1) Lookups & Pure 3-State Membership Computation (SPEC §2.1)

    /// 查询某资产所属的普通用户相簿数量（用于缩略图角标 `📁 N` 与 `uncategorized` 过滤）
    public func albumCount(forAssetID assetID: String) -> Int {
        assetToAlbums[assetID]?.count ?? 0
    }

    /// 查询某资产所属的相簿 ID 集合
    public func albumIDs(forAssetID assetID: String) -> Set<String> {
        assetToAlbums[assetID] ?? []
    }

    /// 查询某相簿包含的资产 ID 集合
    public func assetIDs(inAlbumID albumID: String) -> Set<String> {
        albumToAssets[albumID] ?? []
    }

    /// 计算当前选中资产集合相对于所有普通相簿的【所属相簿】与【未加入相簿】分组行（耗时 < 1ms）
    public func computeMembershipRows(
        for selectedAssetIDs: Set<String>,
        searchQuery: String = ""
    ) -> (belonging: [AlbumMembershipRow], notJoined: [AlbumMembershipRow]) {
        Self.computeMembershipRows(
            selectedAssetIDs: selectedAssetIDs,
            albums: albums,
            albumToAssets: albumToAssets,
            searchQuery: searchQuery
        )
    }

    /// 纯函数：根据选中资产 ID 集合、相簿节点列表与相簿成员映射计算三态归属行（SPEC §2.1）
    /// - 【所属相簿】区块：`allSelectedInAlbum` 在前，`partiallySelectedInAlbum` 在后，同级按 `breadcrumbPath` 升序
    /// - 【未加入相簿】区块：`noneSelectedInAlbum` 按 `breadcrumbPath` 升序
    /// - 支持按 `searchQuery` 对 `breadcrumbPath` 或 `title` 进行大小写不敏感子串过滤
    public nonisolated static func computeMembershipRows(
        selectedAssetIDs: Set<String>,
        albums: [UserAlbumNode],
        albumToAssets: [String: Set<String>],
        searchQuery: String = ""
    ) -> (belonging: [AlbumMembershipRow], notJoined: [AlbumMembershipRow]) {
        guard !selectedAssetIDs.isEmpty else {
            return ([], [])
        }

        let trimmedQuery = searchQuery.trimmingCharacters(in: .whitespacesAndNewlines)
        let filteredAlbums: [UserAlbumNode]
        if trimmedQuery.isEmpty {
            filteredAlbums = albums
        } else {
            filteredAlbums = albums.filter {
                $0.breadcrumbPath.localizedCaseInsensitiveContains(trimmedQuery) ||
                $0.title.localizedCaseInsensitiveContains(trimmedQuery)
            }
        }

        let totalSelected = selectedAssetIDs.count
        var allInRows: [AlbumMembershipRow] = []
        var partialRows: [AlbumMembershipRow] = []
        var noneInRows: [AlbumMembershipRow] = []

        for album in filteredAlbums {
            let members = albumToAssets[album.id] ?? []
            var matchedCount = 0
            for selectedID in selectedAssetIDs {
                if members.contains(selectedID) {
                    matchedCount += 1
                }
            }

            if matchedCount == 0 {
                noneInRows.append(
                    AlbumMembershipRow(
                        album: album,
                        matchedSelectedCount: 0,
                        totalSelectedCount: totalSelected,
                        relation: .noneSelectedInAlbum
                    )
                )
            } else if matchedCount == totalSelected {
                allInRows.append(
                    AlbumMembershipRow(
                        album: album,
                        matchedSelectedCount: matchedCount,
                        totalSelectedCount: totalSelected,
                        relation: .allSelectedInAlbum
                    )
                )
            } else {
                partialRows.append(
                    AlbumMembershipRow(
                        album: album,
                        matchedSelectedCount: matchedCount,
                        totalSelectedCount: totalSelected,
                        relation: .partiallySelectedInAlbum
                    )
                )
            }
        }

        allInRows.sort {
            $0.album.breadcrumbPath.localizedStandardCompare($1.album.breadcrumbPath) == .orderedAscending
        }
        partialRows.sort {
            $0.album.breadcrumbPath.localizedStandardCompare($1.album.breadcrumbPath) == .orderedAscending
        }
        noneInRows.sort {
            $0.album.breadcrumbPath.localizedStandardCompare($1.album.breadcrumbPath) == .orderedAscending
        }

        return (belonging: allInRows + partialRows, notJoined: noneInRows)
    }

    /// 纯函数：计算点击相簿行时应执行的增/删差集动作（SPEC §2.1）
    /// - 若为 `.noneSelectedInAlbum` 或 `.partiallySelectedInAlbum`：补齐加入差集 `S \ M_A`
    /// - 若为 `.allSelectedInAlbum`：将全部选中项 `S` 从相簿移出
    public nonisolated static func computeToggleAction(
        relation: AlbumMembershipRelation,
        selectedAssetIDs: Set<String>,
        existingAlbumAssetIDs: Set<String>
    ) -> MembershipToggleAction {
        switch relation {
        case .noneSelectedInAlbum, .partiallySelectedInAlbum:
            let deltaToAdd = selectedAssetIDs.subtracting(existingAlbumAssetIDs)
            return .add(assetIDs: deltaToAdd)
        case .allSelectedInAlbum:
            return .remove(assetIDs: selectedAssetIDs)
        }
    }

    /// 查找指定父文件夹下是否已存在同名普通相簿（EC-7）
    public func findExistingAlbum(title: String, parentFolderID: String?) -> UserAlbumNode? {
        Self.findExistingAlbum(title: title, parentFolderID: parentFolderID, in: albums)
    }

    public nonisolated static func findExistingAlbum(
        title: String,
        parentFolderID: String?,
        in albums: [UserAlbumNode]
    ) -> UserAlbumNode? {
        let trimmed = title.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmed.isEmpty else { return nil }
        return albums.first { album in
            album.parentFolderID == parentFolderID &&
            album.title.compare(trimmed, options: [.caseInsensitive, .diacriticInsensitive]) == .orderedSame
        }
    }

    // MARK: - PhotoKit Album Membership Mutations (FR-6.2 & FR-6.3)

    /// 将指定资产集合批量加入目标普通相簿
    public func addAssets(_ assetIDs: Set<String>, toAlbum albumID: String) async throws {
        let existing = albumToAssets[albumID] ?? []
        let deltaIDs = Array(assetIDs.subtracting(existing))
        guard !deltaIDs.isEmpty else { return }

        guard let collection = PHAssetCollection.fetchAssetCollections(
            withLocalIdentifiers: [albumID],
            options: nil
        ).firstObject else {
            return
        }
        let assetsFetch = PHAsset.fetchAssets(withLocalIdentifiers: deltaIDs, options: nil)
        guard assetsFetch.count > 0 else { return }

        try await PHPhotoLibrary.shared().performChanges {
            let changeRequest = PHAssetCollectionChangeRequest(for: collection)
            changeRequest?.addAssets(assetsFetch)
        }

        applyLocalAdd(assetIDs: Set(deltaIDs), toAlbum: albumID)
    }

    /// 将指定资产集合从目标普通相簿中移出，并返回移出后该相簿剩余的真实资产数量
    @discardableResult
    public func removeAssets(_ assetIDs: Set<String>, fromAlbum albumID: String) async throws -> Int {
        let existing = albumToAssets[albumID] ?? []
        let toRemoveIDs = Array(assetIDs.intersection(existing))
        guard !toRemoveIDs.isEmpty else {
            return existing.count
        }

        guard let collection = PHAssetCollection.fetchAssetCollections(
            withLocalIdentifiers: [albumID],
            options: nil
        ).firstObject else {
            return 0
        }
        let assetsFetch = PHAsset.fetchAssets(withLocalIdentifiers: toRemoveIDs, options: nil)
        guard assetsFetch.count > 0 else {
            return existing.count
        }

        try await PHPhotoLibrary.shared().performChanges {
            let changeRequest = PHAssetCollectionChangeRequest(for: collection)
            changeRequest?.removeAssets(assetsFetch)
        }

        applyLocalRemove(assetIDs: Set(toRemoveIDs), fromAlbum: albumID)
        return albumToAssets[albumID]?.count ?? 0
    }

    /// 原子“新建相簿并加入选中资产”（FR-6.3 & EC-7）：
    /// - 若同一父文件夹下已存在同名普通相簿，直接将选中资产归入该现有相簿并返回 `joinedExistingAlbum = true`
    /// - 否则在单次 `PHPhotoLibrary.shared().performChanges` 事务内创建相簿、加入选中资产并挂载至指定父文件夹
    @discardableResult
    public func createAlbumAndAddAssets(
        title: String,
        parentFolderID: String? = nil,
        assetIDs: Set<String>
    ) async throws -> CreateAlbumResult {
        let trimmedTitle = title.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmedTitle.isEmpty else {
            throw NSError(
                domain: "PhotoCleaner.AlbumGraphService",
                code: 1,
                userInfo: [NSLocalizedDescriptionKey: "相簿名称不能为空"]
            )
        }

        // EC-7: 同层级同名相簿防重，直接归入现有相簿
        if let existingAlbum = findExistingAlbum(title: trimmedTitle, parentFolderID: parentFolderID) {
            if !assetIDs.isEmpty {
                try await addAssets(assetIDs, toAlbum: existingAlbum.id)
            }
            let updatedNode = albumsByID[existingAlbum.id] ?? existingAlbum
            return CreateAlbumResult(
                albumID: existingAlbum.id,
                albumNode: updatedNode,
                joinedExistingAlbum: true
            )
        }

        let orderedIDs = Array(assetIDs)
        let assetsFetch = orderedIDs.isEmpty ? nil : PHAsset.fetchAssets(withLocalIdentifiers: orderedIDs, options: nil)
        let parentFolder: PHCollectionList? = {
            guard let parentFolderID else { return nil }
            return PHCollectionList.fetchCollectionLists(withLocalIdentifiers: [parentFolderID], options: nil).firstObject
        }()

        var createdLocalIdentifier: String = ""

        try await PHPhotoLibrary.shared().performChanges {
            let createRequest = PHAssetCollectionChangeRequest.creationRequestForAssetCollection(withTitle: trimmedTitle)
            let placeholder = createRequest.placeholderForCreatedAssetCollection
            createdLocalIdentifier = placeholder.localIdentifier

            if let assetsFetch, assetsFetch.count > 0 {
                createRequest.addAssets(assetsFetch)
            }

            if let parentFolder,
               let folderRequest = PHCollectionListChangeRequest(for: parentFolder) {
                folderRequest.addChildCollections([placeholder] as NSArray)
            }
        }

        await reloadGraph()
        let createdNode = albumsByID[createdLocalIdentifier]
        return CreateAlbumResult(
            albumID: createdLocalIdentifier,
            albumNode: createdNode,
            joinedExistingAlbum: false
        )
    }

    // MARK: - Optimistic In-Memory Graph Patching

    private func applyLocalAdd(assetIDs: Set<String>, toAlbum albumID: String) {
        var members = albumToAssets[albumID] ?? []
        members.formUnion(assetIDs)
        albumToAssets[albumID] = members

        for assetID in assetIDs {
            assetToAlbums[assetID, default: []].insert(albumID)
        }

        if var node = albumsByID[albumID] {
            node.assetCount = members.count
            albumsByID[albumID] = node
            if let idx = albums.firstIndex(where: { $0.id == albumID }) {
                albums[idx] = node
            }
        }
        graphRevision &+= 1
    }

    private func applyLocalRemove(assetIDs: Set<String>, fromAlbum albumID: String) {
        var members = albumToAssets[albumID] ?? []
        members.subtract(assetIDs)
        albumToAssets[albumID] = members

        for assetID in assetIDs {
            assetToAlbums[assetID]?.remove(albumID)
            if assetToAlbums[assetID]?.isEmpty == true {
                assetToAlbums.removeValue(forKey: assetID)
            }
        }

        if var node = albumsByID[albumID] {
            node.assetCount = members.count
            albumsByID[albumID] = node
            if let idx = albums.firstIndex(where: { $0.id == albumID }) {
                albums[idx] = node
            }
        }
        graphRevision &+= 1
    }
}
