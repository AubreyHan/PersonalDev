import SwiftUI

public struct FilterHeaderView: View {
    @Bindable var viewModel: MainViewModel
    @State private var isTypePopoverPresented = false

    public init(viewModel: MainViewModel) {
        self.viewModel = viewModel
    }

    public var body: some View {
        HStack(spacing: 16) {
            // 1. 起始日期与结束日期日历控件（分开选择，2000-01-01 至今天，默认均为当天）
            HStack(spacing: 12) {
                HStack(spacing: 6) {
                    Text("起始日期:")
                        .font(.subheadline)
                        .fontWeight(.medium)
                        .foregroundStyle(.secondary)
                        .lineLimit(1)
                        .fixedSize(horizontal: true, vertical: false)

                    DatePicker(
                        "",
                        selection: $viewModel.startDate,
                        in: MainViewModel.earliestAllowedDate...Date(),
                        displayedComponents: .date
                    )
                    .labelsHidden()
                    .datePickerStyle(.compact)
                    .fixedSize()
                }
                .fixedSize(horizontal: true, vertical: false)

                HStack(spacing: 6) {
                    Text("结束日期:")
                        .font(.subheadline)
                        .fontWeight(.medium)
                        .foregroundStyle(.secondary)
                        .lineLimit(1)
                        .fixedSize(horizontal: true, vertical: false)

                    DatePicker(
                        "",
                        selection: $viewModel.endDate,
                        in: MainViewModel.earliestAllowedDate...Date(),
                        displayedComponents: .date
                    )
                    .labelsHidden()
                    .datePickerStyle(.compact)
                    .fixedSize()
                }
                .fixedSize(horizontal: true, vertical: false)
            }
            .fixedSize(horizontal: true, vertical: false)

            Divider()
                .frame(height: 18)

            // 2. 动态运动类型多选下拉框（仅展示当前起止日期范围内实际存在的类型）
            HStack(spacing: 8) {
                Text("运动类型:")
                    .font(.subheadline)
                    .fontWeight(.medium)
                    .foregroundStyle(.secondary)
                    .lineLimit(1)
                    .fixedSize(horizontal: true, vertical: false)

                Button {
                    isTypePopoverPresented.toggle()
                } label: {
                    HStack(spacing: 8) {
                        Text(typeFilterSummaryText)
                            .font(.subheadline)
                            .lineLimit(1)
                            .fixedSize(horizontal: true, vertical: false)
                        Spacer(minLength: 4)
                        Image(systemName: "chevron.down")
                            .font(.caption2.weight(.semibold))
                            .foregroundStyle(.secondary)
                    }
                    .padding(.horizontal, 10)
                    .padding(.vertical, 5)
                    .frame(width: 145, alignment: .leading)
                    .background(
                        RoundedRectangle(cornerRadius: 6, style: .continuous)
                            .fill(Color(nsColor: .controlColor))
                    )
                    .overlay(
                        RoundedRectangle(cornerRadius: 6, style: .continuous)
                            .strokeBorder(Color(nsColor: .separatorColor), lineWidth: 1)
                    )
                }
                .buttonStyle(.plain)
                .disabled(viewModel.availableTypes.isEmpty)
                .opacity(viewModel.availableTypes.isEmpty ? 0.55 : 1.0)
                .popover(isPresented: $isTypePopoverPresented, arrowEdge: .bottom) {
                    VStack(alignment: .leading, spacing: 10) {
                        HStack {
                            Text("选择运动类型")
                                .font(.caption)
                                .fontWeight(.semibold)
                                .foregroundStyle(.secondary)

                            Spacer()

                            Button("全选") {
                                viewModel.selectAllTypes()
                            }
                            .buttonStyle(.link)
                            .font(.caption)

                            Text("·")
                                .foregroundStyle(.tertiary)

                            Button("清空") {
                                viewModel.deselectAllTypes()
                            }
                            .buttonStyle(.link)
                            .font(.caption)
                        }

                        Divider()

                        ScrollView {
                            VStack(alignment: .leading, spacing: 8) {
                                ForEach(viewModel.availableTypes, id: \.self) { typeKey in
                                    Toggle(
                                        isOn: Binding(
                                            get: { viewModel.selectedTypes.contains(typeKey) },
                                            set: { _ in viewModel.toggleType(typeKey) }
                                        )
                                    ) {
                                        Text(typeKey)
                                            .font(.subheadline)
                                            .lineLimit(1)
                                    }
                                    .toggleStyle(.checkbox)
                                }
                            }
                            .frame(maxWidth: .infinity, alignment: .leading)
                        }
                        .frame(maxHeight: 240)
                    }
                    .padding(12)
                    .frame(width: 230)
                }
            }
            .fixedSize(horizontal: true, vertical: false)

            Spacer(minLength: 12)

            // 3. 右侧：当前自动识别的区域标签、刷新按钮、「更换/退出 Key」按钮
            HStack(spacing: 10) {
                if let region = viewModel.currentRegion {
                    HStack(spacing: 5) {
                        Circle()
                            .fill(Color.green)
                            .frame(width: 6, height: 6)
                        Text(region.displayName)
                            .font(.caption)
                            .fontWeight(.medium)
                            .lineLimit(1)
                    }
                    .padding(.horizontal, 10)
                    .padding(.vertical, 4)
                    .background(Color.accentColor.opacity(0.12))
                    .clipShape(Capsule())
                    .fixedSize(horizontal: true, vertical: false)
                }

                Button {
                    Task {
                        await viewModel.loadActivities()
                    }
                } label: {
                    Label("刷新", systemImage: "arrow.clockwise")
                        .lineLimit(1)
                        .fixedSize(horizontal: true, vertical: false)
                }
                .controlSize(.regular)
                .disabled(viewModel.isLoadingActivities)

                Button {
                    viewModel.logout()
                } label: {
                    Text("更换/退出 Key")
                        .lineLimit(1)
                        .fixedSize(horizontal: true, vertical: false)
                }
                .controlSize(.regular)
            }
            .fixedSize(horizontal: true, vertical: false)
        }
        .padding(.horizontal, 18)
        .padding(.vertical, 12)
        .background(.bar)
    }

    private var typeFilterSummaryText: String {
        let total = viewModel.availableTypes.count
        guard total > 0 else {
            return "暂无类型"
        }
        let selectedCount = viewModel.selectedTypes.intersection(viewModel.availableTypes).count
        if selectedCount == 0 {
            return "未选择 (0/\(total))"
        } else if selectedCount == total {
            return "全部类型 (\(total))"
        } else {
            return "已选 \(selectedCount)/\(total) 种"
        }
    }
}
