# Spec: 借鉴 rssguard 的四项功能（LiteTune / main.c）

用户已授权四项改进（原话“我相信你，你看着改进就行了”）；全局热键、缓存自动清理已实现，不碰。
风格约束：简洁、奥卡姆剃刀、中文注释、编译零警告、不自行截图测试、不提交 git。

## 已探明的关键事实（main.c 行号基于当前工作区）

- 配置持久化：`LoadConfig` L3216（默认值重置区 L3224-3238）、`ParseConfigLine` L2986、`BuildConfigText` L3310（`lastep=` 写法 L3350-3363）。
- 每播客续播记录：`LastEp` 结构 L298-304，`LastEpHash/Find/Set/Drop/PurgeOrphans/FreeAll` L2625-2705；`RemovePodcast` 里 `LastEpDrop` L5776；`FEEDS_DONE` 里 `LastEpPurgeOrphans` L10135。
- 加载流程：`ParseFeed` L2285（XML）；`LoadOneFeed` L3522（netFirst 语义：0=缓存优先 1=强制联网）；`FeedLoaderThread` L3576 发 `WM_APP_FEED_ADDED`(wParam=0) 与 `WM_APP_FEEDS_DONE`(wParam=loaded, lParam=0)；`SingleFeedLoaderThread` L3598（wParam=1 新增 / 2 原位替换）。
- 主窗口消息：`WM_APP_FEED_ADDED` L10059、`WM_APP_FEEDS_DONE` L10114（`g_fullReload` 区分全量/单条）。
- 播客列表自绘：`WM_DRAWITEM` + `IDC_LIST_POD` L9215-9386；右侧剧集数绘制 L9369-9381；行高 54。
- 选中处理：`WM_COMMAND` `LBN_SELCHANGE`（仅用户操作触发，程序 LB_SETCURSEL 不触发）L9723-9725。
- 托盘：`NOTIFYICONDATA` uID=1 创建于 L9095-9110；删除于 WM_DESTROY L10361。
- 定时器：主窗口 `WM_TIMER` L9880；已用 ID 1/2/3/4/5；WM_DESTROY 里 KillTimer L10352。
- 设置菜单：`OnSettingsButton` L8326；自动清理子菜单范式 L8345-8357（命令 id 30-33）；命令分发 L8398+；已用 id：1-9,20-26,30-33,100+分钟。
- 日期解析：`ParseDate` L2216 支持 ISO8601（`2015-02-03T12:34:56Z`）→ dateKey（YYYYMMDDHHMM 分钟级）。
- `U8ToW` L1871；`ParseDuration` L2205。

## 功能 1：刷新发现新剧集时托盘气泡通知

- 新增 `TrayBalloonNewEpisodes(int total)`：NIM_MODIFY + NIF_INFO，标题 "LiteTune"，正文用新格式串 `TR_BALLOON_NEW_FMT`（zh“发现 %d 集新剧集”/en“Found %d new episode(s)”），NIIF_INFO。
- 触发点：`WM_APP_FEEDS_DONE`：仅当 `g_fullReload && netFirst && 总数>0 && !IsWindowVisible(g_hwnd)`（窗口可见时用户直接看角标，不打扰）。
- netFirst 传递：`FeedLoaderThread` 改发 `WM_APP_FEEDS_DONE` 时 `lParam = a->netFirst`（先存局部变量再 FreeFeedLoadArg）。单条线程保持 lParam=0。
- 启动加载（netFirst=0）与 OPML 批量导入不弹气泡。

## 功能 2：播客列表每订阅新剧集角标

- `Podcast` 结构加 `int newCount;`（现有 memset/calloc 路径自动清零）。
- “已看”状态：仿 LastEp 新增并行存储 `typedef struct { unsigned int hash; long long seenKey; } SeenEp;` + `g_seenEps/g_seenEpCount/g_seenEpCap` + `SeenFind/SeenSet/SeenDrop/SeenPurgeOrphans/SeenFreeAll`（无堆字符串，比 LastEp 更简单）。
- 持久化：`BuildConfigText` 写 `seenep=<hex>,<dateKey>`；`ParseConfigLine` 解析；`LoadConfig` 默认值区调 `SeenFreeAll`；`RemovePodcast` 调 `SeenDrop`；`FEEDS_DONE` 调 `SeenPurgeOrphans`。
- 计数：新增 `UpdateNewCount(Podcast *p)` = 统计 `dateKey > seenKey` 的集数（无记录视为全已看→0；dateKey=0 的集永不计入）。
- 调用点：
  - `WM_APP_FEED_ADDED`：替换路径（wParam=2）与全量插入（wParam=0）落地后 `UpdateNewCount`；单条新增（wParam=1）先把 seenKey 置为当前 max dateKey（新订阅不炸角标）再计数（=0）。
  - `LBN_SELCHANGE`（IDC_LIST_POD）：`SeenSet(hash, maxDateKey)` + `newCount=0` + `InvalidateRect(g_listPod)`。程序式选中不清角标（LBN_SELCHANGE 不触发）。
- 绘制：`WM_DRAWITEM` 播客行右上角画红色圆角徽标（rc.right-26..rc.right-4, rc.top+4, 高 16），数字用 g_fontSmall 白色居中；仅非缓存行、非失败占位、`newCount>0` 时画；与垂直居中的剧集数不重叠（剧集数中心 27px，徽标 4-20px）。

