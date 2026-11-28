# LiteTune 1.9 电台重构与零卡顿方案

## Context

上一轮只做了"电台套在播客壳里"的表面改动，留下 7 个问题：切台卡 UI、体积涨到 275,968 B、码率不进播放条、tooltip 过大、电台/曲目历史不持久、内置台写死、右键与删除交互错乱（电台右键弹播客菜单、右侧简介变播客内容）。

本轮按"电台与播客只在界面上相似，数据与代码路径彻底分离"的原则重构，并把 Media Foundation 播放器移出 UI 线程，做到切台/切播客时 UI 完全不等待。

用户已确认：
- 添加电台：名称 + 地址两个输入框；名称留空则自动取名；后续可右键改台名。
- 启动时自动播放上次电台。
- 体积：重构后约 272 KB 可接受，目标是**没有冗余代码**，每步编译后汇报实际体积。

## 一、播放器线程化（零卡顿，最优先）

现状：`PlayerPlayFile` 在 UI 线程 `Release` 旧播放器再 `MFPCreateMediaPlayer`，打开/关闭网络流时阻塞 UI 约 1 秒（`main.c` L7058-7081）。探测线程只解决了"连不上"，没解决播放器本身。

改法：MFPlay 播放器归一个专用播放线程所有，UI 线程只投递命令，永不阻塞。

- 新增全局：`g_pbWnd`（播放线程的消息窗）、`g_pbTid`、`volatile LONG g_pbPosSec/g_pbDurSec`（秒，供 UI 读）、`g_pbReady` 事件。
- 新增 `PbThread`：`CoInitializeEx(STA)` → 注册窗口类 → 创建 `HWND_MESSAGE` 窗口 → `SetEvent(g_pbReady)` → 消息循环。窗口过程处理统一命令：
  ```c
  typedef struct { int cmd; wchar_t *url, *title; long long i64; int i32; } PbCmd;
  enum { PB_PLAY=1, PB_PAUSE, PB_RESUME, PB_STOP, PB_SEEK, PB_VOL, PB_MUTE, PB_RATE, PB_QUIT };
  ```
  仅第一次 `PLAY` 时 `MFPCreateMediaPlayer(NULL, FALSE, MFP_OPTION_NONE, &g_pc, 播放线程窗口, &g_player)`；之后一律
  `CreateMediaItemFromURL` + `SetMediaItem(item, TRUE)` 复用播放器（不 Release，避免网络流拆链阻塞）。
- 播放线程 `SetTimer` 200ms 读 `GetPosition/GetDuration` 写入 `g_pbPosSec/g_pbDurSec`；UI 的 `PlayerGet100ns` 改为读这两个原子值（秒×10^7），不再跨线程调 COM。
- UI 侧全部改投递：`PlayerPlayFile/PlayerStop/PlayerSeekToFrac/SeekToSec/ApplySpeed/ToggleMute/SetVolumeValue/PauseResume`、MFP PLAY 事件里的 `SetPosition`。
- `PauseResume`/`SetVolumeValue` 里 `if (g_player)` 的判断改成 UI 可见状态（`g_playState`/`g_pbActive`）。
- `WM_APP_MF_EVENT` 的 ERROR 分支：清电台播放标记、`IcyStop`、清 `g_stFormat`、状态栏提示。
- `WM_DESTROY`：投递 `PB_QUIT`（线程内 Release 播放器、销毁窗口），`WaitForSingleObject(≤3s)`；删掉 UI 线程里的 `g_player->Release`。
- 删除 `RadioProbeThread`、`WM_APP_RADIO_PROBE`、`g_radioProbeIdx`（播放线程已天然异步，探测线程是冗余）。
- 注意：`PC_OnMediaPlayerEvent` 仍只 PostMessage 到主窗，无需改。

## 二、电台独立数据模型

- 新类型（替换 `g_radioPod` / `kRadioStations` / `LoadRadioPodcast`）：
  ```c
  #define MAX_RADIOS 64
  #define RADIO_HIST_MAX 30        /* 每台保留最近 30 首，控制 ini 体积 */
  typedef struct {
      wchar_t name[128];
      wchar_t url[512];
      wchar_t *hist[RADIO_HIST_MAX];   /* 每项 "HH:MM\t曲目" */
      int histCount;                   /* 0..MAX，最旧在下标 0 */
  } RadioStation;
  static RadioStation g_radios[MAX_RADIOS];
  static int g_radioCount, g_radioSel = -1, g_radioPlayIdx = -1;
  static wchar_t g_radioCurUrl[512];
  ```
