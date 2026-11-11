import Foundation
import Observation
import AppKit

public enum ActivitySortField: String, CaseIterable, Sendable {
    case name
    case dateTime
    case type
    case distance
    case duration
}

@MainActor
@Observable
public final class MainViewModel {
    // MARK: - Constants

    /// 日历控件允许选择的最早起始日期：2000-01-01 00:00:00（本地日历）
    nonisolated public static var earliestAllowedDate: Date {
        var components = DateComponents()
        components.year = 2000
        components.month = 1
        components.day = 1
        components.hour = 0
        components.minute = 0
        components.second = 0
        return Calendar.current.date(from: components) ?? Date(timeIntervalSince1970: 946684800)
    }

    nonisolated public static let defaultSaveDirectoryDefaultsKey = "GarminActivity.saveDirectoryPath"
    nonisolated public static let lastKnownRegionDefaultsKey = "GarminActivity.lastKnownRegion"

    // MARK: - Dependencies

    private let keychainService: any KeychainStoring
    private let authService: any AuthServicing
    private let apiClient: any GarminApiClientProtocol
    private let exporter: any ActivityExporting
    private let userDefaults: UserDefaults
    private let saveDirectoryDefaultsKey: String
    private let urlOpener: @MainActor (URL) -> Void

    // MARK: - Auth & Region State

    public var apiKeyInput: String = ""
    public var isAuthenticated: Bool = false
    public var currentRegion: GarminRegion? = nil
    public var lastKnownRegion: GarminRegion? = nil
    public var isAuthenticating: Bool = false
    public var authErrorMessage: String? = nil

    private var activeApiKey: String? = nil

    // MARK: - Date Filter State

    private var isAdjustingDateBounds = false

    /// 每次打开软件（初始化 MainViewModel）时，起始日期与结束日期默认固定设为当天 00:00:00（绝不持久化上一次的日期）
    public var startDate: Date {
        didSet {
            guard startDate != oldValue, !isAdjustingDateBounds else { return }
            if startDate > endDate {
                isAdjustingDateBounds = true
                endDate = startDate
                isAdjustingDateBounds = false
            }
            guard isAuthenticated else { return }
            Task { [weak self] in
                await self?.loadActivities()
            }
        }
    }

    public var endDate: Date {
        didSet {
            guard endDate != oldValue, !isAdjustingDateBounds else { return }
            if endDate < startDate {
                isAdjustingDateBounds = true
                startDate = endDate
                isAdjustingDateBounds = false
            }
            guard isAuthenticated else { return }
            Task { [weak self] in
                await self?.loadActivities()
            }
        }
    }

    // MARK: - Activity List & Dynamic Type Filter State

    public var allActivities: [ActivityItem] = [] {
        didSet {
            selectedTypes = Set(availableTypes)
            pruneHiddenSelections()
        }
    }

    /// 只包含当前 allActivities 中实际存在的唯一 typeKey 列表（按首次出现顺序）
    public var availableTypes: [String] {
        var seen = Set<String>()
        var result: [String] = []
        for activity in allActivities {
            let key = activity.typeKey
            if !seen.contains(key) {
                seen.insert(key)
                result.append(key)
            }
        }
        return result
    }

    public var selectedTypes: Set<String> = [] {
        didSet {
            pruneHiddenSelections()
        }
    }

    public var sortField: ActivitySortField = .dateTime
    public var sortAscending: Bool = false

    public var filteredActivities: [ActivityItem] {
        let filtered = allActivities.filter { selectedTypes.contains($0.typeKey) }
        return filtered.sorted { lhs, rhs in
            let orderedAsc: Bool
            switch sortField {
            case .name:
                let cmp = lhs.displayName.localizedStandardCompare(rhs.displayName)
                if cmp == .orderedSame {
                    orderedAsc = lhs.activityId < rhs.activityId
                } else {
                    orderedAsc = (cmp == .orderedAscending)
                }
            case .dateTime:
                if lhs.startTimeLocal == rhs.startTimeLocal {
                    orderedAsc = lhs.activityId < rhs.activityId
                } else {
                    orderedAsc = lhs.startTimeLocal < rhs.startTimeLocal
                }
            case .type:
                let cmp = lhs.typeKey.localizedStandardCompare(rhs.typeKey)
                if cmp == .orderedSame {
                    orderedAsc = lhs.startTimeLocal < rhs.startTimeLocal
                } else {
                    orderedAsc = (cmp == .orderedAscending)
                }
            case .distance:
                let lDist = lhs.distance ?? -1
                let rDist = rhs.distance ?? -1
                if lDist == rDist {
                    orderedAsc = lhs.startTimeLocal < rhs.startTimeLocal
                } else {
                    orderedAsc = lDist < rDist
                }
            case .duration:
                let lDur = lhs.duration ?? -1
                let rDur = rhs.duration ?? -1
                if lDur == rDur {
                    orderedAsc = lhs.startTimeLocal < rhs.startTimeLocal
                } else {
                    orderedAsc = lDur < rDur
                }
            }
            return sortAscending ? orderedAsc : !orderedAsc
        }
    }

