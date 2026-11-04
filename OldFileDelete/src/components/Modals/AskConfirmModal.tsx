import React from "react";
import { Trash2, ShieldCheck, FileQuestion } from "lucide-react";
import { useAskQueueStore } from "../../store/useAskQueueStore";

export const AskConfirmModal: React.FC = () => {
  const queue = useAskQueueStore((s) => s.queue);
  const handleDecision = useAskQueueStore((s) => s.handleDecision);

  const current = queue[0];
  if (!current) return null;

  const formatBytes = (bytes: number) => {
    if (bytes === 0) return "0 B";
    const k = 1024;
    const sizes = ["B", "KB", "MB", "GB"];
    const i = Math.floor(Math.log(bytes) / Math.log(k));
    return `${parseFloat((bytes / Math.pow(k, i)).toFixed(1))} ${sizes[i]}`;
  };

  const formatDate = (ms: number) => {
    if (!ms) return "-";
    return new Date(ms).toLocaleString("zh-CN");
  };

  return (
    <div className="fixed inset-0 z-50 flex items-center justify-center bg-black/50 backdrop-blur-sm p-4">
      <div className="w-full max-w-md bg-white dark:bg-[#252525] rounded-2xl shadow-2xl border border-black/10 dark:border-white/10 overflow-hidden animate-in fade-in zoom-in-95 duration-150">
        {/* Header */}
        <div className="px-5 py-4 border-b border-black/10 dark:border-white/10 flex items-center justify-between bg-amber-500/5">
          <div className="flex items-center gap-2 text-sm font-semibold text-gray-900 dark:text-white">
            <FileQuestion className="w-4 h-4 text-mac-warning" />
            <span>清理前逐一确认 (待确认: {queue.length} 项)</span>
          </div>
          <span className="text-[11px] px-2 py-0.5 rounded-full bg-amber-500/10 text-amber-600 font-medium">
            第 1 / {queue.length} 项
          </span>
        </div>

        {/* Content */}
        <div className="p-5 space-y-4">
          <div className="bg-black/[0.03] dark:bg-white/[0.03] rounded-xl p-4 space-y-2 text-xs">
            <div className="flex justify-between items-start gap-2">
              <span className="text-gray-400 shrink-0">文件名称:</span>
              <span className="font-semibold text-gray-800 dark:text-gray-100 text-right truncate">
                {current.fileName}
              </span>
            </div>

            <div className="flex justify-between items-start gap-2">
              <span className="text-gray-400 shrink-0">所在路径:</span>
              <span className="text-gray-500 text-[11px] text-right truncate max-w-[220px]" title={current.filePath}>
                {current.filePath}
              </span>
            </div>

            <div className="flex justify-between items-center">
              <span className="text-gray-400">文件大小:</span>
              <span className="font-mono text-gray-700 dark:text-gray-300">
                {formatBytes(current.sizeBytes)}
              </span>
            </div>

            <div className="flex justify-between items-center">
              <span className="text-gray-400">添加时间:</span>
              <span className="text-gray-700 dark:text-gray-300">
                {formatDate(current.dateAdded)}
              </span>
            </div>

            <div className="flex justify-between items-center pt-1 border-t border-black/5 dark:border-white/5">
              <span className="text-gray-400">触发规则:</span>
              <span className="font-medium text-mac-accent">
                {current.ruleName}
              </span>
            </div>
          </div>

          <p className="text-[11px] text-gray-400 text-center">
            命中“询问后删除”规则。请对此文件做出明确处置，系统绝不会自动批量删除。
          </p>

          {/* Action Buttons */}
          <div className="grid grid-cols-2 gap-3 pt-1">
            <button
              onClick={() => handleDecision("keep")}
              className="flex items-center justify-center gap-1.5 py-2.5 px-4 text-xs font-semibold rounded-xl bg-black/5 hover:bg-black/10 dark:bg-white/10 dark:hover:bg-white/15 text-gray-800 dark:text-gray-200 transition"
            >
              <ShieldCheck className="w-4 h-4 text-mac-success" />
              保留文件
            </button>
            <button
              onClick={() => handleDecision("trash")}
              className="flex items-center justify-center gap-1.5 py-2.5 px-4 text-xs font-semibold rounded-xl bg-mac-danger hover:bg-red-600 text-white transition shadow-sm"
            >
              <Trash2 className="w-4 h-4" />
              移至废纸篓
            </button>
          </div>
        </div>
      </div>
    </div>
  );
};
