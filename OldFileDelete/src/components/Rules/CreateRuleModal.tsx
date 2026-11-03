import React, { useState, useEffect } from "react";
import { X, Sparkles, Sliders } from "lucide-react";
import { FolderRule, RuleAction, TimeUnit } from "../../types";

interface Props {
  isOpen: boolean;
  folderId: string;
  initialRule?: FolderRule | null;
  onClose: () => void;
  onSave: (rule: FolderRule) => void;
}

export const CreateRuleModal: React.FC<Props> = ({
  isOpen,
  folderId,
  initialRule,
  onClose,
  onSave,
}) => {
  const [name, setName] = useState("");
  const [pattern, setPattern] = useState("");
  const [action, setAction] = useState<RuleAction>("direct_trash");
  const [timeValue, setTimeValue] = useState<number>(0);
  const [timeUnit, setTimeUnit] = useState<TimeUnit>("days");
  const [isAny, setIsAny] = useState<boolean>(true);

  useEffect(() => {
    if (initialRule) {
      setName(initialRule.name);
      setPattern(initialRule.pattern);
      setAction(initialRule.action);
      setTimeValue(initialRule.dateAdded.value);
      setTimeUnit(initialRule.dateAdded.unit);
      setIsAny(initialRule.dateAdded.isAny);
    } else {
      setName("");
      setPattern("");
      setAction("direct_trash");
      setTimeValue(0);
      setTimeUnit("days");
      setIsAny(true);
    }
  }, [initialRule, isOpen]);

  if (!isOpen) return null;

  const handleSubmit = (e: React.FormEvent) => {
    e.preventDefault();
    if (!name.trim() || !pattern.trim()) return;

    const rule: FolderRule = {
      id: initialRule ? initialRule.id : "",
      name: name.trim(),
      enabled: initialRule ? initialRule.enabled : true,
      pattern: pattern.trim(),
      action,
      dateAdded: {
        value: isAny ? 0 : Number(timeValue),
        unit: timeUnit,
        isAny,
      },
      order: initialRule ? initialRule.order : 999,
    };

    onSave(rule);
    onClose();
  };

  return (
    <div className="fixed inset-0 z-50 flex items-center justify-center bg-black/40 backdrop-blur-sm p-4">
      <div className="w-full max-w-lg bg-white dark:bg-[#252525] rounded-2xl shadow-2xl border border-black/10 dark:border-white/10 overflow-hidden">
        {/* Header */}
        <div className="px-6 py-4 border-b border-black/10 dark:border-white/10 flex items-center justify-between">
          <div className="flex items-center gap-2 text-sm font-semibold text-gray-900 dark:text-white">
            <Sliders className="w-4 h-4 text-mac-accent" />
            <span>{initialRule ? "编辑规则 (Edit Rule)" : "创建新规则 (Create Rule)"}</span>
          </div>
          <button
            onClick={onClose}
            className="p-1 text-gray-400 hover:text-gray-600 dark:hover:text-gray-200 rounded-md transition"
          >
            <X className="w-4 h-4" />
          </button>
        </div>

        {/* Form Body */}
        <form onSubmit={handleSubmit} className="p-6 space-y-5">
          {/* Rule Name */}
          <div>
            <label className="block text-xs font-semibold text-gray-700 dark:text-gray-300 mb-1.5">
              Rule Name (规则名称)
            </label>
            <input
              type="text"
              required
              placeholder="例如: 清理临时安装包 DMG"
              value={name}
              onChange={(e) => setName(e.target.value)}
              className="w-full text-xs px-3 py-2 rounded-lg border border-gray-300 dark:border-gray-700 bg-white dark:bg-[#1a1a1a] text-gray-900 dark:text-gray-100 focus:outline-none focus:ring-2 focus:ring-mac-accent"
            />
          </div>

          {/* Date Added Condition */}
          <div>
            <label className="block text-xs font-semibold text-gray-700 dark:text-gray-300 mb-1.5">
              Date Added (添加到该目录的时间)
            </label>
            <div className="flex items-center gap-3">
              <span className="text-xs text-gray-500">exceeds</span>
              <div className="flex items-center gap-1.5">
                <input
                  type="checkbox"
                  id="any-time"
                  checked={isAny}
                  onChange={(e) => setIsAny(e.target.checked)}
                  className="rounded text-mac-accent focus:ring-0"
                />
                <label htmlFor="any-time" className="text-xs text-gray-600 dark:text-gray-400">
                  Any (不限时间)
                </label>
              </div>

              {!isAny && (
                <>
                  <input
                    type="number"
                    min={1}
                    value={timeValue}
                    onChange={(e) => setTimeValue(Math.max(1, parseInt(e.target.value) || 1))}
                    className="w-20 text-xs px-2.5 py-1.5 rounded-lg border border-gray-300 dark:border-gray-700 bg-white dark:bg-[#1a1a1a] text-gray-900 dark:text-gray-100 focus:outline-none focus:ring-2 focus:ring-mac-accent"
                  />
                  <select
                    value={timeUnit}
                    onChange={(e) => setTimeUnit(e.target.value as TimeUnit)}
                    className="text-xs px-2.5 py-1.5 rounded-lg border border-gray-300 dark:border-gray-700 bg-white dark:bg-[#1a1a1a] text-gray-900 dark:text-gray-100 focus:outline-none focus:ring-2 focus:ring-mac-accent"
                  >
                    <option value="minutes">分 (Minutes)</option>
                    <option value="hours">小时 (Hours)</option>
                    <option value="days">天 (Days)</option>
                  </select>
                </>
              )}
            </div>
          </div>

          {/* File Name Pattern */}
          <div>
            <label className="block text-xs font-semibold text-gray-700 dark:text-gray-300 mb-1.5">
              File Name Pattern (文件名通配符匹配)
            </label>
            <input
              type="text"
              required
              placeholder="例如: *.dmg, a*.j*, **/temp*"
              value={pattern}
              onChange={(e) => setPattern(e.target.value)}
              className="w-full text-xs px-3 py-2 rounded-lg border border-gray-300 dark:border-gray-700 bg-white dark:bg-[#1a1a1a] text-gray-900 dark:text-gray-100 focus:outline-none focus:ring-2 focus:ring-mac-accent"
            />
            <p className="mt-1 text-[11px] text-gray-400">
              大小写严格敏感。支持逗号多规则分隔（OR 逻辑），支持 <code>*a</code>（主干结尾）、<code>**</code>（递归子目录）。
            </p>
          </div>

          {/* Execution Action */}
          <div>
            <label className="block text-xs font-semibold text-gray-700 dark:text-gray-300 mb-1.5">
              Execution Action (处置动作)
            </label>
            <select
              value={action}
              onChange={(e) => setAction(e.target.value as RuleAction)}
              className="w-full text-xs px-3 py-2 rounded-lg border border-gray-300 dark:border-gray-700 bg-white dark:bg-[#1a1a1a] text-gray-900 dark:text-gray-100 focus:outline-none focus:ring-2 focus:ring-mac-accent"
            >
              <option value="list">List Matches (仅在匹配清单中展示，不做任何清理)</option>
              <option value="keep">Keep (锁定保留，短路终止后续规则)</option>
              <option value="direct_trash">Directly Move to Trash (直接移入系统废纸篓)</option>
              <option value="ask_trash">Ask before Trashing (询问后删除，逐一确认)</option>
            </select>
          </div>

          {/* Actions */}
          <div className="pt-3 border-t border-black/10 dark:border-white/10 flex items-center justify-end gap-2">
            <button
              type="button"
              onClick={onClose}
              className="px-4 py-2 text-xs font-medium rounded-lg text-gray-600 dark:text-gray-300 hover:bg-black/5 dark:hover:bg-white/10 transition"
            >
              取消
            </button>
            <button
              type="submit"
              className="px-5 py-2 text-xs font-medium rounded-lg bg-mac-accent text-white hover:bg-blue-600 transition shadow-sm"
            >
              {initialRule ? "保存修改" : "创建规则"}
            </button>
          </div>
        </form>
      </div>
    </div>
  );
};
