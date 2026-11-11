import AppKit
import Photos
import SwiftUI

/// 网格缩略图单元格（严格仅使用本地缩略图 `isNetworkAccessAllowed = false`，展示完整角标与待删半透明红遮罩，FR-1.5 & FR-3.2）
public struct AssetThumbnailCell: View {
    public let asset: AssetItem
    public let isSelected: Bool
    public let isTrashed: Bool
    public let isNewUnreviewed: Bool
    public let albumCount: Int
    public let similarityGroup: SimilarityGroup?
    public let isFirstInSimilarityGroup: Bool
    public let showFileSizeBadge: Bool
    public let mediaLoadService: MediaLoadService

    public var onSelect: ((NSEvent.ModifierFlags) -> Void)?
    public var onDoubleClick: (() -> Void)?
    public var onToggleFavorite: (() -> Void)?
    public var onToggleTrash: (() -> Void)?
    public var onCompareGroup: ((SimilarityGroup) -> Void)?

    @State private var thumbnailImage: NSImage?
    @State private var isInCloudOnly: Bool = false
    @State private var activeRequestID: PHImageRequestID?
    @State private var isHovering: Bool = false

    public init(
        asset: AssetItem,
        isSelected: Bool,
        isTrashed: Bool,
        isNewUnreviewed: Bool = false,
        albumCount: Int = 0,
        similarityGroup: SimilarityGroup? = nil,
        isFirstInSimilarityGroup: Bool = false,
        showFileSizeBadge: Bool = false,
        mediaLoadService: MediaLoadService = .shared,
        onSelect: ((NSEvent.ModifierFlags) -> Void)? = nil,
        onDoubleClick: (() -> Void)? = nil,
        onToggleFavorite: (() -> Void)? = nil,
        onToggleTrash: (() -> Void)? = nil,
        onCompareGroup: ((SimilarityGroup) -> Void)? = nil
    ) {
        self.asset = asset
        self.isSelected = isSelected
        self.isTrashed = isTrashed
        self.isNewUnreviewed = isNewUnreviewed
        self.albumCount = albumCount
        self.similarityGroup = similarityGroup
        self.isFirstInSimilarityGroup = isFirstInSimilarityGroup
        self.showFileSizeBadge = showFileSizeBadge
        self.mediaLoadService = mediaLoadService
        self.onSelect = onSelect
        self.onDoubleClick = onDoubleClick
        self.onToggleFavorite = onToggleFavorite
        self.onToggleTrash = onToggleTrash
        self.onCompareGroup = onCompareGroup
    }

    private var effectiveIsInCloud: Bool {
        !asset.isLocallyAvailable || isInCloudOnly
    }

    public var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            // 相似组首张顶部平铺操作胶囊：【相似组 · N 张 | 对比此组】（FR-5.2：平铺不折叠）
            if let group = similarityGroup {
                if isFirstInSimilarityGroup {
                    similarityGroupHeaderPill(for: group)
                } else {
                    similarityGroupMemberSubtlePill(for: group)
                }
            }

