import SwiftUI

/// 左侧常驻 366 天（12 个月，永久含 2月29日）四态进度日历导航面板（FR-2.1 ~ FR-2.5, EC-1）
public struct Calendar366Sidebar: View {
    public let appViewModel: AppViewModel

    private let dayColumns: [GridItem] = Array(
        repeating: GridItem(.flexible(minimum: 26, maximum: 38), spacing: 4),
        count: 7
    )

    public init(appViewModel: AppViewModel) {
        self.appViewModel = appViewModel
    }

    public var body: some View {
        VStack(spacing: 10) {
            // 1. 顶部全局清理统计看板
            CleanupStatsCard(appViewModel: appViewModel)
                .padding(.horizontal, 12)
                .padding(.top, 10)

            // 2. 快捷导航按钮栏：【今天】 & 【下一待整理日 (⌘])】
            quickNavigationHeader
                .padding(.horizontal, 12)

            // 3. 四态颜色图例
            statusLegendRow
                .padding(.horizontal, 12)

            Divider()

            // 4. 12 个月 / 366 天（含 02-29）可滚动网格
            ScrollViewReader { scrollProxy in
                ScrollView {
                    LazyVStack(alignment: .leading, spacing: 14) {
                        ForEach(1...12, id: \.self) { month in
                            monthSectionView(for: month)
                                .id("month-\(month)")
                        }
                    }
                    .padding(.horizontal, 12)
                    .padding(.vertical, 8)
                }
                .onAppear {
                    scrollToMonth(appViewModel.selectedMonthDay.month, proxy: scrollProxy, animated: false)
                }
                .onChange(of: appViewModel.selectedMonthDay) { _, newDay in
                    scrollToMonth(newDay.month, proxy: scrollProxy, animated: true)
                }
            }
        }
        .frame(minWidth: 256, idealWidth: 276, maxWidth: 320)
        .background(Color(nsColor: .windowBackgroundColor))
    }

    // MARK: - Quick Navigation Header

    private var quickNavigationHeader: some View {
        VStack(spacing: 6) {
            HStack(spacing: 6) {
                Button {
                    appViewModel.selectToday()
                } label: {
                    HStack(spacing: 4) {
                        Image(systemName: "sun.max.fill")
                            .font(.caption)
                        Text("今天 (\(MonthDay.today().displayTitle))")
                            .font(.caption.weight(.medium))
                            .lineLimit(1)
                    }
                    .frame(maxWidth: .infinity)
                }
                .buttonStyle(.bordered)
                .controlSize(.small)
                .help("跳转到当年今日对应的跨年份月-日切片")

                Button {
                    _ = appViewModel.selectNextPendingDay()
                } label: {
                    HStack(spacing: 4) {
                        Text("下一待整理日")
                            .font(.caption.weight(.medium))
                            .lineLimit(1)
                        Text("⌘]")
                            .font(.system(size: 10, design: .monospaced))
                            .foregroundStyle(.secondary)
                    }
                    .frame(maxWidth: .infinity)
                }
                .buttonStyle(.borderedProminent)
                .controlSize(.small)
                .disabled(appViewModel.pendingDaysCount == 0)
                .help("跳转到下一个处于“待整理”或“有新增待审”状态的日期 (⌘])")
            }

            // 当前选中日快捷标记完成/重置按钮（Task 3.1）
            currentDayQuickActionRow
        }
    }

    @ViewBuilder
    private var currentDayQuickActionRow: some View {
        let dayVM = appViewModel.daySliceVM
        let summary = dayVM.currentDaySummary
        HStack(spacing: 6) {
            Text("当前: \(summary.monthDay.displayTitle)")
                .font(.caption.weight(.semibold))
                .foregroundStyle(.primary)

            Text("· \(summary.totalCount) 项")
                .font(.caption.monospacedDigit())
                .foregroundStyle(.secondary)

            Spacer()

            switch summary.status {
            case .empty:
                Text("历年无照片")
                    .font(.caption2)
                    .foregroundStyle(.tertiary)

            case .pending, .hasNew:
                Button {
                    dayVM.markDayCompleted()
                } label: {
                    Label("标记当日已整理", systemImage: "checkmark.circle.fill")
                        .font(.caption2.weight(.medium))
                }
                .buttonStyle(.bordered)
                .tint(.green)
                .controlSize(.mini)

            case .completed:
                Button {
                    dayVM.resetDayProgress()
                } label: {
                    Label("重置未整理", systemImage: "arrow.counterclockwise")
                        .font(.caption2)
                }
                .buttonStyle(.bordered)
                .controlSize(.mini)
            }
        }
        .padding(.horizontal, 8)
        .padding(.vertical, 5)
        .background(
            RoundedRectangle(cornerRadius: 7)
                .fill(Color(nsColor: .controlBackgroundColor).opacity(0.65))
        )
    }

