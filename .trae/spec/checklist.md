# Checklist: 第二阶段（项 4 / 7 / 8 / 9）

范围：托盘新剧集数字角标、剧集收藏、剧集即时过滤、每播客独立刷新间隔。
简介区滚轮问题不在本阶段范围（用户明确后续统一处理）。

- [x] `FillEpisodeList` 按过滤条件插入，并用 `g_epMap` 记录“行号 → 剧集下标”
- [x] 所有“行号当剧集下标”的读取点已改造：StartBatchCache、ChangeSort、HighlightEpisodeRow、剧集右键 LVM_HITTEST、LVN_ITEMCHANGED、NM_DBLCLK、NM_CUSTOMDRAW、BuildEpisodeTipText、WM_APP_EP_READY
- [x] NM_CUSTOMDRAW 中“行号”与“剧集下标”拆成 row / ep，选中态仍按行号判断
- [x] 收藏存储 FavHas/FavSet/FavDrop/FavPurgeOrphans/FavFreeAll 存在；`fav=` 配置可读写；LoadConfig 重置、RemovePodcast 删除、FEEDS_DONE 清理已接入
- [x] 过滤控件 IDC_EDIT_FILTER / IDC_BTN_FAVONLY 已创建、随字号重建字体、挂 tooltip
- [x] 剧集面板布局：顶部 24px 行（过滤框 + 星形开关 + 缓存按钮），列表下移 28px，滚动条同步
- [x] EN_CHANGE 即时过滤、星形按钮切换“只看已收藏”，均重填列表并同步滚动条
- [x] 星形按钮自绘：开启实心琥珀色，关闭灰描边；深色主题下过滤框背景为面板色
- [x] 剧集右键“收藏/取消收藏”生效，非缓存视图才出现；切换后单行刷新星标（只看收藏时整表重填）
- [x] `Podcast.refreshMin/lastRefreshMin` 与 `podrefresh=` 持久化；加载后由 PodInitRefresh 回填
- [x] `ArmAutoRefreshTimer` 改为 60 秒心跳；WM_TIMER 按全局/每播客到期分别触发，忙时跳过
- [x] 播客右键“刷新间隔”子菜单（跟随全局/关/30/60/120/360）可设置并即时生效
- [x] `MakeBadgeIcon` / `UpdateTrayBadge` 合成红底白字托盘图标，无新剧集恢复基础图标与提示
- [x] 角标在 FEEDS_DONE、FEED_ADDED、播客点选清角标后更新；WM_DESTROY 销毁角标图标
- [x] 新增 9 条 i18n 串在枚举与中英两张表中顺序一致
- [x] MinGW `-Wall -Wextra` 编译零警告零错误，LiteTune.exe 生成成功
- [ ] 用户实机测试（交叠布局、过滤输入、收藏持久化、托盘角标、每播客刷新）
