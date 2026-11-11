import SwiftUI
import AppKit
import Photos

/// 全局待删暂存篮明细预览抽屉/弹窗（支持检视所有待删缩略图、单张恢复与一键清空暂存篮，FR-7.2）
public struct TrashBasketDrawerView: View {
    @Bindable public var trashBasketVM: TrashBasketViewModel
    public let mediaLoadService: MediaLoadService

    public init(
        trashBasketVM: TrashBasketViewModel,
        mediaLoadService: MediaLoadService = .shared
    ) {
        self.trashBasketVM = trashBasketVM
        self.mediaLoadService = mediaLoadService
    }

    public init(
        viewModel: TrashBasketViewModel,
        mediaLoadService: MediaLoadService = .shared
    ) {
        self.trashBasketVM = viewModel
        self.mediaLoadService = mediaLoadService
    }

    public init(appViewModel: AppViewModel) {
        self.trashBasketVM = appViewModel.trashBasketVM
        self.mediaLoadService = appViewModel.mediaLoadService
    }

    private let columns = [
        GridItem(.adaptive(minimum: 120, maximum: 150), spacing: 12)
    ]

    public var body: some View {
        VStack(spacing: 0) {
            drawerHeader
            Divider()

            if trashBasketVM.isEmpty {
                emptyPlaceholder
            } else {
                ScrollView {
                    LazyVGrid(columns: columns, spacing: 12) {
                        ForEach(trashBasketVM.trashedAssetItems) { item in
                            TrashedAssetCard(
                                item: item,
                                mediaLoadService: mediaLoadService,
                                onRestore: {
                                    trashBasketVM.restoreAsset(id: item.id)
                                }
                            )
                        }
                    }
                    .padding(16)
                }
            }

            Divider()
            drawerFooter
        }
        .frame(minWidth: 580, idealWidth: 680, minHeight: 380, idealHeight: 460)
        .background(Color(nsColor: .windowBackgroundColor))
        .onAppear {
            trashBasketVM.preloadFileSizesIfNeeded()
        }
    }

    private var drawerHeader: some View {
        HStack(spacing: 12) {
            Image(systemName: "trash.circle.fill")
                .font(.title2)
                .foregroundStyle(.red)

            VStack(alignment: .leading, spacing: 2) {
                Text("全局待删暂存篮明细")
                    .font(.headline)
                Text(trashBasketVM.statusSummaryText)
                    .font(.caption.monospacedDigit())
                    .foregroundStyle(.secondary)
            }

            Spacer()

            if trashBasketVM.canUndo {
                Button {
                    _ = trashBasketVM.undoLastAction()
                } label: {
                    Label("撤销 (⌘Z)", systemImage: "arrow.uturn.backward")
                }
                .buttonStyle(.bordered)
                .controlSize(.small)
            }

            if !trashBasketVM.isEmpty {
                Button(role: .destructive) {
                    trashBasketVM.clearBasket()
                } label: {
                    Label("清空暂存篮", systemImage: "arrow.counterclockwise")
                }
                .buttonStyle(.bordered)
                .controlSize(.small)
                .help("将全部项目移出暂存篮恢复保留（不删除照片）")
            }

            Button {
                trashBasketVM.isDrawerPresented = false
            } label: {
                Image(systemName: "xmark.circle.fill")
                    .font(.title3)
                    .foregroundStyle(.secondary)
            }
            .buttonStyle(.plain)
        }
        .padding(.horizontal, 16)
        .padding(.vertical, 12)
    }