    public func toggleSort(by field: ActivitySortField) {
        if sortField == field {
            sortAscending.toggle()
        } else {
            sortField = field
            switch field {
            case .name, .type:
                sortAscending = true
            case .dateTime, .distance, .duration:
                sortAscending = false
            }
        }
    }

    public var isLoadingActivities: Bool = false
    public var errorMessage: String? = nil

    // MARK: - Selection & Same-Type Merge State

    public var selectedActivityIDs: Set<Int64> = [] {
        didSet {
            syncMergeState()
        }
    }

    public var selectedActivities: [ActivityItem] {
        filteredActivities.filter { selectedActivityIDs.contains($0.activityId) }
    }

    public var canMerge: Bool {
        guard selectedActivities.count >= 2 else { return false }
        let uniqueTypes = Set(selectedActivities.map { $0.typeKey })
        return uniqueTypes.count == 1
    }

    public var isMergeEnabled: Bool = false {
        didSet {
            if isMergeEnabled && !canMerge {
                isMergeEnabled = false
            }
        }
    }

    // MARK: - Export & Missing GPS Alert State

    public var saveDirectoryURL: URL {
        didSet {
            userDefaults.set(saveDirectoryURL.path, forKey: saveDirectoryDefaultsKey)
        }
    }

    public var selectedExportFormats: Set<ExportFormat> = [.gpx]

    public var orderedSelectedExportFormats: [ExportFormat] {
        ExportFormat.allCases.filter { selectedExportFormats.contains($0) }
    }

    public var exportFormat: ExportFormat {
        get { orderedSelectedExportFormats.first ?? .gpx }
        set { selectedExportFormats = [newValue] }
    }

    public var isDownloading: Bool = false
    public var statusMessage: String? = nil
    public var lastExportedURLs: [URL] = []

    public var showMissingGPSAlert: Bool = false
    public var missingGPSActivityNames: [String] = []

    // MARK: - Initialization

    public init(
        keychainService: any KeychainStoring = KeychainService.shared,
        authService: any AuthServicing = PersonalApiAuthService(),
        apiClient: any GarminApiClientProtocol = GarminApiClient(),
        exporter: any ActivityExporting = ActivityExporter(),
        userDefaults: UserDefaults = .standard,
        saveDirectoryDefaultsKey: String = MainViewModel.defaultSaveDirectoryDefaultsKey,
        urlOpener: (@MainActor (URL) -> Void)? = nil
    ) {
        self.keychainService = keychainService
        self.authService = authService
        self.apiClient = apiClient
        self.exporter = exporter
        self.userDefaults = userDefaults
        self.saveDirectoryDefaultsKey = saveDirectoryDefaultsKey
        self.urlOpener = urlOpener ?? { url in
            // 仅在非 XCTest 环境下自动调用系统浏览器打开，避免单元测试触发外部浏览器
            if ProcessInfo.processInfo.environment["XCTestConfigurationFilePath"] == nil {
                NSWorkspace.shared.open(url)
            }
        }

        if let rawRegion = userDefaults.string(forKey: MainViewModel.lastKnownRegionDefaultsKey),
           let region = GarminRegion(rawValue: rawRegion) {
            self.lastKnownRegion = region
        }

        // 每次冷启动固定设为当天 00:00:00
        let today = Calendar.current.startOfDay(for: Date())
        self.startDate = today
        self.endDate = today

        let defaultDownloads = FileManager.default.urls(for: .downloadsDirectory, in: .userDomainMask).first
            ?? URL(fileURLWithPath: NSHomeDirectory()).appendingPathComponent("Downloads")

        if let savedPath = userDefaults.string(forKey: saveDirectoryDefaultsKey),
           !savedPath.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty {
            self.saveDirectoryURL = URL(fileURLWithPath: savedPath)
        } else {
            self.saveDirectoryURL = defaultDownloads
        }
    }

