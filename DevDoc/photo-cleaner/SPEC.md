# PhotoCleaner 详细规格说明书 (SPEC)

---

## 1. 核心领域数据模型 (Core Swift Models)

### 1.1 月-日切片与进度状态 (`MonthDay.swift`)

```swift
import Foundation

/// 表示公历全年 366 个固定“月-日”切片之一（含 2月29日）
public struct MonthDay: Hashable, Codable, Comparable, Sendable {
    public let month: Int // 1...12
    public let day: Int   // 1...31

    /// 唯一紧凑键，格式 "MM-DD"，如 "08-13"、"02-29"，用于 JSON 字典持久化 Key
    public var storageKey: String {
        String(format: "%02d-%02d", month, day)
    }

    public static func < (lhs: MonthDay, rhs: MonthDay) -> Bool {
        lhs.month != rhs.month ? lhs.month < rhs.month : lhs.day < rhs.day
    }

    /// 全年固定 366 天序列（按闰年如 2024 年日历生成 01-01 至 12-31）
    public static let all366Days: [MonthDay] = { ... }()
}

/// 某月-日切片的四态整理进度
public enum DayReviewStatus: String, Sendable {
    case empty      // 历年该日 0 张照片/视频（置灰，自动跳过）
    case pending    // 有资产，且从未标记完成（reviewedIDs 为空）
    case completed  // 有资产，且当前全部存量资产 ID 均属于 reviewedIDs
    case hasNew     // 曾标记完成，但当前存在差集 (currentIDs - reviewedIDs 非空)
}

/// 单个日历格的聚合摘要
public struct DaySliceSummary: Identifiable, Sendable {
    public var id: String { monthDay.storageKey }
    public let monthDay: MonthDay
    public let totalCount: Int
    public let newUnreviewedCount: Int
    public let status: DayReviewStatus
}
```

---

### 1.2 资产包装与过滤规格 (`AssetItem.swift`)

```swift
import Photos

public enum MediaFilter: String, CaseIterable, Identifiable, Sendable {
    case all              // 全部
    case photosOnly       // 仅照片（含 Live Photo）
    case videosOnly       // 仅视频
    case screenshots      // 仅截屏与屏幕录制
    case largeFiles       // 大体积（>= 20 MB）
    case similarGroups    // 仅相似/连拍组内项
    case uncategorized    // 未归属任何普通用户相簿
    case newOnly          // 仅新增未审项（仅在 DayReviewStatus == .hasNew 时展示）

    public var id: String { rawValue }
}

public struct AssetExifDetail: Sendable {
    public let filename: String
    public let fileSizeByteCount: Int64
    public let pixelWidth: Int
    public let pixelHeight: Int
    public let uniformTypeIdentifier: String // 如 public.heic, com.apple.quicktime-movie
    public let captureDate: Date?
    public let cameraMakeAndModel: String?   // 如 "Apple iPhone 16 Pro"
    public let lensAndExposure: String?      // 如 "24mm f/1.78 · 1/120s · ISO 64"
    public let locationDescription: String?  // 逆地理编码城市/地标或经纬度
    public let isLocallyAvailable: Bool      // 原片是否已在本地磁盘（非仅在 iCloud）
}
```

---

### 1.3 用户相簿树与三态归属模型 (`AlbumModels.swift`)

