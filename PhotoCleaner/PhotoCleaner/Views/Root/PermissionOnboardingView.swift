import Photos
import SwiftUI

/// 照片图库权限引导页（FR-1.2：当权限为 `.notDetermined`、`.denied`、`.restricted` 或 `.limited` 时展示引导与系统设置跳转）
public struct PermissionOnboardingView: View {
    public let appViewModel: AppViewModel
    public var onContinueWithLimitedAccess: (() -> Void)?

    @State private var isRequesting: Bool = false

    public init(
        appViewModel: AppViewModel,
        onContinueWithLimitedAccess: (() -> Void)? = nil
    ) {
        self.appViewModel = appViewModel
        self.onContinueWithLimitedAccess = onContinueWithLimitedAccess
    }

    private var status: PHAuthorizationStatus {
        appViewModel.authorizationStatus
    }

    public var body: some View {
        VStack(spacing: 28) {
            Spacer()

            // Hero Icon
            ZStack {
                Circle()
                    .fill(
                        LinearGradient(
                            colors: [Color.accentColor.opacity(0.22), Color.cyan.opacity(0.12)],
                            startPoint: .topLeading,
                            endPoint: .bottomTrailing
                        )
                    )
                    .frame(width: 96, height: 96)

                Image(systemName: statusIconName)
                    .font(.system(size: 42, weight: .semibold))
                    .foregroundStyle(statusIconTint)
            }

            // Title & Description
            VStack(spacing: 10) {
                Text(titleText)
                    .font(.system(size: 24, weight: .bold))
                    .foregroundStyle(.primary)

                Text(subtitleText)
                    .font(.body)
                    .foregroundStyle(.secondary)
                    .multilineTextAlignment(.center)
                    .frame(maxWidth: 520)
            }

            // Value Propositions Card
            VStack(alignment: .leading, spacing: 14) {
                featureRow(
                    icon: "calendar.badge.checkmark",
                    title: "366 天跨年份日历切片整理",
                    detail: "按月-日聚合历年同日照片与视频，逐日攻克，进度自动记忆"
                )
                featureRow(
                    icon: "folder.badge.gearshape",
                    title: "相簿归属透明可视与空相簿自动清零",
                    detail: "一键查看/调整所属相簿，当相簿或文件夹变空时即刻自动清理"
                )
                featureRow(
                    icon: "lock.shield",
                    title: "100% 本地端侧运行 & 收藏 ❤️ 防误删锁定",
                    detail: "严格遵循官方 PhotoKit API，所有删除均安全移入系统“最近删除”"
                )
            }
            .padding(20)
            .frame(maxWidth: 520)
            .background(
                RoundedRectangle(cornerRadius: 14)
                    .fill(Color(nsColor: .controlBackgroundColor).opacity(0.75))
            )
            .overlay(
                RoundedRectangle(cornerRadius: 14)
                    .strokeBorder(Color.secondary.opacity(0.18), lineWidth: 1)
            )

            // Primary & Secondary CTAs
            actionButtonsSection

            Spacer()
        }
        .padding(36)
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .background(Color(nsColor: .windowBackgroundColor))
    }

