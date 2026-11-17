# 技术架构设计 (ARCHITECTURE) - english-word

## 1. 整体系统架构 (System Architecture)

`english-word` 采用 **Local-First PWA (React 19 + TypeScript + Dexie.js IndexedDB)** 结合 **Cloudflare 全家桶 Serverless (Pages + Workers + Hono + D1 SQLite + Workers AI)** 的现代边缘全栈架构：
- **前端与离线引擎层 (PWA Client)**：负责跨平台响应式 UI、四步递进式学习/复习状态机、下划线字母格盲打交互、本地 IndexedDB 数据读写、真人发音 MP3 离线预缓存，以及断网环境下的本地 TTS / 语音识别降级兜底。
- **边缘计算与 AI 服务层 (Cloudflare Workers)**：基于轻量级路由框架 `Hono` 提供 REST API、JWT 角色鉴权（`admin` / `user`）、单词增删改查、增量同步端点，并调用 **Workers AI** 实现「单词/词组结构化智能补全」、「词组高拟真语音合成」与「Whisper 真人跟读语音评测」。
- **云端持久化层 (Cloudflare D1)**：基于边缘分布式 SQLite (`D1`) 存储用户账号（`users`）、个人专属词库（`user_words`）以及用户的学习与艾宾浩斯复习记录（`user_word_progress`）。

### 架构全景图

```mermaid
flowchart TB
    subgraph Client ["跨平台自适应 PWA 客户端 (React 19 + TypeScript + Vite PWA)"]
        direction TB
        subgraph Views ["双角色视图层 (Role-Based Views)"]
            AdminView["Admin 管理控制台<br/>- 用户账号增删与密码重置<br/>- 系统用户总览与词数统计"]
            UserView["User 学习者工作台<br/>- 个人专属词库管理与智能录词<br/>- 今日学习：认知页 + 4步闯关状态机<br/>- 今日复习：艾宾浩斯盲测 + 错词回炉<br/>- 个人学情与薄弱错词榜"]
        end

        subgraph Engines ["客户端核心引擎层"]
            StudyMachine["四步闯关状态机 (Zustand)<br/>- 2.1 原词/合并变形盲打<br/>- 2.2 四选一干扰项生成<br/>- 2.3 跟读评测与录音对比<br/>- 2.4 例句变形挖空校验"]
            EbbinghausScheduler["艾宾浩斯调度器<br/>- 6阶段周期 (+1/2/4/7/15/30d)<br/>- 未复习自动顺延算法<br/>- 错词重置与队尾回炉队列"]
            AudioEngine["双模音频与评测引擎<br/>- 真人MP3离线预缓存播放器<br/>- MediaRecorder 录音对比<br/>- 离线 Web Speech 降级兜底"]
            SyncManager["双向增量同步管理器<br/>- Outbox 变更队列<br/>- 网络恢复自动 Push/Pull"]
        end

        subgraph LocalStorage ["浏览器离线存储层"]
            IDB[("IndexedDB (Dexie.js)<br/>- words / progress / review_logs<br/>- study_checkpoints / sync_outbox")]
            CacheAPI[("Cache Storage<br/>- 离线静态资源 (SW)<br/>- 预缓存真人发音 MP3")]
        end

        Views --> Engines
        Engines <--> LocalStorage
    end

    subgraph Edge ["Cloudflare Serverless 边缘后端 (Workers + Hono)"]
        AuthMiddleware["JWT 鉴权与 RBAC 中间件<br/>(admin / user)"]
        AdminAPI["Admin 用户账号管理服务"]
        WordAPI["User 个人词库管理服务"]
        WordAIEnricher["智能查词与结构化补全管线<br/>- 音节切分 / 词形变化 / 同音词 / 挖空例句"]
        SpeechService["语音合成与跟读评测服务<br/>- 词典原声代理 & Neural TTS<br/>- Whisper 语音识别与音素打分"]
        SyncAPI["个人数据增量同步服务"]
    end

    subgraph CloudInfra ["Cloudflare 数据与 AI 基础设施"]
        D1[("Cloudflare D1 (SQLite)<br/>- users<br/>- user_words<br/>- user_word_progress")]
        WorkersAI["Cloudflare Workers AI<br/>- @cf/meta/llama-3.1-8b-instruct (查词补全)<br/>- @cf/openai/whisper (跟读评测)"]
        DictUpstream["权威词典与原声源<br/>(Youdao JSON / Suggest API / Audio)"]
    end

    SyncManager <-->|"HTTPS JSON / 离线重连"| AuthMiddleware
    AdminView <--> AuthMiddleware
    UserView <--> AuthMiddleware
    AudioEngine <-->|"音频流缓存 / 录音评测"| SpeechService

    AuthMiddleware --> AdminAPI
    AuthMiddleware --> WordAPI
    AuthMiddleware --> WordAIEnricher
    AuthMiddleware --> SpeechService
    AuthMiddleware --> SyncAPI

    AdminAPI <--> D1
    WordAPI <--> D1
    SyncAPI <--> D1
    WordAIEnricher <--> DictUpstream
    WordAIEnricher <--> WorkersAI
    SpeechService <--> WorkersAI
    SpeechService <--> DictUpstream
```

---

## 2. 技术选型与决策依据 (Tech Stack)

