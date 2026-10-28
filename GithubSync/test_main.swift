import AppKit

@main
class MyApp: NSObject, NSApplicationDelegate {
    func applicationDidFinishLaunching(_ notification: Notification) {
        print("DID FINISH LAUNCHING!")
        exit(0)
    }
}
