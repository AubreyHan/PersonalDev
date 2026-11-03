import React, { useState } from "react";
import { AlertTriangle, Trash2, X } from "lucide-react";
import { api } from "../../api";

interface Props {
  isOpen: boolean;
  onClose: () => void;
}

export const UninstallModal: React.FC<Props> = ({ isOpen, onClose }) => {
  const [isUninstalling, setIsUninstalling] = useState(false);

  if (!isOpen) return null;

  const handleConfirm = async () => {
    setIsUninstalling(true);
    try {
      await api.cleanUninstall();
    } catch (e) {
      console.error(e);
      setIsUninstalling(false);
    }
  };

  return (
    <div className="fixed inset-0 z-50 flex items-center justify-center bg-black/50 backdrop-blur-sm p-4">
      <div className="w-full max-w-sm bg-white dark:bg-[#252525] rounded-2xl shadow-2xl border border-black/10 dark:border-white/10 overflow-hidden">
        <div className="p-5 text-center space-y-3">
          <div className="w-12 h-12 rounded-full bg-red-100 dark:bg-red-950/40 text-mac-danger mx-auto flex items-center justify-center">
            <AlertTriangle className="w-6 h-6" />
          </div>

          <h3 className="text-sm font-bold text-gray-900 dark:text-white">
            彻底卸载 FilePurger？
          </h3>

          <p className="text-xs text-gray-500 dark:text-gray-400">
            执行彻底卸载将注销 macOS 开机启动项、永久擦除本地所有配置，并将应用程序自身移入废纸篓退出，不留任何残留垃圾。
          </p>

          <div className="grid grid-cols-2 gap-2 pt-2">
            <button
              onClick={onClose}
              disabled={isUninstalling}
              className="py-2 text-xs font-medium rounded-lg text-gray-700 dark:text-gray-300 hover:bg-black/5 transition"
            >
              取消
            </button>
            <button
              onClick={handleConfirm}
              disabled={isUninstalling}
              className="py-2 text-xs font-semibold rounded-lg bg-mac-danger hover:bg-red-600 text-white transition shadow-sm disabled:opacity-50"
            >
              {isUninstalling ? "卸载中..." : "彻底卸载并退出"}
            </button>
          </div>
        </div>
      </div>
    </div>
  );
};
