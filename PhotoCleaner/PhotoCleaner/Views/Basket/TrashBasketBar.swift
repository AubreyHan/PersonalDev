import SwiftUI

/// 底部常驻全局待删暂存篮状态栏与一键提交按钮（FR-7.2 ~ FR-7.3）
public struct TrashBasketBar: View {
    @Bindable public var trashBasketVM: TrashBasketViewModel

    public init(trashBasketVM: TrashBasketViewModel) {
        self.trashBasketVM = trashBasketVM
    }

    public init(viewModel: TrashBasketViewModel) {
        self.trashBasketVM = viewModel
    }

    public init(appViewModel: AppViewModel) {
        self.trashBasketVM = appViewModel.trashBasketVM
    }

    public var body: some View {
        HStack(spacing: 14) {
            HStack(spacing: 8) {
                ZStack {
                    Circle()
                        .fill(trashBasketVM.isEmpty ? Color.secondary.opacity(0.15) : Color.red.opacity(0.16))
                        .frame(width: 28, height: 28)
                    Image(systemName: trashBasketVM.isEmpty ? "trash" : "trash.fill")
                        .font(.system(size: 13, weight: .semibold))
                        .foregroundStyle(trashBasketVM.isEmpty ? Color.secondary : Color.red)
                }

                VStack(alignment: .leading, spacing: 1) {
                    Text(trashBasketVM.statusSummaryText)
                        .font(.subheadline.monospacedDigit().weight(.semibold))
                        .foregroundStyle(trashBasketVM.isEmpty ? .secondary : .primary)
                    Text("跨日期自动累积保存 · 收藏 ❤️ 照片已锁定保护")
                        .font(.caption2)
                        .foregroundStyle(.tertiary)
                }
            }

            Spacer()

            Button {
                _ = trashBasketVM.undoLastAction()
            } label: {
                HStack(spacing: 4) {
                    Image(systemName: "arrow.uturn.backward")
                    Text("撤销")
                    Text("⌘Z")
                        .font(.caption2.monospaced())
                        .foregroundStyle(.secondary)
                }
            }
            .buttonStyle(.bordered)
            .controlSize(.small)
            .disabled(!trashBasketVM.canUndo)
            .help("撤销最近一次加入或移出待删暂存篮操作 (⌘Z)")

            Button {
                trashBasketVM.isDrawerPresented.toggle()
            } label: {
                HStack(spacing: 4) {
                    Image(systemName: trashBasketVM.isDrawerPresented ? "chevron.down" : "chevron.up")
                    Text("展开明细")
                }
            }
            .buttonStyle(.bordered)
            .controlSize(.small)
            .disabled(trashBasketVM.isEmpty && !trashBasketVM.isDrawerPresented)

            Button(role: .destructive) {
                Task { @MainActor in
                    _ = await trashBasketVM.commitDeletions()
                }
            } label: {
                HStack(spacing: 6) {
                    if trashBasketVM.isCommitting {
                        ProgressView()
                            .controlSize(.small)
                    } else {
                        Image(systemName: "trash.slash.fill")
                    }
                    Text("提交清理 (\(trashBasketVM.count))")
                        .fontWeight(.semibold)
                    Text("⌘↩")
                        .font(.caption2.monospaced())
                        .opacity(0.85)
                }
                .padding(.horizontal, 4)
            }
            .buttonStyle(.borderedProminent)
            .tint(.red)
            .controlSize(.regular)
            .disabled(trashBasketVM.isEmpty || trashBasketVM.isCommitting)
            .help("批量移入系统相册「最近删除」并自动清理变空的相簿 (⌘Return)")
        }
        .padding(.horizontal, 16)
        .padding(.vertical, 10)
        .background(.regularMaterial)
        .overlay(alignment: .top) {
            Divider()
        }
    }
}
