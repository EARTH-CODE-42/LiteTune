# Spec：第三阶段界面与交互打磨（用户 13 条建议）

约束：纯 C / Win32，单文件 `main.c`；风格简洁、奥卡姆剃刀、中文注释；每批编译零警告；不自行截图测试；不提交 git。
第 13 条「以后重构」本次不做任何动作。

## 批次划分（每批结束编译 + 自审，再进下一批）

1. 快修：`#12` 日期/时长、`#10` tooltip 语言、`#6` 打开文件位置、`#7` 勾选符号、`#4` 消息框统一、`#3` 滚轮路由
2. 标题栏：`#2` 图标 + 高度 + tooltip + 关闭叉对称
3. 剧集面板：`#5` 过滤行移到边框外并清理兼容代码 + 手机遥控按钮；`#1` 批量收藏；`#8` 缓存按钮改“只看已缓存”+ 右键批量下载
4. 托盘浮窗：`#11` 单窗自绘 5 按钮、悬停高亮、圆角、右键播放/暂停、去掉托盘菜单
5. 配置：`#9` ini 分节（向后兼容）

## 关键现状（当前 main.c）

- `FillEpisodeList` L6318：插入项 `lvi.mask = LVIF_TEXT | LVIF_PARAM` 后复用同一 `lvi` 设子列 1/2。`LVIF_PARAM` 对 `iSubItem != 0` 非法，子列设置失败 → 日期/时长空白。
- `MessageBoxTh` L906：亮色直接 `MessageBoxW` 早退；暗色挂 `WH_CBT` 钩子 + `MsgBoxSubProc`（主题色函数已区分亮/暗）。
- `MenuDrawCheck` L9442：`LineTo` 不画终点像素 → 右臂不对称。`MenuDrawChevron` L9455 已用 `SetPixel` 逐点镜像。
- `CapGlyph` L10225：`CAP_CLOSE` 两条 `LineTo` 端点跨 -5..+6，终点不绘制导致上下不对称。`PaintCaption` L10263 无图标，标题从 `band.left + 12`。
- 主窗口 `WM_MOUSEWHEEL` L10929：简介补丁用 `lParam` 的屏幕坐标，KatMouse 可能传 (0,0) → 失效；滑块在子窗口 `SliderProc` L7975 自己处理，播客/剧集滚轮在 `PanelProc` L8216-8238 处理，三套逻辑不一致。
- 剧集面板布局 `LayoutControls` L8397-8410：过滤行在面板边框内（`g_rcPanel[1].top = by`）；L8444-8451 有把缓存/收藏按钮压到列表之上的 z-order 兼容代码；`g_listEp` 创建时带 `WS_CLIPSIBLINGS`。
- 剧集右键菜单 L11285-11350；单选收藏 cmd 4；`StartBatchCache` L4965 已支持多选；`EpisodeIsCached` L2918 内部用 `CachePathFor(e->url, path, MAX_PATH)`。
- 配置 `BuildConfigText` L4214：已有 `[settings]` 与 `[feeds]`，但 `resume/lastep/seenep/fav/podrefresh` 全塞在 `[settings]` 内；`LoadConfig` L4115。
- 托盘 `WM_TRAYICON` L12065：悬停弹浮窗、左键切换主窗口、右键弹“显示主窗口/退出”菜单。
- 浮窗 `FlyoutProc` L9755 + `CreateFlyout` L9815：4 个独立 `BUTTON` 子窗，父窗收不到 `WM_MOUSEMOVE`，悬停高亮失效；`WS_BORDER` 直角边框。
- i18n：`tr()` L450，zh 表 L452、en 表 L452 之后；语言切换命令 L9642；tooltip 批量刷新块在 L8997 附近，缺 `g_btnFavOnly` / `g_editFilter`。

## 批次 1：快修

### #12 剧集日期/时长
`FillEpisodeList`：`LVM_INSERTITEMW` 成功后、设置子列前加 `lvi.mask = LVIF_TEXT; lvi.lParam = 0;`。日期/时长恢复显示，缓存视图列序不变。

### #10 tooltip 语言同步
语言切换分支（`cmd == 20 || cmd == 21`）调用现有 tooltip 刷新块；补：
`UpdateTipText(g_btnFavOnly, tr(TR_FAV_ONLY_TIP))`、`UpdateTipText(g_editFilter, tr(TR_TIP_FILTER))`、`UpdateTipText(g_btnCache, tr(TR_TIP_CACHE))`、`UpdateTipText(g_btnPhone, tr(TR_TIP_PHONE))`。

