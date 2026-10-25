# LiteTune 1.4

## 新增功能 ✨

- **简介区可折叠**：鼠标移到简介面板左缘中间，会浮出一个细窄小按钮（移开即隐藏）。点击后右侧简介收起，剧集列表自动铺满整块空间；收起后按钮移到剧集列表右缘中间，图案变为 `‹`，点击即可恢复。折叠状态会记入配置（`deschide`），下次启动保持不变；亮色 / 暗色主题都有对应配色的按钮

## 改进与修复 🛠

- **暗色菜单全面重绘**：设置菜单、右键菜单、托盘菜单统一改为自绘。勾选项是简洁的两笔 √（不再是彩色 emoji 方框），子菜单用细线 ›（不再是系统黑色实心三角），文字颜色随亮 / 暗主题自动切换，不再出现黑字
- **暗色菜单尺寸统一**：行高更紧凑、菜单更宽，与亮色模式观感一致
- **定时关机父项勾选**：设定定时关机后，菜单里的「定时关机」项左侧正确显示 √
- **亮色输入框蓝色横条修复**：添加订阅输入框聚焦时不再出现 Win11 补画的主题色下划线，亮 / 暗模式边框一致
- **Win10 暗色标题栏即时生效**：切换亮 / 暗主题后标题栏立即变色，不再需要最小化、点「关于」或切换窗口焦点
- **菜单代码重构**：所有菜单项统一走 `MenuAdd` / `MenuAddPopup` / `MenuAddSep` 一套构建接口，主题应用与资源释放成对出现，删除了旧的原生菜单递归改写与 uxtheme 实验补丁
- **手机遥控防火墙规则校验**：开启服务时会核对入站规则指向的 exe 路径（复制 / 移动过程序后不再出现「本机浏览器能开、手机电脑连不上」）
- **手机服务更耐瞬断**：接受连接遇到瞬时错误时自动重试，不会悄悄停止监听

## 下载

- [LiteTune.exe](https://github.com/EARTH-CODE-42/win32-podcast/releases/download/v1.4/LiteTune.exe)（约 270 KB，绿色单文件，Win7–Win11 32/64 位通用）
- SourceForge 镜像：https://sourceforge.net/projects/litetune/files/

---

## What's New in 1.4

### Features ✨

- **Collapsible description pane**: hover near the middle of the description pane's left edge and a slim handle fades in (it disappears when the pointer leaves). Click it to hide the description and let the episode list take over the full width; once hidden, the handle moves to the middle of the episode list's right edge with its arrow flipped to `‹`, and clicking it restores the pane. The collapsed state is saved to the config (`deschide`) and remembered on the next launch, with matching colours in both light and dark themes

### Improvements & Fixes 🛠

- **Fully owner-drawn dark menus**: the settings menu, context menus and tray menu now share one custom-drawn implementation. Checked items use a clean two-stroke √ (no more colourful emoji box), submenus use a thin › (no more black native triangle), and text follows the light / dark palette — no more dark text on a dark background
- **Consistent dark menu sizing**: tighter row height and a wider layout that matches the light theme
- **Sleep timer parent item**: the Sleep Timer entry now shows the √ marker while a timer is set
- **Light-mode input box underline fixed**: the Add Subscription box no longer gets the accent-coloured underline Windows 11 draws on focused text fields; light and dark borders now match
- **Instant dark title bar on Windows 10**: switching the theme now repaints the title bar immediately — no need to minimize the window, open About, or change focus
- **Menu code refactor**: every item is built through the same `MenuAdd` / `MenuAddPopup` / `MenuAddSep` helpers, theme application and cleanup are paired, and the old native-menu recursion plus the experimental uxtheme patches were removed
- **Phone-server firewall check**: starting the service now verifies that the inbound rule really points at this exe, so a copied or moved copy no longer gives you a page that opens locally while phones and other PCs time out
- **Transient accept errors retried**: the listener no longer stops silently when `accept` hits a temporary error

### Download

- [LiteTune.exe](https://github.com/EARTH-CODE-42/win32-podcast/releases/download/v1.4/LiteTune.exe) (~270 KB, portable single exe, Windows 7–11 32/64-bit)
- SourceForge mirror: https://sourceforge.net/projects/litetune/files/
