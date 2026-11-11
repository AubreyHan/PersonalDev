import XCTest
@testable import GarminActivity

// MARK: - Test Doubles

private final class MockKeychainStore: KeychainStoring, @unchecked Sendable {
    var stored: StoredCredentials?
    var saveCallCount = 0
    var clearCallCount = 0

    func saveCredentials(apiKey: String, region: GarminRegion) throws {
        saveCallCount += 1
        stored = StoredCredentials(apiKey: apiKey, region: region)
    }

    func loadCredentials() throws -> StoredCredentials? {
        stored
    }

    func clearCredentials() throws {
        clearCallCount += 1
        stored = nil
    }
}

private final class MockAuthService: AuthServicing, @unchecked Sendable {
    var detectedRegion: GarminRegion = .china
    var tokenToReturn: String = "mock-jwt-token"
    var shouldFailAuth: Bool = false

    func authenticateAndDetectRegion(apiKey: String) async throws -> AuthSession {
        if shouldFailAuth {
            throw AuthServiceError.invalidApiKey
        }
        return AuthSession(
            apiKey: apiKey,
            region: detectedRegion,
            accessToken: tokenToReturn,
            expiresAt: Date().addingTimeInterval(86400)
        )
    }

    func obtainValidToken(apiKey: String, region: GarminRegion, forceRefresh: Bool) async throws -> AuthSession {
        if shouldFailAuth {
            throw AuthServiceError.invalidApiKey
        }
        return AuthSession(
            apiKey: apiKey,
            region: region,
            accessToken: tokenToReturn,
            expiresAt: Date().addingTimeInterval(86400)
        )
    }
}

private final class MockGarminApiClient: GarminApiClientProtocol, @unchecked Sendable {
    var activitiesToReturn: [ActivityItem] = []
    var zipDataByActivityId: [Int64: Data] = [:]
    var defaultZipData: Data = Data()
    var fetchedStartDates: [Date] = []

    func fetchActivities(from startDate: Date, region: GarminRegion, accessToken: String) async throws -> [ActivityItem] {
        fetchedStartDates.append(startDate)
        return activitiesToReturn
    }

    func downloadActivityZip(activityId: Int64, region: GarminRegion, accessToken: String) async throws -> Data {
        if let custom = zipDataByActivityId[activityId] {
            return custom
        }
        return defaultZipData
    }
}

private final class SpyActivityExporter: ActivityExporting, @unchecked Sendable {
    let underlying = ActivityExporter()
    var exportSingleCallCount = 0
    var exportMergedCallCount = 0
    var lastExportedFormat: ExportFormat?

    func exportSingle(
        parsed: ParsedActivityData,
        activity: ActivityItem,
        format: ExportFormat,
        to directoryURL: URL
    ) throws -> URL {
        exportSingleCallCount += 1
        lastExportedFormat = format
        return try underlying.exportSingle(parsed: parsed, activity: activity, format: format, to: directoryURL)
    }

    func exportMerged(
        items: [(activity: ActivityItem, parsed: ParsedActivityData)],
        format: ExportFormat,
        to directoryURL: URL
    ) throws -> URL {
        exportMergedCallCount += 1
        lastExportedFormat = format
        return try underlying.exportMerged(items: items, format: format, to: directoryURL)
    }

    func sanitizeFileNameComponent(_ name: String) -> String {
        underlying.sanitizeFileNameComponent(name)
    }
}

// MARK: - MainViewModelTests

@MainActor
final class MainViewModelTests: XCTestCase {

    // MARK: 1. 起始日期默认当天 & 最早可选日期 2000-01-01

