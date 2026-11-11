import Foundation

public enum AuthServiceError: LocalizedError, Equatable, Sendable {
    case emptyApiKey
    case invalidApiKey
    case invalidTokenResponse
    case networkError(String)

    public var errorDescription: String? {
        switch self {
        case .emptyApiKey:
            return "请输入 Personal API Key"
        case .invalidApiKey:
            return "Personal API Key 无效或已过期"
        case .invalidTokenResponse:
            return "服务器返回的 Token 格式无效"
        case .networkError(let message):
            return "网络请求失败：\(message)"
        }
    }
}

public struct AuthSession: Equatable, Sendable {
    public let apiKey: String
    public let region: GarminRegion
    public let accessToken: String
    public let expiresAt: Date

    public init(apiKey: String, region: GarminRegion, accessToken: String, expiresAt: Date) {
        self.apiKey = apiKey
        self.region = region
        self.accessToken = accessToken
        self.expiresAt = expiresAt
    }
}

public protocol AuthServicing: Sendable {
    func authenticateAndDetectRegion(apiKey: String) async throws -> AuthSession
    func obtainValidToken(apiKey: String, region: GarminRegion, forceRefresh: Bool) async throws -> AuthSession
}

extension AuthServicing {
    public func obtainValidToken(apiKey: String, region: GarminRegion) async throws -> AuthSession {
        try await obtainValidToken(apiKey: apiKey, region: region, forceRefresh: false)
    }
}