| 层次 / 领域 | 选用技术 | 版本/库 | 选型理由 |
| :--- | :--- | :--- | :--- |
| **前端框架** | **React + TypeScript** | `React 19, TS 5.x` | 组件化生态成熟，严格类型检查保障复杂四步状态机与艾宾浩斯算法零运行时类型错误。 |
| **构建与 PWA 引擎** | **Vite + vite-plugin-pwa** | `Vite 6.x, Workbox` | 开箱即用生成 Service Worker 与 `manifest.webmanifest`，支持静态资源离线缓存与桌面/移动端安装。 |
| **UI 与响应式样式** | **Tailwind CSS + Lucide Icons** | `v3.4+` | 轻松实现 PC 端宽屏、iPad 平板与手机端自适应布局，定制下划线字母格、抖动动画与星级进度条。 |
| **客户端状态管理** | **Zustand** | `v5.x` | 极简响应式状态管理，驱动 `2.1 ~ 2.4` 闯关步骤切换与实时按键反馈。 |
| **客户端离线数据库** | **Dexie.js (IndexedDB)** | `v4.x` | 工业级 IndexedDB 封装，支持复合索引、响应式查询 (`useLiveQuery`)，完美支撑离线单词学习、子步骤断点持久化与增量同步队列。 |
| **边缘后端运行时** | **Cloudflare Workers + Hono** | `Hono v4.x` | 零冷启动、全球边缘低延迟，前后端同仓共享 TypeScript 接口类型。 |
| **云端关系型数据库** | **Cloudflare D1 (SQLite)** | `D1 Serverless` | 原生 SQL 支持单用户词库、学情进度索引，免费额度极高。 |
| **智能补全与语音评测** | **Cloudflare Workers AI** | `@cf/meta/llama-3.1-8b-instruct`<br/>`@cf/openai/whisper` | 无需额外部署 GPU 服务器，直接在 Worker 内免费调用 LLM 生成音节切分/变形/挖空例句，并调用 Whisper 评测跟读音频。 |
| **密码哈希与鉴权** | **Web Crypto API (PBKDF2 + JWT)** | `jose` | 100% 兼容 Cloudflare Workers V8 Isolate 环境，无原生 C++ 依赖，轻量安全。 |

---

## 3. 核心业务管线设计 (Core Technical Pipelines)

### 3.1 词典 + Workers AI 混合智能补全管线

```mermaid
sequenceDiagram
    participant U as 用户端 UI
    participant W as Cloudflare Worker (/api/words/enrich)
    participant Dict as 权威词典 API (有道)
    participant AI as Workers AI (Llama-3.1-8B)

    U->>W: 输入英文单词或词组 (如 "be related to" 或 "write")
    par 并行拉取权威词典与 AI 结构化增强
        W->>Dict: 请求标准音标、词性释义、web_trans 及双语例句
        W->>AI: Prompt 严格输出 JSON：<br/>1. syllables (音节切分如 in·for·ma·tion)<br/>2. meanings (真实中文释义)<br/>3. inflections (复数/三单/过去式/过去分词/现在分词/比较级)<br/>4. homophones (同音异义词数组)<br/>5. examples (双语例句 + 句中目标词精准匹配值 targetInSentence)
    end
    W->>W: 结构化解析：提取冒号前精准释义，处理 be 动词时态变形，清洗占位符
    W-->>U: 返回完整预览数据，供用户确认或手动微调后保存至个人词库
```

### 3.2 跟读评测与录音对比管线

1. **录音采集与本地暂存**：
   - 用户在 `2.3 跟读评测` 点击录音按钮时，前端通过 `navigator.mediaDevices.getUserMedia` + `MediaRecorder` 录制朗读音频 `Blob`，生成本地 Object URL 供一键回放对比。
2. **双模识别与音素相似度打分**：
   - **在线模式（默认首选）**：音频上传至 Worker 端点 `/api/speech/evaluate`，调用 `@cf/openai/whisper` 转写并打分（得分 $\ge 80$ 判为准确）。
   - **离线模式（断网兜底）**：自动切换浏览器原生 `SpeechRecognition` 进行本地音素匹配。
   - **静音环境兜底**：学生随时开启「静音模式」，转为听音辨音标闯关。

---

## 4. 代码工程目录规范 (Project Structure)

```text
EnglishWord/
├── shared/                         # 前后端严格共享的核心协议与纯逻辑函数
│   ├── types.ts                    # 数据模型定义 (User, Word, Progress, Ebbinghaus 等)
│   ├── inflections.ts              # 词形合并、字母槽位切分 (tokenizeIntoSlots)
│   └── ebbinghaus.ts               # 艾宾浩斯 6 阶段算法与顺延推算器
├── functions/                      # Cloudflare Workers 边缘 API (Hono)
│   └── api/
│       ├── [[route]].ts            # Hono 顶层路由分发
│       ├── middleware/             # auth.ts (JWT 验证与 admin/user 角色控制)
│       ├── routes/
│       │   ├── auth.ts             # 登录、改密、初始化 admin
│       │   ├── admin.ts            # 用户账号管理
│       │   ├── words.ts            # 个人词库增删改查与 enrich 智能补全
│       │   ├── sync.ts             # 个人词库与进度增量同步
│       │   └── speech.ts           # TTS 发音代理与 Whisper 评测
│       └── services/
│           ├── enricher.ts         # 有道词典 + Workers AI 查词补全
│           └── crypto.ts           # PBKDF2 密码哈希与 JWT 签名
├── src/                            # 前端 PWA 源码 (React 19 + TypeScript)
│   ├── db/
│   │   └── index.ts                # Dexie.js 数据库定义 (words, progress, checkpoints)
│   ├── stores/                     # Zustand 状态 (authStore, studyStore, reviewStore)
│   ├── services/                   # apiClient, syncService, speechService
│   ├── components/                 # 学习/复习通用组件 (LetterSlotInput, WordEnrichModal 等)
│   └── pages/                      # 页面路由 (LoginPage, WordLibraryPage, StudyPage, ReviewPage, AdminPage)
└── cloudflare/
    └── worker.js                   # 构建产物
```
