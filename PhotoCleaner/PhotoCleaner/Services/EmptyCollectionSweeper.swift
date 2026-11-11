import Foundation
import Observation
import Photos

@MainActor
@Observable
public final class EmptyCollectionSweeper {
    public static let shared = EmptyCollectionSweeper()

    public struct SweepResult: Equatable, Sendable {
        public let deletedAlbumIDs: [String]
        public let deletedFolderIDs: [String]
        public let wasUserCancelled: Bool

        public var deletedAlbumCount: Int { deletedAlbumIDs.count }
        public var deletedFolderCount: Int { deletedFolderIDs.count }
        public var totalDeletedCount: Int { deletedAlbumIDs.count + deletedFolderIDs.count }

        public init(
            deletedAlbumIDs: [String] = [],
            deletedFolderIDs: [String] = [],
            wasUserCancelled: Bool = false
        ) {
            self.deletedAlbumIDs = deletedAlbumIDs
            self.deletedFolderIDs = deletedFolderIDs
            self.wasUserCancelled = wasUserCancelled
        }
    }

    public struct SweepPlan: Equatable, Sendable {
        public let emptyAlbumIDs: [String]
        public let emptyFolderRounds: [[String]]
        public let allEmptyFolderIDs: [String]
        public let remainingAlbums: [UserAlbumNode]
        public let remainingFolders: [UserFolderNode]

        public init(
            emptyAlbumIDs: [String],
            emptyFolderRounds: [[String]],
            allEmptyFolderIDs: [String],
            remainingAlbums: [UserAlbumNode],
            remainingFolders: [UserFolderNode]
        ) {
            self.emptyAlbumIDs = emptyAlbumIDs
            self.emptyFolderRounds = emptyFolderRounds
            self.allEmptyFolderIDs = allEmptyFolderIDs
            self.remainingAlbums = remainingAlbums
            self.remainingFolders = remainingFolders
        }
    }

    /// EC-3: 本次会话内用户点击系统弹窗“不允许/取消”后忽略的空相簿 ID 集合，防止重复弹窗骚扰
    public private(set) var sessionIgnoredEmptyAlbumIDs: Set<String> = []

    /// EC-3: 本次会话内用户拒绝删除的空文件夹 ID 集合
    public private(set) var sessionIgnoredEmptyFolderIDs: Set<String> = []

    /// 是否正在执行清理
    public private(set) var isSweeping: Bool = false

    public init() {}

    public func resetSessionIgnoredIDs() {
        sessionIgnoredEmptyAlbumIDs.removeAll()
        sessionIgnoredEmptyFolderIDs.removeAll()
    }

    public func markAlbumIDsIgnoredForSession<S: Sequence>(_ ids: S) where S.Element == String {
        sessionIgnoredEmptyAlbumIDs.formUnion(ids)
    }

    public func markFolderIDsIgnoredForSession<S: Sequence>(_ ids: S) where S.Element == String {
        sessionIgnoredEmptyFolderIDs.formUnion(ids)
    }

    // MARK: - Live PhotoKit Two-Stage Bottom-Up Sweep (SPEC §2.2 & FR-6.4)

    /// 扫描并即刻自动删除所有真实资产数为 0 的用户普通相簿及连带变空的上级用户文件夹
    /// 仅在以下三个本应用明确时机调用：
    /// 1. 冷启动完成首次相簿索引时
    /// 2. 本应用内从相簿移出资产后
    /// 3. 本应用内提交待删暂存篮成功删除照片后
    @discardableResult
    public func sweepEmptyAlbumsAndFolders(
        albumGraphService: AlbumGraphService? = nil
    ) async -> SweepResult {
        guard !isSweeping else {
            return SweepResult()
        }
        let status = PHPhotoLibrary.authorizationStatus(for: .readWrite)
        guard status == .authorized || status == .limited else {
            return SweepResult()
        }

        isSweeping = true
        defer { isSweeping = false }

        var deletedAlbumIDs: [String] = []
        var deletedFolderIDs: [String] = []
        var wasCancelled = false

        // Step 1: 查找并单次合并删除所有真实资产数 == 0（含隐藏照片 EC-2）的白名单普通相簿
        let ignoredAlbums = sessionIgnoredEmptyAlbumIDs
        let emptyAlbums = await Task.detached(priority: .userInitiated) {
            Self.fetchWhitelistedEmptyRegularAlbums(ignoring: ignoredAlbums)
        }.value

        if !emptyAlbums.isEmpty {
            let candidateIDs = emptyAlbums.map(\.localIdentifier)
            do {
                try await PHPhotoLibrary.shared().performChanges {
                    PHAssetCollectionChangeRequest.deleteAssetCollections(emptyAlbums as NSArray)
                }
                deletedAlbumIDs.append(contentsOf: candidateIDs)
            } catch {
                if Self.isUserCancelledError(error) {
                    sessionIgnoredEmptyAlbumIDs.formUnion(candidateIDs)
                    wasCancelled = true
                }
                if let albumGraphService {
                    await albumGraphService.reloadGraph()
                }
                return SweepResult(
                    deletedAlbumIDs: deletedAlbumIDs,
                    deletedFolderIDs: deletedFolderIDs,
                    wasUserCancelled: wasCancelled
                )
            }
        }

        // Step 2: 自底向上（按深度从最深叶子文件夹向根部）循环删除内部无任何子项的空相簿文件夹
        while true {
            let ignoredFolders = sessionIgnoredEmptyFolderIDs
            let deepestEmptyFolders = await Task.detached(priority: .userInitiated) {
                Self.fetchDeepestEmptyRegularFolders(ignoring: ignoredFolders)
            }.value

            guard !deepestEmptyFolders.isEmpty else {
                break
            }

            let candidateFolderIDs = deepestEmptyFolders.map(\.localIdentifier)
            do {
                try await PHPhotoLibrary.shared().performChanges {
                    PHCollectionListChangeRequest.deleteCollectionLists(deepestEmptyFolders as NSArray)
                }
                deletedFolderIDs.append(contentsOf: candidateFolderIDs)
            } catch {
                if Self.isUserCancelledError(error) {
                    sessionIgnoredEmptyFolderIDs.formUnion(candidateFolderIDs)
                    wasCancelled = true
                }
                break
            }
        }

        if let albumGraphService, (!deletedAlbumIDs.isEmpty || !deletedFolderIDs.isEmpty) {
            await albumGraphService.reloadGraph()
        }

        return SweepResult(
            deletedAlbumIDs: deletedAlbumIDs,
            deletedFolderIDs: deletedFolderIDs,
            wasUserCancelled: wasCancelled
        )
    }

