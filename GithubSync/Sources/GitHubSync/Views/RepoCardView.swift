import SwiftUI

public struct RepoCardView: View {
    @ObservedObject var appState: AppState
    public let repo: GitRepo

    private let dateFormatter: DateFormatter = {
        let df = DateFormatter()
        df.dateStyle = .none
        df.timeStyle = .medium
        return df
    }()

    public var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack(alignment: .center) {
                // 图标与仓库名称
                HStack(spacing: 8) {
                    Image(systemName: "shippingbox.fill")
                        .foregroundColor(.accentColor)
                        .font(.system(size: 16))

                    Text(repo.name)
                        .font(.system(size: 14, weight: .bold))
                        .lineLimit(1)
                }

                Spacer()

                // 4色状态徽标
                StatusBadgeView(
                    mode: repo.syncMode,
                    status: repo.status,
                    errorMessage: repo.lastErrorMessage
                )
            }

            // 路径展示
            Text(repo.path)
                .font(.system(size: 11))
                .foregroundColor(.secondary)
                .lineLimit(1)
                .truncationMode(.middle)

            Divider()

            // 底部元信息栏
            HStack(spacing: 12) {
                // 当前分支
                HStack(spacing: 4) {
                    Image(systemName: "arrow.triangle.branch")
                        .font(.system(size: 10))
                    Text(repo.currentBranch)
                        .font(.system(size: 11, weight: .medium))
                }
                .foregroundColor(.secondary)

                // 未提交变更
                if repo.uncommittedChanges > 0 {
                    HStack(spacing: 3) {
                        Circle()
                            .fill(Color.orange)
                            .frame(width: 5, height: 5)
                        Text("\(repo.uncommittedChanges) 项未提交")
                            .font(.system(size: 11))
                            .foregroundColor(.orange)
                    }
                }

                Spacer()

                // 最后同步时间
                HStack(spacing: 4) {
                    Image(systemName: "clock")
                        .font(.system(size: 10))
                    if let time = repo.lastSyncTime {
                        Text("上次同步: \(dateFormatter.string(from: time))")
                    } else {
                        Text("尚未同步")
                    }
                }
                .font(.system(size: 10))
                .foregroundColor(.secondary)
            }

            // 若有错误则展示醒目错误提示栏
            if repo.status == .error, let err = repo.lastErrorMessage {
                HStack(spacing: 4) {
                    Image(systemName: "exclamationmark.triangle.fill")
                        .foregroundColor(.red)
                        .font(.system(size: 11))
                    Text(err)
                        .font(.system(size: 11))
                        .foregroundColor(.red)
                        .lineLimit(2)
                }
                .padding(.horizontal, 8)
                .padding(.vertical, 4)
                .background(Color.red.opacity(0.08))
                .cornerRadius(4)
            }
        }
        .padding(12)
        .background(Color(NSColor.controlBackgroundColor))
        .cornerRadius(8)
        .overlay(
            RoundedRectangle(cornerRadius: 8)
                .stroke(Color(NSColor.separatorColor), lineWidth: 0.8)
        )
        // 右键策略上下文菜单 (Context Menu)
        .contextMenu {
            Section(header: Text("同步策略切换")) {
                Button {
                    appState.setRepoSyncMode(repo: repo, mode: .bidirectionalNewest)
                } label: {
                    HStack {
                        Text("双向同步（新覆盖旧）")
                        if repo.syncMode == .bidirectionalNewest {
                            Image(systemName: "checkmark")
                        }
                    }
                }

                Button {
                    appState.setRepoSyncMode(repo: repo, mode: .localFirst)
                } label: {
                    HStack {
                        Text("本地优先（本地覆盖远端）")
                        if repo.syncMode == .localFirst {
                            Image(systemName: "checkmark")
                        }
                    }
                }

                Button {
                    appState.setRepoSyncMode(repo: repo, mode: .remoteFirst)
                } label: {
                    HStack {
                        Text("远端优先（远端覆盖本地）")
                        if repo.syncMode == .remoteFirst {
                            Image(systemName: "checkmark")
                        }
                    }
                }

                Button {
                    appState.setRepoSyncMode(repo: repo, mode: .stopped)
                } label: {
                    HStack {
                        Text("停止同步")
                        if repo.syncMode == .stopped {
                            Image(systemName: "checkmark")
                        }
                    }
                }
            }

            Divider()

            Button {
                appState.revealInFinder(path: repo.path)
            } label: {
                Label("在访达中显示", systemImage: "folder")
            }

            Button {
                appState.openInTerminal(path: repo.path)
            } label: {
                Label("在终端中打开", systemImage: "terminal")
            }
        }
    }
}