```swift
import Foundation

/// 用户自建普通相簿节点（subtype == .albumRegular 且支持增删内容）
public struct UserAlbumNode: Identifiable, Hashable, Sendable {
    public let id: String                 // PHAssetCollection.localIdentifier
    public let title: String              // 相簿名，如 "2025日本"
    public let breadcrumbPath: String     // 含文件夹前缀完整路径，如 "旅行 / 2025日本"
    public let parentFolderID: String?    // 父级 PHCollectionList.localIdentifier（顶层为 nil）
    public let assetCount: Int            // 当前相簿内资产总数
}

/// 用户自建相簿文件夹节点（subtype == .regularFolder）
public struct UserFolderNode: Identifiable, Hashable, Sendable {
    public let id: String                 // PHCollectionList.localIdentifier
    public let title: String
    public let breadcrumbPath: String
    public let parentFolderID: String?
    public let childFolderIDs: [String]
    public let childAlbumIDs: [String]
}

/// 选中 1~N 个资产时，相对于某个普通相簿的三态归属关系
public enum AlbumMembershipRelation: Sendable {
    case allSelectedInAlbum      // [✓] 全部选中资产均属于该相簿（归入【所属相簿】区块）
    case partiallySelectedInAlbum// [-] 部分选中资产属于该相簿（归入【所属相簿】区块）
    case noneSelectedInAlbum     // [ ] 所有选中资产均不属于该相簿（归入【未加入相簿】区块）
}

public struct AlbumMembershipRow: Identifiable, Sendable {
    public var id: String { album.id }
    public let album: UserAlbumNode
    public let matchedSelectedCount: Int  // 当前选中的 K 个资产中有几个在该相簿中
    public let totalSelectedCount: Int    // 当前共选中 K 个资产
    public let relation: AlbumMembershipRelation
}
```

---

### 1.4 本地持久化状态 Schema (`PersistedState.swift`)

存储路径：`~/Library/Application Support/PhotoCleaner/state.json`（采用写临时文件 + `FileManager.replaceItemAt` 原子替换防损坏）。

```json
{
  "schemaVersion": 1,
  "reviewedAssetIDsByDay": {
    "08-13": [
      "B84E2C1A-91F2-4D30-A123-000000000001/L0/001",
      "C91F3D2B-82E1-4C10-B456-000000000002/L0/001"
    ],
    "11-11": [
      "D12A4E3C-73D0-4B20-C789-000000000003/L0/001"
    ]
  },
  "trashBasketAssetIDs": [
    "E45B6F7D-11A2-4E30-D012-000000000099/L0/001"
  ],
  "lifetimeStats": {
    "totalDeletedAssetCount": 1280,
    "totalFreedBytes": 19756849561,
    "totalEmptyAlbumsDeleted": 14
  },
  "preferences": {
    "includeHiddenAssets": false,
    "autoDeleteEmptyAlbums": true,
    "autoAdvanceAfterMarkDelete": true,
    "similaritySensitivity": "standard"
  }
}
```

---

## 2. 核心业务规则与算法规范 (Core Business Rules & Algorithms)

### 2.1 相簿归属切换与批量三态交互规则
设当前用户在网格或大图快审中选中的资产集合为 $S$（$|S| \ge 1$），目标普通相簿为 $A$，该相簿当前包含的资产集合为 $M_A$：
1. **分区排序规则**:
   - **【所属相簿】区块**: 包含所有 $S \cap M_A \neq \emptyset$ 的相簿。内部排序：`allSelectedInAlbum` 在前，`partiallySelectedInAlbum` 在后，同级按 `breadcrumbPath` 本地化字典序升序排列。
   - **【未加入相簿】区块**: 包含所有 $S \cap M_A = \emptyset$ 的相簿，按 `breadcrumbPath` 升序排列。
   - 当顶部搜索框非空时，两个区块同时按关键词对 `breadcrumbPath` 做大小写不敏感子串过滤。
2. **点击行切换动作**:
   - 若当前状态为 `noneSelectedInAlbum` `[ ]` **或** `partiallySelectedInAlbum` `[-]`：
     - 计算待补齐差集 $\Delta_{\text{add}} = S \setminus M_A$；
     - 调用 `PHAssetCollectionChangeRequest(for: collection)?.addAssets(deltaAssets)`，完成后状态变为 `allSelectedInAlbum` `[✓]`。
   - 若当前状态为 `allSelectedInAlbum` `[✓]`：
     - 调用 `PHAssetCollectionChangeRequest(for: collection)?.removeAssets(selectedAssets)`，将 $S$ 全部从相簿 $A$ 中移出，状态变为 `noneSelectedInAlbum` `[ ]`；
     - **联动检测**: 移出成功后刷新相簿 $A$ 的成员数，若 `assetCount == 0` 且偏好设置 `autoDeleteEmptyAlbums == true`，立即调用 §2.2 空相簿级联清理算法。
