import Foundation

// MARK: - 同步模式定义
public enum SyncMode: String, Codable, CaseIterable, Identifiable, Sendable {
    case bidirectionalNewest = "bidirectional_newest" // 双向同步（新覆盖旧）
    case localFirst = "local_first"                   // 本地优先（本地覆盖远端）
    case remoteFirst = "remote_first"                 // 远端优先（远端覆盖本地）
    case stopped = "stopped"                         // 停止同步（默认）

    public var id: String { rawValue }

    public var displayName: String {
        switch self {
        case .bidirectionalNewest:
            return "双向同步（新覆盖旧）"
        case .localFirst:
            return "本地优先（本地覆盖远端）"
        case .remoteFirst:
            return "远端优先（远端覆盖本地）"
        case .stopped:
            return "停止同步"
        }
    }

    public var badgeTitle: String {
        switch self {
        case .bidirectionalNewest:
            return "双向同步"
        case .localFirst:
            return "本地优先"
        case .remoteFirst:
            return "远端优先"
        case .stopped:
            return "已暂停"
        }
    }
}

// MARK: - 同步运行状态
public enum SyncStatus: String, Codable, Sendable {
    case idle = "idle"         // 空闲就绪
    case syncing = "syncing"   // 正在同步（显示 Loading 动画）
    case success = "success"   // 最近一次同步成功
    case error = "error"       // 同步出错（显示红色告警）
    case offline = "offline"   // 网络离线

    public var displayName: String {
        switch self {
        case .idle: return "就绪"
        case .syncing: return "同步中"
        case .success: return "同步成功"
        case .error: return "同步失败"
        case .offline: return "网络离线"
        }
    }
}

// MARK: - 监视的根文件夹
public struct WatchedFolder: Identifiable, Codable, Hashable, Sendable {
    public let id: String
    public var name: String
    public var path: String
    public var addedAt: Date
    public var repoCount: Int

    public init(id: String = UUID().uuidString, name: String, path: String, addedAt: Date = Date(), repoCount: Int = 0) {
        self.id = id
        self.name = name
        self.path = path
        self.addedAt = addedAt
        self.repoCount = repoCount
    }
}

// MARK: - Git 仓库实体
public struct GitRepo: Identifiable, Codable, Hashable, Sendable {
    public let id: String                    // 仓库绝对路径的标准形式
    public var folderId: String              // 所属根文件夹 ID
    public var name: String                  // 仓库目录名
    public var path: String                  // 仓库绝对根路径
    public var currentBranch: String         // 当前分支
    public var syncMode: SyncMode            // 当前同步策略（默认 stopped）
    public var status: SyncStatus            // 运行状态
    public var lastSyncTime: Date?           // 最近同步时间
    public var lastErrorMessage: String?     // 错误原因
    public var uncommittedChanges: Int       // 未提交变更数

    public init(
        id: String,
        folderId: String,
        name: String,
        path: String,
        currentBranch: String = "main",
        syncMode: SyncMode = .stopped,
        status: SyncStatus = .idle,
        lastSyncTime: Date? = nil,
        lastErrorMessage: String? = nil,
        uncommittedChanges: Int = 0
    ) {
        self.id = id
        self.folderId = folderId
        self.name = name
        self.path = path
        self.currentBranch = currentBranch
        self.syncMode = syncMode
        self.status = status
        self.lastSyncTime = lastSyncTime
        self.lastErrorMessage = lastErrorMessage
        self.uncommittedChanges = uncommittedChanges
    }
}

// MARK: - 全局设置
public struct AppSettings: Codable, Sendable {
    public var debounceSeconds: Int
    public var remotePollIntervalMinutes: Int
    public var launchAtStartup: Bool
    public var firstRunCompleted: Bool

    public static let `default` = AppSettings(
        debounceSeconds: 5,
        remotePollIntervalMinutes: 5,
        launchAtStartup: false,
        firstRunCompleted: false
    )
}

// MARK: - 仓库持久化配置项
public struct RepoSavedConfig: Codable, Sendable {
    public var syncMode: SyncMode
    public var lastSyncTime: Date?
}

// MARK: - 整体存储模型
public struct AppStorageData: Codable, Sendable {
    public var version: Int
    public var settings: AppSettings
    public var watchedFolders: [WatchedFolder]
    public var repoConfigs: [String: RepoSavedConfig] // key: repo.path

    public static let initial = AppStorageData(
        version: 1,
        settings: .default,
        watchedFolders: [],
        repoConfigs: [:]
    )
}
