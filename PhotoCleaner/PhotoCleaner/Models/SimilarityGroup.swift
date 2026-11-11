import Foundation

/// 相似图识别灵敏度档位（SPEC §1.4 & §2.3）
public enum SimilaritySensitivity: String, Codable, CaseIterable, Identifiable, Hashable, Sendable {
    case standard
    case strict
    case off

    public var id: String { rawValue }

    public var displayTitle: String {
        switch self {
        case .standard: return "标准"
        case .strict: return "严格"
        case .off: return "关闭"
        }
    }

    /// Vision 特征距离阈值（SPEC §2.3）
    public var maxFeatureDistance: Float? {
        switch self {
        case .standard: return 0.45
        case .strict: return 0.32
        case .off: return nil
        }
    }

    /// 同日内最大拍摄时间间隔（秒，SPEC §2.3）
    public var maxTimeInterval: TimeInterval? {
        switch self {
        case .standard: return 14_400 // 4 小时
        case .strict: return 600      // 10 分钟
        case .off: return nil
        }
    }
}

/// 相似组的聚合来源类型
public enum SimilarityGroupKind: String, Codable, Hashable, Sendable {
    case burst              // 硬件连拍 (相同 burstIdentifier)
    case visionFeaturePrint // Vision 视觉特征距离聚类
}

/// 当日相似/连拍组结构（按年份隔离，平铺高亮）
public struct SimilarityGroup: Identifiable, Hashable, Codable, Sendable {
    public typealias Kind = SimilarityGroupKind

    public let id: String
    public let year: Int
    public let monthDay: MonthDay
    public let assetIDs: [String]
    public let kind: SimilarityGroupKind

    public init(
        id: String,
        year: Int,
        monthDay: MonthDay,
        assetIDs: [String],
        kind: SimilarityGroupKind = .visionFeaturePrint
    ) {
        self.id = id
        self.year = year
        self.monthDay = monthDay
        self.assetIDs = assetIDs
        self.kind = kind
    }
}
