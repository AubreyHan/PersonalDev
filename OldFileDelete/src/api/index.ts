import { invoke } from "@tauri-apps/api/core";
import { FolderItem, FolderRule, TrashRuleConfig } from "../types";

export const api = {
  getAllFolders: () => invoke<FolderItem[]>("get_all_folders"),
  addFolder: (path: string) => invoke<FolderItem>("add_folder", { path }),
  removeFolder: (folderId: string) => invoke<boolean>("remove_folder", { folderId }),
  updateTopWhitelist: (folderId: string, whitelist: string) =>
    invoke<boolean>("update_top_whitelist", { folderId, whitelist }),

  saveRule: (folderId: string, rule: FolderRule) =>
    invoke<FolderRule>("save_rule", { folderId, rule }),
  deleteRule: (folderId: string, ruleId: string) =>
    invoke<boolean>("delete_rule", { folderId, ruleId }),
  reorderRules: (folderId: string, ruleIds: string[]) =>
    invoke<boolean>("reorder_rules", { folderId, ruleIds }),

  getTrashConfig: () => invoke<TrashRuleConfig>("get_trash_config"),
  saveTrashConfig: (config: TrashRuleConfig) =>
    invoke<boolean>("save_trash_config", { config }),

  triggerScanNow: (folderId?: string) =>
    invoke<boolean>("trigger_scan_now", { folderId }),
  respondAskItem: (itemPath: string, decision: "trash" | "keep") =>
    invoke<boolean>("respond_ask_item", { itemPath, decision }),

  getAutostartStatus: () => invoke<boolean>("get_autostart_status"),
  setAutostart: (enabled: boolean) => invoke<boolean>("set_autostart", { enabled }),
  cleanUninstall: () => invoke<boolean>("clean_uninstall"),
};
