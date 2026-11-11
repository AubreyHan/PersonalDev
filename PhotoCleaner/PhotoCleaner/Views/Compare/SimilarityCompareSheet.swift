import SwiftUI
import AppKit
import Photos

/// 同屏并排同步缩放对比挑片视图（FR-5.3, FR-7.1）
public struct SimilarityCompareSheet: View {
    @Bindable public var similarityVM: SimilarityCompareViewModel
    public let daySliceVM: DaySliceViewModel?
    public let mediaLoadService: MediaLoadService

    @State private var gestureScale: CGFloat = 1.0
    @State private var gesturePan: CGSize = .zero

    public init(
        similarityVM: SimilarityCompareViewModel,
        daySliceVM: DaySliceViewModel? = nil,
        mediaLoadService: MediaLoadService = .shared
    ) {
        self.similarityVM = similarityVM
        self.daySliceVM = daySliceVM
        self.mediaLoadService = mediaLoadService
    }

    public init(
        viewModel: SimilarityCompareViewModel,
        daySliceVM: DaySliceViewModel? = nil,
        mediaLoadService: MediaLoadService = .shared
    ) {
        self.similarityVM = viewModel
        self.daySliceVM = daySliceVM
        self.mediaLoadService = mediaLoadService
    }

    public init(appViewModel: AppViewModel) {
        self.similarityVM = appViewModel.similarityVM
        self.daySliceVM = appViewModel.daySliceVM
        self.mediaLoadService = appViewModel.mediaLoadService
    }

    private var effectiveScale: CGFloat {
        min(max(similarityVM.sharedZoomScale * gestureScale, 1.0), 6.0)
    }

    private var effectiveOffset: CGSize {
        guard effectiveScale > 1.01 else { return .zero }
        return CGSize(
            width: similarityVM.sharedPanOffset.width + gesturePan.width,
            height: similarityVM.sharedPanOffset.height + gesturePan.height
        )
    }

    private var gridColumns: [GridItem] {
        let count = max(2, min(similarityVM.comparingAssets.count, 4))
        let columnCount = count <= 3 ? count : 2
        return Array(repeating: GridItem(.flexible(minimum: 240), spacing: 12), count: columnCount)
    }

    private var unselectedCount: Int {
        max(0, similarityVM.comparingAssets.count - similarityVM.keptAssetIDs.count)
    }

    public var body: some View {
        VStack(spacing: 0) {
            topToolbar
            Divider()

            if similarityVM.comparingAssets.isEmpty {
                VStack(spacing: 10) {
                    Spacer()
                    Image(systemName: "square.on.square.dashed")
                        .font(.system(size: 40))
                        .foregroundStyle(.secondary)
                    Text("暂无对比项")
                        .font(.headline)
                        .foregroundStyle(.secondary)
                    Spacer()
                }
                .frame(maxWidth: .infinity, maxHeight: .infinity)
            } else {
                ScrollView {
                    LazyVGrid(columns: gridColumns, spacing: 12) {
                        ForEach(similarityVM.comparingAssets) { item in
                            CompareAssetCard(
                                item: item,
                                isKept: similarityVM.isKept(item.id),
                                effectiveScale: effectiveScale,
                                effectiveOffset: effectiveOffset,
                                mediaLoadService: mediaLoadService,
                                onToggleKept: {
                                    similarityVM.toggleKept(assetID: item.id)
                                },
                                onSelectOnlyKept: {
                                    similarityVM.selectOnlyKept(assetID: item.id)
                                },
                                onToggleFavorite: {
                                    Task { @MainActor in
                                        if let daySliceVM {
                                            await daySliceVM.toggleFavorite(for: item.id)
                                        } else {
                                            let newFav = !item.isFavorite
                                            try? await similarityVM.indexer.setFavorite(newFav, for: item.id)
                                            similarityVM.updateAssetFavoriteState(assetID: item.id, isFavorite: newFav)
                                            _ = similarityVM.trashBasketVM.handleAssetFavoriteChanged(
                                                assetID: item.id,
                                                isFavorite: newFav
                                            )
                                        }
                                    }
                                },
                                onDoubleClickZoom: {
                                    withAnimation(.spring(response: 0.24, dampingFraction: 0.85)) {
                                        if similarityVM.sharedZoomScale > 1.05 {
                                            similarityVM.resetZoomAndPan()
                                        } else {
                                            similarityVM.setZoomScale(2.5)
                                        }
                                    }
                                }
                            )
                            .gesture(syncedMagnificationGesture)
                            .simultaneousGesture(syncedPanGesture)
                        }
                    }
                    .padding(14)
                }
                .background(Color.black.opacity(0.88))
            }

            Divider()
            bottomActionBar
        }
        .frame(minWidth: 860, idealWidth: 1040, minHeight: 600, idealHeight: 720)
        .background(Color(nsColor: .windowBackgroundColor))
    }

    // MARK: - Top Toolbar

