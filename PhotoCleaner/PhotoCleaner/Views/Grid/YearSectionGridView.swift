import AppKit
import SwiftUI

/// 用于收集网格中每个缩略图单元格在滚动内容坐标系下的矩形 Frame，支持拖拽橡皮筋框选（FR-3.4）
private struct AssetCellFramesPreferenceKey: PreferenceKey {
    static let defaultValue: [String: CGRect] = [:]

    static func reduce(value: inout [String: CGRect], nextValue: () -> [String: CGRect]) {
        value.merge(nextValue(), uniquingKeysWith: { _, new in new })
    }
}

/// 按年份倒序分组的缩略图网格视图（支持平铺相似组高亮、单击/⌘多选/Shift连选、双击/Space快审与橡皮筋拖拽框选，FR-3.1 ~ FR-3.4, FR-5.2）
public struct YearSectionGridView: View {
    public let appViewModel: AppViewModel

    @State private var cellFramesByAssetID: [String: CGRect] = [:]
    @State private var dragStartPoint: CGPoint?
    @State private var dragCurrentPoint: CGPoint?
    @State private var dragBaseSelection: Set<String> = []

    private let gridCoordinateSpaceName = "YearSectionGridCoordinateSpace"

    private let columns: [GridItem] = [
        GridItem(.adaptive(minimum: 148, maximum: 196), spacing: 12)
    ]

    public init(appViewModel: AppViewModel) {
        self.appViewModel = appViewModel
    }

    private var dayVM: DaySliceViewModel {
        appViewModel.daySliceVM
    }

    private var similarityVM: SimilarityCompareViewModel {
        appViewModel.similarityVM
    }

    private var basketVM: TrashBasketViewModel {
        appViewModel.trashBasketVM
    }

    private var albumVM: AlbumMembershipViewModel {
        appViewModel.albumMembershipVM
    }

    private var currentRubberBandRect: CGRect? {
        guard let start = dragStartPoint, let current = dragCurrentPoint else { return nil }
        let minX = min(start.x, current.x)
        let minY = min(start.y, current.y)
        let width = abs(current.x - start.x)
        let height = abs(current.y - start.y)
        guard width > 4 || height > 4 else { return nil }
        return CGRect(x: minX, y: minY, width: width, height: height)
    }

