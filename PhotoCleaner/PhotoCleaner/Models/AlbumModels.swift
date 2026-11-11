import Foundation

/// 用户自建普通相簿节点（subtype == .albumRegular 且支持增删内容，SPEC §1.3）
public struct UserAlbumNode: Identifiable, Hashable, Codable, Sendable {
    public let id: String                 // PHAssetCollection.localIdentifier
    public let title: String              // 相簿名，如 "2025日本"
    public let breadcrumbPath: String     // 含文件夹前缀完整路径，如 "旅行 / 2025日本"
    public let parentFolderID: String?    // 父级 PHCollectionList.localIdentifier（顶层为 nil）
    public var assetCount: Int            // 当前相簿内资产总数
    public let canDelete: Bool            // 是否支持删除

    public init(
        id: String,
        title: String,
        breadcrumbPath: String? = nil,
        parentFolderID: String? = nil,
        assetCount: Int = 0,
        canDelete: Bool = true
    ) {
        self.id = id
        self.title = title
        self.breadcrumbPath = breadcrumbPath ?? title
        self.parentFolderID = parentFolderID
        self.assetCount = assetCount
        self.canDelete = canDelete
    }
}

/// 用户自建相簿文件夹节点（subtype == .regularFolder，SPEC §1.3）
public struct UserFolderNode: Identifiable, Hashable, Codable, Sendable {
    public let id: String                 // PHCollectionList.localIdentifier
    public let title: String
    public let breadcrumbPath: String
    public let parentFolderID: String?
    public var childFolderIDs: [String]
    public var childAlbumIDs: [String]
    public let canDelete: Bool

    public init(
        id: String,
        title: String,
        breadcrumbPath: String? = nil,
        parentFolderID: String? = nil,
        childFolderIDs: [String] = [],
        childAlbumIDs: [String] = [],
        canDelete: Bool = true
    ) {
        self.id = id
        self.title = title
        self.breadcrumbPath = breadcrumbPath ?? title
        self.parentFolderID = parentFolderID
        self.childFolderIDs = childFolderIDs
        self.childAlbumIDs = childAlbumIDs
        self.canDelete = canDelete
    }
}

/// 选中 1~N 个资产时，相对于某个普通相簿的三态归属关系（SPEC §1.3）
public enum AlbumMembershipRelation: String, Codable, Hashable, Sendable {
    case allSelectedInAlbum       // [✓] 全部选中资产均属于该相簿（归入【所属相簿】区块）
    case partiallySelectedInAlbum // [-] 部分选中资产属于该相簿（归入【所属相簿】区块）
    case noneSelectedInAlbum      // [ ] 所有选中资产均不属于该相簿（归入【未加入相簿】区块）

    public var isBelonging: Bool {
        self == .allSelectedInAlbum || self == .partiallySelectedInAlbum
    }
}

/// 右侧相簿归属面板的单行展示模型（SPEC §1.3）
public struct AlbumMembershipRow: Identifiable, Hashable, Sendable {
    public var id: String { album.id }
    public let album: UserAlbumNode
    public let matchedSelectedCount: Int  // 当前选中的 K 个资产中有几个在该相簿中
    public let totalSelectedCount: Int    // 当前共选中 K 个资产
    public let relation: AlbumMembershipRelation

    public init(
        album: UserAlbumNode,
        matchedSelectedCount: Int,
        totalSelectedCount: Int,
        relation: AlbumMembershipRelation? = nil
    ) {
        self.album = album
        self.matchedSelectedCount = matchedSelectedCount
        self.totalSelectedCount = totalSelectedCount
        if let relation {
            self.relation = relation
        } else if totalSelectedCount <= 0 || matchedSelectedCount <= 0 {
            self.relation = .noneSelectedInAlbum
        } else if matchedSelectedCount >= totalSelectedCount {
            self.relation = .allSelectedInAlbum
        } else {
            self.relation = .partiallySelectedInAlbum
        }
    }
}