3. **面板内新建相簿并归入**:
   - 输入新相簿名 `trimmedTitle`（非空），可选选择父级文件夹 `parentFolder`（默认顶层）；
   - 在**单次** `PHPhotoLibrary.shared().performChanges` 事务内执行：
     1. `let createReq = PHAssetCollectionChangeRequest.creationRequestForAssetCollection(withTitle: trimmedTitle)`
     2. `createReq.addAssets(selectedAssets)`
     3. 若指定了 `parentFolder`，调用 `PHCollectionListChangeRequest(for: parentFolder)?.addChildCollections([createReq.placeholderForCreatedAssetCollection])`
   - 事务完成后刷新 `AlbumGraphService`，新相簿立即出现在【所属相簿】区块。

---

### 2.2 空相簿与空文件夹自底向上级联清理算法 (`EmptyCollectionSweeper`)

1. **纳入扫描白名单（安全防线）**:
   - **相簿节点**: 必须同时满足 `collectionType == .album`、`collectionSubtype == .albumRegular`、`canPerform(.delete)` 为 `true`（严格排除系统智能相簿 `.smartAlbum`、iCloud 共享相簿 `.albumCloudShared`、从 Mac/Finder 同步的只读相簿 `.albumSyncedAlbum`）。
   - **文件夹节点**: 必须同时满足 `collectionListType == .folder`、`collectionListSubtype == .regularFolder`、`canPerform(.delete)` 为 `true`。
2. **两阶段自底向上执行步骤**:
   - **Step 1（批量删除空普通相簿）**:
     - 查询所有真实资产数（`PHAsset.fetchAssets(in: collection, options: nil).count` — 注意此处 `options` 使用默认包含隐藏资产计数，**防止某相簿内其实藏有“已隐藏”照片却被误判为空相簿删掉！**）严格等于 `0` 的白名单普通相簿集合 $E_{\text{albums}}$；
     - 若 $E_{\text{albums}} \neq \emptyset$，在单次 `performChanges` 中执行 `PHAssetCollectionChangeRequest.deleteAssetCollections(E_albums)`。
   - **Step 2（循环自底向上删除空相簿文件夹）**:
     - 重新遍历用户文件夹树，按树深度**从最深叶子层向根部（Bottom-Up）**检查每个白名单文件夹 $F$：
     - 查询 `PHCollection.fetchCollections(in: F, options: nil).count == 0`（内部既无任何子相簿——包括智能相簿/普通相簿，也无任何子文件夹）；
     - 若本轮发现空文件夹集合 $E_{\text{folders}} \neq \emptyset$，在单次 `performChanges` 中执行 `PHCollectionListChangeRequest.deleteCollectionLists(E_folders)`，随后重复检查其父级文件夹，直至整棵目录树不存在空文件夹为止；
     -若用户在系统确认弹窗中点击“不允许/取消”，捕获 `PHPhotosError.userCancelled` 静默终止本轮清理，不视为异常崩溃，也不无限循环重试。

---

### 2.3 当日相似图聚类参数规范 (`DaySimilarityEngine`)
1. **输入范围**: 当前选中 `MonthDay` 下所有 `mediaType == .image` 的资产（Live Photo 取其静态封面图参与比对，纯视频不跑图像特征提取）。
2. **分组隔离**: 不同年份的照片绝对不跨年合并为相似组（例如 `2024-08-13` 与 `2025-08-13` 各自独立聚类）。
3. **特征距离阈值 (`VNFeaturePrintObservation.computeDistance`)**:
   - **严格档 (`strict`)**: `distance <= 0.32` 且同日拍摄时间差 `<= 600s (10分钟)`；
   - **标准档 (`standard`, 默认)**: `distance <= 0.45` 且同日拍摄时间差 `<= 14400s (4小时)`；
   - **原生连拍直通**: 凡 `asset.burstIdentifier != nil` 相同的照片无条件归入同一组。
4. **输出排序**: 每个 `SimilarityGroup` 内部按 `creationDate` 升序排列；网格渲染时保持原时间轴位置不移动、不折叠。

---

### 2.4 全局快捷键映射规范 (Keyboard Shortcuts)

