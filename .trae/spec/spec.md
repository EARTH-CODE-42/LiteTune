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

---

# 第二阶段 Spec：剩余四项功能（项 4 / 7 / 8 / 9）

用户指令：“简介滚鼠标还是滚不动，目前发现了其它问题，后面再统一改，你先把剩余的先实现了吧”。
因此 **简介区滚轮 bug 不在本阶段范围**，本阶段只实现：托盘图标新剧集角标、剧集收藏、剧集即时过滤框、每播客独立刷新间隔。
约束不变：纯 C/Win32、单文件、零警告（`-Wall -Wextra`）、奥卡姆剃刀、中文注释、不改无关代码、不提交 git、不自行截图。

## 关键事实（当前 main.c 行号）

- 结构：`Episode` L100 前；`Podcast` L121-144（`newCount` L141）；配置 `LoadConfig` L3783、`ParseConfigLine` L3529、`BuildConfigText` L3880、`SaveConfig` L3964。
- 每播客记录存储范式：`SeenEp` + `SeenFind/Set/Drop/PurgeOrphans/FreeAll` L3086-3145；配置键 `seenep=` 解析 L3584、写出 L3934。`LastEpHash(char*)` 复用为播客 ID。
- 剧集列表：ListView `g_listEp` 创建 L10031；列头 `SetEpisodeColumns` L5866；填充 `FillEpisodeList` L5906（**当前 list 行号 == eps 下标**）；tooltip `BuildEpisodeTipText` L5941（调用处 L7699 传行号）。
- 所有“行号当剧集下标”的读取点（**过滤后必须全部改走映射**）：
  - `StartBatchCache` L4624-4631（多选行）
  - `ChangeSort` L6121-6135
  - `HighlightEpisodeRow` L6139-6150
  - 剧集右键 `LVM_HITTEST` L10760
  - `WM_NOTIFY` LVN_ITEMCHANGED L10874、NM_DBLCLK L10878、NM_CUSTOMDRAW L10903
  - `WM_APP_EP_READY` 更新下载状态 `li.iItem = epIdx` L11333
- 布局：`LayoutControls` 剧集面板 L7963-7967（`g_btnCache` 右上角 L7960-7961）；简介 `g_editDesc` 布局 L7976。
- 控制 ID 枚举 L158-165；控件创建 L9983-10067；`ApplyFontsToControls()` L10069；子类化 L10075-10092。
- `WM_DRAWITEM`（按钮/播客行）L10194；`WM_COMMAND` L10796（`LBN_SELCHANGE` L10815-10827）；`WM_NOTIFY` L10855。
- 托盘：创建 L10105-10119（`g_trayIco`、`g_trayAdded`、uID=1）；退出销毁 L11537-11544。
- 定时器：`WM_TIMER` L10983（ID6 自动刷新 L11015-11021）；`ArmAutoRefreshTimer` L9058；设置菜单 L9066。
- 自动刷新当前实现：全局周期到点 `ReloadFeeds(1)`（全量、会停播清列表）。
- 多语言：枚举 L349-412（`TR_N` 最后）；zh 表 L416 起、en 表 L514 起，**顺序必须与枚举一致**。
- 单播客刷新队列：`PumpRefreshQueue` L6539、`RefreshSinglePodcast` L6557（原位替换、串行）。

## A. 剧集行映射（项 7/8 前置，必须先做）

1. 新增 `static int *g_epMap; static int g_epMapN, g_epMapCap;`
   - `static int EpIdxOfRow(int row)`：`row>=0 && row<g_epMapN ? g_epMap[row] : -1`
   - `static int RowOfEpIdx(int ep)`：线性扫描 `g_epMap` 返回行号，找不到 -1
   - `static void EpMapPush(int ep)`：按 `g_epMapCap` 2 倍扩容（初始 128）。
2. `FillEpisodeList` 重写：`g_epMapN=0`；逐集判断 `EpMatchesFilter()`，命中才插入；`lvi.mask = LVIF_TEXT | LVIF_PARAM`、`lvi.lParam = i`；插入后 `EpMapPush(i)`。
3. 上述所有读取点改为先 `EpIdxOfRow(...)` 再访问 `eps`；`HighlightEpisodeRow` / `EP_READY` 改为 `RowOfEpIdx(ep)`，返回 -1 则跳过。`ChangeSort` 重填后按 `RowOfEpIdx(i)` 恢复选中。`BuildEpisodeTipText` 入口自行转换行号。`StartBatchCache` 用 `LVM_GETITEMCOUNT` 作为行循环上界。
4. 无过滤时映射为恒等（行 i ↔ 集 i），行为与现状一致。

