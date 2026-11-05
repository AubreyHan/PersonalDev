# Vibe Coding 实施路线图 (IMPLEMENTATION_PLAN) - english-word

> [!IMPORTANT]
> **执行提示 (Execution Notice)**：
> 本文档是提供给 Antigravity AI 在业务工程仓库 `/Users/aubreyhan/Tools/git_repo/PersonalDev/EnglishWord` 中进行 Vibe Coding 的分阶段原子化实施手册。
> 每个任务均具备明确的输入上下文、产出文件与确定性验证指令。

---

## 阶段一：项目脚手架、D1 数据库与核心共享算法 (Phase 1: Foundation & Shared Core)

### 任务 1.1：初始化 React 19 + Vite PWA + Cloudflare Pages/Workers + D1 脚手架
- **目标**：在 `/Users/aubreyhan/Tools/git_repo/PersonalDev/EnglishWord` 中搭建前后端一体化 TypeScript 工程与单元测试框架。
- **实施要点**：
  - 初始化 `package.json`，安装核心依赖：
    - 前端：`react`, `react-dom`, `react-router-dom`, `zustand`, `dexie`, `dexie-react-hooks`, `lucide-react`, `clsx`, `tailwind-merge`。
    - 后端/边缘：`hono`, `jose`。
    - 构建与测试：`typescript`, `vite`, `@vitejs/plugin-react`, `vite-plugin-pwa`, `tailwindcss`, `wrangler`, `@cloudflare/workers-types`, `vitest`.
  - 配置 `wrangler.toml`，声明 D1 数据库绑定 `DB` 与 Workers AI 绑定 `AI`。
  - 配置 `vite.config.ts` 集成 `VitePWA`（配置 `manifest.webmanifest`、图标、离线缓存策略与 `/api/speech/tts` 音频 CacheFirst 规则）。
- **验证指令**：
  ```bash
  npm run typecheck && npm run build
  ```

### 任务 1.2：编写共享领域模型与三大核心纯函数算法 (`shared/`)
- **目标**：实现 100% 单元测试覆盖的艾宾浩斯调度器、词形变化合并器与音素/同音词比对器。
- **实施要点**：
  - 创建 `shared/types.ts`：定义完整接口（参考 `SPEC.md` 第 2 节）。
  - 创建 `shared/inflections.ts`：
    - 实现 `mergeWordInflections(baseText, inflections)`：将拼写相同的规则变化（如过去式与过去分词同为 `played`）自动合并为单个 `MergedInflectionItem`，过滤掉与原词完全一致的无意义重复项。
    - 实现 `tokenizeIntoSlots(targetText)`：将单词或带空格/连字符的词组拆分为字母输入格与静态分隔符。
  - 创建 `shared/ebbinghaus.ts`：
    - 实现 `EBBINGHAUS_INTERVALS = [1, 2, 4, 7, 15, 30]`。
    - 实现 `isWordDueForReview(progress, todayDateStr)`（支持跨天未复习自动顺延：`nextReviewDate <= todayDateStr`）。
    - 实现 `calculateNextReviewState(currentProgress, passedWithoutError, todayDateStr)`：一次全对则 `stage + 1`（满 6 阶段升为 `mastered`），出错或看提示则重置为 `stage = 1, nextReviewDate = today + 1d`，并累加 `errorCount` 与 `stageResetCount`。
  - 创建 `shared/phonetics.ts`：
    - 实现 `evaluatePronunciationSimilarity(targetText, recognizedText)`：基于音素归一化与 Levenshtein 距离计算 `0~100` 得分（$\ge 80$ 判为准确）。
    - 实现 `isHomophoneMatch(inputWord, targetWord, homophones)`：检测学生是否误输入了合法的同音异义词。
- **验证指令**：
  ```bash
  npx vitest run shared/__tests__/
  ```

