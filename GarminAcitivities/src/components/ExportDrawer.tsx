import React from "react";
import { useExportStore } from "../store/exportStore";
import { CheckCircle2, FolderOpen, Pause, Play, RotateCcw, X, XCircle } from "lucide-react";
import { revealItemInDir } from "@tauri-apps/plugin-opener";

export const ExportDrawer: React.FC = () => {
  const {
    isDrawerOpen,
    closeDrawer,
    tasks,
    summary,
    pauseQueue,
    resumeQueue,
    cancelQueue,
  } = useExportStore();

  if (!isDrawerOpen) return null;

  const taskList = Object.values(tasks);
  const total = summary?.totalTasks || taskList.length || 1;
  const completed = summary?.completedTasks || taskList.filter((t) => t.status === "saved").length;
  const failed = summary?.failedTasks || taskList.filter((t) => t.status === "failed").length;
  const percent = Math.min(100, Math.round(((completed + failed) / total) * 100));
  const isPaused = summary?.isPaused ?? false;

  const handleReveal = async (filePath?: string) => {
    if (!filePath) return;
    try {
      await revealItemInDir(filePath);
    } catch (e) {
      console.warn("Reveal failed:", e);
    }
  };

  return (
    <div className="fixed inset-y-0 right-0 z-40 w-96 bg-[#1a1a1a] border-l border-gray-800 shadow-2xl flex flex-col animate-in slide-in-from-right duration-200">
      {/* Header */}
      <div className="p-4 border-b border-gray-800 flex items-center justify-between bg-[#1f1f1f]">
        <div>
          <h3 className="font-bold text-sm text-white">导出管理任务队列</h3>
          <p className="text-[11px] text-gray-400 mt-0.5">并发限制: 2 · 带防封禁抖动延时</p>
        </div>
        <button
          onClick={closeDrawer}
          className="p-1.5 text-gray-400 hover:text-white rounded-lg hover:bg-gray-800 transition-colors"
        >
          <X className="w-4 h-4" />
        </button>
      </div>

      {/* Summary card */}
      <div className="p-4 border-b border-gray-800/80 bg-[#161616]">
        <div className="flex items-center justify-between text-xs mb-2">
          <span className="text-gray-400">总体完成进度</span>
          <span className="font-mono font-semibold text-sky-400">{percent}%</span>
        </div>
        {/* Progress Bar */}
        <div className="w-full h-2 bg-gray-800 rounded-full overflow-hidden mb-3">
          <div
            className="h-full bg-gradient-to-r from-sky-500 to-emerald-500 transition-all duration-300"
            style={{ width: `${percent}%` }}
          />
        </div>

        <div className="grid grid-cols-3 gap-2 text-center text-[11px] mb-3">
          <div className="p-1.5 rounded bg-gray-800/50">
            <div className="text-gray-400">总任务</div>
            <div className="font-semibold text-white">{total}</div>
          </div>
          <div className="p-1.5 rounded bg-emerald-950/30 border border-emerald-900/40">
            <div className="text-emerald-400">已完成</div>
            <div className="font-semibold text-emerald-300">{completed}</div>
          </div>
          <div className="p-1.5 rounded bg-red-950/30 border border-red-900/40">
            <div className="text-red-400">失败/重试</div>
            <div className="font-semibold text-red-300">{failed}</div>
          </div>
        </div>

        {/* Queue Actions */}
        <div className="flex items-center space-x-2">
          {isPaused ? (
            <button
              onClick={resumeQueue}
              className="flex-1 py-1.5 bg-emerald-600 hover:bg-emerald-500 text-white rounded-lg text-xs font-medium flex items-center justify-center space-x-1 transition-colors"
            >
              <Play className="w-3.5 h-3.5 fill-current" />
              <span>继续队列</span>
            </button>
          ) : (
            <button
              onClick={pauseQueue}
              className="flex-1 py-1.5 bg-gray-800 hover:bg-gray-700 text-gray-200 rounded-lg text-xs font-medium flex items-center justify-center space-x-1 transition-colors"
            >
              <Pause className="w-3.5 h-3.5" />
              <span>暂停下载</span>
            </button>
          )}

          <button
            onClick={cancelQueue}
            className="px-3 py-1.5 bg-red-950/50 hover:bg-red-900/50 border border-red-800/50 text-red-300 rounded-lg text-xs font-medium flex items-center justify-center space-x-1 transition-colors"
          >
            <XCircle className="w-3.5 h-3.5" />
            <span>取消</span>
          </button>
        </div>
      </div>

      {/* Task List */}
      <div className="flex-1 overflow-auto divide-y divide-gray-800/60 p-2">
        {taskList.length === 0 ? (
          <div className="py-12 text-center text-xs text-gray-500">当前没有正在运行的导出任务</div>
        ) : (
          taskList.map((task) => (
            <div key={task.activityId} className="p-2.5 flex items-center justify-between text-xs">
              <div className="truncate mr-2">
                <div className="font-mono text-white text-[11px]">#{task.activityId}</div>
                {task.errorMessage && (
                  <div className="text-[10px] text-red-400 truncate">{task.errorMessage}</div>
                )}
              </div>

              <div className="flex items-center space-x-1.5 flex-shrink-0">
                {task.status === "downloading" && (
                  <span className="flex items-center space-x-1 text-sky-400 text-[11px]">
                    <div className="w-3 h-3 border border-sky-400 border-t-transparent rounded-full animate-spin" />
                    <span>下载中</span>
                  </span>
                )}

                {task.status === "saved" && (
                  <div className="flex items-center space-x-1">
                    <span className="flex items-center space-x-0.5 text-emerald-400 text-[11px]">
                      <CheckCircle2 className="w-3.5 h-3.5" />
                      <span>已落盘</span>
                    </span>
                    {task.savedPath && (
                      <button
                        onClick={() => handleReveal(task.savedPath)}
                        className="p-1 hover:bg-gray-800 text-gray-400 hover:text-white rounded"
                        title="打开所在文件夹"
                      >
                        <FolderOpen className="w-3 h-3" />
                      </button>
                    )}
                  </div>
                )}

                {task.status === "failed" && (
                  <span className="flex items-center space-x-0.5 text-red-400 text-[11px]">
                    <XCircle className="w-3.5 h-3.5" />
                    <span>失败</span>
                  </span>
                )}
              </div>
            </div>
          ))
        )}
      </div>
    </div>
  );
};
