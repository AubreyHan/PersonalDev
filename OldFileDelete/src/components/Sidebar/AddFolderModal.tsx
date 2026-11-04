import React, { useState } from "react";
import { FolderPlus, AlertCircle, X, ShieldAlert } from "lucide-react";
import { useFolderStore } from "../../store/useFolderStore";

interface Props {
  isOpen: boolean;
  onClose: () => void;
}

export const AddFolderModal: React.FC<Props> = ({ isOpen, onClose }) => {
  const [folderPath, setFolderPath] = useState("");
  const [isSubmitting, setIsSubmitting] = useState(false);
  const [errorMsg, setErrorMsg] = useState<string | null>(null);

  const addFolder = useFolderStore((s) => s.addFolder);

  if (!isOpen) return null;

  const handleSubmit = async (e: React.FormEvent) => {
    e.preventDefault();
    if (!folderPath.trim()) return;

    setErrorMsg(null);
    setIsSubmitting(true);

    const success = await addFolder(folderPath.trim());
    setIsSubmitting(false);

    if (success) {
      setFolderPath("");
      onClose();
    } else {
      setErrorMsg("添加被拦截：该路径与现有监控目录存在父子包含重叠，或命中了系统受保护核心目录！");
    }
  };

  return (
    <div className="fixed inset-0 z-50 flex items-center justify-center bg-black/40 backdrop-blur-sm p-4">
      <div className="w-full max-w-md bg-white dark:bg-[#252525] rounded-2xl shadow-2xl border border-black/10 dark:border-white/10 overflow-hidden">
        {/* Header */}
        <div className="px-5 py-4 border-b border-black/10 dark:border-white/10 flex items-center justify-between">
          <div className="flex items-center gap-2 text-sm font-semibold text-gray-900 dark:text-white">
            <FolderPlus className="w-4 h-4 text-mac-accent" />
            <span>添加监控目录</span>
          </div>
          <button
            onClick={onClose}
            className="p-1 text-gray-400 hover:text-gray-600 dark:hover:text-gray-200 rounded-md transition"
          >
            <X className="w-4 h-4" />
          </button>
        </div>

        {/* Form Body */}
        <form onSubmit={handleSubmit} className="p-5 space-y-4">
          <div>
            <label className="block text-xs font-medium text-gray-700 dark:text-gray-300 mb-1.5">
              目录绝对路径
            </label>
            <input
              type="text"
              autoFocus
              value={folderPath}
              onChange={(e) => {
                setFolderPath(e.target.value);
                setErrorMsg(null);
              }}
              placeholder="例如: /Users/username/Desktop 或 ~/Downloads"
              className="w-full px-3 py-2 text-xs rounded-lg border border-gray-300 dark:border-gray-700 bg-white dark:bg-[#1a1a1a] text-gray-900 dark:text-gray-100 focus:outline-none focus:ring-2 focus:ring-mac-accent"
            />
          </div>

          {/* Quick presets */}
          <div className="flex flex-wrap gap-1.5 text-[11px] text-gray-500 dark:text-gray-400">
            <span>常用目录:</span>
            {["Downloads", "Desktop", "Documents"].map((name) => (
              <button
                type="button"
                key={name}
                onClick={() => setFolderPath(`~/${name}`)}
                className="px-2 py-0.5 rounded bg-black/5 dark:bg-white/10 hover:bg-black/10 transition"
              >
                ~/{name}
              </button>
            ))}
          </div>

          {/* Error Alert */}
          {errorMsg && (
            <div className="p-3 bg-red-50 dark:bg-red-950/30 border border-red-200 dark:border-red-900/50 rounded-xl flex items-start gap-2 text-xs text-mac-danger">
              <ShieldAlert className="w-4 h-4 shrink-0 mt-0.5" />
              <span>{errorMsg}</span>
            </div>
          )}

          <div className="p-3 bg-blue-50/50 dark:bg-blue-950/20 border border-blue-200/50 dark:border-blue-800/30 rounded-xl flex items-start gap-2 text-[11px] text-blue-600 dark:text-blue-300">
            <AlertCircle className="w-3.5 h-3.5 shrink-0 mt-0.5" />
            <span>为保障数据绝对安全，系统严禁监控父子嵌套重叠的文件夹及系统核心目录。</span>
          </div>

          {/* Actions */}
          <div className="pt-2 flex items-center justify-end gap-2">
            <button
              type="button"
              onClick={onClose}
              className="px-3 py-1.5 text-xs font-medium rounded-lg text-gray-600 dark:text-gray-300 hover:bg-black/5 dark:hover:bg-white/10 transition"
            >
              取消
            </button>
            <button
              type="submit"
              disabled={isSubmitting || !folderPath.trim()}
              className="px-4 py-1.5 text-xs font-medium rounded-lg bg-mac-accent text-white hover:bg-blue-600 disabled:opacity-50 transition"
            >
              {isSubmitting ? "校验中..." : "确认添加"}
            </button>
          </div>
        </form>
      </div>
    </div>
  );
};