## 功能 3：周期性自动刷新

- 新全局 `g_autoRefreshMin`（默认 60，0=关），配置键 `autorefresh=`（默认值不写盘，与 cachedays 一致）。
- 设置菜单加子菜单“自动刷新”：关 / 30 / 60 / 120 / 360 分钟，命令 id 40-44，单选勾选，复用 TR_CC_OFF；新串 `TR_M_AUTOREFRESH`、`TR_AR_MIN_FMT`（“%d 分钟”/“%d min”）、`TR_AR_SET_FMT`（“自动刷新：%ls”/“Auto-refresh: %ls”）。
- 定时器 ID 6：`ArmAutoRefreshTimer()`（KillTimer 6 后按需 SetTimer 周期 min*60000）；在 wWinMain `ReloadFeeds(0)` 后首次武装；菜单变更后重武装 + SetStatusTemp 反馈。
- `WM_TIMER` case 6：播放中/下载中/批量缓存中/加载中跳过（`g_playState!=0 || g_downloading || g_batchActive || g_feedsLoading` 直接 return，下个周期自然重试），否则 `ReloadFeeds(1)`。周期定时器无需重新武装。
- WM_DESTROY 加 KillTimer(hwnd, 6)。

## 功能 4：JSON Feed（jsonfeed.org）解析

- `ParseFeed` 开头探测：跳过空白后首字符为 `{` → 走新 `ParseJsonFeed`，否则原 XML 路径（调用方 LoadOneFeed 不动；缓存文件扩展名 .xml 与内容无关，无需改）。
- 最小 JSON 助手（无第三方库，与现有 XmlText 风格一致）：
  - `JsonUnescape(const char *s, const char *end)` → malloc UTF-8：处理 `\" \\ \/ \b \f \n \r \t \uXXXX`（含代理对）。
  - `JsonFindKey(begin, end, "key")` → 值起始指针（匹配 `"key"` 后跳过空白与 `:`）。
  - `JsonSkipValue(p)` → 值结束指针：串内状态机，正确处理字符串转义、对象/数组嵌套深度、数字/true/false/null。
  - `JsonObjRange`：找 `{` 后配对的 `}`。
- 频道映射：title→title；description→desc（走 U8ToWCrlf，与 XML desc 一致）；home_page_url→link；icon 或 favicon→imageUrl（char* UTF-8，无需 CleanXmlText）；language→lang；authors[0].name（1.1）或 author.name（1.0）→author；copyright 无对应字段留空。title 缺失回退 TR_UNTITLED、desc 缺失回退空串（与 XML 一致）。
- 单集映射（items 数组逐个对象范围切出）：title→e->title；content_html 或 content_text→e->desc（crlf）；attachments 中优先 mime_type 含 "audio" 的 url，否则第一个 attachment 的 url，无 attachment 跳过该集（与 XML 无 enclosure 跳过一致）；date_published→ParseDate；duration_in_seconds→durationSec（JSON number）；authors/author→e->author；origIdx 顺序递增；上限 10000 与 XML 一致。
- 字符串值统一 JsonUnescape 后按需 U8ToW/U8ToWCrlf。

## 新增多语言串（枚举追加在 TR_CLIP_OK/TR_CLIP_FAIL 之后、TR_N 之前；双语表同步追加尾部）

| 枚举 | zh | en |
|---|---|---|
| TR_M_AUTOREFRESH | 自动刷新 | Auto-Refresh |
| TR_AR_MIN_FMT | %d 分钟 | %d min |
| TR_AR_SET_FMT | 自动刷新：%ls | Auto-refresh: %ls |
| TR_BALLOON_NEW_FMT | 发现 %d 集新剧集 | Found %d new episode(s) |

## 验证步骤

1. `.\mingw32\bin\windres.exe resource.rc -O coff -o resource32.o`
2. `.\mingw32\bin\gcc.exe -Os -s -mwindows -municode -std=gnu17 -Wall -Wextra -D_WIN32_WINNT=0x0601 -Wl,--subsystem,windows:6.01 -o LiteTune.exe main.c resource32.o -lwinhttp -lwinmm -lole32 -luuid -lshlwapi -lcomctl32 -lshell32 -lpropsys -lws2_32 -liphlpapi -lgdiplus -ladvapi32 -lcomdlg32 -lmf -lmfplat -lmfplay -lmfreadwrite` → 必须零警告。
3. 自审 diff（不改无关代码）。
4. 交给用户晚间测试，附测试清单；不提交 git。

## 用户测试清单（交付时附上）

- 刷新后列表出现红色新剧集角标；点进该播客角标消失；重启后未点的播客角标仍在。
- 窗口最小化到托盘时自动/手动刷新发现新剧集 → 托盘气泡；窗口可见时不弹。
- 设置→自动刷新：切换间隔、关，状态栏有反馈；重启后设置保留。
- 添加一个 JSON Feed（如 https://www.jsonfeed.org/feed.json 或实际播客 JSON Feed）能正常解析出剧集并可播放。
- 新添加的 RSS 订阅不出现全量角标。
