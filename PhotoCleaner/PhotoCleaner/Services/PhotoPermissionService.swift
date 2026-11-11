import AppKit
import Foundation
import Observation
import Photos

@MainActor
@Observable
public final class PhotoPermissionService {
    public static let shared = PhotoPermissionService()

    public private(set) var authorizationStatus: PHAuthorizationStatus

    public var status: PHAuthorizationStatus {
        authorizationStatus
    }

    public var isAuthorized: Bool {
        authorizationStatus == .authorized
    }

    public var isLimited: Bool {
        authorizationStatus == .limited
    }

    public var isDeniedOrRestricted: Bool {
        authorizationStatus == .denied || authorizationStatus == .restricted
    }

    public init() {
        self.authorizationStatus = PHPhotoLibrary.authorizationStatus(for: .readWrite)
    }

    @discardableResult
    public func refreshStatus() -> PHAuthorizationStatus {
        let current = PHPhotoLibrary.authorizationStatus(for: .readWrite)
        self.authorizationStatus = current
        return current
    }

    @discardableResult
    public func checkAuthorizationStatus() -> PHAuthorizationStatus {
        refreshStatus()
    }

    @discardableResult
    public static func currentAuthorizationStatus() -> PHAuthorizationStatus {
        PHPhotoLibrary.authorizationStatus(for: .readWrite)
    }

    @discardableResult
    public func requestAuthorization() async -> PHAuthorizationStatus {
        let current = PHPhotoLibrary.authorizationStatus(for: .readWrite)
        if current == .notDetermined {
            let newStatus = await PHPhotoLibrary.requestAuthorization(for: .readWrite)
            self.authorizationStatus = newStatus
            return newStatus
        } else {
            self.authorizationStatus = current
            return current
        }
    }

    @discardableResult
    public static func requestReadWriteAuthorization() async -> PHAuthorizationStatus {
        await shared.requestAuthorization()
    }

    public func openSystemSettings() {
        Self.openSystemPrivacyPhotosSettings()
    }

    public static func openSystemPrivacyPhotosSettings() {
        let candidateURLs = [
            "x-apple.systempreferences:com.apple.preference.security?Privacy_Photos",
            "x-apple.systempreferences:com.apple.settings.PrivacySecurity.extension?Privacy_Photos",
            "x-apple.systempreferences:com.apple.preference.security"
        ]
        for urlString in candidateURLs {
            if let url = URL(string: urlString), NSWorkspace.shared.open(url) {
                return
            }
        }
    }
}
