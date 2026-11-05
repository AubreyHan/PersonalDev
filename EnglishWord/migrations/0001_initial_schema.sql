-- 1. 用户表 (admin / teacher / student)
CREATE TABLE IF NOT EXISTS users (
    id TEXT PRIMARY KEY,
    username TEXT NOT NULL UNIQUE,
    display_name TEXT NOT NULL,
    password_hash TEXT NOT NULL,
    role TEXT NOT NULL CHECK (role IN ('admin', 'teacher', 'student')),
    created_at INTEGER NOT NULL,
    updated_at INTEGER NOT NULL
);

-- 2. 师生多对多绑定表
CREATE TABLE IF NOT EXISTS teacher_students (
    teacher_id TEXT NOT NULL,
    student_id TEXT NOT NULL,
    bound_at INTEGER NOT NULL,
    PRIMARY KEY (teacher_id, student_id),
    FOREIGN KEY (teacher_id) REFERENCES users(id) ON DELETE CASCADE,
    FOREIGN KEY (student_id) REFERENCES users(id) ON DELETE CASCADE
);
CREATE INDEX IF NOT EXISTS idx_ts_student ON teacher_students(student_id);

-- 3. 教师词库表
CREATE TABLE IF NOT EXISTS teacher_words (
    id TEXT PRIMARY KEY,
    teacher_id TEXT NOT NULL,
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
    UNIQUE(teacher_id, normalized_text),
    FOREIGN KEY (teacher_id) REFERENCES users(id) ON DELETE CASCADE
);
CREATE INDEX IF NOT EXISTS idx_tw_teacher ON teacher_words(teacher_id);
CREATE INDEX IF NOT EXISTS idx_tw_norm ON teacher_words(normalized_text);

-- 4. 学生学习与艾宾浩斯复习进度表 (跨教师同词全局互通)
CREATE TABLE IF NOT EXISTS student_word_progress (
    student_id TEXT NOT NULL,
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
    PRIMARY KEY (student_id, normalized_text),
    FOREIGN KEY (student_id) REFERENCES users(id) ON DELETE CASCADE
);
CREATE INDEX IF NOT EXISTS idx_swp_review ON student_word_progress(student_id, status, next_review_date);
