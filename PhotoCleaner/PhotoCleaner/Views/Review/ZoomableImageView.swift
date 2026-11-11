import SwiftUI
import AppKit

/// 支持双指缩放、双击 100%/自适应切换与拖拽平移的高清图像检视器
public struct ZoomableImageView: View {
    public let image: NSImage?
    public let isLoading: Bool
    public let downloadProgress: Double?

    @State private var currentScale: CGFloat = 1.0
    @State private var gestureScale: CGFloat = 1.0
    @State private var currentOffset: CGSize = .zero
    @State private var gestureOffset: CGSize = .zero

    private let minScale: CGFloat = 1.0
    private let maxScale: CGFloat = 6.0

    public init(
        image: NSImage?,
        isLoading: Bool = false,
        downloadProgress: Double? = nil
    ) {
        self.image = image
        self.isLoading = isLoading
        self.downloadProgress = downloadProgress
    }

    private var effectiveScale: CGFloat {
        min(max(currentScale * gestureScale, minScale), maxScale)
    }

    private var effectiveOffset: CGSize {
        guard effectiveScale > 1.01 else { return .zero }
        return CGSize(
            width: currentOffset.width + gestureOffset.width,
            height: currentOffset.height + gestureOffset.height
        )
    }

    public var body: some View {
        GeometryReader { proxy in
            ZStack {
                Color.black.opacity(0.92)
                    .ignoresSafeArea()

                if let image {
                    Image(nsImage: image)
                        .resizable()
                        .aspectRatio(contentMode: .fit)
                        .scaleEffect(effectiveScale)
                        .offset(effectiveOffset)
                        .frame(width: proxy.size.width, height: proxy.size.height)
                        .contentShape(Rectangle())
                        .onTapGesture(count: 2) {
                            withAnimation(.spring(response: 0.25, dampingFraction: 0.82)) {
                                if currentScale > 1.05 {
                                    resetTransform()
                                } else {
                                    currentScale = 2.5
                                }
                            }
                        }
                        .gesture(magnificationGesture)
                        .simultaneousGesture(panGesture(in: proxy.size))
                } else if isLoading {
                    VStack(spacing: 12) {
                        if let progress = downloadProgress, progress > 0 && progress < 1.0 {
                            ProgressView(value: progress)
                                .progressViewStyle(.circular)
                                .controlSize(.large)
                            Text("正在从 iCloud 加载高清原图 \(Int(progress * 100))%...")
                                .font(.caption)
                                .foregroundStyle(.secondary)
                        } else {
                            ProgressView()
                                .controlSize(.large)
                            Text("加载高清预览中...")
                                .font(.caption)
                                .foregroundStyle(.secondary)
                        }
                    }
                } else {
                    VStack(spacing: 8) {
                        Image(systemName: "photo.badge.exclamationmark")
                            .font(.system(size: 36))
                            .foregroundStyle(.secondary)
                        Text("无法加载图像")
                            .font(.callout)
                            .foregroundStyle(.secondary)
                    }
                }

                // 顶部右侧或底部浮动缩放比例指示器（放大时显示）
                if effectiveScale > 1.05 {
                    VStack {
                        Spacer()
                        HStack {
                            Spacer()
                            Button {
                                withAnimation(.spring(response: 0.22, dampingFraction: 0.85)) {
                                    resetTransform()
                                }
                            } label: {
                                HStack(spacing: 4) {
                                    Image(systemName: "arrow.up.left.and.arrow.down.right")
                                    Text("\(Int(effectiveScale * 100))% · 重置")
                                }
                                .font(.caption.monospacedDigit())
                                .padding(.horizontal, 10)
                                .padding(.vertical, 5)
                                .background(.ultraThinMaterial, in: Capsule())
                            }
                            .buttonStyle(.plain)
                            .padding(16)
                        }
                    }
                }
            }
            .clipped()
            .onChange(of: image) { _, _ in
                resetTransform()
            }
        }
    }

    private var magnificationGesture: some Gesture {
        MagnificationGesture()
            .onChanged { value in
                gestureScale = value
            }
            .onEnded { value in
                let newScale = min(max(currentScale * value, minScale), maxScale)
                currentScale = newScale
                gestureScale = 1.0
                if newScale <= 1.01 {
                    currentOffset = .zero
                    gestureOffset = .zero
                }
            }
    }

    private func panGesture(in containerSize: CGSize) -> some Gesture {
        DragGesture(minimumDistance: 4)
            .onChanged { value in
                guard effectiveScale > 1.01 else { return }
                gestureOffset = value.translation
            }
            .onEnded { value in
                guard effectiveScale > 1.01 else {
                    currentOffset = .zero
                    gestureOffset = .zero
                    return
                }
                let maxPanX = max((containerSize.width * (effectiveScale - 1.0)) / 2.0, 0)
                let maxPanY = max((containerSize.height * (effectiveScale - 1.0)) / 2.0, 0)
                let rawX = currentOffset.width + value.translation.width
                let rawY = currentOffset.height + value.translation.height
                currentOffset = CGSize(
                    width: min(max(rawX, -maxPanX), maxPanX),
                    height: min(max(rawY, -maxPanY), maxPanY)
                )
                gestureOffset = .zero
            }
    }

    private func resetTransform() {
        currentScale = 1.0
        gestureScale = 1.0
        currentOffset = .zero
        gestureOffset = .zero
    }
}
