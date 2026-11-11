import SwiftUI

public struct LoginView: View {
    @Bindable var viewModel: MainViewModel

    public init(viewModel: MainViewModel) {
        self.viewModel = viewModel
    }

    public var body: some View {
        VStack(spacing: 20) {
            Image(systemName: "figure.run.circle.fill")
                .resizable()
                .scaledToFit()
                .frame(width: 56, height: 56)
                .foregroundStyle(.tint)

            VStack(spacing: 6) {
                Text("Garmin Activity Exporter")
                    .font(.title2)
                    .fontWeight(.semibold)

                Text("请输入 Garmin Connect 个人 API Key，系统将自动探测国际区 (.com) 或中国区 (.cn)")
                    .font(.subheadline)
                    .foregroundStyle(.secondary)
                    .multilineTextAlignment(.center)
            }

            VStack(alignment: .leading, spacing: 8) {
                Text("Personal API Key")
                    .font(.caption)
                    .fontWeight(.medium)
                    .foregroundStyle(.secondary)

                TextField("粘贴您的 Personal API Key...", text: $viewModel.apiKeyInput)
                    .textFieldStyle(.roundedBorder)
                    .frame(width: 420)
                    .disabled(viewModel.isAuthenticating)
                    .onSubmit {
                        guard !viewModel.isAuthenticating else { return }
                        Task {
                            await viewModel.login()
                        }
                    }
            }

            if let errorMessage = viewModel.authErrorMessage, !errorMessage.isEmpty {
                HStack(spacing: 6) {
                    Image(systemName: "exclamationmark.triangle.fill")
                        .foregroundStyle(.red)
                    Text(errorMessage)
                        .font(.callout)
                        .foregroundStyle(.red)
                }
                .frame(maxWidth: 420, alignment: .leading)
            }

            Button {
                Task {
                    await viewModel.login()
                }
            } label: {
                HStack(spacing: 8) {
                    if viewModel.isAuthenticating {
                        ProgressView()
                            .controlSize(.small)
                    }
                    Text(viewModel.isAuthenticating ? "正在验证并探测区域..." : "连接并自动识别区域")
                        .frame(minWidth: 160)
                }
            }
            .buttonStyle(.borderedProminent)
            .controlSize(.large)
            .disabled(
                viewModel.isAuthenticating ||
                viewModel.apiKeyInput.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty
            )

            HStack(spacing: 16) {
                Button {
                    viewModel.openPersonalApiKeyPage(for: .global)
                } label: {
                    Label("打开国际区获取 Key (garmin.com)", systemImage: "safari")
                        .font(.caption)
                }
                .buttonStyle(.link)

                Text("·")
                    .foregroundStyle(.tertiary)

                Button {
                    viewModel.openPersonalApiKeyPage(for: .china)
                } label: {
                    Label("打开中国区获取 Key (garmin.cn)", systemImage: "safari")
                        .font(.caption)
                }
                .buttonStyle(.link)
            }
            .padding(.top, 4)
        }
        .padding(40)
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }
}