    // MARK: - Authentication Actions

    /// 当前优先打开的获取 Personal API Key 页面（若上次登录过中国区则打开 .cn，否则默认打开国际区 .com）
    public var preferredTokenPageURL: URL {
        (currentRegion ?? lastKnownRegion ?? .global).personalApiTokenPageURL
    }

    /// 在浏览器中打开获取 Personal API Key 的页面
    public func openPersonalApiKeyPage(for region: GarminRegion? = nil) {
        let targetURL = region?.personalApiTokenPageURL ?? preferredTokenPageURL
        urlOpener(targetURL)
    }

    /// 启动时检查 Keychain，若有保存的 apiKey 和 region，静默获取有效 Token 并加载当天活动列表；若需要输入 Key 则自动打开获取 Key 页面
    public func bootstrap() async {
        guard let stored = try? keychainService.loadCredentials(),
              !stored.apiKey.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty else {
            openPersonalApiKeyPage()
            return
        }

        isAuthenticating = true
        authErrorMessage = nil
        rememberRegion(stored.region)

        do {
            let session = try await authService.obtainValidToken(
                apiKey: stored.apiKey,
                region: stored.region,
                forceRefresh: false
            )
            activeApiKey = session.apiKey
            apiKeyInput = session.apiKey
            currentRegion = session.region
            rememberRegion(session.region)
            isAuthenticated = true
            isAuthenticating = false

            await loadActivities()
        } catch {
            isAuthenticating = false
            isAuthenticated = false
            currentRegion = nil
            authErrorMessage = error.localizedDescription
            openPersonalApiKeyPage(for: stored.region)
        }
    }

    /// 调用双区自动探测接口登录，成功后写入 Keychain 并加载当天活动列表
    public func login() async {
        let trimmedKey = apiKeyInput.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmedKey.isEmpty else {
            authErrorMessage = AuthServiceError.emptyApiKey.localizedDescription
            return
        }

        isAuthenticating = true
        authErrorMessage = nil

        do {
            let session = try await authService.authenticateAndDetectRegion(apiKey: trimmedKey)
            try? keychainService.saveCredentials(apiKey: session.apiKey, region: session.region)
            activeApiKey = session.apiKey
            currentRegion = session.region
            rememberRegion(session.region)
            isAuthenticated = true
            isAuthenticating = false

            await loadActivities()
        } catch {
            isAuthenticating = false
            isAuthenticated = false
            currentRegion = nil
            authErrorMessage = error.localizedDescription
        }
    }

    /// 清除 Keychain 凭据，重置状态回到登录页，并自动打开获取 Key 的页面
    public func logout() {
        let previousRegion = currentRegion ?? lastKnownRegion
        try? keychainService.clearCredentials()
        activeApiKey = nil
        apiKeyInput = ""
        isAuthenticated = false
        currentRegion = nil
        isAuthenticating = false
        authErrorMessage = nil
        allActivities = []
        selectedTypes = []
        selectedActivityIDs = []
        isMergeEnabled = false
        errorMessage = nil
        statusMessage = nil
        lastExportedURLs = []
        showMissingGPSAlert = false
        missingGPSActivityNames = []
        openPersonalApiKeyPage(for: previousRegion)
    }

    private func rememberRegion(_ region: GarminRegion) {
        lastKnownRegion = region
        userDefaults.set(region.rawValue, forKey: MainViewModel.lastKnownRegionDefaultsKey)
    }

    // MARK: - Activity Loading & Filtering

    /// 拉取从 startDate 至今的所有活动
    public func loadActivities() async {
        guard let region = currentRegion else { return }
        let apiKey = resolveActiveApiKey()

        isLoadingActivities = true
        errorMessage = nil
        statusMessage = nil

        do {
            let session = try await authService.obtainValidToken(
                apiKey: apiKey,
                region: region,
                forceRefresh: false
            )
            let fetched: [ActivityItem]
            do {
                fetched = try await apiClient.fetchActivities(
                    from: startDate,
                    to: endDate,
                    region: region,
                    accessToken: session.accessToken
                )
            } catch GarminApiError.unauthorized {
                let refreshed = try await authService.obtainValidToken(
                    apiKey: apiKey,
                    region: region,
                    forceRefresh: true
                )
                fetched = try await apiClient.fetchActivities(
                    from: startDate,
                    to: endDate,
                    region: region,
                    accessToken: refreshed.accessToken
                )
            }

            allActivities = fetched
            isLoadingActivities = false
        } catch {
            isLoadingActivities = false
            errorMessage = error.localizedDescription
        }
    }

