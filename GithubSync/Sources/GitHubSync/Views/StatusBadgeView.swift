import SwiftUI

public struct StatusBadgeView: View {
    public let mode: SyncMode
    public let status: SyncStatus
    public let errorMessage: String?

    public init(mode: SyncMode, status: SyncStatus, errorMessage: String? = nil) {
        self.mode = mode
        self.status = status
        self.errorMessage = errorMessage
    }

    public var body: some View {
        HStack(spacing: 5) {
            // 状态动画或指示图标
            switch status {
            case .syncing:
                ProgressView()
                    .controlSize(.mini)
            case .error:
                Image(systemName: "exclamationmark.circle.fill")
                    .foregroundColor(.red)
                    .font(.system(size: 11))
                    .help(errorMessage ?? "同步出错")
            case .success:
                Circle()
                    .fill(Color.green)
                    .frame(width: 6, height: 6)
            case .idle:
                Circle()
                    .fill(badgeColor.opacity(0.8))
                    .frame(width: 6, height: 6)
            case .offline:
                Image(systemName: "wifi.slash")
                    .foregroundColor(.orange)
                    .font(.system(size: 10))
            }

            // 模式文本
            Text(mode.badgeTitle)
                .font(.system(size: 11, weight: .semibold))
                .foregroundColor(badgeColor)
        }
        .padding(.horizontal, 8)
        .padding(.vertical, 3)
        .background(badgeColor.opacity(0.12))
        .cornerRadius(6)
        .overlay(
            RoundedRectangle(cornerRadius: 6)
                .stroke(badgeColor.opacity(0.25), lineWidth: 1)
        )
    }

    private var badgeColor: Color {
        switch mode {
        case .bidirectionalNewest:
            return .green
        case .localFirst:
            return .blue
        case .remoteFirst:
            return .purple
        case .stopped:
            return .secondary
        }
    }
}
