# LiteTune 1.7.0

> **极简、绿色便携的 RSS 播客播放器** —— 纯 C + Win32，单文件、约 300 KB，Win7–Win11 32/64 位通用。

## 下载

- [LiteTune.exe](https://github.com/EARTH-CODE-42/LiteTune/releases/download/v1.7.0/LiteTune.exe)（约 300 KB，绿色单文件，Win7–Win11 32/64 位通用）
- 使用说明见 [README.md](https://github.com/EARTH-CODE-42/LiteTune/blob/main/README.md)

---

## 主要改动

### 完整字幕系统（全新）
- 简介区加 **「简介 / 字幕」两个 tab**，点一下在剧集说明和字幕条列表之间切换
- 自动读取 RSS `<podcast:transcript>`（VTT / SRT 均支持），也可右键菜单手动为单集指定字幕地址
- 字幕列表滚轮每次跳一个 cue（**不再跳回 00:00**），点击字幕条即时定位到对应时间
- **桌面悬浮字幕窗**：独立可拖动、可缩放、置顶，主窗口最小化后依然保持可见；右键暂停，悬停右上角叉叉关闭；字号按窗口高度自适应（正好两行）
- 字幕自动缓存到本地，切换剧集后播放位置回到对应 cue

### 内建播客搜索
- 新增播客对话框加 Tab → **播客搜索**，调 iTunes Search API
- 搜索结果带图标、作者、简介，直接点加号一键订阅

### 国际化（i18n）
- 所有硬编码中文字符串抽出到翻译表，程序启动时按系统语言或设置切换
- 覆盖按钮文字、tooltip、倍速显示、下载进度提示、格式化信息串等全部 UI
- 新功能默认双语：字幕 tab 按钮、倍速按钮、播客搜索对话框

### 按钮视觉重整
- 字幕开关 / 简介 tab / 字幕 tab 统一成收藏按钮同款造型：按下不回弹，按下态用 **DrawEdge 凹陷 + 底色变化** 表示，字体颜色保持正常

### 缓存搬家补全
- 缓存目录切换的同名冲突检测补了 `.vtt` 字幕缓存（之前只查 mp3/img/xml/hdr）
- **删除整播客 / 单集 / 批量 / 全部缓存** 现在会顺带删掉对应的字幕缓存，不再留下孤儿 vtt

### 体验修复
- 滚轮跳转播放加 700ms 保护期（Media Foundation seek 异步生效期间不重算字幕下标）
- 播放中浏览其它剧集时，字幕数据不被替换回来（`SubFollowsEpisode`）
- 下载按钮 tooltip 精简（去掉 `right-click to batch download`）

---

## 兼容

- Windows 7 SP1 / 8 / 8.1 / 10 / 11，32 位与 64 位均可运行
- 配置文件格式与 1.6 完全一致，直接覆盖 exe 升级即可，订阅、缓存、播放记录都保留
- 依赖系统自带的 msvcrt / Media Foundation / WinHTTP，**不引入任何第三方 DLL**

---

## What's New in 1.7.0

### Full Subtitle System (new)
- The episode pane gains **two tabs — Notes / Subs** — one click swaps between the episode description and a cue list
- Picks up RSS `<podcast:transcript>` (VTT and SRT both supported); right-click any episode to override with a custom URL
- The cue list **advances one cue per wheel tick** (no more jumping back to 00:00); click any cue to seek to that timestamp
- **Floating desktop caption window**: draggable, resizable, always-on-top, stays visible when the main window is minimised; right-click to pause, hover the top-right X to close; font size auto-fits to two lines based on window height
- Cached locally (`trans_<hash>.vtt`) — no re-download after the first use

### Built-in Podcast Search
- The "add podcast" dialog now has a second tab, **Podcast Search**, backed by the iTunes Search API
- Results show artwork, author and a short description — one click to subscribe

### Full i18n
- All hard-coded UI strings moved into a translation table; language follows the system locale or your setting
- Covers button labels, tooltips, speed readout, download progress and the info-bar format string — everything
- Every new control ships bilingual: subtitle tabs, speed button, podcast search dialog

### Unified Button Visuals
- The subtitle tab / notes tab / subtitle toggle buttons now use the same **pressed-doesn't-pop-back** behaviour as the favourite button: `DrawEdge` paints a sunken border on the active state, plus a darker background — text colour stays normal

### Safer Cache Folders
- Same-name conflict check before cache-folder switching now covers `.vtt` subtitle files (was mp3/img/xml/hdr only)
- **Delete podcast cache / delete episode cache / delete all cache** now also removes the matching subtitle cache — no orphaned `.vtt` files left behind

### Fixes & Polish
- 700 ms debounce after any manual seek (Media Foundation seeks are async) so the subtitle row doesn't jitter during wheel jumps
- Caption data no longer clobbers when you browse other episodes while one is still playing
- Download button tooltip simplified (removed "right-click to batch download")

---

## Compatibility

- Windows 7 SP1 / 8 / 8.1 / 10 / 11 (32-bit and 64-bit)
- Config format is unchanged from 1.6 — just replace the exe; subscriptions, cache and playback history are kept
- Uses only built-in msvcrt / Media Foundation / WinHTTP — **no third-party DLLs required**
