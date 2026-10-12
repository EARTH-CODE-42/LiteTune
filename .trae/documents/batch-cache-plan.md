# 批量缓存功能实现计划

## Context

用户希望右键点击播客列表中的某个播客，选择"批量缓存…"，输入数量 N 后，自动下载该播客最近的 N 集音频（已缓存的跳过）。当前只支持单集下载播放（`RequestPlayEpisode` → `EpisodeDlThread`），没有右键菜单入口和批量下载机制。

## 最终效果

1. **右键播客列表** → 弹出菜单"批量缓存…"
2. 点击后弹出输入框（复用 `AskInputBox`，mode=1 数字模式），提示"缓存最近几集？"
3. 输入 N（1-99），确认后：
   - 取该播客**最新 N 集**（列表已按发布时间倒序，取前 N 个）
   - 已缓存（`EpisodeIsCached()`）的跳过
   - 全部已缓存时提示"无需缓存"
   - 有需要下载的 → 状态栏显示"批量缓存 2/5…"，逐个下载
4. 下载过程中：
   - 状态栏实时显示进度"批量缓存 X/Y…"
   - 不打断当前播放（如果正在播放，批量缓存在后台静默下载）
   - 每集下完刷新列表（黄色缓存标记出现）
5. 全部完成后状态栏提示"批量缓存完成（成功 N 集）"

## 实现方案

### 1. 新增批量下载队列数据结构

```c
/* 批量缓存队列：不干扰正常播放下载，独立线程逐集下载 */
typedef struct {
    int      pod;        /* 播客下标 */
    int      total;      /* 总需下载数 */
    int      done;       /* 已完成数 */
    int      ok;         /* 成功数 */
    wchar_t *urls[64];   /* 待下载 URL 列表（最多 64，N 上限 99 但实际单批够用） */
    int      urlCount;
    int      curIdx;     /* 当前正在下载的索引 */
} BatchCacheCtx;
static BatchCacheCtx *g_batch = NULL;  /* UI 线程访问，同一时刻只有一个批量任务 */
```

### 2. 新增右键菜单（播客列表）

在 `PodListProc`（子类化窗口过程）中增加 `WM_RBUTTONUP` 处理：
- 命中播客行 → `CreatePopupMenu` → "批量缓存…" → `TrackPopupMenu`
- 选中 → 调用 `AskInputBox(tr(TR_BATCH_TITLE), 1)` 获取数量
- 数量合法（1-99）→ 调用 `StartBatchCache(pod, N)`

### 3. 批量下载逻辑（复用 `EpisodeDlThread` 的核心）

**关键设计：不复用 `DlJob`/`EpisodeDlThread`**，因为现有下载机制与播放强耦合（`g_dlGen` 作废、`g_curDlJob`、`g_downloading` 状态、`WM_APP_EP_READY` 里检查 `g_playUrl` 匹配）。批量缓存应该**不打断播放**。

**新写一个独立的 `BatchDlThread`**：
- 参数：`BatchCacheCtx*`
- 循环遍历 `urls[]`，对每个 URL：
  - `CachePathFor()` 算路径 → `GetFileAttributesW` 检查已存在 → 跳过
  - `HttpFetch(url, &len, 0, NULL, NULL)` 下载（无进度回调，不打扰 UI）
  - 写盘 `_wfopen(cache, L"wb")`
  - `PostMessage(g_hwnd, WM_APP_BATCH_DONE, idx, ok)` 通知 UI 刷新
- 全部完成后 `PostMessage(g_hwnd, WM_APP_BATCH_FINISHED, ok, total)`

### 4. 新增消息定义

```c
#define WM_APP_BATCH_DONE     (WM_APP + 13)  /* wParam=idx, lParam=ok */
#define WM_APP_BATCH_FINISHED (WM_APP + 14)  /* wParam=成功数, lParam=总数 */
```

### 5. UI 处理

- `WM_APP_BATCH_DONE`：更新 `g_batch->done++`，刷新剧集列表（`InvalidateRect(g_listEp)`），状态栏更新"批量缓存 X/Y…"
- `WM_APP_BATCH_FINISHED`：状态栏显示"批量缓存完成（成功 N/M 集）"，`free(g_batch)`，`g_batch = NULL`
- 如果批量下载进行中用户又点了新批量缓存 → 先 `WaitForSingleObject` 等旧线程结束（或直接拒绝"批量缓存进行中"）

### 6. 文案新增（TR 枚举）

```c
TR_BATCH_CACHE,     /* "批量缓存…" / "Batch Cache…" */
TR_BATCH_TITLE,     /* "缓存最近几集？" / "Cache latest N episodes?" */
TR_BATCH_LABEL,     /* "数量（1-99）" / "Count (1-99)" */
TR_BATCH_BAD,       /* "请输入 1-99 的数字" / "Enter 1-99" */
TR_BATCH_PROGRESS,  /* "批量缓存 %d/%d…" / "Batch caching %d/%d…" */
TR_BATCH_DONE,      /* "批量缓存完成（成功 %d/%d 集）" / "Batch done (%d/%d)" */
TR_BATCH_RUNNING,   /* "批量缓存进行中" / "Batch cache in progress" */
TR_BATCH_NO_NEED,   /* "全部已缓存" / "All cached" */
```

### 7. AskInputBox 适配

现有 `AskInputBox` mode=1 限定 1-600，批量缓存需要 1-99。方案：加一个 `g_ibMax` 变量，或新增 mode=2（数字模式，范围 1-99）。选用 mode=2 更清晰，在 `InputBoxProc` 的 OK 处理里加一个分支校验。

## 涉及文件

- `main.c`：全部改动集中在此
  - 数据结构（BatchCacheCtx、WM_APP_BATCH_* 定义）
  - `BatchDlThread` 函数
  - `StartBatchCache` 函数
  - PodListProc 里加 WM_RBUTTONUP 右键菜单
  - WndProc 里加 WM_APP_BATCH_DONE / WM_APP_FINISHED 处理
  - TR 枚举新增 + tr() 文案
  - AskInputBox mode=2 分支

## 验证

1. `windres resource.rc -O coff -o resource.o && gcc -O2 -s -mwindows -municode -Wall -Wextra -o LiteTune.exe main.c resource.o -lcomctl32 -lwinhttp -lmf -lmfplat -lmfplay -lmfreadwrite -lole32 -lgdiplus -lws2_32 -liphlpapi -lshell32` 零警告
2. 启动 ALIVE
3. 右键播客 → "批量缓存…" → 输入 3 → 状态栏显示"批量缓存 0/3…" → 逐集变黄 → "批量缓存完成（成功 3/3 集）"
4. 全已缓存的播客再批量缓存 → "全部已缓存"
5. 批量缓存进行中再右键 → "批量缓存进行中"（拒绝）
