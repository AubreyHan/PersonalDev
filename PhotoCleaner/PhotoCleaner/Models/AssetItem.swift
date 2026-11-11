import Foundation
import Photos

/// 顶部多维过滤胶囊枚举（SPEC §1.2）
public enum MediaFilter: String, CaseIterable, Identifiable, Codable, Hashable, Sendable {
    case all              // 全部
    case photosOnly       // 仅照片（含 Live Photo）
    case videosOnly       // 仅视频
    case screenshots      // 仅截屏与屏幕录制
    case largeFiles       // 大体积（>= 20 MB）
    case similarGroups    // 仅相似/连拍组内项
    case uncategorized    // 未归属任何普通用户相簿
    case newOnly          // 仅新增未审项（仅在 DayReviewStatus == .hasNew 时展示）

    public var id: String { rawValue }

    /// 大体积过滤阈值：20 MB
    public static let largeFileThresholdBytes: Int64 = 20 * 1024 * 1024

    public var displayTitle: String {
        switch self {
        case .all: return "全部"
        case .photosOnly: return "仅照片"
        case .videosOnly: return "仅视频"
        case .screenshots: return "仅截屏·录屏"
        case .largeFiles: return "大体积(>20MB)"
        case .similarGroups: return "相似组"
        case .uncategorized: return "未入相簿"
        case .newOnly: return "仅看新增"
        }
    }

    public var systemImage: String {
        switch self {
        case .all: return "square.grid.2x2"
        case .photosOnly: return "photo"
        case .videosOnly: return "video"
        case .screenshots: return "camera.viewfinder"
        case .largeFiles: return "internaldrive"
        case .similarGroups: return "square.on.square"
        case .uncategorized: return "folder.badge.questionmark"
        case .newOnly: return "sparkles"
        }
    }
}

/// 右侧检视栏展示的资产 EXIF 与元数据详情（SPEC §1.2）
public struct AssetExifDetail: Hashable, Codable, Sendable {
    public let filename: String
    public let fileSizeByteCount: Int64
    public let pixelWidth: Int
    public let pixelHeight: Int
    public let uniformTypeIdentifier: String // 如 public.heic, com.apple.quicktime-movie
    public let captureDate: Date?
    public let cameraMakeAndModel: String?   // 如 "Apple iPhone 16 Pro"
    public let lensAndExposure: String?      // 如 "24mm f/1.78 · 1/120s · ISO 64"
    public let locationDescription: String?  // 逆地理编码城市/地标或经纬度
    public let isLocallyAvailable: Bool      // 原片是否已在本地磁盘（非仅在 iCloud）

    public init(
        filename: String,
        fileSizeByteCount: Int64,
        pixelWidth: Int,
        pixelHeight: Int,
        uniformTypeIdentifier: String,
        captureDate: Date? = nil,
        cameraMakeAndModel: String? = nil,
        lensAndExposure: String? = nil,
        locationDescription: String? = nil,
        isLocallyAvailable: Bool = true
    ) {
        self.filename = filename
        self.fileSizeByteCount = fileSizeByteCount
        self.pixelWidth = pixelWidth
        self.pixelHeight = pixelHeight
        self.uniformTypeIdentifier = uniformTypeIdentifier
        self.captureDate = captureDate
        self.cameraMakeAndModel = cameraMakeAndModel
        self.lensAndExposure = lensAndExposure
        self.locationDescription = locationDescription
        self.isLocallyAvailable = isLocallyAvailable
    }

    public var dimensionsDescription: String {
        "\(pixelWidth) × \(pixelHeight)"
    }
}

/// PHAsset 轻量领域包装模型（支持真实 PHAsset 包装与纯内存单元测试构造）
public struct AssetItem: Identifiable, Hashable, @unchecked Sendable {
    public let id: String
    public var localIdentifier: String { id }
    public let phAsset: PHAsset?
    public let mediaType: PHAssetMediaType
    public let mediaSubtypes: PHAssetMediaSubtype
    public let creationDate: Date?
    public let year: Int
    public let monthDay: MonthDay
    public let duration: TimeInterval
    public var isFavorite: Bool
    public let isScreenshot: Bool
    public let isLivePhoto: Bool
    public var isLocallyAvailable: Bool
    public var fileSizeByteCount: Int64?
    public let pixelWidth: Int
    public let pixelHeight: Int
    public let burstIdentifier: String?
    public let canDelete: Bool

