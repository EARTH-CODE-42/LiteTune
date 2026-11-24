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
- **体积**：不追数字，但能换掉白带的库就换。实测 `-flto` 和 `-ffunction-sections --gc-sections` 都反而 +46%（318KB→465KB）；编译器参数已近最优，收益来自换实现（如 printf 改 msvcrt，-76KB）
- **键盘 ←/→ 快进快退 15s 已有**（`VK_LEFT`/`VK_RIGHT`），不用重复做
- **发布**：GitHub Release 用户自己发，SourceForge 用户手动跑 publish-sourceforge.ps1，AI 不代发

## P0（必修，直接影响可用性）

- [x] **下载 99% 失败修复**
  - 根因：`EnsureCacheDir()` 用单层 `CreateDirectoryW`，父目录不存在时静默失败 → HTTP 下载完成（进度 99%）→ `_wfopen(cache, L"wb")` 失败 → 报"下载失败"
  - 修法：递归建目录（逐级 CreateDirectoryW 或 SHCreateDirectoryExW，shell32 已链接）
  - 启动自检：目录建不出/不可写 → 回退默认目录并提示一次
  - `_wfopen` 失败时提示"缓存目录不可写"，不再笼统报"下载失败"
- [x] **导入配置剔除 cachedir**：只导入订阅和设置，导出格式保持不变
- [x] **本地字幕导入**
  - 缓存为 `sub_<剧集URL哈希>.vtt`（`.srt` 保留原扩展名，解析器按扩展名分发）
  - 查找优先级：`sub_` 本地 → `trans_` 网络缓存 → 才下载
  - 不写 ini；删缓存时现有 `IsCacheFileName` 前缀规则自动覆盖
  - 入口：剧集右键菜单「导入字幕…」（GetOpenFileName，comdlg32 已链接）
- [x] **启动即闪退（1.8 回归，发布前发现）**
  - 根因：`CacheAutoClean` 调 `CacheNameHash(fd.cFileName, ext, NULL, NULL)`，
    而 `CacheNameHash` 无条件写 `*ph` → 空指针写，WER 报 c0000005、偏移 0x7d43
    （反汇编确认就是 `mov %eax,(%edi)`）；缓存目录里只要有本程序命名的文件
    （首启默认 `%TEMP%\LiteTune` 常残留旧版缓存），启动约 4 秒内必崩
  - 首启弹窗点完语言/存储方式后才崩，是因为 `CacheAutoClean` 在 `LoadConfig` 之后
  - 修法：`if (ph) *ph = (unsigned int)wcstoul(...)`；用「ini + 一个缓存文件」复现，
    旧版 0xC0000005、修复版正常，并模拟真实首启自动点按钮验证能进主窗口

## P1（体验提升）

- [x] **缓存命名可读化**
  - `播客名_剧集名_<8hex>.mp3`；查找用 `*_<hash>.mp3` 通配（FindFirstFile），**旧 `<8hex>.mp3` 文件必须兼容**，不迁移
  - 非法字符过滤 `<>:"/\|?*` + 每段截断约 60 字（防 260 路径限制，中文名容易超）
  - 同步更新 `IsCacheFileName`、`CacheAutoClean`、`CacheDirHasConflict`、删缓存三处
