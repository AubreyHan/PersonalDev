import SwiftUI
import AppKit
import AVFoundation
import Photos
import PhotosUI

/// 大图快审主界面容器（支持高清静态图缩放、Live Photo 实况播放、视频流播放、顶部工具栏与底部同步胶片条，FR-4.1 ~ FR-4.5）
public struct QuickReviewContainer: View {
    @Bindable public var daySliceVM: DaySliceViewModel
    public let trashBasketVM: TrashBasketViewModel
    public let mediaLoadService: MediaLoadService

    @State private var highResImage: NSImage?
    @State private var livePhoto: PHLivePhoto?
    @State private var playerItem: AVPlayerItem?
    @State private var isLoadingMedia: Bool = false
    @State private var activeRequestID: PHImageRequestID?

    public init(
        daySliceVM: DaySliceViewModel,
        trashBasketVM: TrashBasketViewModel? = nil,
        mediaLoadService: MediaLoadService = .shared
    ) {
        self.daySliceVM = daySliceVM
        self.trashBasketVM = trashBasketVM ?? daySliceVM.trashBasketVM
        self.mediaLoadService = mediaLoadService
    }

    public init(appViewModel: AppViewModel) {
        self.daySliceVM = appViewModel.daySliceVM
        self.trashBasketVM = appViewModel.trashBasketVM
        self.mediaLoadService = appViewModel.mediaLoadService
    }

    private var filmstripAssets: [AssetItem] {
        let ordered = daySliceVM.yearSections.flatMap(\.assets)
        return ordered.isEmpty ? daySliceVM.filteredAssets : ordered
    }

    private var currentAsset: AssetItem? {
        daySliceVM.activeReviewAsset
    }

    private var currentIndexDescription: String {
        guard let current = currentAsset,
              let idx = filmstripAssets.firstIndex(where: { $0.id == current.id }) else {
            return "0 / 0"
        }
        return "\(idx + 1) / \(filmstripAssets.count)"
    }

    public var body: some View {
        VStack(spacing: 0) {
            topReviewToolbar
            Divider()

            ZStack {
                Color.black.opacity(0.94)

                if let asset = currentAsset {
                    mediaStage(for: asset)
                } else {
                    VStack(spacing: 10) {
                        Image(systemName: "photo")
                            .font(.system(size: 40))
                            .foregroundStyle(.secondary)
                        Text("当前无审阅项")
                            .font(.headline)
                            .foregroundStyle(.secondary)
                    }
                }
            }
            .frame(maxWidth: .infinity, maxHeight: .infinity)

            Divider()
            bottomFilmstripBar
        }
        .background(Color(nsColor: .windowBackgroundColor))
        .onAppear {
            loadMedia(for: currentAsset)
        }
        .onChange(of: currentAsset?.id) { _, _ in
            loadMedia(for: currentAsset)
        }
        .onDisappear {
            cancelActiveMediaRequest()
        }
    }

    // MARK: - Top Review Toolbar

