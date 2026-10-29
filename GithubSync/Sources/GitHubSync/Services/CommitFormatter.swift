import Foundation

public final class CommitFormatter {
    public static let shared = CommitFormatter()

    private let dateFormatter: DateFormatter

    private init() {
        let df = DateFormatter()
        df.dateFormat = "yyyy-MM-dd HH:mm:ss"
        self.dateFormatter = df
    }

    public func format(statusLines: [String], date: Date = Date()) -> String {
        let timestamp = dateFormatter.string(from: date)

        var entries: [(status: String, fileName: String)] = []

        for line in statusLines {
            guard line.count >= 3 else { continue }
            let prefix = String(line.prefix(2))
            let pathPart = String(line.dropFirst(3)).trimmingCharacters(in: .whitespaces)
            let cleanPath = pathPart.trimmingCharacters(in: CharacterSet(charactersIn: "\""))
            let fileName = URL(fileURLWithPath: cleanPath).lastPathComponent

            let statusText: String
            if prefix.contains("M") {
                statusText = "modified"
            } else if prefix.contains("A") || prefix.contains("?") {
                statusText = "added"
            } else if prefix.contains("D") {
                statusText = "deleted"
            } else if prefix.contains("R") {
                statusText = "renamed"
            } else {
                statusText = "updated"
            }

            entries.append((status: statusText, fileName: fileName))
        }

        let total = entries.count
        if total == 0 {
            return "auto-sync: \(timestamp)"
        }

        if total <= 3 {
            let details = entries.map { "\($0.status): \($0.fileName)" }.joined(separator: "; ")
            return "auto-sync: \(timestamp) - \(total) files (\(details))"
        } else {
            let firstTwo = entries.prefix(2).map { "\($0.status): \($0.fileName)" }.joined(separator: ", ")
            let remaining = total - 2
            return "auto-sync: \(timestamp) - \(total) files (\(firstTwo), +\(remaining) more)"
        }
    }
}
