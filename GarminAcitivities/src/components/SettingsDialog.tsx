import React, { useEffect, useState } from "react";
import { useExportStore } from "../store/exportStore";
import { Save, Settings, X } from "lucide-react";
import { AppSettings } from "../types";

export const SettingsDialog: React.FC = () => {
  const { isSettingsOpen, closeSettings, settings, updateSettings, fetchSettings } = useExportStore();

  const [form, setForm] = useState<AppSettings>({
    exportDirectory: "",
    defaultFormat: "gpx",
    namingTemplate: "{date}_{type}_{name}_{id}",
    concurrency: 2,
    delayJitterMs: 500,
  });

  useEffect(() => {
    if (settings) {
      setForm(settings);
    } else {
      fetchSettings();
    }
  }, [settings, fetchSettings]);

  if (!isSettingsOpen) return null;

  // 实时预览最终文件名示例
  const renderLivePreview = () => {
    const tpl = form.namingTemplate || "{date}_{type}_{name}_{id}";
    let preview = tpl
      .replace("{date}", "2026-11-03")
      .replace("{compact_date}", "20261103")
      .replace("{time}", "0630")
      .replace("{id}", "16892401234")
      .replace("{type}", "跑步")
      .replace("{type_en}", "running")
      .replace("{name}", "奥森公园晨跑 10K")
      .replace("{dist_km}", "10.1km");

    return `${preview}.${form.defaultFormat.toLowerCase()}`;
  };

  const handleSave = async (e: React.FormEvent) => {
    e.preventDefault();
    await updateSettings(form);
  };

  return (
    <div className="fixed inset-0 z-50 flex items-center justify-center bg-black/75 backdrop-blur-sm p-4">
      <div className="w-full max-w-lg bg-[#1e1e1e] border border-gray-800 rounded-2xl shadow-2xl p-6 text-gray-100 animate-in fade-in zoom-in-95 duration-200">
        {/* Header */}
        <div className="flex items-center justify-between pb-4 border-b border-gray-800 mb-5">
          <div className="flex items-center space-x-2.5 text-sky-400">
            <Settings className="w-5 h-5" />
            <h3 className="font-bold text-base text-white">导出与存储偏好设置</h3>
          </div>
          <button
            onClick={closeSettings}
            className="p-1 text-gray-400 hover:text-white rounded-lg hover:bg-gray-800 transition-colors"
          >
            <X className="w-4 h-4" />
          </button>
        </div>

        <form onSubmit={handleSave} className="space-y-4">
          {/* Export Directory */}
          <div>
            <label className="block text-xs font-medium text-gray-300 mb-1.5 flex items-center justify-between">
              <span>导出保存根目录 (平铺直存)</span>
              <span className="text-[10px] text-emerald-400 font-normal">不创建任何多余年/月子目录</span>
            </label>
            <div className="flex items-center space-x-2">
              <input
                type="text"
                required
                value={form.exportDirectory}
                onChange={(e) => setForm({ ...form, exportDirectory: e.target.value })}
                className="flex-1 bg-[#161616] border border-gray-800 rounded-lg px-3 py-2 text-xs text-white placeholder-gray-600 focus:outline-none focus:border-sky-500 font-mono"
              />
            </div>
            <p className="text-[11px] text-gray-500 mt-1">
              所有 GPX/TCX 文件均直接写入此文件夹，配合活动 ID 彻底避免重名覆盖。
            </p>
          </div>

          {/* Export Format */}
          <div>
            <label className="block text-xs font-medium text-gray-300 mb-1.5">默认轨迹导出格式</label>
            <div className="grid grid-cols-2 gap-3">
              <label
                className={`flex items-center space-x-2 p-3 rounded-lg border cursor-pointer transition-all ${
                  form.defaultFormat === "gpx"
                    ? "bg-sky-500/10 border-sky-500 text-white"
                    : "bg-[#161616] border-gray-800 text-gray-400 hover:border-gray-700"
                }`}
              >
                <input
                  type="radio"
                  name="format"
                  value="gpx"
                  checked={form.defaultFormat === "gpx"}
                  onChange={() => setForm({ ...form, defaultFormat: "gpx" })}
                  className="hidden"
                />
                <span className="text-xs font-semibold">GPX 格式 (通用标准)</span>
              </label>

              <label
                className={`flex items-center space-x-2 p-3 rounded-lg border cursor-pointer transition-all ${
                  form.defaultFormat === "tcx"
                    ? "bg-sky-500/10 border-sky-500 text-white"
                    : "bg-[#161616] border-gray-800 text-gray-400 hover:border-gray-700"
                }`}
              >
                <input
                  type="radio"
                  name="format"
                  value="tcx"
                  checked={form.defaultFormat === "tcx"}
                  onChange={() => setForm({ ...form, defaultFormat: "tcx" })}
                  className="hidden"
                />
                <span className="text-xs font-semibold">TCX 格式 (分圈训练)</span>
              </label>
            </div>
          </div>

          {/* Naming Template */}
          <div>
            <label className="block text-xs font-medium text-gray-300 mb-1.5">
              文件名命名规则模板
            </label>
            <input
              type="text"
              required
              value={form.namingTemplate}
              onChange={(e) => setForm({ ...form, namingTemplate: e.target.value })}
              className="w-full bg-[#161616] border border-gray-800 rounded-lg px-3 py-2 text-xs text-white placeholder-gray-600 focus:outline-none focus:border-sky-500 font-mono"
            />
            {/* Live Preview Box */}
            <div className="mt-2 p-2.5 bg-[#141414] border border-gray-800/80 rounded-lg">
              <div className="text-[10px] text-gray-500 mb-1 flex items-center space-x-1">
                <span>实时生成效果预览：</span>
              </div>
              <div className="text-xs font-mono text-sky-400 break-all select-all">
                {renderLivePreview()}
              </div>
            </div>

            {/* Template tags reference */}
            <div className="mt-2 text-[10px] text-gray-500 flex flex-wrap gap-1.5">
              <span>支持占位符:</span>
              <code className="text-gray-300 bg-gray-800/60 px-1 py-0.5 rounded">{"{date}"}</code>
              <code className="text-gray-300 bg-gray-800/60 px-1 py-0.5 rounded">{"{time}"}</code>
              <code className="text-gray-300 bg-gray-800/60 px-1 py-0.5 rounded">{"{type}"}</code>
              <code className="text-gray-300 bg-gray-800/60 px-1 py-0.5 rounded">{"{name}"}</code>
              <code className="text-gray-300 bg-gray-800/60 px-1 py-0.5 rounded">{"{dist_km}"}</code>
              <code className="text-sky-300 bg-sky-950/40 px-1 py-0.5 rounded">{"{id}"}</code>
            </div>
          </div>

          {/* Actions */}
          <div className="pt-3 flex justify-end space-x-2 border-t border-gray-800">
            <button
              type="button"
              onClick={closeSettings}
              className="px-4 py-2 bg-gray-800 hover:bg-gray-700 text-gray-300 rounded-lg text-xs transition-colors"
            >
              取消
            </button>
            <button
              type="submit"
              className="px-5 py-2 bg-sky-500 hover:bg-sky-400 text-white font-medium rounded-lg text-xs shadow-md shadow-sky-500/20 transition-all flex items-center space-x-1.5"
            >
              <Save className="w-3.5 h-3.5" />
              <span>保存配置</span>
            </button>
          </div>
        </form>
      </div>
    </div>
  );
};
