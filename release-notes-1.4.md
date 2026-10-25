# LiteTune 1.4

## 改进与修复 🛠

- **暗色菜单全面重绘**：设置菜单、右键菜单、托盘菜单统一改为自绘。勾选项是简洁的两笔 √（不再是彩色 emoji 方框），子菜单用细线 ›（不再是系统黑色实心三角），文字颜色随亮 / 暗主题自动切换，不再出现黑字
- **暗色菜单尺寸统一**：行高更紧凑、菜单更宽，与亮色模式观感一致
- **定时关机父项勾选**：设定定时关机后，菜单里的「定时关机」项左侧正确显示 √
- **亮色输入框蓝色横条修复**：添加订阅输入框聚焦时不再出现 Win11 补画的主题色下划线，亮 / 暗模式边框一致
- **Win10 暗色标题栏即时生效**：切换亮 / 暗主题后标题栏立即变色，不再需要最小化、点「关于」或切换窗口焦点
- **菜单代码重构**：所有菜单项统一走 `MenuAdd` / `MenuAddPopup` / `MenuAddSep` 一套构建接口，主题应用与资源释放成对出现，删除了旧的原生菜单递归改写与 uxtheme 实验补丁

## 下载

- [LiteTune.exe](https://github.com/EARTH-CODE-42/win32-podcast/releases/download/v1.4/LiteTune.exe)（约 270 KB，绿色单文件，Win7–Win11 32/64 位通用）
- SourceForge 镜像：https://sourceforge.net/projects/litetune/files/

---

## What's New in 1.4

### Improvements & Fixes 🛠

- **Fully owner-drawn dark menus**: the settings menu, context menus and tray menu now share one custom-drawn implementation. Checked items use a clean two-stroke √ (no more colourful emoji box), submenus use a thin › (no more black native triangle), and text follows the light / dark palette — no more dark text on a dark background
- **Consistent dark menu sizing**: tighter row height and a wider layout that matches the light theme
- **Sleep timer parent item**: the Sleep Timer entry now shows the √ marker while a timer is set
- **Light-mode input box underline fixed**: the Add Subscription box no longer gets the accent-coloured underline Windows 11 draws on focused text fields; light and dark borders now match
- **Instant dark title bar on Windows 10**: switching the theme now repaints the title bar immediately — no need to minimize the window, open About, or change focus
- **Menu code refactor**: every item is built through the same `MenuAdd` / `MenuAddPopup` / `MenuAddSep` helpers, theme application and cleanup are paired, and the old native-menu recursion plus the experimental uxtheme patches were removed

### Download

- [LiteTune.exe](https://github.com/EARTH-CODE-42/win32-podcast/releases/download/v1.4/LiteTune.exe) (~270 KB, portable single exe, Windows 7–11 32/64-bit)
- SourceForge mirror: https://sourceforge.net/projects/litetune/files/
