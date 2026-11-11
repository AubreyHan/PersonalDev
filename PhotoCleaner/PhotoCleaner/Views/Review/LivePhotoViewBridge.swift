import SwiftUI
import AppKit
import Photos
import PhotosUI

/// 桥接 macOS 原生 PHLivePhotoView，支持悬停与快捷键 (L) 触发完整动态与声音播放
public struct LivePhotoViewBridge: NSViewRepresentable {
    public let livePhoto: PHLivePhoto?
    public let playbackTrigger: Int
    public let isMuted: Bool

    public init(
        livePhoto: PHLivePhoto?,
        playbackTrigger: Int = 0,
        isMuted: Bool = false
    ) {
        self.livePhoto = livePhoto
        self.playbackTrigger = playbackTrigger
        self.isMuted = isMuted
    }

    public func makeCoordinator() -> Coordinator {
        Coordinator()
    }

    public func makeNSView(context: Context) -> PHLivePhotoView {
        let view = PHLivePhotoView()
        view.contentMode = .aspectFit
        view.isMuted = isMuted
        view.livePhoto = livePhoto
        context.coordinator.lastPlaybackTrigger = playbackTrigger
        context.coordinator.currentLivePhotoID = livePhoto.map { ObjectIdentifier($0) }
        return view
    }

    public func updateNSView(_ nsView: PHLivePhotoView, context: Context) {
        nsView.isMuted = isMuted
        let incomingID = livePhoto.map { ObjectIdentifier($0) }
        if context.coordinator.currentLivePhotoID != incomingID {
            context.coordinator.currentLivePhotoID = incomingID
            nsView.stopPlayback()
            nsView.livePhoto = livePhoto
        }
        if context.coordinator.lastPlaybackTrigger != playbackTrigger {
            context.coordinator.lastPlaybackTrigger = playbackTrigger
            if nsView.livePhoto != nil {
                nsView.startPlayback(with: .full)
            }
        }
    }

    public static func dismantleNSView(_ nsView: PHLivePhotoView, coordinator: Coordinator) {
        nsView.stopPlayback()
        nsView.livePhoto = nil
    }

    public final class Coordinator {
        var lastPlaybackTrigger: Int = 0
        var currentLivePhotoID: ObjectIdentifier?
    }
}
