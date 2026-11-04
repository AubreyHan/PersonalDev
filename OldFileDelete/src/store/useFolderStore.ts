import { create } from "zustand";
import { FolderItem, FolderRule, TrashRuleConfig } from "../types";
import { api } from "../api";
import { toast } from "sonner";

interface FolderState {
  folders: FolderItem[];
  selectedFolderId: string;
  trashConfig: TrashRuleConfig;
  isLoading: boolean;

  loadInitialData: () => Promise<void>;
  setSelectedFolderId: (id: string) => void;
  addFolder: (path: string) => Promise<boolean>;
  removeFolder: (id: string) => Promise<void>;
  updateTopWhitelist: (folderId: string, whitelist: string) => Promise<void>;
  saveRule: (folderId: string, rule: FolderRule) => Promise<void>;
  deleteRule: (folderId: string, ruleId: string) => Promise<void>;
  reorderRules: (folderId: string, ruleIds: string[]) => Promise<void>;
  saveTrashConfig: (config: TrashRuleConfig) => Promise<void>;
}

export const useFolderStore = create<FolderState>((set, get) => ({
  folders: [],
  selectedFolderId: "preset_downloads",
  trashConfig: { daysThreshold: 0, maxCapacityGb: 0, maxCapacityGB: 0 },
  isLoading: true,

  loadInitialData: async () => {
    try {
      const [folders, rawTrashConfig] = await Promise.all([
        api.getAllFolders(),
        api.getTrashConfig(),
      ]);
      const maxCap = rawTrashConfig.maxCapacityGb ?? rawTrashConfig.maxCapacityGB ?? 0;
      const trashConfig: TrashRuleConfig = {
        daysThreshold: rawTrashConfig.daysThreshold || 0,
        maxCapacityGb: maxCap,
        maxCapacityGB: maxCap,
      };
      set({ folders, trashConfig, isLoading: false });
      if (folders.length > 0 && !folders.some((f) => f.id === get().selectedFolderId)) {
        set({ selectedFolderId: folders[0].id });
      }
    } catch (err: any) {
      console.error("Failed to load initial data", err);
      set({ isLoading: false });
    }
  },

  setSelectedFolderId: (id: string) => set({ selectedFolderId: id }),

  addFolder: async (path: string) => {
    try {
      const newFolder = await api.addFolder(path);
      set((state) => ({
        folders: [...state.folders, newFolder],
        selectedFolderId: newFolder.id,
      }));
      toast.success(`成功添加监控目录: ${newFolder.name}`);
      return true;
    } catch (err: any) {
      toast.error(`添加失败: ${err}`);
      return false;
    }
  },

  removeFolder: async (id: string) => {
    try {
      await api.removeFolder(id);
      set((state) => {
        const remaining = state.folders.filter((f) => f.id !== id);
        return {
          folders: remaining,
          selectedFolderId:
            state.selectedFolderId === id
              ? remaining[0]?.id || "trash"
              : state.selectedFolderId,
        };
      });
      toast.success("已从监控列表移除目录");
    } catch (err: any) {
      toast.error(`移除失败: ${err}`);
    }
  },

  updateTopWhitelist: async (folderId: string, whitelist: string) => {
    try {
      await api.updateTopWhitelist(folderId, whitelist);
      set((state) => ({
        folders: state.folders.map((f) =>
          f.id === folderId ? { ...f, topWhitelist: whitelist } : f
        ),
      }));
    } catch (err: any) {
      toast.error(`更新置顶白名单失败: ${err}`);
    }
  },

  saveRule: async (folderId: string, rule: FolderRule) => {
    try {
      const saved = await api.saveRule(folderId, rule);
      set((state) => ({
        folders: state.folders.map((f) => {
          if (f.id !== folderId) return f;
          const exists = f.rules.some((r) => r.id === saved.id);
          const newRules = exists
            ? f.rules.map((r) => (r.id === saved.id ? saved : r))
            : [...f.rules, saved];
          return { ...f, rules: newRules };
        }),
      }));
      toast.success("规则已保存");
    } catch (err: any) {
      toast.error(`保存规则失败: ${err}`);
    }
  },

  deleteRule: async (folderId: string, ruleId: string) => {
    try {
      await api.deleteRule(folderId, ruleId);
      set((state) => ({
        folders: state.folders.map((f) =>
          f.id === folderId
            ? { ...f, rules: f.rules.filter((r) => r.id !== ruleId) }
            : f
        ),
      }));
      toast.success("规则已删除");
    } catch (err: any) {
      toast.error(`删除规则失败: ${err}`);
    }
  },

  reorderRules: async (folderId: string, ruleIds: string[]) => {
    try {
      await api.reorderRules(folderId, ruleIds);
      set((state) => ({
        folders: state.folders.map((f) => {
          if (f.id !== folderId) return f;
          const map = new Map(f.rules.map((r) => [r.id, r]));
          const reordered = ruleIds
            .map((id, idx) => {
              const rule = map.get(id);
              return rule ? { ...rule, order: idx } : null;
            })
            .filter((r): r is FolderRule => r !== null);
          return { ...f, rules: reordered };
        }),
      }));
    } catch (err: any) {
      toast.error(`调整顺序失败: ${err}`);
    }
  },

  saveTrashConfig: async (config: TrashRuleConfig) => {
    try {
      await api.saveTrashConfig(config);
      set({ trashConfig: config });
      toast.success("回收站配置已保存");
    } catch (err: any) {
      toast.error(`保存回收站配置失败: ${err}`);
    }
  },
}));