    private var emptyPlaceholder: some View {
        VStack(spacing: 10) {
            Spacer()
            Image(systemName: "checkmark.shield")
                .font(.system(size: 40))
                .foregroundStyle(.secondary)
            Text("待删暂存篮为空")
                .font(.subheadline.weight(.medium))
                .foregroundStyle(.secondary)
            Text("在网格或大图快审中按 Delete / X 标记的待删照片会跨日期暂存在此处，最后一键批量提交。")
                .font(.caption)
                .foregroundStyle(.tertiary)
                .multilineTextAlignment(.center)
                .padding(.horizontal, 40)
            Spacer()
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }

    private var drawerFooter: some View {
        HStack {
            Image(systemName: "shield.lefthalf.filled")
                .foregroundStyle(.secondary)
            Text("提交后照片将移入系统相册「最近删除」（保留 30 天可随时找回）")
                .font(.caption)
                .foregroundStyle(.secondary)

            Spacer()

            Button("关闭") {
                trashBasketVM.isDrawerPresented = false
            }
            .buttonStyle(.bordered)

            Button(role: .destructive) {
                Task { @MainActor in
                    _ = await trashBasketVM.commitDeletions()
                }
            } label: {
                HStack(spacing: 5) {
                    if trashBasketVM.isCommitting {
                        ProgressView()
                            .controlSize(.small)
                    } else {
                        Image(systemName: "trash.fill")
                    }
                    Text("提交清理 (\(trashBasketVM.count) 项)")
                }
            }
            .buttonStyle(.borderedProminent)
            .tint(.red)
            .disabled(trashBasketVM.isEmpty || trashBasketVM.isCommitting)
        }
        .padding(.horizontal, 16)
        .padding(.vertical, 12)
    }
}

private struct TrashedAssetCard: View {
    let item: AssetItem
    let mediaLoadService: MediaLoadService
    let onRestore: () -> Void

    @State private var thumbnail: NSImage?
    @State private var requestID: PHImageRequestID?

    var body: some View {
        VStack(alignment: .leading, spacing: 6) {
            ZStack(alignment: .topTrailing) {
                GeometryReader { proxy in
                    ZStack {
                        Color.secondary.opacity(0.14)
                        if let thumbnail {
                            Image(nsImage: thumbnail)
                                .resizable()
                                .aspectRatio(contentMode: .fill)
                                .frame(width: proxy.size.width, height: proxy.size.height)
                                .clipped()
                        } else {
                            Image(systemName: item.isVideo ? "video" : "photo")
                                .font(.title2)
                                .foregroundStyle(.tertiary)
                        }
                    }
                }
                .aspectRatio(1, contentMode: .fit)
                .clipShape(RoundedRectangle(cornerRadius: 8))

                Button(action: onRestore) {
                    HStack(spacing: 3) {
                        Image(systemName: "arrow.uturn.backward")
                        Text("恢复")
                    }
                    .font(.system(size: 10, weight: .semibold))
                    .padding(.horizontal, 7)
                    .padding(.vertical, 4)
                    .foregroundStyle(.white)
                    .background(Color.black.opacity(0.72), in: Capsule())
                }
                .buttonStyle(.plain)
                .padding(6)
                .help("从待删暂存篮恢复此项")
            }

            HStack(spacing: 4) {
                Text("\(item.year)-\(item.monthDay.storageKey)")
                    .font(.caption2.monospacedDigit())
                    .foregroundStyle(.secondary)
                Spacer()
                if let bytes = item.fileSizeByteCount, bytes > 0 {
                    Text(ByteFormatter.compactString(fromByteCount: bytes))
                        .font(.caption2.monospacedDigit())
                        .foregroundStyle(.secondary)
                }
            }
        }
        .padding(6)
        .background(Color(nsColor: .controlBackgroundColor), in: RoundedRectangle(cornerRadius: 10))
        .onAppear {
            loadLocalThumbnail()
        }
        .onDisappear {
            if let requestID {
                mediaLoadService.cancelImageRequest(requestID)
                self.requestID = nil
            }
        }
    }

    private func loadLocalThumbnail() {
        guard thumbnail == nil, let phAsset = item.phAsset else { return }
        requestID = mediaLoadService.requestThumbnail(
            for: phAsset,
            targetSize: CGSize(width: 240, height: 240)
        ) { img, _ in
            if let img {
                self.thumbnail = img
            }
        }
    }
}
