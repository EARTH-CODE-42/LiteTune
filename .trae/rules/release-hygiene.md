---
alwaysApply: true
scene: release_hygiene
---

1. **历史曾用名保留**：仓库从 `win32-podcast` 改名为 `LiteTune` 后，旧 release-notes（1.3–1.6）里的下载链接、deepwiki badge 等一律保持 `win32-podcast`；只有新版 release-notes 从 1.7 起用 `LiteTune`
2. **素材文件不轻易删**：用户对自己的截图（screenshot.png 等）有感情
3. **版本号全量同步**：main.c 里关于对话框的中英文版本号（两处）、resource.rc 的 FILEVERSION/PRODUCTVERSION（两处数字 + 两语系各两处字符串）、WinHTTP User-Agent（两处）必须一起改。遗漏会导致版本号对不上
4. **README 默认语言**：2026-11 起英文成为默认（README.md 英文版，README.zh-CN.md 中文版）
5. **大小描述**：所有 README / release-notes 里统一用"约 300 KB"或 "~300 KB"，不写精确数字（编译参数变化会导致 1–2 KB 浮动，不值得每次改）
6. **编译零警告**：`-Wall -Wextra` 必须零 warning 才能交付给用户冒烟测试
7. **发布节奏**：GitHub 发 Release 用户自己操作，`publish-sourceforge.ps1` 用户在 GitHub 发布后手动跑，不要由 AI 自动执行发布脚本
