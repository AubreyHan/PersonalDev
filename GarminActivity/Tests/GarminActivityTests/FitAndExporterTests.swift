import Foundation
import XCTest
@testable import GarminActivity

final class FitAndExporterTests: XCTestCase {

    private var tempDirectoryURL: URL!

    override func setUpWithError() throws {
        try super.setUpWithError()
        let baseTemp = FileManager.default.temporaryDirectory
        tempDirectoryURL = baseTemp.appendingPathComponent("GarminActivityFitTests_\(UUID().uuidString)", isDirectory: true)
        try FileManager.default.createDirectory(at: tempDirectoryURL, withIntermediateDirectories: true)
    }

    override func tearDownWithError() throws {
        if let dir = tempDirectoryURL, FileManager.default.fileExists(atPath: dir.path) {
            try? FileManager.default.removeItem(at: dir)
        }
        try super.tearDownWithError()
    }

    // MARK: - 1. ZipExtractor 打包与解压测试

    func testZipExtractorPackAndExtractFitData() throws {
        let baseDate = Date(timeIntervalSince1970: 1_762_800_000) // 2025-11-10T18:40:00Z
        let points = (0..<25).map { idx in
            TrackPoint(
                timestamp: baseDate.addingTimeInterval(Double(idx) * 5.0),
                latitudeDegrees: 31.2304 + Double(idx) * 0.0001,
                longitudeDegrees: 121.4737 + Double(idx) * 0.0001,
                altitudeMeters: 12.0 + Double(idx) * 0.2,
                distanceMeters: Double(idx) * 15.0,
                heartRateBpm: UInt8(135 + (idx % 20)),
                cadenceRpm: 88,
                powerWatts: 220,
                speedMps: 3.1,
                temperatureCelsius: 18
            )
        }

        let syntheticFit = FitParser.buildSyntheticFit(records: points, sport: 1)
        let zipArchive = ZipExtractor.createSingleFileZip(
            fileName: "16892401234_ACTIVITY.fit",
            fileData: syntheticFit
        )

        let extractedFit = try ZipExtractor.extractFitData(from: zipArchive)
        XCTAssertEqual(extractedFit, syntheticFit)

        // 当直接传入已经是 .FIT 格式的 Data 时，应原样返回
        let passthrough = try ZipExtractor.extractFitData(from: syntheticFit)
        XCTAssertEqual(passthrough, syntheticFit)

        // 无效数据应抛出 .invalidZipArchive
        XCTAssertThrowsError(try ZipExtractor.extractFitData(from: Data([0x00, 0x01, 0x02]))) { error in
            XCTAssertEqual(error as? ExportError, .invalidZipArchive)
        }
    }

    // MARK: - 2. FitParser 构建合成 FIT 并解析还原测试

