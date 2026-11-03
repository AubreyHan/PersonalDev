import { create } from "zustand";
import { api } from "../api/client";
import { GarminRegion, UserProfile } from "../types";

interface AuthState {
  user: UserProfile | null;
  isLoggedIn: boolean;
  isLoading: boolean;
  mfaRequired: boolean;
  error: string | null;

  checkAuth: () => Promise<void>;
  login: (region: GarminRegion, email: string, pass: string, remember: boolean) => Promise<boolean>;
  submitMfa: (code: string) => Promise<boolean>;
  logout: () => Promise<void>;
  clearError: () => void;
}

export const useAuthStore = create<AuthState>((set, get) => ({
  user: null,
  isLoggedIn: false,
  isLoading: false,
  mfaRequired: false,
  error: null,

  checkAuth: async () => {
    set({ isLoading: true });
    try {
      const res = await api.getCurrentUser();
      if (res.isLoggedIn && res.user) {
        set({ user: res.user, isLoggedIn: true, isLoading: false });
      } else {
        set({ user: null, isLoggedIn: false, isLoading: false });
      }
    } catch {
      set({ user: null, isLoggedIn: false, isLoading: false });
    }
  },

  login: async (region, email, password, rememberMe) => {
    set({ isLoading: true, error: null, mfaRequired: false });
    try {
      const res = await api.login({ region, email, password, rememberMe });
      if (res.success && res.user) {
        set({ user: res.user, isLoggedIn: true, isLoading: false });
        return true;
      } else if (res.requireMfa) {
        set({ mfaRequired: true, isLoading: false });
        return false;
      } else {
        set({ error: res.error || "登录失败，请检查账号密码", isLoading: false });
        return false;
      }
    } catch (err: any) {
      set({ error: err?.toString() || "网络请求异常", isLoading: false });
      return false;
    }
  },

  submitMfa: async (code) => {
    set({ isLoading: true, error: null });
    try {
      const res = await api.submitMfa(code);
      if (res.success && res.user) {
        set({ user: res.user, isLoggedIn: true, mfaRequired: false, isLoading: false });
        return true;
      } else {
        set({ error: res.error || "验证码错误或已失效", isLoading: false });
        return false;
      }
    } catch (err: any) {
      set({ error: err?.toString() || "提交验证码失败", isLoading: false });
      return false;
    }
  },

  logout: async () => {
    const user = get().user;
    try {
      await api.logout(true, user?.displayName);
    } finally {
      set({ user: null, isLoggedIn: false, mfaRequired: false, error: null });
    }
  },

  clearError: () => set({ error: null }),
}));
