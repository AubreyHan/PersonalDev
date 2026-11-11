import CoreGraphics
import Foundation
import Photos
import Vision

public actor DaySimilarityEngine {
    public static let shared = DaySimilarityEngine()

    /// 用于纯算法聚类的轻量输入结构（便于单元测试与后台 Vision 聚类复用）
    public struct ClusterCandidate: Identifiable, Hashable, Sendable {
        public let id: String
        public let year: Int
        public let monthDay: MonthDay
        public let creationDate: Date?
        public let burstIdentifier: String?
        public let mediaType: PHAssetMediaType

        public init(
            id: String,
            year: Int,
            monthDay: MonthDay,
            creationDate: Date? = nil,
            burstIdentifier: String? = nil,
            mediaType: PHAssetMediaType = .image
        ) {
            self.id = id
            self.year = year
            self.monthDay = monthDay
            self.creationDate = creationDate
            self.burstIdentifier = burstIdentifier
            self.mediaType = mediaType
        }

        public init(assetItem: AssetItem) {
            self.id = assetItem.id
            self.year = assetItem.year
            self.monthDay = assetItem.monthDay
            self.creationDate = assetItem.creationDate
            self.burstIdentifier = assetItem.burstIdentifier
            self.mediaType = assetItem.mediaType
        }
    }

    private var currentTask: Task<[SimilarityGroup], Never>?
    private var currentMonthDay: MonthDay?

    /// 内存缓存：(assetID) -> VNFeaturePrintObservation，避免同会话重复提取特征
    private var featurePrintCache: [String: VNFeaturePrintObservation] = [:]

    public init() {}

    /// 切换 MonthDay 时取消上一日期尚未完成的相似图分析任务
    public func cancelCurrentTask() {
        currentTask?.cancel()
        currentTask = nil
        currentMonthDay = nil
    }

    public func clearFeatureCache() {
        featurePrintCache.removeAll()
    }

    // MARK: - Per-Day Vision FeaturePrint & Burst Clustering (FR-5.1 & SPEC §2.3)

    /// 对指定 MonthDay 的资产列表执行按需相似/连拍聚类（自动取消上一次未完成任务）
    public func analyze(
        monthDay: MonthDay,
        assets: [AssetItem],
        sensitivity: SimilaritySensitivity
    ) async -> [SimilarityGroup] {
        currentTask?.cancel()
        currentMonthDay = monthDay

        guard sensitivity != .off else {
            return []
        }

        let imageAssets = assets.filter { $0.mediaType == .image }
        guard imageAssets.count >= 2 else {
            return []
        }

        let task = Task <[SimilarityGroup], Never> { [weak self] in
            guard let self else { return [] }
            return await self.performClustering(
                monthDay: monthDay,
                imageAssets: imageAssets,
                sensitivity: sensitivity
            )
        }
        currentTask = task
        return await task.value
    }

    /// 重载：直接接受 PHAsset 数组
    public func analyze(
        monthDay: MonthDay,
        phAssets: [PHAsset],
        sensitivity: SimilaritySensitivity
    ) async -> [SimilarityGroup] {
        let items = phAssets.map { AssetItem(phAsset: $0) }
        return await analyze(monthDay: monthDay, assets: items, sensitivity: sensitivity)
    }

    private func performClustering(
        monthDay: MonthDay,
        imageAssets: [AssetItem],
        sensitivity: SimilaritySensitivity
    ) async -> [SimilarityGroup] {
        guard !Task.isCancelled else { return [] }

        // 按年份分组（不同年份绝对不跨年合并，SPEC §2.3）
        let groupedByYear = Dictionary(grouping: imageAssets, by: \.year)
        var observationsByID: [String: VNFeaturePrintObservation] = [:]

        for (_, yearAssets) in groupedByYear where yearAssets.count >= 2 {
            for item in yearAssets {
                if Task.isCancelled { return [] }
                if let cached = featurePrintCache[item.id] {
                    observationsByID[item.id] = cached
                    continue
                }
                guard let phAsset = item.phAsset else { continue }
                // 严格使用本地缩略图 (isNetworkAccessAllowed = false)，绝不触发 iCloud 原片下载
                if let cgImage = await MediaLoadService.requestLocalThumbnailCGImage(for: phAsset),
                   let observation = Self.extractFeaturePrint(from: cgImage) {
                    featurePrintCache[item.id] = observation
                    observationsByID[item.id] = observation
                }
            }
        }

        guard !Task.isCancelled else { return [] }

        let candidates = imageAssets.map { ClusterCandidate(assetItem: $0) }
        let capturedObservations = observationsByID

        return Self.clusterCandidates(
            candidates,
            monthDay: monthDay,
            sensitivity: sensitivity
        ) { idA, idB in
            guard let obsA = capturedObservations[idA],
                  let obsB = capturedObservations[idB] else {
                return nil
            }
            var distance: Float = 0
            do {
                try obsA.computeDistance(&distance, to: obsB)
                return distance
            } catch {
                return nil
            }
        }
    }

    nonisolated private static func extractFeaturePrint(from cgImage: CGImage) -> VNFeaturePrintObservation? {
        let request = VNGenerateImageFeaturePrintRequest()
        let handler = VNImageRequestHandler(cgImage: cgImage, options: [:])
        do {
            try handler.perform([request])
            return request.results?.first as? VNFeaturePrintObservation
        } catch {
            return nil
        }
    }

    // MARK: - Pure Static Union-Find Clustering Algorithm (For Unit Tests & Engine)

    /// 纯函数：使用AssetItem列表与特征距离回调执行年份隔离的并查集连通分量聚类（SPEC §2.3）
    public nonisolated static func clusterAssetItems(
        _ items: [AssetItem],
        monthDay: MonthDay? = nil,
        sensitivity: SimilaritySensitivity,
        distanceProvider: (String, String) -> Float?
    ) -> [SimilarityGroup] {
        let resolvedMonthDay = monthDay ?? items.first?.monthDay ?? MonthDay(month: 1, day: 1)
        let candidates = items.map { ClusterCandidate(assetItem: $0) }
        return clusterCandidates(
            candidates,
            monthDay: resolvedMonthDay,
            sensitivity: sensitivity,
            distanceProvider: distanceProvider
        )
    }

    /// 纯函数：基于 `burstIdentifier` 与 Vision 特征距离 + 拍摄时间窗口的并查集（Union-Find）连通分量聚类
    /// - 规则 1：仅纳入 `mediaType == .image` 资产
    /// - 规则 2：严格按 `year` 隔离，不同年份绝不合并
    /// - 规则 3：相同非空 `burstIdentifier` 无条件归入同一连通分量
    /// - 规则 4：`strict` 档要求 `distance <= 0.32 && abs(Δt) <= 600s`；`standard` 档要求 `distance <= 0.45 && abs(Δt) <= 14400s`
    /// - 规则 5：组内按 `creationDate` 升序排列，仅输出成员数 >= 2 的相似组
    public nonisolated static func clusterCandidates(
        _ candidates: [ClusterCandidate],
        monthDay: MonthDay,
        sensitivity: SimilaritySensitivity,
        distanceProvider: (String, String) -> Float?
    ) -> [SimilarityGroup] {
        guard sensitivity != .off,
              let maxDistance = sensitivity.maxFeatureDistance,
              let maxTimeInterval = sensitivity.maxTimeInterval else {
            return []
        }

        let imageCandidates = candidates.filter { $0.mediaType == .image }
        guard imageCandidates.count >= 2 else {
            return []
        }

        let groupedByYear = Dictionary(grouping: imageCandidates, by: \.year)
        let sortedYears = groupedByYear.keys.sorted(by: >)
        var allGroups: [SimilarityGroup] = []

        for year in sortedYears {
            guard let yearItems = groupedByYear[year], yearItems.count >= 2 else {
                continue
            }

            // 年内按拍摄时间升序排列（保持网格时间轴顺序）
            let sortedItems = yearItems.sorted { lhs, rhs in
                let dateA = lhs.creationDate ?? .distantPast
                let dateB = rhs.creationDate ?? .distantPast
                if dateA != dateB {
                    return dateA < dateB
                }
                return lhs.id < rhs.id
            }

            let n = sortedItems.count
            var parent = Array(0..<n)
            var matchedByBurst = Array(repeating: false, count: n)

            func find(_ i: Int) -> Int {
                var root = i
                while parent[root] != root {
                    root = parent[root]
                }
                var curr = i
                while curr != root {
                    let nxt = parent[curr]
                    parent[curr] = root
                    curr = nxt
                }
                return root
            }

            func union(_ i: Int, _ j: Int) {
                let rootI = find(i)
                let rootJ = find(j)
                if rootI != rootJ {
                    if rootI < rootJ {
                        parent[rootJ] = rootI
                    } else {
                        parent[rootI] = rootJ
                    }
                }
            }

            // 1. 优先合并相同非空 burstIdentifier（硬件连拍簇）
            var burstFirstIndex: [String: Int] = [:]
            for idx in 0..<n {
                if let burstID = sortedItems[idx].burstIdentifier?.trimmingCharacters(in: .whitespacesAndNewlines),
                   !burstID.isEmpty {
                    if let firstIdx = burstFirstIndex[burstID] {
                        union(firstIdx, idx)
                        matchedByBurst[firstIdx] = true
                        matchedByBurst[idx] = true
                    } else {
                        burstFirstIndex[burstID] = idx
                    }
                }
            }

            // 2. 在同一年份内按时间窗口与特征距离阈值合并连通分量
            for i in 0..<n {
                for j in (i + 1)..<n {
                    if find(i) == find(j) { continue }

                    if let dateI = sortedItems[i].creationDate,
                       let dateJ = sortedItems[j].creationDate {
                        let timeDiff = abs(dateJ.timeIntervalSince(dateI))
                        if timeDiff > maxTimeInterval {
                            continue
                        }
                    }

                    let idI = sortedItems[i].id
                    let idJ = sortedItems[j].id
                    if let dist = distanceProvider(idI, idJ) ?? distanceProvider(idJ, idI),
                       dist <= maxDistance {
                        union(i, j)
                    }
                }
            }

            // 3. 收集连通分量（保持 sortedItems 内的时间升序）
            var componentsOrder: [Int] = []
            var componentsMap: [Int: [Int]] = [:]

            for idx in 0..<n {
                let root = find(idx)
                if componentsMap[root] == nil {
                    componentsOrder.append(root)
                    componentsMap[root] = [idx]
                } else {
                    componentsMap[root]?.append(idx)
                }
            }

            for root in componentsOrder {
                guard let indices = componentsMap[root], indices.count >= 2 else {
                    continue
                }
                let memberIDs = indices.map { sortedItems[$0].id }
                let allBurst = indices.allSatisfy { matchedByBurst[$0] }
                let kind: SimilarityGroupKind = allBurst ? .burst : .visionFeaturePrint
                let groupID = "\(monthDay.storageKey)-\(year)-\(memberIDs.first ?? UUID().uuidString)"
                allGroups.append(
                    SimilarityGroup(
                        id: groupID,
                        year: year,
                        monthDay: monthDay,
                        assetIDs: memberIDs,
                        kind: kind
                    )
                )
            }
        }

        return allGroups
    }
}