### 任务 1.3：建立 Cloudflare D1 数据库 Schema 与初始管理员种子数据
- **目标**：创建 SQLite 数据表并初始化默认管理员 `admin`（密码 `P@ss=w0rd`）。
- **实施要点**：
  - 创建 `migrations/0001_initial_schema.sql`（包含 `users`, `teacher_students`, `teacher_words`, `student_word_progress` 四张表及索引）。
  - 创建 `functions/api/services/crypto.ts`：使用 Web Crypto `PBKDF2` 实现密码哈希生成与校验，以及基于 `jose` 的 JWT 签发与验证。
  - 创建 `migrations/0002_seed_admin.sql`（或在 Worker 启动中间件中幂等检测创建 `username = 'admin'`，初始密码 `P@ss=w0rd`）。
- **验证指令**：
  ```bash
  npx wrangler d1 migrations apply english-word-db --local
  ```

---

## 阶段二：Cloudflare Workers 后端接口与 AI/语音服务 (Phase 2: Backend API & AI Pipelines)

### 任务 2.1：实现认证接口与 Admin 师生层级管理接口
- **目标**：完成登录、自愿改密、以及 Admin“先建教师、再在教师下建/绑学生、解绑与删除保护”全套接口。
- **实施要点**：
  - 创建 `functions/api/middleware/auth.ts`：解析 Bearer Token，提供 `requireRole('admin' | 'teacher' | 'student')`。
  - 创建 `functions/api/routes/auth.ts`：`POST /api/auth/login`、`POST /api/auth/change-password`、`GET /api/auth/me`。
  - 创建 `functions/api/routes/admin.ts`：
    - 教师 CRUD（新建教师默认密码 `passw0rd`）；
    - 在指定教师名下新建学生（默认密码 `passw0rd` 并写入 `teacher_students`）或批量绑定已有学生；
    - 学生解绑 (`action=unbind`，检测是否为唯一归属教师) 与彻底删除 (`action=delete_account`)；
    - 教师/学生密码一键重置为 `passw0rd`。
- **验证指令**：
  ```bash
  npx vitest run functions/__tests__/admin.test.ts
  ```

### 任务 2.2：实现教师端「词典 + Workers AI」智能补全、词库管理与学情接口
- **目标**：实现输入单词/词组一键补全音标、音节切分、释义、变形、同音词及带挖空的双语例句，并提供学生进度与高频错词榜查询。
- **实施要点**：
  - 创建 `functions/api/services/enricher.ts`：
    - 结合 Free Dictionary API / 有道词典与 Cloudflare Workers AI (`@cf/meta/llama-3.1-8b-instruct`)，兼顾本地确定性规则兜底（当本地开发环境未连接云端 AI 时提供内置高精词库智能推导），输出严格符合 `TeacherWord` 结构的 JSON（含 `in·for·ma·tion` 音节切分、`inflections`、`homophones` 及带 `targetInSentence` 的双语例句）。
  - 创建 `functions/api/routes/teacher.ts`：
    - `POST /api/teacher/words/enrich`（智能补全预览）；
    - `GET / POST / PUT / DELETE /api/teacher/words`（含同教师 `normalized_text` 唯一性去重拦截 `409 DUPLICATE_WORD`）；
    - `GET /api/teacher/analytics/students` 与 `GET /api/teacher/analytics/students/:studentId/weak-words`（按 `error_count DESC, stage_reset_count DESC` 输出薄弱错词榜）。
- **验证指令**：
  ```bash
  npx vitest run functions/__tests__/teacher.test.ts
  ```

### 任务 2.3：实现真人发音 TTS 同域代理与 Whisper 语音评测接口 + 学生增量同步接口
- **目标**：打通单词/词组真人发音音频流代理与云端跟读评测，以及学生端离线双向同步 API。
- **实施要点**：
  - 创建 `functions/api/routes/speech.ts`：
    - `GET /api/speech/tts`：代理词典真人 MP3 或调用高自然度 TTS 合成词组音频流，附加长效缓存响应头供前端 Cache Storage 离线持久化。
    - `POST /api/speech/evaluate`：调用 `@cf/openai/whisper` 识别上传音频，结合 `evaluatePronunciationSimilarity` 返回得分与准确度判定。
  - 创建 `functions/api/routes/student.ts`：
    - `GET /api/student/sync/pull`：聚合该学生绑定的所有教师词库（按 `normalized_text` 合并多教师例句）及该学生的 `student_word_progress`。
    - `POST /api/student/sync/push`：幂等合并客户端上报的学习完成、子步骤断点与艾宾浩斯复习结果。