- [x] **字幕缓存可读化**：与音频共用主名 `播客_剧集_<hash>.vtt/.srt`；先查同名文件，没有再看覆盖地址/RSS 地址下载并落盘；本地导入直接存进缓存；兼容旧 `sub_`/`trans_` 名
- [x] **订阅/封面缓存可读化**：`播客名_<8hex>.xml/.hdr`；封面按真实图片格式存 `.jpg/.png/.gif/.webp/.bmp`（旧 `feed_`/`cover_.img` 兼容并在加载时顺手改名）
- [x] **字幕窗记住位置与大小**：ini 存 `subwin=x,y,w,h`，`WM_EXITSIZEMOVE` 写入；启动校验是否落在当前显示器内，否则回默认
- [x] **字幕窗悬停控制条**：⏯ / -15s / +15s / ✕，复用托盘浮窗 TrackMouseEvent 逻辑；不与现有右键暂停、悬停叉叉关闭冲突
- [x] **流式下载 + 断点续传**（本轮改动最大，建议 P1 里最后做）
  - 边下边写替代"整集读进内存再落盘"（现在 100MB 播客峰值 100MB 内存）
  - `Range` 续传，断线不重来（服务器端遥控播放已支持 Range，可参照）
  - 顺带：每集下载进度显示（列表行百分比）+ 写盘错误即时反馈
  - 落地：新增 `HttpDownloadFile`——先写 `<缓存名>.mp3.part`，64KB 缓冲逐块落盘，
    完成后 `MoveFileEx` 改名；峰值内存从整集大小降到 64KB
  - 续传：按 `.part` 已有字节发 `Range: bytes=N-`；服务端回 200（忽略 Range）、
    206 区间起点不符、416 时自动清掉 `.part` 重下一次；取消/断线保留 `.part`
  - 行百分比：批量线程记录每集进度，下载中的行在时长列临时显示 `45%`
  - 写盘错误即时反馈：fopen/fwrite/fflush 失败立刻返回 diskError（不再等整集下完），
    先回退默认缓存目录重试，仍失败才提示
  - 清理同步：`CacheAutoClean` 覆盖 `*.mp3.part`，并修正可读化后订阅/封面缓存漏清理；
    删单集/整播客缓存时一并删 `.part`

## P2（新功能）

- [x] **拖放**：`DragAcceptFiles` + `WM_DROPFILES`
  - 整窗：.opml / .ini 拖入即导入（复用菜单导入逻辑，配置 ini 仍需确认后重启生效）
  - 拖到具体剧集行：按屏幕坐标命中行（`LVM_HITTEST`）→ 复制进字幕缓存并立即生效；
    未命中行则绑到当前选中集，没有选中集时提示"请把字幕拖到某一集上"
  - 落地：`ImportFileAt(path)` 从菜单导入中拆出供拖放复用；新增 `BindLocalSub`
    统一"本地字幕→缓存→清地址覆盖→重载"流程（菜单导入与拖放共用）
- [x] **章节支持**（Podcasting 2.0 chapters JSON）：章节列表复用字幕列表 UI，点击跳转；很多英文播客自带
  - 简介区 tab：简介 / 章节 / 字幕 三切换 + 方形字幕开关；字幕/章节只跟播放中的剧集（未播放时跟选中）
  - 修复：先下后播阶段 g_playState 仍为 0 导致切播客后章节被清空；tab 间内容类型串台（g_subKind）
  - 无数据时列表给占位提示，避免看起来像坏了
- [x] **立即清理无用缓存**：删除缓存目录里不属于任何现有播客/剧集的孤儿文件（只认本程序 8 位哈希命名）
  - 入口并入「自动清理缓存」子菜单；「立即刷新」并入「自动刷新」子菜单
  - 升级为清理对话框：可选范围（失效播客 / 现有播客 / 全部）× 内容（音频/字幕/章节/封面/订阅信息），显示待删数量与体积
  - 已缓存列表右键「清空全部缓存」与本功能合并为同一个对话框
- [x] 未知播客占位名改英文 `(Untitled)`，避免中文文件名/兼容性问题

- [x] **剧集列表 header 水平滚动后列文本被擦掉**
  - 真实根因：系统实现水平滚动不是给 header 设内部偏移，而是**把整个 header 窗口左移**；header 宽度只有列表可视区那么宽，移出去后右侧空出来的区域**已不属于 header**，所以怎么重画 header/怎么 Invalidate 都盖不住，露出的是 ListView 底色（看起来像文本被擦）
  - 修法：`FitListViewHeader` 把 header 宽改为 `max(ListView 窗口宽, 所有列总宽)`，左移后仍能盖满；`HeaderProc` 的 WM_SIZE 复用同一函数（原来那里会把宽度拉回列表宽，正好抵消）
  - 试错记录（都已删除，别再走）：子类化拦 `HDM_SETOFFSET`、WM_HSCROLL 里 InvalidateRect、`HeaderPaintIfScrolled` 自绘整条表头——全是错误归因
  - 结论：`HDF_OWNERDRAW` 本身没问题，不需要放弃暗色 header 自绘

