# 详细设计与规范 (SPEC) - english-word

## 1. 数据库模型设计 (Cloudflare D1 SQLite DDL)

系统在 Cloudflare D1 中维护 3 张核心表，实现个人私有词库与独立进度的多租户存储：

```sql
-- 1. 用户表 (包含 admin 与 user 两种角色)
CREATE TABLE IF NOT EXISTS users (
    id TEXT PRIMARY KEY,                          -- UUID
    username TEXT NOT NULL UNIQUE,                -- 登录账号 (唯一，如 "admin", "aubrey")
    display_name TEXT NOT NULL,                   -- 显示姓名/昵称
    password_hash TEXT NOT NULL,                  -- PBKDF2-SHA256 哈希值
    role TEXT NOT NULL CHECK (role IN ('admin', 'user')),
    created_at INTEGER NOT NULL,                  -- Unix 毫秒时间戳
    updated_at INTEGER NOT NULL
);

-- 2. 用户个人词库表 (每位用户维护自己的生词/词组列表，完全独立私有)
CREATE TABLE IF NOT EXISTS user_words (
    id TEXT PRIMARY KEY,                          -- UUID
    user_id TEXT NOT NULL,                        -- 所属用户 ID
    text TEXT NOT NULL,                           -- 原始单词或词组 (如 "information", "be related to")
    normalized_text TEXT NOT NULL,                -- 标准化小写去首尾空格标识
    syllables TEXT NOT NULL,                      -- 分音节展示字符串 (如 "in·for·ma·tion"，单音节或词组则同 text)
    phonetic_us TEXT DEFAULT '',                  -- 美式音标
    phonetic_uk TEXT DEFAULT '',                  -- 英式音标
    audio_us_url TEXT DEFAULT '',                 -- 美音发音 URL
    audio_uk_url TEXT DEFAULT '',                 -- 英音发音 URL
    meanings_json TEXT NOT NULL,                  -- JSON: Array<{ pos: string, zh: string }>
    inflections_json TEXT NOT NULL,               -- JSON: WordInflections (复数/三单/过去式/过去分词/现在分词/比较级/最高级)
    homophones_json TEXT NOT NULL DEFAULT '[]',   -- JSON: string[] 同音异义词列表 (如 ["right", "rite"])
    examples_json TEXT NOT NULL,                  -- JSON: Array<{ en: string, zh: string, targetInSentence: string }>
    tags_json TEXT NOT NULL DEFAULT '[]',         -- JSON: string[] 标签数组
    created_at INTEGER NOT NULL,
    updated_at INTEGER NOT NULL,
    UNIQUE(user_id, normalized_text),             -- 同一用户禁止重复录入同一单词/词组
    FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE
);
CREATE INDEX IF NOT EXISTS idx_uw_user ON user_words(user_id);
CREATE INDEX IF NOT EXISTS idx_uw_norm ON user_words(normalized_text);

-- 3. 用户单词学习与艾宾浩斯复习进度表
CREATE TABLE IF NOT EXISTS user_word_progress (
    user_id TEXT NOT NULL,
    normalized_text TEXT NOT NULL,                -- 对应单词标准化文本
    status TEXT NOT NULL DEFAULT 'unlearned' CHECK (status IN ('unlearned', 'learning', 'reviewing', 'mastered')),
    ebbinghaus_stage INTEGER NOT NULL DEFAULT 0,  -- 0:未入池, 1:+1d, 2:+2d, 3:+4d, 4:+7d, 5:+15d, 6:+30d
    next_review_date TEXT DEFAULT NULL,           -- 下次到期复习本地日期 "YYYY-MM-DD"
    last_studied_at INTEGER DEFAULT NULL,         -- 最近一次学习/复习毫秒时间戳
    last_reviewed_date TEXT DEFAULT NULL,         -- 最近一次完成复习的本地日期 "YYYY-MM-DD"
    study_checkpoint_json TEXT DEFAULT NULL,      -- JSON: 学习模块子步骤断点 (如 {"step":"2.3","readPassCount":2})
    total_review_count INTEGER NOT NULL DEFAULT 0,-- 累计完成复习次数
    error_count INTEGER NOT NULL DEFAULT 0,       -- 累计出错/看提示次数
    stage_reset_count INTEGER NOT NULL DEFAULT 0, -- 累计被重置回 Stage 1 的次数
    last_error_step TEXT DEFAULT NULL,            -- 最近一次卡壳步骤 ('2.1' | '2.2' | '2.3' | '2.4')
    updated_at INTEGER NOT NULL,
    PRIMARY KEY (user_id, normalized_text),
    FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE
);
CREATE INDEX IF NOT EXISTS idx_uwp_review ON user_word_progress(user_id, status, next_review_date);
```

---

## 2. 核心 TypeScript 数据结构定义 (`shared/types.ts`)