### #6 打开文件位置
新增 `RevealInExplorer(const wchar_t *path)`：`SHParseDisplayName` 取绝对 PIDL → `SHOpenFolderAndSelectItems(pidl, 1, (PCUITEMID_CHILD_ARRAY)&pidl, 0)` → `CoTaskMemFree`。
剧集右键菜单：`cached` 为真时加 cmd 5 `TR_CTX_REVEAL`，路径用 `CachePathFor(e->url, ...)`；命令 5 调 `RevealInExplorer`。

### #7 勾选符号
`MenuDrawCheck` 去掉 `CreatePen/LineTo`，改 `SetPixel`：
左臂 `i=0..3: (cx-4+i, cy+i)`；右臂 `i=0..4: (cx+i, cy+2-i)`（顶点在 `(-1,+3)`，两臂逐像素相接，无终点丢失）。

### #4 消息框统一自绘
`MessageBoxTh` 删除 `if (!g_darkMode) return MessageBoxW(...)` 早退，亮/暗都走 `WH_CBT` + `MsgBoxSubProc`。`MsgBoxSubProc`/`MsgBoxBtnEnum` 已用 `ThPanel/ThText` 等主题色，亮色下自然得到同款自绘外观；`ApplyDarkTitlebar(h, g_darkMode)` 已有参数，无需改。

### #3 滚轮路由统一
主窗口 `WM_MOUSEWHEEL`（Ctrl 分支之后）：
1. `GetCursorPos(&sp)`；`under = WindowFromPoint(sp)`；若 `GetAncestor(under, GA_ROOT) != g_hwnd` 则不处理（交回默认）。
2. 若 `under == g_editDesc` 或 `IsChild(g_editDesc, under)` → `EM_LINESCROLL` ±3 + `SyncScrollBar(g_vsDesc)`。
3. 若 `under == g_sldSeek || under == g_sldVol` → 转发 `WM_MOUSEWHEEL` 给该滑块（复用 `SliderProc`，不依赖 lParam）。
4. 若 `under == g_listEp` 或其后代 → 转发给列表（`PanelProc`/默认处理），并 `return 0`。
5. 若 `under == g_listPod` → 转发给列表。
6. 其余保留原有逻辑：`lParam` 坐标有效时按旧补丁滚动简介，否则不处理。
这样 KatMouse 把滚轮消息送到主窗口时，按光标下真实控件分发，简介/进度/音量/列表行为一致。

## 批次 2：标题栏（#2）

- `TITLE_H` 34 → 30（`CAP_BTN_W` 保持 44）。
- `PaintCaption` 在 `band.left + 8` 用 `DrawIconEx(g_trayIco, x, cy-8, 16, 16)` 画程序图标；标题文字左边界改为 `band.left + 8 + 16 + 8`。
- 新增 `g_hwndCapTip`：`TOOLTIPS_CLASSW` + `TTF_TRACK|TTF_ABSOLUTE` 的追踪 tooltip，`uId=1`、`hwnd=g_hwnd`；`WM_CREATE` 创建。
- 标题栏悬停（`WM_MOUSEMOVE` 且 `y < 0`）算出 `bi` 后：`bi >= 0` → `TTM_UPDATETIPTEXTW` + `TTM_TRACKPOSITION`（按钮中心上方）+ `TTM_TRACKACTIVATE TRUE`；`bi < 0` 或 `WM_MOUSELEAVE`/`WM_NCMOUSEMOVE` → 关闭。
- 文案：`CapTipText(bi)` 返回 `TR_CAP_MIN / TR_CAP_TRAY / TR_CAP_MAX 或 TR_CAP_RESTORE（IsZoomed）/ TR_CAP_CLOSE`。
- `CAP_CLOSE` 叉改用 `SetPixel` 双对角，`i=0..10: (cx-5+i, cy-5+i)` 与 `(cx+5-i, cy-5+i)`，上下左右严格对称。
- 新增 5 条 i18n（见下表）。

## 批次 3：剧集面板（#5 / #1 / #8）

