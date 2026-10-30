# 详细设计与规范 (SPEC) - qnap-photosphere

## 1. 数据模型与 SQLite DDL

数据库存放于 SSD 卷（如 `/share/CACHEDEV1_DATA/.qphotosphere/photosphere.db`），开启 WAL 模式以支持极速并发读写。

```sql
-- 用户表
CREATE TABLE users (
    id TEXT PRIMARY KEY,               -- 用户 UUID
    username TEXT UNIQUE NOT NULL,     -- 用户名 (如 aubrey)
    password_hash TEXT NOT NULL,       -- Argon2 / Bcrypt 哈希
    display_name TEXT,                 -- 昵称
    is_admin BOOLEAN DEFAULT 0,        -- 是否管理员
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP
);

-- 资产表 (核心媒体元数据)
CREATE TABLE assets (
    id TEXT PRIMARY KEY,               -- 资产 UUID
    user_id TEXT NOT NULL REFERENCES users(id),
    original_filename TEXT NOT NULL,   -- 原始文件名 (如 IMG_1024.HEIC)
    disk_path TEXT NOT NULL,           -- HDD 物理存储绝对路径
    file_size INTEGER NOT NULL,        -- 文件字节数
    mime_type TEXT NOT NULL,           -- image/jpeg, video/quicktime 等
    sha256 TEXT NOT NULL,              -- 完整内容哈希 (去重索引)
    phash INTEGER,                     -- 64 位感知哈希 (视觉相似对比)
    
    -- EXIF 拍摄信息
    taken_at DATETIME,                 -- 拍摄时间 (从 EXIF 提取，UTC)
    has_valid_exif BOOLEAN DEFAULT 1,  -- 是否具备合法 EXIF
    width INTEGER,
    height INTEGER,
    camera_make TEXT,                  -- 相机厂商 (如 Apple, Sony)
    camera_model TEXT,                 -- 相机型号 (如 iPhone 15 Pro)
    lens_model TEXT,
    focal_length REAL,
    f_number REAL,
    iso INTEGER,
    exposure_time TEXT,
    latitude REAL,                     -- GPS 纬度
    longitude REAL,                    -- GPS 经度
    altitude REAL,
    country TEXT,
    city TEXT,

    -- 挑片与整理状态
    pick_status TEXT DEFAULT 'none',   -- 'none', 'picked', 'rejected'
    rating INTEGER DEFAULT 0,          -- 星级 0-5
    is_favorite BOOLEAN DEFAULT 0,
    is_archived BOOLEAN DEFAULT 1,     -- 1: 已归档到正式库, 0: 待确认队列
    is_trashed BOOLEAN DEFAULT 0,      -- 是否已移入回收站
    trashed_at DATETIME,

    -- 配对与关联 (Live Photo / RAW+JPG)
    group_id TEXT,                     -- 资产组 UUID (Live Photo 的 HEIC 与 MOV 相同 group_id)
    group_type TEXT,                   -- 'primary', 'companion_video', 'companion_raw', 'xmp'

    created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
    updated_at DATETIME DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX idx_assets_user_taken ON assets(user_id, taken_at DESC);
CREATE INDEX idx_assets_sha256 ON assets(sha256);
CREATE INDEX idx_assets_phash ON assets(phash);
CREATE INDEX idx_assets_pick_status ON assets(user_id, pick_status);
CREATE INDEX idx_assets_group_id ON assets(group_id);

-- 人脸聚类表
CREATE TABLE faces (
    id TEXT PRIMARY KEY,
    asset_id TEXT NOT NULL REFERENCES assets(id) ON DELETE CASCADE,
    person_id TEXT,                    -- 关联到具体人脸实体
    bounding_box TEXT NOT NULL,        -- 人脸检测框 JSON: {"x": 0.1, "y": 0.2, "w": 0.15, "h": 0.2}
    embedding BLOB NOT NULL,           -- 512 维特征向量 (Float32 数组)
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE persons (
    id TEXT PRIMARY KEY,
    name TEXT NOT NULL,
    representative_face_id TEXT,       -- 代表封面头像 ID
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP
);

-- 相册表 (虚拟相册，解耦物理路径)
CREATE TABLE albums (
    id TEXT PRIMARY KEY,
    user_id TEXT NOT NULL REFERENCES users(id),
    name TEXT NOT NULL,
    description TEXT,
    is_family_shared BOOLEAN DEFAULT 0,-- 是否家庭共享公共相册
    cover_asset_id TEXT REFERENCES assets(id),
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE album_assets (
    album_id TEXT NOT NULL REFERENCES albums(id) ON DELETE CASCADE,
    asset_id TEXT NOT NULL REFERENCES assets(id) ON DELETE CASCADE,
    added_at DATETIME DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (album_id, asset_id)
);
```

