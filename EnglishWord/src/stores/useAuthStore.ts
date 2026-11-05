import { create } from 'zustand';
import { apiRequest, getStoredToken, getStoredUser, setStoredAuth, clearStoredAuth } from '../services/apiClient';
import type { User, UserRole } from '../../shared/types';

interface AuthState {
  token: string | null;
  user: User | null;
  isLoading: boolean;
  login: (username: string, pass: string) => Promise<User>;
  logout: () => void;
  updateUserDisplayName: (name: string) => void;
}

export const useAuthStore = create<AuthState>((set) => ({
  token: getStoredToken(),
  user: getStoredUser(),
  isLoading: false,

  login: async (username: string, pass: string) => {
    set({ isLoading: true });
    try {
      const data = await apiRequest('/api/auth/login', {
        method: 'POST',
        body: JSON.stringify({ username, password: pass }),
      });
      setStoredAuth(data.token, data.user);
      set({ token: data.token, user: data.user, isLoading: false });
      return data.user;
    } catch (err) {
      set({ isLoading: false });
      throw err;
    }
  },

  logout: () => {
    clearStoredAuth();
    set({ token: null, user: null });
    window.location.href = '/login';
  },

  updateUserDisplayName: (displayName: string) => {
    set((state) => {
      if (!state.user) return state;
      const updated = { ...state.user, displayName };
      localStorage.setItem('english_word_user', JSON.stringify(updated));
      return { user: updated };
    });
  },
}));
