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