    private var topToolbar: some View {
        HStack(spacing: 12) {
            Image(systemName: "square.split.2x1.fill")
                .font(.title3)
                .foregroundStyle(Color.accentColor)

            VStack(alignment: .leading, spacing: 2) {
                HStack(spacing: 8) {
                    Text(similarityVM.activeGroup != nil ? "相似组同屏挑片" : "多图并排对比")
                        .font(.headline)
                    if let group = similarityVM.activeGroup {
                        Text(group.kind == .burst ? "原生连拍簇" : "Vision 视觉相似")
                            .font(.caption2.weight(.medium))
                            .padding(.horizontal, 6)
                            .padding(.vertical, 2)
                            .foregroundStyle(Color.accentColor)
                            .background(Color.accentColor.opacity(0.14), in: Capsule())
                    }
                }
                Text("共 \(similarityVM.comparingAssets.count) 张 · 双指/按钮缩放将同步放大所有卡片以比对对焦细节")
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }

            Spacer()

            // 同步缩放控制条
            HStack(spacing: 8) {
                Button {
                    similarityVM.zoomOut()
                } label: {
                    Image(systemName: "minus.magnifyingglass")
                }
                .buttonStyle(.bordered)
                .controlSize(.small)
                .disabled(similarityVM.sharedZoomScale <= 1.01)

                Slider(
                    value: Binding(
                        get: { Double(similarityVM.sharedZoomScale) },
                        set: { similarityVM.setZoomScale(CGFloat($0)) }
                    ),
                    in: 1.0...5.0
                )
                .frame(width: 110)

                Button {
                    similarityVM.zoomIn()
                } label: {
                    Image(systemName: "plus.magnifyingglass")
                }
                .buttonStyle(.bordered)
                .controlSize(.small)
                .disabled(similarityVM.sharedZoomScale >= 5.0)

                Text("\(Int(effectiveScale * 100))%")
                    .font(.caption.monospacedDigit())
                    .frame(width: 44, alignment: .trailing)

                Button("重置 100%") {
                    withAnimation(.spring(response: 0.22, dampingFraction: 0.85)) {
                        similarityVM.resetZoomAndPan()
                    }
                }
                .buttonStyle(.bordered)
                .controlSize(.small)
                .disabled(similarityVM.sharedZoomScale <= 1.01 && similarityVM.sharedPanOffset == .zero)
            }

            Button {
                similarityVM.closeCompare()
            } label: {
                Image(systemName: "xmark.circle.fill")
                    .font(.title3)
                    .foregroundStyle(.secondary)
            }
            .buttonStyle(.plain)
            .help("退出对比视图 (Esc)")
        }
        .padding(.horizontal, 16)
        .padding(.vertical, 12)
    }

    // MARK: - Bottom Bar CTA

    private var bottomActionBar: some View {
        HStack(spacing: 14) {
            HStack(spacing: 6) {
                Image(systemName: "checkmark.circle.fill")
                    .foregroundStyle(.green)
                Text("已勾选保留 \(similarityVM.keptAssetIDs.count) 张 · 待淘汰 \(unselectedCount) 张")
                    .font(.subheadline.monospacedDigit().weight(.medium))
            }

            Text("（已带 ❤️ 收藏的照片在淘汰时将自动受保护跳过）")
                .font(.caption)
                .foregroundStyle(.secondary)

            Spacer()

            Button("取消 (Esc)") {
                similarityVM.closeCompare()
            }
            .buttonStyle(.bordered)

            Button {
                _ = similarityVM.keepSelectedAndTrashOthers()
            } label: {
                HStack(spacing: 6) {
                    Image(systemName: "checkmark.shield.fill")
                    Text("保留已选，其余移入待删篮 (\(unselectedCount))")
                        .fontWeight(.semibold)
                }
                .padding(.horizontal, 6)
            }
            .buttonStyle(.borderedProminent)
            .tint(.accentColor)
            .disabled(similarityVM.keptAssetIDs.isEmpty || unselectedCount == 0)
        }
        .padding(.horizontal, 16)
        .padding(.vertical, 12)
    }

    // MARK: - Synced Gestures

    private var syncedMagnificationGesture: some Gesture {
        MagnificationGesture()
            .onChanged { value in
                gestureScale = value
            }
            .onEnded { value in
                similarityVM.setZoomScale(similarityVM.sharedZoomScale * value)
                gestureScale = 1.0
            }
    }

    private var syncedPanGesture: some Gesture {
        DragGesture(minimumDistance: 4)
            .onChanged { value in
                guard effectiveScale > 1.01 else { return }
                gesturePan = value.translation
            }
            .onEnded { value in
                guard effectiveScale > 1.01 else {
                    similarityVM.sharedPanOffset = .zero
                    gesturePan = .zero
                    return
                }
                similarityVM.sharedPanOffset = CGSize(
                    width: similarityVM.sharedPanOffset.width + value.translation.width,
                    height: similarityVM.sharedPanOffset.height + value.translation.height
                )
                gesturePan = .zero
            }
    }
}