## B. 项 8：剧集即时过滤框 + 已收藏筛选

1. 全局：`static wchar_t g_epFilter[128]; static int g_favOnly;`
   - `static int EpMatchesFilter(const Podcast *p, int ep)`：`g_favOnly` 时要求非缓存视图且该集已收藏；`g_epFilter[0]` 时要求标题含子串（`StrStrIW(title, g_epFilter)`，大小写不敏感，shlwapi 已链接）。
2. 控件（ID 枚举加 `IDC_EDIT_FILTER`、`IDC_BTN_FAVONLY`，置于 `IDC_TAB_DESC` 前）：
   - `g_editFilter`：`EDIT`，`WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL`，单行；创建于 `g_editDesc` 之后。
   - `g_btnFavOnly`：`BUTTON`，`WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW`，创建于 12 按钮循环之后。
   - `ApplyFontsToControls` 里设置 `g_font`（过滤框）与 `g_fontSmall`（按钮）；`AddButtonTip(g_editFilter, tr(TR_TIP_FILTER))`、`AddButtonTip(g_btnFavOnly, tr(TR_FAV_ONLY_TIP))`。
3. 布局（`LayoutControls` 剧集面板，行高 24，位于面板顶部 `by+2`）：
   ```
   g_btnCache   x + w2 - 28 - DSB_W,          by+2, 24, 24
   g_btnFavOnly x + w2 - 28 - DSB_W - 27,     by+2, 24, 24
   g_editFilter x+2,                          by+3, w2 - sbw - 4 - 54 - 2, 22
   g_listEp     x+2,                          by+28, w2 - sbw - 4, bh - sbh - 30
   g_vsEp       x + w2 - 2 - sbw,             by+28, sbw, bh - sbh - 30
   g_hsEp       x+2,                          by + bh - 2 - sbh, w2 - sbw - 4, sbh
   ```
   `g_rcPanel[1]` 仍为整面板，随窗口重排。
4. 消息：
   - `WM_COMMAND`：`IDC_EDIT_FILTER` + `EN_CHANGE` → `GetWindowTextW(g_editFilter, g_epFilter, 128)` → `FillEpisodeList()` + `SyncScrollBar(g_vsEp)`；
     `IDC_BTN_FAVONLY` + `BN_CLICKED` → 翻转 `g_favOnly`、`InvalidateRect(g_btnFavOnly)`、`FillEpisodeList()` + `SyncScrollBar(g_vsEp)`。
   - `WM_DRAWITEM`（`ODT_BUTTON`）加 `IDC_BTN_FAVONLY` 分支：画 `ThWinBg()` 底 + `DrawEdge`（按下 `EDGE_SUNKEN` 否则 `EDGE_RAISED`）+ 五角星（`Polygon`，开启时填充琥珀色 `RGB(255,190,60)`，关闭时 `ThDim()` 描边）。
   - `ReloadFeeds` 开头清空过滤状态：`g_epFilter[0]=0; g_favOnly=0; SetWindowTextW(g_editFilter, L"")`（此时控件可能为空，判空）。

## C. 项 7：剧集收藏

1. 存储（URL 标识，跨排序/刷新稳定）：
   ```c
   typedef struct { unsigned int hash; wchar_t *epUrl; } FavEp;
   static FavEp *g_favs; static int g_favN, g_favCap;   // 上限 512
   ```
   - `FavHas(unsigned int hash, const wchar_t *url)`；`FavSet(hash,url,int on)`；`FavDrop(hash)`（释放该播客全部）；`FavPurgeOrphans()`（hash 不在现有 feeds 则删）；`FavFreeAll()`。
   - 全部仿 `LastEp*` 写法。
2. 配置：
   - `BuildConfigText` 写 `fav=<hash hex>,<utf8url>`（**只在第一个逗号切分**，URL 可含逗号）；在 `seenep` 段之后。
   - `ParseConfigLine` 解析 `fav`：`strchr(val, ',')`，前半 hex → hash，后半整段 `U8ToW`。
   - `LoadConfig` 默认值区调 `FavFreeAll()`；`RemovePodcast` 里 `FavDrop(LastEpHash(feedUrl))`；`WM_APP_FEEDS_DONE` 里 `FavPurgeOrphans()`。
