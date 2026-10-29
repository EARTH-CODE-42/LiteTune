# Tasks: 借鉴 rssguard 四项功能

依赖顺序执行；每步完成后即时勾选。

1. [x] 基础设施：Podcast 加 `newCount` 字段；SeenEp 存储（结构/全局数组/Find/Set/Drop/PurgeOrphans/FreeAll）；`seenep=` 配置读写；LoadConfig 默认值重置；RemovePodcast 里 SeenDrop；FEEDS_DONE 里 SeenPurgeOrphans
2. [x] TR 字符串：枚举 + zh/en 表各追加 4 条（TR_M_AUTOREFRESH / TR_AR_MIN_FMT / TR_AR_SET_FMT / TR_BALLOON_NEW_FMT）
3. [x] 新剧集计数与角标：UpdateNewCount；FEED_ADDED 三个路径接入；LBN_SELCHANGE 标记已看；WM_DRAWITEM 右上角红角标绘制
4. [x] 托盘气泡：TrayBalloonNewEpisodes；FeedLoaderThread FEEDS_DONE lParam=netFirst；FEEDS_DONE 触发判断（fullReload && netFirst && total>0 && 窗口隐藏）
5. [x] 自动刷新：g_autoRefreshMin + `autorefresh=` 配置；设置菜单子菜单（id 40-44）；ArmAutoRefreshTimer + WM_TIMER case 6（播放/下载/批量/加载中跳过）+ WM_DESTROY KillTimer 6
6. [x] JSON Feed：ParseFeed 探测 `{`；JsonUnescape/JsonFindKey/JsonSkipValue/JsonObjRange；ParseJsonFeed（频道 + items + attachments）
7. [x] 编译零警告验证 + diff 自审 + 输出用户测试清单（不提交 git）
