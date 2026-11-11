import SwiftUI

public struct ActivityListView: View {
    @Bindable var viewModel: MainViewModel

    public init(viewModel: MainViewModel) {
        self.viewModel = viewModel
    }

    public var body: some View {
        VStack(spacing: 0) {
            // 表头：全选/取消全选复选框 + 严格 5 项基础信息列（名称、日期/时间、类型、距离、时长）
            HStack(spacing: 14) {
                Toggle(
                    "",
                    isOn: Binding(
                        get: { viewModel.areAllFilteredSelected },
                        set: { _ in viewModel.toggleSelectAllFiltered() }
                    )
                )
                .toggleStyle(.checkbox)
                .labelsHidden()
                .disabled(viewModel.filteredActivities.isEmpty)
                .frame(width: 28, alignment: .center)

                Text("名称")
                    .font(.caption)
                    .fontWeight(.semibold)
                    .foregroundStyle(.secondary)
                    .lineLimit(1)
                    .frame(maxWidth: .infinity, alignment: .leading)

                Text("日期/时间")
                    .font(.caption)
                    .fontWeight(.semibold)
                    .foregroundStyle(.secondary)
                    .lineLimit(1)
                    .frame(width: 170, alignment: .leading)

                Text("类型")
                    .font(.caption)
                    .fontWeight(.semibold)
                    .foregroundStyle(.secondary)
                    .lineLimit(1)
                    .frame(width: 140, alignment: .leading)

                Text("距离")
                    .font(.caption)
                    .fontWeight(.semibold)
                    .foregroundStyle(.secondary)
                    .lineLimit(1)
                    .frame(width: 105, alignment: .trailing)

                Text("时长")
                    .font(.caption)
                    .fontWeight(.semibold)
                    .foregroundStyle(.secondary)
                    .lineLimit(1)
                    .frame(width: 95, alignment: .trailing)
            }
            .padding(.horizontal, 18)
            .padding(.vertical, 9)
            .background(Color(nsColor: .controlBackgroundColor))

            Divider()

            // 列表主体
            if viewModel.isLoadingActivities {
                VStack(spacing: 12) {
                    ProgressView()
                    Text("正在拉取活动列表...")
                        .font(.callout)
                        .foregroundStyle(.secondary)
                }
                .frame(maxWidth: .infinity, maxHeight: .infinity)
            } else if viewModel.filteredActivities.isEmpty {
                VStack(spacing: 10) {
                    Image(systemName: "tray")
                        .font(.system(size: 32))
                        .foregroundStyle(.tertiary)
                    Text(viewModel.allActivities.isEmpty ? "所选起止日期范围内暂无活动记录" : "当前筛选类型下无活动记录")
                        .font(.callout)
                        .foregroundStyle(.secondary)
                }
                .frame(maxWidth: .infinity, maxHeight: .infinity)
            } else {
                List(viewModel.filteredActivities) { activity in
                    HStack(spacing: 14) {
                        Toggle(
                            "",
                            isOn: Binding(
                                get: { viewModel.isActivitySelected(activity.activityId) },
                                set: { isSelected in
                                    viewModel.setActivitySelection(activity.activityId, isSelected: isSelected)
                                }
                            )
                        )
                        .toggleStyle(.checkbox)
                        .labelsHidden()
                        .frame(width: 28, alignment: .center)

                        Text(activity.displayName)
                            .fontWeight(.medium)
                            .lineLimit(1)
                            .truncationMode(.tail)
                            .frame(maxWidth: .infinity, alignment: .leading)

                        Text(activity.formattedDateTime)
                            .font(.system(.subheadline, design: .monospaced))
                            .foregroundStyle(.secondary)
                            .lineLimit(1)
                            .frame(width: 170, alignment: .leading)

                        HStack(spacing: 0) {
                            Text(activity.typeKey)
                                .font(.caption)
                                .fontWeight(.medium)
                                .lineLimit(1)
                                .padding(.horizontal, 8)
                                .padding(.vertical, 3)
                                .background(Color.secondary.opacity(0.12))
                                .clipShape(Capsule())
                        }
                        .frame(width: 140, alignment: .leading)

                        Text(activity.formattedDistance)
                            .font(.system(.subheadline, design: .monospaced))
                            .lineLimit(1)
                            .frame(width: 105, alignment: .trailing)

                        Text(activity.formattedDuration)
                            .font(.system(.subheadline, design: .monospaced))
                            .lineLimit(1)
                            .frame(width: 95, alignment: .trailing)
                    }
                    .padding(.vertical, 3)
                    .contentShape(Rectangle())
                    .onTapGesture {
                        viewModel.toggleActivitySelection(activity.activityId)
                    }
                }
                .listStyle(.inset(alternatesRowBackgrounds: true))
            }
        }
    }
}
