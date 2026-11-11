import Foundation

/// 表示公历全年 366 个固定“月-日”切片之一（含 2月29日）
public struct MonthDay: Hashable, Codable, Comparable, Identifiable, Sendable {
    public let month: Int // 1...12
    public let day: Int   // 1...31

    public var id: String { storageKey }

    public init(month: Int, day: Int) {
        self.month = month
        self.day = day
    }

    /// 从 "MM-DD" 紧凑键解析 `MonthDay`
    public init?(storageKey: String) {
        let parts = storageKey.split(separator: "-")
        guard parts.count == 2,
              let m = Int(parts[0]),
              let d = Int(parts[1]),
              (1...12).contains(m),
              (1...Self.daysInMonth(m)).contains(d) else {
            return nil
        }
        self.month = m
        self.day = d
    }

    /// 唯一紧凑键，格式 "MM-DD"，如 "08-13"、"02-29"，用于 JSON 字典持久化 Key
    public var storageKey: String {
        String(format: "%02d-%02d", month, day)
    }

    /// 中文展示标题，如 "8月13日"
    public var displayTitle: String {
        "\(month)月\(day)日"
    }

    public static func < (lhs: MonthDay, rhs: MonthDay) -> Bool {
        lhs.month != rhs.month ? lhs.month < rhs.month : lhs.day < rhs.day
    }

    /// 按系统日历提取指定日期的月-日
    public static func from(date: Date, calendar: Calendar = .current) -> MonthDay {
        let components = calendar.dateComponents([.month, .day], from: date)
        return MonthDay(
            month: components.month ?? 1,
            day: components.day ?? 1
        )
    }

    /// 当前系统日期对应的月-日
    public static func today(calendar: Calendar = .current, now: Date = Date()) -> MonthDay {
        from(date: now, calendar: calendar)
    }

    /// 每个公历月份的最大天数（2 月固定按闰年 29 天计，EC-1）
    public static func daysInMonth(_ month: Int) -> Int {
        switch month {
        case 1, 3, 5, 7, 8, 10, 12:
            return 31
        case 4, 6, 9, 11:
            return 30
        case 2:
            return 29
        default:
            return 0
        }
    }

    /// 获取某个月份的所有日期切片
    public static func days(forMonth month: Int) -> [MonthDay] {
        let count = daysInMonth(month)
        guard count > 0 else { return [] }
        return (1...count).map { MonthDay(month: month, day: $0) }
    }

    /// 全年固定 366 天序列（01-01 至 12-31，永久包含 02-29）
    public static let all366Days: [MonthDay] = {
        (1...12).flatMap { month in
            days(forMonth: month)
        }
    }()

    /// 四态判定纯函数（FR-2.3 / SPEC §1.1）
    public static func computeStatus(currentIDs: Set<String>, reviewedIDs: Set<String>) -> DayReviewStatus {
        if currentIDs.isEmpty {
            return .empty
        }
        if reviewedIDs.isEmpty {
            return .pending
        }
        let unreviewed = currentIDs.subtracting(reviewedIDs)
        return unreviewed.isEmpty ? .completed : .hasNew
    }

    /// 计算单个日历格的聚合摘要
    public static func computeSummary(
        monthDay: MonthDay,
        currentIDs: Set<String>,
        reviewedIDs: Set<String>
    ) -> DaySliceSummary {
        let status = computeStatus(currentIDs: currentIDs, reviewedIDs: reviewedIDs)
        let newCount: Int
        switch status {
        case .empty, .completed:
            newCount = 0
        case .pending:
            newCount = currentIDs.count
        case .hasNew:
            newCount = currentIDs.subtracting(reviewedIDs).count
        }
        return DaySliceSummary(
            monthDay: monthDay,
            totalCount: currentIDs.count,
            newUnreviewedCount: newCount,
            status: status
        )
    }
}

/// 某月-日切片的四态整理进度
public enum DayReviewStatus: String, Codable, Hashable, CaseIterable, Sendable {
    case empty      // 历年该日 0 张照片/视频（置灰，自动跳过）
    case pending    // 有资产，且从未标记完成（reviewedIDs 为空）
    case completed  // 有资产，且当前全部存量资产 ID 均属于 reviewedIDs
    case hasNew     // 曾标记完成，但当前存在差集 (currentIDs - reviewedIDs 非空)

    public var displayTitle: String {
        switch self {
        case .empty: return "空置"
        case .pending: return "待整理"
        case .completed: return "已整理"
        case .hasNew: return "有新增待审"
        }
    }
}

/// 单个日历格的聚合摘要
public struct DaySliceSummary: Identifiable, Hashable, Codable, Sendable {
    public var id: String { monthDay.storageKey }
    public let monthDay: MonthDay
    public let totalCount: Int
    public let newUnreviewedCount: Int
    public let status: DayReviewStatus

    public init(
        monthDay: MonthDay,
        totalCount: Int,
        newUnreviewedCount: Int,
        status: DayReviewStatus
    ) {
        self.monthDay = monthDay
        self.totalCount = totalCount
        self.newUnreviewedCount = newUnreviewedCount
        self.status = status
    }
}