    public var body: some View {
        Group {
            if dayVM.allDayAssets.isEmpty {
                emptyDayPlaceholder
            } else if dayVM.yearSections.isEmpty {
                emptyFilterPlaceholder
            } else {
                scrollableYearSectionsGrid
            }
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .background(Color(nsColor: .underPageBackgroundColor).opacity(0.35))
    }

    // MARK: - Scrollable Year-Grouped Grid with Rubber-Band Selection

    private var scrollableYearSectionsGrid: some View {
        ScrollViewReader { scrollProxy in
            ScrollView {
                ZStack(alignment: .topLeading) {
                    LazyVStack(alignment: .leading, spacing: 22, pinnedViews: [.sectionHeaders]) {
                        ForEach(dayVM.yearSections) { section in
                            Section {
                                LazyVGrid(columns: columns, spacing: 12) {
                                    ForEach(section.assets) { asset in
                                        gridCell(for: asset)
                                            .id(asset.id)
                                    }
                                }
                                .padding(.horizontal, 16)
                                .padding(.bottom, 6)
                            } header: {
                                yearSectionHeader(for: section)
                            }
                        }
                    }
                    .padding(.vertical, 12)

                    // 橡皮筋拖拽框选半透明选框图层
                    if let rect = currentRubberBandRect {
                        Rectangle()
                            .fill(Color.accentColor.opacity(0.16))
                            .overlay(
                                Rectangle()
                                    .strokeBorder(Color.accentColor.opacity(0.85), lineWidth: 1.5)
                            )
                            .frame(width: rect.width, height: rect.height)
                            .offset(x: rect.minX, y: rect.minY)
                            .allowsHitTesting(false)
                    }
                }
                .coordinateSpace(name: gridCoordinateSpaceName)
                .onPreferenceChange(AssetCellFramesPreferenceKey.self) { updatedFrames in
                    cellFramesByAssetID = updatedFrames
                }
                .gesture(rubberBandDragGesture)
            }
            .onChange(of: dayVM.reviewCursorAssetID) { _, cursorID in
                if let cursorID {
                    withAnimation(.easeInOut(duration: 0.15)) {
                        scrollProxy.scrollTo(cursorID, anchor: .center)
                    }
                }
            }
        }
    }

    // MARK: - Year Section Header (FR-3.1)

    private func yearSectionHeader(for section: YearSection) -> some View {
        let yearGroups = similarityVM.groups(forYear: section.year)
        let unfavoritedIDs = section.unfavoritedAssetIDs
        let allUnfavoritedSelected = !unfavoritedIDs.isEmpty && unfavoritedIDs.isSubset(of: dayVM.selectedAssetIDs)

        return HStack(spacing: 10) {
            HStack(spacing: 6) {
                Image(systemName: "calendar")
                    .font(.subheadline.weight(.semibold))
                    .foregroundStyle(Color.accentColor)

                Text(section.headerTitle)
                    .font(.headline)
                    .foregroundStyle(.primary)
            }

            if !yearGroups.isEmpty {
                Text("含 \(yearGroups.count) 个相似组")
                    .font(.caption2.weight(.medium))
                    .foregroundStyle(.purple)
                    .padding(.horizontal, 7)
                    .padding(.vertical, 2)
                    .background(Color.purple.opacity(0.14), in: Capsule())
            }

            Spacer()

            // 【全选本年未收藏项】快捷操作（FR-3.1）
            if !unfavoritedIDs.isEmpty {
                Button {
                    if allUnfavoritedSelected {
                        dayVM.selectedAssetIDs.subtract(unfavoritedIDs)
                        albumVM.updateSelection(dayVM.selectedAssetIDs)
                    } else {
                        dayVM.selectAllUnfavorited(inYear: section.year)
                    }
                } label: {
                    Label(
                        allUnfavoritedSelected ? "取消本年未收藏选择" : "全选本年未收藏项 (\(unfavoritedIDs.count))",
                        systemImage: allUnfavoritedSelected ? "checkmark.circle.fill" : "checkmark.circle"
                    )
                    .font(.caption.weight(.medium))
                }
                .buttonStyle(.bordered)
                .controlSize(.small)
                .help("一键选中 \(section.year) 年该日所有未点亮 ❤️ 收藏的照片与视频，便于批量加入待删篮或归入相簿")
            }
        }
        .padding(.horizontal, 16)
        .padding(.vertical, 8)
        .background(.regularMaterial)
        .overlay(alignment: .bottom) {
            Divider()
        }
    }

    // MARK: - Grid Cell & Context Menu

    private func gridCell(for asset: AssetItem) -> some View {
        let group = similarityVM.group(forAssetID: asset.id)
        let isFirstInGroup = similarityVM.isFirstAssetInGroup(asset.id)
        let isSelected = dayVM.selectedAssetIDs.contains(asset.id)
        let isTrashed = basketVM.isTrashed(asset.id)
        let isNew = dayVM.isNewUnreviewed(asset.id)
        let albumCount = albumVM.albumCount(forAssetID: asset.id)

        return AssetThumbnailCell(
            asset: asset,
            isSelected: isSelected,
            isTrashed: isTrashed,
            isNewUnreviewed: isNew,
            albumCount: albumCount,
            similarityGroup: group,
            isFirstInSimilarityGroup: isFirstInGroup,
            showFileSizeBadge: dayVM.activeFilter == .largeFiles,
            mediaLoadService: appViewModel.mediaLoadService,
            onSelect: { modifiers in
                dayVM.handleGridTap(on: asset.id, modifiers: modifiers)
            },
            onDoubleClick: {
                dayVM.enterQuickReview(for: asset.id)
            },
            onToggleFavorite: {
                Task { @MainActor in
                    await dayVM.toggleFavorite(for: asset.id)
                }
            },
            onToggleTrash: {
                _ = basketVM.toggleAsset(asset)
            },
            onCompareGroup: { simGroup in
                similarityVM.openCompare(for: simGroup, allDayAssets: dayVM.allDayAssets)
            }
        )
        .background(
            GeometryReader { proxy in
                Color.clear.preference(
                    key: AssetCellFramesPreferenceKey.self,
                    value: [asset.id: proxy.frame(in: .named(gridCoordinateSpaceName))]
                )
            }
        )
        .contextMenu {
            Button {
                dayVM.enterQuickReview(for: asset.id)
            } label: {
                Label("空格大图快审 (Space)", systemImage: "eye")
            }

            Button {
                Task { @MainActor in
                    await dayVM.toggleFavorite(for: asset.id)
                }
            } label: {
                Label(
                    asset.isFavorite ? "取消收藏 ❤️ (F)" : "标记为收藏 ❤️ (F)",
                    systemImage: asset.isFavorite ? "heart.slash" : "heart.fill"
                )
            }

            if let group {
                Button {
                    similarityVM.openCompare(for: group, allDayAssets: dayVM.allDayAssets)
                } label: {
                    Label("对比此相似组 (\(group.assetIDs.count) 张)", systemImage: "square.split.2x1")
                }
            } else if dayVM.selectedAssetIDs.count >= 2 {
                Button {
                    similarityVM.openCompare(assets: dayVM.selectedAssets)
                } label: {
                    Label("对比已选中的 \(dayVM.selectedAssetIDs.count) 张照片 (C)", systemImage: "square.split.2x1")
                }
            }

            Divider()

            Button(role: isTrashed ? .none : .destructive) {
                if dayVM.selectedAssetIDs.contains(asset.id) && dayVM.selectedAssetIDs.count > 1 {
                    dayVM.toggleTrashForSelection()
                } else {
                    _ = basketVM.toggleAsset(asset)
                }
            } label: {
                Label(
                    isTrashed ? "从待删暂存篮恢复 (X)" : "移入待删暂存篮 (Delete / X)",
                    systemImage: isTrashed ? "arrow.uturn.backward.circle" : "trash"
                )
            }
        }
    }

    // MARK: - Rubber-Band Drag Selection Gesture

    private var rubberBandDragGesture: some Gesture {
        DragGesture(minimumDistance: 6, coordinateSpace: .named(gridCoordinateSpaceName))
            .onChanged { value in
                if dragStartPoint == nil {
                    dragStartPoint = value.startLocation
                    let isAdditive = NSEvent.modifierFlags.contains(.command) || NSEvent.modifierFlags.contains(.shift)
                    dragBaseSelection = isAdditive ? dayVM.selectedAssetIDs : []
                }
                dragCurrentPoint = value.location

                guard let rect = currentRubberBandRect else { return }
                var hitIDs = dragBaseSelection
                for (assetID, frame) in cellFramesByAssetID where frame.intersects(rect) {
                    hitIDs.insert(assetID)
                }
                dayVM.selectRubberBand(assetIDs: hitIDs, additive: false)
            }
            .onEnded { _ in
                dragStartPoint = nil
                dragCurrentPoint = nil
                dragBaseSelection = []
            }
    }

    // MARK: - Friendly Empty States (Task 3.2 & Task 6.2)

    private var emptyDayPlaceholder: some View {
        VStack(spacing: 16) {
            Spacer()

            Image(systemName: "calendar.badge.checkmark")
                .font(.system(size: 46))
                .foregroundStyle(.tertiary)

            Text("历年「\(dayVM.selectedMonthDay.displayTitle)」暂无照片或视频")
                .font(.title3.weight(.semibold))
                .foregroundStyle(.primary)

            Text("您可以在左侧 366 天日历中点选其他有照片的日期，或直接跳转到下一个待整理日。")
                .font(.subheadline)
                .foregroundStyle(.secondary)
                .multilineTextAlignment(.center)
                .frame(maxWidth: 420)

            HStack(spacing: 10) {
                Button {
                    _ = appViewModel.selectNextPendingDay()
                } label: {
                    Label("跳至下一待整理日 (⌘])", systemImage: "arrow.right.circle.fill")
                        .padding(.horizontal, 8)
                }
                .buttonStyle(.borderedProminent)
                .controlSize(.regular)

                if dayVM.selectedMonthDay != MonthDay.today() {
                    Button {
                        appViewModel.selectToday()
                    } label: {
                        Label("回到今天", systemImage: "sun.max")
                            .padding(.horizontal, 8)
                    }
                    .buttonStyle(.bordered)
                    .controlSize(.regular)
                }
            }

            Spacer()
        }
        .padding(32)
    }

    private var emptyFilterPlaceholder: some View {
        VStack(spacing: 14) {
            Spacer()

            Image(systemName: dayVM.activeFilter.systemImage)
                .font(.system(size: 42))
                .foregroundStyle(.tertiary)

            Text("当前过滤条件「\(dayVM.activeFilter.displayTitle)」下无匹配项目")
                .font(.headline)
                .foregroundStyle(.primary)

            Text("「\(dayVM.selectedMonthDay.displayTitle)」共有 \(dayVM.totalDayAssetCount) 项资产，但没有符合该筛选条件的项目。")
                .font(.caption)
                .foregroundStyle(.secondary)

            Button {
                dayVM.activeFilter = .all
            } label: {
                Label("查看全部 (\(dayVM.totalDayAssetCount))", systemImage: "square.grid.2x2")
            }
            .buttonStyle(.borderedProminent)
            .controlSize(.small)

            Spacer()
        }
        .padding(32)
    }
}
