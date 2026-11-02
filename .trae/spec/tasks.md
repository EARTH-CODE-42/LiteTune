# Tasks: 借鉴 rssguard 四项功能

依赖顺序执行；每步完成后即时勾选。

1. [x] 基础设施：Podcast 加 `newCount` 字段；SeenEp 存储（结构/全局数组/Find/Set/Drop/PurgeOrphans/FreeAll）；`seenep=` 配置读写；LoadConfig 默认值重置；RemovePodcast 里 SeenDrop；FEEDS_DONE 里 SeenPurgeOrphans
2. [x] TR 字符串：枚举 + zh/en 表各追加 4 条（TR_M_AUTOREFRESH / TR_AR_MIN_FMT / TR_AR_SET_FMT / TR_BALLOON_NEW_FMT）
3. [x] 新剧集计数与角标：UpdateNewCount；FEED_ADDED 三个路径接入；LBN_SELCHANGE 标记已看；WM_DRAWITEM 右上角红角标绘制
4. [x] 托盘气泡：TrayBalloonNewEpisodes；FeedLoaderThread FEEDS_DONE lParam=netFirst；FEEDS_DONE 触发判断（fullReload && netFirst && total>0 && 窗口隐藏）
5. [x] 自动刷新：g_autoRefreshMin + `autorefresh=` 配置；设置菜单子菜单（id 40-44）；ArmAutoRefreshTimer + WM_TIMER case 6（播放/下载/批量/加载中跳过）+ WM_DESTROY KillTimer 6
6. [x] JSON Feed：ParseFeed 探测 `{`；JsonUnescape/JsonFindKey/JsonSkipValue/JsonObjRange；ParseJsonFeed（频道 + items + attachments）
7. [x] 编译零警告验证 + diff 自审 + 输出用户测试清单（不提交 git）

# Tasks: 第二阶段（项 4 / 7 / 8 / 9）

依赖顺序执行；每步完成后即时勾选并编译（零警告）。

1. [x] 行映射：`g_epMap` + `EpIdxOfRow` / `RowOfEpIdx` / `EpMapPush`；`FillEpisodeList` 写 lParam 与映射；改造 StartBatchCache、ChangeSort、HighlightEpisodeRow、右键命中、LVN_ITEMCHANGED / NM_DBLCLK / NM_CUSTOMDRAW、EP_READY、BuildEpisodeTipText
2. [x] 收藏存储：`FavEp` 列表 + FavHas/Set/Drop/PurgeOrphans/FreeAll；配置 `fav=` 读写；LoadConfig 重置、RemovePodcast 删除、FEEDS_DONE 清理
3. [x] 过滤 UI：IDC_EDIT_FILTER / IDC_BTN_FAVONLY 创建、字体、tooltip、布局（顶部 24px 行）、EN_CHANGE / BN_CLICKED、星形按钮自绘、ReloadFeeds 清空
4. [x] 收藏入口与星标：EpListTitle + 列表第 0 列、剧集右键“收藏/取消收藏”（cmd 4）、单行刷新
5. [x] 每播客刷新间隔：Podcast.refreshMin/lastRefreshMin、PodRefresh 持久化 `podrefresh=`、PodInitRefresh、ArmAutoRefreshTimer 改 60s 心跳、WM_TIMER 按到期入队、EnqueueRefreshUrl 重构、右键子菜单（id 20-25）
6. [x] 托盘角标：MakeBadgeIcon / UpdateTrayBadge（NIM_MODIFY + tip）、各调用点、WM_DESTROY 销毁
7. [x] i18n 9 条中英串补齐（枚举与两张表尾部）
8. [x] 全量编译零警告 + diff 自审 + 交付测试清单（不提交 git）

# Tasks: 第三阶段界面与交互打磨（13 条建议）

每批结束编译零警告并自审；不提交 git。

## 批次 1：快修
- [x] #12 `FillEpisodeList` 子列设置前改 `lvi.mask = LVIF_TEXT; lvi.lParam = 0;`
- [x] #10 语言切换补 `g_btnFavOnly` / `g_editFilter` / `g_btnCache` / `g_btnPhone` tooltip 刷新
- [x] #6 `RevealInExplorer` + 剧集右键 cmd 5“打开文件位置”
- [x] #7 `MenuDrawCheck` 改 `SetPixel` 双段绘制
- [x] #4 `MessageBoxTh` 去掉亮色早退，统一走暗色钩子
- [x] #3 主窗口 `WM_MOUSEWHEEL` 改 `GetCursorPos` + `WindowFromPoint` 分发（简介/滑块/列表）
- [x] 批次 1 编译零警告 + 自审

## 批次 2：标题栏
- [x] #2 标题栏高度 34→30、左侧画 `g_trayIco`、标题左移
- [x] #2 新增 `g_hwndCapTip`，4 个按钮悬停 tooltip；新增 5 条标题栏 i18n
- [x] #2 `CAP_CLOSE` 叉改 `SetPixel` 镜像绘制
- [x] 批次 2 编译零警告 + 自审

## 批次 3：剧集面板
- [x] #5 过滤/收藏/缓存行移到面板边框外，`g_rcPanel[1].top = by+28`，列表 `by+30`
- [x] #5 删除 z-order 兼容块与 `WS_CLIPSIBLINGS`
- [x] #5 手机遥控提为工具栏第 5 按钮（`IDC_BTN_PHONE`/`ICO_PHONE`/`TogglePhoneServer`），移除设置菜单重复入口
- [x] #8 新增 `g_cacheOnly` + `EpMatchesFilter` 缓存判断 + 按钮切换 + `IconKindOf` 按下态 + 新 tooltip 文案
- [x] #1 剧集右键多选：批量收藏/取消收藏（cmd 7/8）、批量下载（cmd 6），混合选择时两项都出
- [x] 新增批次 3 的 8 条 i18n（TR_TIP_PHONE / TR_CTX_REVEAL / 3 条格式串 / 2 条状态串）
- [x] 批次 3 编译零警告 + 自审

## 批次 4：托盘浮窗
- [x] #11 浮窗改单窗口自绘 5 按钮（上一曲/播放暂停/下一曲/随机/退出），移除子按钮
- [x] #11 悬停高亮、圆角窗口、`WM_LBUTTONUP` 内联动作、退出按钮 `WM_CLOSE`
- [x] #11 托盘右键改 `PauseResume()`，删除右键菜单；确认后删除 `TR_TRAY_SHOW` / `TR_TRAY_EXIT`
- [x] #11 `WM_TIMER` case 7 仅当光标不在图标/浮窗内才隐藏，避免重复弹出
- [x] 批次 4 编译零警告 + 自审

## 批次 5：配置
- [x] #9 `BuildConfigText` 按 `[settings]/[lastplayed]/[favorites]/[seen]/[podrefresh]/[feeds]` 分节
- [x] #9 `ParseConfigLine` 忽略节行，旧文件仍可读
- [x] 批次 5 编译零警告 + 自审
- [ ] 交付用户实机测试清单（不提交 git）