    // MARK: - 4-State Legend

    private var statusLegendRow: some View {
        HStack(spacing: 10) {
            legendItem(color: Color.secondary.opacity(0.25), label: "空置", dotColor: nil)
            legendItem(color: Color.primary.opacity(0.14), label: "待整理", dotColor: nil)
            legendItem(color: Color.green.opacity(0.75), label: "已整理", dotColor: nil)
            legendItem(color: Color.orange.opacity(0.75), label: "有新增", dotColor: .orange)
            Spacer()
        }
        .font(.system(size: 10))
        .foregroundStyle(.secondary)
    }

    private func legendItem(color: Color, label: String, dotColor: Color?) -> some View {
        HStack(spacing: 4) {
            ZStack(alignment: .topTrailing) {
                RoundedRectangle(cornerRadius: 3)
                    .fill(color)
                    .frame(width: 10, height: 10)
                if let dotColor {
                    Circle()
                        .fill(dotColor)
                        .frame(width: 4, height: 4)
                        .offset(x: 1, y: -1)
                }
            }
            Text(label)
        }
    }

    // MARK: - Month Section & 366-Day Cells

    private func monthSectionView(for month: Int) -> some View {
        let monthSummaries = appViewModel.summaries(forMonth: month)
        let nonEmptyInMonth = monthSummaries.filter { $0.status != .empty }.count
        let completedInMonth = monthSummaries.filter { $0.status == .completed }.count

        return VStack(alignment: .leading, spacing: 6) {
            HStack {
                Text("\(month)月")
                    .font(.subheadline.weight(.bold))
                    .foregroundStyle(.primary)

                if month == 2 {
                    Text("含闰日 2/29")
                        .font(.system(size: 10, weight: .medium))
                        .foregroundStyle(.secondary)
                        .padding(.horizontal, 5)
                        .padding(.vertical, 1)
                        .background(Color.secondary.opacity(0.12), in: Capsule())
                }

                Spacer()

                if nonEmptyInMonth > 0 {
                    Text("\(completedInMonth)/\(nonEmptyInMonth) 天已清")
                        .font(.caption2.monospacedDigit())
                        .foregroundStyle(completedInMonth == nonEmptyInMonth ? Color.green : Color.secondary)
                } else {
                    Text("无照片")
                        .font(.caption2)
                        .foregroundStyle(.tertiary)
                }
            }

            LazyVGrid(columns: dayColumns, spacing: 4) {
                ForEach(monthSummaries) { summary in
                    dayCellButton(for: summary)
                }
            }
        }
        .padding(8)
        .background(
            RoundedRectangle(cornerRadius: 9)
                .fill(Color(nsColor: .controlBackgroundColor).opacity(0.45))
        )
    }

