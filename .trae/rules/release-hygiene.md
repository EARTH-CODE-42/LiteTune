---
alwaysApply: true
scene: release_hygiene
---

1. **版本号共 10 处**：main.c 关于框中英 2 处 + WinHTTP UA 2 处；resource.rc FILEVERSION/
   PRODUCTVERSION 数字 2 处 + 两语系字符串 4 处。
2. **README 默认英文**（README.md 英文、README.zh-CN.md 中文）；大小统一写"约 300 KB"。
3. **旧版 release-notes（1.3–1.6）保留 `win32-podcast` 链接**，1.7 起用 `LiteTune`。
4. **用户截图等素材不删**。
5. **默认用户自己发布**；明确要求代发时：git 代理失效加 `-c http.proxy=`；无 `gh`，
   用 REST API + `git credential`（token 不回显）；GitHub 换资产先删后传；
   SourceForge 用 `-Version x.y.0` 指定目录。
6. **兼容底线**：Win7–11 32/64、单文件、零第三方 DLL、约 300 KB、配置向下兼容。
