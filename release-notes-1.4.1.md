# LiteTune 1.4.1

## 新增功能 ✨

- **新剧集角标**：刷新后发现新剧集，播客列表右侧显示红色未读数字（超过 99 显示 `99+`）。点开该播客即清零；没点开的角标重启后依然保留（记录在配置 `seenep`）。新添加的订阅不会整片飘红
- **托盘气泡提醒**：窗口收在托盘时，刷新（手动或自动）发现新剧集，托盘气泡提示「发现 N 集新剧集」；窗口可见时不打扰，直接看角标即可
- **定时自动刷新**：设置菜单新增「自动刷新」——关 / 30 / 60 / 120 / 360 分钟，默认 60 分钟。空闲时自动联网更新全部订阅；播放、下载、批量缓存期间自动跳过，不打断当前收听
- **JSON Feed 支持**：除 RSS / Atom 外，也可直接订阅 [jsonfeed.org](https://jsonfeed.org) 格式的源。自研零依赖解析器，支持附件（自动挑音频）、发布日期、时长、作者（1.1 的 `authors` 与 1.0 的 `author`）和封面

## 改进与修复 🛠

- **高 DPI 显示一致**：改为不做 DPI 感知、由系统整体缩放窗口。125% / 150% 缩放的笔记本上与 100% 台式机显示内容完全一致，不再出现字号偏大、文字溢出截断（代价是高缩放屏上略模糊）
- **OPML 导入弹窗层级**：在「添加订阅」里点「从 OPML 文件导入」，文件选择框现在正确盖在订阅弹窗之上；在订阅弹窗点「取消」时导入框一并关闭
- **批量下载完成判定**：修复批量缓存偶发「进度到 100% 但仍未结束」的判断问题，进度与完成提示更准确
- **下载线程资源释放**：单集 / 批量下载线程先查缓存文件再取锁，线程句柄按顺序释放，减少偶发下载失败
- **解析内存**：JSON Feed 改为按字符串实际长度分配缓冲，大型源不再为每个字段申请整篇文档大小的内存；订阅条目超过 1 万上限时不再泄漏一个 URL

## 下载

- [LiteTune.exe](https://github.com/EARTH-CODE-42/win32-podcast/releases/download/v1.4.1/LiteTune.exe)（约 280 KB，绿色单文件，Win7–Win11 32/64 位通用）
- SourceForge 镜像：https://sourceforge.net/projects/litetune/files/

---

## What's New in 1.4.1

### Features ✨

- **New-episode badges**: after a refresh, podcasts with new episodes show a red unread count on the right of the list (`99+` above 99). Opening the podcast clears it; unopened badges survive a restart (persisted as `seenep`). Newly added subscriptions never light up with a full backlog
- **Tray balloon notice**: while the window sits in the tray, a refresh (manual or scheduled) that finds new episodes shows "Found N new episode(s)" in a balloon tip. When the window is visible it stays silent — the badge already tells you
- **Scheduled auto-refresh**: a new "Auto-Refresh" entry in Settings — Off / 30 / 60 / 120 / 360 minutes, 60 by default. Feeds update automatically while idle; refreshes are skipped during playback, downloads and batch caching so listening is never interrupted
- **JSON Feed support**: besides RSS / Atom, [jsonfeed.org](https://jsonfeed.org) feeds can be subscribed directly. A built-in zero-dependency parser handles attachments (picks the audio one), publication dates, durations, authors (`authors` in 1.1 and `author` in 1.0) and cover art

### Improvements & Fixes 🛠

- **Consistent high-DPI rendering**: the app is now DPI-unaware and lets Windows scale the whole window. A 125% / 150% laptop now shows exactly the same content as a 100% desktop — no more oversized fonts or clipped text (slightly blurry on high-DPI screens, which is the trade-off)
- **OPML import dialog layering**: choosing "Import from OPML file" inside the Add Subscription dialog now opens the file picker on top of it, and cancelling the subscription dialog also closes the picker
- **Batch download completion**: fixed the occasional "progress at 100% but not finished" judgement, so progress and completion messages are accurate
- **Download thread cleanup**: episode and batch download threads now check the cache file before taking the lock and release handles in order, reducing occasional failures
- **Parser memory**: JSON Feed buffers are allocated to the actual string length instead of the rest of the document, and feeds beyond the 10,000-episode cap no longer leak a URL

### Download

- [LiteTune.exe](https://github.com/EARTH-CODE-42/win32-podcast/releases/download/v1.4.1/LiteTune.exe) (~280 KB, portable single exe, Windows 7–11 32/64-bit)
- SourceForge mirror: https://sourceforge.net/projects/litetune/files/
