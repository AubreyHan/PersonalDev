import Foundation

public enum GarminApiError: LocalizedError, Equatable, Sendable {
    case unauthorized
    case httpError(statusCode: Int, message: String)
    case emptyDownload
    case networkError(String)

    public var errorDescription: String? {
        switch self {
        case .unauthorized:
            return "认证已过期或未授权 (HTTP 401)"
        case .httpError(let statusCode, let message):
            return message.isEmpty ? "服务器返回错误 (HTTP \(statusCode))" : "服务器错误 (HTTP \(statusCode)): \(message)"
        case .emptyDownload:
            return "下载的活动文件为空"
        case .networkError(let message):
            return "网络请求错误：\(message)"
        }
    }
}

public protocol GarminApiClientProtocol: Sendable {
    func fetchActivities(from startDate: Date, to endDate: Date, region: GarminRegion, accessToken: String) async throws -> [ActivityItem]
    func downloadActivityZip(activityId: Int64, region: GarminRegion, accessToken: String) async throws -> Data
}

extension GarminApiClientProtocol {
    public func fetchActivities(from startDate: Date, region: GarminRegion, accessToken: String) async throws -> [ActivityItem] {
        try await fetchActivities(from: startDate, to: Date(), region: region, accessToken: accessToken)
    }
}

public final class GarminApiClient: GarminApiClientProtocol, Sendable {
    private let urlSession: URLSession
    private static let pageLimit = 99
    private static let activitySearchPath = "/activitylist-service/activities/search/activities"
    private static let activityDownloadPathPrefix = "/download-service/files/activity"

    public init(urlSession: URLSession = .shared) {
        self.urlSession = urlSession
    }

    public func fetchActivities(
        from startDate: Date,
        to endDate: Date = Date(),
        region: GarminRegion,
        accessToken: String
    ) async throws -> [ActivityItem] {
        let startDateString = Self.formatLocalStartDate(startDate)
        let endDateString = Self.formatLocalStartDate(endDate)
        var startOffset = 0
        var seenIDs = Set<Int64>()
        var collectedActivities: [ActivityItem] = []
        let decoder = JSONDecoder()

        while true {
            guard var components = URLComponents(
                url: region.baseURL.appendingPathComponent(Self.activitySearchPath),
                resolvingAgainstBaseURL: true
            ) else {
                throw GarminApiError.networkError("无法构建活动列表查询 URL")
            }

            components.queryItems = [
                URLQueryItem(name: "start", value: String(startOffset)),
                URLQueryItem(name: "limit", value: String(Self.pageLimit)),
                URLQueryItem(name: "startDate", value: startDateString),
                URLQueryItem(name: "endDate", value: endDateString)
            ]

            guard let url = components.url else {
                throw GarminApiError.networkError("无效的活动列表查询 URL")
            }

            var request = URLRequest(url: url)
            request.httpMethod = "GET"
            request.setValue("Bearer \(accessToken)", forHTTPHeaderField: "Authorization")
            request.setValue("application/json", forHTTPHeaderField: "Accept")

            let (data, httpResponse) = try await performRequest(request)

            switch httpResponse.statusCode {
            case 200:
                let pageItems: [ActivityItem]
                do {
                    pageItems = try decoder.decode([ActivityItem].self, from: data)
                } catch {
                    throw GarminApiError.networkError("解析活动列表响应失败: \(error.localizedDescription)")
                }

                var newlyAddedCount = 0
                for item in pageItems {
                    if seenIDs.insert(item.activityId).inserted {
                        collectedActivities.append(item)
                        newlyAddedCount += 1
                    }
                }

                if pageItems.count < Self.pageLimit || newlyAddedCount == 0 {
                    return collectedActivities
                }

                startOffset += Self.pageLimit

            case 401:
                throw GarminApiError.unauthorized

            default:
                let message = String(data: data, encoding: .utf8)?.trimmingCharacters(in: .whitespacesAndNewlines) ?? ""
                throw GarminApiError.httpError(statusCode: httpResponse.statusCode, message: message)
            }
        }
    }

    public func downloadActivityZip(
        activityId: Int64,
        region: GarminRegion,
        accessToken: String
    ) async throws -> Data {
        let url = region.baseURL
            .appendingPathComponent(Self.activityDownloadPathPrefix)
            .appendingPathComponent(String(activityId))

        var request = URLRequest(url: url)
        request.httpMethod = "GET"
        request.setValue("Bearer \(accessToken)", forHTTPHeaderField: "Authorization")
        request.setValue(
            "application/zip, application/x-zip-compressed, application/octet-stream",
            forHTTPHeaderField: "Accept"
        )

        let (data, httpResponse) = try await performRequest(request)

        switch httpResponse.statusCode {
        case 200:
            guard !data.isEmpty else {
                throw GarminApiError.emptyDownload
            }
            return data

        case 401:
            throw GarminApiError.unauthorized

        default:
            let message = String(data: data, encoding: .utf8)?.trimmingCharacters(in: .whitespacesAndNewlines) ?? ""
            throw GarminApiError.httpError(statusCode: httpResponse.statusCode, message: message)
        }
    }

    // MARK: - Private Helpers

    private func performRequest(_ request: URLRequest) async throws -> (Data, HTTPURLResponse) {
        let data: Data
        let response: URLResponse
        do {
            (data, response) = try await urlSession.data(for: request)
        } catch let error as GarminApiError {
            throw error
        } catch {
            throw GarminApiError.networkError(error.localizedDescription)
        }

        guard let httpResponse = response as? HTTPURLResponse else {
            throw GarminApiError.networkError("无效的 HTTP 响应")
        }

        return (data, httpResponse)
    }

    static func formatLocalStartDate(_ date: Date, timeZone: TimeZone = .current) -> String {
        let formatter = DateFormatter()
        formatter.locale = Locale(identifier: "en_US_POSIX")
        formatter.timeZone = timeZone
        formatter.dateFormat = "yyyy-MM-dd"
        return formatter.string(from: date)
    }
}