    func testStartDateDefaultsToTodayOnEveryNewInstanceAndEarliestDateIs2000() {
        let keychain = MockKeychainStore()
        let auth = MockAuthService()
        let api = MockGarminApiClient()
        let exporter = SpyActivityExporter()

        let expectedToday = Calendar.current.startOfDay(for: Date())

        let vm1 = MainViewModel(
            keychainService: keychain,
            authService: auth,
            apiClient: api,
            exporter: exporter
        )
        XCTAssertEqual(vm1.startDate, expectedToday)

        // 修改第一个实例的日期为过去某天
        let pastDate = Calendar.current.date(byAdding: .day, value: -30, to: expectedToday)!
        vm1.startDate = pastDate
        XCTAssertEqual(vm1.startDate, pastDate)

        // 重新新建一个实例（模拟下次打开软件），startDate 必须仍然默认是当天
        let vm2 = MainViewModel(
            keychainService: keychain,
            authService: auth,
            apiClient: api,
            exporter: exporter
        )
        XCTAssertEqual(vm2.startDate, expectedToday)

        // 验证最早可选日期为 2000-01-01
        let components = Calendar.current.dateComponents([.year, .month, .day], from: MainViewModel.earliestAllowedDate)
        XCTAssertEqual(components.year, 2000)
        XCTAssertEqual(components.month, 1)
        XCTAssertEqual(components.day, 1)
    }

    // MARK: 2. 动态类型提取与多选过滤逻辑

    func testDynamicAvailableTypesAndFilteringPrunesHiddenSelection() async {
        let keychain = MockKeychainStore()
        let auth = MockAuthService()
        let api = MockGarminApiClient()
        let exporter = SpyActivityExporter()

        let run1 = ActivityItem(
            activityId: 101,
            activityName: "晨跑",
            startTimeLocal: "2026-11-10 07:00:00",
            startTimeGMT: "2026-11-09 23:00:00",
            activityType: ActivityTypeDTO(typeKey: "running"),
            distance: 5000,
            duration: 1500
        )
        let cycle1 = ActivityItem(
            activityId: 102,
            activityName: "夜骑",
            startTimeLocal: "2026-11-10 20:00:00",
            startTimeGMT: "2026-11-10 12:00:00",
            activityType: ActivityTypeDTO(typeKey: "cycling"),
            distance: 20000,
            duration: 3600
        )
        let run2 = ActivityItem(
            activityId: 103,
            activityName: "间歇跑",
            startTimeLocal: "2026-11-11 07:00:00",
            startTimeGMT: "2026-11-10 23:00:00",
            activityType: ActivityTypeDTO(typeKey: "running"),
            distance: 8000,
            duration: 2400
        )
        api.activitiesToReturn = [run1, cycle1, run2]

        let vm = MainViewModel(
            keychainService: keychain,
            authService: auth,
            apiClient: api,
            exporter: exporter
        )
        vm.apiKeyInput = "test-cn-key"
        await vm.login()

        XCTAssertTrue(vm.isAuthenticated)
        XCTAssertEqual(vm.currentRegion, .china)

        // availableTypes 只包含当前活动实际存在的唯一类型（running, cycling），不含 swimming 等不存在类型
        XCTAssertEqual(vm.availableTypes, ["running", "cycling"])
        XCTAssertEqual(vm.selectedTypes, Set(["running", "cycling"]))
        XCTAssertEqual(vm.filteredActivities.count, 3)

        // 勾选全部 3 个活动
        vm.selectAllFilteredActivities()
        XCTAssertEqual(vm.selectedActivityIDs, Set([101, 102, 103]))

        // 取消勾选 cycling 类型 -> 列表仅剩 2 条 running，且 cycling(102) 自动从已勾选集合中移除
        vm.toggleType("cycling")
        XCTAssertEqual(vm.filteredActivities.map(\.activityId), [101, 103])
        XCTAssertEqual(vm.selectedActivityIDs, Set([101, 103]))

        // 取消全选类型 -> 列表为空，勾选项自动清空
        vm.deselectAllTypes()
        XCTAssertTrue(vm.filteredActivities.isEmpty)
        XCTAssertTrue(vm.selectedActivityIDs.isEmpty)

        // 全选类型恢复
        vm.selectAllTypes()
        XCTAssertEqual(vm.filteredActivities.count, 3)
    }

    // MARK: 3. 同类型合并条件判定与 isMergeEnabled 自动复位

