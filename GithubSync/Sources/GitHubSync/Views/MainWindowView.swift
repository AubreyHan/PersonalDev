import SwiftUI

public struct MainWindowView: View {
    @StateObject private var appState = AppState.shared

    public init() {}

    public var body: some View {
        NavigationSplitView {
            SidebarView(appState: appState)
                .frame(minWidth: 200, idealWidth: 240, maxWidth: 300)
        } detail: {
            RepoListView(appState: appState)
                .frame(minWidth: 500)
        }
        .frame(minWidth: 800, minHeight: 520)
        .sheet(isPresented: $appState.showSettings) {
            SettingsModalView(appState: appState)
        }
        .sheet(isPresented: $appState.showLogsModal) {
            LogViewerView(appState: appState)
        }
        .sheet(isPresented: $appState.showPermissionModal) {
            PermissionModalView(appState: appState)
        }
        .alert(
            "提示",
            isPresented: $appState.showAlert,
            actions: {
                Button("确定", role: .cancel) {}
            },
            message: {
                Text(appState.alertMessage ?? "")
            }
        )
    }
}
