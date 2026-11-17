# english-word (智能英语单词学习与艾宾浩斯复习系统)

`english-word` 是一款基于 **React 19 + TypeScript + Vite PWA + IndexedDB (Dexie.js)** 与 **Cloudflare 全家桶 (Pages + Workers + D1 SQLite + Workers AI)** 构建的**跨平台自适应、离线优先 (Local-First)** 扁平多租户英语单词与词组精学软件。

---

## 核心特性

- **扁平多租户架构 (`admin` / `user`)**：
  - **Admin 管理员**：系统管理员仅负责全站用户账号统筹（添加新用户、重置密码为 `passw0rd`、删除用户及查看系统用户概览），默认账号 `admin`，初始密码 `P@ss=w0rd`。
  - **User 学习者（无教师/学生身份分层）**：每个常规用户均为独立平等的学习者，**每个人均可自行添加专属单词或词组**。词库、学习进度与艾宾浩斯复习计划完全独立私有、互不干扰。
- **单条智能录词与结构化补全**：
  - 用户只需输入英文单词或词组，系统通过有道权威词典 API + Cloudflare Workers AI 自动解析并填充**真实中文释义（短语自动截取核心释义）、音标、音节切分（如 `in·for·ma·tion`）、全套词形变化（含 `be` 动词不规则时态）、同音异义词列表及带精准挖空标记的双语例句**。
- **四步递进式「学习」闯关引擎**：
  - **步骤 1（认知展示页）**：展示单词/词组、音节拆解、音标、词性释义、词形变化及双语例句，自动播放真人原声发音。
  - **步骤 2（四步强制闯关）**：
    - **2.1 单词盲打听写**：隐藏原词与中文释义，仅听原声发音原词盲打 **3 遍**；词形变化（相同拼写自动合并）依次弹出盲打 **3 遍**；配备下划线字母格（词组自动留空）、按键音效与“看一眼提示重置当前遍数”机制。
    - **2.2 单词翻译四选一**：1 个正确释义 + 3 个混淆项，选错打乱重选。
    - **2.3 真人跟读语音评测**：云端 Whisper (Workers AI) + 本地语音双模评测，**累计 3 次准确读音**，支持「我的录音 vs 标准音」回放对比与静音环境兜底。
    - **2.4 语境例句变形填空**：根据完整中文翻译与语境提示，手动拼写填入正确的单词形态。
  - **子步骤级断点续学**：中途退出再进无缝续接未完成步骤。
- **艾宾浩斯遗忘曲线盲测「复习」引擎**：
  - **盲测防剧透与随机全覆盖**：所有已学单词自动进入复习池，按 **6 阶段艾宾浩斯周期（+1、+2、+4、+7、+15、+30 天）**筛选每日待复习词；**严禁提前展示当天待复习列表**，按随机顺序（Shuffle）逐个抽考并 100% 覆盖。
  - **未复习自动顺延**：当天未完成复习自动顺延至今天，后续各周期自然动态顺延，绝不挤压。
  - **智能减负与错词回炉惩罚**：直接进入无展示页的 4 步考核，默认听写与跟读各 **1 次**快速通关；若中途出错或看提示，**当场触发 3 遍强化惩罚** + **艾宾浩斯阶段重置回第 1 阶段（次日必考）** + **追加至今日复习队列末尾当日回炉重考**。
  - **同音词保护与毕业词抽查**：盲打提示词性，误输合法同音词仅友好提醒而不扣分；修满 6 阶段标为「已彻底掌握」，支持随机抽查，答错自动复活拉回复习池。
- **PWA 跨平台自适应与离线优先架构**：
  - 完美适配桌面端键盘极速盲打与移动端/平板触屏。
  - 基于 IndexedDB 本地数据库与 Cache Storage 离线预缓存发音 MP3，断网环境照常学习与复习，联网后通过增量协议与 Cloudflare D1 双向同步。

---

## 线上部署信息 (Live Deployment)

- **公网 HTTPS 访问入口**：[https://english-word.aubreyhan.workers.dev](https://english-word.aubreyhan.workers.dev)
- **部署架构**：Cloudflare Workers 单一边缘全栈架构 (内置静态资源路由 + Hono API + Service Worker)
- **云端 D1 数据库**：`english_word_d1` (`75fd4e87-82eb-416f-81e1-7dcd4777682a`)
- **Workers AI 绑定**：`AI` (支持 `@cf/meta/llama-3.1-8b-instruct` 智能补全与 `@cf/openai/whisper` 跟读语音评测)
- **初始超级管理员**：账号 `admin`，密码 `P@ss=w0rd`
- **新建普通用户默认密码**：`passw0rd`

---

## 文档导航

- [PRD.md](file:///Users/aubreyhan/Tools/git_repo/PersonalDev/DevDoc/english-word/PRD.md)：完整产品需求文档、角色权限、核心学习/复习闭环规格与验收标准。
- [ARCHITECTURE.md](file:///Users/aubreyhan/Tools/git_repo/PersonalDev/DevDoc/english-word/ARCHITECTURE.md)：PWA 离线优先架构图、智能补全与评测管线、目录结构规范。
- [SPEC.md](file:///Users/aubreyhan/Tools/git_repo/PersonalDev/DevDoc/english-word/SPEC.md)：Cloudflare D1 数据表、REST API 规范、数据结构定义。
- [IMPLEMENTATION_PLAN.md](file:///Users/aubreyhan/Tools/git_repo/PersonalDev/DevDoc/english-word/IMPLEMENTATION_PLAN.md)：实施步骤与验证路线图。
