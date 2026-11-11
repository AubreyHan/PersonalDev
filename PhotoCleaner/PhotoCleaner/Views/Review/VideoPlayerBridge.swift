import SwiftUI
import AppKit
import AVKit
import AVFoundation

/// 桥接 macOS 原生 AVPlayerView，支持 HDR、慢动作与 iCloud 流式视频播放
public struct VideoPlayerBridge: NSViewRepresentable {
    public let playerItem: AVPlayerItem?
    public let autoPlay: Bool
    public let isMuted: Bool

    public init(
        playerItem: AVPlayerItem?,
        autoPlay: Bool = true,
        isMuted: Bool = false
    ) {
        self.playerItem = playerItem
        self.autoPlay = autoPlay
        self.isMuted = isMuted
    }

    public func makeCoordinator() -> Coordinator {
        Coordinator()
    }

    public func makeNSView(context: Context) -> AVPlayerView {
        let playerView = AVPlayerView()
        playerView.controlsStyle = .floating
        playerView.showsFullScreenToggleButton = false
        playerView.videoGravity = .resizeAspect

        let player = AVPlayer()
        player.isMuted = isMuted
        playerView.player = player
        context.coordinator.player = player

        if let playerItem {
            context.coordinator.currentItemID = ObjectIdentifier(playerItem)
            player.replaceCurrentItem(with: playerItem)
            if autoPlay {
                player.play()
            }
        }
        return playerView
    }

    public func updateNSView(_ nsView: AVPlayerView, context: Context) {
        guard let player = context.coordinator.player else { return }
        player.isMuted = isMuted

        let incomingID = playerItem.map { ObjectIdentifier($0) }
        if context.coordinator.currentItemID != incomingID {
            context.coordinator.currentItemID = incomingID
            player.pause()
            player.replaceCurrentItem(with: playerItem)
            if playerItem != nil && autoPlay {
                player.play()
            }
        }
    }

    public static func dismantleNSView(_ nsView: AVPlayerView, coordinator: Coordinator) {
        coordinator.player?.pause()
        coordinator.player?.replaceCurrentItem(with: nil)
        nsView.player = nil
    }

    public final class Coordinator {
        var player: AVPlayer?
        var currentItemID: ObjectIdentifier?
    }
}