    func testFitParserBuildAndParseRoundtrip() throws {
        let t0 = Date(timeIntervalSince1970: 1_762_815_600) // 精确到秒
        let inputPoints = [
            TrackPoint(
                timestamp: t0,
                latitudeDegrees: 39.904200,
                longitudeDegrees: 116.407400,
                altitudeMeters: 45.6,
                distanceMeters: 0.0,
                heartRateBpm: 128,
                cadenceRpm: 85,
                powerWatts: 210,
                speedMps: 2.85,
                temperatureCelsius: 21
            ),
            TrackPoint(
                timestamp: t0.addingTimeInterval(10),
                latitudeDegrees: 39.904500,
                longitudeDegrees: 116.407800,
                altitudeMeters: 47.2,
                distanceMeters: 35.5,
                heartRateBpm: 142,
                cadenceRpm: 90,
                powerWatts: 245,
                speedMps: 3.55,
                temperatureCelsius: 22
            ),
            TrackPoint(
                timestamp: t0.addingTimeInterval(20),
                latitudeDegrees: 39.904900,
                longitudeDegrees: 116.408300,
                altitudeMeters: 49.0,
                distanceMeters: 75.0,
                heartRateBpm: 156,
                cadenceRpm: 92,
                powerWatts: 270,
                speedMps: 3.95,
                temperatureCelsius: 22
            )
        ]

        let fitData = FitParser.buildSyntheticFit(records: inputPoints, sport: 1)
        let activity = ActivityItem(
            activityId: 1001,
            activityName: "奥森晨跑",
            startTimeLocal: "2026-11-11 07:00:00",
            startTimeGMT: "2026-11-10 23:00:00",
            activityType: ActivityTypeDTO(typeId: 1, typeKey: "running"),
            distance: 75.0,
            duration: 20.0,
            hasPolyline: true
        )

        let parsed = try FitParser.parse(fitData: fitData, activity: activity)

        XCTAssertEqual(parsed.activityId, 1001)
        XCTAssertEqual(parsed.typeKey, "running")
        XCTAssertEqual(parsed.rawFitData, fitData)
        XCTAssertTrue(parsed.hasValidGPSCoordinates)
        XCTAssertEqual(parsed.trackPoints.count, 3)
        XCTAssertEqual(parsed.laps.count, 1)

        for (idx, expected) in inputPoints.enumerated() {
            let actual = parsed.trackPoints[idx]
            XCTAssertEqual(actual.timestamp.timeIntervalSince1970, expected.timestamp.timeIntervalSince1970, accuracy: 1.0)
            XCTAssertNotNil(actual.latitudeDegrees)
            XCTAssertNotNil(actual.longitudeDegrees)
            XCTAssertEqual(actual.latitudeDegrees!, expected.latitudeDegrees!, accuracy: 0.0001)
            XCTAssertEqual(actual.longitudeDegrees!, expected.longitudeDegrees!, accuracy: 0.0001)
            XCTAssertEqual(actual.altitudeMeters!, expected.altitudeMeters!, accuracy: 0.25)
            XCTAssertEqual(actual.distanceMeters!, expected.distanceMeters!, accuracy: 0.05)
            XCTAssertEqual(actual.heartRateBpm, expected.heartRateBpm)
            XCTAssertEqual(actual.cadenceRpm, expected.cadenceRpm)
            XCTAssertEqual(actual.powerWatts, expected.powerWatts)
            XCTAssertEqual(actual.speedMps!, expected.speedMps!, accuracy: 0.01)
            XCTAssertEqual(actual.temperatureCelsius, expected.temperatureCelsius)
        }

        XCTAssertEqual(parsed.totalDistanceMeters, 75.0, accuracy: 0.1)
        XCTAssertEqual(parsed.totalElapsedTimeSeconds, 20.0, accuracy: 0.1)
        XCTAssertEqual(parsed.laps[0].maxHeartRateBpm, 156)
    }

    // MARK: - 3. 无 GPS 室内活动导出保护与回退测试

    func testIndoorActivityMissingGPSProtectionAndFallbackExports() throws {
        let t0 = Date(timeIntervalSince1970: 1_762_820_000)
        // 无经纬度坐标的室内训练轨迹点
        let indoorPoints = [
            TrackPoint(
                timestamp: t0,
                latitudeDegrees: nil,
                longitudeDegrees: nil,
                altitudeMeters: nil,
                distanceMeters: 0.0,
                heartRateBpm: 118,
                cadenceRpm: 80,
                powerWatts: 180,
                speedMps: 2.5,
                temperatureCelsius: 24
            ),
            TrackPoint(
                timestamp: t0.addingTimeInterval(30),
                latitudeDegrees: nil,
                longitudeDegrees: nil,
                altitudeMeters: nil,
                distanceMeters: 80.0,
                heartRateBpm: 145,
                cadenceRpm: 86,
                powerWatts: 215,
                speedMps: 2.8,
                temperatureCelsius: 25
            )
        ]

        let indoorFit = FitParser.buildSyntheticFit(records: indoorPoints, sport: 1)
        let indoorActivity = ActivityItem(
            activityId: 2002,
            activityName: "室内跑步机:间歇/测试",
            startTimeLocal: "2026-11-11 20:00:00",
            startTimeGMT: "2026-11-11 12:00:00",
            activityType: ActivityTypeDTO(typeId: 1, typeKey: "treadmill_running"),
            distance: 80.0,
            duration: 30.0,
            hasPolyline: false
        )

        let parsed = try FitParser.parse(fitData: indoorFit, activity: indoorActivity)
        XCTAssertFalse(parsed.hasValidGPSCoordinates)

        let exporter = ActivityExporter()

        // 导出 .gpx 必须抛出 .missingGPSForGPX
        XCTAssertThrowsError(
            try exporter.exportSingle(parsed: parsed, activity: indoorActivity, format: .gpx, to: tempDirectoryURL)
        ) { error in
            XCTAssertEqual(
                error as? ExportError,
                .missingGPSForGPX(activityNames: ["室内跑步机:间歇/测试"])
            )
        }

        // 回退导出 .tcx 成功并保留心率、功率、时间序列，且文件名非法字符被净化
        let tcxURL = try exporter.exportSingle(parsed: parsed, activity: indoorActivity, format: .tcx, to: tempDirectoryURL)
        XCTAssertEqual(tcxURL.lastPathComponent, "2026-11-11_treadmill_running_室内跑步机_间歇_测试_2002.tcx")
        let tcxContent = try String(contentsOf: tcxURL, encoding: .utf8)
        XCTAssertTrue(tcxContent.contains("<TrainingCenterDatabase"))
        XCTAssertTrue(tcxContent.contains("<HeartRateBpm><Value>118</Value></HeartRateBpm>"))
        XCTAssertTrue(tcxContent.contains("<HeartRateBpm><Value>145</Value></HeartRateBpm>"))
        XCTAssertTrue(tcxContent.contains("<ns3:Watts>215</ns3:Watts>"))
        XCTAssertFalse(tcxContent.contains("<Position>"))

        // 回退导出 .fit 成功并可再次被 FitParser 完整解析还原
        let fitURL = try exporter.exportSingle(parsed: parsed, activity: indoorActivity, format: .fit, to: tempDirectoryURL)
        XCTAssertEqual(fitURL.lastPathComponent, "2026-11-11_treadmill_running_室内跑步机_间歇_测试_2002.fit")
        let savedFitData = try Data(contentsOf: fitURL)
        let reparsed = try FitParser.parse(fitData: savedFitData, activity: indoorActivity)
        XCTAssertEqual(reparsed.trackPoints.count, 2)
        XCTAssertEqual(reparsed.trackPoints[0].heartRateBpm, 118)
        XCTAssertEqual(reparsed.trackPoints[1].heartRateBpm, 145)
    }

