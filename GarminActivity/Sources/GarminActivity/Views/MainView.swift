import SwiftUI

public struct MainView: View {
    @Bindable var viewModel: MainViewModel

    public init(viewModel: MainViewModel = MainViewModel()) {
        self.viewModel = viewModel
    }

    public var body: some View {
        Group {
            if viewModel.isAuthenticated {
                VStack(spacing: 0) {
                    FilterHeaderView(viewModel: viewModel)
                    Divider()
                    ActivityListView(viewModel: viewModel)
                    Divider()
                    DownloadFooterView(viewModel: viewModel)
                }
            } else {
                LoginView(viewModel: viewModel)
            }
        }
        .frame(minWidth: 860, minHeight: 580)
        .task {
            await viewModel.bootstrap()
        }
        .alert("所选活动不含 GPS 轨迹", isPresented: $viewModel.showMissingGPSAlert) {
            Button("改用 TCX 下载") {
                Task {
                    await viewModel.retryDownloadWithFormat(.tcx)
                }
            }
            Button("改用 FIT 下载") {
                Task {
                    await viewModel.retryDownloadWithFormat(.fit)
                }
            }
            Button("取消", role: .cancel) {
                viewModel.showMissingGPSAlert = false
            }
        } message: {
            let names = viewModel.missingGPSActivityNames.joined(separator: "、")
            if names.isEmpty {
                Text("所选活动不含 GPS 轨迹，GPX 格式无法保存无坐标数据点。建议改用 TCX 或 FIT 格式导出以保留完整心率与传感器时序数据。")
            } else {
                Text("活动「\(names)」不含 GPS 轨迹，GPX 格式无法保存无坐标数据点。是否改用 TCX 或 FIT 格式下载？")
            }
        }
    }
}
