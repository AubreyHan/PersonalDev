import { create } from "zustand";
import { MatchListItem } from "../types";

interface MatchListState {
  matches: MatchListItem[];
  setMatches: (items: MatchListItem[]) => void;
  clearMatches: () => void;
}

export const useMatchListStore = create<MatchListState>((set) => ({
  matches: [],
  setMatches: (items: MatchListItem[]) => set({ matches: items }),
  clearMatches: () => set({ matches: [] }),
}));
