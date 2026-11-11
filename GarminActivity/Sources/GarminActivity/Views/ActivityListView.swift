import SwiftUI

public struct ActivityListView: View {
    @Bindable var viewModel: MainViewModel

    public init(viewModel: MainViewModel) {
        self.viewModel = viewModel
    }

    public var body: some View {
        VStack(spacing: 0) {
            // 表头：全选/取消全选复选框 + 严格 5 项基础信息列（名称、日期/时间、类型、距离、时长）
            HStack(spacing: 12) {
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
                    .frame(maxWidth: .infinity, alignment: .leading)

                Text("日期/时间")
                    .font(.caption)
                    .fontWeight(.semibold)
                    .foregroundStyle(.secondary)
                    .frame(width: 165, alignment: .leading)

                Text("类型")
                    .font(.caption)
                    .fontWeight(.semibold)
                    .foregroundStyle(.secondary)
                    .frame(width: 130, alignment: .leading)

                Text("距离")
                    .font(.caption)
                    .fontWeight(.semibold)
                    .foregroundStyle(.secondary)
                    .frame(width: 100, alignment: .trailing)

                Text("时长")
                    .font(.caption)
                    .fontWeight(.semibold)
                    .foregroundStyle(.secondary)
                    .frame(width: 90, alignment: .trailing)
            }
            .padding(.horizontal, 16)
            .padding(.vertical, 8)
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
                VStack(spacing: 8) {
                    Image(systemName: "tray")
                        .font(.system(size: 32))
                        .foregroundStyle(.secondary)
                    Text(viewModel.allActivities.isEmpty ? "所选起始日期至今暂无活动记录" : "当前筛选类型下无活动记录")
                        .font(.callout)
                        .foregroundStyle(.secondary)
                }
                .frame(maxWidth: .infinity, maxHeight: .infinity)
            } else {
                List(viewModel.filteredActivities) { activity in
                    HStack(spacing: 12) {
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
                            .lineLimit(1)
                            .truncationMode(.tail)
                            .frame(maxWidth: .infinity, alignment: .leading)

                        Text(activity.formattedDateTime)
                            .font(.system(.body, design: .monospaced))
                            .foregroundStyle(.secondary)
                            .frame(width: 165, alignment: .leading)

                        Text(activity.typeKey)
                            .frame(width: 130, alignment: .leading)

                        Text(activity.formattedDistance)
                            .font(.system(.body, design: .monospaced))
                            .frame(width: 100, alignment: .trailing)

                        Text(activity.formattedDuration)
                            .font(.system(.body, design: .monospaced))
                            .frame(width: 90, alignment: .trailing)
                    }
                    .padding(.vertical, 2)
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
