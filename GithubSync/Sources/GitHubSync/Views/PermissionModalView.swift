import SwiftUI

public struct PermissionModalView: View {
    @ObservedObject var appState: AppState

    public var body: some View {
        VStack(spacing: 20) {
            Image(systemName: "lock.shield.fill")
                .font(.system(size: 46))
                .foregroundColor(.accentColor)

            Text("需要磁盘访问权限")
                .font(.system(size: 18, weight: .bold))

            Text("GitHub Auto Sync 需要读取与写入您所选的代码目录，以便在后台实时监听文件变动并自动执行 Git 提交与同步。")
                .font(.system(size: 13))
                .foregroundColor(.secondary)
                .multilineTextAlignment(.center)
                .padding(.horizontal, 16)

            VStack(spacing: 10) {
                Button {
                    PermissionService.shared.openPrivacySettings()
                } label: {
                    HStack {
                        Image(systemName: "gearshape")
                        Text("前往 macOS 系统设置授权")
                    }
                    .frame(maxWidth: .infinity)
                }
                .buttonStyle(.bordered)
                .controlSize(.large)

                Button {
                    appState.completeFirstRun()
                } label: {
                    Text("我已完成授权")
                        .frame(maxWidth: .infinity)
                }
                .buttonStyle(.borderedProminent)
                .controlSize(.large)
            }
            .padding(.horizontal, 20)
        }
        .padding(24)
        .frame(width: 380, height: 320)
    }
}
