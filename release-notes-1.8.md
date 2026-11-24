# LiteTune 1.8.0

> **极简、绿色便携的 RSS 播客播放器** —— 纯 C + Win32，单文件、约 300 KB，Win7–Win11 32/64 位通用。

## 下载

- [LiteTune.exe](https://github.com/EARTH-CODE-42/LiteTune/releases/download/v1.8.0/LiteTune.exe)（约 300 KB，绿色单文件，Win7–Win11 32/64 位通用）
- 使用说明见 [README.md](https://github.com/EARTH-CODE-42/LiteTune/blob/main/README.md)

---

## 主要改动

### 章节支持（Podcasting 2.0）
- 简介区升级为 **「简介 / 章节 / 字幕」三个 tab**，点一下切换
- 自动读取订阅源的 `podcast:chapters` JSON，章节列表与字幕列表同款样式，点击任一章即跳转
- 本集没有章节时给出占位提示，不会看起来像坏了
- 桌面字幕窗可跟随「字幕」或「章节」——以你最后点击的 tab 为准

### 流式下载 + 断点续传
- 音频改为**边下边写盘**：100 MB 的播客峰值内存从 100 MB 降到 64 KB
- 断线后按 HTTP `Range` 续传，不再整集重下；服务端不支持时自动清掉半成品重来
- 下载中的剧集在**时长列实时显示百分比**
- 写盘失败立即提示「缓存目录不可写」，不再笼统显示「下载失败」

### 缓存命名可读化
- 音频：`播客名_剧集名_<8hex>.mp3`
- 字幕 / 章节 / 订阅 / 封面同理：`播客名_剧集名_<hash>.vtt/.srt/.json`、`播客名_<hash>.xml`，封面按真实图片格式保存
- 在资源管理器里按名排序天然成组；旧的纯哈希文件名**继续兼容**，不迁移

### 拖放与本地字幕
- 把 `.opml` / `.ini` 拖进主窗口即导入订阅或完整配置（配置需重启生效）
- 把 `.vtt` / `.srt` 拖到某一集上即绑定该集字幕
- 剧集右键新增「导入字幕…」，自己挑文件同样可以；字幕自动复制进缓存，重启后依然生效

### 缓存清理对话框
- 「立即清理无用缓存」升级为对话框：显示待删文件数与总体积
- 可选清理范围（失效播客 / 现有播客 / 全部）× 内容（音频 / 字幕 / 章节 / 封面 / 订阅信息）
- 「已缓存」列表右键的「清空全部缓存」与本功能合并，入口不再重复

### 桌面字幕窗增强
- 记住位置与大小，重启后回到原处（超出当前显示器时自动回到默认位置）
- 鼠标移入显示控制条：播放暂停 / -15 秒 / +15 秒 / 关闭

### 修复
- 剧集列表 header 水平滚动后列文本被擦掉
- 切到「章节」tab 偶尔整片空白；占位提示只显示一个字；有章节的剧集切到无章节剧集后残留旧章节
- 拖动窗口边缘缩放时，章节按钮与剧集列表右下角闪过白色小方块
- 上述修复的副作用：缩放后剧集列表滚轮失效（已改为自己处理滚轮，滚动更可靠）
- 缓存目录父目录不存在导致下载到 99% 失败；启动时自检目录是否可写

### 体积与整洁化
- printf 改用 msvcrt 实现，去掉 MinGW C99 printf 白带的约 75 KB
- 合并多处重复代码（缓存查找、悬停提示、右键菜单主题化、自绘弹窗脚手架等）
- 源码更短，exe 体积仍维持 **约 300 KB**

---

## 兼容

- Windows 7 SP1 / 8 / 8.1 / 10 / 11，32 位与 64 位均可运行
- 配置文件格式与 1.7 完全一致，直接覆盖 exe 升级即可，订阅、缓存、播放记录都保留
- 缓存文件新旧命名双向兼容，无需手动改名
- 依赖系统自带的 msvcrt / Media Foundation / WinHTTP，**不引入任何第三方 DLL**

---

## What's New in 1.8.0

### Podcasting 2.0 Chapters
- The notes pane now has **Notes / Chapters / Subtitles** tabs
- Reads the feed's `podcast:chapters` JSON; click any chapter to jump, with a placeholder when an episode has none
- The desktop caption window follows whichever tab you clicked last (subtitles or chapters)

### Streaming Downloads with Resume
- Audio is written to disk as it arrives — peak memory for a 100 MB episode drops from 100 MB to 64 KB
- Interrupted downloads continue with an HTTP `Range` request; servers that ignore Range are handled by starting over cleanly
- Live percentage shown in the duration column while downloading
- Disk errors are reported immediately as "cache folder not writable" instead of a generic failure at 99%

### Readable Cache Names
- Audio: `Podcast_Episode_<8hex>.mp3`; subtitles, chapters, feed and artwork cache use the same readable scheme
- Legacy hash-only filenames still work — no migration needed

### Drag & Drop and Local Subtitles
- Drop an `.opml` / `.ini` onto the window to import subscriptions or a full config (restart required for config)
- Drop a `.vtt` / `.srt` onto an episode row to bind it; right-click → "Import subtitles…" works too
- Subtitles are copied into the cache and survive restarts

### Cache Cleanup Dialog
- Lists stale / orphaned files with counts and total size
- Pick the scope (failed feeds / current feeds / everything) and content types (audio / subtitles / chapters / artwork / feed data)

### Desktop Caption Window
- Remembers position and size across restarts
- Hover control bar: play-pause / -15 s / +15 s / close

### Fixes
- Header text no longer erased after horizontal scrolling
- Chapters tab no longer goes blank; placeholder text fully rendered; stale chapters cleared when switching episodes
- White square flash at the chapter button and the episode list's bottom-right corner while resizing
- Mouse wheel keeps working after resizing the window
- Cache downloads no longer fail at 99% when the parent folder is missing

### Size & Cleanup
- Replaced MinGW's C99 printf with msvcrt (~75 KB saved)
- Merged duplicated code paths; still a single **~300 KB** executable

---

## Compatibility

- Windows 7 SP1 / 8 / 8.1 / 10 / 11 (32-bit and 64-bit)
- Config format is unchanged from 1.7 — just replace the exe; subscriptions, cache and playback history are kept
- Old and new cache filenames are both supported
- Uses only built-in msvcrt / Media Foundation / WinHTTP — **no third-party DLLs required**