public actor PersonalApiAuthService: AuthServicing {
    private struct CacheKey: Hashable, Sendable {
        let apiKey: String
        let region: GarminRegion
    }

    private let urlSession: URLSession
    private var sessionCache: [CacheKey: AuthSession] = [:]
    private static let tokenEndpointPath = "/personalapitoken-service/personal-api/personal-api-key/token"
    private static let defaultExpirationInterval: TimeInterval = 24 * 3600
    private static let minimumRemainingValidity: TimeInterval = 3600

    public init(urlSession: URLSession = .shared) {
        self.urlSession = urlSession
    }

    public func authenticateAndDetectRegion(apiKey: String) async throws -> AuthSession {
        let trimmedKey = apiKey.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmedKey.isEmpty else {
            throw AuthServiceError.emptyApiKey
        }

        var encounteredUnauthorized = false
        var encounteredInvalidResponse = false
        var lastNetworkError: String?

        for region in [GarminRegion.global, GarminRegion.china] {
            do {
                let session = try await requestToken(apiKey: trimmedKey, region: region)
                sessionCache[CacheKey(apiKey: trimmedKey, region: region)] = session
                return session
            } catch let error as AuthServiceError {
                switch error {
                case .invalidApiKey:
                    encounteredUnauthorized = true
                case .invalidTokenResponse:
                    encounteredInvalidResponse = true
                case .networkError(let msg):
                    lastNetworkError = msg
                case .emptyApiKey:
                    throw error
                }
            } catch {
                lastNetworkError = error.localizedDescription
            }
        }

        if encounteredUnauthorized {
            throw AuthServiceError.invalidApiKey
        }
        if encounteredInvalidResponse {
            throw AuthServiceError.invalidTokenResponse
        }
        throw AuthServiceError.networkError(lastNetworkError ?? "未知网络错误")
    }

    public func obtainValidToken(
        apiKey: String,
        region: GarminRegion,
        forceRefresh: Bool = false
    ) async throws -> AuthSession {
        let trimmedKey = apiKey.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmedKey.isEmpty else {
            throw AuthServiceError.emptyApiKey
        }

        let cacheKey = CacheKey(apiKey: trimmedKey, region: region)
        if !forceRefresh,
           let cached = sessionCache[cacheKey],
           cached.expiresAt.timeIntervalSinceNow > Self.minimumRemainingValidity {
            return cached
        }

        let freshSession = try await requestToken(apiKey: trimmedKey, region: region)
        sessionCache[cacheKey] = freshSession
        return freshSession
    }

    // MARK: - Internal Helpers

    private func requestToken(apiKey: String, region: GarminRegion) async throws -> AuthSession {
        guard let url = URL(string: Self.tokenEndpointPath, relativeTo: region.baseURL)?.absoluteURL else {
            throw AuthServiceError.networkError("无效的请求地址")
        }

        var request = URLRequest(url: url)
        request.httpMethod = "POST"
        request.setValue("ApiKey \(apiKey)", forHTTPHeaderField: "Authorization")
        request.setValue("*/*", forHTTPHeaderField: "Accept")

        let data: Data
        let response: URLResponse
        do {
            (data, response) = try await urlSession.data(for: request)
        } catch let error as AuthServiceError {
            throw error
        } catch {
            throw AuthServiceError.networkError(error.localizedDescription)
        }

        guard let httpResponse = response as? HTTPURLResponse else {
            throw AuthServiceError.networkError("无效的 HTTP 响应")
        }

        switch httpResponse.statusCode {
        case 200:
            let (token, expiresAt) = try Self.parseTokenAndExpiration(from: data)
            return AuthSession(
                apiKey: apiKey,
                region: region,
                accessToken: token,
                expiresAt: expiresAt
            )
        case 401, 403:
            throw AuthServiceError.invalidApiKey
        default:
            throw AuthServiceError.networkError("HTTP \(httpResponse.statusCode)")
        }
    }

    static func parseTokenAndExpiration(from data: Data, now: Date = Date()) throws -> (String, Date) {
        guard let rawString = String(data: data, encoding: .utf8) else {
            throw AuthServiceError.invalidTokenResponse
        }

        let trimmed = rawString.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmed.isEmpty else {
            throw AuthServiceError.invalidTokenResponse
        }

        var tokenCandidate: String?
        var jsonExpirationDate: Date?

        if trimmed.first == "{" {
            guard let jsonObject = try? JSONSerialization.jsonObject(with: Data(trimmed.utf8)) as? [String: Any] else {
                throw AuthServiceError.invalidTokenResponse
            }
            if let accessToken = (jsonObject["access_token"] as? String)
                ?? (jsonObject["accessToken"] as? String)
                ?? (jsonObject["token"] as? String) {
                let cleanToken = accessToken.trimmingCharacters(in: .whitespacesAndNewlines)
                if !cleanToken.isEmpty {
                    tokenCandidate = cleanToken
                }
            }
            if let expNumber = jsonObject["exp"] as? NSNumber {
                jsonExpirationDate = Date(timeIntervalSince1970: expNumber.doubleValue)
            } else if let expiresIn = jsonObject["expires_in"] as? NSNumber {
                jsonExpirationDate = now.addingTimeInterval(expiresIn.doubleValue)
            }
        } else {
            let unquoted = trimmed.trimmingCharacters(in: CharacterSet(charactersIn: "\"'"))
            if isValidJWTStructure(unquoted) {
                tokenCandidate = unquoted
            }
        }

        guard let token = tokenCandidate, !token.isEmpty else {
            throw AuthServiceError.invalidTokenResponse
        }

        if let jwtExpiration = extractJWTExpiration(from: token) {
            return (token, jwtExpiration)
        }
        if let jsonExpirationDate = jsonExpirationDate {
            return (token, jsonExpirationDate)
        }
        return (token, now.addingTimeInterval(defaultExpirationInterval))
    }

    private static func isValidJWTStructure(_ token: String) -> Bool {
        let segments = token.split(separator: ".", omittingEmptySubsequences: false)
        guard segments.count == 3 else { return false }
        guard segments.allSatisfy({ !$0.isEmpty }) else { return false }
        return decodeBase64URL(String(segments[0])) != nil && decodeBase64URL(String(segments[1])) != nil
    }

    static func extractJWTExpiration(from token: String) -> Date? {
        let segments = token.split(separator: ".", omittingEmptySubsequences: false)
        guard segments.count == 3 else { return nil }
        guard let payloadData = decodeBase64URL(String(segments[1])),
              let json = try? JSONSerialization.jsonObject(with: payloadData) as? [String: Any] else {
            return nil
        }

        if let expNumber = json["exp"] as? NSNumber {
            return Date(timeIntervalSince1970: expNumber.doubleValue)
        }
        if let expString = json["exp"] as? String, let expDouble = Double(expString) {
            return Date(timeIntervalSince1970: expDouble)
        }
        return nil
    }

    private static func decodeBase64URL(_ base64URL: String) -> Data? {
        var base64 = base64URL
            .replacingOccurrences(of: "-", with: "+")
            .replacingOccurrences(of: "_", with: "/")
        let remainder = base64.count % 4
        if remainder > 0 {
            base64 += String(repeating: "=", count: 4 - remainder)
        }
        return Data(base64Encoded: base64)
    }
}
