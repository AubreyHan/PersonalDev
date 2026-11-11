import SwiftUI

/// 左侧栏顶部的全局清理统计看板（FR-2.5：展示 `已整理 X / Y 有照天数` 进度条、`累计清理 N 张` 与 `累计释放 X MB/GB`）
public struct CleanupStatsCard: View {
    public let completedDaysCount: Int
    public let nonEmptyDaysCount: Int
    public let hasNewDaysCount: Int
    public let totalDeletedAssetCount: Int
    public let formattedTotalFreedBytes: String
    public let totalEmptyAlbumsDeleted: Int

    public init(appViewModel: AppViewModel) {
        self.completedDaysCount = appViewModel.completedDaysCount
        self.nonEmptyDaysCount = appViewModel.nonEmptyDaysCount
        self.hasNewDaysCount = appViewModel.hasNewDaysCount
        self.totalDeletedAssetCount = appViewModel.totalDeletedAssetCount
        self.formattedTotalFreedBytes = appViewModel.formattedTotalFreedBytes
        self.totalEmptyAlbumsDeleted = appViewModel.totalEmptyAlbumsDeleted
    }

    public init(
        completedDaysCount: Int,
        nonEmptyDaysCount: Int,
        hasNewDaysCount: Int = 0,
        totalDeletedAssetCount: Int,
        formattedTotalFreedBytes: String,
        totalEmptyAlbumsDeleted: Int = 0
    ) {
        self.completedDaysCount = completedDaysCount
        self.nonEmptyDaysCount = nonEmptyDaysCount
        self.hasNewDaysCount = hasNewDaysCount
        self.totalDeletedAssetCount = totalDeletedAssetCount
        self.formattedTotalFreedBytes = formattedTotalFreedBytes
        self.totalEmptyAlbumsDeleted = totalEmptyAlbumsDeleted
    }

    private var progressFraction: Double {
        guard nonEmptyDaysCount > 0 else { return 0.0 }
        return min(max(Double(completedDaysCount) / Double(nonEmptyDaysCount), 0.0), 1.0)
    }

    private var progressPercentageText: String {
        "\(Int((progressFraction * 100).rounded()))%"
    }

    public var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            // Top Row: Progress Header
            HStack(alignment: .firstTextBaseline) {
                Label("已整理进度", systemImage: "chart.bar.fill")
                    .font(.caption.weight(.semibold))
                    .foregroundStyle(.secondary)

                Spacer()

                Text("已整理 \(completedDaysCount) / \(nonEmptyDaysCount) 有照天数")
                    .font(.caption.monospacedDigit().weight(.semibold))
                    .foregroundStyle(.primary)
            }

            // Progress Bar
            ProgressView(value: progressFraction)
                .tint(.green)
                .controlSize(.small)
                .accessibilityLabel("366天整理进度 \(progressPercentageText)")

            if hasNewDaysCount > 0 {
                HStack(spacing: 4) {
                    Circle()
                        .fill(Color.orange)
                        .frame(width: 6, height: 6)
                    Text("\(hasNewDaysCount) 个已整理日期有新增待审照片")
                        .font(.system(size: 11, weight: .medium))
                        .foregroundStyle(.orange)
                    Spacer()
                }
            }

            Divider()

            // Bottom Metrics Grid: 累计清理 N 张 & 累计释放 X MB/GB
            HStack(spacing: 10) {
                metricBox(
                    icon: "trash.slash.fill",
                    tint: .pink,
                    title: "累计清理",
                    value: "\(totalDeletedAssetCount) 张"
                )

                metricBox(
                    icon: "internaldrive.fill",
                    tint: .mint,
                    title: "累计释放",
                    value: formattedTotalFreedBytes
                )
            }

            if totalEmptyAlbumsDeleted > 0 {
                HStack(spacing: 5) {
                    Image(systemName: "folder.badge.minus")
                        .font(.system(size: 10))
                        .foregroundStyle(.secondary)
                    Text("已自动清零空相簿/文件夹 \(totalEmptyAlbumsDeleted) 个")
                        .font(.system(size: 10).monospacedDigit())
                        .foregroundStyle(.secondary)
                    Spacer()
                }
            }
        }
        .padding(12)
        .background(
            RoundedRectangle(cornerRadius: 11)
                .fill(Color(nsColor: .controlBackgroundColor).opacity(0.85))
        )
        .overlay(
            RoundedRectangle(cornerRadius: 11)
                .strokeBorder(Color.secondary.opacity(0.18), lineWidth: 1)
        )
    }

    private func metricBox(icon: String, tint: Color, title: String, value: String) -> some View {
        HStack(spacing: 7) {
            Image(systemName: icon)
                .font(.system(size: 13, weight: .semibold))
                .foregroundStyle(tint)
                .frame(width: 24, height: 24)
                .background(tint.opacity(0.14), in: RoundedRectangle(cornerRadius: 6))

            VStack(alignment: .leading, spacing: 1) {
                Text(title)
                    .font(.system(size: 10))
                    .foregroundStyle(.secondary)
                Text(value)
                    .font(.caption.monospacedDigit().weight(.semibold))
                    .foregroundStyle(.primary)
                    .lineLimit(1)
            }
            Spacer(minLength: 0)
        }
        .padding(.horizontal, 8)
        .padding(.vertical, 6)
        .background(
            RoundedRectangle(cornerRadius: 8)
                .fill(Color(nsColor: .windowBackgroundColor).opacity(0.6))
        )
    }
}
