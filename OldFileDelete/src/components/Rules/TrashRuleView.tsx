import React, { useState } from "react";
import { Clock, HardDrive, ShieldAlert } from "lucide-react";
import { useFolderStore } from "../../store/useFolderStore";

export const TrashRuleView: React.FC = () => {
  const trashConfig = useFolderStore((s) => s.trashConfig);
  const saveTrashConfig = useFolderStore((s) => s.saveTrashConfig);

  const [days, setDays] = useState(trashConfig.daysThreshold);
  const [maxGb, setMaxGb] = useState(trashConfig.maxCapacityGB);
  const [isSaving, setIsSaving] = useState(false);

  const handleSave = async (e: React.FormEvent) => {
    e.preventDefault();
    setIsSaving(true);
    await saveTrashConfig({
      daysThreshold: Number(days),
      maxCapacityGB: Number(maxGb),
    });
    setIsSaving(false);
  };

  return (
    <div className="space-y-6">
      <div className="bg-amber-500/10 border border-amber-500/20 rounded-xl p-4 flex items-start gap-3 text-xs text-amber-700 dark:text-amber-400">
        <ShieldAlert className="w-4 h-4 shrink-0 mt-0.5" />
        <div>
          <span className="font-semibold block mb-0.5">系统废纸篓自动管理规则说明:</span>
          废纸篓清理属于不可逆物理擦除。规则填 0 表示关闭该项策略，系统绝不主动清除未触发规则的文件。
        </div>
      </div>

      <form onSubmit={handleSave} className="space-y-4">
        {/* Rule 1: Days threshold */}
        <div className="bg-white dark:bg-[#202020] border border-black/10 dark:border-white/10 rounded-xl p-5 shadow-sm space-y-2">
          <div className="flex items-center gap-2 text-sm font-semibold text-gray-900 dark:text-gray-100">
            <Clock className="w-4 h-4 text-mac-accent" />
            <span>规则 1：超龄天数自动清除</span>
          </div>
          <p className="text-xs text-gray-500 dark:text-gray-400">
            当文件放入废纸篓超过指定天数后彻底清除（填 0 关闭此规则）。
          </p>
          <div className="flex items-center gap-3 pt-2">
            <input
              type="number"
              min={0}
              max={3650}
              value={days}
              onChange={(e) => setDays(Math.max(0, parseInt(e.target.value) || 0))}
              className="w-24 text-xs px-3 py-1.5 rounded-lg border border-gray-300 dark:border-gray-700 bg-gray-50 dark:bg-[#1a1a1a] text-gray-900 dark:text-gray-100 focus:outline-none focus:ring-2 focus:ring-mac-accent"
            />
            <span className="text-xs text-gray-600 dark:text-gray-300">天 (Days)</span>
          </div>
        </div>

        {/* Rule 2: Max Capacity GB */}
        <div className="bg-white dark:bg-[#202020] border border-black/10 dark:border-white/10 rounded-xl p-5 shadow-sm space-y-2">
          <div className="flex items-center gap-2 text-sm font-semibold text-gray-900 dark:text-gray-100">
            <HardDrive className="w-4 h-4 text-mac-warning" />
            <span>规则 2：废纸篓容量上限控制 (FIFO)</span>
          </div>
          <p className="text-xs text-gray-500 dark:text-gray-400">
            当整个废纸篓总占用超过该阈值时，按移入时间升序（最旧的文件优先）逐步彻底清空，直至总容量回落达标（填 0 关闭此规则）。
          </p>
          <div className="flex items-center gap-3 pt-2">
            <input
              type="number"
              min={0}
              max={10000}
              value={maxGb}
              onChange={(e) => setMaxGb(Math.max(0, parseInt(e.target.value) || 0))}
              className="w-24 text-xs px-3 py-1.5 rounded-lg border border-gray-300 dark:border-gray-700 bg-gray-50 dark:bg-[#1a1a1a] text-gray-900 dark:text-gray-100 focus:outline-none focus:ring-2 focus:ring-mac-accent"
            />
            <span className="text-xs text-gray-600 dark:text-gray-300">GB</span>
          </div>
        </div>

        <div className="flex justify-end pt-2">
          <button
            type="submit"
            disabled={isSaving}
            className="px-5 py-2 text-xs font-medium rounded-lg bg-mac-accent text-white hover:bg-blue-600 transition shadow-sm disabled:opacity-50"
          >
            {isSaving ? "保存中..." : "保存废纸篓配置"}
          </button>
        </div>
      </form>
    </div>
  );
};