    func testCanMergeAndIsMergeEnabledAutoReset() {
        let vm = MainViewModel(
            keychainService: MockKeychainStore(),
            authService: MockAuthService(),
            apiClient: MockGarminApiClient(),
            exporter: SpyActivityExporter()
        )

        let run1 = ActivityItem(
            activityId: 1,
            activityName: "Run 1",
            startTimeLocal: "2026-11-01 08:00:00",
            activityType: ActivityTypeDTO(typeKey: "running")
        )
        let run2 = ActivityItem(
            activityId: 2,
            activityName: "Run 2",
            startTimeLocal: "2026-11-02 08:00:00",
            activityType: ActivityTypeDTO(typeKey: "running")
        )
        let cycle1 = ActivityItem(
            activityId: 3,
            activityName: "Cycle 1",
            startTimeLocal: "2026-11-03 08:00:00",
            activityType: ActivityTypeDTO(typeKey: "cycling")
        )

        vm.allActivities = [run1, run2, cycle1]

        // 1) 仅勾选 1 个活动 -> canMerge == false
        vm.toggleActivitySelection(1)
        XCTAssertFalse(vm.canMerge)

        // 2) 勾选 2 个不同类型活动 (running + cycling) -> canMerge == false
        vm.toggleActivitySelection(3)
        XCTAssertEqual(vm.selectedActivities.count, 2)
        XCTAssertFalse(vm.canMerge)

        // 3) 取消 cycling，勾选第 2 个 running -> 2 个同类型活动，canMerge == true
        vm.toggleActivitySelection(3)
        vm.toggleActivitySelection(2)
        XCTAssertEqual(vm.selectedActivities.count, 2)
        XCTAssertTrue(vm.canMerge)

        // 开启合并
        vm.isMergeEnabled = true
        XCTAssertTrue(vm.isMergeEnabled)

        // 4) 再额外勾选不同类型 cycling -> canMerge 变为 false，isMergeEnabled 必须自动复位为 false
        vm.toggleActivitySelection(3)
        XCTAssertFalse(vm.canMerge)
        XCTAssertFalse(vm.isMergeEnabled)

        // 5) 恢复 2 个 running 并开启合并，然后取消勾选其中 1 个使数量剩 1 -> isMergeEnabled 自动复位为 false
        vm.toggleActivitySelection(3)
        XCTAssertTrue(vm.canMerge)
        vm.isMergeEnabled = true
        XCTAssertTrue(vm.isMergeEnabled)

        vm.toggleActivitySelection(2)
        XCTAssertFalse(vm.canMerge)
        XCTAssertFalse(vm.isMergeEnabled)
    }

    // MARK: 4. 合并下载 vs 单文件下载 & 无 GPS 活动导出 GPX 提示与重试