    private var topReviewToolbar: some View {
        HStack(spacing: 12) {
            Button {
                daySliceVM.exitQuickReview()
            } label: {
                HStack(spacing: 5) {
                    Image(systemName: "chevron.left")
                    Text("返回网格 (Space / Esc)")
                }
            }
            .buttonStyle(.bordered)
            .controlSize(.small)

            if let asset = currentAsset {
                HStack(spacing: 8) {
                    Text("\(asset.year)年\(asset.monthDay.month)月\(asset.monthDay.day)日")
                        .font(.subheadline.weight(.semibold))

                    Text(currentIndexDescription)
                        .font(.caption.monospacedDigit())
                        .padding(.horizontal, 7)
                        .padding(.vertical, 2)
                        .foregroundStyle(.secondary)
                        .background(Color.secondary.opacity(0.14), in: Capsule())

                    if asset.isLivePhoto {
                        Label("实况", systemImage: "livephoto")
                            .font(.caption2.weight(.medium))
                            .padding(.horizontal, 6)
                            .padding(.vertical, 2)
                            .background(Color.yellow.opacity(0.16), in: Capsule())
                    } else if asset.isVideo, let dur = asset.formattedDuration {
                        Label(dur, systemImage: "video.fill")
                            .font(.caption2.monospacedDigit().weight(.medium))
                            .padding(.horizontal, 6)
                            .padding(.vertical, 2)
                            .background(Color.blue.opacity(0.16), in: Capsule())
                    }

                    if trashBasketVM.isTrashed(asset.id) {
                        Label("已在待删篮", systemImage: "trash.fill")
                            .font(.caption2.weight(.semibold))
                            .padding(.horizontal, 7)
                            .padding(.vertical, 2)
                            .foregroundStyle(.white)
                            .background(Color.red, in: Capsule())
                    }
                }
            }

            Spacer()

            // iCloud 下载进度指示器
            if let progress = daySliceVM.iCloudDownloadProgress, progress > 0 && progress < 1.0 {
                HStack(spacing: 6) {
                    ProgressView(value: progress)
                        .progressViewStyle(.circular)
                        .controlSize(.small)
                    Text("iCloud \(Int(progress * 100))%")
                        .font(.caption2.monospacedDigit())
                        .foregroundStyle(.secondary)
                }
                .padding(.horizontal, 8)
                .padding(.vertical, 3)
                .background(Color.blue.opacity(0.12), in: Capsule())
            }

            if let asset = currentAsset {
                // Live Photo 播放按钮 (`L`)
                if asset.isLivePhoto {
                    Button {
                        daySliceVM.triggerLivePhotoPlayback()
                    } label: {
                        Label("播放实况 (L)", systemImage: "livephoto.play")
                    }
                    .buttonStyle(.bordered)
                    .controlSize(.small)
                }

                // 收藏切换按钮 (`F`)
                Button {
                    Task { @MainActor in
                        await daySliceVM.toggleFavorite(for: asset.id)
                    }
                } label: {
                    HStack(spacing: 4) {
                        Image(systemName: asset.isFavorite ? "heart.fill" : "heart")
                            .foregroundStyle(asset.isFavorite ? .pink : .primary)
                        Text(asset.isFavorite ? "已收藏 (F)" : "收藏 (F)")
                    }
                }
                .buttonStyle(.bordered)
                .controlSize(.small)
                .help("同步修改系统相册收藏 ❤️ 状态 (快捷键 F)")

                // 待删暂存篮切换按钮 (`Delete` / `X`)
                let isTrashed = trashBasketVM.isTrashed(asset.id)
                Button(role: isTrashed ? nil : .destructive) {
                    daySliceVM.toggleTrashForSelection()
                } label: {
                    HStack(spacing: 4) {
                        Image(systemName: isTrashed ? "arrow.uturn.backward" : "trash")
                        Text(isTrashed ? "移出待删篮 (X)" : "加入待删篮 (Delete/X)")
                    }
                }
                .buttonStyle(.borderedProminent)
                .tint(isTrashed ? .orange : .red)
                .controlSize(.small)
                .help(asset.isFavorite ? "当前照片已带 ❤️ 收藏锁定保护" : "加入或移出全局待删暂存篮 (快捷键 Delete / X)")
            }

            Divider()
                .frame(height: 18)

            // 右侧检视栏开关 (`I`)
            Button {
                daySliceVM.toggleInspector()
            } label: {
                Image(systemName: "sidebar.right")
                    .foregroundStyle(daySliceVM.isInspectorVisible ? Color.accentColor : Color.secondary)
            }
            .buttonStyle(.bordered)
            .controlSize(.small)
            .help("显示/隐藏右侧相簿归属与 EXIF 检视栏 (快捷键 I)")
        }
        .padding(.horizontal, 14)
        .padding(.vertical, 9)
        .background(.bar)
    }

    // MARK: - Media Stage (Image / Live Photo / Video)

