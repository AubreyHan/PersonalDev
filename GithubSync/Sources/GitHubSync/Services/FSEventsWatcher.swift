import Foundation
import CoreServices

@MainActor
public protocol FSEventsWatcherDelegate: AnyObject {
    func fileWatcherDidDetectRepoStructureChange(in folderPath: String)
    func fileWatcherDidDetectFileChange(at path: String)
}

public final class FSEventsWatcher: @unchecked Sendable {
    public static let shared = FSEventsWatcher()

    public weak var delegate: FSEventsWatcherDelegate?

    private var eventStream: FSEventStreamRef?
    private var watchedPaths: [String] = []
    private let queue = DispatchQueue(label: "com.aubreyhan.GitHubSync.fsevents", qos: .utility)

    private init() {}

    public func updateWatchedPaths(_ paths: [String]) {
        queue.async {
            self.stopWatching()
            self.watchedPaths = paths
            if !paths.isEmpty {
                self.startWatching()
            }
        }
    }

    private func startWatching() {
        guard !watchedPaths.isEmpty else { return }

        let pathsToWatch = watchedPaths as CFArray
        var context = FSEventStreamContext(
            version: 0,
            info: Unmanaged.passUnretained(self).toOpaque(),
            retain: nil,
            release: nil,
            copyDescription: nil
        )

        let flags: FSEventStreamCreateFlags = UInt32(
            kFSEventStreamCreateFlagUseCFTypes |
            kFSEventStreamCreateFlagFileEvents |
            kFSEventStreamCreateFlagNoDefer
        )

        let callback: FSEventStreamCallback = { (streamRef, clientCallBackInfo, numEvents, eventPaths, eventFlags, eventIds) in
            guard let info = clientCallBackInfo else { return }
            let watcher = Unmanaged<FSEventsWatcher>.fromOpaque(info).takeUnretainedValue()
            let paths = unsafeBitCast(eventPaths, to: NSArray.self) as! [String]
            watcher.handleEvents(paths: paths)
        }

        guard let stream = FSEventStreamCreate(
            kCFAllocatorDefault,
            callback,
            &context,
            pathsToWatch,
            FSEventStreamEventId(kFSEventStreamEventIdSinceNow),
            1.0, // 1秒延迟收集
            flags
        ) else {
            return
        }

        self.eventStream = stream
        FSEventStreamSetDispatchQueue(stream, queue)
        FSEventStreamStart(stream)
    }

    public func stopWatching() {
        if let stream = eventStream {
            FSEventStreamStop(stream)
            FSEventStreamInvalidate(stream)
            FSEventStreamRelease(stream)
            eventStream = nil
        }
    }

    private func handleEvents(paths: [String]) {
        for path in paths {
            // 忽略 .DS_Store、临时文件等
            if path.hasSuffix(".DS_Store") || path.contains("/.Trash/") {
                continue
            }

            // 忽略 .git 内部的锁文件与临时变动，防止死循环
            if path.contains("/.git/") {
                continue
            }

            // 忽略常见的大型依赖生成目录
            if path.contains("/node_modules/") || path.contains("/target/") || path.contains("/.build/") {
                continue
            }

            // 结构性变更：如果刚好是 .git 目录本身的创建/删除
            if path.hasSuffix("/.git") {
                for watched in watchedPaths {
                    if path.hasPrefix(watched) {
                        DispatchQueue.main.async {
                            self.delegate?.fileWatcherDidDetectRepoStructureChange(in: watched)
                        }
                        break
                    }
                }
                continue
            }

            // 普通文件变动通知
            DispatchQueue.main.async {
                self.delegate?.fileWatcherDidDetectFileChange(at: path)
            }
        }
    }

    deinit {
        stopWatching()
    }
}
