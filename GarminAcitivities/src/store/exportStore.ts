import { create } from "zustand";
import { listen, UnlistenFn } from "@tauri-apps/api/event";
import { api } from "../api/client";
import { AppSettings, QueueSummaryEvent, TaskProgressEvent } from "../types";

interface ExportState {
  tasks: Record<number, TaskProgressEvent>;
  summary: QueueSummaryEvent | null;
  isDrawerOpen: boolean;
  settings: AppSettings | null;
  isSettingsOpen: boolean;

  openDrawer: () => void;
  closeDrawer: () => void;
  toggleDrawer: () => void;
  openSettings: () => void;
  closeSettings: () => void;

  fetchSettings: () => Promise<void>;
  updateSettings: (payload: AppSettings) => Promise<boolean>;

  initEventListeners: () => Promise<UnlistenFn[]>;
  pauseQueue: () => Promise<void>;
  resumeQueue: () => Promise<void>;
  cancelQueue: () => Promise<void>;
}

export const useExportStore = create<ExportState>((set) => ({
  tasks: {},
  summary: null,
  isDrawerOpen: false,
  settings: null,
  isSettingsOpen: false,

  openDrawer: () => set({ isDrawerOpen: true }),
  closeDrawer: () => set({ isDrawerOpen: false }),
  toggleDrawer: () => set((state) => ({ isDrawerOpen: !state.isDrawerOpen })),
  openSettings: () => set({ isSettingsOpen: true }),
  closeSettings: () => set({ isSettingsOpen: false }),

  fetchSettings: async () => {
    try {
      const s = await api.getSettings();
      set({ settings: s });
    } catch (e) {
      console.error("Failed to load settings:", e);
    }
  },

  updateSettings: async (payload) => {
    try {
      await api.updateSettings(payload);
      set({ settings: payload, isSettingsOpen: false });
      return true;
    } catch (e) {
      console.error("Failed to update settings:", e);
      return false;
    }
  },

  initEventListeners: async () => {
    const unlistenTask = await listen<TaskProgressEvent>("export:task-progress", (event) => {
      const payload = event.payload;
      set((state) => ({
        tasks: {
          ...state.tasks,
          [payload.activityId]: payload,
        },
      }));
    });

    const unlistenSummary = await listen<QueueSummaryEvent>("export:queue-summary", (event) => {
      set({ summary: event.payload });
    });

    return [unlistenTask, unlistenSummary];
  },

  pauseQueue: async () => {
    await api.controlQueue("pause");
    set((state) => ({
      summary: state.summary ? { ...state.summary, isPaused: true } : null,
    }));
  },

  resumeQueue: async () => {
    await api.controlQueue("resume");
    set((state) => ({
      summary: state.summary ? { ...state.summary, isPaused: false } : null,
    }));
  },

  cancelQueue: async () => {
    await api.controlQueue("cancel");
    set((state) => ({
      summary: state.summary
        ? { ...state.summary, remainingTasks: 0, isPaused: false }
        : null,
    }));
  },
}));
