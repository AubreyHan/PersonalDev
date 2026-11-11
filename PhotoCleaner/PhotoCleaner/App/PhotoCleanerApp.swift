import SwiftUI

@main
struct PhotoCleanerApp: App {
    @NSApplicationDelegateAdaptor(AppDelegate.self) private var appDelegate
    @State private var appViewModel = AppViewModel()

    var body: some Scene {
        WindowGroup("PhotoCleaner") {
            MainSplitView(appViewModel: appViewModel)
                .environment(appViewModel)
                .frame(minWidth: 1080, minHeight: 700)
                .onAppear {
                    wireTerminationHandlers()
                }
        }
        .commands {
            CommandGroup(replacing: .newItem) {}
        }

        Settings {
            PreferencesView(appViewModel: appViewModel)
                .environment(appViewModel)
        }
    }

    private func wireTerminationHandlers() {
        appDelegate.pendingTrashCountProvider = { [weak appViewModel] in
            appViewModel?.trashBasketVM.trashedAssetIDs.count ?? 0
        }
        appDelegate.commitTrashBeforeTerminateHandler = { [weak appViewModel] in
            guard let appViewModel else { return true }
            await appViewModel.trashBasketVM.commitDeletions()
            return appViewModel.trashBasketVM.trashedAssetIDs.isEmpty
        }
    }
}
