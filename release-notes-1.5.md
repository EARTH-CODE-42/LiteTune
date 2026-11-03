# LiteTune 1.5.0

> **极简、绿色便携的 RSS 播客播放器** —— 纯 C + Win32，单文件、约 292 KB，Win7–Win11 32/64 位通用。

## 下载

- [LiteTune.exe](https://github.com/EARTH-CODE-42/win32-podcast/releases/download/v1.5.0/LiteTune.exe)（约 292 KB，绿色单文件，Win7–Win11 32/64 位通用）
- 使用说明见 [README.md](https://github.com/EARTH-CODE-42/win32-podcast/blob/main/README.md)

---

## 主要改动

### 托盘浮窗（全新）
- 托盘图标悬停 300ms 弹出迷你控制条（上一曲 / 播放暂停 / 下一曲 / 随机下一曲 / 退出）
- 鼠标移开自动收起；每个按钮有 tooltip
- 主窗口最小化后消失，托盘图标仍在；双击托盘图标恢复主窗口

### 标题栏自绘
- 左侧显示程序图标 + 标题，紧凑美观
- 按钮悬停 tooltip；关闭叉不对称（上右下左）

### 弹窗菜单 & 消息框
- 所有右键菜单（播客/剧集/工具栏）改为自绘，亮暗主题共用一套绘制代码，行高、勾选、子菜单箭头一致
- 消息框（关于、配置切换确认等）同样自绘，配色、字体跟随主题

### 剧集面板
- 工具栏重构：加/删/设置 移到播客列表上方；收藏/缓存 开关移到剧集列表上方
- 刷新按钮移入设置菜单，腾出工具栏空间
- 手机遥控按钮移到设置按钮右侧（不再重叠）

### 收藏 & 缓存
- 收藏剧集可批量（多选 → 右键）：收藏/取消收藏/批量下载
- 已缓存剧集统一聚合到"已缓存"虚拟播客（列表最后一行），支持断网浏览、播放、导出
- 剧集右键"打开文件位置"：正确打开文件夹并选中文件（修复之前误用外部播放器的 bug）
- 收藏/缓存开关按钮统一视觉：关 = 灰色镂空图标 + 凸起边，开 = 主色实心图标 + 凹陷边
- 收藏星保持黄色（不改），缓存箭头为蓝色（与主色调一致）

### 配置存储
- 原单一 `feeds.ini` 升级为分节格式：`[settings] / [lastplayed] / [favorites] / [seen] / [podrefresh] / [feeds]`，可读性更好、未来可扩展

### UI 细节
- 搜索框外增加 1px 细边框
- 剧集日期/时长显示（修复之前回归丢失的 bug）
- 标题栏最小化按钮 → 托盘（而非任务栏）
- 任务栏缩略图按钮：上一曲 / 播放暂停 / 下一曲
- 手机遥控网页：自动打开本机浏览器，移动端通过局域网访问

### 体积优化
- 编译新增 `-fno-unwind-tables -fno-asynchronous-unwind-tables`，去掉 GCC 生成的 C++ 异常栈展开表（`.eh_fram` 节从 17.8 KB 砍到 8 KB）
- 301 KB → **292 KB**（零功能损失，纯体积优化）

---

## 兼容

- Windows 7 SP1 / 8 / 8.1 / 10 / 11，32 位与 64 位均可运行
- 首次启动自动在当前目录创建 `feeds.ini`；可选切换为配置内嵌到 exe 尾部（完全单文件）
- 依赖系统自带的 msvcrt / Media Foundation / WinHTTP，**不引入任何第三方 DLL**

---

## What's New in 1.5.0

### System Tray Flyout
- Hover the tray icon for 300 ms → a 5-button compact control bar (Prev / PlayPause / Next / RandomNext / Quit) pops up below
- Auto-dismiss when the mouse leaves; each button shows a tooltip
- Main window minimizes to tray, not taskbar

### Custom Title Bar
- App icon + title on the left; compact 30px height
- Hover tooltips for each button; asymmetric close glyph

### Custom Popup Menus & Message Boxes
- All context menus (feed / episode / toolbar) are now owner-drawn. Light and dark themes share one render path — consistent row height, checkmarks, and submenu chevrons
- Message boxes (About, config-switch confirmations, etc.) use the same drawing style

### Episode Panel Layout
- Add / Delete / Settings buttons moved above the feed list; Favorites / Cache filters moved above the episode list
- Refresh button moved into the Settings menu (infrequent action)
- Phone remote button sits to the right of the Settings button

### Favorites & Cache
- Multi-select episodes → right-click → batch favorites / un-favorites / batch download
- All cached episodes aggregated into a "Cached" virtual feed (last row in the feed list). Works offline — browse, play, export
- Episode right-click "Open file location" now correctly opens the folder and selects the file (previously launched the file in an external player)
- Favorite / cache toggle buttons unified: off = gray outline + raised edge; on = solid color + sunken edge
- Favorite star keeps its original yellow (unchanged); cache arrow uses the accent blue

### Config Format
- The single flat `feeds.ini` is now sectioned: `[settings] / [lastplayed] / [favorites] / [seen] / [podrefresh] / [feeds]` — more readable, ready for future extensions

### UI Polish
- 1px frame around the search / filter edit box
- Episode date and duration columns restored (regression fix)
- Title bar minimize → tray (not taskbar)
- Taskbar thumbnail toolbar: Prev / PlayPause / Next
- Phone remote web page: opens automatically on the machine; mobile devices access it over LAN

### Size Optimization
- Added `-fno-unwind-tables -fno-asynchronous-unwind-tables` to the build, stripping GCC's C++ exception unwinding tables (`.eh_fram` section from 17.8 KB down to 8 KB)
- 301 KB → **292 KB** (zero functionality loss — pure size win)

---

## Compatibility

- Windows 7 SP1 / 8 / 8.1 / 10 / 11 (32-bit and 64-bit)
- First run auto-creates `feeds.ini` next to the exe; optionally switch to config embedded in the exe tail (fully single-file)
- Uses only built-in msvcrt / Media Foundation / WinHTTP — **no third-party DLLs required**