    private func dayCellButton(for summary: DaySliceSummary) -> some View {
        let isSelected = summary.monthDay == appViewModel.selectedMonthDay
        let isToday = summary.monthDay == MonthDay.today()

        return Button {
            appViewModel.selectDay(summary.monthDay)
        } label: {
            ZStack(alignment: .topTrailing) {
                VStack(spacing: 1) {
                    Text("\(summary.monthDay.day)")
                        .font(.system(size: 11, weight: isSelected || isToday ? .bold : .medium, design: .rounded))
                        .foregroundStyle(cellTextColor(for: summary.status, isSelected: isSelected))

                    if summary.totalCount > 0 {
                        Text("\(summary.totalCount)")
                            .font(.system(size: 8, weight: .regular, design: .monospaced))
                            .foregroundStyle(cellSubtextColor(for: summary.status, isSelected: isSelected))
                            .lineLimit(1)
                    } else {
                        Text("·")
                            .font(.system(size: 8))
                            .foregroundStyle(.quaternary)
                    }
                }
                .frame(maxWidth: .infinity)
                .frame(height: 30)
                .background(
                    RoundedRectangle(cornerRadius: 6)
                        .fill(cellBackgroundColor(for: summary.status, isSelected: isSelected))
                )
                .overlay(
                    RoundedRectangle(cornerRadius: 6)
                        .strokeBorder(
                            cellBorderColor(for: summary.status, isSelected: isSelected, isToday: isToday),
                            lineWidth: isSelected ? 2.0 : (isToday ? 1.2 : 0.8)
                        )
                )

                // `hasNew` 琥珀橙色新增角标圆点
                if summary.status == .hasNew {
                    Circle()
                        .fill(Color.orange)
                        .frame(width: 6, height: 6)
                        .overlay(
                            Circle()
                                .stroke(Color(nsColor: .windowBackgroundColor), lineWidth: 1)
                        )
                        .offset(x: -2, y: 2)
                } else if summary.status == .completed {
                    Image(systemName: "checkmark")
                        .font(.system(size: 6, weight: .bold))
                        .foregroundStyle(Color.green)
                        .offset(x: -2, y: 2)
                }
            }
        }
        .buttonStyle(.plain)
        .help(tooltipText(for: summary))
    }

    // MARK: - Cell Styling & Tooltip Helpers

    private func cellBackgroundColor(for status: DayReviewStatus, isSelected: Bool) -> Color {
        if isSelected {
            switch status {
            case .completed:
                return Color.green.opacity(0.38)
            case .hasNew:
                return Color.orange.opacity(0.38)
            case .pending, .empty:
                return Color.accentColor.opacity(0.28)
            }
        }
        switch status {
        case .empty:
            return Color.secondary.opacity(0.07)
        case .pending:
            return Color.primary.opacity(0.09)
        case .completed:
            return Color.green.opacity(0.20)
        case .hasNew:
            return Color.orange.opacity(0.22)
        }
    }

    private func cellBorderColor(for status: DayReviewStatus, isSelected: Bool, isToday: Bool) -> Color {
        if isSelected {
            return Color.accentColor
        }
        if isToday {
            return Color.accentColor.opacity(0.75)
        }
        switch status {
        case .empty:
            return Color.clear
        case .pending:
            return Color.secondary.opacity(0.22)
        case .completed:
            return Color.green.opacity(0.55)
        case .hasNew:
            return Color.orange.opacity(0.65)
        }
    }

    private func cellTextColor(for status: DayReviewStatus, isSelected: Bool) -> Color {
        if isSelected {
            return .primary
        }
        switch status {
        case .empty:
            return Color.secondary.opacity(0.45)
        case .pending:
            return .primary
        case .completed:
            return Color.green
        case .hasNew:
            return Color.orange
        }
    }

    private func cellSubtextColor(for status: DayReviewStatus, isSelected: Bool) -> Color {
        if isSelected {
            return .primary.opacity(0.85)
        }
        switch status {
        case .empty:
            return Color.secondary.opacity(0.3)
        case .pending:
            return .secondary
        case .completed:
            return Color.green.opacity(0.85)
        case .hasNew:
            return Color.orange.opacity(0.9)
        }
    }

    private func tooltipText(for summary: DaySliceSummary) -> String {
        let title = summary.monthDay.displayTitle
        switch summary.status {
        case .empty:
            return "\(title) · 历年无照片 (空置)"
        case .pending:
            return "\(title) · 历年共 \(summary.totalCount) 项 (待整理)"
        case .completed:
            return "\(title) · 历年共 \(summary.totalCount) 项 (已整理)"
        case .hasNew:
            return "\(title) · 历年共 \(summary.totalCount) 项 (含新增 \(summary.newUnreviewedCount) 项待审)"
        }
    }

    private func scrollToMonth(_ month: Int, proxy: ScrollViewProxy, animated: Bool) {
        let targetID = "month-\(month)"
        if animated {
            withAnimation(.easeInOut(duration: 0.2)) {
                proxy.scrollTo(targetID, anchor: .top)
            }
        } else {
            proxy.scrollTo(targetID, anchor: .top)
        }
    }
}
