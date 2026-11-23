# 修复：桌面字幕开启时点“章节”变成空白

## 背景 / 根因

桌面字幕窗（`SubBarText`）与简介区列表共用 `g_subCues`，`g_subKind` 标识当前内容（1=字幕 2=章节）。

- 点「章节」→ `LoadChapters` 异步下载 → `ParseChaptersJson` 先 `FreeSubCues()`
- 该集没有 `chaptersUrl` 或下载/解析失败时，数组一直是空的
- 空数组时 `SubBarText()` 返回 NULL，桌面字幕窗什么都不画 → 看起来“变空白”
- 简介区 tab 有占位提示，但桌面字幕窗没有

按用户意见：**保留共用数组**，不强制“点章节也显示字幕”（有人就想在桌面上看章节）；空白时在 tab 与桌面字幕框给出“（本集没有字幕）/（本集没有章节信息）”提示即可。

## 方案（只改 main.c，净增约 10 行）

### 1. `LoadSubForCurTab()`（约 L8143）

恢复“按 tab 切换内容类型”的简洁结构，避免字幕/章节串台，也修正上一次编辑多出来的括号块：

```c
want = (g_subInDesc == 2) ? 2 : 1;
if (g_subKind != want) {          /* 切 tab 时清掉另一种内容，空列表走占位提示 */
    FreeSubCues();
    free(g_subTransUrl);
    g_subTransUrl = NULL;
}
if (want == 2) {
    if (e->chaptersUrl && e->chaptersUrl[0])
        LoadChapters(e->url, p->title, e->title, e->chaptersUrl);
    return;                       /* 没有章节地址就保持空，由占位/桌面提示兜底 */
}
/* want == 1：原字幕加载分支（localFile / SubOv / transcriptUrl） */
```

- tab=0（简介）且正在播放时同样按 want=1 准备字幕，桌面字幕窗才有内容
- 章节为空不再“假装有数据”，面板和桌面窗都走提示

### 2. 桌面字幕窗空白提示（`SubBarProc` 的 `WM_PAINT`，约 L11098）

```c
const wchar_t *txt = SubBarText();
if (!txt || !txt[0])
    txt = tr(g_subInDesc == 2 ? TR_PH_CHAPTERS : TR_PH_SUBS);
```

- 复用现有翻译条目（中文“（本集没有章节信息）/（本集没有字幕）”，英文已存在），**不新增任何字符串**
- 提示复用现有居中绘制逻辑，无新增绘制代码

### 3. 简介区列表占位（已有，无需改）

`SubRefreshUI` 里 `g_subCueCount == 0 && g_subInDesc != 0` 已经会显示
`TR_PH_CHAPTERS / TR_PH_SUBS` 占位行；配合第 1 步的同步清理，不会再出现“章节 tab 里显示字幕”的串台。

### 4. 不改的部分

- 不拆数组、不新增 `g_chapCues`、不加 `PanelKindOK` 之类 guard（同步清空后不存在 kind 与 tab 不一致的持久状态），控制代码量
- `SubTick` / `SubBarText` 继续按共用数组工作：章节加载完成后桌面窗自然显示章节标题
- 不新增翻译条目、不做额外布局改动

## 验证

1. `windres` + `gcc -Os -Wall -Wextra` 编译，必须零警告；对比 exe 体积（应基本不变，净增约 10 行）
2. 交给用户手动测试：
   - 播放带字幕剧集、开桌面字幕 → 点「章节」→ 有章节则显示章节标题；无章节则显示“（本集没有章节信息）”，不再空白
   - 章节 tab 无数据时列表显示占位行，不显示上一集/字幕内容
   - 切回「字幕」tab 正常恢复字幕；无字幕时 tab 与桌面框都提示“（本集没有字幕）”
   - 桌面字幕窗滚轮切条、点击 seek 仍正常
3. 在 `TODO-1.8.md` 记录该修复
