import Foundation

public enum FolderValidationError: LocalizedError {
    case pathNotFound
    case alreadyExists
    case isSubdirectory(parentName: String)
    case isParentDirectory(childName: String)

    public var errorDescription: String? {
        switch self {
        case .pathNotFound:
            return "所选路径不存在或无法访问"
        case .alreadyExists:
            return "该文件夹已存在于监控列表中"
        case .isSubdirectory(let parentName):
            return "无法添加：该目录属于已添加文件夹【\(parentName)】的子目录"
        case .isParentDirectory(let childName):
            return "无法添加：该目录包含了已监控的子文件夹【\(childName)】，请先移除子文件夹"
        }
    }
}

public final class DiscoveryEngine: @unchecked Sendable {
    public static let shared = DiscoveryEngine()

    private let ignoredDirectories: Set<String> = [
        "node_modules", "Pods", "target", ".build", ".cache",
        ".Trash", "build", ".gradle", "DerivedData", ".svn", ".hg"
    ]

    private init() {}

    /// 校验待添加文件夹是否合法（防嵌套、防重复）
    public func validateNewFolder(path: String, existingFolders: [WatchedFolder]) throws -> (canonicalPath: String, name: String) {
        let url = URL(fileURLWithPath: path).resolvingSymlinksInPath()
        let canonicalPath = url.path

        var isDir: ObjCBool = false
        guard FileManager.default.fileExists(atPath: canonicalPath, isDirectory: &isDir), isDir.boolValue else {
            throw FolderValidationError.pathNotFound
        }

        let folderName = url.lastPathComponent

        for existing in existingFolders {
            let existCanonical = URL(fileURLWithPath: existing.path).resolvingSymlinksInPath().path

            if canonicalPath == existCanonical {
                throw FolderValidationError.alreadyExists
            }

            // 检查是否是子目录 (以 existing + "/" 开头)
            if canonicalPath.hasPrefix(existCanonical + "/") {
                throw FolderValidationError.isSubdirectory(parentName: existing.name)
            }

            // 检查是否是父目录 (existing 以 canonicalPath + "/" 开头)
            if existCanonical.hasPrefix(canonicalPath + "/") {
                throw FolderValidationError.isParentDirectory(childName: existing.name)
            }
        }

        return (canonicalPath, folderName)
    }

    /// 递归扫描指定目录下的 Git 仓库
    public func scanRepos(in folder: WatchedFolder) async -> [GitRepo] {
        return await withCheckedContinuation { continuation in
            DispatchQueue.global(qos: .userInitiated).async {
                var foundRepos: [GitRepo] = []
                let rootURL = URL(fileURLWithPath: folder.path).resolvingSymlinksInPath()

                // 首先检查根目录自身是否是 Git 仓库
                let rootGitURL = rootURL.appendingPathComponent(".git")
                if FileManager.default.fileExists(atPath: rootGitURL.path) {
                    let repo = self.createGitRepoEntity(at: rootURL, folderId: folder.id)
                    foundRepos.append(repo)
                    continuation.resume(returning: foundRepos)
                    return
                }

                guard let enumerator = FileManager.default.enumerator(
                    at: rootURL,
                    includingPropertiesForKeys: [.isDirectoryKey],
                    options: [.skipsHiddenFiles, .producesRelativePathURLs]
                ) else {
                    continuation.resume(returning: [])
                    return
                }

                while let itemURL = enumerator.nextObject() as? URL {
                    let dirName = itemURL.lastPathComponent

                    // 遇到被忽略的目录时跳过递归其子项
                    if self.ignoredDirectories.contains(dirName) {
                        enumerator.skipDescendants()
                        continue
                    }

                    // 检查此目录是否包含 .git
                    let gitSubURL = itemURL.appendingPathComponent(".git")
                    if FileManager.default.fileExists(atPath: gitSubURL.path) {
                        let repo = self.createGitRepoEntity(at: itemURL, folderId: folder.id)
                        foundRepos.append(repo)
                        // 发现 Git 仓库后，不再深入其内部搜寻嵌套仓库
                        enumerator.skipDescendants()
                    }
                }

                continuation.resume(returning: foundRepos)
            }
        }
    }

    /// 构造 GitRepo 实体并载入持久化配置
    private func createGitRepoEntity(at repoURL: URL, folderId: String) -> GitRepo {
        let canonicalPath = repoURL.resolvingSymlinksInPath().path
        let name = repoURL.lastPathComponent

        var branch = "main"
        let uncommitted = 0

        // 尝试快速提取当前分支
        let headURL = repoURL.appendingPathComponent(".git/HEAD")
        if let headContent = try? String(contentsOf: headURL, encoding: .utf8) {
            let trimmed = headContent.trimmingCharacters(in: .whitespacesAndNewlines)
            if trimmed.hasPrefix("ref: refs/heads/") {
                branch = String(trimmed.dropFirst("ref: refs/heads/".count))
            }
        }

        // 载入历史保存的同步策略
        let savedConfig = StorageManager.shared.getRepoConfig(path: canonicalPath)
        let mode = savedConfig?.syncMode ?? .stopped
        let lastSync = savedConfig?.lastSyncTime

        return GitRepo(
            id: canonicalPath,
            folderId: folderId,
            name: name,
            path: canonicalPath,
            currentBranch: branch,
            syncMode: mode,
            status: .idle,
            lastSyncTime: lastSync,
            lastErrorMessage: nil,
            uncommittedChanges: uncommitted
        )
    }
}
