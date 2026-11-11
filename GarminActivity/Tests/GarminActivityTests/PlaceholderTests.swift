import XCTest
@testable import GarminActivity

final class PlaceholderTests: XCTestCase {
    func testModelsAndFormattingContracts() throws {
        // GarminRegion
        XCTAssertEqual(GarminRegion.global.baseURL.absoluteString, "https://personal.connectapi.garmin.com")
        XCTAssertEqual(GarminRegion.china.baseURL.absoluteString, "https://personal.connectapi.garmin.cn")

        // ExportFormat
        XCTAssertEqual(ExportFormat.gpx.fileExtension, "gpx")
        XCTAssertEqual(ExportFormat.tcx.fileExtension, "tcx")
        XCTAssertEqual(ExportFormat.fit.fileExtension, "fit")
        XCTAssertEqual(ExportFormat.gpx.displayName, "GPX")
        XCTAssertEqual(ExportFormat.tcx.displayName, "TCX")
        XCTAssertEqual(ExportFormat.fit.displayName, "FIT")

        // ActivityItem
        let item = ActivityItem(
            activityId: 16892401234,
            activityName: "晨跑",
            startTimeLocal: "2026-11-11 07:15:20",
            startTimeGMT: "2026-11-10 23:15:20",
            activityType: ActivityTypeDTO(typeId: 1, typeKey: "running", parentTypeId: 17),
            distance: 10025.5,
            duration: 3120.4,
            hasPolyline: true
        )
        XCTAssertEqual(item.id, 16892401234)
        XCTAssertEqual(item.typeKey, "running")
        XCTAssertEqual(item.displayName, "晨跑")
        XCTAssertEqual(item.datePrefix, "2026-11-11")
        XCTAssertEqual(item.formattedDistance, "10.03 km")
        XCTAssertEqual(item.formattedDuration, "52:00")
        XCTAssertNotNil(item.startGMTDate)

        // ParsedActivityData GPS check
        let indoorData = ParsedActivityData(
            activityId: 1,
            activityName: "Indoor Run",
            typeKey: "indoor_running",
            startTime: Date(),
            totalElapsedTimeSeconds: 600,
            totalTimerTimeSeconds: 600,
            totalDistanceMeters: 1500,
            trackPoints: [TrackPoint(timestamp: Date(), heartRateBpm: 145)]
        )
        XCTAssertFalse(indoorData.hasValidGPSCoordinates)

        let outdoorData = ParsedActivityData(
            activityId: 2,
            activityName: "Outdoor Run",
            typeKey: "running",
            startTime: Date(),
            totalElapsedTimeSeconds: 600,
            totalTimerTimeSeconds: 600,
            totalDistanceMeters: 1500,
            trackPoints: [TrackPoint(timestamp: Date(), latitudeDegrees: 31.2304, longitudeDegrees: 121.4737)]
        )
        XCTAssertTrue(outdoorData.hasValidGPSCoordinates)
    }
}