private struct CompareAssetCard: View {
    let item: AssetItem
    let isKept: Bool
    let effectiveScale: CGFloat
    let effectiveOffset: CGSize
    let mediaLoadService: MediaLoadService
    let onToggleKept: () -> Void
    let onSelectOnlyKept: () -> Void
    let onToggleFavorite: () -> Void
    let onDoubleClickZoom: () -> Void

    @State private var highResImage: NSImage?
    @State private var downloadProgress: Double?
    @State private var requestID: PHImageRequestID?

    var body: some View {
        VStack(spacing: 0) {
            ZStack {
                Color.black

                if let highResImage {
                    Image(nsImage: highResImage)
                        .resizable()
                        .aspectRatio(contentMode: .fit)
                        .scaleEffect(effectiveScale)
                        .offset(effectiveOffset)
                } else {
                    VStack(spacing: 8) {
                        if let progress = downloadProgress, progress > 0 && progress < 1.0 {
                            ProgressView(value: progress)
                                .progressViewStyle(.circular)
                                .controlSize(.small)
                            Text("iCloud \(Int(progress * 100))%")
                                .font(.caption2)
                                .foregroundStyle(.secondary)
                        } else {
                            ProgressView()
                                .controlSize(.small)
                        }
                    }
                }

                // 顶部左右浮层：保留勾选框 + 收藏红心
                VStack {
                    HStack {
                        Button(action: onToggleKept) {
                            HStack(spacing: 5) {
                                Image(systemName: isKept ? "checkmark.circle.fill" : "circle")
                                    .font(.system(size: 15, weight: .semibold))
                                    .foregroundStyle(isKept ? .green : .white.opacity(0.85))
                                Text(isKept ? "保留 (Keep)" : "未勾选 (将移入待删)")
                                    .font(.caption.weight(.semibold))
                                    .foregroundStyle(.white)
                            }
                            .padding(.horizontal, 10)
                            .padding(.vertical, 5)
                            .background(
                                (isKept ? Color.green.opacity(0.82) : Color.black.opacity(0.65)),
                                in: Capsule()
                            )
                        }
                        .buttonStyle(.plain)

                        Spacer()

                        Button(action: onToggleFavorite) {
                            Image(systemName: item.isFavorite ? "heart.fill" : "heart")
                                .font(.system(size: 14, weight: .semibold))
                                .foregroundStyle(item.isFavorite ? .pink : .white)
                                .padding(7)
                                .background(Color.black.opacity(0.6), in: Circle())
                        }
                        .buttonStyle(.plain)
                        .help("切换收藏 ❤️（已收藏照片免疫批量删除）")
                    }
                    .padding(10)

                    Spacer()
                }
            }
            .frame(minHeight: 300, idealHeight: 380)
            .clipped()
            .contentShape(Rectangle())
            .onTapGesture(count: 2, perform: onDoubleClickZoom)
            .onTapGesture(count: 1, perform: onToggleKept)

            // 卡片底部参数栏：像素分辨率 + 文件体积 + 仅保留此张快捷按钮
            HStack(spacing: 8) {
                if item.pixelWidth > 0 && item.pixelHeight > 0 {
                    Text("\(item.pixelWidth)×\(item.pixelHeight)")
                        .font(.caption2.monospacedDigit())
                        .foregroundStyle(.secondary)
                }

                if let bytes = item.fileSizeByteCount, bytes > 0 {
                    Text("· \(ByteFormatter.string(fromByteCount: bytes))")
                        .font(.caption2.monospacedDigit())
                        .foregroundStyle(.secondary)
                }

                if !item.isLocallyAvailable {
                    Label("iCloud", systemImage: "icloud")
                        .font(.caption2)
                        .foregroundStyle(.blue)
                }

                Spacer()

                Button("仅留此张") {
                    onSelectOnlyKept()
                }
                .buttonStyle(.link)
                .font(.caption2.weight(.semibold))
            }
            .padding(.horizontal, 10)
            .padding(.vertical, 7)
            .background(Color(nsColor: .controlBackgroundColor))
        }
        .clipShape(RoundedRectangle(cornerRadius: 10))
        .overlay(
            RoundedRectangle(cornerRadius: 10)
                .strokeBorder(isKept ? Color.green : Color.secondary.opacity(0.3), lineWidth: isKept ? 2.5 : 1)
        )
        .onAppear {
            loadCompareImage()
        }
        .onDisappear {
            if let requestID {
                mediaLoadService.cancelImageRequest(requestID)
                self.requestID = nil
            }
        }
    }

    private func loadCompareImage() {
        guard highResImage == nil, let phAsset = item.phAsset else { return }
        requestID = mediaLoadService.requestHighResImage(
            for: phAsset,
            targetSize: CGSize(width: 1920, height: 1920),
            progressHandler: { prog in
                self.downloadProgress = prog
            },
            resultHandler: { img, isDegraded, _ in
                if let img {
                    self.highResImage = img
                }
                if !isDegraded {
                    self.downloadProgress = nil
                }
            }
        )
    }
}
