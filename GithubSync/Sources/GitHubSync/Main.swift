import SwiftUI
import AppKit

@main
final class AppDelegate: NSObject, NSApplicationDelegate, NSWindowDelegate {
    private static var sharedDelegate: AppDelegate?

    public static func main() {
        let app = NSApplication.shared
        let delegate = AppDelegate()
        sharedDelegate = delegate
        app.delegate = delegate
        app.setActivationPolicy(.regular)
        app.run()
    }

    private var window: NSWindow?
    private var statusItem: NSStatusItem?

    func applicationDidFinishLaunching(_ notification: Notification) {
        // 1. 初始化系统托盘 (Menu Bar Status Item)
        setupStatusItem()

        // 2. 配置主窗口
        let contentView = MainWindowView()
        let hostingController = NSHostingController(rootView: contentView)

        let window = NSWindow(
            contentRect: NSRect(x: 0, y: 0, width: 920, height: 600),
            styleMask: [.titled, .closable, .miniaturizable, .resizable, .fullSizeContentView],
            backing: .buffered,
            defer: false
        )
        window.center()
        window.title = "GitHub Auto Sync"
        window.titleVisibility = .visible
        window.titlebarAppearsTransparent = false
        window.contentViewController = hostingController
        window.delegate = self
        window.isReleasedWhenClosed = false
        self.window = window

        // 首次启动显示并激活主窗口
        window.makeKeyAndOrderFront(nil)
        NSApp.activate(ignoringOtherApps: true)
    }

    // MARK: - 系统托盘配置 (极简右键菜单与状态栏图标)
    private func setupStatusItem() {
        let item = NSStatusBar.system.statusItem(withLength: NSStatusItem.variableLength)
        self.statusItem = item
        item.isVisible = true

        if let button = item.button {
            button.toolTip = "GitHub Auto Sync"
            button.imagePosition = .imageOnly

            // 优先加载 App 专属浅色图标，回退至原生 SF Symbol
            let iconURL = Bundle.main.bundleURL.appendingPathComponent("Contents/Resources/AppIcon.icns")
            if let customIcon = NSImage(contentsOf: iconURL) {
                customIcon.size = NSSize(width: 18, height: 18)
                button.image = customIcon
            } else if let symbol = NSImage(systemSymbolName: "arrow.triangle.2.circlepath", accessibilityDescription: "GitHub Auto Sync") {
                symbol.size = NSSize(width: 18, height: 18)
                symbol.isTemplate = true
                button.image = symbol
            } else {
                button.title = "Sync"
            }
        }

        let menu = NSMenu()

        let openItem = NSMenuItem(title: "打开主界面", action: #selector(openMainWindow), keyEquivalent: "o")
        openItem.target = self
        menu.addItem(openItem)

        let logItem = NSMenuItem(title: "查看运行日志", action: #selector(openLogViewer), keyEquivalent: "l")
        logItem.target = self
        menu.addItem(logItem)

        menu.addItem(NSMenuItem.separator())

        let quitItem = NSMenuItem(title: "退出应用", action: #selector(quitApp), keyEquivalent: "q")
        quitItem.target = self
        menu.addItem(quitItem)

        item.menu = menu
    }

    @objc @MainActor private func openMainWindow() {
        guard let window = window else { return }
        window.makeKeyAndOrderFront(nil)
        NSApp.activate(ignoringOtherApps: true)
    }

    @objc @MainActor private func openLogViewer() {
        openMainWindow()
        AppState.shared.showLogsModal = true
    }

    @objc private func quitApp() {
        // 优雅停机流水线 (Graceful Shutdown)
        FSEventsWatcher.shared.stopWatching()
        Scheduler.shared.stopAll()
        StorageManager.shared.save()

        NSApp.terminate(nil)
    }

    // MARK: - 窗口生命周期拦截 (点击 X 关闭转为隐藏到托盘)
    func windowShouldClose(_ sender: NSWindow) -> Bool {
        sender.orderOut(nil)
        return false
    }

    func applicationShouldHandleReopen(_ sender: NSApplication, hasVisibleWindows flag: Bool) -> Bool {
        if !flag {
            openMainWindow()
        }
        return true
    }
}