- 函数：`AddRadio(name,url)`、`RemoveRadio(i)`、`MoveRadioTo(from,to)`、`RadioFindByUrl`、`RadioNameFromUrl`（自动取名：取 `://` 后到 `/` 的主机名）、`RadioHistAppend(i, line)`、`RadioHistClearAll`。
- `ActivePodcast()`：`g_activeTab==1` 时返回 `NULL`（所有播客路径自动 no-op，不再借用 `g_radioPod`）。
- 删除 `g_tipIsRadio` 对 `g_radioPod` 的引用，改 `g_radios`。

## 三、电台界面与交互

- `FillRadioList`：只用 `g_radios`；恢复 `g_radioSel` 选中；空列表时状态栏提示"右键添加电台"。
- `ShowRadioDetail(idx)`：清空/清 `g_epMap`，改用 `SetRadioColumns()`（曲目/时间），把 `hist[]` 逐行填入；右侧简介 = 台名 + 地址（若正播放且已有 ICY 头则附台名/类型/码率/格式/主页）。
- 切回播客 tab：`SetEpisodeColumns(0)`，恢复播客列头。
- 行自绘 `WM_DRAWITEM`：电台行读 `g_radios`，保留蓝色竖条（仅播放中），多选由 `ODS_SELECTED` 底色体现。
- tooltip：`UpdatePodTipFromPoint` 显示前按类型 `SetWindowPos` 改尺寸——电台约 360×72（有"正在播放"时 94），播客保持 440×160；`PodTipProc` 电台分支缩小内边距，并显示码率/格式（若有）。
- 播放条：ICY 头到达时（且是该播放任务）把 `MP3 · 128 kbps` 这类文本写入 `g_stFormat`；`PlayerStop` 时清空。
- 右键 `WM_CONTEXTMENU`（`g_listPod`）按 tab 完全分流：
  - 播客空白：添加订阅（现状）；播客行：原菜单，多选时追加"删除 N 个播客"。
  - 电台空白：添加电台；电台行：播放 / 修改台名 / 删除（多选时"删除 N 个电台"）。
  - 电台分支**绝不调用** `SelectPodcast`，只 `ShowRadioDetail`。
- 双击电台 `PlayRadioStation`：投递 `PB_PLAY` + `IcyStart`，不再探活；设置 `g_radioPlayIdx/g_radioSel`，刷新列表与状态栏。
- ICY 消息：`WM_APP_ICY_META` 始终更新 `g_icyCurTitle`/状态栏并 `RadioHistAppend`；仅当"电台 tab 且选中该台"时才往中间列表追加行。`WM_APP_ICY_HEADERS` 不再依赖当前 tab，按 `g_icyJob` 判归属，更新简介与播放条码率。

## 四、添加 / 改名电台（输入框扩展）

`AskInputBox`（`main.c` L10473）扩展：
- mode 3：改台名，单输入框，预填 `g_ibBuf`。
- mode 4：添加电台，两个输入框（名称 `ID_IB_EDIT2` 可空 + 地址 `ID_IB_EDIT`），窗口加高约 42px；地址必须含 `://`，重复地址/超过 `MAX_RADIOS` 给出提示。
- 两个 Labу 文案走 `tr()`：名称（留空自动取名）、流地址。
- 添加后：`AddRadio` + `FillRadioList`，选中新台。

## 五、去掉 × 按钮 / Ctrl 多选批量删除 / 拖动排序