3. 列表星标：
   - `static const wchar_t* EpListTitle(int ep)` 用静态缓冲 2048：非缓存视图且已收藏 → `★ %ls`，否则原标题；`FillEpisodeList` 用它填第 0 列（缓存视图列 0 是播客名，不加星）。
   - 切换收藏后只更新该行第 0 列（`LVM_SETITEMW`），若 `g_favOnly` 开启则整表 `FillEpisodeList()`。
4. 入口：剧集右键菜单（L10771-10775）在复制链接之上加：
   - `g_curPod < g_podCount`（订阅视图）才加；文案 `FavHas ? TR_CTX_UNFAV : TR_CTX_FAV`，`MF_CHECKED` 表示已收藏；命令 id 4；
   - `cmd==4` → `FavSet(hash, e->url, !FavHas(...))` + 更新行 + `SetStatusTemp(TR_FAV_ON / TR_FAV_OFF)`。
   - 收藏只作用于订阅视图；缓存聚合视图不提供。

## D. 项 9：每播客独立刷新间隔

1. 结构：`Podcast` 加 `int refreshMin;`（-1=跟随全局，0=关闭，>0=分钟）与 `int lastRefreshMin;`（分钟级时间戳，瞬时）。
2. 持久化列表：
   ```c
   typedef struct { unsigned int hash; int min; } PodRefresh;
   static PodRefresh *g_podRefs; static int g_podRefN, g_podRefCap;  // 上限 512
   ```
   - `PodRefreshFind(hash)` 返回分钟，缺省 -1；`PodRefreshSet(hash,min)`（min<0 删除，否则写入）；`PodRefreshDrop`、`PodRefreshPurgeOrphans`、`PodRefreshFreeAll`、`PodRefreshAnyPositive`。
   - 配置键 `podrefresh=<hash hex>,<min>`（只写 >=0 的），解析范围 -1..1440；`LoadConfig` 重置、`RemovePodcast` 删除、`FEEDS_DONE` 清理孤儿。
3. 应用：`static void PodApplyRefresh(Podcast *p)` = 按 `feedUrl` 查表赋 `refreshMin`、`lastRefreshMin = time(NULL)/60`；在 `WM_APP_FEED_ADDED` 的三条落地路径（全量插入 / 新增 / 原位替换）以及失败占位条目上调用。
4. 调度改造：
   - `ArmAutoRefreshTimer`：`KillTimer(6)`；若 `g_autoRefreshMin > 0 || PodRefreshAnyPositive()` 则 `SetTimer(hwnd, 6, 60000, NULL)`（改为每分钟心跳）。
   - `WM_TIMER` ID6 重写：忙（播放/下载/批量/加载）则 skip；`int nowMin = time(NULL)/60`；遍历 pods，算 `eff = refreshMin==0 ? 0 : refreshMin>0 ? refreshMin : g_autoRefreshMin`；`eff>0 && nowMin - lastRefreshMin >= eff` → `lastRefreshMin=nowMin`，静默入队刷新。
   - 队列重构：抽出 `static int EnqueueRefreshUrl(const char *url)`（去重、入队、首条立即 `PumpRefreshQueue()`；不设状态栏），`RefreshSinglePodcast` 调它后按原样 `SetStatusTemp`；定时器直接调 `EnqueueRefreshUrl`，避免每分钟刷状态栏。
   - 启动时 `ArmAutoRefreshTimer()` 调用点不变（`ReloadFeeds(0)` 之后）。
   - 说明：自动刷新由“全量 `ReloadFeeds(1)`”改为“按播客原位刷新”，不再停播/清空列表；窗口隐藏时的新剧集提醒由托盘角标（项 4）承担，原全量刷新气泡逻辑保留不动。
5. 右键菜单（`g_listPod` 右键，L10713-10724）：在“复制 Feed URL”前加子菜单 `TR_M_REFRESH_INT`，命令 id 20-26：
   - 20 跟随全局（`refreshMin==-1` 勾选）、21 关闭（`==0` 勾选）、22..26 = 15/30/60/120/360 分钟（对应值勾选），文案复用 `TR_AR_MIN_FMT`。
   - 处理：`PodRefreshSet(hash, v)` + 写回 `g_pods[item].refreshMin` + `ArmAutoRefreshTimer()` + `SetStatusTemp(TR_AR_SET_FMT 或 TR_CC_OFF 或 TR_RI_FOLLOW)`。

## E. 项 4：托盘图标新剧集角标