- [x] **桌面字幕开启时点“章节”变空白**
  - 根因：字幕/章节共用 `g_subCues`；切章节时清空字幕，没有 `chaptersUrl` 或下载失败就一直为空，
    桌面字幕窗 `SubBarText()` 返回 NULL，什么都不画
  - 修法（保留共用数组，不拆数据结构）：
    - 新增 `g_subBarKind`（1=字幕 2=章节）记录桌面字幕内容偏好，只有点 subs/chapters tab
      才切换；简介 tab 不改变桌面字幕（用户拍板）。数组内容类型永远 = `g_subBarKind`
    - `LoadSubForCurTab`：播放中跟播放剧集，未播放跟选中剧集（新增 `SelectedEpisode` 复用）
    - 桌面字幕窗无条目时：未播放且有数据 → 显示选中剧集标题；无数据 → 按 `g_subBarKind`
      提示「（本集没有字幕）/（本集没有章节信息）」（复用现有翻译，不新增字符串）
    - **tab 列表空白的真正 bug**：占位行早已加入列表，但 owner-draw 只画
      `idx < g_subCueCount` 的条目，占位行被跳过 → 补上占位行绘制
  - 后续三连修（用户冒烟发现）：
    - 占位提示只显示一个字符：占位行宽度没计入 `g_subTextW`，自绘区域只有 10px →
      `SubRefreshUI` 把占位文本宽度也计入 `maxW`
    - 还没播到第一句字幕就误报"没字幕"：有数据但无当前条目时改为显示剧集标题，
      只有完全没数据才提示"没有字幕/章节"
    - 有章节剧集切到无章节剧集后残留旧章节：`want==2` 且该集无 `chaptersUrl` 时
      立即 `FreeSubCues()`（字幕分支靠 `LoadTranscript`/`OnSubReady` 清空，所以没这问题）
  - 代码净增约 30 行，exe 268,288 字节（+0.5KB）