    public func toggleType(_ typeKey: String) {
        if selectedTypes.contains(typeKey) {
            selectedTypes.remove(typeKey)
        } else {
            selectedTypes.insert(typeKey)
        }
    }

    public func selectAllTypes() {
        selectedTypes = Set(availableTypes)
    }

    public func deselectAllTypes() {
        selectedTypes.removeAll()
    }

    // MARK: - Export Format Multi-Select

    public func toggleExportFormat(_ format: ExportFormat) {
        if selectedExportFormats.contains(format) {
            selectedExportFormats.remove(format)
        } else {
            selectedExportFormats.insert(format)
        }
    }

    public func selectAllExportFormats() {
        selectedExportFormats = Set(ExportFormat.allCases)
    }

    public func deselectAllExportFormats() {
        selectedExportFormats.removeAll()
    }

    // MARK: - Activity Selection

    public func isActivitySelected(_ activityId: Int64) -> Bool {
        selectedActivityIDs.contains(activityId)
    }

    public func toggleActivitySelection(_ activityId: Int64) {
        if selectedActivityIDs.contains(activityId) {
            selectedActivityIDs.remove(activityId)
        } else {
            selectedActivityIDs.insert(activityId)
        }
    }

    public func setActivitySelection(_ activityId: Int64, isSelected: Bool) {
        if isSelected {
            selectedActivityIDs.insert(activityId)
        } else {
            selectedActivityIDs.remove(activityId)
        }
    }

    public var areAllFilteredSelected: Bool {
        let visible = filteredActivities
        guard !visible.isEmpty else { return false }
        return visible.allSatisfy { selectedActivityIDs.contains($0.activityId) }
    }

    public func selectAllFilteredActivities() {
        for activity in filteredActivities {
            selectedActivityIDs.insert(activity.activityId)
        }
    }

    public func deselectAllFilteredActivities() {
        let visibleIDs = Set(filteredActivities.map(\.activityId))
        selectedActivityIDs.subtract(visibleIDs)
    }

    public func toggleSelectAllFiltered() {
        if areAllFilteredSelected {
            deselectAllFilteredActivities()
        } else {
            selectAllFilteredActivities()
        }
    }

    // MARK: - Directory Selection & Download

    public func chooseSaveDirectory() {
        let panel = NSOpenPanel()
        panel.title = "选择导出文件保存目录"
        panel.canChooseFiles = false
        panel.canChooseDirectories = true
        panel.canCreateDirectories = true
        panel.allowsMultipleSelection = false
        panel.directoryURL = saveDirectoryURL

        if panel.runModal() == .OK, let selectedURL = panel.url {
            saveDirectoryURL = selectedURL
        }
    }

