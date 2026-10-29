import SwiftUI

public struct RepoListView: View {
    @ObservedObject var appState: AppState

    public var body: some View {
        VStack(spacing: 0) {
            // 顶部搜索与统计条
            HStack(spacing: 10) {
                HStack {
                    Image(systemName: "magnifyingglass")
                        .foregroundColor(.secondary)
                    TextField("搜索仓库名称、分支或路径...", text: $appState.searchText)
                        .textFieldStyle(.plain)
                    if !appState.searchText.isEmpty {
                        Button {
                            appState.searchText = ""
                        } label: {
                            Image(systemName: "xmark.circle.fill")
                                .foregroundColor(.secondary)
                        }
                        .buttonStyle(.plain)
                    }
                }
                .padding(7)
                .background(Color(NSColor.controlBackgroundColor))
                .cornerRadius(6)
                .overlay(
                    RoundedRectangle(cornerRadius: 6)
                        .stroke(Color(NSColor.separatorColor), lineWidth: 0.8)
                )

                Spacer()

                Text("共 \(appState.filteredRepos.count) 个仓库")
                    .font(.system(size: 11))
                    .foregroundColor(.secondary)
            }
            .padding(.horizontal, 16)
            .padding(.vertical, 12)
            .background(Color(NSColor.windowBackgroundColor))

            Divider()

            // 仓库卡片滚动列表
            if appState.filteredRepos.isEmpty {
                VStack(spacing: 12) {
                    Spacer()
                    Image(systemName: "folder.badge.questionmark")
                        .font(.system(size: 40))
                        .foregroundColor(.secondary.opacity(0.6))

                    if appState.watchedFolders.isEmpty {
                        Text("尚未添加任何监控文件夹")
                            .font(.system(size: 14, weight: .medium))
                            .foregroundColor(.secondary)
                        Text("点击左下角【+ 添加文件夹】开始自动扫描 Git 仓库")
                            .font(.system(size: 12))
                            .foregroundColor(.secondary.opacity(0.8))
                        Button("添加文件夹") {
                            appState.promptAddFolder()
                        }
                        .buttonStyle(.borderedProminent)
                        .controlSize(.regular)
                        .padding(.top, 4)
                    } else if !appState.searchText.isEmpty {
                        Text("未找到与“\(appState.searchText)”匹配的仓库")
                            .font(.system(size: 13))
                            .foregroundColor(.secondary)
                    } else {
                        Text("当前文件夹下未发现 Git 仓库")
                            .font(.system(size: 13))
                            .foregroundColor(.secondary)
                    }
                    Spacer()
                }
                .frame(maxWidth: .infinity, maxHeight: .infinity)
            } else {
                ScrollView {
                    LazyVStack(spacing: 10) {
                        ForEach(appState.filteredRepos) { repo in
                            RepoCardView(appState: appState, repo: repo)
                        }
                    }
                    .padding(16)
                }
            }
        }
        .background(Color(NSColor.controlBackgroundColor).opacity(0.5))
    }
}
