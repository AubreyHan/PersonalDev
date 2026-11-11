import Foundation
import Security

public enum KeychainError: LocalizedError, Equatable, Sendable {
    case encodingFailed
    case decodingFailed
    case unhandledError(status: OSStatus)

    public var errorDescription: String? {
        switch self {
        case .encodingFailed:
            return "凭据编码失败"
        case .decodingFailed:
            return "凭据解码失败"
        case .unhandledError(let status):
            return "钥匙串访问错误 (OSStatus: \(status))"
        }
    }
}

public struct StoredCredentials: Codable, Equatable, Sendable {
    public let apiKey: String
    public let region: GarminRegion

    public init(apiKey: String, region: GarminRegion) {
        self.apiKey = apiKey
        self.region = region
    }
}

public protocol KeychainStoring: Sendable {
    func saveCredentials(apiKey: String, region: GarminRegion) throws
    func loadCredentials() throws -> StoredCredentials?
    func clearCredentials() throws
}

public final class KeychainService: KeychainStoring, Sendable {
    public static let shared = KeychainService()
    public let service: String
    private let account = "personal_api_credentials"

    public init(service: String = "com.garmin.activity.exporter") {
        self.service = service
    }

    public func saveCredentials(apiKey: String, region: GarminRegion) throws {
        let credentials = StoredCredentials(apiKey: apiKey, region: region)
        guard let data = try? JSONEncoder().encode(credentials) else {
            throw KeychainError.encodingFailed
        }

        let query: [String: Any] = [
            kSecClass as String: kSecClassGenericPassword,
            kSecAttrService as String: service,
            kSecAttrAccount as String: account
        ]

        let attributesToUpdate: [String: Any] = [
            kSecValueData as String: data
        ]

        let updateStatus = SecItemUpdate(query as CFDictionary, attributesToUpdate as CFDictionary)
        if updateStatus == errSecItemNotFound {
            var addQuery = query
            addQuery[kSecValueData as String] = data
            let addStatus = SecItemAdd(addQuery as CFDictionary, nil)
            guard addStatus == errSecSuccess else {
                throw KeychainError.unhandledError(status: addStatus)
            }
        } else if updateStatus != errSecSuccess {
            throw KeychainError.unhandledError(status: updateStatus)
        }
    }

    public func loadCredentials() throws -> StoredCredentials? {
        let query: [String: Any] = [
            kSecClass as String: kSecClassGenericPassword,
            kSecAttrService as String: service,
            kSecAttrAccount as String: account,
            kSecReturnData as String: true,
            kSecMatchLimit as String: kSecMatchLimitOne
        ]

        var item: CFTypeRef?
        let status = SecItemCopyMatching(query as CFDictionary, &item)

        if status == errSecItemNotFound {
            return nil
        }

        guard status == errSecSuccess else {
            throw KeychainError.unhandledError(status: status)
        }

        guard let data = item as? Data,
              let credentials = try? JSONDecoder().decode(StoredCredentials.self, from: data) else {
            throw KeychainError.decodingFailed
        }

        return credentials
    }

    public func clearCredentials() throws {
        let query: [String: Any] = [
            kSecClass as String: kSecClassGenericPassword,
            kSecAttrService as String: service,
            kSecAttrAccount as String: account
        ]

        let status = SecItemDelete(query as CFDictionary)
        guard status == errSecSuccess || status == errSecItemNotFound else {
            throw KeychainError.unhandledError(status: status)
        }
    }
}
