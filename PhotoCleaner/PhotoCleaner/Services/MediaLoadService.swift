import AppKit
import AVFoundation
import CoreLocation
import Foundation
import ImageIO
import Observation
import Photos
import PhotosUI

@MainActor
@Observable
public final class MediaLoadService {
    public static let shared = MediaLoadService()

    public nonisolated static let defaultGridThumbnailSize = CGSize(width: 320, height: 320)

    private let cachingManager: PHCachingImageManager
    private let geocoder = CLGeocoder()

    /// 内存缓存：assetID -> 精确字节数 (EC-6)
    public private(set) var fileSizeCache: [String: Int64] = [:]

    /// 内存缓存：assetID -> 原片是否在本地可用
    public private(set) var localAvailabilityCache: [String: Bool] = [:]

    /// 内存缓存：assetID -> 完整 EXIF 元数据
    public private(set) var exifDetailCache: [String: AssetExifDetail] = [:]

    /// 逆地理编码缓存："lat,lon" -> 地点描述 (EC-6 防限流)
    private var locationDescriptionCache: [String: String] = [:]

    public init(cachingManager: PHCachingImageManager = PHCachingImageManager()) {
        self.cachingManager = cachingManager
        self.cachingManager.allowsCachingHighQualityImages = false
    }

    // MARK: - Grid Thumbnail Pipeline (Strictly Local Only — FR-1.5)

    /// 构建网格缩略图与相似图分析专用的本地安全 PHImageRequestOptions
    /// 强制 `isNetworkAccessAllowed = false`，严禁后台批量拉取 iCloud 原片
    public nonisolated static func makeLocalThumbnailOptions(
        deliveryMode: PHImageRequestOptionsDeliveryMode = .opportunistic,
        isSynchronous: Bool = false
    ) -> PHImageRequestOptions {
        let options = PHImageRequestOptions()
        options.isNetworkAccessAllowed = false
        options.deliveryMode = deliveryMode
        options.resizeMode = .fast
        options.isSynchronous = isSynchronous
        return options
    }

    /// 请求网格缩略图（强制 `isNetworkAccessAllowed = false`，返回 requestID 支持取消）
    @discardableResult
    public func requestThumbnail(
        for asset: PHAsset,
        targetSize: CGSize = MediaLoadService.defaultGridThumbnailSize,
        contentMode: PHImageContentMode = .aspectFill,
        resultHandler: @escaping @MainActor (NSImage?, Bool) -> Void
    ) -> PHImageRequestID {
        let options = Self.makeLocalThumbnailOptions(deliveryMode: .opportunistic)
        return cachingManager.requestImage(
            for: asset,
            targetSize: targetSize,
            contentMode: contentMode,
            options: options
        ) { [weak self] image, info in
            let isInCloud = (info?[PHImageResultIsInCloudKey] as? NSNumber)?.boolValue ?? false
            let isDegraded = (info?[PHImageResultIsDegradedKey] as? NSNumber)?.boolValue ?? false
            Task { @MainActor in
                if isInCloud && image == nil {
                    self?.localAvailabilityCache[asset.localIdentifier] = false
                } else if image != nil && !isDegraded {
                    self?.localAvailabilityCache[asset.localIdentifier] = !isInCloud
                }
                resultHandler(image, isInCloud)
            }
        }
    }

    /// 异步获取单张本地缩略图（强制 `isNetworkAccessAllowed = false`）
    public func loadThumbnail(
        for asset: PHAsset,
        targetSize: CGSize = MediaLoadService.defaultGridThumbnailSize,
        contentMode: PHImageContentMode = .aspectFill
    ) async -> (image: NSImage?, isInCloud: Bool) {
        await withCheckedContinuation { continuation in
            let options = Self.makeLocalThumbnailOptions(deliveryMode: .highQualityFormat)
            var didResume = false
            cachingManager.requestImage(
                for: asset,
                targetSize: targetSize,
                contentMode: contentMode,
                options: options
            ) { image, info in
                let isDegraded = (info?[PHImageResultIsDegradedKey] as? NSNumber)?.boolValue ?? false
                guard !isDegraded, !didResume else { return }
                didResume = true
                let isInCloud = (info?[PHImageResultIsInCloudKey] as? NSNumber)?.boolValue ?? false
                continuation.resume(returning: (image, isInCloud))
            }
        }
    }

