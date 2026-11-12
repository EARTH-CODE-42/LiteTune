# LiteTune 1.8 开发待办

> **新对话开工指南**：用户说"开工"时，先完整阅读本文件，按 P0 → P1 → P2 顺序执行。
> 完成一项就在前面打勾 `[x]`。新想法先记入"候选"区，不直接动手。
> 编译命令见 README.md（mingw32 官方命令），**必须零警告**才交付用户测试。
> 唯一源码文件：`main.c`（约 15k 行）。函数定位用 grep 函数名，不要记行号（会变）。

## 已拍板的决策（不要再翻案）

- **缓存命名**：哈希**后缀** `播客_剧集_<8hex>.mp3`——文件管理器按名排序天然成组（用户明确要求后缀，不是前缀）
- **导入配置**：不再导入 `cachedir`，从根上解决"配置搬到新机器后下载 99% 失败"
- **本地字幕**：`sub_<剧集URL哈希>.vtt`，文件即状态，**不写 ini**
- **下载**：边下边写，保留"先下后播"，**不做边下边播**（与 1.6 恢复的设计冲突）
- **体积**：不追数字。实测 `-flto` 和 `-ffunction-sections --gc-sections` 都反而 +46%（318KB→465KB），现行编译参数已近最优，只做重构和死代码清理
- **键盘 ←/→ 快进快退 15s 已有**（`VK_LEFT`/`VK_RIGHT`），不用重复做
- **发布**：GitHub Release 用户自己发，SourceForge 用户手动跑 publish-sourceforge.ps1，AI 不代发

## P0（必修，直接影响可用性）

- [ ] **下载 99% 失败修复**
  - 根因：`EnsureCacheDir()` 用单层 `CreateDirectoryW`，父目录不存在时静默失败 → HTTP 下载完成（进度 99%）→ `_wfopen(cache, L"wb")` 失败 → 报"下载失败"
  - 修法：递归建目录（逐级 CreateDirectoryW 或 SHCreateDirectoryExW，shell32 已链接）
  - 启动自检：目录建不出/不可写 → 回退默认目录并提示一次
  - `_wfopen` 失败时提示"缓存目录不可写"，不再笼统报"下载失败"
- [ ] **导入配置剔除 cachedir**：只导入订阅和设置，导出格式保持不变
- [ ] **本地字幕导入**
  - 缓存为 `sub_<剧集URL哈希>.vtt`（`.srt` 保留原扩展名，解析器按扩展名分发）
  - 查找优先级：`sub_` 本地 → `trans_` 网络缓存 → 才下载
  - 不写 ini；删缓存时现有 `IsCacheFileName` 前缀规则自动覆盖
  - 入口：剧集右键菜单「导入字幕…」（GetOpenFileName，comdlg32 已链接）

## P1（体验提升）

- [ ] **缓存命名可读化**
  - `播客名_剧集名_<8hex>.mp3`；查找用 `*_<hash>.mp3` 通配（FindFirstFile），**旧 `<8hex>.mp3` 文件必须兼容**，不迁移
  - 非法字符过滤 `<>:"/\|?*` + 每段截断约 60 字（防 260 路径限制，中文名容易超）
  - 同步更新 `IsCacheFileName`、`CacheAutoClean`、`CacheDirHasConflict`、删缓存三处
- [ ] **字幕窗记住位置与大小**：ini 存 `subwin=x,y,w,h`，`WM_EXITSIZEMOVE` 写入；启动校验是否落在当前显示器内，否则回默认
- [ ] **字幕窗悬停控制条**：⏯ / -15s / +15s / ✕，复用托盘浮窗 TrackMouseEvent 逻辑；不与现有右键暂停、悬停叉叉关闭冲突
- [ ] **流式下载 + 断点续传**（本轮改动最大，建议 P1 里最后做）
  - 边下边写替代"整集读进内存再落盘"（现在 100MB 播客峰值 100MB 内存）
  - `Range` 续传，断线不重来（服务器端遥控播放已支持 Range，可参照）
  - 顺带：每集下载进度显示（列表行百分比）+ 写盘错误即时反馈

## P2（新功能）

- [ ] **拖放**：`DragAcceptFiles` + `WM_DROPFILES`（当前代码完全没有）
  - 整窗：.opml / .ini / .vtt
  - 拖到具体剧集行：`DragQueryPoint` + `LVHITTEST` → 绑定该集字幕（依赖 P0 的 sub_ 命名先落地）
- [ ] **章节支持**（Podcasting 2.0 chapters JSON）：章节列表复用字幕列表 UI，点击跳转；很多英文播客自带
- [ ] **重构与死代码审计**
  - `MessageBoxTh` 仍是系统 MessageBoxW + WH_CBT 钩子做暗色，统一到已有的自绘弹窗，删掉钩子
  - 清理无引用辅助函数、未使用翻译条目

## 1.9 候选（本轮不做，已讨论过方向）

### 静音裁剪（Trim Silence / Smart Speed）
- 竞品 Overcast / Pocket Casts 招牌功能，评测常拿这点批评 LiteTune
- **可行**：`IMFSourceReader` 已在用（读时长那段），离线扫描 PCM 算 RMS，无需新库
- 思路：扫描已缓存文件 → 静音段列表存 `<hash>.sil`（阈值约 -45dB、最短静音 300ms，可调）→ 播放时按表 seek 跳过
- 与"先下后播"完美兼容（需要完整文件）；扫描比实时快几十倍，1 小时音频几秒扫完
- 设置里加开关，默认关
- **廉价响度归一化**可顺带做：扫描时算整集 RMS → 播放时用现有 SetVolume 按增益补偿，不用 DSP

### 网络收音机（用户有意转向）
- 定位扩展：播客 + 电台二合一，都是"听"的场景；市面上小体积二合一工具少，有差异化
- **技术基础已具备**：MFPlay `CreateMediaItemFromURL` 支持 http 流，Icecast/Shoutcast 的 MP3/AAC 直连流理论上可播
- 分两步走：
  1. 先做"输入 URL 收听"（如 `http://myradio24.org/1987`）+ 手动收藏，验证 MFPlay 播 Icecast 的可行性（spike）
  2. 再接 Radio-Browser 目录（radio-browser.info，免费开源 API、无需 key，4 万+电台，WinHTTP 直接接）
- UI：左侧新增"电台"分类（与"已缓存"并列）；电台无剧集列表，点击即播；进度条显示"直播中"
- ICY metadata（StreamTitle 当前曲目）：发 `Icy-MetaData:1` 头，按 metaint 间隔解析——体验关键但 MFPlay 不一定暴露，可能要自己解析流，是主要脏活
- 风险：m3u8/HLS 流 Win7 不支持，第一版只保证 MP3/AAC 直连流；守住单文件、零依赖、体积底线

### 播放队列 / 连续播放
### 标记已播/未播（可与收藏共用图标位）

## 明确不做

- **DSP（均衡器/实时音效）**：MFPlay 是黑盒，拿不到 PCM 回调；要 DSP 必须 IMFSourceReader 解码 + WASAPI 渲染重写播放引擎，体积风险工作量都上台阶，不划算
- **边下边播**：见"已拍板的决策"
- **跳过片头片尾 / 按播客自动缓存规则 / 睡眠定时淡出**：用户暂缓
