import { create } from 'zustand';
import { localDb } from '../db';
import { triggerSync, saveLocalProgressUpdate } from '../db/syncEngine';
import { initializeFirstStudyState, getTodayDateStr } from '@shared/ebbinghaus';
import type { TeacherWord, StudentWordProgress, StudyCheckpoint } from '@shared/types';

interface StudyState {
  words: TeacherWord[];
  currentIndex: number;
  currentSubStep: '1' | '2.1' | '2.2' | '2.3' | '2.4';
  checkpoint: StudyCheckpoint | null;
  selectedTeacherId: string | 'all';
  isLoading: boolean;
  isCompletedAll: boolean;

  loadWordsAndProgress: () => Promise<void>;
  setSelectedTeacherId: (tid: string | 'all') => void;
  startStudyWord: (index: number) => void;
  moveToSubStep: (step: '1' | '2.1' | '2.2' | '2.3' | '2.4') => void;
  updateCheckpoint: (partial: Partial<StudyCheckpoint>) => void;
  completeCurrentWord: () => Promise<void>;
}

export const useStudyStore = create<StudyState>((set, get) => ({
  words: [],
  currentIndex: 0,
  currentSubStep: '1',
  checkpoint: null,
  selectedTeacherId: 'all',
  isLoading: true,
  isCompletedAll: false,

  loadWordsAndProgress: async () => {
    set({ isLoading: true });
    // 后台静默触发一次同步
    triggerSync().catch(() => {});

    const allWords = await localDb.words.toArray();
    const allProgress = await localDb.progress.toArray();
    const progressMap = new Map(allProgress.map((p) => [p.normalizedText, p]));

    // 优先筛选未学或正在初学中的词条 (status === 'unlearned' || 'learning')
    const unlearnedWords = allWords.filter((w) => {
      const p = progressMap.get(w.normalizedText);
      return !p || p.status === 'unlearned' || p.status === 'learning';
    });

    if (unlearnedWords.length === 0) {
      set({ words: allWords, isCompletedAll: true, isLoading: false });
      return;
    }

    // 检查第一个词是否有保存的断点
    const firstWord = unlearnedWords[0];
    const p = progressMap.get(firstWord.normalizedText);
    const savedCp = p?.studyCheckpoint || null;

    set({
      words: unlearnedWords,
      currentIndex: 0,
      currentSubStep: savedCp?.step || '1',
      checkpoint: savedCp,
      isCompletedAll: false,
      isLoading: false,
    });
  },

  setSelectedTeacherId: (tid) => {
    set({ selectedTeacherId: tid });
    get().loadWordsAndProgress();
  },

  startStudyWord: (index) => {
    const word = get().words[index];
    if (!word) return;
    set({
      currentIndex: index,
      currentSubStep: '1',
      checkpoint: null,
    });
  },

  moveToSubStep: (step) => {
    const { words, currentIndex, checkpoint } = get();
    const word = words[currentIndex];
    if (!word) return;

    const newCp: StudyCheckpoint = {
      step,
      dictationTargetIndex: checkpoint?.dictationTargetIndex ?? 0,
      dictationRepCount: checkpoint?.dictationRepCount ?? 0,
      pronunciationPassCount: checkpoint?.pronunciationPassCount ?? 0,
      clozeSentenceIndex: checkpoint?.clozeSentenceIndex ?? 0,
      updatedAt: Date.now(),
    };

    set({ currentSubStep: step, checkpoint: newCp });

    // 本地持久化子步骤断点
    saveLocalProgressUpdate(word.normalizedText, {
      status: 'learning',
      studyCheckpoint: newCp,
    });
  },

  updateCheckpoint: (partial) => {
    const { words, currentIndex, checkpoint, currentSubStep } = get();
    const word = words[currentIndex];
    if (!word) return;

    const mergedCp: StudyCheckpoint = {
      step: partial.step ?? checkpoint?.step ?? currentSubStep,
      dictationTargetIndex: partial.dictationTargetIndex ?? checkpoint?.dictationTargetIndex ?? 0,
      dictationRepCount: partial.dictationRepCount ?? checkpoint?.dictationRepCount ?? 0,
      pronunciationPassCount: partial.pronunciationPassCount ?? checkpoint?.pronunciationPassCount ?? 0,
      clozeSentenceIndex: partial.clozeSentenceIndex ?? checkpoint?.clozeSentenceIndex ?? 0,
      updatedAt: Date.now(),
    };

    set({ checkpoint: mergedCp });
    saveLocalProgressUpdate(word.normalizedText, {
      status: 'learning',
      studyCheckpoint: mergedCp,
    });
  },

  completeCurrentWord: async () => {
    const { words, currentIndex } = get();
    const word = words[currentIndex];
    if (!word) return;

    const initState = initializeFirstStudyState(getTodayDateStr());

    // 标记完成初学，推入艾宾浩斯复习池
    await saveLocalProgressUpdate(word.normalizedText, {
      ...initState,
      lastStudiedAt: Date.now(),
    });

    const nextIndex = currentIndex + 1;
    if (nextIndex < words.length) {
      set({
        currentIndex: nextIndex,
        currentSubStep: '1',
        checkpoint: null,
      });
    } else {
      set({ isCompletedAll: true });
    }
  },
}));