    /// 供 DaySimilarityEngine 提取本地缩略图 CGImage（强制 `isNetworkAccessAllowed = false`）
    public nonisolated static func requestLocalThumbnailCGImage(
        for asset: PHAsset,
        targetSize: CGSize = CGSize(width: 256, height: 256)
    ) async -> CGImage? {
        await withCheckedContinuation { continuation in
            let options = makeLocalThumbnailOptions(deliveryMode: .highQualityFormat, isSynchronous: false)
            var didResume = false
            PHImageManager.default().requestImage(
                for: asset,
                targetSize: targetSize,
                contentMode: .aspectFit,
                options: options
            ) { image, info in
                let isDegraded = (info?[PHImageResultIsDegradedKey] as? NSNumber)?.boolValue ?? false
                guard !isDegraded, !didResume else { return }
                didResume = true
                let cgImage = image?.cgImage(forProposedRect: nil, context: nil, hints: nil)
                continuation.resume(returning: cgImage)
            }
        }
    }

    // MARK: - Sliding Window Cache Preheat & Release

    public func startCaching(
        assets: [PHAsset],
        targetSize: CGSize = MediaLoadService.defaultGridThumbnailSize,
        contentMode: PHImageContentMode = .aspectFill
    ) {
        guard !assets.isEmpty else { return }
        let options = Self.makeLocalThumbnailOptions(deliveryMode: .opportunistic)
        cachingManager.startCachingImages(
            for: assets,
            targetSize: targetSize,
            contentMode: contentMode,
            options: options
        )
    }

    public func stopCaching(
        assets: [PHAsset],
        targetSize: CGSize = MediaLoadService.defaultGridThumbnailSize,
        contentMode: PHImageContentMode = .aspectFill
    ) {
        guard !assets.isEmpty else { return }
        let options = Self.makeLocalThumbnailOptions(deliveryMode: .opportunistic)
        cachingManager.stopCachingImages(
            for: assets,
            targetSize: targetSize,
            contentMode: contentMode,
            options: options
        )
    }

    public func stopCachingAllImages() {
        cachingManager.stopCachingImagesForAllAssets()
    }

    public func cancelImageRequest(_ requestID: PHImageRequestID) {
        cachingManager.cancelImageRequest(requestID)
    }

    // MARK: - On-Demand High-Res Image / Live Photo / Video (Quick Review & Compare — FR-1.5 & FR-4.2)

    /// 请求高清大图（仅在大图快审与同屏对比视图按需开启 `isNetworkAccessAllowed = true`）
    @discardableResult
    public func requestHighResImage(
        for asset: PHAsset,
        targetSize: CGSize = CGSize(width: 2560, height: 2560),
        progressHandler: (@MainActor @Sendable (Double) -> Void)? = nil,
        resultHandler: @escaping @MainActor (NSImage?, Bool, Error?) -> Void
    ) -> PHImageRequestID {
        let options = PHImageRequestOptions()
        options.isNetworkAccessAllowed = true
        options.deliveryMode = .opportunistic
        options.resizeMode = .exact
        if let progressHandler {
            options.progressHandler = { progress, _, _, _ in
                Task { @MainActor in
                    progressHandler(min(max(progress, 0.0), 1.0))
                }
            }
        }

        return cachingManager.requestImage(
            for: asset,
            targetSize: targetSize,
            contentMode: .aspectFit,
            options: options
        ) { [weak self] image, info in
            let isDegraded = (info?[PHImageResultIsDegradedKey] as? NSNumber)?.boolValue ?? false
            let error = info?[PHImageErrorKey] as? Error
            Task { @MainActor in
                if image != nil && !isDegraded {
                    self?.localAvailabilityCache[asset.localIdentifier] = true
                }
                resultHandler(image, isDegraded, error)
            }
        }
    }

    /// 异步加载高清大图（按需允许网络流式下载并回调进度 0.0...1.0）
    public func loadHighResImage(
        for asset: PHAsset,
        targetSize: CGSize = CGSize(width: 2560, height: 2560),
        progressHandler: (@MainActor @Sendable (Double) -> Void)? = nil
    ) async -> NSImage? {
        await withCheckedContinuation { continuation in
            let options = PHImageRequestOptions()
            options.isNetworkAccessAllowed = true
            options.deliveryMode = .highQualityFormat
            options.resizeMode = .exact
            if let progressHandler {
                options.progressHandler = { progress, _, _, _ in
                    Task { @MainActor in
                        progressHandler(min(max(progress, 0.0), 1.0))
                    }
                }
            }
            var didResume = false
            cachingManager.requestImage(
                for: asset,
                targetSize: targetSize,
                contentMode: .aspectFit,
                options: options
            ) { image, info in
                let isDegraded = (info?[PHImageResultIsDegradedKey] as? NSNumber)?.boolValue ?? false
                guard !isDegraded, !didResume else { return }
                didResume = true
                continuation.resume(returning: image)
            }
        }
    }

