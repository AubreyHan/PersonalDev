import SwiftUI

/// 右侧检视栏容器（快捷键 `I` 显隐，顶部展示相簿归属双向面板，底部展示 EXIF 详情）
public struct RightInspectorColumn: View {
    public let daySliceVM: DaySliceViewModel
    public let albumMembershipVM: AlbumMembershipViewModel

    public init(
        daySliceVM: DaySliceViewModel,
        albumMembershipVM: AlbumMembershipViewModel
    ) {
        self.daySliceVM = daySliceVM
        self.albumMembershipVM = albumMembershipVM
    }

    public init(daySliceVM: DaySliceViewModel) {
        self.daySliceVM = daySliceVM
        self.albumMembershipVM = daySliceVM.albumMembershipVM
    }

    public init(appViewModel: AppViewModel) {
        self.daySliceVM = appViewModel.daySliceVM
        self.albumMembershipVM = appViewModel.albumMembershipVM
    }

    private var effectiveSelectedCount: Int {
        if !daySliceVM.selectedAssetIDs.isEmpty {
            return daySliceVM.selectedAssetIDs.count
        }
        if daySliceVM.workspaceMode == .quickReview, daySliceVM.activeReviewAsset != nil {
            return 1
        }
        return 0
    }

    private var totalSelectedBytes: Int64? {
        let items = daySliceVM.selectedAssets
        guard items.count > 1 else { return nil }
        let sum = items.reduce(Int64(0)) { partial, item in
            let bytes = item.fileSizeByteCount ?? daySliceVM.mediaLoadService.cachedFileSize(for: item.id) ?? 0
            return partial + max(0, bytes)
        }
        return sum > 0 ? sum : nil
    }

    public var body: some View {
        VStack(spacing: 0) {
            inspectorHeader
            Divider()

            if effectiveSelectedCount == 0 {
                emptySelectionPlaceholder
            } else {
                VStack(spacing: 12) {
                    AlbumMembershipPanel(viewModel: albumMembershipVM)
                        .frame(maxHeight: .infinity)

                    ExifMetadataSection(
                        detail: effectiveSelectedCount == 1 ? daySliceVM.currentExifDetail : nil,
                        fallbackAsset: daySliceVM.primarySelectedAsset,
                        selectedCount: effectiveSelectedCount,
                        totalSelectedBytes: totalSelectedBytes,
                        isLoading: effectiveSelectedCount == 1 && daySliceVM.currentExifDetail == nil
                    )
                }
                .padding(12)
            }
        }
        .frame(minWidth: 270, idealWidth: 300, maxWidth: 340)
        .background(Color(nsColor: .windowBackgroundColor))
    }

    private var inspectorHeader: some View {
        HStack(spacing: 8) {
            Image(systemName: "sidebar.right")
                .foregroundStyle(.secondary)
            Text("检视器")
                .font(.headline)
            Spacer()
            if effectiveSelectedCount > 0 {
                Button {
                    Task { @MainActor in
                        await daySliceVM.toggleFavoriteForSelection()
                    }
                } label: {
                    let isFav = daySliceVM.primarySelectedAsset?.isFavorite ?? false
                    Image(systemName: isFav ? "heart.fill" : "heart")
                        .foregroundStyle(isFav ? .pink : .secondary)
                }
                .buttonStyle(.plain)
                .help("切换收藏 ❤️ (F)")
            }

            Button {
                daySliceVM.toggleInspector()
            } label: {
                Image(systemName: "xmark")
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }
            .buttonStyle(.plain)
            .help("隐藏右侧检视栏 (I)")
        }
        .padding(.horizontal, 14)
        .padding(.vertical, 10)
    }

    private var emptySelectionPlaceholder: some View {
        VStack(spacing: 12) {
            Spacer()
            Image(systemName: "photo.on.rectangle.angled")
                .font(.system(size: 36))
                .foregroundStyle(.tertiary)
            Text("未选中任何照片或视频")
                .font(.subheadline.weight(.medium))
                .foregroundStyle(.secondary)
            Text("在网格中单击或框选多张照片，即可查看并批量调整其【所属相簿】与【未加入相簿】，或检视 EXIF 拍摄详情。")
                .font(.caption)
                .foregroundStyle(.tertiary)
                .multilineTextAlignment(.center)
                .padding(.horizontal, 24)
            Spacer()
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }
}
