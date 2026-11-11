import SwiftUI

public struct FilterHeaderView: View {
    @Bindable var viewModel: MainViewModel
    @State private var isTypePopoverPresented = false

    public init(viewModel: MainViewModel) {
        self.viewModel = viewModel
    }

    public var body: some View {
        HStack(spacing: 16) {
            // 1. 起始日期日历控件（仅起始日期，无结束日期，范围 2000-01-01 至今天）
            HStack(spacing: 8) {
                Text("起始日期:")
                    .fontWeight(.medium)

                DatePicker(
                    "",
                    selection: $viewModel.startDate,
                    in: MainViewModel.earliestAllowedDate...Date(),
                    displayedComponents: .date
                )
                .labelsHidden()
                .datePickerStyle(.compact)
            }

            Divider()
                .frame(height: 20)

            // 2. 动态运动类型多选下拉框（仅展示当前日期范围内实际存在的类型）
            HStack(spacing: 8) {
                Text("运动类型:")
                    .fontWeight(.medium)

                Button {
                    isTypePopoverPresented.toggle()
                } label: {
                    HStack(spacing: 6) {
                        Text(typeFilterSummaryText)
                            .lineLimit(1)
                        Image(systemName: "chevron.down")
                            .font(.caption)
                            .foregroundStyle(.secondary)
                    }
                    .frame(minWidth: 130, alignment: .leading)
                }
                .disabled(viewModel.availableTypes.isEmpty)
                .popover(isPresented: $isTypePopoverPresented, arrowEdge: .bottom) {
                    VStack(alignment: .leading, spacing: 10) {
                        HStack {
                            Button("全选") {
                                viewModel.selectAllTypes()
                            }
                            .buttonStyle(.link)

                            Spacer()

                            Button("取消全选") {
                                viewModel.deselectAllTypes()
                            }
                            .buttonStyle(.link)
                        }
                        .padding(.bottom, 2)

                        Divider()

                        ScrollView {
                            VStack(alignment: .leading, spacing: 6) {
                                ForEach(viewModel.availableTypes, id: \.self) { typeKey in
                                    Toggle(
                                        isOn: Binding(
                                            get: { viewModel.selectedTypes.contains(typeKey) },
                                            set: { _ in viewModel.toggleType(typeKey) }
                                        )
                                    ) {
                                        Text(typeKey)
                                    }
                                    .toggleStyle(.checkbox)
                                }
                            }
                            .frame(maxWidth: .infinity, alignment: .leading)
                        }
                        .frame(maxHeight: 240)
                    }
                    .padding(12)
                    .frame(width: 220)
                }
            }

            Spacer()

            // 3. 右侧：当前自动识别的区域标签、刷新按钮、「更换/退出 Key」按钮
            if let region = viewModel.currentRegion {
                Text(region.displayName)
                    .font(.caption)
                    .fontWeight(.medium)
                    .padding(.horizontal, 8)
                    .padding(.vertical, 4)
                    .background(Color.accentColor.opacity(0.12))
                    .clipShape(Capsule())
            }

            Button {
                Task {
                    await viewModel.loadActivities()
                }
            } label: {
                Label("刷新", systemImage: "arrow.clockwise")
            }
            .disabled(viewModel.isLoadingActivities)

            Button("更换/退出 Key") {
                viewModel.logout()
            }
        }
        .padding(.horizontal, 16)
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