    /// 请求原生 PHLivePhoto（按需开启 `isNetworkAccessAllowed = true`）
    @discardableResult
    public func requestLivePhoto(
        for asset: PHAsset,
        targetSize: CGSize = CGSize(width: 2048, height: 2048),
        progressHandler: (@MainActor @Sendable (Double) -> Void)? = nil,
        resultHandler: @escaping @MainActor (PHLivePhoto?, Bool, Error?) -> Void
    ) -> PHImageRequestID {
        let options = PHLivePhotoRequestOptions()
        options.isNetworkAccessAllowed = true
        options.deliveryMode = .opportunistic
        if let progressHandler {
            options.progressHandler = { progress, _, _, _ in
                Task { @MainActor in
                    progressHandler(min(max(progress, 0.0), 1.0))
                }
            }
        }

        return cachingManager.requestLivePhoto(
            for: asset,
            targetSize: targetSize,
            contentMode: .aspectFit,
            options: options
        ) { livePhoto, info in
            let isDegraded = (info?[PHLivePhotoInfoIsDegradedKey] as? NSNumber)?.boolValue ?? false
            let error = info?[PHLivePhotoInfoErrorKey] as? Error
            Task { @MainActor in
                resultHandler(livePhoto, isDegraded, error)
            }
        }
    }

    /// 异步加载 PHLivePhoto
    public func loadLivePhoto(
        for asset: PHAsset,
        targetSize: CGSize = CGSize(width: 2048, height: 2048),
        progressHandler: (@MainActor @Sendable (Double) -> Void)? = nil
    ) async -> PHLivePhoto? {
        await withCheckedContinuation { continuation in
            let options = PHLivePhotoRequestOptions()
            options.isNetworkAccessAllowed = true
            options.deliveryMode = .highQualityFormat
            if let progressHandler {
                options.progressHandler = { progress, _, _, _ in
                    Task { @MainActor in
                        progressHandler(min(max(progress, 0.0), 1.0))
                    }
                }
            }
            var didResume = false
            cachingManager.requestLivePhoto(
                for: asset,
                targetSize: targetSize,
                contentMode: .aspectFit,
                options: options
            ) { livePhoto, info in
                let isDegraded = (info?[PHLivePhotoInfoIsDegradedKey] as? NSNumber)?.boolValue ?? false
                guard !isDegraded, !didResume else { return }
                didResume = true
                continuation.resume(returning: livePhoto)
            }
        }
    }

    /// 请求原生视频播放 AVPlayerItem（按需开启 `isNetworkAccessAllowed = true`）
    @discardableResult
    public func requestPlayerItem(
        for asset: PHAsset,
        progressHandler: (@MainActor @Sendable (Double) -> Void)? = nil,
        resultHandler: @escaping @MainActor (AVPlayerItem?, Error?) -> Void
    ) -> PHImageRequestID {
        let options = PHVideoRequestOptions()
        options.isNetworkAccessAllowed = true
        options.deliveryMode = .automatic
        if let progressHandler {
            options.progressHandler = { progress, _, _, _ in
                Task { @MainActor in
                    progressHandler(min(max(progress, 0.0), 1.0))
                }
            }
        }

        return cachingManager.requestPlayerItem(
            forVideo: asset,
            options: options
        ) { playerItem, info in
            let error = info?[PHImageErrorKey] as? Error
            Task { @MainActor in
                resultHandler(playerItem, error)
            }
        }
    }

    /// 异步加载 AVPlayerItem
    public func loadPlayerItem(
        for asset: PHAsset,
        progressHandler: (@MainActor @Sendable (Double) -> Void)? = nil
    ) async -> AVPlayerItem? {
        await withCheckedContinuation { continuation in
            var didResume = false
            _ = requestPlayerItem(for: asset, progressHandler: progressHandler) { item, _ in
                guard !didResume else { return }
                didResume = true
                continuation.resume(returning: item)
            }
        }
    }

    // MARK: - Exact File Size & EXIF Metadata Extraction (EC-6 & FR-4.3)

    public func cachedFileSize(for assetID: String) -> Int64? {
        fileSizeCache[assetID]
    }

    /// 异步读取并缓存 PHAsset 的精确文件体积（通过 PHAssetResource `fileSize` KVC，EC-6）
    @discardableResult
    public func loadFileSize(for asset: PHAsset) async -> Int64 {
        let id = asset.localIdentifier
        if let cached = fileSizeCache[id] {
            return cached
        }
        let info = await Task.detached(priority: .utility) {
            Self.extractResourceSummary(for: asset)
        }.value
        fileSizeCache[id] = info.totalByteCount
        localAvailabilityCache[id] = info.isLocallyAvailable
        return info.totalByteCount
    }

