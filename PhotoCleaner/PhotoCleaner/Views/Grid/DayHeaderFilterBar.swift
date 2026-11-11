import SwiftUI

/// 主网格视图顶部：当前月-日标题、完成整理/重置按钮、同屏对比按钮与 8 种 `MediaFilter` 多维过滤胶囊栏（FR-2.4, FR-3.3）
public struct DayHeaderFilterBar: View {
    public let appViewModel: AppViewModel

    public init(appViewModel: AppViewModel) {
        self.appViewModel = appViewModel
    }

    private var dayVM: DaySliceViewModel {
        appViewModel.daySliceVM
    }

    private var summary: DaySliceSummary {
        dayVM.currentDaySummary
    }

    public var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            // Top Row: Selected MonthDay Title + Status Badge + Primary Actions
            HStack(alignment: .center, spacing: 12) {
                HStack(alignment: .firstTextBaseline, spacing: 8) {
                    Text("\(dayVM.selectedMonthDay.displayTitle) · 历年今日")
                        .font(.title2.weight(.bold))
                        .foregroundStyle(.primary)

                    Text("(共 \(dayVM.totalDayAssetCount) 项)")
                        .font(.subheadline.monospacedDigit())
                        .foregroundStyle(.secondary)

                    statusBadge(for: summary)
                }

                if appViewModel.similarityVM.isAnalyzing {
                    HStack(spacing: 5) {
                        ProgressView()
                            .controlSize(.mini)
                        Text("正在识别相似照片...")
                            .font(.caption2)
                            .foregroundStyle(.secondary)
                    }
                    .padding(.horizontal, 8)
                    .padding(.vertical, 3)
                    .background(Color.secondary.opacity(0.1), in: Capsule())
                }

                Spacer()

                // Action Buttons
                HStack(spacing: 8) {
                    // 【对比选中 (C)】
                    Button {
                        _ = appViewModel.handleShortcut(.openCompare)
                    } label: {
                        Label("对比选中 (C)", systemImage: "square.split.2x1")
                            .font(.caption.weight(.medium))
                    }
                    .buttonStyle(.bordered)
                    .controlSize(.small)
                    .disabled(dayVM.selectedAssetIDs.count < 2 && dayVM.allDayAssets.count < 2)
                    .help("并排同步缩放对比选中的多张照片或当前相似组 (快捷键 C)")

                    // 【完成当日整理】 / 【重置未整理】
                    dayCompletionActionButton

                    // 右侧检视栏开关 (I)
                    Button {
                        dayVM.toggleInspector()
                    } label: {
                        Image(systemName: "sidebar.right")
                            .foregroundStyle(dayVM.isInspectorVisible ? Color.accentColor : Color.secondary)
                    }
                    .buttonStyle(.bordered)
                    .controlSize(.small)
                    .help("显示/隐藏右侧相簿归属与 EXIF 检视栏 (快捷键 I)")
                }
            }