    /// 下载当前勾选项并导出为选定格式（支持多格式同时导出与同类型多选合并）
    public func downloadSelected() async {
        let itemsToDownload = selectedActivities
        guard !itemsToDownload.isEmpty else {
            errorMessage = ExportError.noActivitiesSelected.localizedDescription
            return
        }

        let formatsToExport = orderedSelectedExportFormats
        guard !formatsToExport.isEmpty else {
            errorMessage = "请至少选择一种下载类型"
            return
        }

        let region = currentRegion ?? .global
        let apiKey = resolveActiveApiKey()

        isDownloading = true
        errorMessage = nil
        statusMessage = nil
        showMissingGPSAlert = false
        missingGPSActivityNames = []

        do {
            var session = try await authService.obtainValidToken(
                apiKey: apiKey,
                region: region,
                forceRefresh: false
            )

            var parsedPairs: [(activity: ActivityItem, parsed: ParsedActivityData)] = []
            parsedPairs.reserveCapacity(itemsToDownload.count)

            for activity in itemsToDownload {
                let zipData: Data
                do {
                    zipData = try await apiClient.downloadActivityZip(
                        activityId: activity.activityId,
                        region: region,
                        accessToken: session.accessToken
                    )
                } catch GarminApiError.unauthorized {
                    session = try await authService.obtainValidToken(
                        apiKey: apiKey,
                        region: region,
                        forceRefresh: true
                    )
                    zipData = try await apiClient.downloadActivityZip(
                        activityId: activity.activityId,
                        region: region,
                        accessToken: session.accessToken
                    )
                }

                let fitData = try ZipExtractor.extractFitData(from: zipData)
                let parsed = try FitParser.parse(fitData: fitData, activity: activity)
                parsedPairs.append((activity: activity, parsed: parsed))
            }

            if formatsToExport.contains(.gpx) {
                let missingNames = parsedPairs
                    .filter { !$0.parsed.hasValidGPSCoordinates }
                    .map { $0.activity.displayName }
                if !missingNames.isEmpty {
                    throw ExportError.missingGPSForGPX(activityNames: missingNames)
                }
            }

            var exportedURLs: [URL] = []

            if canMerge && isMergeEnabled {
                for format in formatsToExport {
                    let mergedURL = try exporter.exportMerged(
                        items: parsedPairs,
                        format: format,
                        to: saveDirectoryURL
                    )
                    exportedURLs.append(mergedURL)
                }
                lastExportedURLs = exportedURLs
                if exportedURLs.count == 1 {
                    statusMessage = "已合并导出 1 个文件：\(exportedURLs[0].lastPathComponent)"
                } else {
                    let names = exportedURLs.map(\.lastPathComponent).joined(separator: "、")
                    statusMessage = "已合并导出 \(exportedURLs.count) 个文件：\(names)"
                }
            } else {
                do {
                    for pair in parsedPairs {
                        for format in formatsToExport {
                            let fileURL = try exporter.exportSingle(
                                parsed: pair.parsed,
                                activity: pair.activity,
                                format: format,
                                to: saveDirectoryURL
                            )
                            exportedURLs.append(fileURL)
                        }
                    }
                } catch let error as ExportError {
                    if case .missingGPSForGPX(let thrownNames) = error {
                        for url in exportedURLs {
                            try? FileManager.default.removeItem(at: url)
                        }
                        let allMissingNames = parsedPairs
                            .filter { !$0.parsed.hasValidGPSCoordinates }
                            .map { $0.activity.displayName }
                        let names = allMissingNames.isEmpty ? thrownNames : allMissingNames
                        throw ExportError.missingGPSForGPX(activityNames: names)
                    }
                    throw error
                }
                lastExportedURLs = exportedURLs
                statusMessage = "成功导出 \(exportedURLs.count) 个文件至 \(saveDirectoryURL.lastPathComponent)"
            }

            isDownloading = false
        } catch let error as ExportError {
            isDownloading = false
            if case .missingGPSForGPX(let activityNames) = error {
                missingGPSActivityNames = activityNames
                showMissingGPSAlert = true
            } else {
                errorMessage = error.localizedDescription
            }
        } catch {
            isDownloading = false
            errorMessage = error.localizedDescription
        }
    }

    /// 当 GPX 遇到无 GPS 活动时，一键将 GPX 替换为 TCX 或 FIT 格式重试下载
    public func retryDownloadWithFormat(_ format: ExportFormat) async {
        selectedExportFormats.remove(.gpx)
        selectedExportFormats.insert(format)
        showMissingGPSAlert = false
        missingGPSActivityNames = []
        await downloadSelected()
    }

    // MARK: - Internal Helpers

    private func resolveActiveApiKey() -> String {
        if let activeApiKey, !activeApiKey.isEmpty {
            return activeApiKey
        }
        if let stored = try? keychainService.loadCredentials(), !stored.apiKey.isEmpty {
            return stored.apiKey
        }
        return apiKeyInput.trimmingCharacters(in: .whitespacesAndNewlines)
    }

    private func pruneHiddenSelections() {
        let visibleIDs = Set(filteredActivities.map(\.activityId))
        let pruned = selectedActivityIDs.intersection(visibleIDs)
        if pruned != selectedActivityIDs {
            selectedActivityIDs = pruned
        } else {
            syncMergeState()
        }
    }

    private func syncMergeState() {
        if !canMerge && isMergeEnabled {
            isMergeEnabled = false
        }
    }
}
