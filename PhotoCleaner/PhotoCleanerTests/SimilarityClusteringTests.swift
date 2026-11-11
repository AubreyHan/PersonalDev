import Foundation
import Photos
import XCTest
@testable import PhotoCleaner

final class SimilarityClusteringTests: XCTestCase {
    private let targetDay = MonthDay(month: 8, day: 13)
    private let baseDate = Date(timeIntervalSince1970: 1_755_060_000)

    func testHardwareBurstIdentifierUnconditionalMerging() {
        let items = [
            AssetItem(
                id: "burst-1",
                mediaType: .image,
                creationDate: baseDate,
                year: 2025,
                monthDay: targetDay,
                burstIdentifier: "BURST-UUID-100"
            ),
            AssetItem(
                id: "burst-2",
                mediaType: .image,
                creationDate: baseDate.addingTimeInterval(1),
                year: 2025,
                monthDay: targetDay,
                burstIdentifier: "BURST-UUID-100"
            ),
            AssetItem(
                id: "single-other",
                mediaType: .image,
                creationDate: baseDate.addingTimeInterval(2),
                year: 2025,
                monthDay: targetDay,
                burstIdentifier: nil
            )
        ]

        // 即使特征距离返回 0.99（远高于阈值），相同 burstIdentifier 也必须无条件归入同一组且 kind == .burst
        let groups = DaySimilarityEngine.clusterAssetItems(
            items,
            monthDay: targetDay,
            sensitivity: .strict
        ) { _, _ in
            0.99
        }

        XCTAssertEqual(groups.count, 1)
        XCTAssertEqual(groups[0].year, 2025)
        XCTAssertEqual(groups[0].kind, .burst)
        XCTAssertEqual(groups[0].assetIDs, ["burst-1", "burst-2"])
    }

    func testDistanceThresholdTimeWindowAndTransitiveUnionFind() {
        // A (t=0) 与 B (t=120s) 距离 0.30
        // B (t=120s) 与 C (t=240s) 距离 0.31
        // A 与 C 直接距离 0.52（超过阈值，但通过 B 连通分量并查集归入同一组！）
        // D (t=7200s, 即 2小时后) 与 A 距离 0.25：
        //   - 在 standard (<=0.45, <=14400s) 下应与 A/B/C 合并
        //   - 在 strict (<=0.32, <=600s) 下因超过 600s 时间窗口不应合并
        let itemA = AssetItem(id: "img-A", mediaType: .image, creationDate: baseDate, year: 2025, monthDay: targetDay)
        let itemB = AssetItem(id: "img-B", mediaType: .image, creationDate: baseDate.addingTimeInterval(120), year: 2025, monthDay: targetDay)
        let itemC = AssetItem(id: "img-C", mediaType: .image, creationDate: baseDate.addingTimeInterval(240), year: 2025, monthDay: targetDay)
        let itemD = AssetItem(id: "img-D", mediaType: .image, creationDate: baseDate.addingTimeInterval(7_200), year: 2025, monthDay: targetDay)

        let pairwiseDistances: [Set<String>: Float] = [
            ["img-A", "img-B"]: 0.30,
            ["img-B", "img-C"]: 0.31,
            ["img-A", "img-C"]: 0.52,
            ["img-A", "img-D"]: 0.25
        ]

        // 1. 标准档 (standard: <= 0.45 & <= 4h) -> A, B, C, D 全部连通为 1 组，按时间升序排列
        let standardGroups = DaySimilarityEngine.clusterAssetItems(
            [itemC, itemA, itemD, itemB], // 故意乱序输入，验证输出按 creationDate 升序
            monthDay: targetDay,
            sensitivity: .standard
        ) { id1, id2 in
            pairwiseDistances[[id1, id2]]
        }

        XCTAssertEqual(standardGroups.count, 1)
        XCTAssertEqual(standardGroups[0].kind, .visionFeaturePrint)
        XCTAssertEqual(standardGroups[0].assetIDs, ["img-A", "img-B", "img-C", "img-D"])

        // 2. 严格档 (strict: <= 0.32 & <= 600s) -> A, B, C 合并，D 因时间间隔 7200s > 600s 被排除
        let strictGroups = DaySimilarityEngine.clusterAssetItems(
            [itemC, itemA, itemD, itemB],
            monthDay: targetDay,
            sensitivity: .strict
        ) { id1, id2 in
            pairwiseDistances[[id1, id2]]
        }

        XCTAssertEqual(strictGroups.count, 1)
        XCTAssertEqual(strictGroups[0].assetIDs, ["img-A", "img-B", "img-C"])

        // 3. 关闭档 (off) -> 返回空列表
        let offGroups = DaySimilarityEngine.clusterAssetItems(
            [itemA, itemB, itemC],
            monthDay: targetDay,
            sensitivity: .off
        ) { _, _ in 0.0 }
        XCTAssertTrue(offGroups.isEmpty)
    }

    func testCrossYearIsolationAndVideoExclusion() {
        // 2024年8月13日 与 2025年8月13日 各有照片，哪怕距离为 0.0 也绝不跨年合并（SPEC §2.3）
        // 纯视频 (.video) 不参与图像相似聚类
        let y2025_1 = AssetItem(id: "2025-1", mediaType: .image, creationDate: baseDate, year: 2025, monthDay: targetDay)
        let y2024_1 = AssetItem(id: "2024-1", mediaType: .image, creationDate: baseDate, year: 2024, monthDay: targetDay)
        let y2024_2 = AssetItem(id: "2024-2", mediaType: .image, creationDate: baseDate.addingTimeInterval(10), year: 2024, monthDay: targetDay)
        let y2024_video = AssetItem(id: "2024-vid", mediaType: .video, creationDate: baseDate.addingTimeInterval(5), year: 2024, monthDay: targetDay)

        let groups = DaySimilarityEngine.clusterAssetItems(
            [y2025_1, y2024_1, y2024_2, y2024_video],
            monthDay: targetDay,
            sensitivity: .standard
        ) { _, _ in
            0.05 // 任意两张距离极近
        }

        // 2025 年仅有 1 张照片，不成组；2024 年 2 张静态图成组，视频被排除
        XCTAssertEqual(groups.count, 1)
        XCTAssertEqual(groups[0].year, 2024)
        XCTAssertEqual(groups[0].assetIDs, ["2024-1", "2024-2"])
    }
}
