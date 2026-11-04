import React, { useEffect, useState } from "react";
import {
  Trash2,
  Folder,
  Plus,
  Play,
  ArrowDownWideNarrow,
  Trash,
} from "lucide-react";
import { DragDropContext, Droppable, Draggable, DropResult } from "@hello-pangea/dnd";
import { listen } from "@tauri-apps/api/event";

import { useFolderStore } from "./store/useFolderStore";
import { useAskQueueStore } from "./store/useAskQueueStore";
import { useMatchListStore } from "./store/useMatchListStore";
import { api } from "./api";
import { FolderRule } from "./types";

import { TopWhitelistBar } from "./components/Rules/TopWhitelistBar";
import { RuleCard } from "./components/Rules/RuleCard";
import { CreateRuleModal } from "./components/Rules/CreateRuleModal";
import { TrashRuleView } from "./components/Rules/TrashRuleView";
import { MatchListTable } from "./components/Rules/MatchListTable";
import { AddFolderModal } from "./components/Sidebar/AddFolderModal";
import { AskConfirmModal } from "./components/Modals/AskConfirmModal";
import { UninstallModal } from "./components/Modals/UninstallModal";

export const App: React.FC = () => {
  const {
    folders,
    selectedFolderId,
    loadInitialData,
    setSelectedFolderId,
    removeFolder,
    saveRule,
    deleteRule,
    reorderRules,
  } = useFolderStore();

  const addAskItems = useAskQueueStore((s) => s.addItems);
  const setMatches = useMatchListStore((s) => s.setMatches);

  const [isAddModalOpen, setIsAddModalOpen] = useState(false);
  const [isRuleModalOpen, setIsRuleModalOpen] = useState(false);
  const [editingRule, setEditingRule] = useState<FolderRule | null>(null);
  const [isUninstallModalOpen, setIsUninstallModalOpen] = useState(false);
  const [isScanning, setIsScanning] = useState(false);

  useEffect(() => {
    loadInitialData();

    // Listen for backend Tauri events
    const unlistenAsk = listen("app:ask_queue_update", (event: any) => {
      if (Array.isArray(event.payload)) {
        addAskItems(event.payload);
      }
    });

    const unlistenMatches = listen("app:match_list_update", (event: any) => {
      if (Array.isArray(event.payload)) {
        setMatches(event.payload);
      }
    });

    const unlistenUninstall = listen("app:request_uninstall", () => {
      setIsUninstallModalOpen(true);
    });

    return () => {
      unlistenAsk.then((fn) => fn());
      unlistenMatches.then((fn) => fn());
      unlistenUninstall.then((fn) => fn());
    };
  }, []);

  const selectedFolder = folders.find((f) => f.id === selectedFolderId);

  const handleManualScan = async () => {
    setIsScanning(true);
    try {
      await api.triggerScanNow(selectedFolder?.isTrash ? undefined : selectedFolder?.id);
    } catch (e) {
      console.error(e);
    } finally {
      setIsScanning(false);
    }
  };

  const handleDragEnd = (result: DropResult) => {
    if (!result.destination || !selectedFolder) return;
    const items = Array.from(selectedFolder.rules);
    const [reorderedItem] = items.splice(result.source.index, 1);
    items.splice(result.destination.index, 0, reorderedItem);

    const newIds = items.map((r) => r.id);
    reorderRules(selectedFolder.id, newIds);
  };

  return (
    <div className="flex h-screen w-screen bg-[#f5f5f7] dark:bg-[#1e1e1e] text-sm text-gray-800 dark:text-gray-200 select-none">
      {/* Left Sidebar */}
      <aside className="w-64 border-r border-black/10 dark:border-white/10 flex flex-col bg-mac-sidebar dark:bg-mac-sidebarDark backdrop-blur-md">
        <div className="p-4 border-b border-black/10 dark:border-white/10 flex items-center justify-between">
          <span className="font-semibold text-xs tracking-wider uppercase text-gray-500 dark:text-gray-400">
            监控目录 (Folders)
          </span>
          <button
            onClick={() => setIsAddModalOpen(true)}
            title="添加监控目录"
            className="p-1 hover:bg-black/5 dark:hover:bg-white/10 rounded-md transition text-gray-600 dark:text-gray-300"
          >
            <Plus className="w-4 h-4" />
          </button>
        </div>

        <nav className="flex-1 overflow-y-auto p-2 space-y-1">
          {folders.map((folder) => {
            const isSelected = folder.id === selectedFolderId;
            return (
              <div
                key={folder.id}
                onClick={() => setSelectedFolderId(folder.id)}
                className={`group relative w-full flex items-center gap-2.5 px-3 py-2 rounded-lg cursor-pointer text-left transition ${
                  isSelected
                    ? "bg-mac-accent text-white font-medium shadow-sm"
                    : "hover:bg-black/5 dark:hover:bg-white/5 text-gray-700 dark:text-gray-300"
                }`}
              >
                {folder.isTrash ? (
                  <Trash2 className="w-4 h-4 shrink-0" />
                ) : (
                  <Folder className="w-4 h-4 shrink-0" />
                )}
                <span className="truncate flex-1 text-xs">{folder.name}</span>

                {folder.isPreset && !isSelected && (
                  <span className="text-[10px] px-1.5 py-0.5 rounded bg-black/5 dark:bg-white/10 text-gray-500">
                    预置
                  </span>
                )}

                {/* Remove button for custom folders */}
                {!folder.isTrash && !folder.isPreset && (
                  <button
                    onClick={(e) => {
                      e.stopPropagation();
                      removeFolder(folder.id);
                    }}
                    title="从监控列表中移除"
                    className={`opacity-0 group-hover:opacity-100 p-1 rounded hover:bg-black/10 dark:hover:bg-white/20 transition ${
                      isSelected ? "text-white" : "text-gray-400 hover:text-red-500"
                    }`}
                  >
                    <Trash className="w-3 h-3" />
                  </button>
                )}
              </div>
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
        <header className="h-14 px-6 border-b border-black/10 dark:border-white/10 flex items-center justify-between shrink-0">
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
              onClick={handleManualScan}
              disabled={isScanning}
              className="flex items-center gap-1.5 px-3 py-1.5 text-xs font-medium rounded-lg bg-black/5 hover:bg-black/10 dark:bg-white/10 dark:hover:bg-white/15 transition text-gray-700 dark:text-gray-200 disabled:opacity-50"
            >
              <Play className="w-3.5 h-3.5" />
              {isScanning ? "正在扫描..." : "立即扫描"}
            </button>
          </div>
        </header>

        {/* Workspace Body */}
        <div className="flex-1 overflow-y-auto p-6 space-y-6">
          {selectedFolder?.isTrash ? (
            <TrashRuleView />
          ) : (
            <>
              {/* Top Whitelist Bar */}
              <TopWhitelistBar
                folderId={selectedFolder?.id || ""}
                initialValue={selectedFolder?.topWhitelist || ""}
              />

              {/* Waterfall Rules Section */}
              <section className="space-y-3">
                <div className="flex items-center justify-between">
                  <div className="flex items-center gap-2 text-xs font-semibold text-gray-500 dark:text-gray-400 uppercase tracking-wider">
                    <ArrowDownWideNarrow className="w-4 h-4 text-mac-accent" />
                    <span>瀑布规则链 (Waterfall Rules - 从上到下首命中短路)</span>
                  </div>
                  <button
                    onClick={() => {
                      setEditingRule(null);
                      setIsRuleModalOpen(true);
                    }}
                    className="text-xs font-medium text-mac-accent hover:underline flex items-center gap-1"
                  >
                    <Plus className="w-3.5 h-3.5" />
                    新建规则
                  </button>
                </div>

                {/* Drag and drop rules */}
                {(!selectedFolder?.rules || selectedFolder.rules.length === 0) ? (
                  <div className="border border-dashed border-gray-300 dark:border-gray-700 rounded-xl p-8 text-center text-gray-400 text-xs">
                    暂无规则，点击右上角“新建规则”为该文件夹添加清理规则。
                  </div>
                ) : (
                  <DragDropContext onDragEnd={handleDragEnd}>
                    <Droppable droppableId="rules-list">
                      {(provided) => (
                        <div
                          {...provided.droppableProps}
                          ref={provided.innerRef}
                          className="space-y-2.5"
                        >
                          {selectedFolder.rules.map((rule, index) => (
                            <Draggable key={rule.id} draggableId={rule.id} index={index}>
                              {(providedDraggable) => (
                                <div
                                  ref={providedDraggable.innerRef}
                                  {...providedDraggable.draggableProps}
                                  {...providedDraggable.dragHandleProps}
                                >
                                  <RuleCard
                                    rule={rule}
                                    onToggle={(enabled) =>
                                      saveRule(selectedFolder.id, { ...rule, enabled })
                                    }
                                    onEdit={() => {
                                      setEditingRule(rule);
                                      setIsRuleModalOpen(true);
                                    }}
                                    onDelete={() =>
                                      deleteRule(selectedFolder.id, rule.id)
                                    }
                                  />
                                </div>
                              )}
                            </Draggable>
                          ))}
                          {provided.placeholder}
                        </div>
                      )}
                    </Droppable>
                  </DragDropContext>
                )}
              </section>

              {/* Match List Table */}
              <MatchListTable />
            </>
          )}
        </div>
      </main>

      {/* Modals */}
      <AddFolderModal
        isOpen={isAddModalOpen}
        onClose={() => setIsAddModalOpen(false)}
      />

      <CreateRuleModal
        isOpen={isRuleModalOpen}
        folderId={selectedFolder?.id || ""}
        initialRule={editingRule}
        onClose={() => {
          setIsRuleModalOpen(false);
          setEditingRule(null);
        }}
        onSave={(rule) => {
          if (selectedFolder) {
            saveRule(selectedFolder.id, rule);
          }
        }}
      />

      <AskConfirmModal />

      <UninstallModal
        isOpen={isUninstallModalOpen}
        onClose={() => setIsUninstallModalOpen(false)}
      />
    </div>
  );
};

export default App;
