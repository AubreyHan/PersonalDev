import Foundation

public enum ExportFormat: String, Codable, CaseIterable, Identifiable, Sendable {
    case gpx
    case tcx
    case fit

    public var id: String { rawValue }

    public var fileExtension: String {
        switch self {
        case .gpx:
            return "gpx"
        case .tcx:
            return "tcx"
        case .fit:
            return "fit"
        }
    }

    public var displayName: String {
        switch self {
        case .gpx:
            return "GPX"
        case .tcx:
            return "TCX"
        case .fit:
            return "FIT"
        }
    }
}