    // MARK: - Pure Testable Bottom-Up Graph Resolution (For Unit Tests without PhotoKit)

    /// 纯函数：在内存相簿与文件夹节点图上执行两阶段自底向上空相簿/空文件夹级联规划（SPEC §2.2）
    /// - 严格校验 `canDelete == true`
    /// - 强制叠加 `hiddenAssetCountsByAlbumID`（模拟 `includeHiddenAssets = true`，EC-2）
    /// - 排除 `ignoredAlbumIDs` 与 `ignoredFolderIDs`（EC-3）
    /// - 自底向上按轮次（从最深层空文件夹到顶层父文件夹）级联清理空文件夹
    public nonisolated static func resolveBottomUpSweepPlan(
        albums: [UserAlbumNode],
        folders: [UserFolderNode],
        hiddenAssetCountsByAlbumID: [String: Int] = [:],
        ignoredAlbumIDs: Set<String> = [],
        ignoredFolderIDs: Set<String> = []
    ) -> SweepPlan {
        // Step 1: 识别所有真实资产数（可见 + 隐藏）为 0 且可删除、未被忽略的普通相簿
        var emptyAlbumIDs: [String] = []
        var emptyAlbumIDSet = Set<String>()
        var remainingAlbums: [UserAlbumNode] = []

        for album in albums {
            let hiddenCount = max(0, hiddenAssetCountsByAlbumID[album.id] ?? 0)
            let trueTotalCount = max(0, album.assetCount) + hiddenCount
            if album.canDelete && !ignoredAlbumIDs.contains(album.id) && trueTotalCount == 0 {
                emptyAlbumIDs.append(album.id)
                emptyAlbumIDSet.insert(album.id)
            } else {
                remainingAlbums.append(album)
            }
        }

        // 构建可变文件夹映射，移除已被清理的空相簿引用
        let survivingAlbumIDs = Set(remainingAlbums.map(\.id))
        var folderMap: [String: UserFolderNode] = [:]
        for var folder in folders {
            folder.childAlbumIDs = folder.childAlbumIDs.filter {
                !emptyAlbumIDSet.contains($0) && survivingAlbumIDs.contains($0)
            }
            folderMap[folder.id] = folder
        }

        // 计算每个文件夹的深度，确保同轮次按深度倒序（Bottom-Up）排列
        func depth(of folderID: String, visited: Set<String> = []) -> Int {
            guard !visited.contains(folderID),
                  let parentID = folderMap[folderID]?.parentFolderID else {
                return 0
            }
            var nextVisited = visited
            nextVisited.insert(folderID)
            return 1 + depth(of: parentID, visited: nextVisited)
        }

        var emptyFolderRounds: [[String]] = []
        var allEmptyFolderIDs: [String] = []
        var deletedFolderIDSet = Set<String>()

        // Step 2: 循环自底向上剥离空叶子文件夹，直到无新增空文件夹
        while true {
            var currentRoundEmptyFolders: [(id: String, depth: Int)] = []

            for folder in folderMap.values {
                guard !deletedFolderIDSet.contains(folder.id) else { continue }
                let activeChildAlbums = folder.childAlbumIDs.filter { !emptyAlbumIDSet.contains($0) }
                let activeChildFolders = folder.childFolderIDs.filter { !deletedFolderIDSet.contains($0) }

                if activeChildAlbums.isEmpty &&
                   activeChildFolders.isEmpty &&
                   folder.canDelete &&
                   !ignoredFolderIDs.contains(folder.id) {
                    currentRoundEmptyFolders.append((id: folder.id, depth: depth(of: folder.id)))
                }
            }

            guard !currentRoundEmptyFolders.isEmpty else {
                break
            }

            currentRoundEmptyFolders.sort { lhs, rhs in
                if lhs.depth != rhs.depth {
                    return lhs.depth > rhs.depth
                }
                return lhs.id < rhs.id
            }

            let roundIDs = currentRoundEmptyFolders.map(\.id)
            emptyFolderRounds.append(roundIDs)
            allEmptyFolderIDs.append(contentsOf: roundIDs)
            deletedFolderIDSet.formUnion(roundIDs)
        }

        let remainingFolders = folders
            .filter { !deletedFolderIDSet.contains($0.id) }
            .map { folder -> UserFolderNode in
                var updated = folder
                updated.childAlbumIDs = folder.childAlbumIDs.filter { !emptyAlbumIDSet.contains($0) }
                updated.childFolderIDs = folder.childFolderIDs.filter { !deletedFolderIDSet.contains($0) }
                return updated
            }

        return SweepPlan(
            emptyAlbumIDs: emptyAlbumIDs,
            emptyFolderRounds: emptyFolderRounds,
            allEmptyFolderIDs: allEmptyFolderIDs,
            remainingAlbums: remainingAlbums,
            remainingFolders: remainingFolders
        )
    }

