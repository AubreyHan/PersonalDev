import SwiftUI

/// 右侧检视栏底部的 EXIF 拍摄参数、体积、分辨率与本地/iCloud 存储状态面板
public struct ExifMetadataSection: View {
    public let detail: AssetExifDetail?
    public let fallbackAsset: AssetItem?
    public let selectedCount: Int
    public let totalSelectedBytes: Int64?
    public let isLoading: Bool

    @State private var isExpanded: Bool = true

    public init(
        detail: AssetExifDetail?,
        fallbackAsset: AssetItem? = nil,
        selectedCount: Int = 1,
        totalSelectedBytes: Int64? = nil,
        isLoading: Bool = false
    ) {
        self.detail = detail
        self.fallbackAsset = fallbackAsset
        self.selectedCount = selectedCount
        self.totalSelectedBytes = totalSelectedBytes
        self.isLoading = isLoading
    }

    private static let captureDateFormatter: DateFormatter = {
        let formatter = DateFormatter()
        formatter.locale = Locale(identifier: "zh_CN")
        formatter.dateFormat = "yyyy-MM-dd HH:mm:ss"
        return formatter
    }()

    public var body: some View {
        DisclosureGroup(isExpanded: $isExpanded) {
            VStack(alignment: .leading, spacing: 8) {
                if selectedCount > 1 && detail == nil {
                    multiSelectionSummaryView
                } else if let detail {
                    detailRowsView(detail)
                } else if let asset = fallbackAsset {
                    fallbackAssetView(asset)
                } else if isLoading {
                    HStack(spacing: 8) {
                        ProgressView()
                            .controlSize(.small)
                        Text("正在读取元数据...")
                            .font(.caption)
                            .foregroundStyle(.secondary)
                    }
                    .padding(.vertical, 6)
                } else {
                    Text("选中单张照片或视频以查看详细 EXIF 信息")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                        .padding(.vertical, 4)
                }
            }
            .padding(.top, 8)
        } label: {
            HStack(spacing: 6) {
                Image(systemName: "info.circle")
                    .foregroundStyle(.secondary)
                Text("EXIF 与元数据")
                    .font(.subheadline.weight(.semibold))
                Spacer()
                if isLoading {
                    ProgressView()
                        .controlSize(.mini)
                } else if let isLocal = detail?.isLocallyAvailable ?? fallbackAsset?.isLocallyAvailable {
                    availabilityBadge(isLocallyAvailable: isLocal)
                }
            }
        }
        .padding(12)
        .background(Color(nsColor: .controlBackgroundColor).opacity(0.6), in: RoundedRectangle(cornerRadius: 10))
    }

    @ViewBuilder
    private func detailRowsView(_ detail: AssetExifDetail) -> some View {
        VStack(alignment: .leading, spacing: 7) {
            metadataRow(icon: "doc.text", label: "文件名", value: detail.filename)

            if let date = detail.captureDate {
                metadataRow(
                    icon: "calendar.badge.clock",
                    label: "拍摄时间",
                    value: Self.captureDateFormatter.string(from: date)
                )
            }

            metadataRow(
                icon: "internaldrive",
                label: "文件大小",
                value: detail.fileSizeByteCount > 0 ? ByteFormatter.string(fromByteCount: detail.fileSizeByteCount) : "未知"
            )

            metadataRow(
                icon: "aspectratio",
                label: "像素尺寸",
                value: detail.dimensionsDescription
            )

            if !detail.uniformTypeIdentifier.isEmpty {
                metadataRow(
                    icon: "tag",
                    label: "格式类型",
                    value: formattedUTI(detail.uniformTypeIdentifier)
                )
            }

            if let camera = detail.cameraMakeAndModel, !camera.isEmpty {
                metadataRow(
                    icon: "camera",
                    label: "相机/设备",
                    value: camera
                )
            }

            if let lens = detail.lensAndExposure, !lens.isEmpty {
                metadataRow(
                    icon: "camera.aperture",
                    label: "镜头与曝光",
                    value: lens
                )
            }

            if let location = detail.locationDescription, !location.isEmpty {
                metadataRow(
                    icon: "location",
                    label: "拍摄地点",
                    value: location
                )
            }
        }
    }

    @ViewBuilder
    private func fallbackAssetView(_ asset: AssetItem) -> some View {
        VStack(alignment: .leading, spacing: 7) {
            if let date = asset.creationDate {
                metadataRow(
                    icon: "calendar.badge.clock",
                    label: "拍摄时间",
                    value: Self.captureDateFormatter.string(from: date)
                )
            }

            metadataRow(
                icon: "internaldrive",
                label: "文件大小",
                value: ByteFormatter.format(asset.fileSizeByteCount, placeholder: isLoading ? "计算中..." : "未缓存")
            )

            if asset.pixelWidth > 0 && asset.pixelHeight > 0 {
                metadataRow(
                    icon: "aspectratio",
                    label: "像素尺寸",
                    value: "\(asset.pixelWidth) × \(asset.pixelHeight)"
                )
            }

            if let duration = asset.formattedDuration {
                metadataRow(
                    icon: "video",
                    label: "视频时长",
                    value: duration
                )
            }
        }
    }

    @ViewBuilder
    private var multiSelectionSummaryView: some View {
        VStack(alignment: .leading, spacing: 7) {
            metadataRow(
                icon: "checkmark.circle",
                label: "已选数量",
                value: "\(selectedCount) 项资产"
            )
            if let totalSelectedBytes, totalSelectedBytes > 0 {
                metadataRow(
                    icon: "internaldrive",
                    label: "合计大小",
                    value: ByteFormatter.string(fromByteCount: totalSelectedBytes)
                )
            }
        }
    }

    private func metadataRow(icon: String, label: String, value: String) -> some View {
        HStack(alignment: .top, spacing: 8) {
            Image(systemName: icon)
                .font(.caption)
                .foregroundStyle(.secondary)
                .frame(width: 14, alignment: .center)

            Text(label)
                .font(.caption)
                .foregroundStyle(.secondary)
                .frame(width: 58, alignment: .leading)

            Text(value)
                .font(.caption.monospacedDigit())
                .foregroundStyle(.primary)
                .textSelection(.enabled)
                .lineLimit(2)
                .frame(maxWidth: .infinity, alignment: .leading)
        }
    }

    private func availabilityBadge(isLocallyAvailable: Bool) -> some View {
        HStack(spacing: 4) {
            Image(systemName: isLocallyAvailable ? "internaldrive.fill" : "icloud.and.arrow.down")
                .font(.system(size: 10))
            Text(isLocallyAvailable ? "本地原片" : "iCloud 云端")
                .font(.system(size: 10, weight: .medium))
        }
        .padding(.horizontal, 6)
        .padding(.vertical, 2)
        .foregroundStyle(isLocallyAvailable ? Color.green : Color.blue)
        .background(
            (isLocallyAvailable ? Color.green : Color.blue).opacity(0.14),
            in: Capsule()
        )
    }

    private func formattedUTI(_ uti: String) -> String {
        let lower = uti.lowercased()
        if lower.contains("heic") { return "HEIC (\(uti))" }
        if lower.contains("heif") { return "HEIF (\(uti))" }
        if lower.contains("jpeg") || lower.contains("jpg") { return "JPEG (\(uti))" }
        if lower.contains("png") { return "PNG (\(uti))" }
        if lower.contains("raw") || lower.contains("dng") { return "RAW (\(uti))" }
        if lower.contains("quicktime") { return "MOV (\(uti))" }
        if lower.contains("mpeg-4") || lower.contains("mp4") { return "MP4 (\(uti))" }
        return uti
    }
}
