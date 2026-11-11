import SwiftUI

/// 标准 macOS 偏好设置窗口 (`⌘,`，FR-8.2 & Task 6.2)
public struct PreferencesView: View {
    @Bindable public var appViewModel: AppViewModel

    @State private var isConfirmingResetProgress: Bool = false
    @State private var isConfirmingResetStats: Bool = false

    public init(appViewModel: AppViewModel) {
        self.appViewModel = appViewModel
    }

    public var body: some View {
        Form {
            // 1. 图库与相簿自动化设置
            Section {
                Toggle(isOn: $appViewModel.preferences.includeHiddenAssets) {
                    VStack(alignment: .leading, spacing: 2) {
                        Text("包含“已隐藏”相簿中的照片与视频")
                            .fontWeight(.medium)
                        Text("默认关闭以保护隐私。开启后，“已隐藏”资产将纳入 366 天日历索引与网格展示。")
                            .font(.caption)
                            .foregroundStyle(.secondary)
                    }
                }

                Toggle(isOn: $appViewModel.preferences.autoDeleteEmptyAlbums) {
                    VStack(alignment: .leading, spacing: 2) {
                        Text("自动即刻删除空相簿与空相簿文件夹")
                            .fontWeight(.medium)
                        Text("当启动自检、将照片移出相簿或提交待删暂存篮导致普通用户相簿变空时，立即自动删除该空相簿及连带变空的上级文件夹（含隐藏照片的相簿绝不误删）。")
                            .font(.caption)
                            .foregroundStyle(.secondary)
                    }
                }
            } header: {
                Label("图库与空相簿清理 (PhotoKit)", systemImage: "folder.badge.gearshape")
            }

            // 2. 审阅流与相似图识别设置
            Section {
                Toggle(isOn: $appViewModel.preferences.autoAdvanceAfterMarkDelete) {
                    VStack(alignment: .leading, spacing: 2) {
                        Text("大图快审标记待删后自动跳至下一张")
                            .fontWeight(.medium)
                        Text("在空格大图快审模式下按 Delete / X 将当前照片加入待删暂存篮后，自动前进到下一张照片。")
                            .font(.caption)
                            .foregroundStyle(.secondary)
                    }
                }

                Picker(selection: $appViewModel.preferences.similaritySensitivity) {
                    ForEach(SimilaritySensitivity.allCases) { mode in
                        Text(sensitivityDescription(for: mode))
                            .tag(mode)
                    }
                } label: {
                    VStack(alignment: .leading, spacing: 2) {
                        Text("当日相似照片自动识别灵敏度")
                            .fontWeight(.medium)
                        Text("仅使用本地缩略图运行 Apple Vision 特征聚类，绝不后台下载 iCloud 原图。")
                            .font(.caption)
                            .foregroundStyle(.secondary)
                    }
                }
            } header: {
                Label("快审流与 Vision 相似识别", systemImage: "sparkles.rectangle.stack")
            }

            // 3. 进度与统计数据维护
            Section {
                HStack {
                    VStack(alignment: .leading, spacing: 2) {
                        Text("366 天整理进度记录")
                            .fontWeight(.medium)
                        Text("当前已整理 \(appViewModel.completedDaysCount) / \(appViewModel.nonEmptyDaysCount) 个有照片天数")
                            .font(.caption)
                            .foregroundStyle(.secondary)
                    }

                    Spacer()

                    Button(role: .destructive) {
                        isConfirmingResetProgress = true
                    } label: {
                        Text("清空已整理进度...")
                    }
                    .disabled(appViewModel.completedDaysCount == 0 && appViewModel.hasNewDaysCount == 0)
                }

                HStack {
                    VStack(alignment: .leading, spacing: 2) {
                        Text("历史累计清理统计")
                            .fontWeight(.medium)
                        Text("累计清理 \(appViewModel.totalDeletedAssetCount) 张 · 释放 \(appViewModel.formattedTotalFreedBytes) · 清零空相簿 \(appViewModel.totalEmptyAlbumsDeleted) 个")
                            .font(.caption)
                            .foregroundStyle(.secondary)
                    }

                    Spacer()

                    Button(role: .destructive) {
                        isConfirmingResetStats = true
                    } label: {
                        Text("重置累计统计...")
                    }
                    .disabled(
                        appViewModel.totalDeletedAssetCount == 0 &&
                        appViewModel.totalFreedBytes == 0 &&
                        appViewModel.totalEmptyAlbumsDeleted == 0
                    )
                }
            } header: {
                Label("数据与进度重置", systemImage: "arrow.counterclockwise.circle")
            }
        }
        .formStyle(.grouped)
        .frame(width: 560, height: 430)
        .alert("确认清空所有“已整理”进度记录？", isPresented: $isConfirmingResetProgress) {
            Button("取消", role: .cancel) {}
            Button("确认清空", role: .destructive) {
                appViewModel.resetAllReviewProgress()
            }
        } message: {
            Text("清空后，366 天日历中所有已标记完成的日期将恢复为“待整理”状态（不会影响您的系统相册照片或待删暂存篮）。")
        }
        .alert("确认重置历史累计清理统计？", isPresented: $isConfirmingResetStats) {
            Button("取消", role: .cancel) {}
            Button("确认重置", role: .destructive) {
                appViewModel.resetLifetimeStats()
            }
        } message: {
            Text("重置后，累计清理照片数、累计释放空间与空相簿清零计数将归零。")
        }
    }

    private func sensitivityDescription(for mode: SimilaritySensitivity) -> String {
        switch mode {
        case .standard:
            return "标准 (推荐 · 距离 ≤ 0.45，4 小时内)"
        case .strict:
            return "严格 (仅极高相似 · 距离 ≤ 0.32，10 分钟内)"
        case .off:
            return "关闭自动识别"
        }
    }
}
