import SwiftUI
import Combine

public struct LogViewerView: View {
    @ObservedObject var appState: AppState
    @Environment(\.dismiss) private var dismiss

    @State private var logLines: [String] = []
    @State private var filterText: String = ""
    @State private var selectedLevel: String = "ALL"
    @State private var autoScroll: Bool = true
    @State private var fileSizeString: String = "0 KB"
    @State private var showClearConfirm: Bool = false

    private let timer = Timer.publish(every: 1.5, on: .main, in: .common).autoconnect()

    public init(appState: AppState) {
        self.appState = appState
    }

    public var body: some View {
        VStack(spacing: 0) {
            // 顶部导航栏
            HStack(alignment: .center) {
                VStack(alignment: .leading, spacing: 3) {
                    HStack(spacing: 8) {
                        Text("运行日志")
                            .font(.system(size: 16, weight: .bold))

                        // 容量徽标 (<= 1MB FIFO)
                        HStack(spacing: 4) {
                            Circle()
                                .fill(Color.green)
                                .frame(width: 6, height: 6)
                            Text("容量: \(fileSizeString) / 1.0 MB (FIFO 循环)")
                                .font(.system(size: 11, weight: .medium))
                                .foregroundColor(.secondary)
                        }
                        .padding(.horizontal, 6)
                        .padding(.vertical, 2)
                        .background(Color.secondary.opacity(0.12))
                        .cornerRadius(4)
                    }

                    Text("存放路径: \(AppLogger.shared.logFileURL.path)")
                        .font(.system(size: 10))
                        .foregroundColor(.secondary)
                        .lineLimit(1)
                        .truncationMode(.middle)
                }

                Spacer()

                Button {
                    dismiss()
                } label: {
                    Image(systemName: "xmark.circle.fill")
                        .foregroundColor(.secondary)
                        .font(.system(size: 16))
                }
                .buttonStyle(.plain)
            }
            .padding(.horizontal, 16)
            .padding(.vertical, 12)
            .background(Color(NSColor.windowBackgroundColor))

            Divider()

            // 工具栏：过滤、搜索、操作按钮
            HStack(spacing: 10) {
                // 搜索框
                HStack {
                    Image(systemName: "magnifyingglass")
                        .foregroundColor(.secondary)
                    TextField("搜索日志内容...", text: $filterText)
                        .textFieldStyle(.plain)
                        .font(.system(size: 12))
                    if !filterText.isEmpty {
                        Button {
                            filterText = ""
                        } label: {
                            Image(systemName: "xmark.circle.fill")
                                .foregroundColor(.secondary)
                        }
                        .buttonStyle(.plain)
                    }
                }
                .padding(6)
                .background(Color(NSColor.controlBackgroundColor))
                .cornerRadius(6)
                .overlay(
                    RoundedRectangle(cornerRadius: 6)
                        .stroke(Color(NSColor.separatorColor), lineWidth: 0.8)
                )
                .frame(width: 220)

                // 等级过滤
                Picker("等级", selection: $selectedLevel) {
                    Text("全部").tag("ALL")
                    Text("SYNC").tag("SYNC")
                    Text("INFO").tag("INFO")
                    Text("WARN").tag("WARN")
                    Text("ERROR").tag("ERROR")
                }
                .pickerStyle(.segmented)
                .frame(width: 250)

                Toggle("自动滚动", isOn: $autoScroll)
                    .font(.system(size: 11))

                Spacer()

                // 操作按钮组
                Button {
                    copyLogsToClipboard()
                } label: {
                    Label("拷贝", systemImage: "doc.on.doc")
                }
                .controlSize(.small)
                .help("拷贝全部日志内容到剪贴板")

                Button {
                    appState.revealInFinder(path: AppLogger.shared.logFileURL.path)
                } label: {
                    Label("访达定位", systemImage: "folder")
                }
                .controlSize(.small)
                .help("在访达中定位 app.log 文件")

                Button(role: .destructive) {
                    showClearConfirm = true
                } label: {
                    Label("清空", systemImage: "trash")
                }
                .controlSize(.small)
                .help("清空当前所有日志内容")
            }
            .padding(.horizontal, 16)
            .padding(.vertical, 8)
            .background(Color(NSColor.controlBackgroundColor).opacity(0.6))

            Divider()

            // 日志主文本控制台
            ScrollViewReader { proxy in
                ScrollView {
                    LazyVStack(alignment: .leading, spacing: 2) {
                        if filteredLines.isEmpty {
                            HStack {
                                Spacer()
                                Text(logLines.isEmpty ? "暂无日志" : "无匹配项")
                                    .font(.system(size: 12))
                                    .foregroundColor(.secondary)
                                    .padding(.top, 40)
                                Spacer()
                            }
                        } else {
                            ForEach(Array(filteredLines.enumerated()), id: \.offset) { index, line in
                                Text(line)
                                    .font(.system(size: 11, design: .monospaced))
                                    .foregroundColor(colorForLine(line))
                                    .textSelection(.enabled)
                                    .frame(maxWidth: .infinity, alignment: .leading)
                                    .id(index)
                            }
                        }
                    }
                    .padding(12)
                }
                .background(Color(NSColor.textBackgroundColor))
                .onChange(of: filteredLines.count) { _ in
                    if autoScroll && !filteredLines.isEmpty {
                        withAnimation {
                            proxy.scrollTo(filteredLines.count - 1, anchor: .bottom)
                        }
                    }
                }
            }
        }
        .frame(minWidth: 780, minHeight: 480)
        .onAppear {
            reloadLogs()
        }
        .onReceive(timer) { _ in
            reloadLogs()
        }
        .confirmationDialog(
            "确定要清空全部日志吗？",
            isPresented: $showClearConfirm,
            titleVisibility: .visible
        ) {
            Button("清空全部日志", role: .destructive) {
                AppLogger.shared.clear()
                reloadLogs()
            }
            Button("取消", role: .cancel) {}
        }
    }

    private var filteredLines: [String] {
        logLines.filter { line in
            let matchText = filterText.isEmpty || line.localizedCaseInsensitiveContains(filterText)
            let matchLevel: Bool
            if selectedLevel == "ALL" {
                matchLevel = true
            } else {
                matchLevel = line.contains("[\(selectedLevel)]")
            }
            return matchText && matchLevel
        }
    }

    private func reloadLogs() {
        let text = AppLogger.shared.readAllLogs()
        self.logLines = text.components(separatedBy: .newlines).filter { !$0.isEmpty }

        let bytes = AppLogger.shared.getCurrentFileSize()
        if bytes < 1024 {
            self.fileSizeString = "\(bytes) B"
        } else if bytes < 1024 * 1024 {
            self.fileSizeString = String(format: "%.1f KB", Double(bytes) / 1024.0)
        } else {
            self.fileSizeString = String(format: "%.2f MB", Double(bytes) / (1024.0 * 1024.0))
        }
    }

    private func copyLogsToClipboard() {
        let text = filteredLines.joined(separator: "\n")
        NSPasteboard.general.clearContents()
        NSPasteboard.general.setString(text, forType: .string)
    }

    private func colorForLine(_ line: String) -> Color {
        if line.contains("[ERROR]") {
            return .red
        } else if line.contains("[WARN]") {
            return .orange
        } else if line.contains("[SYNC]") {
            return .green
        } else if line.contains("[SYSTEM]") {
            return .purple
        } else {
            return Color(NSColor.textColor)
        }
    }
}
