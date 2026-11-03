import React, { useState, useEffect } from "react";
import { ShieldCheck } from "lucide-react";
import { useFolderStore } from "../../store/useFolderStore";

interface Props {
  folderId: string;
  initialValue: string;
}

export const TopWhitelistBar: React.FC<Props> = ({ folderId, initialValue }) => {
  const [val, setVal] = useState(initialValue);
  const updateTopWhitelist = useFolderStore((s) => s.updateTopWhitelist);

  useEffect(() => {
    setVal(initialValue);
  }, [initialValue]);

  const handleBlur = () => {
    if (val !== initialValue) {
      updateTopWhitelist(folderId, val);
    }
  };

  const handleKeyDown = (e: React.KeyboardEvent) => {
    if (e.key === "Enter") {
      (e.target as HTMLInputElement).blur();
    }
  };

  return (
    <section className="bg-blue-50/60 dark:bg-blue-950/20 border border-blue-200/80 dark:border-blue-800/40 rounded-xl p-4 shadow-sm">
      <div className="flex items-center gap-2 mb-2 text-blue-700 dark:text-blue-300 font-semibold text-xs uppercase tracking-wider">
        <ShieldCheck className="w-4 h-4 text-blue-600 dark:text-blue-400" />
        <span>置顶保留白名单 (Top Whitelist - 永久免死保护)</span>
      </div>
      <input
        type="text"
        value={val}
        onChange={(e) => setVal(e.target.value)}
        onBlur={handleBlur}
        onKeyDown={handleKeyDown}
        placeholder="输入免死通配符，逗号隔开 (如: keep*, *.important, **/backup*)"
        className="w-full text-xs px-3 py-2 rounded-lg border border-blue-200 dark:border-blue-800/60 bg-white dark:bg-[#1a1a1a] text-gray-800 dark:text-gray-200 focus:outline-none focus:ring-2 focus:ring-blue-500/30 transition placeholder:text-gray-400"
      />
      <p className="mt-1.5 text-[11px] text-blue-600/80 dark:text-blue-400/80">
        所有命中此规则的文件及其子文件夹将拥有最高优先级，直接免除一切删除动作。
      </p>
    </section>
  );
};
