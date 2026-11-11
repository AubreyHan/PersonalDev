import SwiftUI

public struct DownloadFooterView: View {
    @Bindable var viewModel: MainViewModel
    @State private var isFormatPopoverPresented = false

    public init(viewModel: MainViewModel) {
        self.viewModel = viewModel
    }

    public var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            if let errorMessage = viewModel.errorMessage, !errorMessage.isEmpty {
                HStack(spacing: 6) {
                    Image(systemName: "exclamationmark.circle.fill")
                        .foregroundStyle(.red)
                    Text(errorMessage)
                        .font(.caption)
                        .foregroundStyle(.red)
                        .lineLimit(1)
                        .truncationMode(.tail)
                }
            } else if let statusMessage = viewModel.statusMessage, !statusMessage.isEmpty {
                HStack(spacing: 6) {
                    Image(systemName: "checkmark.circle.fill")
                        .foregroundStyle(.green)
                    Text(statusMessage)
                        .font(.caption)
                        .foregroundStyle(.secondary)
                        .lineLimit(1)
                        .truncationMode(.middle)
                }
            }

            HStack(spacing: 16) {
                // 1. 左侧：当前保存目录路径显示 + 「更改目录...」按钮
                HStack(spacing: 8) {
                    Image(systemName: "folder.fill")
                        .foregroundStyle(.secondary)

                    Text(viewModel.saveDirectoryURL.path)
                        .font(.caption)
                        .foregroundStyle(.secondary)
                        .lineLimit(1)
                        .truncationMode(.middle)
                        .frame(minWidth: 120, maxWidth: 260, alignment: .leading)
                        .help(viewModel.saveDirectoryURL.path)

                    Button {
                        viewModel.chooseSaveDirectory()
                    } label: {
                        Text("更改目录...")
                            .lineLimit(1)
                            .fixedSize(horizontal: true, vertical: false)
                    }
                    .controlSize(.small)
                }

                Spacer(minLength: 12)

                // 2. 中间：下载类型多选下拉框（支持勾选 GPX / TCX / FIT 一种或多种）
                HStack(spacing: 8) {
                    Text("下载类型:")
                        .font(.subheadline)
                        .fontWeight(.medium)
                        .foregroundStyle(.secondary)
                        .lineLimit(1)
                        .fixedSize(horizontal: true, vertical: false)

                    Button {
                        isFormatPopoverPresented.toggle()
                    } label: {
                        HStack(spacing: 8) {
                            Text(exportFormatSummaryText)
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
                    .popover(isPresented: $isFormatPopoverPresented, arrowEdge: .top) {
                        VStack(alignment: .leading, spacing: 10) {
                            HStack {
                                Text("选择下载格式 (可多选)")
                                    .font(.caption)
                                    .fontWeight(.semibold)
                                    .foregroundStyle(.secondary)

                                Spacer()

                                Button("全选") {
                                    viewModel.selectAllExportFormats()
                                }
                                .buttonStyle(.link)
                                .font(.caption)
                            }

                            Divider()

                            VStack(alignment: .leading, spacing: 8) {
                                ForEach(ExportFormat.allCases) { format in
                                    Toggle(
                                        isOn: Binding(
                                            get: { viewModel.selectedExportFormats.contains(format) },
                                            set: { _ in viewModel.toggleExportFormat(format) }
                                        )
                                    ) {
                                        Text("\(format.displayName) (.\(format.fileExtension))")
                                            .font(.subheadline)
                                            .lineLimit(1)
                                    }
                                    .toggleStyle(.checkbox)
                                }
                            }
                            .frame(maxWidth: .infinity, alignment: .leading)
                        }
                        .padding(12)
                        .frame(width: 210)
                    }
                }
                .fixedSize(horizontal: true, vertical: false)

                // 3. 合并勾选项：严格只在勾选 >= 2 且活动类型完全相同时展示
                if viewModel.canMerge {
                    Toggle("合并为一个文件", isOn: $viewModel.isMergeEnabled)
                        .toggleStyle(.checkbox)
                        .font(.subheadline)
                        .lineLimit(1)
                        .fixedSize(horizontal: true, vertical: false)
                }

                // 4. 右侧：下载主按钮
                Button {
                    Task {
                        await viewModel.downloadSelected()
                    }
                } label: {
                    HStack(spacing: 6) {
                        if viewModel.isDownloading {
                            ProgressView()
                                .controlSize(.small)
                        }
                        Text("下载选中项 (\(viewModel.selectedActivities.count))")
                            .lineLimit(1)
                            .fixedSize(horizontal: true, vertical: false)
                    }
                    .padding(.horizontal, 4)
                }
                .buttonStyle(.borderedProminent)
                .controlSize(.regular)
                .disabled(
                    viewModel.selectedActivities.isEmpty ||
                    viewModel.selectedExportFormats.isEmpty ||
                    viewModel.isDownloading
                )
                .fixedSize(horizontal: true, vertical: false)
            }
        }
        .padding(.horizontal, 18)
        .padding(.vertical, 12)
        .background(.bar)
    }

    private var exportFormatSummaryText: String {
        let ordered = viewModel.orderedSelectedExportFormats
        if ordered.isEmpty {
            return "未选择格式"
        } else if ordered.count == ExportFormat.allCases.count {
            return "全部 (GPX/TCX/FIT)"
        } else {
            return ordered.map(\.displayName).joined(separator: " + ")
        }
    }
}
