import React, { useState } from "react";
import { useActivityStore } from "../store/activityStore";
import { useExportStore } from "../store/exportStore";
import { Calendar, Filter, RefreshCw, Zap } from "lucide-react";
import { api } from "../api/client";

export const FilterBar: React.FC = () => {
  const { query, setQuery, fetchActivities, isLoading } = useActivityStore();
  const { openDrawer } = useExportStore();
  const [isSyncing, setIsSyncing] = useState(false);

  const activityOptions = [
    { label: "全部运动类型", value: "all" },
    { label: "🏃 跑步 (Running)", value: "running" },
    { label: "🚴 骑行 (Cycling)", value: "cycling" },
    { label: "🏊 游泳 (Swimming)", value: "swimming" },
    { label: "🥾 徒步 (Hiking)", value: "hiking" },
    { label: "🚶 健走 (Walking)", value: "walking" },
    { label: "🏋️ 健身力量 (Strength)", value: "fitness_equipment" },
  ];

  const handleIncrementalSync = async () => {
    setIsSyncing(true);
    try {
      const res = await api.startIncrementalExport({ format: "gpx" });
      if (res.queuedCount > 0) {
        openDrawer();
      }
    } catch (e) {
      console.error("Incremental sync error:", e);
    } finally {
      setIsSyncing(false);
    }
  };

  return (
    <div className="bg-[#181818] border-b border-gray-800 px-6 py-3 flex flex-wrap items-center justify-between gap-4">
      {/* Left: Filters */}
      <div className="flex flex-wrap items-center gap-3">
        {/* Date Range Picker (Closed Interval) */}
        <div className="flex items-center space-x-1.5 bg-[#121212] border border-gray-800 rounded-lg px-2.5 py-1.5 text-xs text-gray-300">
          <Calendar className="w-3.5 h-3.5 text-sky-400" />
          <span className="text-gray-500">日期:</span>
          <input
            type="date"
            value={query.startDate || ""}
            onChange={(e) => setQuery({ startDate: e.target.value })}
            className="bg-transparent text-gray-200 focus:outline-none cursor-pointer"
            title="起始日期 (闭区间包含当天)"
          />
          <span className="text-gray-500">至</span>
          <input
            type="date"
            value={query.endDate || ""}
            onChange={(e) => setQuery({ endDate: e.target.value })}
            className="bg-transparent text-gray-200 focus:outline-none cursor-pointer"
            title="截止日期 (闭区间包含当天)"
          />
          {(query.startDate || query.endDate) && (
            <button
              onClick={() => setQuery({ startDate: "", endDate: "" })}
              className="ml-1 text-gray-500 hover:text-gray-300"
              title="清除日期筛选"
            >
              ✕
            </button>
          )}
        </div>

        {/* Activity Type Dropdown */}
        <div className="flex items-center space-x-1.5 bg-[#121212] border border-gray-800 rounded-lg px-2.5 py-1.5 text-xs text-gray-300">
          <Filter className="w-3.5 h-3.5 text-emerald-400" />
          <select
            value={query.activityType || "all"}
            onChange={(e) => setQuery({ activityType: e.target.value })}
            className="bg-transparent text-gray-200 focus:outline-none cursor-pointer pr-2"
          >
            {activityOptions.map((opt) => (
              <option key={opt.value} value={opt.value} className="bg-[#1a1a1a] text-white">
                {opt.label}
              </option>
            ))}
          </select>
        </div>

        {/* Refresh button */}
        <button
          onClick={() => fetchActivities()}
          disabled={isLoading}
          className="p-2 bg-[#121212] border border-gray-800 rounded-lg hover:border-gray-700 text-gray-400 hover:text-white transition-colors"
          title="刷新列表"
        >
          <RefreshCw className={`w-3.5 h-3.5 ${isLoading ? "animate-spin text-sky-400" : ""}`} />
        </button>
      </div>

      {/* Right: Actions */}
      <div className="flex items-center space-x-3">
        <button
          onClick={handleIncrementalSync}
          disabled={isSyncing}
          className="flex items-center space-x-1.5 px-3 py-1.5 bg-gradient-to-r from-sky-500 to-sky-600 hover:from-sky-400 hover:to-sky-500 text-white rounded-lg text-xs font-medium shadow-md shadow-sky-500/10 transition-all cursor-pointer disabled:opacity-50"
        >
          <Zap className="w-3.5 h-3.5" />
          <span>{isSyncing ? "比对中..." : "一键增量同步"}</span>
        </button>
      </div>
    </div>
  );
};
