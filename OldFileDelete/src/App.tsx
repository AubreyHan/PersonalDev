import React, { useState } from 'react';
import { Trash2, Folder, Plus, ShieldCheck, Play, ArrowDownWideNarrow } from 'lucide-react';
import { FolderItem } from './types';

export const App: React.FC = () => {
  const [folders] = useState<FolderItem[]>([
    {
      id: 'trash',
      name: '系统废纸篓',
      path: '~/.Trash',
      isTrash: true,
      isPreset: false,
      topWhitelist: '',
      rules: [],
      createdAt: Date.now(),
    },
    {
      id: 'downloads',
      name: 'Downloads',
      path: '~/Downloads',
      isTrash: false,
      isPreset: true,
      topWhitelist: '',
      rules: [],
      createdAt: Date.now(),
    },
  ]);

  const [selectedFolderId, setSelectedFolderId] = useState<string>('downloads');
  const selectedFolder = folders.find((f) => f.id === selectedFolderId);

  return (
    <div className="flex h-screen w-screen bg-[#f5f5f7] dark:bg-[#1e1e1e] text-sm text-gray-800 dark:text-gray-200 select-none">
      {/* Sidebar */}
      <aside className="w-64 border-r border-black/10 dark:border-white/10 flex flex-col bg-mac-sidebar dark:bg-mac-sidebarDark backdrop-blur-md">
        <div className="p-4 border-b border-black/10 dark:border-white/10 flex items-center justify-between">
          <span className="font-semibold text-xs tracking-wider uppercase text-gray-500 dark:text-gray-400">
            监控目录 (Folders)
          </span>
          <button
            title="添加文件夹"
            className="p-1 hover:bg-black/5 dark:hover:bg-white/10 rounded-md transition text-gray-600 dark:text-gray-300"
          >
            <Plus className="w-4 h-4" />
          </button>
        </div>

        <nav className="flex-1 overflow-y-auto p-2 space-y-1">
          {folders.map((folder) => {
            const isSelected = folder.id === selectedFolderId;
            return (
              <button
                key={folder.id}
                onClick={() => setSelectedFolderId(folder.id)}
                className={`w-full flex items-center gap-2.5 px-3 py-2 rounded-lg text-left transition ${
                  isSelected
                    ? 'bg-mac-accent text-white font-medium shadow-sm'
                    : 'hover:bg-black/5 dark:hover:bg-white/5 text-gray-700 dark:text-gray-300'
                }`}
              >
                {folder.isTrash ? (
                  <Trash2 className="w-4 h-4 shrink-0" />
                ) : (
                  <Folder className="w-4 h-4 shrink-0" />
                )}
                <span className="truncate flex-1">{folder.name}</span>
                {folder.isPreset && !isSelected && (
                  <span className="text-[10px] px-1.5 py-0.5 rounded bg-black/5 dark:bg-white/10 text-gray-500">
                    默认
                  </span>
                )}
              </button>
            );
          })}
        </nav>

        <div className="p-3 border-t border-black/10 dark:border-white/10 text-[11px] text-gray-400 flex items-center justify-between">
          <span>FilePurger v0.1.0</span>
          <span className="inline-flex items-center gap-1 text-mac-success">
            <span className="w-1.5 h-1.5 rounded-full bg-mac-success animate-pulse" />
            0.0% CPU
          </span>
        </div>
      </aside>

      {/* Main Workspace */}
      <main className="flex-1 flex flex-col bg-white dark:bg-[#252525] overflow-hidden">
        {/* Header */}
        <header className="h-14 px-6 border-b border-black/10 dark:border-white/10 flex items-center justify-between">
          <div>
            <h1 className="text-base font-semibold text-gray-900 dark:text-white">
              {selectedFolder?.name}
            </h1>
            <p className="text-xs text-gray-400 truncate max-w-md">
              {selectedFolder?.path}
            </p>
          </div>

          <div className="flex items-center gap-2">
            <button
              className="flex items-center gap-1.5 px-3 py-1.5 text-xs font-medium rounded-md bg-black/5 hover:bg-black/10 dark:bg-white/10 dark:hover:bg-white/15 transition text-gray-700 dark:text-gray-200"
            >
              <Play className="w-3.5 h-3.5" />
              立即扫描
            </button>
          </div>
        </header>

        {/* Workspace Body */}
        <div className="flex-1 overflow-y-auto p-6 space-y-6">
          {/* Top Whitelist Bar */}
          <section className="bg-blue-50/50 dark:bg-blue-950/20 border border-blue-200/60 dark:border-blue-800/40 rounded-xl p-4">
            <div className="flex items-center gap-2 mb-2 text-blue-700 dark:text-blue-300 font-medium text-xs uppercase tracking-wider">
              <ShieldCheck className="w-4 h-4" />
              <span>置顶保留白名单 (Top Whitelist - 最高优先级永久免死)</span>
            </div>
            <input
              type="text"
              placeholder="输入通配符规则，逗号隔开 (如: keep*, *.important, **/backup*)"
              className="w-full text-xs px-3 py-2 rounded-lg border border-blue-200 dark:border-blue-800/60 bg-white dark:bg-[#1a1a1a] text-gray-800 dark:text-gray-200 focus:outline-none focus:ring-2 focus:ring-blue-500/30"
            />
          </section>

          {/* Rules Waterfall Section */}
          <section className="space-y-3">
            <div className="flex items-center justify-between">
              <div className="flex items-center gap-2 text-xs font-semibold text-gray-500 dark:text-gray-400 uppercase tracking-wider">
                <ArrowDownWideNarrow className="w-4 h-4" />
                <span>瀑布规则链 (Waterfall Rules - 从上到下首命中短路)</span>
              </div>
              <button
                className="text-xs font-medium text-mac-accent hover:underline flex items-center gap-1"
              >
                <Plus className="w-3.5 h-3.5" />
                新建规则
              </button>
            </div>

            <div className="border border-dashed border-gray-300 dark:border-gray-700 rounded-xl p-8 text-center text-gray-400 text-xs">
              暂无规则，点击上方“新建规则”为该文件夹添加自动清理规则。
            </div>
          </section>
        </div>
      </main>
    </div>
  );
};

export default App;
