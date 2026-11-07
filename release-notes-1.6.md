# LiteTune 1.6.0

> **极简、绿色便携的 RSS 播客播放器** —— 纯 C + Win32，单文件、约 296 KB，Win7–Win11 32/64 位通用。

## 下载

- [LiteTune.exe](https://github.com/EARTH-CODE-42/win32-podcast/releases/download/v1.6.0/LiteTune.exe)（约 296 KB，绿色单文件，Win7–Win11 32/64 位通用）
- 使用说明见 [README.md](https://github.com/EARTH-CODE-42/win32-podcast/blob/main/README.md)

---

## 主要改动

### 「定位正在播放」小按钮（全新）
- 当前播客的绿色剧集滚出可视区、且鼠标停在剧集面板里时，右下角浮出一个小圆币按钮
- 圆币里的三角指向绿行所在方向（▲ 在上方 / ▼ 在下方），点一下即把绿行滚到列表中段
- 鼠标离开剧集区或绿行重新可见时自动消失，不占地方

### 剧集右键菜单
- 新增「播放」，与双击完全等价
- 多选右键可「删除选中的 N 集缓存」，一次清掉所选剧集的音频（正在播的那集会先停止）
- 菜单顺序重整：播放 / 收藏 → 复制 / 删除或下载 / 打开位置；多选项放在分隔线下方

### 播放体验
- **先下后播**：双击还没缓存的剧集时，它在后台下载，正在播的那集继续放，下载完成才自动切过去（恢复原初版的好设计）
- 暂停时播客列表里的当前播客仍保持绿色字体，一眼能看出"停在这里的是谁"

### 缓存目录切换更安全
- 目标文件夹里若有同名文件（音频 / 封面 / 订阅缓存）则拒绝切换并说明原因，避免覆盖你的数据
- 下载进行中切换目录不再出错：正在下载的音频下完后自动落到新目录，提示语也会统计"下载中"的数量

### 界面修复
- 修复拖动滚动条时表头右端出现小方块残影
- 修复自绘滚动条滑块与两端箭头有 1px 错位（滑块未居中）
- 修复「定位正在播放」圆环右下弧线缺像素

### 代码整理
- 图标绘制统一为一张形状表 + 一个渲染入口（30 种图标共用），主题色改为画刷缓存，绘制更快、代码更短

---

## 兼容

- Windows 7 SP1 / 8 / 8.1 / 10 / 11，32 位与 64 位均可运行
- 配置文件格式与 1.5 完全一致，直接覆盖 exe 升级即可，订阅、缓存、播放记录都保留
- 依赖系统自带的 msvcrt / Media Foundation / WinHTTP，**不引入任何第三方 DLL**

---

## What's New in 1.6.0

### "Locate Playing" Button (new)
- When the green episode scrolls out of view and the mouse is over the episode pane, a small round button appears at the bottom-right
- Its triangle points toward the row (▲ above / ▼ below); one click scrolls it to the middle of the list
- Auto-hides when the mouse leaves the pane or the row becomes visible again

### Episode Context Menu
- New "Play" item, identical to double-clicking
- Multi-selection gains "Delete cache of N selected" — wipes the audio of every selected episode at once (the currently playing one is stopped first)
- Menu reordered: Play / Favorite → Copy / Delete or Download / Open location; multi-select actions sit below the separator

### Playback
- **Download first, switch later**: double-clicking an uncached episode downloads it in the background while the current one keeps playing, and only then does playback switch over (the original design is back)
- The current podcast stays green in the feed list while paused, so you can always tell what is cued up

### Safer Cache Folder Switching
- The switch is refused (with an explanation) when the target folder already holds same-named files — audio, cover art or feed cache — so your data is never overwritten
- Switching folders mid-download no longer misplaces files: audio finished afterwards lands in the new folder, and the prompt also reports how many downloads are in flight

### UI Fixes
- Fixed a small square ghost at the right end of the list header while dragging the scroll thumb
- Fixed a 1px misalignment between the scroll thumb and the end arrows (the thumb is now centred)
- Fixed missing pixels on the bottom-right arc of the "locate playing" ring

### Code Cleanup
- Icon drawing unified into one shape table plus a single render entry point (30 icons share it); theme colours are now cached brushes — faster painting, less code

---

## Compatibility

- Windows 7 SP1 / 8 / 8.1 / 10 / 11 (32-bit and 64-bit)
- Config format is unchanged from 1.5 — just replace the exe; subscriptions, cache and playback history are kept
- Uses only built-in msvcrt / Media Foundation / WinHTTP — **no third-party DLLs required**