    /// 批量异步读取并缓存一组 PHAsset 的文件体积（用于待删暂存篮空间估算与大体积过滤，EC-6）
    public func loadFileSizes(for assets: [PHAsset]) async -> [String: Int64] {
        var missingAssets: [PHAsset] = []
        var result: [String: Int64] = [:]
        result.reserveCapacity(assets.count)

        for asset in assets {
            let id = asset.localIdentifier
            if let cached = fileSizeCache[id] {
                result[id] = cached
            } else {
                missingAssets.append(asset)
            }
        }

        guard !missingAssets.isEmpty else { return result }

        let fetched = await Task.detached(priority: .utility) { () -> [(String, Int64, Bool)] in
            missingAssets.map { asset in
                let summary = Self.extractResourceSummary(for: asset)
                return (asset.localIdentifier, summary.totalByteCount, summary.isLocallyAvailable)
            }
        }.value

        for (id, bytes, localAvailable) in fetched {
            fileSizeCache[id] = bytes
            localAvailabilityCache[id] = localAvailable
            result[id] = bytes
        }
        return result
    }

    /// 提取完整 AssetExifDetail（包含文件名、精确字节大小、分辨率、UTI、相机/镜头/曝光参数、缓存逆地理编码与本地可用状态）
    public func loadExifDetail(for asset: PHAsset) async -> AssetExifDetail {
        let id = asset.localIdentifier
        if let cached = exifDetailCache[id] {
            return cached
        }

        let rawInfo = await Task.detached(priority: .userInitiated) {
            Self.extractRawExifComponents(for: asset)
        }.value

        fileSizeCache[id] = rawInfo.fileSizeByteCount
        localAvailabilityCache[id] = rawInfo.isLocallyAvailable

        var locationDesc: String?
        if let location = asset.location {
            locationDesc = await resolveLocationDescription(for: location)
        }

        let detail = AssetExifDetail(
            filename: rawInfo.filename,
            fileSizeByteCount: rawInfo.fileSizeByteCount,
            pixelWidth: asset.pixelWidth,
            pixelHeight: asset.pixelHeight,
            uniformTypeIdentifier: rawInfo.uniformTypeIdentifier,
            captureDate: asset.creationDate,
            cameraMakeAndModel: rawInfo.cameraMakeAndModel,
            lensAndExposure: rawInfo.lensAndExposure,
            locationDescription: locationDesc,
            isLocallyAvailable: rawInfo.isLocallyAvailable
        )
        exifDetailCache[id] = detail
        return detail
    }

    // MARK: - Private Resource & EXIF Helpers

    private struct ResourceSummary: Sendable {
        let filename: String
        let uniformTypeIdentifier: String
        let totalByteCount: Int64
        let isLocallyAvailable: Bool
    }

    private struct RawExifComponents: Sendable {
        let filename: String
        let uniformTypeIdentifier: String
        let fileSizeByteCount: Int64
        let cameraMakeAndModel: String?
        let lensAndExposure: String?
        let isLocallyAvailable: Bool
    }

    nonisolated private static func extractResourceSummary(for asset: PHAsset) -> ResourceSummary {
        let resources = PHAssetResource.assetResources(for: asset)
        let primary = resources.first(where: {
            $0.type == .photo || $0.type == .video || $0.type == .fullSizePhoto || $0.type == .fullSizeVideo
        }) ?? resources.first

        let filename = primary?.originalFilename ?? (asset.value(forKey: "filename") as? String) ?? "未知文件"
        let uti = primary?.uniformTypeIdentifier ?? (asset.value(forKey: "uniformTypeIdentifier") as? String) ?? "public.data"

        var totalBytes: Int64 = 0
        var locallyAvailable = true

        for res in resources {
            if let sizeNum = res.value(forKey: "fileSize") as? NSNumber {
                totalBytes += sizeNum.int64Value
            }
            if let localAvailNum = res.value(forKey: "locallyAvailable") as? NSNumber {
                if !localAvailNum.boolValue && (res.type == .photo || res.type == .video) {
                    locallyAvailable = false
                }
            }
        }

        return ResourceSummary(
            filename: filename,
            uniformTypeIdentifier: uti,
            totalByteCount: totalBytes,
            isLocallyAvailable: locallyAvailable
        )
    }

