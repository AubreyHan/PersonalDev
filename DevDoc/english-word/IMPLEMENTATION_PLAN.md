# Vibe Coding 实施路线图 (IMPLEMENTATION_PLAN) - english-word

> [!IMPORTANT]
> **架构重构升级 (Architecture Refactor)**：
> 针对新需求“扁平多租户架构，取消老师与学生身份，每个人均可自主添加单词与词组，私有独立词库与进度”，执行以下原子化改造路线。

---

## 阶段一：领域模型与数据库结构重构 (Phase 1: Domain & Schema Refactor)

### 任务 1.1：更新共享核心类型定义 (`shared/types.ts`)
- **目标**：将角色简化为 `admin` / `user`；将 `TeacherWord` 重构为通用的 `WordItem`；将 `StudentWordProgress` 重构为 `UserWordProgress`。
- **文件**：`shared/types.ts`。
- **验证**：`npx vitest run shared/__tests__/`。

### 任务 1.2：云端 D1 数据库结构升级与数据重置
- **目标**：重构 SQLite 数据表，建立 `users`, `user_words`, `user_word_progress`。
- **实施要点**：
  - 清理旧的 `teacher_students`, `teacher_words`, `student_word_progress` 表。
  - 创建新的 `user_words`（外键关联 `user_id`）与 `user_word_progress`（主键 `(user_id, normalized_text)`）。
  - 保留并初始化 `admin`（密码 `P@ss=w0rd`）。
- **验证**：D1 远程查询各表结构正常。

---

## 阶段二：边缘后端 API 路由改造 (Phase 2: Edge API Modernization)

### 任务 2.1：Admin 模块改造 (`functions/api/routes/admin.ts`)
- **目标**：Admin 仅管理系统用户（创建新用户、查看用户词数与进度概览、重置密码、删除用户）。
- **删除**：师生多对多绑定与解绑逻辑。

### 任务 2.2：创建个人词库接口 (`functions/api/routes/words.ts`)
- **目标**：取代原 teacher/student 接口，任何 `user` 角色均可自主管理个人词库。
  - `POST /api/words/enrich`：调用有道 API + Workers AI 进行查词与智能补全。
  - `GET /api/words`：获取当前用户词库。
  - `POST /api/words`：录入新词条（重复录入返回 409）。
  - `PUT /api/words/:id` & `DELETE /api/words/:id`：修改与删除词条。

### 任务 2.3：个人增量同步接口 (`functions/api/routes/sync.ts`)
- **目标**：
  - `GET /api/sync/pull`：拉取当前用户的词库与进度。
  - `POST /api/sync/push`：推送学习完成与复习进度。

---

## 阶段三：前端页面与状态机改造 (Phase 3: Frontend PWA UI & Store)

### 任务 3.1：更新客户端 Dexie 本地离线数据库 (`src/db/index.ts`)
- **目标**：适配 `WordItem` 与 `UserWordProgress`，保留全套离线学习/复习/断点缓存机制。

### 任务 3.2：导航栏与路由重组
- **导航精简**：
  - `user`：词库管理 (含智能录词) | 今日学习 | 今日复习 | 学情统计。
  - `admin`：用户管理。
- **删除**：学生端教师筛选下拉框、教师端学生管理抽屉。

### 任务 3.3：词库页面融合 (`WordLibraryPage.tsx`)
- **目标**：集“查看搜索个人生词列表”与“点击添加新词组智能补全”于一体。

---

## 阶段四：集成测试、构建与部署验证 (Phase 4: Verification & Deployment)

### 任务 4.1：运行全套单元测试与集成测试
- **验证指令**：
  ```bash
  npm run test && npm run build:all
  ```

### 任务 4.2：部署至 Cloudflare Workers
- **验证指令**：
  ```bash
  python3 /Users/aubreyhan/.gemini/config/skills/cloudflare-deploy/scripts/deploy_site_to_cf.py --project-dir /Users/aubreyhan/Tools/git_repo/PersonalDev/EnglishWord --name english-word --static-dir dist --d1-name english_word_d1 --enable-ai
  ```
- **验证线上功能**：
  - admin 创建新用户；
  - 普通用户登录并自主录入 `be related to`，验证自动补全释义与例句；
  - 学习四步闯关与艾宾浩斯复习。
