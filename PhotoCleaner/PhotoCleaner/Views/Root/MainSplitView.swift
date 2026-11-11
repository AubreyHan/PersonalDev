import Photos
import SwiftUI

/// 三栏主体框架：左侧 `Calendar366Sidebar`、中间工作区（`YearSectionGridView` / `QuickReviewContainer` + 底部 `TrashBasketBar`）、右侧 `RightInspectorColumn`、顶部浮动非阻塞 Toast 提示与弹窗挂载（ARCHITECTURE §1.2, §2）
public struct MainSplitView: View {
    @Bindable public var appViewModel: AppViewModel

    @State private var columnVisibility: NavigationSplitViewVisibility = .all
    @State private var didBypassLimitedOnboarding: Bool = false

    public init(appViewModel: AppViewModel) {
        self.appViewModel = appViewModel
    }

    private var dayVM: DaySliceViewModel {
        appViewModel.daySliceVM
    }

    private var basketVM: TrashBasketViewModel {
        appViewModel.trashBasketVM
    }

    private var similarityVM: SimilarityCompareViewModel {
        appViewModel.similarityVM
    }

    private var shouldShowPermissionOnboarding: Bool {
        let status = appViewModel.authorizationStatus
        switch status {
        case .authorized:
            return false
        case .limited:
            return !didBypassLimitedOnboarding && !appViewModel.hasPerformedColdStartLoad
        case .notDetermined, .denied, .restricted:
            return true
        @unknown default:
            return true
        }
    }

    public var body: some View {
        ZStack(alignment: .top) {
            if shouldShowPermissionOnboarding {
                PermissionOnboardingView(
                    appViewModel: appViewModel,
                    onContinueWithLimitedAccess: {
                        didBypassLimitedOnboarding = true
                        Task { @MainActor in
                            await appViewModel.bootstrap()
                        }
                    }
                )
            } else {
                NavigationSplitView(columnVisibility: $columnVisibility) {
                    Calendar366Sidebar(appViewModel: appViewModel)
                        .navigationSplitViewColumnWidth(min: 256, ideal: 276, max: 320)
                } detail: {
                    HStack(spacing: 0) {
                        // 中央主工作区（顶部过滤栏 + 年份网格/大图快审 + 底部待删暂存篮栏）
                        centerWorkspaceColumn
                            .frame(maxWidth: .infinity, maxHeight: .infinity)

                        // 右侧相簿归属与 EXIF 检视栏（快捷键 `I` 显隐）
                        if dayVM.isInspectorVisible {
                            Divider()
                            RightInspectorColumn(appViewModel: appViewModel)
                                .transition(.move(edge: .trailing).combined(with: .opacity))
                        }
                    }
                }
                .navigationTitle("PhotoCleaner · \(dayVM.selectedMonthDay.displayTitle)")
            }

            // 顶部浮动非阻塞 Toast 提示条（如收藏 ❤️ 自动跳过提示、空相簿自动清零提示等）
            if let toast = appViewModel.activeToast {
                toastBannerOverlay(for: toast)
                    .padding(.top, 14)
                    .transition(.move(edge: .top).combined(with: .opacity))
                    .zIndex(100)
            }
        }
        .animation(.spring(response: 0.25, dampingFraction: 0.86), value: appViewModel.activeToast)
        .animation(.easeInOut(duration: 0.18), value: dayVM.isInspectorVisible)
        .sheet(
            isPresented: Binding(
                get: { similarityVM.isComparePresented },
                set: { isPresented in
                    if !isPresented {
                        similarityVM.closeCompare()
                    }
                }
            )
        ) {
            SimilarityCompareSheet(appViewModel: appViewModel)
                .frame(minWidth: 940, idealWidth: 1080, minHeight: 640, idealHeight: 740)
        }
        .sheet(
            isPresented: Binding(
                get: { basketVM.isDrawerPresented },
                set: { basketVM.isDrawerPresented = $0 }
            )
        ) {
            TrashBasketDrawerView(appViewModel: appViewModel)
                .frame(minWidth: 720, idealWidth: 820, minHeight: 480, idealHeight: 560)
        }
        .keyboardShortcutMonitor { action in
            appViewModel.handleShortcut(action)
        }
        .task {
            if appViewModel.authorizationStatus == .authorized || appViewModel.authorizationStatus == .limited {
                await appViewModel.performInitialLoadIfNeeded()
            }
        }
    }

    // MARK: - Center Workspace Column

    private var centerWorkspaceColumn: some View {
        VStack(spacing: 0) {
            switch dayVM.workspaceMode {
            case .grid:
                DayHeaderFilterBar(appViewModel: appViewModel)

                if appViewModel.isBootstrapping && !appViewModel.hasPerformedColdStartLoad {
                    bootstrappingLoadingView
                } else {
                    YearSectionGridView(appViewModel: appViewModel)
                }

            case .quickReview:
                QuickReviewContainer(appViewModel: appViewModel)
            }

            // 底部常驻全局待删暂存篮栏
            Divider()
            TrashBasketBar(appViewModel: appViewModel)
        }
    }

    private var bootstrappingLoadingView: some View {
        VStack(spacing: 14) {
            Spacer()
            ProgressView()
                .controlSize(.large)
            Text("正在扫描系统照片图库并构建 366 天日历与相簿归属索引...")
                .font(.subheadline)
                .foregroundStyle(.secondary)
            Spacer()
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }

    // MARK: - Non-Blocking Floating Toast Banner

    private func toastBannerOverlay(for toast: ToastMessage) -> some View {
        HStack(spacing: 10) {
            Image(systemName: toast.kind.systemImage)
                .font(.system(size: 14, weight: .semibold))
                .foregroundStyle(toastTintColor(for: toast.kind))

            Text(toast.message)
                .font(.subheadline.weight(.medium))
                .foregroundStyle(.primary)
                .lineLimit(2)

            Button {
                appViewModel.dismissToast(id: toast.id)
            } label: {
                Image(systemName: "xmark")
                    .font(.caption2.weight(.bold))
                    .foregroundStyle(.secondary)
            }
            .buttonStyle(.plain)
        }
        .padding(.horizontal, 16)
        .padding(.vertical, 10)
        .background(.ultraThickMaterial, in: Capsule())
        .overlay(
            Capsule()
                .strokeBorder(toastTintColor(for: toast.kind).opacity(0.45), lineWidth: 1)
        )
        .shadow(color: Color.black.opacity(0.18), radius: 10, y: 4)
    }

    private func toastTintColor(for kind: ToastKind) -> Color {
        switch kind {
        case .info:
            return .accentColor
        case .success:
            return .green
        case .warning:
            return .orange
        case .error:
            return .red
        }
    }
}
