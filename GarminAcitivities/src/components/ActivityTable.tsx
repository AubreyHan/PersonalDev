import React, { useState } from "react";
import { useActivityStore } from "../store/activityStore";
import { useExportStore } from "../store/exportStore";
import { api } from "../api/client";
import { CheckCircle2, Download, FolderOpen, Layers } from "lucide-react";
import { revealItemInDir } from "@tauri-apps/plugin-opener";

export const ActivityTable: React.FC = () => {
  const { items, selectedIds, toggleSelect, selectAll, clearSelection, isLoading } = useActivityStore();
  const { openDrawer } = useExportStore();

  const [isMerging, setIsMerging] = useState(false);
  const [isBatchExporting, setIsBatchExporting] = useState(false);
  const [mergeModalOpen, setMergeModalOpen] = useState(false);
  const [customMergeName, setCustomMergeName] = useState("");

  const allSelected = items.length > 0 && selectedIds.size === items.length;

  const handleBatchExport = async () => {
    if (selectedIds.size === 0) return;
    setIsBatchExporting(true);
    try {
      await api.startBatchExport({
        activityIds: Array.from(selectedIds),
        format: "gpx",
      });
      openDrawer();
      clearSelection();
    } catch (e) {
      console.error("Batch export error:", e);
    } finally {
      setIsBatchExporting(false);
    }
  };

  const handleMergeExport = async () => {
    if (selectedIds.size < 2) return;
    setIsMerging(true);
    try {
      const res = await api.mergeActivities({
        activityIds: Array.from(selectedIds),
        format: "gpx",
        customFileName: customMergeName.trim() ? customMergeName.trim() : undefined,
      });
      if (res.success) {
        setMergeModalOpen(false);
        clearSelection();
        if (res.mergedFilePath) {
          try {
            await revealItemInDir(res.mergedFilePath);
          } catch {}
        }
      }
    } catch (e) {
      console.error("Merge export error:", e);
    } finally {
      setIsMerging(false);
    }
  };

  const handleReveal = async (filePath?: string) => {
    if (!filePath) return;
    try {
      await revealItemInDir(filePath);
    } catch (err) {
      console.warn("Reveal item failed:", err);
    }
  };

  return (
    <div className="flex-1 flex flex-col overflow-hidden relative">
      {/* Table Container */}
      <div className="flex-1 overflow-auto">
        <table className="w-full text-left border-collapse">
          {/* Streamlined Headers: Checkbox, Name, Date, Type, Status */}
          <thead className="bg-[#181818] sticky top-0 z-10 border-b border-gray-800 text-xs font-semibold text-gray-400 select-none">
            <tr>
              <th className="py-3 px-4 w-12 text-center">
                <input
                  type="checkbox"
                  checked={allSelected}
                  onChange={selectAll}
                  className="rounded border-gray-700 bg-gray-900 text-sky-500 focus:ring-0 cursor-pointer"
                />
              </th>
              <th className="py-3 px-4">名字 (Name)</th>
              <th className="py-3 px-4 w-48">日期 (Date)</th>
              <th className="py-3 px-4 w-36">类型 (Type)</th>
              <th className="py-3 px-4 w-36 text-center">导出状态 (Status)</th>
            </tr>
          </thead>
          <tbody className="divide-y divide-gray-800/60 text-xs text-gray-200">
            {items.map((item) => {
              const isSelected = selectedIds.has(item.activityId);
              return (
                <tr
                  key={item.activityId}
                  onClick={() => toggleSelect(item.activityId)}
                  className={`transition-colors cursor-pointer ${
                    isSelected ? "bg-sky-500/10 hover:bg-sky-500/15" : "hover:bg-white/[0.02]"
                  }`}
                >
                  {/* 1. Checkbox */}
                  <td className="py-3 px-4 text-center" onClick={(e) => e.stopPropagation()}>
                    <input
                      type="checkbox"
                      checked={isSelected}
                      onChange={() => toggleSelect(item.activityId)}
                      className="rounded border-gray-700 bg-gray-900 text-sky-500 focus:ring-0 cursor-pointer"
                    />
                  </td>

                  {/* 2. Name */}
                  <td className="py-3 px-4 font-medium text-white flex items-center space-x-2">
                    <span className="truncate max-w-md">{item.activityName}</span>
                    <span className="text-[10px] text-gray-500 font-mono">#{item.activityId}</span>
                  </td>

                  {/* 3. Date */}
                  <td className="py-3 px-4 text-gray-400 font-mono">{item.startTimeLocal}</td>

                  {/* 4. Type */}
                  <td className="py-3 px-4">
                    <span className="inline-flex items-center px-2 py-0.5 rounded-full text-[10px] font-medium bg-gray-800 text-gray-300 border border-gray-700">
                      {item.activityType}
                    </span>
                  </td>

                  {/* 5. Status */}
                  <td className="py-3 px-4 text-center" onClick={(e) => e.stopPropagation()}>
                    {item.isExported ? (
                      <button
                        onClick={() => handleReveal(item.localFilePath)}
                        className="inline-flex items-center space-x-1 px-2 py-1 rounded bg-emerald-500/10 text-emerald-400 border border-emerald-500/20 hover:bg-emerald-500/20 transition-colors"
                        title={item.localFilePath || "已导出"}
                      >
                        <CheckCircle2 className="w-3 h-3" />
                        <span>已导出</span>
                        <FolderOpen className="w-3 h-3 ml-0.5 opacity-70" />
                      </button>
                    ) : (
                      <span className="inline-flex items-center px-2 py-0.5 rounded text-[10px] text-gray-500 bg-gray-800/40">
                        未导出
                      </span>
                    )}
                  </td>
                </tr>
              );
            })}
          </tbody>
        </table>

        {/* Empty state */}
        {!isLoading && items.length === 0 && (
          <div className="py-16 text-center text-gray-500 text-xs">
            暂无匹配的活动记录，可调整起止日期或运动分类后刷新
          </div>
        )}
      </div>

      {/* Floating Action Bar when >= 2 items selected */}
      {selectedIds.size >= 2 && (
        <div className="absolute bottom-6 left-1/2 -translate-x-1/2 z-20 bg-[#1e1e1e] border border-sky-500/30 rounded-2xl shadow-2xl px-5 py-3 flex items-center space-x-4 animate-in slide-in-from-bottom-4 duration-200">
          <div className="flex items-center space-x-2 text-xs font-medium text-white">
            <span className="w-5 h-5 rounded-full bg-sky-500/20 text-sky-400 flex items-center justify-center text-[10px] font-bold">
              {selectedIds.size}
            </span>
            <span>已选择 {selectedIds.size} 项</span>
          </div>

          <div className="h-4 w-px bg-gray-700" />

          {/* Batch export */}
          <button
            onClick={handleBatchExport}
            disabled={isBatchExporting}
            className="flex items-center space-x-1.5 px-3 py-1.5 bg-gray-800 hover:bg-gray-700 text-gray-200 rounded-lg text-xs font-medium transition-colors cursor-pointer"
          >
            <Download className="w-3.5 h-3.5 text-sky-400" />
            <span>批量导出文件</span>
          </button>

          {/* Merge into single track export */}
          <button
            onClick={() => setMergeModalOpen(true)}
            className="flex items-center space-x-1.5 px-3.5 py-1.5 bg-sky-500 hover:bg-sky-400 text-white rounded-lg text-xs font-semibold shadow-md shadow-sky-500/20 transition-all cursor-pointer"
          >
            <Layers className="w-3.5 h-3.5" />
            <span>合并为一个文件导出</span>
          </button>

          <button
            onClick={clearSelection}
            className="text-xs text-gray-400 hover:text-white transition-colors"
          >
            取消选择
          </button>
        </div>
      )}

      {/* Merge Dialog */}
      {mergeModalOpen && (
        <div className="fixed inset-0 z-50 flex items-center justify-center bg-black/75 backdrop-blur-sm p-4">
          <div className="w-full max-w-md bg-[#1e1e1e] border border-gray-800 rounded-2xl shadow-2xl p-6 text-gray-100 animate-in fade-in zoom-in-95 duration-200">
            <div className="flex items-center space-x-2.5 mb-3 text-sky-400">
              <Layers className="w-5 h-5" />
              <h3 className="font-bold text-base text-white">合并导出多段轨迹</h3>
            </div>
            <p className="text-xs text-gray-400 leading-relaxed mb-4">
              已选定 {selectedIds.size} 条活动。合并引擎将按运动开始时间严格升序串联轨迹点，并 100% 完整保留心率、踏频、高程等传感器指标。
            </p>

            <div className="mb-4">
              <label className="block text-xs font-medium text-gray-300 mb-1">自定义文件名 (可选)</label>
              <input
                type="text"
                placeholder="例如: 2026-11-03_奥森长距离跑合并.gpx"
                value={customMergeName}
                onChange={(e) => setCustomMergeName(e.target.value)}
                className="w-full bg-[#161616] border border-gray-800 rounded-lg px-3 py-2 text-xs text-white placeholder-gray-600 focus:outline-none focus:border-sky-500"
              />
            </div>

            <div className="flex justify-end space-x-2">
              <button
                onClick={() => setMergeModalOpen(false)}
                className="px-3.5 py-1.5 bg-gray-800 hover:bg-gray-700 text-gray-300 rounded-lg text-xs transition-colors"
              >
                取消
              </button>
              <button
                onClick={handleMergeExport}
                disabled={isMerging}
                className="px-4 py-1.5 bg-sky-500 hover:bg-sky-400 disabled:opacity-50 text-white rounded-lg text-xs font-medium shadow-md shadow-sky-500/20 transition-all flex items-center space-x-1.5"
              >
                {isMerging ? (
                  <>
                    <div className="w-3 h-3 border border-white/30 border-t-white rounded-full animate-spin" />
                    <span>正在无损合并...</span>
                  </>
                ) : (
                  <span>确认合并导出</span>
                )}
              </button>
            </div>
          </div>
        </div>
      )}
    </div>
  );
};