            // Bottom Row: 8 MediaFilter Capsules
            ScrollView(.horizontal, showsIndicators: false) {
                HStack(spacing: 6) {
                    ForEach(dayVM.availableFilters) { filter in
                        filterCapsuleButton(for: filter)
                    }

                    if !dayVM.selectedAssetIDs.isEmpty {
                        Divider()
                            .frame(height: 16)
                            .padding(.horizontal, 2)

                        Text("已选 \(dayVM.selectedAssetIDs.count) 项")
                            .font(.caption.monospacedDigit().weight(.medium))
                            .foregroundStyle(Color.accentColor)

                        Button("取消选择") {
                            dayVM.clearSelection()
                        }
                        .buttonStyle(.plain)
                        .font(.caption)
                        .foregroundStyle(.secondary)
                    }
                }
                .padding(.vertical, 2)
            }
        }
        .padding(.horizontal, 16)
        .padding(.vertical, 12)
        .background(Color(nsColor: .windowBackgroundColor))
        .overlay(alignment: .bottom) {
            Divider()
        }
    }

    // MARK: - Day Status Badge & Completion Button

    @ViewBuilder
    private func statusBadge(for summary: DaySliceSummary) -> some View {
        switch summary.status {
        case .empty:
            Label("空置", systemImage: "tray")
                .font(.caption.weight(.medium))
                .padding(.horizontal, 8)
                .padding(.vertical, 3)
                .foregroundStyle(.secondary)
                .background(Color.secondary.opacity(0.14), in: Capsule())

        case .pending:
            Label("待整理", systemImage: "clock")
                .font(.caption.weight(.medium))
                .padding(.horizontal, 8)
                .padding(.vertical, 3)
                .foregroundStyle(.primary)
                .background(Color.primary.opacity(0.10), in: Capsule())

        case .completed:
            Label("已整理", systemImage: "checkmark.seal.fill")
                .font(.caption.weight(.semibold))
                .padding(.horizontal, 8)
                .padding(.vertical, 3)
                .foregroundStyle(.green)
                .background(Color.green.opacity(0.16), in: Capsule())

        case .hasNew:
            Label("有新增 \(summary.newUnreviewedCount) 项待审", systemImage: "sparkles")
                .font(.caption.weight(.semibold))
                .padding(.horizontal, 8)
                .padding(.vertical, 3)
                .foregroundStyle(.orange)
                .background(Color.orange.opacity(0.18), in: Capsule())
        }
    }

    @ViewBuilder
    private var dayCompletionActionButton: some View {
        switch summary.status {
        case .empty:
            EmptyView()

        case .pending, .hasNew:
            Button {
                dayVM.markDayCompleted()
            } label: {
                Label("完成当日整理", systemImage: "checkmark.circle.fill")
                    .font(.caption.weight(.semibold))
            }
            .buttonStyle(.borderedProminent)
            .tint(.green)
            .controlSize(.small)
            .help("将当前日期下所有未待删照片记入已审快照，标记本日整理完成")

        case .completed:
            HStack(spacing: 6) {
                Button {
                    _ = appViewModel.selectNextPendingDay()
                } label: {
                    Label("下一待整理日 (⌘])", systemImage: "arrow.right.circle.fill")
                        .font(.caption.weight(.medium))
                }
                .buttonStyle(.borderedProminent)
                .controlSize(.small)

                Button {
                    dayVM.resetDayProgress()
                } label: {
                    Label("重置未整理", systemImage: "arrow.counterclockwise")
                        .font(.caption)
                }
                .buttonStyle(.bordered)
                .controlSize(.small)
                .help("清空当前日期的已审阅记录，恢复为“待整理”状态")
            }
        }
    }

    // MARK: - MediaFilter Capsule Button

    private func filterCapsuleButton(for filter: MediaFilter) -> some View {
        let isActive = dayVM.activeFilter == filter
        let count = dayVM.filterCount(for: filter)

        return Button {
            dayVM.activeFilter = filter
        } label: {
            HStack(spacing: 5) {
                Image(systemName: filter.systemImage)
                    .font(.system(size: 11, weight: isActive ? .semibold : .regular))

                Text(filter.displayTitle)
                    .font(.caption.weight(isActive ? .semibold : .medium))

                Text("\(count)")
                    .font(.caption2.monospacedDigit())
                    .padding(.horizontal, 5)
                    .padding(.vertical, 1)
                    .background(
                        isActive
                            ? Color.white.opacity(0.24)
                            : Color.secondary.opacity(0.14),
                        in: Capsule()
                    )
            }
            .padding(.horizontal, 10)
            .padding(.vertical, 5)
            .foregroundStyle(isActive ? Color.white : (filter == .newOnly ? Color.orange : Color.primary))
            .background(
                Capsule()
                    .fill(
                        isActive
                            ? (filter == .newOnly ? Color.orange : Color.accentColor)
                            : Color(nsColor: .controlBackgroundColor)
                    )
            )
            .overlay(
                Capsule()
                    .strokeBorder(
                        isActive
                            ? Color.clear
                            : (filter == .newOnly ? Color.orange.opacity(0.45) : Color.secondary.opacity(0.22)),
                        lineWidth: 1
                    )
            )
        }
        .buttonStyle(.plain)
    }
}