- 删除 `IDC_BTN_DEL` 按钮：创建、LayoutControls、tooltip、`WM_DRAWITEM` 分支、`WM_COMMAND`；删除 `g_btnDel`。
- 彻底删除删除模式：`g_podMode`、`SetPodMode`、`PodListModeClick`、`WM_DRAWITEM` 封面上画大红叉的分支、tooltip 里的 `g_podMode` 判断；删除 `TR_TIP_DEL` 字符串。工具栏固定按钮数从 4 改 3，`LayoutControls` 的 `fixedBtns` 重算。
- 列表加 `LBS_EXTENDEDSEL`；工具栏"+"按当前 tab 分流：播客=添加订阅，电台=添加电台。
- 批量删除：
  - `DeleteSelectedPods()`：先 `LB_GETSELCOUNT/LB_GETSELITEMS` 收集下标，跳过"已缓存"虚拟行，倒序调用 `RemovePodcast`。
  - `DeleteSelectedRadios()`：同样收集，倒序 `RemoveRadio`，最后 `FillRadioList`。
  - 触发：列表 `WM_KEYDOWN VK_DELETE`（PanelProc 内）或右键菜单项。
- 拖动排序：
  - 复用现有 `POD_DRAG_TIMER` 轮询拖拽；`g_activeTab==1` 时移动 `g_radios`（`MoveRadioTo` 后重填列表并保持选中），播客仍走 `MovePodcastTo`。
  - 按住 Ctrl/Shift 时不启动拖拽（让位给多选）；`ListRowCount/MoveRowTo` 两个小分发函数。

## 六、ini 持久化与启动自动播放

`BuildConfigText` / `ParseConfigLine` 增加（电台数据量小，随配置走，不新增文件）：
```
[radio]
radio=<utf8 台名>\t<utf8 地址>
hist=<url hash 8hex>,<HH:MM>\t<utf8 曲目>
radiocur=<utf8 当前台地址>
radioactive=1
```
- `radio=` → `AddRadio`；`hist=` 按 `CacheUrlHash(url)` 找台，找不到丢弃；`radiocur`/`radioactive` 存到 `g_radioCurUrl`、`g_radioAutoPlayCfg`。
- `LoadConfig` 重置电台数组；`SaveConfig` 电台正在播放时写 `radiocur/radioactive`，并且**跳过播客 resume 段**（避免把旧播客当成"上次播放"）。
- `WM_CREATE` 末尾 `PostMessage(WM_APP_RADIO_AUTOSTART)`：若 `g_radioAutoPlayCfg` 且地址能匹配到台，则 `SwitchTab(1)` + 选中 + `PlayRadioStation`。
- `WM_APP_FEEDS_DONE` 调 `TryResumePlayback` 前判断：电台已在播放则跳过播客续播。

## 七、体积与死代码对账

- 预计删除：`RadioProbeThread`（约 60 行重复 WinHTTP）、`g_radioPod/LoadRadioPodcast/kRadioStations`、删除模式整套、`TR_TIP_DEL`。
- 预计新增：播放线程约 150 行、电台模型约 120 行、输入框第二行约 40 行。
- 每完成一节编译一次，汇报 `LiteTune.exe` 字节数；最终目标 ≤ 约 274 KB 且无冗余路径；若明显超出，再列"可砍项"给用户选（不擅自删功能）。

## 修改文件

- 只改 `c:\Users\15923\Desktop\lightpodcast\main.c`（编译 `resource.rc` 不变）。
- 不改 README/版本号（非发版）。

## 验证

1. 编译（零警告）：
   ```powershell
   .\mingw32\bin\windres.exe resource.rc -O coff -o resource32.o
   .\mingw32\bin\gcc.exe -Os -s -mwindows -municode -std=gnu17 -Wall -Wextra -D_WIN32_WINNT=0x0601 -fno-unwind-tables -fno-asynchronous-unwind-tables -Wl,--subsystem,windows:6.01 -o LiteTune.exe main.c resource32.o -lwinhttp -lwinmm -lole32 -luuid -lshlwapi -lcomctl32 -lshell32 -lpropsys -lws2_32 -liphlpapi -lgdiplus -ladvapi32 -lcomdlg32 -lmf -lmfplat -lmfplay -lmfreadwrite
   ```
2. 记录 `Get-Item LiteTune.exe` 字节数并与 275,968 B 对比，汇报。
3. 交用户冒烟（不自行截图）：连点三台/播客↔电台切换时 UI 不冻结；电台右键只出电台菜单、简介不串；码率进播放条；tooltip 尺寸正常；添加/改名/多选删除/拖动排序；重启后电台列表、上次电台自动播放、曲目历史都在。