            // 缩略图主体正方形卡片
            GeometryReader { geo in
                ZStack {
                    thumbnailContentView(size: geo.size)

                    // 顶部角标栏（左：实况 / 截屏 / 新增 / iCloud；右：收藏 ❤️）
                    VStack {
                        topBadgesRow
                        Spacer()
                        bottomBadgesRow
                    }
                    .padding(6)

                    // 已在待删暂存篮：半透明红色遮罩与删除图标（FR-3.2）
                    if isTrashed {
                        trashedOverlayView
                    }
                }
                .frame(width: geo.size.width, height: geo.size.height)
                .clipShape(RoundedRectangle(cornerRadius: 9))
                .overlay(
                    RoundedRectangle(cornerRadius: 9)
                        .strokeBorder(borderColor, lineWidth: borderWidth)
                )
                .shadow(
                    color: isSelected ? Color.accentColor.opacity(0.35) : Color.black.opacity(0.08),
                    radius: isSelected ? 4 : 2,
                    y: 1
                )
            }
            .aspectRatio(1.0, contentMode: .fit)
            .contentShape(Rectangle())
            .gesture(
                TapGesture(count: 2).onEnded {
                    onDoubleClick?()
                }
            )
            .simultaneousGesture(
                TapGesture(count: 1).onEnded {
                    let modifiers = NSEvent.modifierFlags
                    onSelect?(modifiers)
                }
            )
            .onHover { hovering in
                isHovering = hovering
            }
        }
        .padding(similarityGroup != nil ? 4 : 0)
        .background(
            Group {
                if similarityGroup != nil {
                    RoundedRectangle(cornerRadius: 12)
                        .fill(Color.purple.opacity(0.08))
                        .overlay(
                            RoundedRectangle(cornerRadius: 12)
                                .strokeBorder(Color.purple.opacity(0.48), lineWidth: 1.5)
                        )
                }
            }
        )
        .onAppear {
            loadLocalThumbnail()
        }
        .onChange(of: asset.id) { _, _ in
            loadLocalThumbnail()
        }
        .onDisappear {
            cancelPendingRequest()
        }
    }

    // MARK: - Thumbnail Image Content

    @ViewBuilder
    private func thumbnailContentView(size: CGSize) -> some View {
        if let image = thumbnailImage {
            Image(nsImage: image)
                .resizable()
                .scaledToFill()
                .frame(width: size.width, height: size.height)
                .clipped()
        } else {
            ZStack {
                Color(nsColor: .controlBackgroundColor)

                VStack(spacing: 5) {
                    Image(systemName: asset.isVideo ? "video.fill" : "photo")
                        .font(.system(size: 22))
                        .foregroundStyle(.tertiary)

                    if effectiveIsInCloud {
                        Text("仅在 iCloud")
                            .font(.system(size: 9))
                            .foregroundStyle(.secondary)
                    }
                }
            }
        }
    }

    // MARK: - Top & Bottom Badges (FR-3.2)

    private var topBadgesRow: some View {
        HStack(alignment: .top, spacing: 4) {
            // 左上角：新增角标 + 实况 + 截屏 + iCloud 角标
            HStack(spacing: 4) {
                if isNewUnreviewed {
                    Text("新增")
                        .font(.system(size: 9, weight: .bold))
                        .foregroundStyle(.white)
                        .padding(.horizontal, 5)
                        .padding(.vertical, 2)
                        .background(Color.orange, in: Capsule())
                        .help("上次标记整理后新增的照片/视频")
                }

                if asset.isLivePhoto {
                    badgeIcon("livephoto", help: "实况照片 (Live Photo)")
                }

                if asset.isScreenshot {
                    badgeIcon("camera.viewfinder", help: "屏幕截图 / 录屏")
                }

                if effectiveIsInCloud {
                    badgeIcon("icloud.and.arrow.down", tint: .cyan, help: "原片存储在 iCloud（浏览不触发自动下载）")
                }
            }

            Spacer(minLength: 2)

            // 右上角：收藏红心 ❤️（已收藏常驻显示，悬停时显示可点击空心心形）
            if asset.isFavorite || isHovering {
                Button {
                    onToggleFavorite?()
                } label: {
                    Image(systemName: asset.isFavorite ? "heart.fill" : "heart")
                        .font(.system(size: 12, weight: .semibold))
                        .foregroundStyle(asset.isFavorite ? Color.pink : Color.white)
                        .padding(5)
                        .background(Color.black.opacity(0.45), in: Circle())
                }
                .buttonStyle(.plain)
                .help(asset.isFavorite ? "已收藏 ❤️（受防误删保护，按 F 取消）" : "点击或按 F 标记收藏 ❤️")
            }
        }
    }

    private var bottomBadgesRow: some View {
        HStack(alignment: .bottom, spacing: 4) {
            // 左下角：已归属相簿计数小标签 `📁 N`
            if albumCount > 0 {
                HStack(spacing: 3) {
                    Text("📁")
                        .font(.system(size: 9))
                    Text("\(albumCount)")
                        .font(.system(size: 10, weight: .semibold, design: .monospaced))
                        .foregroundStyle(.white)
                }
                .padding(.horizontal, 5)
                .padding(.vertical, 2)
                .background(Color.black.opacity(0.58), in: Capsule())
                .help("已归属于 \(albumCount) 个用户普通相簿")
            }

            if (asset.isLargeFile || showFileSizeBadge), let bytes = asset.fileSizeByteCount, bytes > 0 {
                Text(ByteFormatter.compactString(fromByteCount: bytes))
                    .font(.system(size: 9, weight: .semibold, design: .monospaced))
                    .foregroundStyle(.white)
                    .padding(.horizontal, 5)
                    .padding(.vertical, 2)
                    .background(
                        (asset.isLargeFile ? Color.indigo : Color.black).opacity(0.65),
                        in: Capsule()
                    )
            }

            Spacer(minLength: 2)

            // 右下角：视频时长（如 `01:24`）
            if let durationText = asset.formattedDuration {
                HStack(spacing: 3) {
                    Image(systemName: "play.fill")
                        .font(.system(size: 7))
                    Text(durationText)
                        .font(.system(size: 10, weight: .semibold, design: .monospaced))
                }
                .foregroundStyle(.white)
                .padding(.horizontal, 6)
                .padding(.vertical, 2)
                .background(Color.black.opacity(0.62), in: Capsule())
            }
        }
    }

    private func badgeIcon(_ systemName: String, tint: Color = .white, help: String) -> some View {
        Image(systemName: systemName)
            .font(.system(size: 10, weight: .semibold))
            .foregroundStyle(tint)
            .padding(4)
            .background(Color.black.opacity(0.52), in: Circle())
            .help(help)
    }

    // MARK: - Trashed Overlay & Similarity Header Pill

    private var trashedOverlayView: some View {
        ZStack {
            Color.red.opacity(0.38)

            VStack(spacing: 4) {
                Image(systemName: "trash.fill")
                    .font(.system(size: 20, weight: .bold))
                    .foregroundStyle(.white)
                Text("已在待删篮")
                    .font(.system(size: 10, weight: .bold))
                    .foregroundStyle(.white)
            }
            .padding(.horizontal, 8)
            .padding(.vertical, 5)
            .background(Color.red.opacity(0.78), in: Capsule())
        }
        .allowsHitTesting(false)
    }

    private func similarityGroupHeaderPill(for group: SimilarityGroup) -> some View {
        Button {
            onCompareGroup?(group)
        } label: {
            HStack(spacing: 4) {
                Image(systemName: group.kind == .burst ? "square.stack.3d.forward.dottedline.fill" : "sparkles.rectangle.stack.fill")
                    .font(.system(size: 10, weight: .semibold))
                Text("相似组 · \(group.assetIDs.count) 张 | 对比此组")
                    .font(.system(size: 10, weight: .semibold))
                    .lineLimit(1)
            }
            .foregroundStyle(.white)
            .padding(.horizontal, 7)
            .padding(.vertical, 3)
            .frame(maxWidth: .infinity)
            .background(
                LinearGradient(
                    colors: [Color.purple, Color.indigo],
                    startPoint: .leading,
                    endPoint: .trailing
                ),
                in: Capsule()
            )
        }
        .buttonStyle(.plain)
        .help("点击进入同屏并排同步缩放对比，一键保留最佳照片并淘汰同组其余废片")
    }

    private func similarityGroupMemberSubtlePill(for group: SimilarityGroup) -> some View {
        Button {
            onCompareGroup?(group)
        } label: {
            HStack(spacing: 4) {
                Image(systemName: "link")
                    .font(.system(size: 9, weight: .semibold))
                Text("同组相似 (\(group.assetIDs.count) 张)")
                    .font(.system(size: 10, weight: .medium))
                    .lineLimit(1)
            }
            .foregroundStyle(Color.purple)
            .padding(.horizontal, 7)
            .padding(.vertical, 3)
            .frame(maxWidth: .infinity)
            .background(Color.purple.opacity(0.14), in: Capsule())
        }
        .buttonStyle(.plain)
    }

    // MARK: - Border & Thumbnail Loading

    private var borderColor: Color {
        if isSelected {
            return Color.accentColor
        }
        if isTrashed {
            return Color.red.opacity(0.85)
        }
        if isNewUnreviewed {
            return Color.orange.opacity(0.75)
        }
        return Color.secondary.opacity(0.16)
    }

    private var borderWidth: CGFloat {
        if isSelected { return 3.0 }
        if isTrashed { return 2.0 }
        if isNewUnreviewed { return 1.5 }
        return 1.0
    }

    private func loadLocalThumbnail() {
        cancelPendingRequest()
        guard let phAsset = asset.phAsset else {
            thumbnailImage = nil
            isInCloudOnly = !asset.isLocallyAvailable
            return
        }

        // 强制使用 MediaLoadService 本地安全缩略图接口（`isNetworkAccessAllowed = false`）
        activeRequestID = mediaLoadService.requestThumbnail(
            for: phAsset,
            targetSize: MediaLoadService.defaultGridThumbnailSize,
            contentMode: .aspectFill
        ) { image, inCloud in
            if let image {
                self.thumbnailImage = image
            }
            self.isInCloudOnly = inCloud && image == nil
        }
    }

    private func cancelPendingRequest() {
        if let reqID = activeRequestID {
            mediaLoadService.cancelImageRequest(reqID)
            activeRequestID = nil
        }
    }
}