    func testDownloadMergedAndSingleAndMissingGPSFallbackRetry() async throws {
        let keychain = MockKeychainStore()
        let auth = MockAuthService()
        let api = MockGarminApiClient()
        let exporter = SpyActivityExporter()

        let tempDir = FileManager.default.temporaryDirectory
            .appendingPathComponent("GarminVMTests_\(UUID().uuidString)", isDirectory: true)
        try FileManager.default.createDirectory(at: tempDir, withIntermediateDirectories: true)
        defer { try? FileManager.default.removeItem(at: tempDir) }

        let run1 = ActivityItem(
            activityId: 201,
            activityName: "早间跑",
            startTimeLocal: "2026-11-10 07:00:00",
            startTimeGMT: "2026-11-09 23:00:00",
            activityType: ActivityTypeDTO(typeKey: "running"),
            distance: 3000,
            duration: 900
        )
        let run2 = ActivityItem(
            activityId: 202,
            activityName: "晚间跑",
            startTimeLocal: "2026-11-11 19:00:00",
            startTimeGMT: "2026-11-11 11:00:00",
            activityType: ActivityTypeDTO(typeKey: "running"),
            distance: 5000,
            duration: 1500
        )
        let indoorRun = ActivityItem(
            activityId: 203,
            activityName: "室内跑步机",
            startTimeLocal: "2026-11-11 21:00:00",
            startTimeGMT: "2026-11-11 13:00:00",
            activityType: ActivityTypeDTO(typeKey: "indoor_running"),
            distance: 4000,
            duration: 1200
        )

        // 构造带 GPS 的合成 FIT ZIP
        let gpsPoints1 = [
            TrackPoint(
                timestamp: Date(timeIntervalSince1970: 1_700_000_000),
                latitudeDegrees: 31.2304,
                longitudeDegrees: 121.4737,
                altitudeMeters: 12.0,
                distanceMeters: 100.0,
                heartRateBpm: 145
            )
        ]
        let gpsPoints2 = [
            TrackPoint(
                timestamp: Date(timeIntervalSince1970: 1_700_086_400),
                latitudeDegrees: 31.2400,
                longitudeDegrees: 121.4800,
                altitudeMeters: 15.0,
                distanceMeters: 200.0,
                heartRateBpm: 152
            )
        ]
        // 构造无 GPS 的室内活动 FIT ZIP
        let noGpsPoints = [
            TrackPoint(
                timestamp: Date(timeIntervalSince1970: 1_700_090_000),
                latitudeDegrees: nil,
                longitudeDegrees: nil,
                altitudeMeters: nil,
                distanceMeters: 400.0,
                heartRateBpm: 160,
                cadenceRpm: 180
            )
        ]

        api.zipDataByActivityId[201] = ZipExtractor.createSingleFileZip(
            fileName: "201_ACTIVITY.fit",
            fileData: FitParser.buildSyntheticFit(records: gpsPoints1)
        )
        api.zipDataByActivityId[202] = ZipExtractor.createSingleFileZip(
            fileName: "202_ACTIVITY.fit",
            fileData: FitParser.buildSyntheticFit(records: gpsPoints2)
        )
        api.zipDataByActivityId[203] = ZipExtractor.createSingleFileZip(
            fileName: "203_ACTIVITY.fit",
            fileData: FitParser.buildSyntheticFit(records: noGpsPoints)
        )
        api.activitiesToReturn = [run1, run2, indoorRun]

        let vm = MainViewModel(
            keychainService: keychain,
            authService: auth,
            apiClient: api,
            exporter: exporter
        )
        vm.saveDirectoryURL = tempDir
        vm.apiKeyInput = "valid-key"
        await vm.login()

        // (A) 勾选 2 条同类型 running 活动并开启合并 -> 调用 exportMerged 仅生成 1 个文件
        vm.setActivitySelection(201, isSelected: true)
        vm.setActivitySelection(202, isSelected: true)
        XCTAssertTrue(vm.canMerge)
        vm.isMergeEnabled = true
        vm.exportFormat = .gpx

        await vm.downloadSelected()
        XCTAssertEqual(exporter.exportMergedCallCount, 1)
        XCTAssertEqual(exporter.exportSingleCallCount, 0)
        XCTAssertEqual(vm.lastExportedURLs.count, 1)
        XCTAssertTrue(FileManager.default.fileExists(atPath: vm.lastExportedURLs[0].path))

        // (B) 关闭合并下载 -> 调用 exportSingle 生成 2 个独立文件
        vm.isMergeEnabled = false
        await vm.downloadSelected()
        XCTAssertEqual(exporter.exportMergedCallCount, 1)
        XCTAssertEqual(exporter.exportSingleCallCount, 2)
        XCTAssertEqual(vm.lastExportedURLs.count, 2)

        // (C) 仅勾选无 GPS 的室内跑活动，选择 GPX 导出 -> 触发 showMissingGPSAlert
        vm.deselectAllFilteredActivities()
        vm.setActivitySelection(203, isSelected: true)
        vm.exportFormat = .gpx

        await vm.downloadSelected()
        XCTAssertTrue(vm.showMissingGPSAlert)
        XCTAssertEqual(vm.missingGPSActivityNames, ["室内跑步机"])

        // 一键改用 TCX 重试下载 -> 成功导出 .tcx 并关闭 Alert
        await vm.retryDownloadWithFormat(.tcx)
        XCTAssertFalse(vm.showMissingGPSAlert)
        XCTAssertEqual(vm.exportFormat, .tcx)
        XCTAssertEqual(vm.lastExportedURLs.count, 1)
        XCTAssertEqual(vm.lastExportedURLs[0].pathExtension, "tcx")
        XCTAssertTrue(FileManager.default.fileExists(atPath: vm.lastExportedURLs[0].path))
    }
}
