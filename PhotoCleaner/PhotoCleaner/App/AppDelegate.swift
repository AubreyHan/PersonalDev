import AppKit
import Foundation

@MainActor
public final class AppDelegate: NSObject, NSApplicationDelegate {
    /// 提供当前待删暂存篮内项目数量（若未绑定则回退读取 LocalStateStore）
    public var pendingTrashCountProvider: (@MainActor () -> Int)?

    /// 退出前用户选择“立即提交清理并退出”时执行的异步提交闭包（返回 true 则继续退出，false 则取消退出）
    public var commitTrashBeforeTerminateHandler: (@MainActor () async -> Bool)?

    /// FR-8.1: 纯按需桌面窗口形态，关闭主窗口即彻底终止进程，零后台常驻
    public func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool {
        true
    }

    /// FR-7.4: 当待删暂存篮非空且用户关闭窗口或退出应用时，弹出三选一提示
    public func applicationShouldTerminate(_ sender: NSApplication) -> NSApplication.TerminateReply {
        // 单元测试宿主环境下直接退出，不阻塞自动化测试
        if ProcessInfo.processInfo.environment["XCTestConfigurationFilePath"] != nil {
            return .terminateNow
        }

        let pendingCount = pendingTrashCountProvider?()
            ?? LocalStateStore.shared.load().trashBasketAssetIDs.count

        guard pendingCount > 0 else {
            return .terminateNow
        }

        let alert = NSAlert()
        alert.alertStyle = .warning
        alert.messageText = "待删暂存篮中尚有 \(pendingCount) 项未提交清理"
        alert.informativeText = "您可以立即提交清理并移入系统“最近删除”，或保留当前暂存篮以便下次启动继续整理。"
        alert.addButton(withTitle: "立即提交清理并退出")
        alert.addButton(withTitle: "保留暂存篮，下次继续")
        alert.addButton(withTitle: "取消")

        let response = alert.runModal()
        switch response {
        case .alertFirstButtonReturn:
            if let commitHandler = commitTrashBeforeTerminateHandler {
                Task { @MainActor in
                    let didCommit = await commitHandler()
                    sender.reply(toApplicationShouldTerminate: didCommit)
                }
                return .terminateLater
            }
            return .terminateNow

        case .alertSecondButtonReturn:
            return .terminateNow

        default:
            return .terminateCancel
        }
    }
}
