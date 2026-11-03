import { invoke } from "@tauri-apps/api/core";
import {
  ActivitiesPageResponse,
  ActivityQuery,
  AppSettings,
  GarminRegion,
  UserProfile,
} from "../types";

export interface CurrentUserResponse {
  isLoggedIn: boolean;
  user: UserProfile | null;
}

export interface AuthResponse {
  success: boolean;
  requireMfa: boolean;
  user?: UserProfile;
  error?: string;
}

export const api = {
  // 认证
  login: (payload: { region: GarminRegion; email: string; password: string; rememberMe: boolean }) =>
    invoke<AuthResponse>("auth_login", { payload }),

  submitMfa: (code: string) =>
    invoke<AuthResponse>("auth_submit_mfa", { payload: { code } }),

  getCurrentUser: () =>
    invoke<CurrentUserResponse>("auth_get_current_user"),

  logout: (clearSavedCredentials = false, account?: string) =>
    invoke<{ success: boolean }>("auth_logout", { clearSavedCredentials, account }),

  // 活动列表
  fetchActivitiesPage: (query: ActivityQuery) =>
    invoke<ActivitiesPageResponse>("activities_fetch_page", { query }),

  // 导出
  startBatchExport: (payload: {
    activityIds: number[];
    format: string;
    overrideExisting?: boolean;
    exportDirectory?: string;
    namingTemplate?: string;
  }) => invoke<{ queuedCount: number; jobId: string }>("export_start_batch", { payload }),

  startIncrementalExport: (payload: {
    format: string;
    exportDirectory?: string;
    namingTemplate?: string;
  }) => invoke<{ queuedCount: number; message?: string }>("export_start_incremental", { payload }),

  mergeActivities: (payload: {
    activityIds: number[];
    format: string;
    customFileName?: string;
    exportDirectory?: string;
  }) =>
    invoke<{ success: boolean; mergedFilePath: string; pointCount: number }>(
      "export_merge_activities",
      { payload }
    ),

  controlQueue: (action: "pause" | "resume" | "cancel") =>
    invoke<{ currentStatus: string }>("export_control_queue", { payload: { action } }),

  getSettings: () => invoke<AppSettings>("settings_get"),

  updateSettings: (payload: AppSettings) => invoke<{ success: boolean }>("settings_update", { payload }),
};
