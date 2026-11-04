import React from "react";
import { ListFilter, FileText } from "lucide-react";
import { useMatchListStore } from "../../store/useMatchListStore";

export const MatchListTable: React.FC = () => {
  const matches = useMatchListStore((s) => s.matches);

  if (matches.length === 0) {
    return null;
  }

  const formatBytes = (bytes: number) => {
    if (bytes === 0) return "0 B";
    const k = 1024;
    const sizes = ["B", "KB", "MB", "GB"];
    const i = Math.floor(Math.log(bytes) / Math.log(k));
    return `${parseFloat((bytes / Math.pow(k, i)).toFixed(1))} ${sizes[i]}`;
  };

  const formatDate = (ms: number) => {
    if (!ms) return "-";
    return new Date(ms).toLocaleString("zh-CN", {
      month: "2-digit",
      day: "2-digit",
      hour: "2-digit",
      minute: "2-digit",
    });
  };

  return (
    <div className="bg-white dark:bg-[#202020] border border-black/10 dark:border-white/10 rounded-xl shadow-sm overflow-hidden">
      <div className="px-4 py-3 border-b border-black/10 dark:border-white/10 flex items-center justify-between bg-black/[0.02] dark:bg-white/[0.02]">
        <div className="flex items-center gap-2 text-xs font-semibold text-gray-700 dark:text-gray-300">
          <ListFilter className="w-4 h-4 text-mac-accent" />
          <span>匹配结果清单 (Match List - 共 {matches.length} 项)</span>
        </div>
      </div>

      <div className="max-h-60 overflow-y-auto">
        <table className="w-full text-left text-xs">
          <thead className="bg-black/[0.03] dark:bg-white/[0.03] text-gray-500 text-[11px] sticky top-0">
            <tr>
              <th className="py-2 px-3 font-medium">文件名</th>
              <th className="py-2 px-3 font-medium">命中规则</th>
              <th className="py-2 px-3 font-medium">大小</th>
              <th className="py-2 px-3 font-medium">添加时间</th>
            </tr>
          </thead>
          <tbody className="divide-y divide-black/5 dark:divide-white/5">
            {matches.map((item, index) => (
              <tr key={index} className="hover:bg-black/[0.02] dark:hover:bg-white/[0.02] transition">
                <td className="py-2 px-3 text-gray-800 dark:text-gray-200">
                  <div className="flex items-center gap-1.5">
                    <FileText className="w-3.5 h-3.5 text-gray-400 shrink-0" />
                    <span className="truncate max-w-xs" title={item.filePath}>
                      {item.fileName}
                    </span>
                  </div>
                </td>
                <td className="py-2 px-3 text-gray-500">{item.ruleName}</td>
                <td className="py-2 px-3 text-gray-500 font-mono text-[11px]">
                  {formatBytes(item.sizeBytes)}
                </td>
                <td className="py-2 px-3 text-gray-400 text-[11px]">
                  {formatDate(item.dateAdded)}
                </td>
              </tr>
            ))}
          </tbody>
        </table>
      </div>
    </div>
  );
};
