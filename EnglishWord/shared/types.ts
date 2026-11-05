export type UserRole = 'admin' | 'teacher' | 'student';

export interface User {
  id: string;
  username: string;
  displayName: string;
  role: UserRole;
  createdAt: number;
  updatedAt: number;
}

export interface TeacherStudentRelation {
  teacherId: string;
  studentId: string;
  boundAt: number;
}

export interface WordMeaning {
  pos: string; // 词性，如 "n.", "v.", "adj.", "phr."
  zh: string;  // 中文释义，如 "信息；资料"
}

export interface WordInflections {
  plural?: string;             // 名词复数 (如 "books", "children")
  thirdPerson?: string;        // 动词第三人称单数 (如 "writes", "looks forward to")
  presentParticiple?: string;  // 现在分词 (如 "writing", "looking forward to")
  pastTense?: string;          // 过去式 (如 "wrote", "looked forward to")
  pastParticiple?: string;     // 过去分词 (如 "written", "looked forward to")
  comparative?: string;        // 形容词/副词比较级 (如 "better")
  superlative?: string;        // 形容词/副词最高级 (如 "best")
}

/**
 * 经过「相同拼写自动合并」处理后的待拼写变形项
 * 例如 worked 同时是过去式和过去分词，合并为 { labels: ["过去式", "过去分词"], form: "worked" }
 */
export interface MergedInflectionItem {
  labels: string[];            // 如 ["过去式", "过去分词"]
  form: string;                // 如 "played"
}

export interface WordExample {
  en: string;                  // 英文原句，如 "She wrote three letters yesterday."
  zh: string;                  // 中文翻译，如 "她昨天写了三封信。"
  targetInSentence: string;    // 句中实际出现的目标词或变形，如 "wrote" (忽略大小写精准挖空)
}

export interface TeacherWord {
  id: string;
  teacherId: string;
  teacherName?: string;
  text: string;
  normalizedText: string;
  syllables: string;           // 如 "in·for·ma·tion"
  phoneticUs: string;
  phoneticUk: string;
  audioUsUrl: string;
  audioUkUrl: string;
  meanings: WordMeaning[];
  inflections: WordInflections;
  homophones: string[];        // 同音异义词数组 (小写)，如 ["right", "rite"]
  examples: WordExample[];
  tags: string[];
  createdAt: number;
  updatedAt: number;
}

export type StudyStatus = 'unlearned' | 'learning' | 'reviewing' | 'mastered';

/**
 * 学生端「学习」模块子步骤断点持久化结构 (保存在 IndexedDB 与 D1)
 */
export interface StudyCheckpoint {
  step: '1' | '2.1' | '2.2' | '2.3' | '2.4';
  dictationTargetIndex: number;   // 0 表示原词，1..N 表示合并后的第几个词形变化
  dictationRepCount: number;      // 当前项已盲打正确遍数 (0..3)
  pronunciationPassCount: number; // 2.3 已累计达标的准确读音次数 (0..3)
  clozeSentenceIndex: number;     // 2.4 当前正在填空的例句索引
  updatedAt: number;
}

export interface StudentWordProgress {
  studentId: string;
  normalizedText: string;
  status: StudyStatus;
  ebbinghausStage: number;        // 0:未入池, 1:+1d, 2:+2d, 3:+4d, 4:+7d, 5:+15d, 6:+30d
  nextReviewDate: string | null;  // "YYYY-MM-DD"
  lastStudiedAt: number | null;
  lastReviewedDate: string | null;// "YYYY-MM-DD"
  studyCheckpoint: StudyCheckpoint | null;
  totalReviewCount: number;
  errorCount: number;
  stageResetCount: number;
  lastErrorStep: '2.1' | '2.2' | '2.3' | '2.4' | null;
  updatedAt: number;
}

export interface StudentAnalyticsSummary {
  studentId: string;
  username: string;
  displayName: string;
  totalWords: number;
  unlearnedCount: number;
  reviewingCount: number;
  masteredCount: number;
  todayDueReviewCount: number;
  todayCompletedReviewCount: number;
  lastActiveAt: number | null;
}

export interface StudentWeakWord {
  normalizedText: string;
  text: string;
  meanings: WordMeaning[];
  errorCount: number;
  stageResetCount: number;
  lastErrorStep: string | null;
  currentStage: number;
  nextReviewDate: string | null;
}
