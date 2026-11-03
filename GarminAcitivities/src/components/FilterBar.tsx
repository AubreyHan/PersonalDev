import React, { useState } from "react";
import { useActivityStore } from "../store/activityStore";
import { useExportStore } from "../store/exportStore";
import { Calendar, Filter, RefreshCw, Zap } from "lucide-react";
import { api } from "../api/client";

const formatActivityTypeLabel = (typeKey: string): string => {
  const map: Record<string, string> = {
    running: "🏃 跑步 (Running)",
    trail_running: "⛰️ 越野跑 (Trail Running)",
    treadmill_running: "🏃‍♂️ 跑步机 (Treadmill)",
    track_running: "🏟️ 田径跑 (Track)",
    cycling: "🚴 骑行 (Cycling)",
    road_biking: "🚴 公路骑行 (Road Biking)",
    gravel_cycling: "🚵 砾石骑行 (Gravel Cycling)",
    mountain_biking: "🚵 山地骑行 (MTB)",
    indoor_cycling: "🚴 室内骑行 (Indoor)",
    virtual_ride: "🎮 虚拟骑行 (Virtual Ride)",
    swimming: "🏊 游泳 (Swimming)",
    lap_swimming: "🏊 泳池游泳 (Lap Swimming)",
    open_water_swimming: "🌊 公开水域 (Open Water)",
    hiking: "🥾 徒步 (Hiking)",
    walking: "🚶 健走 (Walking)",
    fitness_equipment: "🏋️ 健身器材 (Fitness)",
    strength_training: "🏋️ 力量训练 (Strength)",
    cardio_training: "❤️ 有氧运动 (Cardio)",
    yoga: "🧘 瑜伽 (Yoga)",
    pilates: "🤸 普拉提 (Pilates)",
    rowing: "🚣 划船 (Rowing)",
    indoor_rowing: "🚣 室内划船 (Indoor Rowing)",
    elliptical: "🏃 椭圆机 (Elliptical)",
    stair_stepper: "🪜 踏步机 (Stairs)",
    skiing: "⛷️ 双板滑雪 (Skiing)",
    snowboarding: "🏂 单板滑雪 (Snowboarding)",
    cross_country_skiing: "🎿 越野滑雪 (XC Skiing)",
    other: "🏅 其他运动 (Other)",
  };
  return map[typeKey] || `🏅 ${typeKey.replace(/_/g, " ")}`;
};

export const FilterBar: React.FC = () => {
  const { query, setQuery, fetchActivities, isLoading, availableTypes, totalCount } = useActivityStore();
  const { openDrawer } = useExportStore();
  const [isSyncing, setIsSyncing] = useState(false);

  // 根据当前选择时期范围内的运动类型动态生成选项列表，而非固定预设
  const dynamicOptions = [
    { label: `全部运动类型 (${totalCount})`, value: "all" },
    ...availableTypes.map((t) => ({
      label: formatActivityTypeLabel(t),
      value: t,
    })),
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

        {/* Dynamic Activity Type Dropdown */}
        <div className="flex items-center space-x-1.5 bg-[#121212] border border-gray-800 rounded-lg px-2.5 py-1.5 text-xs text-gray-300">
          <Filter className="w-3.5 h-3.5 text-emerald-400" />
          <select
            value={query.activityType || "all"}
            onChange={(e) => setQuery({ activityType: e.target.value })}
            className="bg-transparent text-gray-200 focus:outline-none cursor-pointer pr-2"
          >
            {dynamicOptions.map((opt) => (
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