### #5 布局与清理
- 过滤行移到面板边框外，与播客工具栏同高同基线：`g_btnCache`/`g_btnFavOnly`/`g_editFilter` 的 y 从 `by+2` 改为 `by`；`g_rcPanel[1].top = by + 28`；`g_listEp`/`g_vsEp` 的 y 从 `by+28` 改为 `by+30`（与播客列表一致）；水平滚条仍贴面板底部。
- 右对齐：缓存按钮右缘 = 剧集面板右缘 - 2；收藏按钮其左 27px；过滤框从面板左缘 +2 到收藏按钮左缘 -6，宽自适应。
- 删除 L8444-8451 的 z-order 兼容块；删除 `g_listEp` 创建处的 `WS_CLIPSIBLINGS`（grep 确认无其它依赖）。
- 手机遥控提为播客工具栏第 5 个按钮：新增 `IDC_BTN_PHONE` / `g_btnPhone`，位置 `x + 108`，24x24；新增 `ICO_PHONE` 图形；按下态 = `g_httpThread`。
- 把 `OnSettingsButton` 里 `cmd == 9` 的现有逻辑提取为 `TogglePhoneServer()`；按钮点击与设置菜单都调用它（菜单项 9 保留或移除均可——按“低频功能提为按钮”的要求，移除菜单项 9，避免重复入口）。
- 其余按钮保持现状：导入/导出已是子菜单，自动清理/自动刷新/语言/配置模式均属低频设置项，不另提按钮。

### #8 缓存按钮语义
- 新增 `g_cacheOnly`（默认 0）。`EpMatchesFilter`：收藏判断后加 `if (g_cacheOnly && !g_viewCached && !EpisodeIsCached(e)) return 0;`。
- `IDC_BTN_CACHE` 点击改为切换 `g_cacheOnly` + `InvalidateRect(g_btnCache)` + `FillEpisodeList()` + `SyncScrollBar(g_vsEp)`；不再调用 `StartBatchCache`。
- `IconKindOf`：`IDC_BTN_CACHE` 设 `*on = g_cacheOnly`。
- `TR_TIP_CACHE` 文案改为“只显示已缓存（右键多选可批量下载）” / “Show cached only (right-click to batch download)”。
- 批量下载入口移到多选右键菜单（见 #1 的 cmd 6）。

### #1 批量收藏 / 批量操作
`WM_CONTEXTMENU` 的 `src == g_listEp`：
- 先 `LVM_GETSELECTEDCOUNT`；命中行（`EpIdxOfRow`）在被选中且 `selCount > 1` 时进入多选分支。
- 多选分支菜单：
  - 若存在未收藏项 → cmd 7，文案 `TR_CTX_FAV_N_FMT`（%d = 未收藏项数）
  - 若存在已收藏项 → cmd 8，文案 `TR_CTX_UNFAV_N_FMT`（%d = 已收藏项数）
  - 若收藏视图外且有未缓存项 → cmd 6，文案 `TR_CTX_DL_N_FMT`；`StartBatchCache()` 复用现有多选收集逻辑。
  - 其余（复制链接/删除缓存）按原逻辑，但删除缓存只对命中行；打开文件位置仅命中行已缓存时显示。
- 单选分支保持现有 cmd 1/2/3/4 + 新 cmd 5。
- 执行 7/8：先把选中行映射成剧集下标快照（≤256），再逐项 `FavSet(fh,url,1/0)`；状态提示 `TR_FAV_BATCH_ON_FMT` / `TR_FAV_BATCH_OFF_FMT`；`g_favOnly` 时 `FillEpisodeList()`，否则对每行刷新第 0 列。
- 新增 i18n：`TR_CTX_REVEAL`、`TR_CTX_BATCH_DL`（用格式串）、`TR_CTX_FAV_N_FMT`、`TR_CTX_UNFAV_N_FMT`、`TR_CTX_DL_N_FMT`、`TR_FAV_BATCH_ON_FMT`、`TR_FAV_BATCH_OFF_FMT`。

## 批次 4：托盘浮窗与托盘右键（#11）

- 浮窗改为单窗口自绘（移除 4 个 `BUTTON` 子窗与 `WM_DRAWITEM`/`WM_COMMAND` 转发）：
  - `FLY_BTN_N = 5`；`FLY_W = FLY_PAD*2 + FLY_BTN*5 + FLY_GAP*4`；按钮索引 0 上一曲 / 1 播放暂停 / 2 下一曲 / 3 随机 / 4 退出。
  - `FlyoutProc` 处理 `WM_PAINT`：`RoundRect` 圆角背景 + 逐按钮悬停底色 + `DrawGlyphC`（0..3 原图标，4 用 `ICO_CROSS`，颜色 `RGB(255,120,120)`）。
  - `WM_MOUSEMOVE`：按 x 命中按钮 → `g_flyHover`，变化才 `InvalidateRect`；`TrackMouseEvent(TME_LEAVE)`；`WM_MOUSELEAVE` 清空。
  - `WM_LBUTTONUP`：命中按钮即执行：0 `PlayAdjacent(-1)`、1 `PauseResume()`、2 `PlayAdjacent(+1)`、3 随机逻辑（与 `THBN_CLICKED tid==3` 相同）、4 `PostMessage(g_hwnd, WM_CLOSE, 0, 0)`；随后 `SW_HIDE`。
  - `CreateFlyout`：去掉 `WS_BORDER`，`SetWindowRgn(CreateRoundRectRgn(0,0,FLY_W+1,FLY_H+1,10,10))`；保留 `TOOLWINDOW|TOPMOST|NOACTIVATE`。