1. `static HICON g_trayBadgeIco;`
2. `static HICON MakeBadgeIcon(int total)`：
   - 尺寸 `SM_CXSMICON/SM_CYSMICON`；`CreateDIBSection` 32bpp top-down，bits 清零；`DrawIconEx(hdc,0,0,g_trayIco,cx,cy,0,NULL,DI_NORMAL)`。
   - 右下角红色圆 `RGB(214,48,49)`（直径约 0.75×边），白字 `total>9 ? L"9+" : "%d"`，字体 `-MulDiv(cy,6,10)` 加粗、`DT_CENTER|DT_VCENTER|DT_SINGLELINE`。
   - 圆内象素 alpha 置 255，圆外置 0（保证圆角透明）。
   - `CreateIconIndirect`：`fIcon=TRUE`、`hbmColor=hbm`、`hbmMask=CreateBitmap(cx,cy,1,1,NULL)`（32bpp alpha 下掩码被忽略）。
   - 清理 GDI 对象后返回。
3. `static void UpdateTrayBadge(void)`：
   - `if (!g_trayAdded) return;` 统计所有播客 `newCount` 之和；
   - `total>0`：新图标 → `NIM_MODIFY`（`NIF_ICON|NIF_TIP`，tip = `TR_TRAY_NEW_FMT` 格式化；成功后销毁旧 badge 图标并保存新图标）；
   - `total==0` 且当前有 badge：改回 `g_trayIco`、tip 恢复 `L"LiteTune"`、销毁 badge。
4. 调用点：托盘 `NIM_ADD` 之后、`WM_APP_FEED_ADDED` 各路径末尾、`WM_APP_FEEDS_DONE` 计数更新后、`LBN_SELCHANGE` 清角标后、`RemovePodcast` 末尾。
5. `WM_DESTROY` 销毁 `g_trayBadgeIco`（在删托盘图标之后）。

## 新增多语言串（追加在 `TR_BALLOON_NEW_FMT` 之后、`TR_N` 之前，zh/en 表尾部同步）

| 枚举 | zh | en |
|---|---|---|
| TR_TIP_FILTER | 按标题过滤剧集 | Filter episodes by title |
| TR_FAV_ONLY_TIP | 只显示已收藏 | Show favorites only |
| TR_CTX_FAV | 收藏 | Add to favorites |
| TR_CTX_UNFAV | 取消收藏 | Remove from favorites |
| TR_FAV_ON | 已收藏 | Added to favorites |
| TR_FAV_OFF | 已取消收藏 | Removed from favorites |
| TR_M_REFRESH_INT | 刷新间隔 | Refresh interval |
| TR_RI_FOLLOW | 跟随全局设置 | Use global setting |
| TR_TRAY_NEW_FMT | LiteTune - %d 集新剧集 | LiteTune - %d new episode(s) |

## 验证步骤

1. `.\mingw32\bin\windres.exe resource.rc -O coff -o resource32.o`
2. `.\mingw32\bin\gcc.exe -Os -s -mwindows -municode -std=gnu17 -Wall -Wextra -D_WIN32_WINNT=0x0601 -Wl,--subsystem,windows:6.01 -o LiteTune.exe main.c resource32.o -lwinhttp -lwinmm -lole32 -luuid -lshlwapi -lcomctl32 -lshell32 -lpropsys -lws2_32 -liphlpapi -lgdiplus -ladvapi32 -lcomdlg32 -lmf -lmfplat -lmfplay -lmfreadwrite` → 必须零警告。
3. 每次改动后重编；全部完成后自审 diff（不改无关代码），交用户统一测试，不自行截图、不提交 git。

## 用户测试清单（第二阶段交付时附上）

- 托盘图标在有多集新剧集时显示红色数字角标（最多 9+）；点开播客后角标递减，全部已看后消失；重启仍正确。
- 剧集右键“收藏/取消收藏”生效，标题前出现 ★；重启后收藏保留；删除播客后该播客收藏被清除。
- 剧集面板顶部过滤框输入文字即时过滤，清空恢复；已收藏筛选按钮只显示收藏剧集；两条件可叠加。
- 搜索/多选批量缓存/排序/双击播放/续播/下载完成更新在过滤状态下均对应正确的剧集。
- 播客右键“刷新间隔”可设跟随全局/关闭/15/30/60/120/360 分钟；重启保留；到点仅刷新该播客。
- 设置里的全局自动刷新仍可用；自动刷新不再打断正在播放的音频。