- **验证指令**：
  ```bash
  npx vitest run functions/__tests__/student_sync.test.ts
  ```

---

## 阶段三：客户端离线存储、音频缓存与 Admin / 教师端 UI (Phase 3: Local-First DB & Admin/Teacher UI)

### 任务 3.1：构建 Dexie.js 离线数据库、音频预缓存器与自动同步引擎
- **目标**：实现断网可读写、联网自动同步的本地优先数据底座。
- **实施要点**：
  - 创建 `src/db/index.ts`：定义 `words`, `progress`, `syncOutbox` 本地 IndexedDB 表。
  - 创建 `src/db/audioCache.ts`：在后台静默遍历待学/待复习单词，将其 `/api/speech/tts` 音频预拉取存入浏览器 `caches.open('english-word-audio-v1')`；播放时优先从本地 Cache 读取，完全离线且未命中缓存时平滑降级为 `window.speechSynthesis`。
  - 创建 `src/db/syncEngine.ts`：监听 `online` 事件与用户操作，自动执行 `pull` 与 `push`。
- **验证指令**：
  ```bash
  npm run typecheck
  ```

### 任务 3.2：开发登录页、个人改密弹窗与 Admin 多租户管理控制台
- **目标**：完成角色路由守卫与 Admin 先建教师、再在教师下管理学生的完整交互界面。
- **实施要点**：
  - 创建 `src/pages/LoginPage.tsx` 与 `src/components/common/AppLayout.tsx`（含在线/离线状态徽标、角色标识、修改密码弹窗）。
  - 创建 `src/pages/admin/AdminDashboard.tsx` 与 `src/pages/admin/TeacherStudentsDrawer.tsx`：
    - 教师卡片列表（新建教师、重置密码、删除教师保护弹窗）；
    - 点击教师打开「名下学生管理抽屉」：支持 Tab 1「直接新建学生」与 Tab 2「从已有学生中选择绑定」；
    - 学生列表支持「重置密码」、「移出当前教师（解绑）」与「彻底删除账号」。
- **验证指令**：
  ```bash
  npm run build
  ```

### 任务 3.3：开发教师端智能生词管理工作台与学生学情/薄弱错词看板
- **目标**：实现教师单条智能录入（含自动补全预览与微调）、词库去重检索及学生学情追踪。
- **实施要点**：
  - 创建 `src/pages/teacher/TeacherWordList.tsx`：
    - 「添加生词/词组」弹窗：输入英文后点击「智能补全」，自动回填音节切分 (`in·for·ma·tion`)、音标、词性释义、词形变化、同音词表、带挖空标记的双语例句，支持教师增删改任意字段及试听发音；若重复录入则高亮提示已有词条。
  - 创建 `src/pages/teacher/TeacherAnalytics.tsx`：
    - 展示名下每位学生的学习总览与今日复习完成进度条；
    - 点击学生展开「高频错词 / 多次降级顽固词榜单」详情面板。
- **验证指令**：
  ```bash
  npm run build
  ```

---

## 阶段四：学生端「学习」四步闯关与「复习」艾宾浩斯引擎 (Phase 4: Student Study & Review Engines)

