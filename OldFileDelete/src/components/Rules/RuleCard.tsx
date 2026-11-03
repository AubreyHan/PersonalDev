import React from "react";
import { GripVertical, Clock, Filter, Trash2, Edit2, PlayCircle, Shield, AlertTriangle } from "lucide-react";
import { FolderRule } from "../../types";

interface Props {
  rule: FolderRule;
  onToggle: (enabled: boolean) => void;
  onEdit: () => void;
  onDelete: () => void;
}

export const RuleCard: React.FC<Props> = ({ rule, onToggle, onEdit, onDelete }) => {
  const getActionBadge = () => {
    switch (rule.action) {
      case "list":
        return (
          <span className="inline-flex items-center gap-1 px-2 py-0.5 rounded-full text-[11px] font-medium bg-blue-100 text-blue-700 dark:bg-blue-900/40 dark:text-blue-300">
            <PlayCircle className="w-3 h-3" />
            列出 (List)
          </span>
        );
      case "keep":
        return (
          <span className="inline-flex items-center gap-1 px-2 py-0.5 rounded-full text-[11px] font-medium bg-emerald-100 text-emerald-700 dark:bg-emerald-900/40 dark:text-emerald-300">
            <Shield className="w-3 h-3" />
            保留 (Keep)
          </span>
        );
      case "direct_trash":
        return (
          <span className="inline-flex items-center gap-1 px-2 py-0.5 rounded-full text-[11px] font-medium bg-red-100 text-red-700 dark:bg-red-900/40 dark:text-red-300">
            <Trash2 className="w-3 h-3" />
            直接删除 (Trash)
          </span>
        );
      case "ask_trash":
        return (
          <span className="inline-flex items-center gap-1 px-2 py-0.5 rounded-full text-[11px] font-medium bg-amber-100 text-amber-700 dark:bg-amber-900/40 dark:text-amber-300">
            <AlertTriangle className="w-3 h-3" />
            询问后删除 (Ask)
          </span>
        );
    }
  };

  const getTimeLabel = () => {
    if (rule.dateAdded.isAny || rule.dateAdded.value === 0) {
      return "不限时间 (Any)";
    }
    const unitMap = { minutes: "分", hours: "小时", days: "天" };
    return `超 ${rule.dateAdded.value} ${unitMap[rule.dateAdded.unit]}`;
  };

  return (
    <div
      className={`group relative flex items-center gap-3 px-4 py-3 rounded-xl border bg-white dark:bg-[#222222] transition shadow-sm ${
        rule.enabled
          ? "border-black/10 dark:border-white/10 hover:border-mac-accent/50"
          : "opacity-60 border-dashed border-gray-300 dark:border-gray-700"
      }`}
    >
      {/* Drag handle */}
      <div className="cursor-grab active:cursor-grabbing text-gray-400 hover:text-gray-600 dark:hover:text-gray-200">
        <GripVertical className="w-4 h-4" />
      </div>

      {/* Main info */}
      <div className="flex-1 min-w-0">
        <div className="flex items-center gap-2 mb-1">
          <span className="text-xs font-semibold text-gray-900 dark:text-white truncate">
            {rule.name}
          </span>
          {getActionBadge()}
        </div>

        <div className="flex flex-wrap items-center gap-2 text-[11px] text-gray-500 dark:text-gray-400">
          <span className="inline-flex items-center gap-1 bg-black/5 dark:bg-white/5 px-2 py-0.5 rounded">
            <Clock className="w-3 h-3" />
            {getTimeLabel()}
          </span>

          <span className="inline-flex items-center gap-1 bg-black/5 dark:bg-white/5 px-2 py-0.5 rounded font-mono">
            <Filter className="w-3 h-3" />
            {rule.pattern}
          </span>
        </div>
      </div>

      {/* Actions */}
      <div className="flex items-center gap-2 shrink-0">
        {/* Toggle switch */}
        <label className="relative inline-flex items-center cursor-pointer">
          <input
            type="checkbox"
            checked={rule.enabled}
            onChange={(e) => onToggle(e.target.checked)}
            className="sr-only peer"
          />
          <div className="w-8 h-4.5 bg-gray-300 peer-focus:outline-none rounded-full peer dark:bg-gray-700 peer-checked:after:translate-x-full peer-checked:after:border-white after:content-[''] after:absolute after:top-[2px] after:left-[2px] after:bg-white after:border-gray-300 after:border after:rounded-full after:h-3.5 after:w-3.5 after:transition-all peer-checked:bg-mac-accent"></div>
        </label>

        {/* Edit button */}
        <button
          onClick={onEdit}
          title="修改规则"
          className="p-1.5 text-gray-400 hover:text-gray-700 dark:hover:text-gray-200 hover:bg-black/5 dark:hover:bg-white/10 rounded-lg transition"
        >
          <Edit2 className="w-3.5 h-3.5" />
        </button>

        {/* Delete button */}
        <button
          onClick={onDelete}
          title="删除规则"
          className="p-1.5 text-gray-400 hover:text-mac-danger hover:bg-red-50 dark:hover:bg-red-950/30 rounded-lg transition"
        >
          <Trash2 className="w-3.5 h-3.5" />
        </button>
      </div>
    </div>
  );
};
