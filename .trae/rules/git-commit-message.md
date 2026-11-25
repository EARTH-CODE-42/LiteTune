---
alwaysApply: true
scene: git_message
---

1. **代码简洁可读，奥卡姆剃刀**，不做无需求的抽象。
2. **精简体积先看 `size.exe` 的 text**；`-Os` 已合并重复代码，手工抽取常只剩可读性收益。
3. **不自测截图**；零警告后交用户冒烟。崩溃用事件日志偏移 + `objdump` 定位，构造最小复现验证。
4. **常见坑**：可选出参必须判空；隐藏原生滚动条会让 ListView 丢滚轮；owner-draw 按钮要挂
   `SubclassForFontWheel`，否则暗色下闪白。
5. **不自行 commit/push**，用户明确要求才做；提交信息用中文 `fix:` / `feat:` / `chore:` + 简短正文。
6. **发版前**：清临时文件、同步版本号、更新 README、写 release-notes。