    public init(
        id: String,
        phAsset: PHAsset? = nil,
        mediaType: PHAssetMediaType = .image,
        mediaSubtypes: PHAssetMediaSubtype = [],
        creationDate: Date? = nil,
        year: Int = 2026,
        monthDay: MonthDay = MonthDay(month: 1, day: 1),
        duration: TimeInterval = 0,
        isFavorite: Bool = false,
        isScreenshot: Bool = false,
        isLivePhoto: Bool = false,
        isLocallyAvailable: Bool = true,
        fileSizeByteCount: Int64? = nil,
        pixelWidth: Int = 0,
        pixelHeight: Int = 0,
        burstIdentifier: String? = nil,
        canDelete: Bool = true
    ) {
        self.id = id
        self.phAsset = phAsset
        self.mediaType = mediaType
        self.mediaSubtypes = mediaSubtypes
        self.creationDate = creationDate
        self.year = year
        self.monthDay = monthDay
        self.duration = duration
        self.isFavorite = isFavorite
        self.isScreenshot = isScreenshot
        self.isLivePhoto = isLivePhoto
        self.isLocallyAvailable = isLocallyAvailable
        self.fileSizeByteCount = fileSizeByteCount
        self.pixelWidth = pixelWidth
        self.pixelHeight = pixelHeight
        self.burstIdentifier = burstIdentifier
        self.canDelete = canDelete
    }

    public init(
        phAsset: PHAsset,
        calendar: Calendar = .current,
        isLocallyAvailable: Bool = true,
        fileSizeByteCount: Int64? = nil
    ) {
        let date = phAsset.creationDate ?? phAsset.modificationDate ?? Date.distantPast
        let comps = calendar.dateComponents([.year, .month, .day], from: date)
        let year = comps.year ?? 1970
        let monthDay = MonthDay(month: comps.month ?? 1, day: comps.day ?? 1)
        let subtypes = phAsset.mediaSubtypes
        let isScreenshot = subtypes.contains(.photoScreenshot)
        let isLivePhoto = subtypes.contains(.photoLive)

        self.init(
            id: phAsset.localIdentifier,
            phAsset: phAsset,
            mediaType: phAsset.mediaType,
            mediaSubtypes: subtypes,
            creationDate: phAsset.creationDate,
            year: year,
            monthDay: monthDay,
            duration: phAsset.duration,
            isFavorite: phAsset.isFavorite,
            isScreenshot: isScreenshot,
            isLivePhoto: isLivePhoto,
            isLocallyAvailable: isLocallyAvailable,
            fileSizeByteCount: fileSizeByteCount,
            pixelWidth: phAsset.pixelWidth,
            pixelHeight: phAsset.pixelHeight,
            burstIdentifier: phAsset.burstIdentifier,
            canDelete: phAsset.canPerform(.delete)
        )
    }

    public var isVideo: Bool {
        mediaType == .video
    }

    public var isPhoto: Bool {
        mediaType == .image
    }

    public var isLargeFile: Bool {
        (fileSizeByteCount ?? 0) >= MediaFilter.largeFileThresholdBytes
    }

    /// 视频时长格式化，如 "01:24" 或 "1:02:15"
    public var formattedDuration: String? {
        guard isVideo, duration > 0 else { return nil }
        let totalSeconds = Int(duration.rounded())
        let hours = totalSeconds / 3600
        let minutes = (totalSeconds % 3600) / 60
        let seconds = totalSeconds % 60
        if hours > 0 {
            return String(format: "%d:%02d:%02d", hours, minutes, seconds)
        } else {
            return String(format: "%02d:%02d", minutes, seconds)
        }
    }

    public static func == (lhs: AssetItem, rhs: AssetItem) -> Bool {
        lhs.id == rhs.id &&
        lhs.isFavorite == rhs.isFavorite &&
        lhs.isLocallyAvailable == rhs.isLocallyAvailable &&
        lhs.fileSizeByteCount == rhs.fileSizeByteCount &&
        lhs.canDelete == rhs.canDelete
    }

    public func hash(into hasher: inout Hasher) {
        hasher.combine(id)
        hasher.combine(isFavorite)
        hasher.combine(isLocallyAvailable)
        hasher.combine(fileSizeByteCount)
    }
}