    @ViewBuilder
    private func mediaStage(for asset: AssetItem) -> some View {
        ZStack {
            if asset.isVideo {
                if let playerItem {
                    VideoPlayerBridge(playerItem: playerItem, autoPlay: true)
                } else {
                    ZoomableImageView(
                        image: highResImage,
                        isLoading: isLoadingMedia,
                        downloadProgress: daySliceVM.iCloudDownloadProgress
                    )
                }
            } else if asset.isLivePhoto, let livePhoto {
                LivePhotoViewBridge(
                    livePhoto: livePhoto,
                    playbackTrigger: daySliceVM.livePhotoPlayTriggerCount
                )
            } else {
                ZoomableImageView(
                    image: highResImage,
                    isLoading: isLoadingMedia,
                    downloadProgress: daySliceVM.iCloudDownloadProgress
                )
            }

            // 左右翻片悬浮半透明箭头按钮
            HStack {
                Button {
                    daySliceVM.moveCursorPrevious()
                } label: {
                    Image(systemName: "chevron.left.circle.fill")
                        .font(.system(size: 30))
                        .foregroundStyle(.white.opacity(0.75))
                        .shadow(radius: 4)
                }
                .buttonStyle(.plain)
                .padding(.leading, 14)
                .help("上一张 (←)")

                Spacer()

                Button {
                    daySliceVM.moveCursorNext()
                } label: {
                    Image(systemName: "chevron.right.circle.fill")
                        .font(.system(size: 30))
                        .foregroundStyle(.white.opacity(0.75))
                        .shadow(radius: 4)
                }
                .buttonStyle(.plain)
                .padding(.trailing, 14)
                .help("下一张 (→)")
            }
        }
    }

    // MARK: - Bottom Filmstrip Bar

    private var bottomFilmstripBar: some View {
        ScrollViewReader { proxy in
            ScrollView(.horizontal, showsIndicators: false) {
                LazyHStack(spacing: 8) {
                    ForEach(filmstripAssets) { item in
                        FilmstripThumbnailItem(
                            item: item,
                            isCurrentCursor: item.id == currentAsset?.id,
                            isTrashed: trashBasketVM.isTrashed(item.id),
                            mediaLoadService: mediaLoadService,
                            onSelect: {
                                daySliceVM.selectSingle(item.id)
                            }
                        )
                        .id(item.id)
                    }
                }
                .padding(.horizontal, 14)
                .padding(.vertical, 8)
            }
            .frame(height: 76)
            .background(Color(nsColor: .controlBackgroundColor))
            .onChange(of: currentAsset?.id) { _, newID in
                guard let newID else { return }
                withAnimation(.easeInOut(duration: 0.18)) {
                    proxy.scrollTo(newID, anchor: .center)
                }
            }
            .onAppear {
                if let activeID = currentAsset?.id {
                    proxy.scrollTo(activeID, anchor: .center)
                }
            }
        }
    }

    // MARK: - On-Demand High-Res Media Loading (isNetworkAccessAllowed = true)

    private func cancelActiveMediaRequest() {
        if let activeRequestID {
            mediaLoadService.cancelImageRequest(activeRequestID)
            self.activeRequestID = nil
        }
    }