    // MARK: - 4. 多条同类型活动按时间升序合并与异构类型拦截测试

    func testExportMergedChronologicalOrderingAndMismatchedTypeValidation() throws {
        let exporter = ActivityExporter()

        // 构造较早的活动 (2026-11-01)
        let earlyStart = Date(timeIntervalSince1970: 1_761_955_200) // 2025-11-01 00:00:00 UTC
        let earlyPoints = [
            TrackPoint(
                timestamp: earlyStart,
                latitudeDegrees: 30.2500,
                longitudeDegrees: 120.1500,
                altitudeMeters: 15.0,
                distanceMeters: 0.0,
                heartRateBpm: 130,
                cadenceRpm: 84,
                powerWatts: 200,
                speedMps: 3.0,
                temperatureCelsius: 16
            ),
            TrackPoint(
                timestamp: earlyStart.addingTimeInterval(60),
                latitudeDegrees: 30.2510,
                longitudeDegrees: 120.1510,
                altitudeMeters: 16.0,
                distanceMeters: 180.0,
                heartRateBpm: 140,
                cadenceRpm: 86,
                powerWatts: 210,
                speedMps: 3.0,
                temperatureCelsius: 16
            )
        ]
        let earlyActivity = ActivityItem(
            activityId: 3001,
            activityName: "西湖晨跑",
            startTimeLocal: "2026-11-01 07:00:00",
            startTimeGMT: "2026-10-31 23:00:00",
            activityType: ActivityTypeDTO(typeId: 1, typeKey: "running"),
            distance: 180.0,
            duration: 60.0,
            hasPolyline: true
        )
        let earlyFit = FitParser.buildSyntheticFit(records: earlyPoints, sport: 1)
        let earlyParsed = try FitParser.parse(fitData: earlyFit, activity: earlyActivity)

        // 构造较晚的活动 (2026-11-11)
        let lateStart = earlyStart.addingTimeInterval(86400 * 10)
        let latePoints = [
            TrackPoint(
                timestamp: lateStart,
                latitudeDegrees: 31.2300,
                longitudeDegrees: 121.4700,
                altitudeMeters: 8.0,
                distanceMeters: 0.0,
                heartRateBpm: 150,
                cadenceRpm: 90,
                powerWatts: 250,
                speedMps: 3.5,
                temperatureCelsius: 19
            ),
            TrackPoint(
                timestamp: lateStart.addingTimeInterval(60),
                latitudeDegrees: 31.2320,
                longitudeDegrees: 121.4720,
                altitudeMeters: 9.0,
                distanceMeters: 210.0,
                heartRateBpm: 162,
                cadenceRpm: 92,
                powerWatts: 265,
                speedMps: 3.5,
                temperatureCelsius: 20
            )
        ]
        let lateActivity = ActivityItem(
            activityId: 3002,
            activityName: "世纪公园夜跑",
            startTimeLocal: "2026-11-11 20:00:00",
            startTimeGMT: "2026-11-11 12:00:00",
            activityType: ActivityTypeDTO(typeId: 1, typeKey: "running"),
            distance: 210.0,
            duration: 60.0,
            hasPolyline: true
        )
        let lateFit = FitParser.buildSyntheticFit(records: latePoints, sport: 1)
        let lateParsed = try FitParser.parse(fitData: lateFit, activity: lateActivity)

        // 故意乱序传入：[较晚活动, 较早活动]
        let unorderedItems = [
            (activity: lateActivity, parsed: lateParsed),
            (activity: earlyActivity, parsed: earlyParsed)
        ]

        // 4.1 GPX 合并验证
        let mergedGPXURL = try exporter.exportMerged(items: unorderedItems, format: .gpx, to: tempDirectoryURL)
        XCTAssertEqual(mergedGPXURL.lastPathComponent, "2026-11-01_至_2026-11-11_running_合并(2条).gpx")
        let gpxString = try String(contentsOf: mergedGPXURL, encoding: .utf8)
        let firstLatRange = try XCTUnwrap(gpxString.range(of: "30.2500000"))
        let secondLatRange = try XCTUnwrap(gpxString.range(of: "31.2300000"))
        // 验证较早活动的坐标点排在较晚活动之前
        XCTAssertLessThan(firstLatRange.lowerBound, secondLatRange.lowerBound)

        // 4.2 TCX 合并验证
        let mergedTCXURL = try exporter.exportMerged(items: unorderedItems, format: .tcx, to: tempDirectoryURL)
        XCTAssertEqual(mergedTCXURL.lastPathComponent, "2026-11-01_至_2026-11-11_running_合并(2条).tcx")
        let tcxString = try String(contentsOf: mergedTCXURL, encoding: .utf8)
        let firstHRRange = try XCTUnwrap(tcxString.range(of: "<Value>130</Value>"))
        let secondHRRange = try XCTUnwrap(tcxString.range(of: "<Value>162</Value>"))
        XCTAssertLessThan(firstHRRange.lowerBound, secondHRRange.lowerBound)
        // 验证跨活动累计距离平滑累加 (180 + 210 = 390.00)
        XCTAssertTrue(tcxString.contains("<DistanceMeters>390.00</DistanceMeters>"))

        // 4.3 FIT 合并验证
        let mergedFITURL = try exporter.exportMerged(items: unorderedItems, format: .fit, to: tempDirectoryURL)
        XCTAssertEqual(mergedFITURL.lastPathComponent, "2026-11-01_至_2026-11-11_running_合并(2条).fit")
        let mergedFitData = try Data(contentsOf: mergedFITURL)
        let reparsedMergedFit = try FitParser.parse(fitData: mergedFitData, activity: earlyActivity)
        XCTAssertEqual(reparsedMergedFit.trackPoints.count, 4)
        XCTAssertEqual(reparsedMergedFit.trackPoints[0].heartRateBpm, 130)
        XCTAssertEqual(reparsedMergedFit.trackPoints[1].heartRateBpm, 140)
        XCTAssertEqual(reparsedMergedFit.trackPoints[2].heartRateBpm, 150)
        XCTAssertEqual(reparsedMergedFit.trackPoints[3].heartRateBpm, 162)
        XCTAssertEqual(reparsedMergedFit.totalDistanceMeters, 390.0, accuracy: 0.5)

        // 4.4 不同类型活动传入 exportMerged 时必须抛出 .mismatchedTypesForMerge
        let cyclingActivity = ActivityItem(
            activityId: 4001,
            activityName: "骑行台训练",
            startTimeLocal: "2026-11-11 21:00:00",
            startTimeGMT: "2026-11-11 13:00:00",
            activityType: ActivityTypeDTO(typeId: 2, typeKey: "cycling"),
            distance: 5000.0,
            duration: 600.0,
            hasPolyline: true
        )
        let mixedItems = [
            (activity: earlyActivity, parsed: earlyParsed),
            (activity: cyclingActivity, parsed: lateParsed)
        ]
        XCTAssertThrowsError(
            try exporter.exportMerged(items: mixedItems, format: .gpx, to: tempDirectoryURL)
        ) { error in
            XCTAssertEqual(error as? ExportError, .mismatchedTypesForMerge)
        }
    }
}
