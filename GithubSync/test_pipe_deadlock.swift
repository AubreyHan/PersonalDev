import Foundation

print("=== 验证 Pipe 并发排空与大容量输出防死锁测试 ===")

// 模拟输出超 100KB 数据的命令 (超过 64KB Pipe 缓冲区)
let process = Process()
process.executableURL = URL(fileURLWithPath: "/bin/sh")
process.arguments = ["-c", "python3 -c 'print(\"A\" * 100000)'"]

let stdoutPipe = Pipe()
let stderrPipe = Pipe()
process.standardOutput = stdoutPipe
process.standardError = stderrPipe

try process.run()

var stdoutData = Data()
var stderrData = Data()
let readGroup = DispatchGroup()

readGroup.enter()
DispatchQueue.global(qos: .userInitiated).async {
    stdoutData = stdoutPipe.fileHandleForReading.readDataToEndOfFile()
    readGroup.leave()
}

readGroup.enter()
DispatchQueue.global(qos: .userInitiated).async {
    stderrData = stderrPipe.fileHandleForReading.readDataToEndOfFile()
    readGroup.leave()
}

readGroup.wait()
process.waitUntilExit()

assert(stdoutData.count >= 100000, "stdout 读取不完整: \(stdoutData.count)")
assert(process.terminationStatus == 0, "进程未正常退出")
print("✅ 并发排空测试通过！成功无阻塞读取 \(stdoutData.count) 字节数据（超 64KB 限制）")

print("=== 验证超时控制机制 ===")
let sleepProcess = Process()
sleepProcess.executableURL = URL(fileURLWithPath: "/bin/sleep")
sleepProcess.arguments = ["10"]

let timer = DispatchSource.makeTimerSource(queue: DispatchQueue.global(qos: .utility))
timer.schedule(deadline: .now() + 1.0)
var didTimeout = false
timer.setEventHandler {
    didTimeout = true
    if sleepProcess.isRunning {
        sleepProcess.terminate()
    }
}
timer.resume()

try sleepProcess.run()
sleepProcess.waitUntilExit()
timer.cancel()

assert(didTimeout, "未能触发超时")
print("✅ 超时终止保护机制测试通过！进程被及时强制终止")
