import { create } from "zustand";
import { api } from "../api/client";
import { ActivityListItem, ActivityQuery } from "../types";

interface ActivityState {
  items: ActivityListItem[];
  totalCount: number;
  isLoading: boolean;
  fromCache: boolean;
  selectedIds: Set<number>;
  query: ActivityQuery;

  setQuery: (patch: Partial<ActivityQuery>) => void;
  fetchActivities: () => Promise<void>;
  toggleSelect: (id: number) => void;
  selectAll: () => void;
  clearSelection: () => void;
  updateItemExportStatus: (activityId: number, path: string) => void;
}

export const useActivityStore = create<ActivityState>((set, get) => ({
  items: [],
  totalCount: 0,
  isLoading: false,
  fromCache: false,
  selectedIds: new Set<number>(),
  query: {
    start: 0,
    limit: 50,
    activityType: "all",
    startDate: "",
    endDate: "",
  },

  setQuery: (patch) => {
    set((state) => ({
      query: { ...state.query, ...patch, start: 0 }, // 重置分页到第一页
    }));
    get().fetchActivities();
  },

  fetchActivities: async () => {
    set({ isLoading: true });
    try {
      const q = get().query;
      const res = await api.fetchActivitiesPage(q);
      set({
        items: res.items,
        totalCount: res.totalCount,
        fromCache: res.fromCache,
        isLoading: false,
      });
    } catch (err) {
      console.error("Failed to fetch activities:", err);
      set({ isLoading: false });
    }
  },

  toggleSelect: (id) => {
    set((state) => {
      const next = new Set(state.selectedIds);
      if (next.has(id)) {
        next.delete(id);
      } else {
        next.add(id);
      }
      return { selectedIds: next };
    });
  },

  selectAll: () => {
    set((state) => {
      if (state.selectedIds.size === state.items.length) {
        return { selectedIds: new Set() };
      }
      return { selectedIds: new Set(state.items.map((i) => i.activityId)) };
    });
  },

  clearSelection: () => set({ selectedIds: new Set() }),

  updateItemExportStatus: (activityId, path) => {
    set((state) => ({
      items: state.items.map((item) =>
        item.activityId === activityId
          ? { ...item, isExported: true, localFilePath: path }
          : item
      ),
    }));
  },
}));