    /// 判断错误是否为 macOS PhotoKit 系统弹窗中用户点击了“不允许 / 取消”（EC-3）
    public nonisolated static func isUserCancelledError(_ error: Error) -> Bool {
        if let photosError = error as? PHPhotosError, photosError.code == .userCancelled {
            return true
        }
        let nsError = error as NSError
        if nsError.domain == NSCocoaErrorDomain && nsError.code == NSUserCancelledError {
            return true
        }
        if nsError.code == PHPhotosError.userCancelled.rawValue {
            return true
        }
        return false
    }

    // MARK: - Private PhotoKit Traversal Helpers

    /// EC-2 安全红线：判定空普通相簿时，强制设置 `includeHiddenAssets = true` 统计真实总数！
    nonisolated private static func fetchWhitelistedEmptyRegularAlbums(
        ignoring ignoredAlbumIDs: Set<String>
    ) -> [PHAssetCollection] {
        var emptyAlbums: [PHAssetCollection] = []

        let trueCountOptions = PHFetchOptions()
        trueCountOptions.includeHiddenAssets = true

        func visit(_ collections: PHFetchResult<PHCollection>) {
            collections.enumerateObjects { collection, _, _ in
                if let folder = collection as? PHCollectionList,
                   folder.collectionListType == .folder,
                   folder.collectionListSubtype == .regularFolder {
                    let children = PHCollection.fetchCollections(in: folder, options: nil)
                    visit(children)
                } else if let album = collection as? PHAssetCollection,
                          album.collectionType == .album,
                          album.collectionSubtype == .albumRegular,
                          album.canPerform(.delete),
                          !ignoredAlbumIDs.contains(album.localIdentifier) {
                    let trueAssetCount = PHAsset.fetchAssets(in: album, options: trueCountOptions).count
                    if trueAssetCount == 0 {
                        emptyAlbums.append(album)
                    }
                }
            }
        }

        let topLevel = PHCollectionList.fetchTopLevelUserCollections(with: nil)
        visit(topLevel)
        return emptyAlbums
    }

    /// 递归遍历用户文件夹树，返回当前所有内部无任何子集合（`fetchCollections.count == 0`）的白名单空文件夹（按深度倒序）
    nonisolated private static func fetchDeepestEmptyRegularFolders(
        ignoring ignoredFolderIDs: Set<String>
    ) -> [PHCollectionList] {
        var emptyFoldersWithDepth: [(folder: PHCollectionList, depth: Int)] = []

        func visit(_ collections: PHFetchResult<PHCollection>, depth: Int) {
            collections.enumerateObjects { collection, _, _ in
                guard let folder = collection as? PHCollectionList,
                      folder.collectionListType == .folder,
                      folder.collectionListSubtype == .regularFolder else {
                    return
                }
                let children = PHCollection.fetchCollections(in: folder, options: nil)
                if children.count == 0 {
                    if folder.canPerform(.delete) && !ignoredFolderIDs.contains(folder.localIdentifier) {
                        emptyFoldersWithDepth.append((folder, depth))
                    }
                } else {
                    visit(children, depth: depth + 1)
                }
            }
        }

        let topLevel = PHCollectionList.fetchTopLevelUserCollections(with: nil)
        visit(topLevel, depth: 0)

        emptyFoldersWithDepth.sort { $0.depth > $1.depth }
        return emptyFoldersWithDepth.map(\.folder)
    }
}
