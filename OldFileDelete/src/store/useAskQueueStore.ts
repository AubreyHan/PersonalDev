import { create } from "zustand";
import { AskItem } from "../types";
import { api } from "../api";
import { toast } from "sonner";

interface AskQueueState {
  queue: AskItem[];
  addItems: (items: AskItem[]) => void;
  popItem: () => void;
  handleDecision: (decision: "trash" | "keep") => Promise<void>;
}

export const useAskQueueStore = create<AskQueueState>((set, get) => ({
  queue: [],

  addItems: (items: AskItem[]) => {
    set((state) => {
      // De-duplicate by file path
      const existingPaths = new Set(state.queue.map((i) => i.filePath));
      const newItems = items.filter((i) => !existingPaths.has(i.filePath));
      return { queue: [...state.queue, ...newItems] };
    });
  },

  popItem: () => {
    set((state) => ({ queue: state.queue.slice(1) }));
  },

  handleDecision: async (decision: "trash" | "keep") => {
    const current = get().queue[0];
    if (!current) return;

    try {
      await api.respondAskItem(current.filePath, decision);
      if (decision === "trash") {
        toast.info(`已移至废纸篓: ${current.fileName}`);
      } else {
        toast.info(`已保留文件: ${current.fileName}`);
      }
      get().popItem();
    } catch (err: any) {
      toast.error(`操作失败: ${err}`);
    }
  },
}));
