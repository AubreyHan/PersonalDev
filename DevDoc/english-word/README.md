# english-word (智能英语单词学习与艾宾浩斯复习系统)

`english-word` 是一款基于 **React 19 + TypeScript + Vite PWA + IndexedDB (Dexie.js)** 与 **Cloudflare 全家桶 (Pages + Workers + D1 SQLite + Workers AI)** 构建的**跨平台自适应、离线优先 (Local-First)** 多租户英语单词与词组精学软件。

---

## 核心特性

- **多租户三角色层级管理 (`admin` / `teacher` / `student`)**：
  - **Admin 管理员**：内置默认管理员 `admin`（初始密码 `P@ss=w0rd`）；遵循**“先创建教师，再在教师名下新建或关联现有学生”**的严格归属流程，支持师生多对多（N:M）绑定、移出解绑、彻底删除保護与密码一键重置（新用户及重置默认密码均为 `passw0rd`）。
  - **Teacher 教师端**：单条智能录入单词/词组，通过词典 API + Cloudflare Workers AI 自动补全**音标、音节切分（如 `in·for·ma·tion`）、中文释义、全套词形变化、同音异义词列表及带挖空标记的双语例句**；支持名下学生学情概览、今日复习完成度监控与高频错词/多次降级顽固词榜单。
  - **Student 学生端**：跨教师词库自动聚合（支持按教师/状态筛选），跨教师同词学习与复习进度全局互通免重学，围绕**「学习」**与**「复习」**两大核心闭环构建肌肉记忆。
- **四步递进式「学习」闯关引擎**：
  - **步骤 1（认知展示页）**：展示单词/词组、音节切分、音标、词性释义、词形变化（复数/时态/比较级等）及双语例句，自动播放真人发音并提供反复听音按钮。
  - **步骤 2（四步强制闯关）**：
    - **2.1 单词盲打听写**：隐藏原词与中文释义，仅听发音原词盲打 **3 遍**；若有词形变化（拼写相同的规则变化如过去式与过去分词自动合并），每种变化形式也依次渐进式弹出盲打 **3 遍**；配备下划线字母格（词组自动留空）、实时敲击音效及“看一眼提示/退回重学（重置当前计数）”机制。
    - **2.2 单词翻译四选一**：1 个正确中文释义 + 3 个词库/内置混淆池干扰项，选错自动打乱重选。
    - **2.3 真人跟读语音评测**：云端 Whisper (Workers AI) + 本地离线语音双模评测，**累计 3 次准确读音**（读错不清零已点亮进度），支持「我的录音 vs 标准音」回放对比与静音环境兜底开关。
    - **2.4 语境例句变形填空**：展示带中文翻译的挖空英文例句，学生根据时态与语境手动拼写填入正确的单词或其词形变化形态。
  - **子步骤级断点续学**：本地 IndexedDB 实时保存当前单词的子步骤进度（如 `step: 2.3, passCount: 2`），中途退出再进无缝续接。
- **艾宾浩斯遗忘曲线盲测「复习」引擎**：
  - **盲测防剧透与随机全覆盖**：所有已学单词自动进入复习池，按 **6 阶段艾宾浩斯周期（+1、+2、+4、+7、+15、+30 天）**筛选每日待复习词；**严禁提前展示当天待复习列表**，按随机顺序（Shuffle）逐个抽考并 100% 覆盖。
  - **未复习自动顺延**：若当天未完成复习，剩余未复习单词及其后续周期节点自动顺延一天，绝不产生多周期挤压。
  - **智能减负与错词回炉惩罚**：直接进入无展示页的 4 步考核，默认听写与跟读各 **1 次**快速通关；若中途出错或查看提示，**当场触发 3 遍强化惩罚** + **艾宾浩斯阶段重置回第 1 阶段（次日必考）** + **追加至今日复习队列末尾同日回炉重考**。
  - **同音词保护与毕业词抽查复活**：听写时展示词性提示，误输合法同音词仅友好提醒而不触发惩罚；修满 6 阶段标为「已彻底掌握」后支持随时随机抽查，答错自动复活拉回复习池。
- **PWA 跨平台自适应与离线优先架构**：
  - 完美适配桌面端（PC/Mac 键盘极速盲打）、平板（iPad）与手机端触屏。
  - 基于 IndexedDB 本地数据库与 Cache Storage 离线预缓存待学/待复习单词及词组的真人发音 MP3，断网环境照常学习与复习，恢复联网后通过增量同步协议与 Cloudflare D1 云端双向同步。

---

## 线上部署状态 (Live Deployment)

- **公网 HTTPS 访问入口**：[https://english-word.aubreyhan.workers.dev](https://english-word.aubreyhan.workers.dev)
- **部署架构**：Cloudflare Workers 单一边缘全栈架构 (内置静态资源路由 + Hono API + Service Worker)
- **云端 D1 数据库**：`english_word_d1` (`75fd4e87-82eb-416f-81e1-7dcd4777682a`)
- **Workers AI 绑定**：`AI` (支持 `@cf/meta/llama-3.1-8b-instruct` 结构化智能查词与 `@cf/openai/whisper` 跟读语音评测)
- **初始超级管理员**：账号 `admin`，密码 `P@ss=w0rd`
- **新建教师与学生默认密码**：`passw0rd` (用户登录后可在右上角修改密码，管理员可一键重置)

---

## 文档导航

本项目文档完全遵循 Vibe Spec 架构规范，为后续在业务工程仓库 `/Users/aubreyhan/Tools/git_repo/PersonalDev/EnglishWord` 中进行 AI 辅助编码提供完备的上下文输入：

- [PRD.md](file:///Users/aubreyhan/Tools/git_repo/PersonalDev/DevDoc/english-word/PRD.md)：完整产品需求文档、三角色权限矩阵、核心学习/复习闭环规格与验收标准。
- [ARCHITECTURE.md](file:///Users/aubreyhan/Tools/git_repo/PersonalDev/DevDoc/english-word/ARCHITECTURE.md)：PWA 离线优先与 Cloudflare 全家桶架构图、智能补全与语音评测管线、目录结构与离线同步机制。
- [SPEC.md](file:///Users/aubreyhan/Tools/git_repo/PersonalDev/DevDoc/english-word/SPEC.md)：Cloudflare D1 / IndexedDB 数据字典、REST API 规范、艾宾浩斯状态机与子步骤断点数据结构。
- [IMPLEMENTATION_PLAN.md](file:///Users/aubreyhan/Tools/git_repo/PersonalDev/DevDoc/english-word/IMPLEMENTATION_PLAN.md)：分阶段原子化 Vibe Coding 实施路线图与每一步的确定性验证指令。
