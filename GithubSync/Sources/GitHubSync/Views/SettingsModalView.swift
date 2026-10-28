import SwiftUI

public struct SettingsModalView: View {
    @ObservedObject var appState: AppState
    @Environment(\.dismiss) private var dismiss

    @State private var debounceSeconds: Int = 5
    @State private var remotePollMinutes: Int = 5
    @State private var launchAtStartup: Bool = false
    @State private var showConfirmReset: Bool = false

    public var body: some View {
        VStack(alignment: .leading, spacing: 18) {
            // 标题
            HStack {
                Text("偏好设置")
                    .font(.system(size: 16, weight: .bold))
                Spacer()
                Button {
                    dismiss()
                } label: {
                    Image(systemName: "xmark.circle.fill")
                        .foregroundColor(.secondary)
                        .font(.system(size: 16))
                }
                .buttonStyle(.plain)
            }

            Divider()

            // 自动化频率设置
            VStack(alignment: .leading, spacing: 12) {
                Text("同步调度引擎")
                    .font(.system(size: 13, weight: .semibold))

                HStack {
                    Text("本地变动防抖等待：")
                        .font(.system(size: 12))
                    Spacer()
                    Stepper("\(debounceSeconds) 秒", value: $debounceSeconds, in: 2...60)
                        .font(.system(size: 12))
                }

                HStack {
                    Text("远端定时检查周期：")
                        .font(.system(size: 12))
                    Spacer()
                    Stepper("\(remotePollMinutes) 分钟", value: $remotePollMinutes, in: 1...60)
                        .font(.system(size: 12))
                }
            }
            .padding(12)
            .background(Color(NSColor.controlBackgroundColor))
            .cornerRadius(8)

            // 系统级开关
            VStack(alignment: .leading, spacing: 12) {
                Text("系统集成")
                    .font(.system(size: 13, weight: .semibold))

                Toggle("登录时开机自动启动", isOn: $launchAtStartup)
                    .font(.system(size: 12))
                    .onChange(of: launchAtStartup) { newValue in
                        _ = AutoStartManager.shared.setEnabled(newValue)
                    }
            }
            .padding(12)
            .background(Color(NSColor.controlBackgroundColor))
            .cornerRadius(8)

            // 彻底注销与安全清理
            VStack(alignment: .leading, spacing: 8) {
                Text("彻底注销与数据清理")
                    .font(.system(size: 13, weight: .semibold))
                    .foregroundColor(.red)

                Text("一键停止所有监听服务，注销系统开机项并抹除本地配置缓存，安全退出应用，无任何残留。")
                    .font(.system(size: 11))
                    .foregroundColor(.secondary)

                Button(role: .destructive) {
                    showConfirmReset = true
                } label: {
                    HStack {
                        Image(systemName: "trash")
                        Text("彻底注销并退出应用")
                    }
                }
                .controlSize(.regular)
                .padding(.top, 4)
            }
            .padding(12)
            .background(Color.red.opacity(0.06))
            .cornerRadius(8)
            .overlay(
                RoundedRectangle(cornerRadius: 8)
                    .stroke(Color.red.opacity(0.2), lineWidth: 1)
            )

            Spacer()

            // 底部保存按钮
            HStack {
                Spacer()
                Button("完成") {
                    var s = appState.settings
                    s.debounceSeconds = debounceSeconds
                    s.remotePollIntervalMinutes = remotePollMinutes
                    s.launchAtStartup = launchAtStartup
                    appState.updateSettings(s)
                    dismiss()
                }
                .buttonStyle(.borderedProminent)
                .controlSize(.regular)
            }
        }
        .padding(20)
        .frame(width: 440, height: 430)
        .onAppear {
            debounceSeconds = appState.settings.debounceSeconds
            remotePollMinutes = appState.settings.remotePollIntervalMinutes
            launchAtStartup = AutoStartManager.shared.isEnabled
        }
        .confirmationDialog(
            "确定要彻底注销并退出吗？",
            isPresented: $showConfirmReset,
            titleVisibility: .visible
        ) {
            Button("彻底注销并退出", role: .destructive) {
                appState.factoryResetAndExit()
            }
            Button("取消", role: .cancel) {}
        } message: {
            Text("此操作将注销开机自启动项、停止所有 Git 同步服务并清除所有监控配置。本地代码文件不会受到任何影响。")
        }
    }
}