- 托盘右键：`WM_RBUTTONUP` 直接 `PauseResume()`，删除菜单及 id 1/2 分支；`TR_TRAY_SHOW`、`TR_TRAY_EXIT` 若无其它引用则从枚举与中英表删除。
- 主窗口 `WM_TIMER` case 7（光标离开轮询）：仅当光标既不在托盘图标矩形、也不在浮窗矩形内时 `SW_HIDE`；浮窗已显示时不要重复 `ShowFlyout`。

## 批次 5：ini 分节（#9）

`BuildConfigText` 重排输出（解析按行键名，节行忽略，向后兼容旧文件）：
```
; LiteTune config
[settings]
sort / w1 / w2 / deschide / cw0-2 / vol / font / dark / lang / speed /
winw / winh / winx / winy / cachedir / cachedays / autorefresh / traysound

[lastplayed]
resume / resumeplay / resumeurl / lastep=

[favorites]
fav=

[seen]
seenep=

[podrefresh]
podrefresh=

[feeds]
(每行一个 URL)
```
`ParseConfigLine` 开头忽略空行、`;` 注释、`[section]` 行（若尚未忽略则补上），键名匹配逻辑与顺序不变。

## 新增/修改 i18n

新增（枚举与 zh/en 表尾部同步）：
| 枚举 | zh | en |
|---|---|---|
| TR_CAP_MIN | 最小化到任务栏 | Minimize |
| TR_CAP_TRAY | 最小化到托盘 | Minimize to tray |
| TR_CAP_MAX | 最大化 | Maximize |
| TR_CAP_RESTORE | 向下还原 | Restore |
| TR_CAP_CLOSE | 关闭 | Close |
| TR_TIP_PHONE | 手机播放/遥控 | Phone play/remote |
| TR_CTX_REVEAL | 打开文件位置 | Show in folder |
| TR_CTX_FAV_N_FMT | 收藏选中的 %d 集 | Favorite %d selected |
| TR_CTX_UNFAV_N_FMT | 取消收藏选中的 %d 集 | Unfavorite %d selected |
| TR_CTX_DL_N_FMT | 下载选中的 %d 集 | Download %d selected |
| TR_FAV_BATCH_ON_FMT | 已收藏 %d 集 | Favorited %d episode(s) |
| TR_FAV_BATCH_OFF_FMT | 已取消收藏 %d 集 | Unfavorited %d episode(s) |

修改：`TR_TIP_CACHE` 改为“只显示已缓存（右键多选可批量下载）” / “Show cached only (right-click to batch download)”。
删除（确认无引用后）：`TR_TRAY_SHOW`、`TR_TRAY_EXIT`（枚举 + zh/en 同步删除，保持顺序）。

## 验证

每批结束用参数数组编译（PowerShell）：
```
$gcc = ".\mingw32\bin\gcc.exe"
$a = @('-Os','-s','-mwindows','-municode','-std=gnu17','-Wall','-Wextra',
       '-D_WIN32_WINNT=0x0601','-Wl,--subsystem,windows:6.01',
       '-o','LiteTune.exe','main.c','resource32.o','-lwinhttp','-lwinmm','-lole32',
       '-luuid','-lshlwapi','-lcomctl32','-lshell32','-lpropsys','-lws2_32',
       '-liphlpapi','-lgdiplus','-ladvapi32','-lcomdlg32','-lmf','-lmfplat',
       '-lmfplay','-lmfreadwrite')
& $gcc @a
```
要求 EXIT=0 且零警告。`windres resource.rc -O coff -o resource32.o` 先执行。
交付时列用户实机测试点：日期/时长、菜单勾、亮色消息框、KatMouse 下简介/滑块/滚轮、标题栏图标/tooltip/高度、过滤行位置、批量收藏、缓存过滤、右键批量下载、打开文件位置、托盘浮窗 5 按钮/悬停/圆角/右键暂停、ini 分节、语言切换 tooltip。

## 不做（本次）

- #13 代码重构：不动作。
