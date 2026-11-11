import Foundation

public enum GarminRegion: String, Codable, CaseIterable, Sendable {
    case global
    case china

    public var baseURL: URL {
        switch self {
        case .global:
            return URL(string: "https://personal.connectapi.garmin.com")!
        case .china:
            return URL(string: "https://personal.connectapi.garmin.cn")!
        }
    }

    public var displayName: String {
        switch self {
        case .global:
            return "国际区 (Global)"
        case .china:
            return "中国区 (China)"
        }
    }
}