    private func loadMedia(for asset: AssetItem?) {
        cancelActiveMediaRequest()
        highResImage = nil
        livePhoto = nil
        playerItem = nil
        daySliceVM.iCloudDownloadProgress = nil

        guard let asset, let phAsset = asset.phAsset else {
            isLoadingMedia = false
            return
        }

        isLoadingMedia = true
        let targetAssetID = asset.id

        if asset.isVideo {
            // 先加载本地封面缩略图，同时请求流式视频 AVPlayerItem
            _ = mediaLoadService.requestThumbnail(for: phAsset) { img, _ in
                guard self.currentAsset?.id == targetAssetID else { return }
                if let img, self.highResImage == nil {
                    self.highResImage = img
                }
            }
            activeRequestID = mediaLoadService.requestPlayerItem(
                for: phAsset,
                progressHandler: { prog in
                    guard self.currentAsset?.id == targetAssetID else { return }
                    self.daySliceVM.iCloudDownloadProgress = prog
                },
                resultHandler: { item, _ in
                    guard self.currentAsset?.id == targetAssetID else { return }
                    self.playerItem = item
                    self.isLoadingMedia = false
                    self.daySliceVM.iCloudDownloadProgress = nil
                }
            )
        } else if asset.isLivePhoto {
            // 先请求高清静态图保底，再请求 PHLivePhoto
            _ = mediaLoadService.requestHighResImage(
                for: phAsset,
                targetSize: CGSize(width: 2048, height: 2048),
                progressHandler: { prog in
                    guard self.currentAsset?.id == targetAssetID else { return }
                    self.daySliceVM.iCloudDownloadProgress = prog
                },
                resultHandler: { img, isDegraded, _ in
                    guard self.currentAsset?.id == targetAssetID else { return }
                    if let img {
                        self.highResImage = img
                    }
                    if !isDegraded {
                        self.isLoadingMedia = false
                    }
                }
            )
            activeRequestID = mediaLoadService.requestLivePhoto(
                for: phAsset,
                targetSize: CGSize(width: 2048, height: 2048),
                progressHandler: { prog in
                    guard self.currentAsset?.id == targetAssetID else { return }
                    self.daySliceVM.iCloudDownloadProgress = prog
                },
                resultHandler: { fetchedLivePhoto, isDegraded, _ in
                    guard self.currentAsset?.id == targetAssetID else { return }
                    if let fetchedLivePhoto {
                        self.livePhoto = fetchedLivePhoto
                    }
                    if !isDegraded {
                        self.isLoadingMedia = false
                        self.daySliceVM.iCloudDownloadProgress = nil
                    }
                }
            )
        } else {
            activeRequestID = mediaLoadService.requestHighResImage(
                for: phAsset,
                targetSize: CGSize(width: 2560, height: 2560),
                progressHandler: { prog in
                    guard self.currentAsset?.id == targetAssetID else { return }
                    self.daySliceVM.iCloudDownloadProgress = prog
                },
                resultHandler: { img, isDegraded, _ in
                    guard self.currentAsset?.id == targetAssetID else { return }
                    if let img {
                        self.highResImage = img
                    }
                    if !isDegraded {
                        self.isLoadingMedia = false
                        self.daySliceVM.iCloudDownloadProgress = nil
                    }
                }
            )
        }
    }
}

private struct FilmstripThumbnailItem: View {
    let item: AssetItem
    let isCurrentCursor: Bool
    let isTrashed: Bool
    let mediaLoadService: MediaLoadService
    let onSelect: () -> Void

    @State private var thumbnail: NSImage?
    @State private var requestID: PHImageRequestID?

    var body: some View {
        Button(action: onSelect) {
            ZStack {
                Color.secondary.opacity(0.18)

                if let thumbnail {
                    Image(nsImage: thumbnail)
                        .resizable()
                        .aspectRatio(contentMode: .fill)
                        .frame(width: 56, height: 56)
                        .clipped()
                } else {
                    Image(systemName: item.isVideo ? "video" : "photo")
                        .font(.caption)
                        .foregroundStyle(.tertiary)
                }

                if isTrashed {
                    Color.red.opacity(0.45)
                    Image(systemName: "trash.fill")
                        .font(.caption2)
                        .foregroundStyle(.white)
                }

                VStack {
                    HStack {
                        Spacer()
                        if item.isFavorite {
                            Image(systemName: "heart.fill")
                                .font(.system(size: 9))
                                .foregroundStyle(.pink)
                                .padding(3)
                        }
                    }
                    Spacer()
                }
            }
            .frame(width: 56, height: 56)
            .clipShape(RoundedRectangle(cornerRadius: 6))
            .overlay(
                RoundedRectangle(cornerRadius: 6)
                    .strokeBorder(
                        isCurrentCursor ? Color.accentColor : Color.clear,
                        lineWidth: isCurrentCursor ? 2.5 : 0
                    )
            )
            .scaleEffect(isCurrentCursor ? 1.05 : 1.0)
        }
        .buttonStyle(.plain)
        .onAppear {
            guard thumbnail == nil, let phAsset = item.phAsset else { return }
            requestID = mediaLoadService.requestThumbnail(
                for: phAsset,
                targetSize: CGSize(width: 120, height: 120)
            ) { img, _ in
                if let img {
                    self.thumbnail = img
                }
            }
        }
        .onDisappear {
            if let requestID {
                mediaLoadService.cancelImageRequest(requestID)
                self.requestID = nil
            }
        }
    }
}