```typescript
export type UserRole = 'admin' | 'user';

export interface User {
  id: string;
  username: string;
  displayName: string;
  role: UserRole;
  createdAt: number;
  updatedAt: number;
}

export interface WordMeaning {
  pos: string; // 词性，如 "n.", "v.", "adj.", "phr."
  zh: string;  // 地道中文释义
}

export interface WordInflections {
  plural?: string;
  thirdPerson?: string;
  presentParticiple?: string;
  pastTense?: string;
  pastParticiple?: string;
  comparative?: string;
  superlative?: string;
}

export interface MergedInflectionItem {
  labels: string[];            // 如 ["过去式", "过去分词"]
  form: string;                // 如 "played"
}

export interface WordExample {
  en: string;                  // 英文原句
  zh: string;                  // 中文翻译
  targetInSentence: string;    // 句中实际出现的目标词或变形 (挖空匹配)
}

export interface WordItem {
  id: string;
  userId: string;
  text: string;
  normalizedText: string;
  syllables: string;           // 如 "in·for·ma·tion"
  phoneticUs: string;
  phoneticUk: string;
  audioUsUrl: string;
  audioUkUrl: string;
  meanings: WordMeaning[];
  inflections: WordInflections;
  homophones: string[];
  examples: WordExample[];
  tags: string[];
  createdAt: number;
  updatedAt: number;
}

export type WordStudyStatus = 'unlearned' | 'learning' | 'reviewing' | 'mastered';

export interface UserWordProgress {
  userId: string;
  normalizedText: string;
  status: WordStudyStatus;
  ebbinghausStage: number;     // 0 ~ 6
  nextReviewDate: string | null; // "YYYY-MM-DD"
  lastStudiedAt: number | null;
  lastReviewedDate: string | null;
  studyCheckpoint?: {
    step: '2.1' | '2.2' | '2.3' | '2.4';
    dictationRepsCompleted?: number;
    currentInflectionIndex?: number;
    inflectionRepsCompleted?: number;
    readPassCount?: number;
  } | null;
  totalReviewCount: number;
  errorCount: number;
  stageResetCount: number;
  lastErrorStep?: string | null;
  updatedAt: number;
}
```

---

## 3. 本地 IndexedDB 规范 (`Dexie.js`)

前端通过 Dexie 建立本地专属离线数据库：

```typescript
// 客户端本地 Dexie 数据库定义
class EnglishWordDatabase extends Dexie {
  words!: Table<WordItem, string>;
  progress!: Table<UserWordProgress, string>;
  settings!: Table<{ key: string; value: any }, string>;

  constructor() {
    super('EnglishWordDB');
    this.version(2).stores({
      words: 'id, normalizedText, createdAt, updatedAt',
      progress: 'normalizedText, status, ebbinghausStage, nextReviewDate, updatedAt',
      settings: 'key',
    });
  }
}
```

---

## 4. RESTful API 端点规范

### 4.1 认证与鉴权接口 (`/api/auth`)
- `POST /api/auth/login`
  - Body: `{ "username": "aubrey", "password": "passw0rd" }`
  - Response `200`: `{ "token": "jwt_token...", "user": { "id": "...", "username": "aubrey", "role": "user" } }`
- `GET /api/auth/me`
- `POST /api/auth/change-password`

### 4.2 管理员接口 (`/api/admin`, 仅 `role === 'admin'`)
- `GET /api/admin/users`
  - 返回全站用户列表及其词条总数、掌握进度和最近活跃时间。
- `POST /api/admin/users`
  - Body: `{ "username": "new_user", "displayName": "小李" }` (密码默认 `passw0rd`)。
- `DELETE /api/admin/users/:userId`
  - 级联删除该用户账号、词库与进度。
- `POST /api/admin/users/:userId/reset-password`
  - 将用户密码重置为 `passw0rd`。

### 4.3 词库管理与智能补全接口 (`/api/words`, 登录用户可用)
- `POST /api/words/enrich`
  - Body: `{ "text": "be related to" }`
  - 自动调用权威词典 API + Workers AI，返回结构化中文释义、音标、词形变化及真实双语例句。
- `GET /api/words`
  - 获取当前用户的词库列表（支持 `q` 关键词搜索、`tag` 过滤）。
- `POST /api/words`
  - 保存用户新单词/词组。若 `(user_id, normalized_text)` 已存在，返回 `409 DUPLICATE_WORD`。
- `PUT /api/words/:id` & `DELETE /api/words/:id`
  - 修改或删除当前用户的词条。

### 4.4 个人数据增量同步接口 (`/api/sync`)
- `GET /api/sync/pull`
  - 拉取当前用户的最新词库与云端学习/复习进度。
- `POST /api/sync/push`
  - 批量推送本地产生的复习打卡、学习完成与子步骤断点变更。

### 4.5 语音与跟读接口 (`/api/speech`)
- `GET /api/speech/tts?text=be+related+to&accent=us`
  - 真人发音/Neural TTS 音频代理（带持久化 Cache-Control）。
- `POST /api/speech/evaluate`
  - Whisper 跟读评测与音素相似度打分。