    nonisolated private static func extractRawExifComponents(for asset: PHAsset) -> RawExifComponents {
        let summary = extractResourceSummary(for: asset)
        var cameraMakeAndModel: String?
        var lensAndExposure: String?
        var isLocallyAvailable = summary.isLocallyAvailable

        if asset.mediaType == .image {
            let options = PHImageRequestOptions()
            options.isNetworkAccessAllowed = false
            options.isSynchronous = true
            options.deliveryMode = .fastFormat

            PHImageManager.default().requestImageDataAndOrientation(for: asset, options: options) { data, _, _, info in
                if let inCloud = (info?[PHImageResultIsInCloudKey] as? NSNumber)?.boolValue, inCloud && data == nil {
                    isLocallyAvailable = false
                }
                guard let data,
                      let source = CGImageSourceCreateWithData(data as CFData, nil),
                      let props = CGImageSourceCopyPropertiesAtIndex(source, 0, nil) as? [CFString: Any] else {
                    return
                }

                let tiff = props[kCGImagePropertyTIFFDictionary] as? [CFString: Any]
                let exif = props[kCGImagePropertyExifDictionary] as? [CFString: Any]

                let make = (tiff?[kCGImagePropertyTIFFMake] as? String)?.trimmingCharacters(in: .whitespacesAndNewlines)
                let model = (tiff?[kCGImagePropertyTIFFModel] as? String)?.trimmingCharacters(in: .whitespacesAndNewlines)
                if let make, let model, !model.isEmpty {
                    if model.lowercased().hasPrefix(make.lowercased()) {
                        cameraMakeAndModel = model
                    } else {
                        cameraMakeAndModel = "\(make) \(model)"
                    }
                } else if let model, !model.isEmpty {
                    cameraMakeAndModel = model
                }

                var parts: [String] = []
                if let focal35 = exif?[kCGImagePropertyExifFocalLenIn35mmFilm] as? NSNumber {
                    parts.append("\(focal35.intValue)mm")
                } else if let focal = exif?[kCGImagePropertyExifFocalLength] as? NSNumber {
                    parts.append(String(format: "%.0fmm", focal.doubleValue))
                } else if let lensModel = exif?[kCGImagePropertyExifLensModel] as? String, !lensModel.isEmpty {
                    parts.append(lensModel)
                }

                if let fNumber = exif?[kCGImagePropertyExifFNumber] as? NSNumber {
                    parts.append(String(format: "f/%.2g", fNumber.doubleValue))
                }

                if let expTime = exif?[kCGImagePropertyExifExposureTime] as? NSNumber {
                    let val = expTime.doubleValue
                    if val > 0 && val < 1.0 {
                        let denom = Int((1.0 / val).rounded())
                        parts.append("1/\(max(1, denom))s")
                    } else if val >= 1.0 {
                        parts.append(String(format: "%.1fs", val))
                    }
                }

                if let isoArray = exif?[kCGImagePropertyExifISOSpeedRatings] as? [NSNumber],
                   let firstISO = isoArray.first {
                    parts.append("ISO \(firstISO.intValue)")
                }

                if !parts.isEmpty {
                    lensAndExposure = parts.joined(separator: " · ")
                }
            }
        }

        return RawExifComponents(
            filename: summary.filename,
            uniformTypeIdentifier: summary.uniformTypeIdentifier,
            fileSizeByteCount: summary.totalByteCount,
            cameraMakeAndModel: cameraMakeAndModel,
            lensAndExposure: lensAndExposure,
            isLocallyAvailable: isLocallyAvailable
        )
    }

    private func resolveLocationDescription(for location: CLLocation) async -> String {
        let lat = location.coordinate.latitude
        let lon = location.coordinate.longitude
        let cacheKey = String(format: "%.3f,%.3f", lat, lon)
        if let cached = locationDescriptionCache[cacheKey] {
            return cached
        }

        let coordinateFallback = String(format: "%.4f°, %.4f°", lat, lon)
        do {
            let placemarks = try await geocoder.reverseGeocodeLocation(location)
            if let placemark = placemarks.first {
                let components = [
                    placemark.country,
                    placemark.administrativeArea,
                    placemark.locality,
                    placemark.subLocality,
                    placemark.name
                ].compactMap { $0 }.reduce(into: [String]()) { acc, item in
                    if !acc.contains(item) {
                        acc.append(item)
                    }
                }
                if !components.isEmpty {
                    let formatted = components.suffix(3).joined(separator: " · ")
                    locationDescriptionCache[cacheKey] = formatted
                    return formatted
                }
            }
        } catch {
            // 离线或逆地理编码限流时优雅降级为经纬度展示
        }
        locationDescriptionCache[cacheKey] = coordinateFallback
        return coordinateFallback
    }
}