| 快捷键 | 生效视图 | 动作说明 |
| :--- | :--- | :--- |
| `Space` | 网格 / 大图快审 | 在“年份分组网格”与“大图快审”之间无缝切换（视频播放中若焦点在播放器则暂停/播放，按 `Esc` 退回网格） |
| `Esc` | 大图快审 / 对比视图 | 退出当前大图快审或相似对比视图，返回年份分组网格 |
| `←` / `→` | 网格 / 大图快审 | 移动选中焦点至上一项 / 下一项 |
| `Delete` / `⌫` / `X` | 网格 / 大图快审 / 对比 | 将当前选中项加入“待删暂存篮”（若已在篮中则移出恢复；遇到收藏 `❤️` 项自动跳过并弹 Toast） |
| `F` | 网格 / 大图快审 / 对比 | 同步切换选中项在系统相册中的“收藏 ❤️”状态（若点亮红心且该项原在待删篮中，自动移出待删篮） |
| `L` | 大图快审 (Live Photo) | 触发播放当前实况照片动态效果 |
| `C` | 网格（选中 $\ge 2$ 张或相似组） | 打开同屏并排同步缩放对比视图 |
| `I` | 网格 / 大图快审 | 显示 / 隐藏右侧“相簿归属 + EXIF 详情”检视栏 |
| `⌘Z` | 全局 | 撤销最近一次“加入/移出待删暂存篮”操作（维护内存 Undo 栈，最多 50 步） |
| `⌘Return` | 全局（暂存篮非空时） | 触发【提交清理】，批量调用 PhotoKit 将暂存篮资产移入系统“最近删除” |
| `⌘[` / `⌘]` | 全局 | 切换到日历中的前一个有照片日期 / 下一个待整理日期 |

---

## 3. 边界条件与异常矩阵 (Edge Cases & Exception Matrix)

| 编号 | 边界场景 | 确定性处理策略 |
| :--- | :--- | :--- |
| **EC-1** | **闰年 `02-29` 照片处理** | 366 天日历面板永久固定包含 `2月29日` 格子；平年打开应用时若当天非 2月29日正常定位今天，用户随时可点击 `02-29` 清理 2016/2020/2024 等闰年这一天的照片。 |
| **EC-2** | **相簿内仅剩“已隐藏”照片** | 应用默认未开启显示隐藏照片时，某相簿在界面看似 0 张，但底层实际含有 2 张隐藏照片。`EmptyCollectionSweeper` 判定空相簿时**强制使用 `includeHiddenAssets = true` 统计真实总数**，绝不误删含有隐藏隐私照片的非空相簿！ |
| **EC-3** | **用户拒绝“删除空相簿”系统弹窗** | 当 macOS 弹出“允许删除相簿吗？”系统框时，若用户点“不允许”，捕获 `PHPhotosError.userCancelled`，将该相簿 ID 记入本次会话忽略集 `sessionIgnoredEmptyAlbumIDs`，本次运行期间不再反复弹窗骚扰。 |
| **EC-4** | **iCloud 共享图库无删除权限项** | 在 iCloud 共享图库中由他人贡献且当前用户无删除权限（`!asset.canPerform(.delete)`）的资产，尝试加入待删暂存篮时拦截并轻提示“该共享图库项目不支持由当前账号删除”。 |
| **EC-5** | **暂存篮持久化资产已在外部被删** | 应用下次启动恢复 `trashBasketAssetIDs` 或监听到 `photoLibraryDidChange` 时，自动与当前有效 `PHAsset` 交集校验，已被外部删除的失效 ID 静默剔除，不计入重复释放统计。 |
| **EC-6** | **文件体积快速估算与精确读取** | 网格浏览不逐一读取全量 `PHAssetResource` 以免卡顿；当资产加入“待删暂存篮”、开启“大体积过滤”或点开右侧详情栏时，异步通过 `PHAssetResource.assetResources(for: asset)` 读取 `fileSize`（KVC `unsignedLongLongValue`）缓存于内存字典。 |
| **EC-7** | **超多相簿搜索与新建重名** | 若用户在“新建相簿”输入与同层级现有普通相簿完全同名的标题，界面提示“同名相簿已存在，将直接归入现有相簿”并直接调用 `addAssets` 加入现有相簿，避免制造重复同名相簿。 |