- [x] **重构与死代码审计**
  - [x] **printf 换成 msvcrt 实现**：源码里 `#define __USE_MINGW_ANSI_STDIO 0`，只用
        `_vsnprintf/_vsnwprintf` 加一层截断补 `\0` 的包装。MinGW 的 C99 printf（浮点/
        位置参数等）此前白带约 75KB，本程序一次浮点都没用。exe 346KB → 270KB
        - 宽格式串的 `%ls` 全部改成 `%s`（msvcrt 宽 printf 里 `%s` 即宽字符串，`%hs` 是窄）
        - `sscanf` 一并走 msvcrt；已验证 `%d:%d:%d%*[.,]%d`、`%d,%d,%d,%d` 行为一致
  - ~~`MessageBoxTh` 改成自绘弹窗~~：已尝试并撤销——关于弹窗标题栏变白（DWM 暗色属性
        对非系统消息框失效），体积还 +1KB。保留系统 MessageBoxW + WH_CBT 钩子，不再动
  - [x] **合并重复块**：`FindCacheSuffix`（4 处缓存后缀扫描）、`TrackHover` +
        `TrackTipShow` + `TrackTipHide`（4 处悬停跟踪、3 处提示显隐）、`PopupMenu`
        （3 处右键菜单主题化）。`-Os` 下 270KB → 268KB（`-Oz` 还能再省 3KB，但为看清纯代码
        优化效果暂时保持 `-Os`）
  - [x] **清理无引用辅助函数、未使用翻译条目**（449 个 static 函数 0 个无引用）
        - 226 条翻译全部有代码引用，删掉 8 条早期搜索 UI 的残留：`TR_INPUT_LABEL`、
          `TR_BAD_URL`、`TR_SEARCH_BTN`、`TR_SEARCHING`、`TR_SEARCH_NONE`、
          `TR_SEARCH_FAIL`、`TR_TIP_SORT`、`TR_ORPHAN_NONE`（中英两表同步删）
        - 另删未用宏 `SUB_BTN_H`、`ID_IB_SEARCH`
  - [x] **继续合并重复块**：表头暗色补绘抽 `PaintHeaderGap`、表头自绘格式切换抽
        `HeaderSetOwnerDraw`、批量队列按 URL 查找抽 `BatchQueuedIndex`、删单集缓存抽
        `DeleteEpCacheFiles`、输入框确定/取消按钮抽 `IbOkCancelButtons`
        - exe 268,288 → 266,752 字节（-1.5KB），零警告
        - 注：后三处抽取后 GCC 的 `-Os` 交叉跳转已能合并，体积不再变，属于纯代码整洁化
  - [x] **自绘弹窗公共脚手架**：`RegDlgClass`（自绘对话框类）、`RegChildClass`（一次性
        子窗口类，bg 参数区分自绘/纯色）、`CenterDlg`（居中到主窗，首启回退屏幕工作区）、
        `RunModalDlg`（统一模态消息循环，focus/ibKeys 参数覆盖首启、清理框、输入框三种差异）
        - 替换：AskFirstRunMode / ShowCleanCacheDialog / AskInputBox 的注册+居中+模态循环；
          另合并 DSB / FLY / DESC_TAB / LOC_BTN / SUBBAR / LpPodTip 六处类注册
        - 删掉仅用于清理框的 `g_clReg` 标志；`ID_IB_*` 控件 ID 上移到控件 ID 区
        - exe 266,752 → 265,216 字节（-1.5KB），零警告
        - SubBar 是非模态窗，只合并了类注册，窗口过程本身不动
  - [x] **拖动缩放时白方块闪烁**
        - 章节 tab 是唯一没走 `SubclassForFontWheel` 的 owner-draw 按钮 → 擦背景用系统
          COLOR_BTNFACE（240），补上 subclass 后 `PanelProc` 的 WM_ERASEBKGND 拦截生效
        - 剧集/搜索列表右下角 17×17 白方块 = 原生滚动条尺寸盒：自绘条是唯一滚动条，
          在 `PanelProc` 的 WM_SIZE 里 `ShowScrollBar(SB_BOTH, FALSE)` 隐藏原生条
          （已验证 `GetScrollInfo` 在隐藏后仍返回范围/页/位置，自绘条同步不受影响）
        - 副作用（用户冒烟发现）：ListView 的滚轮是 comctl32 靠原生滚动条实现的，
          隐藏后滚轮失效，拖一下自绘条（走 `LVM_SCROLL`）才恢复 → 在 `PanelProc`
          里给 `g_listEp` / `g_ibList` 补自己的 WM_MOUSEWHEEL 分支（3 行 × `DSB_RowH` 像素滚动）
  - [x] **再合并小重复块**：`BatchFree`（批量队列 4 处释放）、`PanelTextColors`
        （4 处 CTLCOLOR 同款配色）、`HashBelongsToPod`（4 处孤儿清理查找）、
        `IbFillFromSelected`（输入框选中/激活两处填 URL）
        - exe 265,216 → 265,728 字节（+0.5KB）；-Os 早已把这几段相同代码交叉跳转合并，
          手工抽取只剩可读性收益，属已知现象

## 1.8 发布准备（已就绪）

- [x] 版本号 1.8.0 全量同步：main.c 关于弹窗中英两处、WinHTTP User-Agent 两处、
      resource.rc FILEVERSION/PRODUCTVERSION + 两语系各两处字符串
- [x] README.md / README.zh-CN.md 版本徽章 → 1.8.0，补上 1.8 新功能（章节、拖放、
      本地字幕、流式下载/断点续传、可读缓存名、清理对话框、字幕窗记忆/控制条）
- [x] 新增 `release-notes-1.8.md`（中英双语，链接 v1.8.0）
- [x] 清理临时文件 `1feeds.ini`；最终 exe 265,728 字节、零警告、版本 1.8.0.0
- [ ] 用户自行在 GitHub 发 Release，之后手动跑 `publish-sourceforge.ps1`

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
- 可选：把正在听的曲目同步到last.fm

### 播放队列 / 连续播放
### 标记已播/未播（可与收藏共用图标位）

## 明确不做

- **DSP（均衡器/实时音效）**：MFPlay 是黑盒，拿不到 PCM 回调；要 DSP 必须 IMFSourceReader 解码 + WASAPI 渲染重写播放引擎，体积风险工作量都上台阶，不划算
- **边下边播**：见"已拍板的决策"
- **跳过片头片尾 / 按播客自动缓存规则 / 睡眠定时淡出**：用户暂缓
