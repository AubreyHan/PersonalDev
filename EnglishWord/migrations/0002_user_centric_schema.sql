-- 清理旧的师生体系表
DROP TABLE IF EXISTS teacher_students;
DROP TABLE IF EXISTS teacher_words;
DROP TABLE IF EXISTS student_word_progress;

-- 1. 创建用户私有词库表 (每位用户维护自己的生词/词组列表，完全独立私有)
CREATE TABLE IF NOT EXISTS user_words (
    id TEXT PRIMARY KEY,
    user_id TEXT NOT NULL,
    text TEXT NOT NULL,
    normalized_text TEXT NOT NULL,
    syllables TEXT NOT NULL,
    phonetic_us TEXT DEFAULT '',
    phonetic_uk TEXT DEFAULT '',
    audio_us_url TEXT DEFAULT '',
    audio_uk_url TEXT DEFAULT '',
    meanings_json TEXT NOT NULL,
    inflections_json TEXT NOT NULL,
    homophones_json TEXT NOT NULL DEFAULT '[]',
    examples_json TEXT NOT NULL,
    tags_json TEXT NOT NULL DEFAULT '[]',
    created_at INTEGER NOT NULL,
    updated_at INTEGER NOT NULL,
    UNIQUE(user_id, normalized_text),
    FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE
);
CREATE INDEX IF NOT EXISTS idx_uw_user ON user_words(user_id);
CREATE INDEX IF NOT EXISTS idx_uw_norm ON user_words(normalized_text);

-- 2. 创建用户单词学习与艾宾浩斯复习进度表
CREATE TABLE IF NOT EXISTS user_word_progress (
    user_id TEXT NOT NULL,
    normalized_text TEXT NOT NULL,
    status TEXT NOT NULL DEFAULT 'unlearned' CHECK (status IN ('unlearned', 'learning', 'reviewing', 'mastered')),
    ebbinghaus_stage INTEGER NOT NULL DEFAULT 0,
    next_review_date TEXT DEFAULT NULL,
    last_studied_at INTEGER DEFAULT NULL,
    last_reviewed_date TEXT DEFAULT NULL,
    study_checkpoint_json TEXT DEFAULT NULL,
    total_review_count INTEGER NOT NULL DEFAULT 0,
    error_count INTEGER NOT NULL DEFAULT 0,
    stage_reset_count INTEGER NOT NULL DEFAULT 0,
    last_error_step TEXT DEFAULT NULL,
    updated_at INTEGER NOT NULL,
    PRIMARY KEY (user_id, normalized_text),
    FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE
);
CREATE INDEX IF NOT EXISTS idx_uwp_review ON user_word_progress(user_id, status, next_review_date);

-- 3. 将现有的非 admin 用户统一转换为 'user' 角色
UPDATE users SET role = 'user' WHERE role != 'admin';
