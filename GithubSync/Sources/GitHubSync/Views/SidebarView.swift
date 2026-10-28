import SwiftUI

public struct SidebarView: View {
    @ObservedObject var appState: AppState

    public var body: some View {
        VStack(spacing: 0) {
            List(selection: $appState.selectedFolderId) {
                // 固定项：全部文件夹
                Section {
                    HStack {
                        Image(systemName: "square.grid.2x2")
                            .foregroundColor(.accentColor)
                            .frame(width: 18)
                        Text("全部文件夹")
                            .font(.system(size: 13, weight: .medium))
                        Spacer()
                        Text("\(appState.repos.count)")
                            .font(.system(size: 11))
                            .foregroundColor(.secondary)
                            .padding(.horizontal, 6)
                            .padding(.vertical, 1)
                            .background(Color.secondary.opacity(0.15))
                            .clipShape(Capsule())
                    }
                    .tag(nil as String?)
                }

                // 监控的根目录列表
                Section("监控根目录") {
                    ForEach(appState.watchedFolders) { folder in
                        HStack {
                            Image(systemName: "folder")
                                .foregroundColor(.blue)
                                .frame(width: 18)

                            VStack(alignment: .leading, spacing: 2) {
                                Text(folder.name)
                                    .font(.system(size: 13))
                                    .lineLimit(1)
                                Text(folder.path)
                                    .font(.system(size: 10))
                                    .foregroundColor(.secondary)
                                    .lineLimit(1)
                                    .truncationMode(.middle)
                            }

                            Spacer()

                            Text("\(folder.repoCount)")
                                .font(.system(size: 11))
                                .foregroundColor(.secondary)
                                .padding(.horizontal, 6)
                                .padding(.vertical, 1)
                                .background(Color.secondary.opacity(0.15))
                                .clipShape(Capsule())
                        }
                        .tag(folder.id as String?)
                        .contextMenu {
                            Button {
                                appState.revealInFinder(path: folder.path)
                            } label: {
                                Label("在访达中显示", systemImage: "folder")
                            }

                            Divider()

                            Button(role: .destructive) {
                                appState.removeFolder(id: folder.id)
                            } label: {
                                Label("移除此监控文件夹", systemImage: "trash")
                            }
                        }
                    }
                }
            }
            .listStyle(.sidebar)

            Divider()

            // 底部操作区
            HStack(spacing: 8) {
                Button {
                    appState.promptAddFolder()
                } label: {
                    HStack(spacing: 4) {
                        Image(systemName: "plus")
                        Text("添加文件夹")
                    }
                    .font(.system(size: 12))
                }
                .buttonStyle(.plain)
                .padding(.horizontal, 8)
                .padding(.vertical, 6)
                .background(Color(NSColor.controlBackgroundColor))
                .cornerRadius(6)
                .overlay(
                    RoundedRectangle(cornerRadius: 6)
                        .stroke(Color(NSColor.separatorColor), lineWidth: 0.8)
                )

                Spacer()

                Button {
                    appState.showLogsModal = true
                } label: {
                    Image(systemName: "list.bullet.rectangle")
                        .font(.system(size: 13))
                        .foregroundColor(.secondary)
                }
                .buttonStyle(.plain)
                .help("查看运行日志")

                Button {
                    appState.showSettings = true
                } label: {
                    Image(systemName: "gearshape")
                        .font(.system(size: 13))
                        .foregroundColor(.secondary)
                }
                .buttonStyle(.plain)
                .help("系统设置与清理")
            }
            .padding(.horizontal, 12)
            .padding(.vertical, 10)
            .background(Color(NSColor.windowBackgroundColor))
        }
    }
}
