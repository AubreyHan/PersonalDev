import Foundation

public struct ActivityTypeDTO: Codable, Equatable, Hashable, Sendable {
    public let typeId: Int64?
    public let typeKey: String
    public let parentTypeId: Int64?

    public init(typeId: Int64? = nil, typeKey: String, parentTypeId: Int64? = nil) {
        self.typeId = typeId
        self.typeKey = typeKey
        self.parentTypeId = parentTypeId
    }
}

public struct ActivityItem: Codable, Identifiable, Equatable, Hashable, Sendable {
    public let activityId: Int64
    public let activityName: String
    public let startTimeLocal: String
    public let startTimeGMT: String?
    public let activityType: ActivityTypeDTO
    public let distance: Double?
    public let duration: Double?
    public let hasPolyline: Bool?

    public var id: Int64 { activityId }

    public var typeKey: String { activityType.typeKey }

    public init(
        activityId: Int64,
        activityName: String,
        startTimeLocal: String,
        startTimeGMT: String? = nil,
        activityType: ActivityTypeDTO,
        distance: Double? = nil,
        duration: Double? = nil,
        hasPolyline: Bool? = nil
    ) {
        self.activityId = activityId
        self.activityName = activityName
        self.startTimeLocal = startTimeLocal
        self.startTimeGMT = startTimeGMT
        self.activityType = activityType
        self.distance = distance
        self.duration = duration
        self.hasPolyline = hasPolyline
    }

    enum CodingKeys: String, CodingKey {
        case activityId
        case activityName
        case startTimeLocal
        case startTimeGMT
        case activityType
        case distance
        case duration
        case hasPolyline
    }

    public init(from decoder: Decoder) throws {
        let container = try decoder.container(keyedBy: CodingKeys.self)
        self.activityId = try container.decode(Int64.self, forKey: .activityId)
        self.activityName = try container.decodeIfPresent(String.self, forKey: .activityName) ?? ""
        self.startTimeLocal = try container.decodeIfPresent(String.self, forKey: .startTimeLocal) ?? ""
        self.startTimeGMT = try container.decodeIfPresent(String.self, forKey: .startTimeGMT)
        self.activityType = try container.decodeIfPresent(ActivityTypeDTO.self, forKey: .activityType)
            ?? ActivityTypeDTO(typeKey: "uncategorized")
        self.distance = try container.decodeIfPresent(Double.self, forKey: .distance)
        self.duration = try container.decodeIfPresent(Double.self, forKey: .duration)
        self.hasPolyline = try container.decodeIfPresent(Bool.self, forKey: .hasPolyline)
    }

    // MARK: - 5-Column Formatting Helpers

    /// 活动展示名称：若 activityName 为空则回退为 typeKey
    public var displayName: String {
        let trimmed = activityName.trimmingCharacters(in: .whitespacesAndNewlines)
        return trimmed.isEmpty ? typeKey : trimmed
    }

    /// 格式化本地开始日期时间
    public var formattedDateTime: String {
        let trimmed = startTimeLocal.trimmingCharacters(in: .whitespacesAndNewlines)
        return trimmed.isEmpty ? "-" : trimmed
    }

    /// 提取 YYYY-MM-DD 供文件命名使用
    public var datePrefix: String {
        let trimmed = startTimeLocal.trimmingCharacters(in: .whitespacesAndNewlines)
        if trimmed.count >= 10 {
            let prefix = String(trimmed.prefix(10))
            if Self.isValidDatePrefix(prefix) {
                return prefix
            }
        }
        if let gmt = startTimeGMT?.trimmingCharacters(in: .whitespacesAndNewlines), gmt.count >= 10 {
            let prefix = String(gmt.prefix(10))
            if Self.isValidDatePrefix(prefix) {
                return prefix
            }
        }
        return "1970-01-01"
    }

    /// 格式化距离：>= 1000m 显示 "X.XX km"，< 1000m 显示 "X m"，nil 或 <= 0 显示 "-"
    public var formattedDistance: String {
        guard let distance = distance, distance > 0 else {
            return "-"
        }
        if distance >= 1000.0 {
            return String(format: "%.2f km", distance / 1000.0)
        } else {
            return String(format: "%.0f m", distance)
        }
    }

    /// 格式化时长：格式化为 "HH:mm:ss" 或 "mm:ss"
    public var formattedDuration: String {
        guard let duration = duration, duration > 0 else {
            return "-"
        }
        let totalSeconds = Int(duration.rounded())
        let hours = totalSeconds / 3600
        let minutes = (totalSeconds % 3600) / 60
        let seconds = totalSeconds % 60
        if hours > 0 {
            return String(format: "%02d:%02d:%02d", hours, minutes, seconds)
        } else {
            return String(format: "%02d:%02d", minutes, seconds)
        }
    }

    /// 解析 startTimeGMT 或 startTimeLocal 供多活动合并按时间升序排序
    public var startGMTDate: Date? {
        if let gmt = startTimeGMT, let date = Self.parseGarminDateString(gmt, timeZone: TimeZone(secondsFromGMT: 0)!) {
            return date
        }
        if let date = Self.parseGarminDateString(startTimeLocal, timeZone: TimeZone(secondsFromGMT: 0)!) {
            return date
        }
        return nil
    }

    private static func isValidDatePrefix(_ prefix: String) -> Bool {
        let bytes = Array(prefix.utf8)
        guard bytes.count == 10 else { return false }
        let dashIndices = [4, 7]
        for (index, byte) in bytes.enumerated() {
            if dashIndices.contains(index) {
                if byte != UInt8(ascii: "-") { return false }
            } else {
                if byte < UInt8(ascii: "0") || byte > UInt8(ascii: "9") { return false }
            }
        }
        return true
    }

    private static func parseGarminDateString(_ raw: String, timeZone: TimeZone) -> Date? {
        let trimmed = raw.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmed.isEmpty else { return nil }

        let formatter = DateFormatter()
        formatter.locale = Locale(identifier: "en_US_POSIX")
        formatter.timeZone = timeZone

        for format in ["yyyy-MM-dd HH:mm:ss", "yyyy-MM-dd'T'HH:mm:ss.SSS", "yyyy-MM-dd'T'HH:mm:ss", "yyyy-MM-dd HH:mm"] {
            formatter.dateFormat = format
            if let date = formatter.date(from: trimmed) {
                return date
            }
        }

        let isoFormatter = ISO8601DateFormatter()
        if let date = isoFormatter.date(from: trimmed) {
            return date
        }
        return nil
    }
}
