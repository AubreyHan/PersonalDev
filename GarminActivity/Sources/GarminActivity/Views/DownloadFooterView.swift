import SwiftUI

public struct DownloadFooterView: View {
    @Bindable var viewModel: MainViewModel

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
                        .lineLimit(2)
                }
            } else if let statusMessage = viewModel.statusMessage, !statusMessage.isEmpty {
                HStack(spacing: 6) {
                    Image(systemName: "checkmark.circle.fill")
                        .foregroundStyle(.green)
                    Text(statusMessage)
                        .font(.caption)
                        .foregroundStyle(.secondary)
                        .lineLimit(1)
                }
            }

            HStack(spacing: 16) {
                // 左侧：当前保存目录路径显示 + 「更改目录...」按钮
                HStack(spacing: 8) {
                    Image(systemName: "folder")
                        .foregroundStyle(.secondary)

                    Text(viewModel.saveDirectoryURL.path)
                        .font(.caption)
                        .foregroundStyle(.secondary)
                        .lineLimit(1)
                        .truncationMode(.middle)
                        .frame(maxWidth: 260, alignment: .leading)
                        .help(viewModel.saveDirectoryURL.path)

                    Button("更改目录...") {
                        viewModel.chooseSaveDirectory()
                    }
                    .controlSize(.small)
                }

                Spacer()

                // 中间：导出格式 Picker (GPX / TCX / FIT)
                Picker("导出格式:", selection: $viewModel.exportFormat) {
                    ForEach(ExportFormat.allCases) { format in
                        Text(format.displayName).tag(format)
                    }
                }
                .pickerStyle(.segmented)
                .frame(width: 210)

                // 合并勾选项：严格只在勾选 >= 2 且活动类型完全相同时展示
                if viewModel.canMerge {
                    Toggle("合并为一个文件", isOn: $viewModel.isMergeEnabled)
                        .toggleStyle(.checkbox)
                }

                // 右侧：下载主按钮
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
                    }
                }
                .buttonStyle(.borderedProminent)
                .disabled(viewModel.selectedActivities.isEmpty || viewModel.isDownloading)
            }
        }
        .padding(.horizontal, 16)
        .padding(.vertical, 12)
        .background(.bar)
    }
}