    @ViewBuilder
    private var actionButtonsSection: some View {
        switch status {
        case .notDetermined:
            Button {
                requestAccess()
            } label: {
                HStack(spacing: 8) {
                    if isRequesting {
                        ProgressView()
                            .controlSize(.small)
                    } else {
                        Image(systemName: "photo.badge.checkmark")
                    }
                    Text(isRequesting ? "正在请求授权..." : "授权访问系统照片图库")
                        .fontWeight(.semibold)
                }
                .padding(.horizontal, 18)
                .padding(.vertical, 6)
            }
            .buttonStyle(.borderedProminent)
            .controlSize(.large)
            .disabled(isRequesting)

        case .denied, .restricted:
            VStack(spacing: 12) {
                HStack(spacing: 12) {
                    Button {
                        appViewModel.permissionService.openSystemSettings()
                    } label: {
                        Label("打开系统设置 -> 隐私与安全性 -> 照片", systemImage: "gearshape.fill")
                            .fontWeight(.semibold)
                            .padding(.horizontal, 12)
                            .padding(.vertical, 4)
                    }
                    .buttonStyle(.borderedProminent)
                    .controlSize(.large)

                    Button {
                        recheckAuthorization()
                    } label: {
                        Label("重新检查权限", systemImage: "arrow.clockwise")
                            .padding(.horizontal, 10)
                            .padding(.vertical, 4)
                    }
                    .buttonStyle(.bordered)
                    .controlSize(.large)
                }

                Text("在系统设置中勾选「PhotoCleaner」并授予“完全访问权限”后，点击“重新检查权限”即可进入。")
                    .font(.caption)
                    .foregroundStyle(.tertiary)
            }

        case .limited:
            VStack(spacing: 12) {
                HStack(spacing: 12) {
                    Button {
                        appViewModel.permissionService.openSystemSettings()
                    } label: {
                        Label("前往系统设置开启完整图库访问", systemImage: "gearshape.fill")
                            .fontWeight(.semibold)
                            .padding(.horizontal, 12)
                            .padding(.vertical, 4)
                    }
                    .buttonStyle(.borderedProminent)
                    .controlSize(.large)

                    Button {
                        if let onContinueWithLimitedAccess {
                            onContinueWithLimitedAccess()
                        } else {
                            Task { @MainActor in
                                await appViewModel.bootstrap()
                            }
                        }
                    } label: {
                        Text("以受限模式继续")
                            .padding(.horizontal, 10)
                            .padding(.vertical, 4)
                    }
                    .buttonStyle(.bordered)
                    .controlSize(.large)
                }

                Text("提示：受限访问模式下无法扫描全部用户相簿与执行空相簿自动清理，建议授予完整访问权限。")
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }

        case .authorized:
            Button {
                Task { @MainActor in
                    await appViewModel.bootstrap()
                }
            } label: {
                Label("进入 PhotoCleaner", systemImage: "arrow.right.circle.fill")
                    .fontWeight(.semibold)
                    .padding(.horizontal, 16)
                    .padding(.vertical, 4)
            }
            .buttonStyle(.borderedProminent)
            .controlSize(.large)

        @unknown default:
            EmptyView()
        }
    }

    private func featureRow(icon: String, title: String, detail: String) -> some View {
        HStack(alignment: .top, spacing: 12) {
            Image(systemName: icon)
                .font(.system(size: 18, weight: .semibold))
                .foregroundStyle(Color.accentColor)
                .frame(width: 26, height: 26)

            VStack(alignment: .leading, spacing: 3) {
                Text(title)
                    .font(.subheadline.weight(.semibold))
                    .foregroundStyle(.primary)
                Text(detail)
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }
        }
    }

    private var statusIconName: String {
        switch status {
        case .notDetermined:
            return "photo.on.rectangle.angled"
        case .denied, .restricted:
            return "lock.trianglebadge.exclamationmark.fill"
        case .limited:
            return "exclamationmark.shield.fill"
        case .authorized:
            return "checkmark.seal.fill"
        @unknown default:
            return "photo"
        }
    }

    private var statusIconTint: Color {
        switch status {
        case .notDetermined:
            return .accentColor
        case .denied, .restricted:
            return .orange
        case .limited:
            return .yellow
        case .authorized:
            return .green
        @unknown default:
            return .accentColor
        }
    }

    private var titleText: String {
        switch status {
        case .notDetermined:
            return "欢迎使用 PhotoCleaner"
        case .denied, .restricted:
            return "需要系统照片图库访问权限"
        case .limited:
            return "当前处于受限照片访问模式"
        case .authorized:
            return "照片图库权限已就绪"
        @unknown default:
            return "照片图库访问授权"
        }
    }

    private var subtitleText: String {
        switch status {
        case .notDetermined:
            return "PhotoCleaner 需要访问您的系统照片图库，以便按 366 天日历切片整理照片、调整相簿归属并将待清理项目移入“最近删除”。"
        case .denied, .restricted:
            return "您尚未授予照片图库访问权限。请前往 macOS「系统设置 -> 隐私与安全性 -> 照片」，为 PhotoCleaner 开启完全访问权限。"
        case .limited:
            return "为确保 366 天全景索引、相簿归属双向管理与空相簿自动清零功能完整运行，建议在系统设置中开启“完全访问权限”。"
        case .authorized:
            return "已完成系统照片图库授权，点击下方按钮立即开始按日整理。"
        @unknown default:
            return "请授予系统照片图库读写权限以继续使用。"
        }
    }

    private func requestAccess() {
        guard !isRequesting else { return }
        isRequesting = true
        Task { @MainActor in
            _ = await appViewModel.permissionService.requestAuthorization()
            isRequesting = false
            if appViewModel.permissionService.isAuthorized || appViewModel.permissionService.isLimited {
                await appViewModel.bootstrap()
            }
        }
    }

    private func recheckAuthorization() {
        let updated = appViewModel.permissionService.refreshStatus()
        if updated == .authorized || updated == .limited {
            Task { @MainActor in
                await appViewModel.bootstrap()
            }
        }
    }
}