### 任务 4.1：开发核心交互组件（下划线字母格、听写、四选一、跟读评测、例句填空）
- **目标**：打造手感丝滑、跨平台兼容（支持物理键盘与手机触屏）的四大闯关组件。
- **实施要点**：
  - 创建 `src/components/study/CognitiveCard.tsx`：步骤 1 认知展示页（分音节高亮展示、音标、词形变化标签、双语例句、进入自动朗读与反复听音按钮）。
  - 创建 `src/components/study/LetterSlotInput.tsx`：下划线字母格输入组件，支持词组空格/连字符自动跳过、Web Audio 合成清脆按键音效、正确变绿与错误泛红抖动动效。
  - 创建 `src/components/study/StepDictation.tsx`（步骤 2.1）：
    - 学习模式：原词盲打 3 遍 -> 合并后的词形变化依次渐进弹出各盲打 3 遍；支持“看一眼提示 / 退回重看”（触发后当前项已打遍数重置为 `0/3`）。
    - 复习模式：展示词性提示、支持同音异义词识别保护（误输同音词仅友好提示不罚写）、默认原词与变形各盲打 1 遍（出错后自动升级为 3 遍强化）。
  - 创建 `src/components/study/StepMeaningQuiz.tsx`（步骤 2.2）：四选一中文释义，选错抖动并重新洗牌选项。
  - 创建 `src/components/study/StepPronunciation.tsx`（步骤 2.3）：
    - 支持录音跟读评测（累计 3 次达标点亮 3 星，复习模式默认 1 次达标）；
    - 提供「回放我的录音 vs 标准音对比」按钮；
    - 提供「当前不便开口（静音模式）」兜底开关。
  - 创建 `src/components/study/StepClozeSentence.tsx`（步骤 2.4）：展示带中文翻译的挖空英文例句，使用 `LetterSlotInput` 拼写填入正确的单词或其词形变化形态。
- **验证指令**：
  ```bash
  npm run typecheck && npm run build
  ```

### 任务 4.2：开发学生端「学习」模块主流程与子步骤级断点续学
- **目标**：串联步骤 1 与步骤 2 (`2.1 -> 2.2 -> 2.3 -> 2.4`)，实现按关联教师筛选与 IndexedDB 子步骤断点实时持久化。
- **实施要点**：
  - 创建 `src/stores/useStudyStore.ts` 与 `src/pages/student/StudyPage.tsx`：
    - 支持顶部切换关联教师、学习状态筛选及列表查阅；
    - 每完成一个小动作（如 2.1 打完第 1 遍、2.3 读准第 2 次）实时写入本地 IndexedDB `study_checkpoint_json`，页面刷新或重开时直接恢复到当前子步骤；
    - 完成 2.4 后将单词推入艾宾浩斯 `Stage 1`（`nextReviewDate = tomorrow`）。
- **验证指令**：
  ```bash
  npm run build
  ```

### 任务 4.3：开发学生端「复习」艾宾浩斯盲测模块与错词回炉/抽查复活机制
- **目标**：实现不提前展示列表的随机盲测复习、未复习自动顺延、错词三重惩罚（当场 3 遍 + 重置 Stage 1 + 当日队尾回炉）以及已掌握词库随机抽查。
- **实施要点**：
  - 创建 `src/stores/useReviewStore.ts` 与 `src/pages/student/ReviewPage.tsx`：
    - 复习首页仅展示今日待复习总数、今日已完成数、艾宾浩斯各阶段词数分布图及「开始今日复习」按钮，**严禁提前列出今日待复习单词清单**；
    - 开始复习后按 Fisher-Yates 洗牌随机抽取单词，跳过步骤 1 直接考核 `2.1 -> 2.2 -> 2.3 -> 2.4`；
    - 若中途出错或点击提示：立即弹出单词认知卡片供重温，将当前词切换为 3 遍强化模式，将该词艾宾浩斯阶段重置为 `Stage 1`，并将其追加至 `intraDayRequeuePool`（今日结束前必须再无提示一遍通关）；
    - 实现「已彻底掌握词库随机抽查」模式，抽查答错自动复活重置回 `Stage 1`；
    - 在 `StudentHome.tsx` 实现「今日有待复习词时优先引导先复习再学新词」的防积压提醒。
- **验证指令**：
  ```bash
  npx vitest run && npm run build
  ```