---

## 2. Immich 兼容移动端 API 规范

后端原生暴露 `/api/*` 路径，直接对接官方 Immich 手机客户端：

### 2.1 服务端信息探测
- **GET `/api/server-info/version`**
  - **Response 200 OK**:
    ```json
    { "major": 1, "minor": 105, "patch": 0 }
    ```
- **GET `/api/server-info/features`**
  - **Response 200 OK**:
    ```json
    { "facialRecognition": true, "map": true, "reverseGeocoding": true }
    ```

### 2.2 用户鉴权
- **POST `/api/auth/login`**
  - **Request Body**:
    ```json
    { "email": "aubrey", "password": "your-password" }
    ```
  - **Response 200 OK**:
    ```json
    {
      "accessToken": "eyJhbGci...",
      "userId": "user-uuid",
      "userEmail": "aubrey",
      "firstName": "Aubrey"
    }
    ```

### 2.3 资产上传与比对
- **POST `/api/asset/bulk-upload-check`** (客户端快速查重)
  - **Request Body**:
    ```json
    { "assets": [{ "id": "local-hash-1", "checksum": "base64-sha1-or-sha256" }] }
    ```
  - **Response 200 OK**:
    ```json
    { "results": [{ "id": "local-hash-1", "action": "accept", "reason": "not_found" }] }
    ```
- **POST `/api/asset/upload`** (图片与视频文件流上传)
  - **Header**: `Authorization: Bearer <token>`, `Content-Type: multipart/form-data`
  - **Body 字段**:
    - `assetData`: 媒体二进制文件
    - `deviceAssetId`: 手机本地 ID
    - `deviceId`: 手机设备唯一标识
    - `fileCreatedAt`: 本地创建时间
    - `livePhotoData`: 可选（iOS Live Photo 伴生 MOV 视频）
  - **处理过程**：服务端接收并落盘到用户 Inbox -> 极速生成缩略图 -> 触发即时归档流水线。

---

## 3. 业务规则与边缘处理

### 3.1 资产落盘重命名算法
```text
输入: 用户私有根目录, EXIF 拍摄时间 (如 2026-10-30), 原文件名 (如 IMG_1024.JPG), 文件 SHA256

1. target_dir = /share/CACHEDEV2_DATA/Photos/users/{username}/20261030/
2. 若 target_dir 不存在，递归创建权限 0755。
3. target_file = target_dir + "IMG_1024.JPG"
4. 若 target_file 已存在:
   a. 计算 target_file 的现有 SHA256。
   b. 若两者 SHA256 相同:
      - 判定为完全重复。
      - 将上传/导入的重复文件移入 /share/.../Photos/.trash/duplicates/{timestamp}_IMG_1024.JPG。
      - 返回已存在资产的 ID，不重复生成数据库记录。
   c. 若两者 SHA256 不同:
      - 循环递增后缀: target_file = target_dir + "IMG_1024_1.JPG", "IMG_1024_2.JPG"... 直至文件名不存在。
5. 执行原子移动 (os.Rename / io.CopyFallback):
   - 导入源文件 -> target_file
6. 返回最终落盘物理绝对路径与文件名。
```

### 3.2 挑片快捷键与状态机
- 每个资产具备三种挑片状态：
  - `none`（未打标，默认状态）
  - `picked`（按 `P` 键，标记保留/精选，UI 上显示绿色旗标）
  - `rejected`（按 `X` 键，标记淘汰，UI 上显示深灰/半透明淘汰样式）
- 再次按下相同键时，状态回退为 `none`。
- 按下 `1-5` 键立即更新评星，`0` 键清除评星。
- 点击“清理淘汰照片”按钮：系统将当前相册或筛选视图中所有 `rejected` 资产软删除（更新 `is_trashed = 1`，物理文件联动移入 HDD 根目录下的 `.trash/rejected/`）。
