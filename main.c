/*
 * LiteTune - 纯 C / Win32 播客播放器
 *
 * 功能：
 *   - 配置双模式（内嵌 exe 尾部 PE overlay / 外挂同目录 feeds.ini）：启动时
 *     由磁盘现状决定——有 feeds.ini 用外挂，否则用内嵌尾部，两者皆无的首启
 *     弹窗询问；设置菜单可随时切换，并支持导入/导出 OPML 订阅列表与配置 ini
 *   - 后台线程下载并解析 RSS 2.0（含 itunes 扩展）
 *   - 音频先下载到 cache\ 缓存，再用 Media Foundation 播放，支持离线收听
 *   - 显示码率 / 采样率 / 声道 / 格式 / 进度，支持 seek、暂停、音量
 *   - 亮色/暗色主题、中英文双语、定时关机、字号调节、任务栏缩略图按钮
 *
 * 编译（MinGW）:
 *   windres resource.rc -O coff -o resource.o
 *   gcc -O2 -s -mwindows -municode -o LiteTune.exe main.c resource.o \
 *       -lcomctl32 -lwinhttp -lmf -lmfplat -lmfplay -lmfreadwrite -lole32 -lgdiplus -lws2_32 -liphlpapi -lshell32
 */
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WINVER       0x0601
#define _WIN32_WINNT 0x0601

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <winhttp.h>
#include <propidl.h>
#include <propsys.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfplay.h>
#include <mfreadwrite.h>
#include <gdiplus.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <math.h>
#include <share.h>
#include <time.h>

/* 只需 3 个音频属性键，手工定义以避免引入体积庞大的 propkey.h */
static const PROPERTYKEY PKEY_Title_L =
    { {0xf29f85e0,0x4ff9,0x1068,{0xab,0x91,0x08,0x00,0x2b,0x27,0xb3,0xd9}}, 2 };
static const PROPERTYKEY PKEY_Music_Artist_L =
    { {0x56a3372e,0xce9c,0x11d2,{0x9f,0x0e,0x00,0x60,0x97,0xc6,0x86,0xf6}}, 2 };
static const PROPERTYKEY PKEY_Music_AlbumTitle_L =
    { {0x56a3372e,0xce9c,0x11d2,{0x9f,0x0e,0x00,0x60,0x97,0xc6,0x86,0xf6}}, 4 };
static const PROPERTYKEY PKEY_Media_Duration_L =
    { {0x64440490,0x4c8b,0x11d1,{0x8b,0x70,0x08,0x00,0x36,0xb1,0x1a,0x03}}, 3 };

/* 只用到这 9 个 Media Foundation GUID，手工定义，不链接整个 mfuuid
 * 静态库（那会带入数百个 GUID 和属性名字符串，exe 多约 60KB）。
 * 头文件里的 DEFINE_GUID 在未定义 INITGUID 时只是 extern 声明，
 * 所以这里用自带的“定义型”宏真正分配存储。 */
#define DEFINE_GUID_L(name,l,w1,w2,b1,b2,b3,b4,b5,b6,b7,b8) \
    const GUID DECLSPEC_SELECTANY name = { l, w1, w2, { b1,b2,b3,b4,b5,b6,b7,b8 } }
DEFINE_GUID_L(MFAudioFormat_PCM,  0x00000001,0x0000,0x0010,0x80,0x00,0x00,0xaa,0x00,0x38,0x9b,0x71);
DEFINE_GUID_L(MFAudioFormat_MP3,  0x00000055,0x0000,0x0010,0x80,0x00,0x00,0xaa,0x00,0x38,0x9b,0x71);
DEFINE_GUID_L(MFAudioFormat_AAC,  0x00001610,0x0000,0x0010,0x80,0x00,0x00,0xaa,0x00,0x38,0x9b,0x71);
DEFINE_GUID_L(MFAudioFormat_WMAudioV8,0x00000161,0x0000,0x0010,0x80,0x00,0x00,0xaa,0x00,0x38,0x9b,0x71);
DEFINE_GUID_L(MFAudioFormat_WMAudioV9,0x00000162,0x0000,0x0010,0x80,0x00,0x00,0xaa,0x00,0x38,0x9b,0x71);
DEFINE_GUID_L(MF_MT_SUBTYPE,      0xf7e34c9a,0x42e8,0x4714,0xb7,0x4b,0xcb,0x29,0xd7,0x2c,0x35,0xe5);
DEFINE_GUID_L(MF_MT_AUDIO_AVG_BYTES_PER_SECOND,0x1aab75c8,0xcfef,0x451c,0xab,0x95,0xac,0x03,0x4b,0x8e,0x17,0x31);
DEFINE_GUID_L(MF_MT_AUDIO_NUM_CHANNELS,0x37e48bf5,0x645e,0x4c5b,0x89,0xde,0xad,0xa9,0xe2,0x9b,0x69,0x6a);
DEFINE_GUID_L(MF_MT_AUDIO_SAMPLES_PER_SECOND,0x5faeeae7,0x0290,0x4c31,0x9e,0x8a,0xc5,0x34,0xf6,0x8d,0x9d,0xba);
/* IID_IPropertyStore：propsys 用，避免链接整个 libuuid（含数百 GUID，约 50KB） */
DEFINE_GUID_L(IID_IPropertyStore_L,0x886d8eeb,0x8cf2,0x4446,0x8d,0x02,0xcd,0xba,0x1d,0xbd,0xcf,0x99);
#define IID_IPropertyStore IID_IPropertyStore_L
/* 任务栏缩略图工具栏（ITaskbarList3）所需 GUID，同样手工定义 */
DEFINE_GUID_L(CLSID_TaskbarList_L,0x56fdf344,0xfd6d,0x11d0,0x95,0x8a,0x00,0x60,0x97,0xc9,0xa0,0x90);
DEFINE_GUID_L(IID_ITaskbarList3_L,0xea1afb91,0x9e28,0x4b86,0x90,0xe9,0x9e,0x9f,0x8a,0x5e,0xef,0xaf);
#define CLSID_TaskbarList CLSID_TaskbarList_L
#define IID_ITaskbarList3 IID_ITaskbarList3_L

/* ================= 常量 ================= */
#define MARGIN    8    /* 区域外边距 */
#define GAP_W     8    /* 区域间距（即可拖动分隔条的宽度） */
#define TOP_H     88   /* 顶部播放区高度（含边框） */
#define MAX_PODCASTS    64
#define MAX_FEED_LINE   2048
#define DL_BUF_SIZE     65536

#define WM_APP_FEED_ADDED   (WM_APP + 1)   /* lParam = Podcast* */
#define WM_APP_FEEDS_DONE   (WM_APP + 2)
#define WM_APP_EP_READY     (WM_APP + 3)   /* lParam = EpReady* */
#define WM_APP_DL_PROGRESS  (WM_APP + 4)   /* wParam = 0..100 */
#define WM_APP_MF_EVENT     (WM_APP + 5)   /* wParam = MFP_EVENT_TYPE, lParam = HRESULT */

/* 滑块几何消息（部分旧版 commctrl.h 未声明） */
#ifndef TBM_GETTHUMBRECT
#define TBM_GETTHUMBRECT    (WM_USER + 25)
#endif
#ifndef TBM_GETCHANNELRECT
#define TBM_GETCHANNELRECT  (WM_USER + 26)
#endif

/* ================= 数据结构 ================= */
typedef struct {
    wchar_t *title;
    wchar_t *desc;
    wchar_t *url;        /* enclosure URL（离线视图里是本地 mp3 完整路径） */
    wchar_t *author;     /* itunes:author（离线视图里是播客名称） */
    wchar_t  dateText[24];
    long long dateKey;   /* YYYYMMDDHHMM 分钟级，0=未知 */
    int      durationSec;
    int      origIdx;    /* RSS 原始顺序，排序 tiebreak */
    int      localFile;  /* 1=离线缓存视图：url 为本地路径，直接播放不联网 */
} Episode;

typedef struct {
    wchar_t *title;
    wchar_t *desc;
    wchar_t *author;     /* itunes:author 频道级 */
    wchar_t *lang;       /* language */
    wchar_t *link;       /* 频道主页 */
    wchar_t *copyright;
    char    *imageUrl;   /* 封面图 URL */
    char    *feedUrl;    /* 该播客的 RSS 订阅源地址 */
    HBITMAP  hImage;     /* 已解码的封面图，NULL=未下载或失败 */
    Episode *eps;
    int      epCount;
    int      epCap;
    int      loadFailed; /* 1=订阅获取/解析失败的占位条目（无剧集，可删除/重试） */
} Podcast;

typedef struct {
    int pod;
    wchar_t *url;   /* 按 URL 标识，排序导致索引变化也不受影响 */
    int ok;
    long gen;       /* 下载代号，过期任务的完成消息会被丢弃 */
} EpReady;

/* 排序列 */
enum { SORT_TITLE = 0, SORT_DATE = 1, SORT_DUR = 2, SORT_PODCAST = 3 };

/* ================= 控件 ID ================= */
enum {
    IDC_ST_TIME = 100, IDC_ST_FORMAT, IDC_ST_VOL,
    IDC_SLD_SEEK, IDC_SLD_VOL,
    IDC_BTN_PREV, IDC_BTN_PLAY, IDC_BTN_NEXT, IDC_BTN_STOP, IDC_BTN_MUTE,
    IDC_BTN_ADD, IDC_BTN_DEL, IDC_BTN_REFRESH, IDC_BTN_PLAYMODE,
    IDC_BTN_DIR, IDC_BTN_SPEED, IDC_BTN_CACHE,
    IDC_LIST_POD, IDC_LIST_EP, IDC_EDIT_DESC,
    IDC_SB_POD, IDC_SB_EP, IDC_SB_DESC, IDC_SB_EP_H,
    IDC_TAB_DESC
};

/* ================= 全局状态 ================= */
static Podcast  g_pods[MAX_PODCASTS];
static int      g_podCount = 0;
static int      g_curPod = -1, g_curEp = -1;

/* “已缓存”虚拟播客：聚合当前缓存目录里所有 mp3，断网也可浏览播放。
 * 固定挂在播客列表最后一行，下标哨兵 = g_podCount */
static Podcast  g_cachePod;
static int      g_viewCached = 0;     /* 剧集列表当前是否为缓存视图 */
static int      g_cacheRowHere = 0;   /* 列表框里是否已追加“已缓存”行 */

static HWND     g_hwnd;   /* 主窗口句柄 */
static HFONT    g_font;          /* 8pt+级别 常规 */
static HFONT    g_fontBold;      /* 8pt+级别 加粗（播客标题） */
static HFONT    g_fontSmall;     /* 7pt+级别 灰色小字（副标题/格式串） */
static HFONT    g_menuFont;      /* 系统标准菜单字体（SPI_GETNONCLIENTMETRICS.lfMenuFont） */
static HBRUSH   g_whiteBrush = NULL;
static HWND     g_sldSeek, g_stTime;
static HWND     g_btnPrev, g_btnPlay, g_btnNext, g_btnStop, g_btnMute;
static HWND     g_sldVol, g_stVol, g_stFormat;
static HWND     g_listPod, g_listEp, g_editDesc;
static HWND g_vsPod, g_vsEp, g_vsDesc, g_hsEp;   /* 自绘滚动条（hs=剧集水平） */
static RECT g_rcPanel[3];   /* 下方三面板矩形（含滚条带），WM_PAINT 统一画边框 */
static HWND     g_btnAdd, g_btnDel, g_btnRefresh, g_btnPlayMode;
static HWND     g_btnDir;
static HWND     g_btnSpeed;        /* 倍速按钮，显示当前倍率如 1.0x */
static HWND g_btnCache;        /* 批量缓存：Ctrl+多选剧集后点击下载 */
/* 简介折叠/展开小按钮（自绘，悬停才显示） */
static HWND     g_btnDescTab;
static int      g_descHidden;      /* 1=简介区隐藏，剧集占满右侧 */
static int      g_descTabShown;    /* 小按钮当前是否已显示（避免反复 ShowWindow） */
static RECT     g_rcDescTab;       /* 小按钮矩形（LayoutControls 写入） */

/* 倍速：整数存 100 倍，避免浮点；默认 100=1.0x */
static int      g_speed = 100;
/* 托盘图标已创建标志 */
static int      g_trayAdded = 0;
static HICON    g_trayIco = NULL;    /* 托盘小图标（资源加载，退出时销毁） */
#define WM_TRAYICON (WM_APP + 100)

/* 媒体键：多数键盘发 WM_APPCOMMAND 广播而非 VK_MEDIA_* 虚拟键，
 * 必须监听 WM_APPCOMMAND 才能像其它播放器一样被键盘/耳机线控控制 */
#ifndef GET_APPCOMMAND_LPARAM
#define GET_APPCOMMAND_LPARAM(lParam) ((short)HIWORD(lParam) & FAPPCOMMAND_MASK)
#endif

/* 托盘迷你控制浮窗：4 个按钮（上一曲/播放暂停/下一曲/随机下一曲），
 * 体验对齐任务栏缩略图工具栏；左键单击托盘图标弹出，再点收起 */
#define FLY_CLASS L"LiteTuneFlyout"
static HWND     g_fly = NULL;        /* 浮窗（popup，平时隐藏） */
static int      g_flyHover = -1;     /* 悬停的按钮下标，-1=无 */
#define FLY_PAD   6                  /* 浮窗内边距 */
#define FLY_BTN   32                 /* 按钮边长 */
#define FLY_GAP   2                  /* 按钮间距 */
#define FLY_W   (FLY_PAD * 2 + FLY_BTN * 4 + FLY_GAP * 3)
#define FLY_H   (FLY_PAD * 2 + FLY_BTN)

/* 缓存目录：默认程序目录下 cache，可由用户更改，退出时存入 ini */
static wchar_t  g_cacheDir[MAX_PATH] = L"";
static int      g_cacheDays = 30;   /* 自动清理天数：0=关（配置 cachedays，默认 30） */

/* 工具提示 */
static HWND     g_hwndTip = NULL;        /* 系统 tooltip：按钮 */
static HWND     g_hwndPodTip = NULL;     /* 自定义 tooltip：播客封面+标题+作者 */
static HWND     g_hwndEpTip = NULL;      /* 剧集列表 tracking tooltip（不依赖标题截断） */
static int      g_tipPodIdx = -1;        /* 当前播客提示对应的条目下标 */

/* 顶栏状态文本（自绘跑马灯） */
static wchar_t  g_statusText[512] = L"就绪";   /* 当前显示内容 */
static wchar_t  g_statusBase[512] = L"就绪";   /* 持久内容（如“正在播放：…”），临时提示结束后恢复 */
static int      g_mqOffset = 0;       /* 滚动偏移（像素） */
static int      g_mqTextW = 0;        /* 状态文本像素宽，-1=需重测 */
static int      g_mqActive = 0;       /* 文本超长，正在滚动 */

#define TEMP_STATUS_MS 2600           /* 临时提示持续时间 */
static int      g_statusTempOn = 0;   /* 临时提示正在显示（定时器4在跑） */
static wchar_t  g_batchSavedBase[512] = L"";  /* 批量缓存前的 base（完成后恢复） */
static int      g_batchActive = 0;  /* 批量缓存进行中：SetStatus 重定向到 saved base */

/* 播客列表模式：0=普通 1=删除 2=排序（互斥） */
static int      g_podMode = 0;
static int      g_muted = 0;          /* 静音 */
static int      g_playMode = 0;       /* 播放模式：0=顺序 1=随机 2=单曲循环 */

/* feeds.txt 中的订阅源（UTF-8）与排序/布局配置 */
static char   **g_feedUrls = NULL;
static int      g_feedUrlCount = 0;
static int      g_sortCol = SORT_DATE;   /* 默认按日期 */
static int      g_sortDir = -1;          /* -1=降序 1=升序 */
static int      g_w1perm = 323;          /* 播客列宽，千分比 */
static int      g_w2perm = 383;          /* 剧集列宽，千分比 */
static int      g_volume = 80;           /* 音量 0-100 */
static int      g_winW = 480, g_winH = 360;  /* 窗口外框尺寸（含标题栏边框），用于保存/恢复 */
static int      g_winX = CW_USEDEFAULT, g_winY = CW_USEDEFAULT;  /* 窗口左上角，CW_USEDEFAULT=首次运行 */
static int      g_colW[3] = { 110, 88, 44 };  /* 剧集列表三列像素宽 */
static int      g_fontLevel = 0;  /* 字号级别 0=小(默认) 1=中 2=大，全局字体随级别+1pt */
static int      g_cfgExternal = 0;       /* 配置存储模式：1=外挂同目录 feeds.ini 0=内嵌 exe 尾部。
                                          * 仅运行时依据磁盘现状（有 feeds.ini 即外挂）得出，不写进配置 */
static int      g_skipSave = 0;          /* 导入配置后退出：不能让旧内存状态覆盖刚导入的尾部 */
static int      g_darkMode = 1;          /* 夜间模式（存 ini：dark=1，默认开） */
static int      g_lang = 1;              /* 界面语言：0=简体中文 1=English（存 ini：lang，默认英文） */
static HBRUSH   g_bgBrush = NULL;        /* 主窗口背景刷，随主题重建 */

/* 定时关机：g_shutdownAt=UTC 秒（0=未启用），倒计时显示在标题栏 */
static long long g_shutdownAt = 0;

/* 任务栏缩略图工具栏（上一曲/播放暂停/下一曲/随机下一曲） */
static ITaskbarList3 *g_taskbar = NULL;
static UINT   g_wmTaskbarBtnCreated = 0;
static HICON  g_tbIco[5] = { NULL };   /* 0=prev 1=play 2=pause 3=next 4=shuffle */

static ULONG_PTR g_gdipToken = 0;

static volatile int g_feedsLoading = 0;
static volatile int g_downloading  = 0;
static int      g_fullReload = 0;    /* 本次加载是否为全量刷新（区别于单条添加） */

/* 播放 */
static IMFPMediaPlayer *g_player = NULL;
static int  g_playState = 0;          /* 0=停止 1=播放 2=暂停 */
static int  g_seeking   = 0;
static int  g_playPod = -1;         /* 正在播放的播客下标（哨兵 g_podCount=离线） */
static int  g_playEp  = -1;         /* 正在播放的剧集下标，独立于列表选中行 */
static wchar_t *g_playUrl = NULL;   /* 当前请求播放的音频 URL */
static long long g_dur100ns = 0;
static int  g_posSec = 0, g_durSec = 0;  /* 500ms 定时器缓存的进度（秒），供手机 /status 读 */

/* 续播记忆：退出时把 g_playUrl+位置写进 ini，下次启动 feeds 加载完自动续播 */
static wchar_t  *g_resumeUrl = NULL;      /* 待续播的剧集 URL/本地路径 */
static long long g_resumeSec = 0;         /* 待续播的位置（秒） */
static long long g_resumeSeek100ns = 0;   /* 播放开始事件里一次性消费的待定位 */
static int       g_resumePaused = 0;      /* 退出时是暂停：重开后定位到位但保持暂停 */

/* 每播客续播标记：记住每个订阅“上次听到哪”（剧集 URL + 秒数）。
 * 键用 feed URL 的 FNV-1a 哈希（列表下标会因增删/排序漂移，URL 不会）；
 * ini 行 lastep=<hex>,<sec>,<utf8url>，启动时清掉对不上现有订阅的孤儿记录。 */
typedef struct {
    unsigned int  hash;
    wchar_t      *epUrl;
    long long     sec;
} LastEp;
static LastEp  *g_lastEps = NULL;
static int      g_lastEpCount = 0, g_lastEpCap = 0;

/* 手机播放/遥控 HTTP 服务 */
/* 端口优先用 80（Windows 防火墙常已放行，手机最容易连上），占用再依次降级 */
static const unsigned short PHONE_PORTS[] = { 80, 8080, 17532 };
static CRITICAL_SECTION g_nowCs;                      /* 保护下面两个“正在播放”字段 */
static wchar_t g_nowPath[MAX_PATH] = L"";             /* 正在播放的本地 mp3 全路径 */
static wchar_t g_nowTitle[256]     = L"";             /* 正在播放的剧集标题 */
static SOCKET  g_httpSock  = INVALID_SOCKET;
static HANDLE  g_httpThread = NULL;
static volatile int g_httpStop = 0;
static unsigned short g_httpPort = 0;                 /* 实际绑定成功的端口 */
#define WM_APP_REMOTE_SEEK (WM_APP + 6)   /* wParam: 0=后退15秒 1=前进15秒 */
#define WM_APP_REMOTE_SHUT (WM_APP + 7)   /* wParam: 定时分钟数，0=取消 */
#define WM_APP_REMOTE_POS  (WM_APP + 8)   /* wParam: 定位千分比 0..1000（手机进度条拖动） */
#define WM_APP_REMOTE_VOL  (WM_APP + 9)   /* wParam: 音量 0..100（手机音量条） */
#define WM_APP_REMOTE_PLAYEP (WM_APP + 10) /* wParam=pod lParam=ep（手机点选剧集） */
#define WM_APP_REMOTE_MODE   (WM_APP + 11) /* 手机切换播放模式（循环切换） */
#define WM_APP_REMOTE_SPEED  (WM_APP + 12) /* wParam: 倍速 50..300（百分之一） */

/* 批量缓存：Ctrl+多选剧集后点缓存按钮，独立线程逐集下载（不打断播放） */
#define WM_APP_BATCH_DONE    (WM_APP + 13) /* wParam=ok(0/1) */
#define WM_APP_BATCH_FINISH  (WM_APP + 14) /* wParam=成功数, lParam=总数 */
#define WM_APP_BATCH_PROGRESS (WM_APP + 16) /* wParam=当前集百分比 0-100 */
#define WM_APP_SEARCH_DONE    (WM_APP + 17) /* lParam=SearchResult* */
#define WM_APP_FEED_PROGRESS (WM_APP + 15) /* wParam=已加载数, lParam=总数 */

/* ================= 多语言（简体中文 / English） ================= */
enum {
    TR_READY = 0,
    TR_SHUTDOWN_TITLE,          /* 标题栏倒计时格式串 */
    TR_COL_TITLE, TR_COL_DATE, TR_COL_DUR, TR_COL_PODCAST,
    TR_UNTITLED,
    TR_CACHED, TR_CACHE_SUBTITLE, TR_UNKNOWN_POD, TR_CACHED_FMT,
    TR_OFFLINE_LOCAL,
    TR_AUDIO, TR_MONO, TR_STEREO, TR_MULTICH,
    TR_CANT_OPEN, TR_LOADING_FEEDS, TR_PREPARING, TR_DL_THREAD_FAIL,
    TR_PLAYING_OFFLINE_FMT, TR_PLAYING_FMT,
    TR_STOPPED, TR_FINISHED, TR_PLAY_ERROR,
    TR_DL_PROGRESS_FMT, TR_AUDIO_GONE, TR_DL_FAILED, TR_NO_AUDIO_DEV,
    TR_OFFLINE_HINT, TR_NO_FEEDS_HINT,
    TR_F_TITLE, TR_F_DESC, TR_F_AUTHOR, TR_F_LANG, TR_F_HOME,
    TR_F_COPYRIGHT, TR_F_FEEDURL, TR_F_PUBDATE, TR_F_LOCALFILE, TR_F_LINK,
    TR_TIP_PODCAST_FMT, TR_TIP_PUBDATE_FMT, TR_TIP_DUR_FMT,
    TR_CACHE_VIEW_DESC,
    TR_INPUT_TITLE, TR_INPUT_LABEL, TR_OK, TR_CANCEL, TR_IB_OPML,
    TR_BAD_URL, TR_DUP_FEED, TR_MAX_FEEDS,
    TR_M_OPEN_CACHE, TR_M_CHANGE_CACHE, TR_M_DEFAULT_CACHE,
    TR_M_CACHE_CLEAN, TR_CC_OFF, TR_CC_DAYS_FMT, TR_CC_SET_FMT,
    TR_SEARCH_HINT, TR_SEARCH_BTN, TR_SEARCHING, TR_SEARCH_NONE,
    TR_SEARCH_FAIL,
    TR_M_NIGHT, TR_M_LANGUAGE, TR_LANG_ZH, TR_LANG_EN,
    TR_M_SHUTDOWN, TR_M_SHUT_CANCEL, TR_SHUT_IN_MINS_FMT,
    TR_M_CUSTOM, TR_CUSTOM_TITLE, TR_CUSTOM_LABEL, TR_CUSTOM_BAD,
    TR_SHUTTING_DOWN,
    TR_M_FONT_FMT, TR_FONT_S, TR_FONT_M, TR_FONT_L,
    TR_M_ABOUT, TR_ABOUT_TEXT,
    TR_NIGHT_ON, TR_NIGHT_OFF, TR_FONT_SWITCHED,
    TR_SHUT_SET, TR_SHUT_CANCELED, TR_SHUT_DENIED,
    TR_CACHE_CHANGED, TR_CACHE_RESET,
    TR_MOVE_Q_NEW_FMT, TR_MOVE_Q_DEF_FMT, TR_MOVE_CACHE, TR_BROWSE_CACHE,
    TR_TB_PREV, TR_TB_PLAY, TR_TB_PAUSE, TR_TB_NEXT, TR_TB_SHUFFLE,
    TR_TIP_PREV, TR_TIP_PLAYPAUSE, TR_TIP_NEXT, TR_TIP_STOP, TR_TIP_MUTE,
    TR_TIP_ADD, TR_TIP_DEL, TR_TIP_SORT, TR_TIP_REFRESH, TR_TIP_SETTINGS,
    TR_TIP_SPEED,
    TR_PM_ORDER, TR_PM_SHUFFLE, TR_PM_REPEAT, TR_PM_ONCE,
    TR_M_PHONE, TR_PHONE_TITLE, TR_PHONE_FAIL,
    TR_PHONE_ON, TR_PHONE_OFF, TR_PHONE_FW,
    TR_SPEED_SET,
    TR_TRAY_SHOW, TR_TRAY_EXIT,
    TR_TIP_CACHE, TR_BATCH_PROGRESS_FMT, TR_BATCH_DONE_FMT, TR_BATCH_NOSEL, TR_BATCH_RUNNING,
    TR_BATCH_DONE_CAP, TR_BATCH_CANCELLED, TR_BATCH_STOPPING,
    TR_LOADING_PROG,
    TR_FEED_FAIL_TITLE, TR_FEED_FAIL_SUB,
    TR_M_IMPORT, TR_M_EXPORT, TR_M_EXP_OPML, TR_M_EXP_INI,
    TR_IMP_TITLE, TR_EXP_OPML_CAP, TR_EXP_INI_CAP, TR_EXP_DONE, TR_EXP_FAIL,
    TR_IMP_INI_Q, TR_IMP_INI_DONE, TR_IMP_WRITE_FAIL,
    TR_IMP_OPML_OK_FMT, TR_IMP_OPML_NONE, TR_IMP_UNKNOWN, TR_IMP_FAIL,
    TR_CFGMODE_CAP, TR_CFGMODE_BODY, TR_CFGMODE_DELINI_Q,
    TR_M_CFGMODE, TR_M_CFG_EMB, TR_M_CFG_EXT,
    TR_CFGMODE_EMB_DONE, TR_CFGMODE_EXT_DONE, TR_CFGMODE_FAIL,
    TR_CTX_REFRESH, TR_CTX_DELETE_POD, TR_CTX_DEL_POD_CACHE,
    TR_CTX_DEL_ALL_CACHE, TR_CTX_OPEN_FOLDER, TR_CTX_DOWNLOAD,
    TR_CTX_DEL_EP_CACHE, TR_REFRESH_QUEUED,
    TR_CTX_COPY_EP_URL, TR_CTX_COPY_FEED_URL,
    TR_CTX_ADD_POD,
    TR_CLIP_OK, TR_CLIP_FAIL,
    TR_N
};

static const wchar_t *tr(int id)
{
    static const wchar_t *const zh[TR_N] = {
        L"就绪",
        L"LiteTune ｜ %d:%02d 后关机",
        L"标题", L"日期", L"时长", L"播客",
        L"(无标题)",
        L"已缓存", L"本地缓存音频，断网也可播放", L"未知播客", L"已缓存 (%d)",
        L"离线播放本地音频",
        L"音频", L"单声", L"立体", L"多声",
        L"无法打开音频文件", L"正在加载订阅...", L"准备音频...", L"下载线程创建失败",
        L"正在播放（离线）：%ls", L"正在播放：%ls",
        L"已停止", L"播放完毕", L"播放出错",
        L"下载音频中... %d%%", L"音频已失效", L"音频下载失败",
        L"无法开始播放：未找到可用的音频输出设备",
        L"没有订阅也能听：正在播放列表最后的「已缓存」，无需联网",
        L"还没有订阅，点左下角「+」按钮添加 RSS 播客",
        L"标题", L"描述", L"作者", L"语言", L"主页",
        L"版权", L"订阅地址", L"发布", L"本地文件", L"链接",
        L"\r\n播客：%ls", L"\r\n发布：%ls", L"\r\n时长：%ls",
        L"【已缓存】\r\n本地缓存音频共 %d 个，无需联网即可播放。\r\n\r\n"
            L"缓存内容来自各播客已播放（自动下载）的音频；更改缓存目录请用右上角设置按钮。",
        L"添加播客订阅",
        L"输入 RSS 订阅地址（http:// 或 https://）：",
        L"确定", L"取消", L"从 OPML 文件导入...",
        L"地址无效，请输入以 http:// 或 https:// 开头的链接",
        L"该订阅地址已存在", L"订阅数量已达上限",
        L"打开缓存目录", L"更改缓存目录...", L"恢复默认目录",
        L"自动清理缓存", L"关", L"%d 天", L"自动清理缓存：%ls",
        L"输入关键词搜索 iTunes（或直接粘 RSS 地址）：", L"搜索", L"搜索中…",
        L"没找到相关播客", L"搜索失败（网络问题？）",
        L"夜间模式", L"语言", L"简体中文", L"English",
        L"定时关机", L"取消定时关机", L"%d 分钟后关机",
        L"自定义分钟数...", L"自定义定时关机",
        L"输入多少分钟后关机（1-600）：",
        L"请输入 1 到 600 之间的整数分钟",
        L"正在关机…（15 秒内可运行 shutdown /a 取消）",
        L"切换字号（当前：%ls）", L"小", L"中", L"大",
        L"关于 LiteTune",
        L"LiteTune  版本 1.4\r\n\r\n"
            L"轻量级播客播放器：订阅 RSS、自动缓存、离线收听。\r\n\r\n"
            L"纯 C + Win32 API 编写，单文件绿色便携，\r\n不依赖任何第三方 DLL。",
        L"夜间模式已开启", L"夜间模式已关闭", L"字号已切换",
        L"定时关机已设定（关闭程序即可取消）", L"定时关机已取消",
        L"关机请求被系统拒绝",
        L"缓存目录已更改", L"缓存目录已恢复默认",
        L"当前缓存目录有 %d 个音频文件，是否移动到新目录？",
        L"当前缓存目录有 %d 个音频文件，是否移动到默认目录？",
        L"移动缓存", L"选择缓存目录",
        L"上一曲", L"播放", L"暂停", L"下一曲", L"随机下一曲",
        L"上一首", L"播放 / 暂停", L"下一首", L"停止", L"静音",
        L"添加播客", L"删除播客", L"调整顺序", L"刷新", L"设置", L"倍速",
        L"顺序播放", L"随机播放", L"单曲循环", L"一次性播放",
        L"手机播放/遥控", L"手机播放/遥控",
        L"手机服务启动失败（端口被占用？）",
        L"手机服务已开启", L"手机服务已关闭",
        L"服务已在本机开启，但防火墙未放行，手机连不上。\r\n\r\n"
            L"请在弹出的权限窗口点「是」；若已错过，可用管理员身份打开命令行执行：\r\n"
            L"netsh advfirewall firewall add rule name=\"LiteTune Phone\" "
            L"dir=in action=allow program=\"本程序完整路径\" enable=yes profile=any\r\n\r\n"
            L"另外，手机要连页面上标注 WiFi/以太网 的地址；复制到别的电脑运行时，"
            L"那台电脑也会重新询问一次防火墙权限。",
        L"倍速已切换",
        L"显示主窗口", L"退出",
        L"批量缓存（Ctrl+点选剧集后点击）",
        L"批量缓存 %d/%d…", L"批量缓存完成（成功 %d/%d 集）",
        L"请先选择要缓存的剧集（Ctrl+点击）", L"批量缓存进行中",
        L"批量缓存完成（成功 %d/%d 集，超上限部分未缓存）",
        L"已取消批量下载", L"正在停止批量下载…",
        L"正在加载订阅 %d/%d ...",
        L"加载失败的订阅",
        L"无法获取或解析，按 × 按钮可删除；刷新可重试",
        L"导入...", L"导出", L"OPML 订阅列表", L"配置文件 (feeds.ini)",
        L"导入", L"导出 OPML", L"导出配置",
        L"已导出", L"无法写入文件",
        L"导入配置将覆盖当前的订阅与全部设置，重启 LiteTune 后生效。\r\n是否继续？",
        L"配置导入成功，重新打开 LiteTune 后生效。\r\n程序现在将退出。",
        L"写入配置失败（exe 被占用或只读？）",
        L"已从 OPML 导入 %d 个新订阅",
        L"OPML 中没有发现新订阅（可能都已存在）",
        L"无法识别的文件格式", L"无法读取文件",
        L"配置存储方式",
        L"首次运行，请选择配置的保存方式（之后可在设置里随时更改）：\r\n\r\n"
            L"内嵌到程序内：配置写进 exe 尾部，单文件便携，复制 exe 即连配置一起带走。\r\n\r\n"
            L"外挂 feeds.ini：配置存为同目录的 feeds.ini，便于升级、备份，也可手动编辑。",
        L"切换到内嵌会删除同目录的 feeds.ini。\r\n是否继续？",
        L"配置存储位置", L"内嵌到程序内", L"外挂 feeds.ini",
        L"配置已切换为内嵌", L"配置已切换为外挂 feeds.ini",
        L"切换配置存储方式失败（exe 被占用或只读？）",
        L"刷新播客", L"删除播客", L"删除缓存文件",
        L"清空全部缓存", L"打开文件夹", L"下载", L"删除缓存",
        L"已加入刷新队列，稍后自动开始",
        L"复制剧集链接", L"复制 Feed URL",
        L"添加订阅...",
        L"已复制到剪贴板", L"复制失败"
    };
    static const wchar_t *const en[TR_N] = {
        L"Ready",
        L"LiteTune ｜ Shut down in %d:%02d",
        L"Title", L"Date", L"Duration", L"Podcast",
        L"(Untitled)",
        L"Cached", L"Local cached audio, playable offline", L"Unknown podcast",
        L"Cached (%d)",
        L"Play local audio offline",
        L"Audio", L"Mono", L"Stereo", L"Multi",
        L"Cannot open audio file", L"Loading feeds...", L"Preparing audio...",
        L"Failed to start download thread",
        L"Playing (offline): %ls", L"Playing: %ls",
        L"Stopped", L"Playback finished", L"Playback error",
        L"Downloading audio... %d%%", L"Audio is no longer available",
        L"Audio download failed",
        L"Cannot play: no available audio output device",
        L"No subscription needed: select the last \"Cached\" entry to listen offline",
        L"No subscriptions yet. Click the \"+\" button at bottom-left to add an RSS feed",
        L"Title", L"Description", L"Author", L"Language", L"Homepage",
        L"Copyright", L"Feed URL", L"Published", L"Local file", L"URL",
        L"\r\nPodcast: %ls", L"\r\nPublished: %ls", L"\r\nDuration: %ls",
        L"[Cached]\r\n%d local audio file(s), playable without a network.\r\n\r\n"
            L"Audio is cached automatically when played. Use the Settings button "
            L"at the top-right to change the cache folder.",
        L"Add Podcast Feed",
        L"Enter an RSS feed URL (http:// or https://):",
        L"OK", L"Cancel", L"Import from OPML file...",
        L"Invalid URL. Please enter a link starting with http:// or https://",
        L"This feed already exists", L"Maximum number of feeds reached",
        L"Open Cache Folder", L"Change Cache Folder...", L"Restore Default Folder",
        L"Auto-Clean Cache", L"Off", L"%d days", L"Auto-clean cache: %ls",
        L"Enter keyword to search iTunes (or paste an RSS URL):", L"Search", L"Searching...",
        L"No podcasts found", L"Search failed (network issue?)",
        L"Night Mode", L"Language", L"简体中文", L"English",
        L"Sleep Timer", L"Cancel Sleep Timer", L"Shut down in %d min",
        L"Custom minutes...", L"Custom Sleep Timer",
        L"Shut down in how many minutes (1-600)?",
        L"Please enter a whole number between 1 and 600",
        L"Shutting down... (run \"shutdown /a\" within 15s to cancel)",
        L"Font Size (Current: %ls)", L"Small", L"Medium", L"Large",
        L"About LiteTune",
        L"LiteTune  version 1.4\r\n\r\n"
            L"A lightweight podcast player: subscribe to RSS feeds,\r\n"
            L"auto-cache episodes and listen offline.\r\n\r\n"
            L"Written in pure C with Win32 API. One portable executable,\r\n"
            L"no third-party DLLs required.",
        L"Night mode on", L"Night mode off", L"Font size changed",
        L"Sleep timer set (quit the app to cancel)", L"Sleep timer canceled",
        L"Shutdown request denied by the system",
        L"Cache folder changed", L"Cache folder restored to default",
        L"The current cache folder contains %d audio file(s). Move them to the new folder?",
        L"The current cache folder contains %d audio file(s). Move them to the default folder?",
        L"Move Cache", L"Select Cache Folder",
        L"Previous", L"Play", L"Pause", L"Next", L"Random next",
        L"Previous", L"Play / Pause", L"Next", L"Stop", L"Mute",
        L"Add podcast", L"Delete podcast", L"Reorder", L"Refresh", L"Settings", L"Speed",
        L"Sequential", L"Shuffle", L"Repeat one", L"Play once",
        L"Phone Speaker/Remote", L"Phone Speaker/Remote",
        L"Failed to start phone service (port in use?)",
        L"Phone service on", L"Phone service off",
        L"The service is running on this PC, but the firewall was not opened, "
            L"so the phone cannot connect.\r\n\r\n"
            L"Click \"Yes\" on the permission prompt. If you missed it, run as "
            L"administrator:\r\n"
            L"netsh advfirewall firewall add rule name=\"LiteTune Phone\" "
            L"dir=in action=allow program=\"full-path-to-this-exe\" "
            L"enable=yes profile=any\r\n\r\n"
            L"Also: on the phone use the address labelled WiFi/Ethernet. "
            L"Copying the program to another PC means that PC will ask for "
            L"firewall permission again.",
        L"Playback speed changed",
        L"Show window", L"Exit",
        L"Batch cache (Ctrl+click episodes, then click)",
        L"Batch caching %d/%d...", L"Batch cache done (%d/%d ok)",
        L"Select episodes first (Ctrl+click)", L"Batch cache in progress",
        L"Batch cache done (%d/%d ok, rest over limit skipped)",
        L"Batch cache cancelled", L"Stopping batch cache…",
        L"Loading feeds %d/%d ...",
        L"Failed to load feed",
        L"Cannot fetch or parse. Click \u00d7 to remove, Refresh to retry",
        L"Import...", L"Export", L"OPML feed list", L"Configuration (feeds.ini)",
        L"Import", L"Export OPML", L"Export Configuration",
        L"Exported", L"Cannot write the file",
        L"Importing a configuration replaces ALL feeds and settings. "
            L"It takes effect after restarting LiteTune.\r\nContinue?",
        L"Configuration imported. It takes effect the next time you start "
            L"LiteTune.\r\nThe program will now exit.",
        L"Failed to write the configuration (exe in use or read-only?)",
        L"Imported %d new feed(s) from OPML",
        L"No new feeds found in the OPML (all may already exist)",
        L"Unrecognized file format", L"Cannot read the file",
        L"Config Storage",
        L"First run: choose where to store the configuration (changeable later in Settings):\r\n\r\n"
            L"Embedded in program: written into the exe's tail; truly single-file "
            L"portable — copying the exe carries the config too.\r\n\r\n"
            L"External feeds.ini: saved as feeds.ini next to the exe; convenient "
            L"for upgrades, backups, and manual editing.",
        L"Switching to embedded will delete the feeds.ini in the same folder.\r\n"
            L"Continue?",
        L"Config Storage", L"Embedded in program", L"External feeds.ini",
        L"Configuration is now embedded",
        L"Configuration is now external (feeds.ini)",
        L"Failed to change config storage (exe in use or read-only?)",
        L"Refresh podcast", L"Delete podcast", L"Delete cached files",
        L"Clear all cache", L"Open folder", L"Download", L"Delete cache",
        L"Added to refresh queue",
        L"Copy episode URL", L"Copy feed URL",
        L"Add subscription...",
        L"Copied to clipboard", L"Copy failed"
    };
    return (g_lang ? en : zh)[id];
}

/* exe 同目录 */
static void ExeDir(wchar_t *out, int outMax)
{
    wchar_t *p;
    GetModuleFileNameW(NULL, out, outMax);
    p = wcsrchr(out, L'\\');
    if (p) *(p + 1) = L'\0';
}

/* ================= 小工具 ================= */
static void ApplyLayout(HWND hwnd, int live);   /* 前置声明，字号切换后要重排 */
static void UpdateMarquee(void);                /* 前置声明，字号切换后要重测文本宽 */
static void UpdatePodTipFromPoint(POINT pt);    /* 前置声明，播客列表悬停提示 */
static char* WToU8(const wchar_t *w);           /* 前置声明，SaveConfig 保存缓存目录 */
static long long PlayerGet100ns(int wantDur);   /* 前置声明，SaveConfig 保存续播位置 */
static void UpdateThumbbarPlayPause(void);      /* 前置声明，播放/暂停状态切换时更新任务栏缩略图按钮 */
static void ApplySpeed(void);                   /* 前置声明，播放开始时恢复倍速 */
static void CycleSpeed(void);                   /* 前置声明，倍速按钮点击 */

/* ---- 主题配色（夜间模式） ---- */
static COLORREF ThWinBg(void)   { return g_darkMode ? RGB(32,32,36)   : GetSysColor(COLOR_BTNFACE); }
static COLORREF ThPanel(void)   { return g_darkMode ? RGB(40,40,46)   : RGB(255,255,255); }
static COLORREF ThSel(void)     { return g_darkMode ? RGB(56,74,100)  : RGB(204,232,255); }
static COLORREF ThText(void)    { return g_darkMode ? RGB(230,230,230): RGB(20,20,20); }
static COLORREF ThDim(void)     { return g_darkMode ? RGB(160,160,168): RGB(120,120,120); }
static COLORREF ThGreen(void)   { return g_darkMode ? RGB(90,210,130) : RGB(0,130,60); }
static COLORREF ThGreenBg(void) { return g_darkMode ? RGB(28,56,38)   : RGB(222,244,228); }
static COLORREF ThYellow(void)  { return g_darkMode ? RGB(226,186,70) : RGB(190,155,0); }
static COLORREF ThBlue(void)    { return g_darkMode ? RGB(96,175,255)  : RGB(0,95,190);  }  /* 批量下载队列 */
static COLORREF ThTrack(void)   { return g_darkMode ? RGB(58,58,64)   : RGB(229,229,232); }  /* 滑块滑槽 */
static COLORREF ThAccent(void)  { return RGB(0,120,215);  }                                 /* 进度/滑块把手主色 */

/* 用纯色填满矩形（临时画刷即用即删） */
static void FillRectColor(HDC dc, const RECT *rc, COLORREF col)
{
    HBRUSH b = CreateSolidBrush(col);
    FillRect(dc, rc, b);
    DeleteObject(b);
}

/* 把一个 Unicode 字符串复制到系统剪贴板。成功返回 1，失败返回 0。 */
static int CopyToClipboardW(const wchar_t *text)
{
    size_t n; HGLOBAL hMem; wchar_t *dst;
    if (!text || !*text) return 0;
    n = (wcslen(text) + 1) * sizeof(wchar_t);
    hMem = GlobalAlloc(GMEM_MOVEABLE, n);
    if (!hMem) return 0;
    dst = (wchar_t*)GlobalLock(hMem);
    if (!dst) { GlobalFree(hMem); return 0; }
    memcpy(dst, text, n);
    GlobalUnlock(hMem);
    if (!OpenClipboard(NULL)) { GlobalFree(hMem); return 0; }
    EmptyClipboard();
    if (!SetClipboardData(CF_UNICODETEXT, hMem)) {
        CloseClipboard(); GlobalFree(hMem); return 0;
    }
    CloseClipboard();
    return 1;
}

/* ---- 暗色弹窗（MessageBox / InputBox 共用） ----
 * 亮色模式不接管任何消息，全部走系统原样 */
static HBRUSH g_dlgBgBrush = NULL;      /* 弹窗背景刷，随主题重建 */
static HBRUSH g_dlgPanelBrush = NULL;   /* 弹窗输入框底刷 */

static HBRUSH DlgBgBrush(void)
{
    if (!g_dlgBgBrush) g_dlgBgBrush = CreateSolidBrush(ThWinBg());
    return g_dlgBgBrush;
}
static HBRUSH DlgPanelBrush(void)
{
    if (!g_dlgPanelBrush) g_dlgPanelBrush = CreateSolidBrush(ThPanel());
    return g_dlgPanelBrush;
}
/* 主题切换后让缓存刷失效（下次取时按新配色重建） */
static void DlgBrushesReset(void)
{
    if (g_dlgBgBrush) { DeleteObject(g_dlgBgBrush); g_dlgBgBrush = NULL; }
    if (g_dlgPanelBrush) { DeleteObject(g_dlgPanelBrush); g_dlgPanelBrush = NULL; }
}

/* 弹窗客户区整体填暗色背景 */
static void FillDlgBg(HWND h, HDC dc)
{
    RECT rc;
    GetClientRect(h, &rc);
    FillRect(dc, &rc, DlgBgBrush());
}

/* 暗色下压按钮绘制：底色、边框（默认键描主色）、焦点框、文字 */
static void DrawDarkBtn(const DRAWITEMSTRUCT *di)
{
    wchar_t txt[128];
    HFONT f, of;
    RECT rc = di->rcItem, fr;
    HBRUSH b = CreateSolidBrush((di->itemState & ODS_SELECTED)
        ? RGB(24,24,28) : RGB(50,50,58));
    HBRUSH bo = CreateSolidBrush((di->itemState & ODS_DEFAULT)
        ? ThAccent() : RGB(86,86,96));
    FillRect(di->hDC, &rc, b);
    FrameRect(di->hDC, &rc, bo);
    DeleteObject(b);
    DeleteObject(bo);
    if (di->itemState & ODS_FOCUS) {
        fr = rc;
        InflateRect(&fr, -3, -3);
        DrawFocusRect(di->hDC, &fr);
    }
    txt[0] = 0;
    GetWindowTextW(di->hwndItem, txt, 128);
    f = (HFONT)SendMessageW(di->hwndItem, WM_GETFONT, 0, 0);
    if (!f) f = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    of = (HFONT)SelectObject(di->hDC, f);
    SetBkMode(di->hDC, TRANSPARENT);
    SetTextColor(di->hDC, ThText());
    DrawTextW(di->hDC, txt, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(di->hDC, of);
}

/* 暗色弹窗通用消息：背景/文字/编辑框/自绘按钮。
 * 已处理返回 1（结果写入 *res），否则返回 0 交回调用方 */
static int DarkDlgMsg(HWND h, UINT msg, WPARAM wp, LPARAM lp, LRESULT *res)
{
    switch (msg) {
    case WM_ERASEBKGND:
        if (!g_darkMode) return 0;
        FillDlgBg(h, (HDC)wp);
        *res = 1;
        return 1;
    case WM_CTLCOLORSTATIC:
        if (!g_darkMode) return 0;
        SetBkColor((HDC)wp, ThWinBg());
        SetTextColor((HDC)wp, ThText());
        *res = (LRESULT)DlgBgBrush();
        return 1;
    case WM_CTLCOLOREDIT:
        if (!g_darkMode) return 0;
        SetBkColor((HDC)wp, ThPanel());
        SetTextColor((HDC)wp, ThText());
        *res = (LRESULT)DlgPanelBrush();
        return 1;
    case WM_CTLCOLORLISTBOX:
        if (!g_darkMode) return 0;
        SetBkColor((HDC)wp, ThPanel());
        SetTextColor((HDC)wp, ThText());
        *res = (LRESULT)DlgPanelBrush();
        return 1;
    case WM_DRAWITEM:
        if (g_darkMode && ((DRAWITEMSTRUCT*)lp)->CtlType == ODT_BUTTON) {
            DrawDarkBtn((DRAWITEMSTRUCT*)lp);
            *res = TRUE;
            return 1;
        }
        return 0;
    }
    return 0;
}

/* MessageBox 暗色：WH_CBT 挂钩在激活时用 SetWindowSubclass 挂上暗色处理，
 * 只处理背景/文字颜色与按钮自绘，图标和布局仍由系统完成。
 * 注意：绝不能用 SetWindowLongPtr(GWLP_USERDATA) 手工链——系统消息框的
 * 对话框窗口自己占用 GWLP_USERDATA 存内部状态，踩掉后 USER32 内部
 * 固定偏移访问违例（点按钮即崩）。SetWindowSubclass 有独立存储，安全 */
static LRESULT CALLBACK MsgBoxSubProc(HWND h, UINT msg, WPARAM wp, LPARAM lp,
                                      UINT_PTR id, DWORD_PTR ref);
static void ApplyDarkTitlebar(HWND hwnd, int dark);

static BOOL CALLBACK MsgBoxBtnEnum(HWND btn, LPARAM lp)
{
    wchar_t cls[16];
    (void)lp;
    if (GetClassNameW(btn, cls, 16) && _wcsicmp(cls, L"Button") == 0) {
        /* 去掉按钮类型低 4 位改自绘；默认键身份由对话框模板的 defID 保留，
         * 绘制时用 ODS_DEFAULT 补主色描边 */
        SetWindowLongPtrW(btn, GWL_STYLE,
            (GetWindowLongPtrW(btn, GWL_STYLE) & ~0xF) | BS_OWNERDRAW);
        InvalidateRect(btn, NULL, TRUE);
    }
    return TRUE;
}

static LRESULT CALLBACK MsgBoxSubProc(HWND h, UINT msg, WPARAM wp, LPARAM lp,
                                      UINT_PTR id, DWORD_PTR ref)
{
    LRESULT r;
    (void)id; (void)ref;
    switch (msg) {
    case WM_PAINT: {
        /* 实测激活前系统已按亮色画过、且激活后仍有局部重绘路径绕过
         * WM_ERASEBKGND（底部按钮区白条）：每次绘制都把客户区填满暗色 */
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        FillDlgBg(h, dc);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_PRINTCLIENT:
        FillDlgBg(h, (HDC)wp);
        return 0;
    case WM_CTLCOLORBTN:
        return (LRESULT)DlgBgBrush();
    }
    if (DarkDlgMsg(h, msg, wp, lp, &r)) return r;
    return DefSubclassProc(h, msg, wp, lp);
}

static LRESULT CALLBACK MsgBoxHookProc(int code, WPARAM wp, LPARAM lp)
{
    if (code == HCBT_ACTIVATE) {
        HWND h = (HWND)wp;
        wchar_t cls[16];
        /* 只认系统对话框类：弹窗关闭、主窗口重新激活时本钩子还在装着，
         * 若不拦会把弹窗专用的暗色过程挂到主窗口上——主窗口自己的
         * WM_DRAWITEM 被截走，按钮图标全部消失 */
        if (!GetClassNameW(h, cls, 16) || _wcsicmp(cls, L"#32770") != 0)
            return CallNextHookEx(NULL, code, wp, lp);
        SetWindowSubclass(h, MsgBoxSubProc, 1, 0);   /* 同 id 重装即更新 */
        EnumChildWindows(h, MsgBoxBtnEnum, 0);
        ApplyDarkTitlebar(h, g_darkMode);   /* 弹窗标题栏跟随暗色（Win10 1809+） */
        /* ApplyDarkMenus 已废弃：弹窗菜单很少，且有 ApplyMenuTheme 在 TrackPopupMenu 时处理 */
        /* 子类化前弹窗可能已按系统配色画过一遍（底部按钮区尤甚），
         * 强制整窗按暗色重画 */
        InvalidateRect(h, NULL, TRUE);
    }
    return CallNextHookEx(NULL, code, wp, lp);
}

/* 统一的消息框入口（全项目都用它）；亮色直接透传系统 */
static int MessageBoxTh(HWND owner, const wchar_t *text,
                        const wchar_t *title, UINT type)
{
    int r;
    HHOOK hook;
    if (!g_darkMode) return MessageBoxW(owner, text, title, type);
    hook = SetWindowsHookExW(WH_CBT, MsgBoxHookProc, NULL, GetCurrentThreadId());
    r = MessageBoxW(owner, text, title, type);
    if (hook) UnhookWindowsHookEx(hook);
    return r;
}


/* 标题栏：定时关机时附倒计时，播放中附剧集标题，否则固定 LiteTune */
static void UpdateWindowTitle(void)
{
    wchar_t t[160];
    if (!g_hwnd) return;
    if (g_shutdownAt > 0) {
        int left = (int)(g_shutdownAt - (long long)time(NULL));
        if (left < 0) left = 0;
        swprintf(t, 160, tr(TR_SHUTDOWN_TITLE), left / 60, left % 60);
    } else if (g_playState != 0 && g_nowTitle[0]) {
        swprintf(t, 160, L"LiteTune - %ls", g_nowTitle);
    } else {
        wcscpy_s(t, 160, L"LiteTune");
    }
    SetWindowTextW(g_hwnd, t);
}

/* Win10 1809+ 暗色标题栏：DWMWA_USE_IMMERSIVE_DARK_MODE（20H1+ 属性值为 20，
 * 1809~1903 为 19）。运行时加载 dwmapi，Win7 上静默失败，无副作用。
 * Win10 的 DWM 收到属性后不会马上重画非客户区（要切一次窗口焦点才刷新），
 * 因此设完属性后主动模拟一次激活/取消激活，逼 DWM 立即重绘标题栏。 */
typedef HRESULT (WINAPI *DwmSetAttrFn)(HWND, DWORD, LPCVOID, DWORD);
static void ApplyDarkTitlebar(HWND hwnd, int dark)
{
    HMODULE m;
    DwmSetAttrFn p;
    BOOL on = dark ? TRUE : FALSE;
    HRESULT hr;

    if (!hwnd) return;
    m = LoadLibraryW(L"dwmapi.dll");
    if (!m) return;
    *(void**)&p = (void*)GetProcAddress(m, "DwmSetWindowAttribute");
    if (p) {
        hr = p(hwnd, 20, &on, sizeof(on));
        if (FAILED(hr)) hr = p(hwnd, 19, &on, sizeof(on));
        if (SUCCEEDED(hr)) {
            SendMessageW(hwnd, WM_NCACTIVATE, FALSE, 0);
            SendMessageW(hwnd, WM_NCACTIVATE, TRUE, 0);
        }
    }
    FreeLibrary(m);
}

/* Tooltip 自绘子类：完全控制背景/文字色，绕过视觉样式下 TTM_SETTIPTEXTCOLOR 失效问题。
 * 对 g_hwndTip / g_hwndEpTip / g_ibTip 通用。 */
static LRESULT CALLBACK TipSubProc(HWND h, UINT msg, WPARAM wp, LPARAM lp,
    UINT_PTR id, DWORD_PTR ref)
{
    (void)id; (void)ref;
    if (msg == WM_ERASEBKGND) {
        RECT rc;
        GetClientRect(h, &rc);
        HBRUSH bg = CreateSolidBrush(ThPanel());
        FillRect((HDC)wp, &rc, bg);
        DeleteObject(bg);
        return 1;
    }
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(h, &ps);
        RECT rc;
        GetClientRect(h, &rc);
        HBRUSH bg = CreateSolidBrush(ThPanel());
        FillRect(hdc, &rc, bg);
        DeleteObject(bg);
        /* 取当前工具的文字 */
        wchar_t txt[2048] = {0};
        TOOLINFOW ti;
        ZeroMemory(&ti, sizeof(ti));
        ti.cbSize = sizeof(ti);
        ti.lpszText = txt;
        if (SendMessageW(h, TTM_GETCURRENTTOOL, 0, (LPARAM)&ti)) {
            SetTextColor(hdc, ThText());
            SetBkMode(hdc, TRANSPARENT);
            HFONT old = (HFONT)SelectObject(hdc, g_font ? g_font : GetStockObject(DEFAULT_GUI_FONT));
            RECT trc = rc;
            trc.left += 6; trc.right -= 6; trc.top += 4; trc.bottom -= 4;
            DrawTextW(hdc, txt, -1, &trc, DT_LEFT | DT_WORDBREAK);
            SelectObject(hdc, old);
        }
        EndPaint(h, &ps);
        return 0;
    }
    return DefSubclassProc(h, msg, wp, lp);
}

/* 给 tooltip 挂上自绘子类（重复调用安全，同 id 重装即更新） */
static void ThemeTipSubclass(HWND tip)
{
    if (tip) SetWindowSubclass(tip, TipSubProc, 1, 0);
}

/* 给剧集表头的 3 列设置/取消自绘格式（选中播客会重建列，必须重新应用） */
static void SubclassHeader(void);
static void ApplyHeaderTheme(void)
{
    HWND hdr;
    int ci;
    if (!g_listEp) return;
    hdr = (HWND)SendMessageW(g_listEp, LVM_GETHEADER, 0, 0);
    if (!hdr) return;
    SubclassHeader();
    for (ci = 0; ci < 3; ci++) {
        HDITEMW hi;
        ZeroMemory(&hi, sizeof(hi));
        hi.mask = HDI_FORMAT;
        if (SendMessageW(hdr, HDM_GETITEMW, ci, (LPARAM)&hi)) {
            if (g_darkMode) hi.fmt |= HDF_OWNERDRAW;
            else            hi.fmt &= ~HDF_OWNERDRAW;
            SendMessageW(hdr, HDM_SETITEMW, ci, (LPARAM)&hi);
        }
    }
    InvalidateRect(hdr, NULL, TRUE);
}

/* 表头自身过程：暗色下补绘最后一列之外的空白区（表头默认背景是白色） */
static WNDPROC g_origHdrProc = NULL;
static LRESULT CALLBACK HeaderProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    LRESULT lr = CallWindowProcW(g_origHdrProc, hwnd, msg, wp, lp);
    if (g_darkMode && msg == WM_PAINT) {
        HDC hdc = GetDC(hwnd);
        RECT rc, last;
        GetClientRect(hwnd, &rc);
        if (hdc && SendMessageW(hwnd, HDM_GETITEMRECT, 2, (LPARAM)&last)
            && last.right < rc.right) {
            RECT gap = { last.right, rc.top, rc.right, rc.bottom };
            HBRUSH b = CreateSolidBrush(RGB(46, 46, 52));
            HPEN pn = CreatePen(PS_SOLID, 1, RGB(28, 28, 32));
            HGDIOBJ ob1 = SelectObject(hdc, b);
            HGDIOBJ ob2 = SelectObject(hdc, pn);
            FillRect(hdc, &gap, b);
            MoveToEx(hdc, rc.left, rc.bottom - 1, NULL);
            LineTo(hdc, rc.right, rc.bottom - 1);
            SelectObject(hdc, ob1);
            SelectObject(hdc, ob2);
            DeleteObject(b);
            DeleteObject(pn);
        }
        if (hdc) ReleaseDC(hwnd, hdc);
    }
    return lr;
}

static void SubclassHeader(void)
{
    HWND hdr = g_listEp ? (HWND)SendMessageW(g_listEp, LVM_GETHEADER, 0, 0) : NULL;
    if (hdr && !g_origHdrProc) {
        g_origHdrProc = (WNDPROC)SetWindowLongPtrW(hdr, GWLP_WNDPROC, (LONG_PTR)HeaderProc);
    }
}

/* 主题切换：重建背景刷、更新控件配色、表头/标题栏/非客户区滚动条一起重绘 */
static LRESULT CALLBACK SliderProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
static void LayoutControls(HWND hwnd);

static WNDPROC g_oldSeekProc = NULL, g_oldVolProc = NULL;   /* 滑块原过程（重建滑块也用） */
static WNDPROC g_oldListEpProc = NULL;   /* 剧集列表原过程（拖拽多选） */
static int g_epDragStart = -1;   /* 拖拽多选起点行，-1=未拖拽 */

/* 剧集列表拖拽多选：按下选中→拖过连续选→松开停止；Ctrl+拖=追加
 * 不用 SetCapture（ListView 内部自己管 capture 会冲突），
 * 改用定时器轮询鼠标位置 + GetAsyncKeyState 检测松开 */
static int g_epDragOn = 0;   /* 1=拖拽多选进行中 */

static LRESULT CALLBACK ListEpDragProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_LBUTTONDOWN: {
        LVHITTESTINFO ht;
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        /* 让 ListView 选中点击的行 */
        LRESULT lr = CallWindowProcW(g_oldListEpProc, hwnd, msg, wp, lp);
        ht.pt = pt;
        g_epDragStart = (int)SendMessageW(hwnd, LVM_HITTEST, 0, (LPARAM)&ht);
        g_epDragOn = 1;
        /* 不调 SetCapture，避免和 ListView 内部冲突 */
        SetTimer(hwnd, 0x4455, 50, NULL);
        return lr;
    }
    case WM_TIMER:
        if (wp == 0x4455) {
            if (g_epDragOn && g_epDragStart >= 0) {
                /* 左键松开 = 停止 */
                if (!(GetAsyncKeyState(VK_LBUTTON) & 0x8000)) {
                    g_epDragOn = 0;
                    g_epDragStart = -1;
                    KillTimer(hwnd, 0x4455);
                    return 0;
                }
                /* 获取鼠标在列表中的位置 */
                {
                    POINT pt;
                    RECT rc;
                    GetCursorPos(&pt);
                    GetClientRect(hwnd, &rc);
                    ScreenToClient(hwnd, &pt);
                    if (PtInRect(&rc, pt)) {
                        LVHITTESTINFO ht;
                        int cur, lo, hi, i;
                        LVITEMW lvi;
                        ht.pt = pt;
                        cur = (int)SendMessageW(hwnd, LVM_HITTEST, 0, (LPARAM)&ht);
                        if (cur >= 0) {
                            lo = (cur < g_epDragStart) ? cur : g_epDragStart;
                            hi = (cur > g_epDragStart) ? cur : g_epDragStart;
                            ZeroMemory(&lvi, sizeof(lvi));
                            lvi.stateMask = LVIS_SELECTED;
                            if (!(GetAsyncKeyState(VK_CONTROL) & 0x8000)) {
                                lvi.state = 0;
                                SendMessageW(hwnd, LVM_SETITEMSTATE, (WPARAM)-1, (LPARAM)&lvi);
                            }
                            lvi.state = LVIS_SELECTED;
                            for (i = lo; i <= hi; i++)
                                SendMessageW(hwnd, LVM_SETITEMSTATE, i, (LPARAM)&lvi);
                            InvalidateRect(hwnd, NULL, FALSE);
                        }
                    }
                    /* 边界自动滚 */
                    if (pt.y < rc.top + 20)       SendMessageW(hwnd, LVM_SCROLL, 0, -30);
                    else if (pt.y > rc.bottom - 20) SendMessageW(hwnd, LVM_SCROLL, 0, 30);
                }
            } else {
                KillTimer(hwnd, 0x4455);
            }
            return 0;
        }
        break;
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN:
        g_epDragOn = 0;
        g_epDragStart = -1;
        KillTimer(hwnd, 0x4455);
        break;
    }
    return CallWindowProcW(g_oldListEpProc, hwnd, msg, wp, lp);
}

/* 销毁重建滑块：实测 trackbar 在主题切换后即使 NM_CUSTOMDRAW 正常触发、
 * 绘制代码执行无误，屏幕上仍保留旧亮色外观（comctl v6 显示缓存行为），
 * 而新建控件的首次绘制路径与程序启动时一致、已验证可靠 */
static void RecreateSlider(HWND *ph, WNDPROC *pold, int id, int maxRange, int pos)
{
    if (*ph) { DestroyWindow(*ph); *ph = NULL; }
    if (!g_hwnd) return;
    *ph = CreateWindowExW(0, TRACKBAR_CLASSW, NULL,
        WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS,
        0, 0, 0, 0, g_hwnd, (HMENU)(INT_PTR)id, NULL, NULL);
    SendMessageW(*ph, TBM_SETRANGE, TRUE, MAKELONG(0, maxRange));
    SendMessageW(*ph, TBM_SETPOS, TRUE, pos);
    *pold = (WNDPROC)SetWindowLongPtrW(*ph, GWLP_WNDPROC, (LONG_PTR)SliderProc);
}

static void ApplyTheme(void)
{
    /* 需要强制重绘的子控件：自绘按钮走 WM_DRAWITEM，滑块走 NM_CUSTOMDRAW，
     * 静态文字走 WM_CTLCOLORSTATIC。实测仅靠 RedrawWindow RDW_ALLCHILDREN
     * 在运行期切换时不会触发这些自绘路径，按钮/滑块会残留亮色。 */
    HWND ctrls[] = {
        g_sldSeek, g_sldVol, g_stTime, g_stVol, g_stFormat,
        g_btnPrev, g_btnPlay, g_btnNext, g_btnStop, g_btnMute,
        g_btnAdd, g_btnDel, g_btnRefresh, g_btnPlayMode, g_btnDir, g_btnCache,
        g_listPod, g_listEp, g_editDesc, g_vsPod, g_vsEp, g_vsDesc,
        g_btnDescTab
    };
    int i;
    if (g_hwnd) {
        HBRUSH nb = CreateSolidBrush(ThWinBg());
        SetClassLongPtrW(g_hwnd, GCLP_HBRBACKGROUND, (LONG_PTR)nb);
        if (g_bgBrush) DeleteObject(g_bgBrush);
        g_bgBrush = nb;
    }
    if (g_whiteBrush) DeleteObject(g_whiteBrush);
    g_whiteBrush = CreateSolidBrush(ThPanel());
    DlgBrushesReset();   /* 弹窗缓存刷随新配色重建 */
    if (g_listEp) {
        SendMessageW(g_listEp, LVM_SETBKCOLOR, 0, (LPARAM)ThPanel());
        SendMessageW(g_listEp, LVM_SETTEXTBKCOLOR, 0, (LPARAM)ThPanel());
        SendMessageW(g_listEp, LVM_SETTEXTCOLOR, 0, (LPARAM)ThText());
        ApplyHeaderTheme();
    }
    for (i = 0; i < (int)(sizeof(ctrls)/sizeof(ctrls[0])); i++)
        if (ctrls[i]) InvalidateRect(ctrls[i], NULL, TRUE);
    /* 两个 trackbar 重建（保留当前位置），并用 LayoutControls 重新定位 */
    if (g_hwnd) {
        int seekPos = g_sldSeek ? (int)SendMessageW(g_sldSeek, TBM_GETPOS, 0, 0) : 0;
        int volPos  = g_sldVol  ? (int)SendMessageW(g_sldVol,  TBM_GETPOS, 0, 0) : g_volume;
        RecreateSlider(&g_sldSeek, &g_oldSeekProc, IDC_SLD_SEEK, 1000, seekPos);
        RecreateSlider(&g_sldVol,  &g_oldVolProc,  IDC_SLD_VOL, 100, volPos);
        LayoutControls(g_hwnd);
    }
    ApplyDarkTitlebar(g_hwnd, g_darkMode);
    /* ApplyMenuTheme 在 TrackPopupMenu 调用点才应用（需要具体 HMENU） */
    /* 系统 tooltip 统一主题色（g_ibTip 在 InputBox 初始化处设置，因为定义在本函数之后） */
    {
        HWND tips[] = { g_hwndTip, g_hwndEpTip };
        int k;
        for (k = 0; k < 2; k++)
            ThemeTipSubclass(tips[k]);
    }
    if (g_hwnd) {
        /* DWM 暗色标题栏属性运行期切换后，需重算非客户区并模拟一次
         * 激活同步，标题栏才会立即按新属性重绘 */
        SetWindowPos(g_hwnd, NULL, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE |
            SWP_FRAMECHANGED);
        SendMessageW(g_hwnd, WM_NCACTIVATE,
            (WPARAM)(GetForegroundWindow() == g_hwnd), 0);
        /* RDW_FRAME：让滚动条等非客户区也重绘为暗色 */
        RedrawWindow(g_hwnd, NULL, NULL,
            RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_ERASE |
            RDW_FRAME | RDW_UPDATENOW);
    }

}

/* 滑块完全自绘（亮/暗两种主题同一种圆角造型，仅配色不同）：
 * 滑槽/已播放部分/蓝色把手；返回 1 表示已处理 */
static int DrawSliderTrack(LPNMCUSTOMDRAW cd)
{
    HDC hdc = cd->hdc;
    HWND sld = cd->hdr.hwndFrom;
    RECT rc = cd->rc, ch, th, tr, pf;
    HBRUSH bg, tb, acb;
    HPEN edge, oldPen_p;
    HGDIOBJ oldPen, oldBr;
    int pos, cy;

    bg = CreateSolidBrush(ThWinBg());
    FillRect(hdc, &rc, bg);
    DeleteObject(bg);

    SendMessageW(sld, TBM_GETCHANNELRECT, 0, (LPARAM)&ch);
    SendMessageW(sld, TBM_GETTHUMBRECT, 0, (LPARAM)&th);

    /* 统一滑槽：以滑道垂直中心固定 4px 高，不依赖控件内部 channel 高度
     * （channel 过薄时圆角矩形会退化成虚线） */
    cy = (ch.top + ch.bottom) / 2;
    tr.left = ch.left; tr.right = ch.right;
    tr.top = cy - 2; tr.bottom = cy + 2;

    oldPen = SelectObject(hdc, GetStockObject(NULL_PEN));
    tb = CreateSolidBrush(ThTrack());
    oldBr = SelectObject(hdc, tb);
    RoundRect(hdc, tr.left, tr.top, tr.right, tr.bottom, 4, 4);

    /* 已播放部分：主题色圆角填充（进度、音量统一；量程用 TBM_GETRANGE，
     * 进度条 0-1000、音量条 0-100 都正确） */
    {
        int mn = 0, mx = 0, span;
        pos = (int)SendMessageW(sld, TBM_GETPOS, 0, 0);
        mn = (int)SendMessageW(sld, TBM_GETRANGEMIN, 0, 0);
        mx = (int)SendMessageW(sld, TBM_GETRANGEMAX, 0, 0);
        span = mx - mn;
        if (span > 0 && pos > mn) {
            pf = tr;
            pf.right = tr.left + (int)((long)(tr.right - tr.left) * (pos - mn) / span);
            if (pf.right > pf.left + 4) {
                acb = CreateSolidBrush(ThAccent());
                SelectObject(hdc, acb);
                RoundRect(hdc, pf.left, pf.top, pf.right, pf.bottom, 4, 4);
                DeleteObject(acb);
            }
        }
    }
    SelectObject(hdc, oldBr);
    DeleteObject(tb);

    /* 把手：主题色圆角块 + 浅边，保证在蓝色进度上也能看清 */
    acb = CreateSolidBrush(ThAccent());
    SelectObject(hdc, acb);
    edge = CreatePen(PS_SOLID, 1, RGB(235, 240, 248));
    oldPen_p = SelectObject(hdc, edge);
    RoundRect(hdc, th.left, th.top, th.right, th.bottom, 3, 3);
    SelectObject(hdc, oldPen_p);
    DeleteObject(edge);
    DeleteObject(acb);

    SelectObject(hdc, oldPen);
    return 1;
}

/* 在按钮矩形内画一个方向三角形：dir 0=上 1=下 2=左 3=右 */
static void DrawScrollArrow(HDC hdc, const RECT *r, int dir, COLORREF col)
{
    int cx = (r->left + r->right) / 2, cy = (r->top + r->bottom) / 2;
    POINT tri[3];
    HBRUSH b = CreateSolidBrush(col);
    HGDIOBJ ob = SelectObject(hdc, b);
    HPEN p = CreatePen(PS_SOLID, 1, col);
    HGDIOBJ op = SelectObject(hdc, p);
    switch (dir) {
    case 0: tri[0].x=cx-3; tri[0].y=cy+2; tri[1].x=cx+3; tri[1].y=cy+2;
            tri[2].x=cx;   tri[2].y=cy-3; break;           /* ▲ */
    case 1: tri[0].x=cx-3; tri[0].y=cy-2; tri[1].x=cx+3; tri[1].y=cy-2;
            tri[2].x=cx;   tri[2].y=cy+3; break;           /* ▼ */
    case 2: tri[0].x=cx+2; tri[0].y=cy-3; tri[1].x=cx+2; tri[1].y=cy+3;
            tri[2].x=cx-3; tri[2].y=cy; break;             /* ◀ */
    default:tri[0].x=cx-2; tri[0].y=cy-3; tri[1].x=cx-2; tri[1].y=cy+3;
            tri[2].x=cx+3; tri[2].y=cy; break;             /* ▶ */
    }
    Polygon(hdc, tri, 3);
    SelectObject(hdc, ob); SelectObject(hdc, op);
    DeleteObject(b); DeleteObject(p);
}

/* ================= 自绘滚动条控件 ================= */
/* 真正的子窗口，不是事后覆盖。直接画暗色，永不闪白。 */
#define DSB_CLASS L"LiteTuneScrollBar"
#define DESC_TAB_CLASS L"LiteTuneDescTab"
/* 滚动条宽度取系统值，确保完全遮盖系统滚动条 */
#define DSB_W 10   /* 自绘滚动条窄宽度（系统条带由 NCCALCSIZE 并回客户区，互不干扰） */

typedef struct {
    int kind;          /* 0=listbox 1=listview 2=多行edit */
    int horizontal;    /* 1=水平（仅 listview 用） */
    int min, max, page, pos;   /* listview=像素，其余=顶部可见项/行索引 */
    int thumbLen, thumbPos;    /* 像素，由 UpdateThumb 计算 */
    int dragging;      /* 正在拖把手 */
    int dragOffset;    /* 拖动起点在把手内的偏移 */
    int repeatDir;     /* 按住箭头/滑槽自动连滚方向：-1 起端 +1 末端 0 无 */
    int repeatPage;    /* 1=滑槽翻页 0=箭头单行 */
    HWND target;       /* 关联的内容控件 */
} DarkScrollBar;

/* 两种主题同一造型，仅配色不同 */
static COLORREF ThSbThumb(void) { return g_darkMode ? RGB(104,104,112) : RGB(202,202,208); }
static COLORREF ThSbArrow(void) { return g_darkMode ? RGB(175,175,183) : RGB(120,120,128); }

/* 用可见项的实际矩形测行高（顶部项一定已布局，比第 0 项可靠；
 * 绝不在两种取法之间来回摇摆——否则把手长度会忽长忽短） */
static int DSB_RowH(HWND lv)
{
    RECT ri;
    DWORD sp;
    int h, top = (int)SendMessageW(lv, LVM_GETTOPINDEX, 0, 0);
    if (top < 0) top = 0;
    /* LVM_GETITEMRECT 的入参 RECT 与 LVITEM 头部重叠：left 字段即 mask，
     * 必须先填 LVIR_BOUNDS（裸栈垃圾会被当 mask → 调用失败且 rect 未填充，
     * 读出栈垃圾行高，拖动滚动条一步滚出几百行） */
    ri.left = LVIR_BOUNDS;
    if (SendMessageW(lv, LVM_GETITEMRECT, top, (LPARAM)&ri) &&
        ri.bottom > ri.top)
        return ri.bottom - ri.top;
    sp = (DWORD)SendMessageW(lv, LVM_GETITEMSPACING, FALSE, 0);
    h = HIWORD(sp);
    return h > 0 ? h : 18;
}

/* 读取目标控件的滚动状态。listbox/edit/listview 纵向统一用“行”单位，
 * listview 横向用 GetScrollInfo 像素。max=最后有效滚动位置。
 * 返回是否可滚动 */
static int DSB_QueryState(DarkScrollBar *dsb)
{
    HWND t = dsb->target;
    RECT rc;
    int n, ih, vis;
    if (!t) return 0;
    GetClientRect(t, &rc);
    if (dsb->kind == 0) {                 /* ListBox：行单位 */
        n  = (int)SendMessageW(t, LB_GETCOUNT, 0, 0);
        ih = (int)SendMessageW(t, LB_GETITEMHEIGHT, 0, 0);
        if (ih <= 0) ih = 16;
        vis = (rc.bottom - rc.top) / ih;
        if (vis < 1) vis = 1;
        dsb->min = 0;
        dsb->max = n > vis ? n - vis : 0;
        dsb->page = vis;
        dsb->pos = (int)SendMessageW(t, LB_GETTOPINDEX, 0, 0);
        return n > vis;
    }
    if (dsb->kind == 2) {                 /* 多行 EDIT：行单位 */
        int lines = (int)SendMessageW(t, EM_GETLINECOUNT, 0, 0);
        int lh = 16;
        HDC dc = GetDC(t);
        if (dc) {
            HFONT f = (HFONT)SendMessageW(t, WM_GETFONT, 0, 0);
            HGDIOBJ of = f ? SelectObject(dc, f) : NULL;
            TEXTMETRICW tm;
            if (GetTextMetricsW(dc, &tm) && tm.tmHeight > 0)
                lh = tm.tmHeight + tm.tmExternalLeading;
            if (f) SelectObject(dc, of);
            ReleaseDC(t, dc);
        }
        if (lh <= 0) lh = 16;
        vis = (rc.bottom - rc.top) / lh;
        if (vis < 1) vis = 1;
        dsb->min = 0;
        dsb->max = lines > vis ? lines - vis : 0;
        dsb->page = vis;
        dsb->pos = (int)SendMessageW(t, EM_GETFIRSTVISIBLELINE, 0, 0);
        return lines > vis;
    }
    if (!dsb->horizontal) {               /* ListView 纵向：行单位（用控件自带口径） */
        int count = (int)SendMessageW(t, LVM_GETITEMCOUNT, 0, 0);
        int per   = (int)SendMessageW(t, LVM_GETCOUNTPERPAGE, 0, 0);
        if (per < 1) per = 1;
        dsb->min = 0;
        dsb->max = count > per ? count - per : 0;
        dsb->page = per;
        dsb->pos = (int)SendMessageW(t, LVM_GETTOPINDEX, 0, 0);
        return count > per;
    }
    /* ListView 横向：控件维护的像素滚动信息。
     * GetScrollInfo 的 nMax 是内容总宽（不是最后滚动位置），
     * 要换算成与纵向一致的口径：max=最后有效滚动位置，
     * 内容不超出一屏时 max==min → SyncScrollBar 自动隐藏 */
    {
        SCROLLINFO si;
        int m;
        ZeroMemory(&si, sizeof(si));
        si.cbSize = sizeof(si);
        si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
        if (!GetScrollInfo(t, SB_HORZ, &si)) {
            dsb->min = 0; dsb->max = 0; dsb->page = 1; dsb->pos = 0;
            return 0;
        }
        m = si.nMax - (int)si.nPage + 1;   /* 最后一个有效滚动位置 */
        if (m < si.nMin) m = si.nMin;
        dsb->min = si.nMin;
        dsb->max = m;
        dsb->page = si.nPage ? (int)si.nPage : 1;
        dsb->pos = si.nPos;
        return dsb->max > dsb->min;
    }
}

/* 箭头单击滚动一步：纵向统一 1 行；listview 横向 16px */
static int DSB_LineStep(const DarkScrollBar *dsb)
{
    if (dsb->kind == 1 && dsb->horizontal) return 16;
    return 1;
}

/* 把目标控件滚动 delta（相对增量，单位同 dsb->pos）。
 * 不回读实际位置——拖动期间由滚动条记账，把手绝对跟手，
 * 消除回读取整导致的“吸住/延迟/拖不到底” */
static void DSB_ScrollTarget(DarkScrollBar *dsb, int delta)
{
    HWND t = dsb->target;
    if (!t || !delta) return;
    if (dsb->kind == 0) {
        SendMessageW(t, LB_SETTOPINDEX, dsb->pos + delta, 0);
    } else if (dsb->kind == 2) {
        SendMessageW(t, EM_LINESCROLL, 0, (LPARAM)delta);
    } else if (dsb->horizontal) {
        SendMessageW(t, LVM_SCROLL, (WPARAM)delta, 0);     /* 横向：像素 */
    } else {
        SendMessageW(t, LVM_SCROLL, 0,
                     (LPARAM)(delta * DSB_RowH(t)));        /* 纵向：行→像素 */
    }
}

static void DSB_UpdateThumb(DarkScrollBar *dsb, HWND hwnd)
{
    RECT rc;
    int track, virtualSize, range, sc;
    GetClientRect(hwnd, &rc);
    track = (dsb->horizontal ? rc.right : rc.bottom) - 2 * DSB_W;
    virtualSize = (dsb->max - dsb->min) + dsb->page;
    if (track <= 0 || virtualSize <= dsb->page || dsb->page <= 0) {
        dsb->thumbLen = 0;
        return;
    }
    dsb->thumbLen = track * dsb->page / virtualSize;
    if (dsb->thumbLen < 20) dsb->thumbLen = 20;
    if (dsb->thumbLen > track) dsb->thumbLen = track;
    range = dsb->max - dsb->min;
    sc = dsb->pos - dsb->min;
    if (sc < 0) sc = 0;
    if (sc > range) sc = range;
    dsb->thumbPos = (range > 0 && track > dsb->thumbLen)
        ? sc * (track - dsb->thumbLen) / range : 0;
}

/* 滚动到 pos（滚动条自己记账，不回读；拖动期间把手绝对跟手） */
static void DSB_SetPos(DarkScrollBar *dsb, HWND hwnd, int pos)
{
    int last = dsb->max;
    int delta;
    if (pos < dsb->min) pos = dsb->min;
    if (pos > last) pos = last;
    delta = pos - dsb->pos;
    if (!delta) return;
    DSB_ScrollTarget(dsb, delta);
    dsb->pos = pos;
    DSB_UpdateThumb(dsb, hwnd);
    InvalidateRect(hwnd, NULL, FALSE);
}

static void DSB_Draw(HWND hwnd, DarkScrollBar *dsb)
{
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    RECT rc, btn;
    HBRUSH thumb;
    HGDIOBJ ob, op;
    int horiz = dsb ? dsb->horizontal : 0;

    GetClientRect(hwnd, &rc);
    FillRectColor(hdc, &rc, ThPanel());

    /* 两端箭头 */
    if (horiz) {
        btn.top = 0; btn.bottom = rc.bottom;
        btn.left = 0; btn.right = DSB_W;
        DrawScrollArrow(hdc, &btn, 2, ThSbArrow());
        btn.left = rc.right - DSB_W; btn.right = rc.right;
        DrawScrollArrow(hdc, &btn, 3, ThSbArrow());
    } else {
        btn.left = 0; btn.right = rc.right;
        btn.top = 0; btn.bottom = DSB_W;
        DrawScrollArrow(hdc, &btn, 0, ThSbArrow());
        btn.top = rc.bottom - DSB_W; btn.bottom = rc.bottom;
        DrawScrollArrow(hdc, &btn, 1, ThSbArrow());
    }

    /* 把手（内容不满一屏时只画空轨道） */
    if (dsb && dsb->thumbLen > 0) {
        int t0 = DSB_W + dsb->thumbPos;
        thumb = CreateSolidBrush(ThSbThumb());
        ob = SelectObject(hdc, thumb);
        op = SelectObject(hdc, GetStockObject(NULL_PEN));
        if (horiz)
            RoundRect(hdc, t0, 2, t0 + dsb->thumbLen, rc.bottom - 2, 4, 4);
        else
            RoundRect(hdc, 2, t0, rc.right - 2, t0 + dsb->thumbLen, 4, 4);
        SelectObject(hdc, op);
        SelectObject(hdc, ob);
        DeleteObject(thumb);
    }
    EndPaint(hwnd, &ps);
}

static LRESULT CALLBACK DarkScrollBarProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    DarkScrollBar *dsb = (DarkScrollBar*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCTW *cs = (CREATESTRUCTW*)lp;
        int prm = (int)(INT_PTR)cs->lpCreateParams;
        dsb = (DarkScrollBar*)calloc(1, sizeof(DarkScrollBar));
        if (!dsb) return -1;
        dsb->kind = prm & 0xFF;
        dsb->horizontal = (prm & 0x100) ? 1 : 0;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)dsb);
        return 0;
    }
    case WM_DESTROY:
        free(dsb);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        DSB_Draw(hwnd, dsb);
        return 0;
    case WM_SIZE:
        if (dsb) {
            DSB_UpdateThumb(dsb, hwnd);
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    case WM_LBUTTONDOWN: {
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        RECT rc;
        int c, len, t0;
        if (!dsb) return 0;
        GetClientRect(hwnd, &rc);
        len = dsb->horizontal ? rc.right : rc.bottom;
        c = dsb->horizontal ? pt.x : pt.y;
        DSB_QueryState(dsb);
        DSB_UpdateThumb(dsb, hwnd);
        if (dsb->thumbLen <= 0) return 0;
        SetCapture(hwnd);
        dsb->repeatDir = 0;
        t0 = DSB_W + dsb->thumbPos;
        if (c < DSB_W) {                     /* 起端箭头：滚一行 */
            DSB_SetPos(dsb, hwnd, dsb->pos - DSB_LineStep(dsb));
            dsb->repeatDir = -1; dsb->repeatPage = 0;
        } else if (c > len - DSB_W) {        /* 末端箭头：滚一行 */
            DSB_SetPos(dsb, hwnd, dsb->pos + DSB_LineStep(dsb));
            dsb->repeatDir = 1; dsb->repeatPage = 0;
        } else if (c >= t0 && c < t0 + dsb->thumbLen) {
            dsb->dragging = 1;
            dsb->dragOffset = c - t0;
        } else if (c < t0) {                 /* 上/左滑槽：翻页 */
            DSB_SetPos(dsb, hwnd, dsb->pos - dsb->page);
            dsb->repeatDir = -1; dsb->repeatPage = 1;
        } else {                             /* 下/右滑槽：翻页 */
            DSB_SetPos(dsb, hwnd, dsb->pos + dsb->page);
            dsb->repeatDir = 1; dsb->repeatPage = 1;
        }
        if (dsb->repeatDir)                  /* 按住箭头/滑槽：先延迟再连滚 */
            SetTimer(hwnd, 2, 350, NULL);
        return 0;
    }
    case WM_TIMER: {
        /* 按住箭头/滑槽自动连滚：首次 350ms 延迟后每 50ms 一步；
         * 每拍重新做命中测试，指针挪开就停 */
        POINT p;
        RECT rc;
        int c, len, t0, dir = 0, page = 0;
        if (!dsb || wp != 2 || !dsb->repeatDir || dsb->dragging) return 0;
        if (!(GetKeyState(VK_LBUTTON) & 0x8000)) {
            KillTimer(hwnd, 2);
            dsb->repeatDir = 0;
            return 0;
        }
        GetCursorPos(&p);
        ScreenToClient(hwnd, &p);
        GetClientRect(hwnd, &rc);
        len = dsb->horizontal ? rc.right : rc.bottom;
        c = dsb->horizontal ? p.x : p.y;
        t0 = DSB_W + dsb->thumbPos;
        if (c < DSB_W)              { dir = -1; page = 0; }
        else if (c > len - DSB_W)   { dir = 1;  page = 0; }
        else if (c < t0)            { dir = -1; page = 1; }
        else if (c >= t0 + dsb->thumbLen) { dir = 1; page = 1; }
        if (!dir || dir != dsb->repeatDir || page != dsb->repeatPage)
            return 0;                        /* 指针离开/跨过把手：原地待命 */
        DSB_SetPos(dsb, hwnd, dsb->pos +
                   (page ? dsb->page : DSB_LineStep(dsb)) * dir);
        SetTimer(hwnd, 2, 50, NULL);         /* 后续连滚间隔 */
        return 0;
    }
    case WM_MOUSEMOVE:
        if (dsb && dsb->dragging) {
            RECT rc;
            int track, c;
            GetClientRect(hwnd, &rc);
            track = (dsb->horizontal ? rc.right : rc.bottom) - 2 * DSB_W;
            c = (dsb->horizontal ? GET_X_LPARAM(lp) : GET_Y_LPARAM(lp)) - DSB_W - dsb->dragOffset;
            if (track > dsb->thumbLen) {
                int range = dsb->max - dsb->min;
                if (range > 0)
                    DSB_SetPos(dsb, hwnd, dsb->min + c * range / (track - dsb->thumbLen));
            }
        }
        return 0;
    case WM_LBUTTONUP:
        if (dsb) {
            dsb->dragging = 0;
            dsb->repeatDir = 0;
            KillTimer(hwnd, 2);
            /* 松手后从控件读回实际位置，修正拖动中累积的夹紧误差 */
            DSB_QueryState(dsb);
            DSB_UpdateThumb(dsb, hwnd);
            InvalidateRect(hwnd, NULL, FALSE);
        }
        ReleaseCapture();
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/* 创建自绘滚动条。kind：0=listbox 1=listview 2=edit；horizontal 仅 listview 用 */
static HWND CreateDarkScrollBar(HWND parent, HWND target, int kind, int horizontal, int id)
{
    int prm = (kind & 0xFF) | (horizontal ? 0x100 : 0);
    HWND h = CreateWindowExW(0, DSB_CLASS, NULL, WS_CHILD | WS_VISIBLE,
        0, 0, 0, 0, parent, (HMENU)(INT_PTR)id, NULL, (LPVOID)(INT_PTR)prm);
    if (h) {
        DarkScrollBar *dsb = (DarkScrollBar*)GetWindowLongPtrW(h, GWLP_USERDATA);
        if (dsb) dsb->target = target;
    }
    return h;
}

/* 定期同步：从目标控件读实际滚动位置刷新把手。
 * 内容不超范围时隐藏滚动条 */
static void SyncScrollBar(HWND hbar)
{
    DarkScrollBar *dsb;
    int oldLen, oldPos, scrollable;
    if (!hbar) return;
    dsb = (DarkScrollBar*)GetWindowLongPtrW(hbar, GWLP_USERDATA);
    if (!dsb || dsb->dragging) return;
    oldLen = dsb->thumbLen; oldPos = dsb->thumbPos;
    scrollable = DSB_QueryState(dsb);
    DSB_UpdateThumb(dsb, hbar);
    ShowWindow(hbar, scrollable ? SW_SHOW : SW_HIDE);
    if (dsb->thumbLen != oldLen || dsb->thumbPos != oldPos)
        InvalidateRect(hbar, NULL, FALSE);
}

/* 简介区的滚条只在简介可见时同步：SyncScrollBar 会按内容重新 ShowWindow，
 * 简介关闭后若还调它，会把条弹回旧位置，屏幕上就像留了一条残影 */
static void SyncDescScrollBar(void)
{
    if (!g_descHidden) SyncScrollBar(g_vsDesc);
}

/* 暗色表头自绘（WM_DRAWITEM/ODT_HEADER 由表头的父窗口 ListView 接收） */
static int DrawHeaderItem(DRAWITEMSTRUCT *di)
{
    wchar_t htext[64] = L"";
    RECT rc = di->rcItem, tr;
    HBRUSH bg = CreateSolidBrush(RGB(46, 46, 52));
    HPEN sep = CreatePen(PS_SOLID, 1, RGB(28, 28, 32));
    HGDIOBJ op;
    /* 列文本直接向表头控件取，普通/缓存两种列序都能正确显示 */
    if (di->itemID < 3) {
        HDITEMW hi;
        ZeroMemory(&hi, sizeof(hi));
        hi.mask = HDI_TEXT;
        hi.pszText = htext;
        hi.cchTextMax = 64;
        SendMessageW(di->hwndItem, HDM_GETITEMW, di->itemID, (LPARAM)&hi);
    }
    FillRect(di->hDC, &rc, bg);
    DeleteObject(bg);
    /* 底分隔线 */
    op = SelectObject(di->hDC, sep);
    MoveToEx(di->hDC, rc.left, rc.bottom - 1, NULL);
    LineTo(di->hDC, rc.right, rc.bottom - 1);
    /* 列间竖分隔线 */
    if (di->itemID > 0) {
        MoveToEx(di->hDC, rc.left, 3, NULL);
        LineTo(di->hDC, rc.left, rc.bottom - 4);
    }
    SelectObject(di->hDC, op);
    DeleteObject(sep);
    SetBkMode(di->hDC, TRANSPARENT);
    SetTextColor(di->hDC, RGB(210, 210, 214));
    SelectObject(di->hDC, g_font);
    tr = rc; tr.left += 8; tr.right -= 14;
    if (htext[0])
        DrawTextW(di->hDC, htext, -1, &tr,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    /* 当前排序列右侧画升降序小三角（仅剧集列表有排序状态）。
     * di->itemID 是列索引，g_sortCol 是排序枚举值，需按视图映射回来：
     *   普通视图：0=标题 1=日期 2=时长
     *   缓存视图：0=播客 1=标题 2=时长 */
    {
        int sortColIdx = -1;
        if (g_viewCached) {
            if (g_sortCol == SORT_PODCAST) sortColIdx = 0;
            else if (g_sortCol == SORT_TITLE) sortColIdx = 1;
            else if (g_sortCol == SORT_DUR) sortColIdx = 2;
        } else {
            if (g_sortCol == SORT_TITLE) sortColIdx = 0;
            else if (g_sortCol == SORT_DATE) sortColIdx = 1;
            else if (g_sortCol == SORT_DUR) sortColIdx = 2;
        }
        if ((int)di->itemID == sortColIdx && GetParent(di->hwndItem) == g_listEp) {
            int cx = rc.right - 9, cy = rc.top + (rc.bottom - rc.top) / 2;
            HBRUSH ab = CreateSolidBrush(RGB(210, 210, 214));
            HGDIOBJ ob = SelectObject(di->hDC, ab);
            POINT tri[3];
            if (g_sortDir < 0) {  /* 降序 ▼ */
                tri[0].x=cx-3; tri[0].y=cy-1;
                tri[1].x=cx+3; tri[1].y=cy-1;
                tri[2].x=cx;   tri[2].y=cy+4;
            } else {             /* 升序 ▲ */
                tri[0].x=cx-3; tri[0].y=cy+2;
                tri[1].x=cx+3; tri[1].y=cy+2;
                tri[2].x=cx;   tri[2].y=cy-3;
            }
            Polygon(di->hDC, tri, 3);
            SelectObject(di->hDC, ob);
            DeleteObject(ab);
        }
    }
    return 1;
}

/* 创建指定字号/字重的微软雅黑字体 */
static HFONT CreateUiFont(int pt, int weight)
{
    HDC hdc = GetDC(NULL);
    int h = -MulDiv(pt, GetDeviceCaps(hdc, LOGPIXELSY), 72);
    ReleaseDC(NULL, hdc);
    return CreateFontW(h, 0, 0, 0, weight, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
}

/* 把当前 g_font 应用到所有控件（改字号级别后也要重发 WM_SETFONT） */
static void ApplyFontsToControls(void)
{
    HWND ctrls[] = {
        g_sldSeek, g_stTime,
        g_btnPrev, g_btnPlay, g_btnNext, g_btnStop, g_btnMute,
        g_sldVol, g_stVol, g_stFormat,
        g_btnAdd, g_btnDel, g_btnRefresh, g_btnPlayMode, g_btnDir, g_btnCache,
        g_listPod, g_listEp, g_editDesc
    };
    int i;
    for (i = 0; i < (int)(sizeof(ctrls)/sizeof(ctrls[0])); i++)
        if (ctrls[i]) SendMessageW(ctrls[i], WM_SETFONT, (WPARAM)g_font, TRUE);
    if (g_stFormat) SendMessageW(g_stFormat, WM_SETFONT, (WPARAM)g_fontSmall, TRUE);
}

/* 直接设定字号级别 0/1/2 并重建三套字体、刷新界面 */
static void ChangeFontLevel(int nv)
{
    if (nv < 0) nv = 0;
    if (nv > 2) nv = 2;
    if (nv == g_fontLevel) return;
    g_fontLevel = nv;
    if (g_font)      DeleteObject(g_font);
    if (g_fontBold)  DeleteObject(g_fontBold);
    if (g_fontSmall) DeleteObject(g_fontSmall);
    /* 级别0：正文8pt/播客标题8pt加粗/副标题7pt，每升一级全部+1pt */
    g_font      = CreateUiFont(8 + nv, FW_NORMAL);
    g_fontBold  = CreateUiFont(8 + nv, FW_BOLD);
    g_fontSmall = CreateUiFont(7 + nv, FW_NORMAL);
    ApplyFontsToControls();
    InvalidateRect(g_listPod, NULL, TRUE);
    InvalidateRect(g_listEp, NULL, TRUE);
    InvalidateRect(g_btnPlay, NULL, TRUE);
    InvalidateRect(g_btnMute, NULL, TRUE);
    ApplyLayout(g_hwnd, IsWindowVisible(g_hwnd));
    UpdateMarquee();
}

static void FmtTime(long long sec, wchar_t *buf, size_t n)
{
    if (sec < 0) sec = 0;
    if (sec >= 3600)
        swprintf(buf, n, L"%02d:%02d:%02d", (int)(sec/3600), (int)((sec%3600)/60), (int)(sec%60));
    else
        swprintf(buf, n, L"%02d:%02d", (int)(sec/60), (int)(sec%60));
}

/* 进度文字“当前 / 总时长”；总时长未知时显示 --:-- */
static void ShowTimeLabel(int posSec, int durSec)
{
    wchar_t buf[64], a[16], b[16];
    FmtTime(posSec, a, 16);
    if (durSec > 0) FmtTime(durSec, b, 16);
    else wcscpy_s(b, 16, L"--:--");
    swprintf(buf, 64, L"%ls / %ls", a, b);
    SetWindowTextW(g_stTime, buf);
}

/* 顶部播放区第一行（状态跑马灯）的客户区矩形，几何与 LayoutControls 保持一致 */
static void StatusRowRect(RECT *r)
{
    RECT rc;
    GetClientRect(g_hwnd, &rc);
    r->left = MARGIN + 8;
    r->right = rc.right - MARGIN - 8;
    r->top = MARGIN + 6;
    r->bottom = r->top + 18;
}

/* 文本变化/字号变化/窗口大小变化后：重测宽度，决定是否开启滚动定时器 */
static void UpdateMarquee(void)
{
    RECT r;
    HDC hdc;
    SIZE sz;
    HFONT old;
    if (!g_hwnd || !IsWindow(g_hwnd)) return;
    StatusRowRect(&r);
    hdc = GetDC(g_hwnd);
    old = (HFONT)SelectObject(hdc, g_font);
    sz.cx = 0; sz.cy = 0;
    GetTextExtentPoint32W(hdc, g_statusText, (int)wcslen(g_statusText), &sz);
    SelectObject(hdc, old);
    ReleaseDC(g_hwnd, hdc);
    g_mqTextW = sz.cx;
    if (sz.cx > (r.right - r.left)) {
        if (!g_mqActive) { g_mqActive = 1; SetTimer(g_hwnd, 2, 40, NULL); }
    } else {
        g_mqActive = 0;
        g_mqOffset = 0;
        KillTimer(g_hwnd, 2);
    }
    InvalidateRect(g_hwnd, &r, FALSE);
}

/* 把 text 显示到状态行：清跑马灯偏移并按新文本重测宽度 */
static void StatusShow(const wchar_t *text)
{
    wcsncpy(g_statusText, text, 511);
    g_statusText[511] = 0;
    g_mqOffset = 0;
    UpdateMarquee();
}

/* 设置持久状态（如“正在播放：…”）；若正在显示临时提示，只更新备份，
 * 等临时提示结束后自然恢复为新内容 */
static void SetStatus(const wchar_t *text)
{
    if (!text) text = L"";
    /* 批量缓存进行中：播放器/其它事件的状态写入 saved base，
     * 不抢批量进度的显示（完成后才恢复） */
    if (g_batchActive) {
        wcsncpy(g_batchSavedBase, text, 511);
        g_batchSavedBase[511] = 0;
        return;
    }
    wcsncpy(g_statusBase, text, 511);
    g_statusBase[511] = 0;
    if (!g_statusTempOn) StatusShow(g_statusBase);   /* 临时提示显示中不抢屏 */
}

/* 批量进度专用：直接写 g_statusBase + g_statusText（绕过重定向） */
static void SetBatchProgress(const wchar_t *text)
{
    if (!text) text = L"";
    wcsncpy(g_statusBase, text, 511);
    g_statusBase[511] = 0;
    if (!g_statusTempOn) StatusShow(g_statusBase);
}

/* 临时提示（如“夜间模式已开启”）：显示片刻后自动恢复持久状态 */
static void SetStatusTemp(const wchar_t *text)
{
    g_statusTempOn = 1;
    StatusShow(text ? text : L"");
    SetTimer(g_hwnd, 4, TEMP_STATUS_MS, NULL);   /* 重复调用会自动重置计时 */
}

/* 临时提示到期：恢复显示持久状态 */
static void RestoreBaseStatus(void)
{
    g_statusTempOn = 0;
    KillTimer(g_hwnd, 4);
    StatusShow(g_statusBase);
}

static wchar_t* U8ToW(const char *src)
{
    int n = MultiByteToWideChar(CP_UTF8, 0, src, -1, NULL, 0);
    wchar_t *dst;
    if (n <= 0) return NULL;
    dst = (wchar_t*)malloc(n * sizeof(wchar_t));
    if (dst) MultiByteToWideChar(CP_UTF8, 0, src, -1, dst, n);
    return dst;
}

/* ================= HTTP 下载 ================= */
/* 下载整个 URL 到内存。progressMsg!=0 时向 g_hwnd 汇报百分比。 */
/* pReqSlot：调用方提供的槽位，函数把请求句柄放进去，外部线程可关闭它来中止下载；
 * 传 NULL 表示不可取消（RSS/封面图）。句柄归还用 Interlocked 交换，避免双重关闭。
 * pProgGen：进度消息 lParam 带上该代号，主线程据此丢弃过期任务的进度。 */
/* 打开 WinHTTP 会话：优先使用当前用户的 IE/系统代理设置。
 * WINHTTP_ACCESS_TYPE_DEFAULT_PROXY 只读机器级 WinHTTP 代理（通常是直连），
 * 会让 Clash 等用户级代理软件的用户无法访问被墙的订阅源（SendRequest 超时） */
static HINTERNET WinHttpOpenUser(void)
{
    WINHTTP_CURRENT_USER_IE_PROXY_CONFIG ie;
    HINTERNET s = NULL;
    ZeroMemory(&ie, sizeof(ie));
    if (WinHttpGetIEProxyConfigForCurrentUser(&ie)) {
        if (ie.lpszProxy && ie.lpszProxy[0]) {
            s = WinHttpOpen(L"LiteTune/1.4", WINHTTP_ACCESS_TYPE_NAMED_PROXY,
                ie.lpszProxy, ie.lpszProxyBypass, 0);
        }
        if (ie.lpszAutoConfigUrl) GlobalFree(ie.lpszAutoConfigUrl);
        if (ie.lpszProxy) GlobalFree(ie.lpszProxy);
        if (ie.lpszProxyBypass) GlobalFree(ie.lpszProxyBypass);
    }
    if (s) return s;
    return WinHttpOpen(L"LiteTune/1.4",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
}

static char* HttpFetch(const wchar_t *url, DWORD *outLen, UINT progressMsg,
                       void * volatile *pReqSlot, volatile long *pProgGen)
{
    URL_COMPONENTS uc;
    wchar_t host[256], path[2048];
    HINTERNET hSes = NULL, hCon = NULL, hReq = NULL;
    char *buf = NULL;
    DWORD cap = 0, total = 0;
    DWORD contentLen = 0;

    ZeroMemory(&uc, sizeof(uc));
    uc.dwStructSize = sizeof(uc);
    uc.lpszHostName = host; uc.dwHostNameLength = 256;
    uc.lpszUrlPath  = path; uc.dwUrlPathLength  = 2048;
    if (!WinHttpCrackUrl(url, 0, 0, &uc)) return NULL;

    hSes = WinHttpOpenUser();
    if (!hSes) return NULL;
    WinHttpSetTimeouts(hSes, 30000, 30000, 60000, 60000);

    hCon = WinHttpConnect(hSes, host,
        uc.nScheme == INTERNET_SCHEME_HTTPS ? INTERNET_DEFAULT_HTTPS_PORT
                                            : INTERNET_DEFAULT_HTTP_PORT, 0);
    if (!hCon) goto fail;
    hReq = WinHttpOpenRequest(hCon, L"GET", path, NULL,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
        uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0);
    if (!hReq) goto fail;

    /* 登记请求句柄到调用方槽位，供 UI 线程关闭以中止下载 */
    if (pReqSlot)
        InterlockedExchangePointer((PVOID volatile*)pReqSlot, (PVOID)hReq);

    /* 允许 gzip/deflate 自动解压 */
    {
        DWORD flags = WINHTTP_DECOMPRESSION_FLAG_GZIP | WINHTTP_DECOMPRESSION_FLAG_DEFLATE;
        WinHttpSetOption(hReq, WINHTTP_OPTION_DECOMPRESSION, &flags, sizeof(flags));
    }

    if (!WinHttpSendRequest(hReq, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
            WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) goto fail;
    if (!WinHttpReceiveResponse(hReq, NULL)) goto fail;

    {
        DWORD status = 0, sz = sizeof(status);
        WinHttpQueryHeaders(hReq, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            NULL, &status, &sz, NULL);
        if (status < 200 || status >= 300) goto fail;
        sz = sizeof(contentLen);
        WinHttpQueryHeaders(hReq, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
            NULL, &contentLen, &sz, NULL);
    }

    for (;;) {
        DWORD avail = 0, got = 0;
        if (!WinHttpQueryDataAvailable(hReq, &avail)) goto fail;
        if (avail == 0) break;
        if (total + avail + 1 > cap) {
            DWORD ncap = cap ? cap * 2 : 262144;
            char *nb;
            while (ncap < total + avail + 1) ncap *= 2;
            nb = (char*)realloc(buf, ncap);
            if (!nb) goto fail;
            buf = nb; cap = ncap;
        }
        if (!WinHttpReadData(hReq, buf + total, avail, &got)) goto fail;
        if (got == 0) break;
        total += got;
        if (progressMsg && contentLen > 0)
            PostMessageW(g_hwnd, progressMsg,
                (WPARAM)((unsigned __int64)total * 100 / contentLen),
                (LPARAM)(pProgGen ? *pProgGen : 0));
    }

    /* 成功：取回句柄自行关闭；若槽位已空说明被外部取消，数据不完整算失败。
     * WinHTTP 句柄必须从内层到外层释放（request → connect → session）。 */
    {
        HINTERNET own = pReqSlot
            ? (HINTERNET)InterlockedExchangePointer((PVOID volatile*)pReqSlot, NULL)
            : hReq;
        if (pReqSlot && !own) {        /* 被 UI 取消 */
            WinHttpCloseHandle(hCon);
            WinHttpCloseHandle(hSes);
            free(buf);
            return NULL;
        }
        WinHttpCloseHandle(own);      /* request */
        WinHttpCloseHandle(hCon);     /* connect */
        WinHttpCloseHandle(hSes);     /* session */
    }
    if (!buf) { buf = (char*)malloc(1); if (!buf) return NULL; buf[0] = 0; }
    else buf[total] = 0;
    *outLen = total;
    return buf;

fail:
    {
        HINTERNET own = pReqSlot
            ? (HINTERNET)InterlockedExchangePointer((PVOID volatile*)pReqSlot, NULL)
            : hReq;
        if (own) WinHttpCloseHandle(own);   /* 外部已关闭则跳过 */
    }
    if (hCon) WinHttpCloseHandle(hCon);
    if (hSes) WinHttpCloseHandle(hSes);
    free(buf);
    return NULL;
}

/* ================= XML 解析 ================= */
/* 在 xml 中找 <tag ...>，tag 后必须是 > 空格 / 等边界 */
static const char* FindOpenTag(const char *xml, const char *tag)
{
    char needle[96];
    const char *p;
    snprintf(needle, sizeof(needle), "<%s", tag);
    p = xml;
    while ((p = strstr(p, needle)) != NULL) {
        char c = p[strlen(needle)];
        if (c == '>' || c == ' ' || c == '/' || c == '\t' || c == '\r' || c == '\n')
            return p;
        p += strlen(needle);
    }
    return NULL;
}

/* 提取 <tag>content</tag> 的原始内容（复制到新缓冲区） */
static char* XmlText(const char *xml, const char *tag)
{
    const char *open = FindOpenTag(xml, tag);
    const char *gt, *close;
    char closeTag[104];
    int len;
    char *buf;
    if (!open) return NULL;
    gt = strchr(open, '>');
    if (!gt) return NULL;
    gt++;
    snprintf(closeTag, sizeof(closeTag), "</%s", tag);
    close = strstr(gt, closeTag);
    if (!close || close <= gt) return NULL;
    len = (int)(close - gt);
    buf = (char*)malloc(len + 1);
    if (!buf) return NULL;
    memcpy(buf, gt, len);
    buf[len] = 0;
    return buf;
}

/* 提取标签属性值（在 [tagStart, tagEnd) 范围内） */
static char* XmlAttrRange(const char *tagStart, const char *tagEnd, const char *attr)
{
    char needle[80];
    const char *p, *q;
    char *buf;
    int len;
    snprintf(needle, sizeof(needle), "%s=\"", attr);
    p = tagStart;
    while ((p = strstr(p, needle)) != NULL && p < tagEnd) {
        p += strlen(needle);
        q = strchr(p, '"');
        if (!q || q > tagEnd) return NULL;
        len = (int)(q - p);
        buf = (char*)malloc(len + 1);
        if (buf) { memcpy(buf, p, len); buf[len] = 0; }
        return buf;
    }
    /* 单引号 */
    snprintf(needle, sizeof(needle), "%s='", attr);
    p = tagStart;
    while ((p = strstr(p, needle)) != NULL && p < tagEnd) {
        p += strlen(needle);
        q = strchr(p, '\'');
        if (!q || q > tagEnd) return NULL;
        len = (int)(q - p);
        buf = (char*)malloc(len + 1);
        if (buf) { memcpy(buf, p, len); buf[len] = 0; }
        return buf;
    }
    return NULL;
}

/* CDATA 解包 + 实体解码 + 去 HTML 标签，原地处理 */
static void CleanXmlText(char *s)
{
    char *dst;
    const char *src;
    size_t n;

    /* CDATA */
    if (strncmp(s, "<![CDATA[", 9) == 0) {
        size_t len = strlen(s);
        if (len >= 12 && strcmp(s + len - 3, "]]>") == 0) {
            memmove(s, s + 9, len - 12);
            s[len - 12] = 0;
        }
    }
    /* 去 HTML 标签 + 解码实体 */
    dst = s; src = s;
    while (*src) {
        if (*src == '<') {
            const char *gt = strchr(src, '>');
            if (gt) {
                /* <br> <p> </p> <div> </div> 转成换行 */
                size_t tagLen = (size_t)(gt - src);
                if (tagLen <= 6 &&
                    (strnicmp(src, "<br", 3) == 0 || strnicmp(src, "</br>", 5) == 0 ||
                     strnicmp(src, "<p>", 3) == 0 || strnicmp(src, "</p>", 4) == 0 ||
                     strnicmp(src, "<div>", 5) == 0 || strnicmp(src, "</div>", 6) == 0)) {
                    *dst++ = '\r'; *dst++ = '\n';
                }
                src = gt + 1; continue;
            }
        }
        if (*src == '&') {
            if (strncmp(src, "&lt;", 4) == 0)        { *dst++ = '<';  src += 4; continue; }
            if (strncmp(src, "&gt;", 4) == 0)        { *dst++ = '>';  src += 4; continue; }
            if (strncmp(src, "&amp;", 5) == 0)       { *dst++ = '&';  src += 5; continue; }
            if (strncmp(src, "&quot;", 6) == 0)      { *dst++ = '"';  src += 6; continue; }
            if (strncmp(src, "&apos;", 6) == 0)      { *dst++ = '\''; src += 6; continue; }
            if (strncmp(src, "&nbsp;", 6) == 0)      { *dst++ = ' ';  src += 6; continue; }
            if (strncmp(src, "&#", 2) == 0) {
                const char *semi = strchr(src, ';');
                long code = 0;
                if (semi && semi - src < 12) {
                    if (src[2] == 'x' || src[2] == 'X') code = strtol(src + 3, NULL, 16);
                    else code = strtol(src + 2, NULL, 10);
                    if (code > 0 && code < 0x110000) {
                        /* 简单转 UTF-8 */
                        if (code < 0x80) *dst++ = (char)code;
                        else if (code < 0x800) {
                            *dst++ = (char)(0xC0 | (code >> 6));
                            *dst++ = (char)(0x80 | (code & 0x3F));
                        } else if (code < 0x10000) {
                            *dst++ = (char)(0xE0 | (code >> 12));
                            *dst++ = (char)(0x80 | ((code >> 6) & 0x3F));
                            *dst++ = (char)(0x80 | (code & 0x3F));
                        } else {
                            *dst++ = (char)(0xF0 | (code >> 18));
                            *dst++ = (char)(0x80 | ((code >> 12) & 0x3F));
                            *dst++ = (char)(0x80 | ((code >> 6) & 0x3F));
                            *dst++ = (char)(0x80 | (code & 0x3F));
                        }
                        src = semi + 1;
                        continue;
                    }
                }
            }
        }
        *dst++ = *src++;
    }
    *dst = 0;

    /* 首尾空白 */
    src = s;
    while (*src == ' ' || *src == '\t' || *src == '\r' || *src == '\n') src++;
    if (src != s) memmove(s, src, strlen(src) + 1);
    n = strlen(s);
    while (n > 0 && (s[n-1] == ' ' || s[n-1] == '\t' || s[n-1] == '\r' || s[n-1] == '\n'))
        s[--n] = 0;
    /* \n -> \r\n（编辑框需要）。重新生成到新缓冲区再拷回太长，直接保留 \n 也可读，
       这里简单替换单个 \n 为 \r\n 需要扩容，交给调用方处理。 */
}

/* 把 UTF-8 文本中的 \n 转成 \r\n 并转为 wchar_t */
static wchar_t* U8ToWCrlf(const char *src)
{
    size_t extra = 0, i, len = strlen(src);
    char *tmp, *d;
    wchar_t *w;
    for (i = 0; i < len; i++)
        if (src[i] == '\n' && (i == 0 || src[i-1] != '\r')) extra++;
    tmp = (char*)malloc(len + extra + 1);
    if (!tmp) return NULL;
    d = tmp;
    for (i = 0; i < len; i++) {
        if (src[i] == '\n' && (i == 0 || src[i-1] != '\r')) *d++ = '\r';
        *d++ = src[i];
    }
    *d = 0;
    w = U8ToW(tmp);
    free(tmp);
    return w;
}

/* 取 RSS 频道文本 */
static wchar_t* GetXmlTextW(const char *xml, const char *tag, int crlf)
{
    char *raw = XmlText(xml, tag);
    wchar_t *w;
    if (!raw) return NULL;
    CleanXmlText(raw);
    w = crlf ? U8ToWCrlf(raw) : U8ToW(raw);
    free(raw);
    return w;
}

static int ParseDuration(const char *s)
{
    int h = 0, m = 0, sec = 0;
    if (!s || !*s) return 0;
    if (sscanf(s, "%d:%d:%d", &h, &m, &sec) == 3) return h*3600 + m*60 + sec;
    if (sscanf(s, "%d:%d", &m, &sec) == 2)         return m*60 + sec;
    return atoi(s);
}

/* RFC822（Tue, 03 Feb 2015 12:34:56 ...）/ ISO8601（2015-02-03T12:34:56...）
 * → 分钟数（year*12*31*24*60 近似），同时输出 "YYYY/MM/DD HH:MM" */
static long long ParseDate(const char *s, wchar_t *outText, int outN)
{
    static const char *months[] =
        { "Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec" };
    int year = 0, mon = 0, day = 0, hour = 0, min = 0, i;
    const char *mp = NULL;
    long long key;

    if (outText && outN > 0) outText[0] = 0;
    if (!s) return 0;

    /* ISO8601: 2015-02-03T12:34:56... / 2015-02-03 12:34
     * 先查长度再按下标取字符，避免短字符串越界读 */
    if (strlen(s) >= 10 && isdigit((unsigned char)s[0]) && s[4] == '-' && s[7] == '-') {
        year = atoi(s);
        mon  = atoi(s + 5);
        day  = atoi(s + 8);
        if (strlen(s) >= 16 && (s[10] == 'T' || s[10] == ' ')) {
            hour = atoi(s + 11);
            if (s[13] == ':') min = atoi(s + 14);
        }
    } else {
        for (i = 0; i < 12; i++) {
            const char *hit = strstr(s, months[i]);
            if (hit) { mp = hit; mon = i + 1; break; }
        }
        if (mp) {
            const char *p;
            /* 年份：月份之后第一个 4 位数 */
            for (p = mp + 3; *p; p++) {
                if (isdigit((unsigned char)p[0]) && isdigit((unsigned char)p[1]) &&
                    isdigit((unsigned char)p[2]) && isdigit((unsigned char)p[3])) {
                    year = (p[0]-'0')*1000 + (p[1]-'0')*100 + (p[2]-'0')*10 + (p[3]-'0');
                    break;
                }
            }
            /* 时分：年份之后形如 "HH:MM" */
            for (p = mp + 3; *p; p++) {
                if (isdigit((unsigned char)p[0]) && isdigit((unsigned char)p[1]) &&
                    p[2] == ':' && isdigit((unsigned char)p[3]) && isdigit((unsigned char)p[4])) {
                    int h = (p[0]-'0')*10 + (p[1]-'0');
                    int m = (p[3]-'0')*10 + (p[4]-'0');
                    if (h >= 0 && h < 24 && m >= 0 && m < 60) { hour = h; min = m; }
                    break;
                }
            }
            /* 日：月份之前最后一个 1~2 位数 */
            for (p = mp - 1; p >= s; p--) {
                if (isdigit((unsigned char)p[0])) {
                    int d = *p - '0';
                    if (p > s && isdigit((unsigned char)p[-1])) { d += (p[-1]-'0')*10; }
                    day = d;
                    break;
                }
            }
        }
    }

    if (year >= 1900 && mon >= 1 && mon <= 12 && day >= 1 && day <= 31) {
        if (outText) swprintf(outText, outN, L"%04d/%02d/%02d %02d:%02d", year, mon, day, hour, min);
        /* 分钟级排序键：年月日时分压进一个 long long，比较即排序 */
        key = (long long)year * 100000000LL + (long long)mon * 1000000LL
            + (long long)day * 10000LL + (long long)hour * 100LL + min;
        return key;
    }
    return 0;
}

/* 解析 RSS，填充 Podcast */
static int ParseFeed(const char *xml, Podcast *pod)
{
    const char *chanEnd, *item;

    memset(pod, 0, sizeof(*pod));

    /* 频道信息：只取第一个 <item> 之前的部分，避免拿到单集标题 */
    item = FindOpenTag(xml, "item");
    chanEnd = item ? item : xml + strlen(xml);
    {
        size_t clen = (size_t)(chanEnd - xml);
        char *head = (char*)malloc(clen + 1);
        if (!head) return 0;
        memcpy(head, xml, clen); head[clen] = 0;
        pod->title = GetXmlTextW(head, "title", 0);
        if (!pod->title) pod->title = GetXmlTextW(head, "itunes:name", 0);
        pod->desc = GetXmlTextW(head, "description", 1);
        if (!pod->desc) pod->desc = GetXmlTextW(head, "itunes:summary", 1);
        pod->author = GetXmlTextW(head, "itunes:author", 0);
        pod->lang = GetXmlTextW(head, "language", 0);
        pod->link = GetXmlTextW(head, "link", 0);
        pod->copyright = GetXmlTextW(head, "copyright", 0);

        /* 封面图：itunes:image 属性 或 image/url 子节点 */
        {
            const char *im = strstr(head, "<itunes:image");
            if (im) {
                const char *gt = strchr(im, '>');
                if (gt) pod->imageUrl = XmlAttrRange(im, gt + 1, "href");
            }
            if (!pod->imageUrl) {
                char *u = XmlText(head, "url");
                if (u) { pod->imageUrl = u; }
            }
            if (pod->imageUrl) CleanXmlText(pod->imageUrl);
        }
        free(head);
    }
    if (!pod->title) pod->title = _wcsdup(tr(TR_UNTITLED));
    if (!pod->desc)  pod->desc  = _wcsdup(L"");

    /* 遍历 <item> */
    while (item) {
        const char *itemEnd = strstr(item, "</item>");
        const char *enc, *encClose;
        char *seg;
        size_t segLen;
        Episode *e;
        char *encUrl = NULL;

        if (!itemEnd) break;
        segLen = (size_t)(itemEnd - item) + 7;
        seg = (char*)malloc(segLen + 1);
        if (!seg) break;
        memcpy(seg, item, segLen); seg[segLen] = 0;

        /* enclosure url */
        enc = strstr(seg, "<enclosure");
        if (enc) {
            encClose = strchr(enc, '>');
            if (encClose) encUrl = XmlAttrRange(enc, encClose + 1, "url");
        }
        if (!encUrl) {  /* 部分 Atom 风格 */
            enc = strstr(seg, "<media:content");
            if (enc) {
                encClose = strchr(enc, '>');
                if (encClose) encUrl = XmlAttrRange(enc, encClose + 1, "url");
            }
        }

        if (encUrl && pod->epCount < 10000) {
            char *d;
            if (pod->epCount >= pod->epCap) {
                /* realloc 必须用临时变量接收：失败会丢原指针，
                 * 且 eps 变 NULL 后 FreePodcast 按 epCount 访问会崩 */
                Episode *ne;
                pod->epCap = pod->epCap ? pod->epCap * 2 : 32;
                ne = (Episode*)realloc(pod->eps, pod->epCap * sizeof(Episode));
                if (!ne) { free(encUrl); free(seg); return 0; }
                pod->eps = ne;
            }
            e = &pod->eps[pod->epCount];
            memset(e, 0, sizeof(*e));
            e->origIdx = pod->epCount;
            e->title = GetXmlTextW(seg, "title", 0);
            if (!e->title) e->title = _wcsdup(tr(TR_UNTITLED));
            e->desc = GetXmlTextW(seg, "description", 1);
            if (!e->desc) e->desc = GetXmlTextW(seg, "itunes:summary", 1);
            if (!e->desc) e->desc = GetXmlTextW(seg, "content:encoded", 1);
            if (!e->desc) e->desc = _wcsdup(L"");
            e->author = GetXmlTextW(seg, "itunes:author", 0);
            CleanXmlText(encUrl); /* 先解码 XML 转义（如 &amp; -> &），再转宽字符 */
            e->url = U8ToW(encUrl);
            if (!e->url) {
                /* 槽位复用前释放已 dup 的字段，防止泄漏 */
                free(e->title); free(e->desc); free(e->author);
                free(encUrl); free(seg);
                item = FindOpenTag(itemEnd, "item");
                continue;
            }
            d = XmlText(seg, "itunes:duration");
            if (!d) d = XmlText(seg, "duration");
            if (d) { e->durationSec = ParseDuration(d); free(d); }
            /* 发布日期：RSS pubDate，Atom published/updated 兜底 */
            d = XmlText(seg, "pubDate");
            if (!d) d = XmlText(seg, "published");
            if (!d) d = XmlText(seg, "updated");
            if (!d) d = XmlText(seg, "date");
            if (d) {
                CleanXmlText(d);
                e->dateKey = ParseDate(d, e->dateText, 24);
                free(d);
            }
            pod->epCount++;
            free(encUrl);
        }
        free(seg);
        item = FindOpenTag(itemEnd, "item");
    }
    return pod->epCount > 0;
}

/* ================= 缓存 ================= */
/* 返回当前缓存目录（末尾带反斜杠）。
 * 默认 %TEMP%\LiteTune\（系统可自动清理，删除程序不留垃圾）；
 * 用户手动选择过则存在 ini 里的 g_cacheDir；GetTempPath 失败才退回 exe\cache */
static void CacheDir(wchar_t *out, int outMax)
{
    if (g_cacheDir[0]) {
        wcsncpy(out, g_cacheDir, outMax - 1);
        out[outMax - 1] = 0;
    } else {
        /* 默认放系统临时文件夹，删除程序后缓存由 Windows 自动清理 */
        wchar_t tmp[MAX_PATH];
        if (GetTempPathW(MAX_PATH, tmp) > 0) {
            swprintf(out, outMax, L"%lsLiteTune", tmp);
        } else {
            ExeDir(out, outMax);
            wcscat_s(out, outMax, L"cache");
        }
    }
    int len = (int)wcslen(out);
    if (len > 0 && out[len - 1] != L'\\' && len < outMax - 1) {
        out[len] = L'\\'; out[len + 1] = 0;
    }
}

static void CachePathFor(const wchar_t *url, wchar_t *out, int outMax)
{
    unsigned long h = 5381;
    const wchar_t *p = url;
    wchar_t dir[MAX_PATH];
    while (*p) h = ((h << 5) + h) ^ (unsigned long)*p++;
    CacheDir(dir, MAX_PATH);
    swprintf(out, outMax, L"%ls%08lx.mp3", dir, h);
}

static void EnsureCacheDir(void)
{
    wchar_t dir[MAX_PATH];
    CacheDir(dir, MAX_PATH);
    dir[wcslen(dir) - 1] = 0;   /* 去掉末尾反斜杠 */
    CreateDirectoryW(dir, NULL);
}

/* 启动时清理 g_cacheDays 天前的旧缓存（0=关）。
 * 只删符合程序命名规则（8 位十六进制 + .mp3）的文件，
 * 因此自定义目录里有别的 mp3 也不会被误删 */
static void CacheAutoClean(void)
{
    wchar_t cdir[MAX_PATH], pat[MAX_PATH];
    WIN32_FIND_DATAW fd;
    HANDLE h;
    FILETIME ftNow;
    ULARGE_INTEGER ullNow, ullFile;
    if (g_cacheDays <= 0) return;
    CacheDir(cdir, MAX_PATH);
    swprintf(pat, MAX_PATH, L"%ls????????.mp3", cdir);
    h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    GetSystemTimeAsFileTime(&ftNow);
    ullNow.LowPart = ftNow.dwLowDateTime;
    ullNow.HighPart = ftNow.dwHighDateTime;
    ullNow.QuadPart -= (ULONGLONG)g_cacheDays * 24 * 3600 * 10000000ULL;
    do {
        const wchar_t *n;
        int i, ok;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        /* 通配符 ? 匹配单字符，但还要确认确实是 8 个十六进制字符 */
        n = fd.cFileName;
        if (wcslen(n) != 12) continue;
        ok = 1;
        for (i = 0; i < 8; i++) {
            wchar_t c = n[i];
            if (!((c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f'))) { ok = 0; break; }
        }
        if (!ok) continue;
        ullFile.LowPart = fd.ftLastWriteTime.dwLowDateTime;
        ullFile.HighPart = fd.ftLastWriteTime.dwHighDateTime;
        if (ullFile.QuadPart < ullNow.QuadPart) {
            wchar_t fp[MAX_PATH];
            swprintf(fp, MAX_PATH, L"%ls%ls", cdir, n);
            DeleteFileW(fp);
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

/* 剧集是否已缓存到本地 */
static int EpisodeIsCached(Episode *e)
{
    wchar_t path[MAX_PATH];
    if (!e || !e->url) return 0;
    CachePathFor(e->url, path, MAX_PATH);
    return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
}

/* ================= 缓存元数据（离线“已缓存”视图用） ================= */
/* 前置声明：离线视图要复用排序/列设置/缓存统计（定义在后面） */
static void SortPodcast(Podcast *p);
static void SetEpisodeColumns(int cached);
static int  CountMp3InDir(const wchar_t *dir);
static void FreePodcast(Podcast *p);
static void ProbeAudioMeta(const wchar_t *path, Episode *e);
static void FreeCachePodcast(void)
{
    FreePodcast(&g_cachePod);
    ZeroMemory(&g_cachePod, sizeof(g_cachePod));
}

/* 扫描缓存目录，用 mp3 重建虚拟播客 */
static void LoadCachePodcast(void)
{
    wchar_t dir[MAX_PATH], pat[MAX_PATH];
    WIN32_FIND_DATAW fd;
    HANDLE hFind;

    g_cachePod.title  = _wcsdup(tr(TR_CACHED));
    g_cachePod.author = _wcsdup(tr(TR_CACHE_SUBTITLE));

    CacheDir(dir, MAX_PATH);
    swprintf(pat, MAX_PATH, L"%ls*.mp3", dir);
    hFind = FindFirstFileW(pat, &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;

    do {
        Episode *e;
        wchar_t mp[MAX_PATH], base[MAX_PATH];
        int dot;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;

        if (g_cachePod.epCount >= g_cachePod.epCap) {
            int nc = g_cachePod.epCap ? g_cachePod.epCap * 2 : 16;
            Episode *ne = (Episode*)realloc(g_cachePod.eps, nc * sizeof(Episode));
            if (!ne) break;
            g_cachePod.eps = ne;
            g_cachePod.epCap = nc;
        }
        e = &g_cachePod.eps[g_cachePod.epCount];
        ZeroMemory(e, sizeof(Episode));
        e->localFile = 1;
        e->origIdx = g_cachePod.epCount;

        swprintf(mp, MAX_PATH, L"%ls%ls", dir, fd.cFileName);
        e->url = _wcsdup(mp);

        /* 默认标题：去掉扩展名的文件名（哈希名），再从 mp3 元数据覆盖 */
        wcsncpy(base, fd.cFileName, MAX_PATH - 1);
        base[MAX_PATH - 1] = 0;
        dot = (int)wcslen(base);
        if (dot >= 4) base[dot - 4] = 0;
        e->title  = _wcsdup(base);
        e->author = _wcsdup(tr(TR_UNKNOWN_POD));

        /* 直接从 mp3 的 ID3 标签补全标题/播客名/时长（无标签则保留默认值） */
        ProbeAudioMeta(mp, e);
        g_cachePod.epCount++;
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);

    SortPodcast(&g_cachePod);
}

/* 缓存目录里的 mp3 数量（供列表行右侧显示，避免每次绘制都扫盘） */
static int g_cacheFileCount = 0;

/* 同步播客列表最后的“已缓存”虚拟行：有缓存才显示 */
static void RefreshCachedEntry(void)
{
    int need, total, has;
    wchar_t row[64];
    if (!g_listPod) return;
    {
        wchar_t cdir[MAX_PATH];
        CacheDir(cdir, MAX_PATH);
        g_cacheFileCount = CountMp3InDir(cdir);
    }
    need = g_cacheFileCount > 0;
    total = (int)SendMessageW(g_listPod, LB_GETCOUNT, 0, 0);
    has = total > g_podCount;
    if (need && !has) {
        int sel = (int)SendMessageW(g_listPod, LB_GETCURSEL, 0, 0);
        swprintf(row, 64, tr(TR_CACHED_FMT), g_cacheFileCount);
        SendMessageW(g_listPod, LB_ADDSTRING, 0, (LPARAM)row);
        g_cacheRowHere = 1;
        if (sel >= 0) SendMessageW(g_listPod, LB_SETCURSEL, sel, 0);
    } else if (!need && has) {
        SendMessageW(g_listPod, LB_DELETESTRING, g_podCount, 0);
        g_cacheRowHere = 0;
        if (g_viewCached) {
            /* 缓存被清空/目录变空：退回普通视图 */
            g_viewCached = 0;
            FreeCachePodcast();
            SetEpisodeColumns(0);
            SendMessageW(g_listEp, LVM_DELETEALLITEMS, 0, 0);
            SetWindowTextW(g_editDesc, L"");
        }
    } else if (need && has) {
        /* 数量可能变化（新增下载/移动目录），更新行文本；保留原选中 */
        int sel = (int)SendMessageW(g_listPod, LB_GETCURSEL, 0, 0);
        swprintf(row, 64, tr(TR_CACHED_FMT), g_cacheFileCount);
        SendMessageW(g_listPod, LB_DELETESTRING, g_podCount, 0);
        SendMessageW(g_listPod, LB_INSERTSTRING, g_podCount, (LPARAM)row);
        if (sel >= 0) SendMessageW(g_listPod, LB_SETCURSEL, sel, 0);
    }
}

/* ================= 配置（feeds.ini：订阅源 + 排序 + 布局） ================= */
static void AddFeedUrl(const char *s)
{
    char **p = (char**)realloc(g_feedUrls, (g_feedUrlCount + 1) * sizeof(char*));
    if (!p) return;
    g_feedUrls = p;
    g_feedUrls[g_feedUrlCount] = _strdup(s);
    if (g_feedUrls[g_feedUrlCount]) g_feedUrlCount++;
}

/* ---------- 每播客续播标记（上次听到哪） ---------- */

/* feed URL 的 FNV-1a 32 位哈希（feedUrl 为 UTF-8 char*，按字节逐个算，跨启动稳定） */
static unsigned int LastEpHash(const char *url)
{
    unsigned int h = 2166136261u;
    if (!url) return 0;
    while (*url) {
        h ^= (unsigned int)(unsigned char)*url++;
        h *= 16777619u;
    }
    return h;
}

static LastEp* LastEpFind(unsigned int hash)
{
    int i;
    for (i = 0; i < g_lastEpCount; i++)
        if (g_lastEps[i].hash == hash) return &g_lastEps[i];
    return NULL;
}

/* 记录/更新某播客的上次播放位置（epUrl 为空串仅刷新秒数时不替换 URL） */
static void LastEpSet(unsigned int hash, const wchar_t *epUrl, long long sec)
{
    LastEp *le = LastEpFind(hash);
    if (!le) {
        if (g_lastEpCount >= 512) return;   /* 防异常膨胀，播客数远小于此 */
        if (g_lastEpCount >= g_lastEpCap) {
            int nc = g_lastEpCap ? g_lastEpCap * 2 : 16;
            LastEp *nb = (LastEp*)realloc(g_lastEps, nc * sizeof(LastEp));
            if (!nb) return;
            g_lastEps = nb; g_lastEpCap = nc;
        }
        le = &g_lastEps[g_lastEpCount++];
        le->hash = hash;
        le->epUrl = NULL;
    }
    if (!le->epUrl || (epUrl && wcscmp(le->epUrl, epUrl) != 0)) {
        free(le->epUrl);
        le->epUrl = epUrl ? _wcsdup(epUrl) : NULL;
        if (!le->epUrl) { le->sec = 0; return; }
    }
    le->sec = sec;
}

/* 删除某播客的记录（删订阅时调用），返回是否删除 */
static int LastEpDrop(unsigned int hash)
{
    int i;
    for (i = 0; i < g_lastEpCount; i++) {
        if (g_lastEps[i].hash == hash) {
            free(g_lastEps[i].epUrl);
            memmove(&g_lastEps[i], &g_lastEps[i + 1],
                    (g_lastEpCount - i - 1) * sizeof(LastEp));
            g_lastEpCount--;
            return 1;
        }
    }
    return 0;
}

/* 启动清理：哈希对不上任何现有订阅的记录删除（防 ini 手编/异常残留） */
static void LastEpPurgeOrphans(void)
{
    int i, keep = 0;
    for (i = 0; i < g_lastEpCount; i++) {
        unsigned int h = g_lastEps[i].hash;
        int j, found = 0;
        for (j = 0; j < g_podCount && !found; j++)
            if (g_pods[j].feedUrl && LastEpHash(g_pods[j].feedUrl) == h) found = 1;
        if (found) g_lastEps[keep++] = g_lastEps[i];
        else free(g_lastEps[i].epUrl);
    }
    g_lastEpCount = keep;
}

static void LastEpFreeAll(void)
{
    int i;
    for (i = 0; i < g_lastEpCount; i++) free(g_lastEps[i].epUrl);
    free(g_lastEps);
    g_lastEps = NULL; g_lastEpCount = 0; g_lastEpCap = 0;
}

/* ================= 订阅 XML / 封面缓存（cache\feed_*.xml、cover_*.img） ================= */

/* 文件名 = 前缀 + URL 哈希 + 扩展名，跨启动稳定 */
static void UrlCachePath(const char *url, const wchar_t *prefix,
                         const wchar_t *ext, wchar_t *out, int outMax)
{
    wchar_t dir[MAX_PATH];
    CacheDir(dir, MAX_PATH);
    swprintf(out, outMax, L"%ls%ls%08x.%ls", dir, prefix, LastEpHash(url), ext);
}

/* 读取缓存文件到 malloc 缓冲（附带结尾 0），失败返回 NULL */
static char* ReadUrlCache(const char *url, const wchar_t *prefix,
                          const wchar_t *ext, DWORD *outLen)
{
    wchar_t path[MAX_PATH];
    FILE *f;
    long sz;
    char *buf;

    UrlCachePath(url, prefix, ext, path, MAX_PATH);
    f = _wfopen(path, L"rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > 16 * 1024 * 1024) { fclose(f); return NULL; }
    buf = (char*)malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return NULL; }
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        free(buf); fclose(f); return NULL;
    }
    fclose(f);
    buf[sz] = 0;
    *outLen = (DWORD)sz;
    return buf;
}

static void WriteUrlCache(const char *url, const wchar_t *prefix,
                          const wchar_t *ext, const char *data, DWORD len)
{
    wchar_t path[MAX_PATH];
    FILE *f;
    if (!data || len == 0) return;
    UrlCachePath(url, prefix, ext, path, MAX_PATH);
    f = _wfopen(path, L"wb");
    if (!f) return;
    fwrite(data, 1, len, f);
    fclose(f);
}

static void ParseSortValue(const char *v)
{
    if (strncmp(v, "title", 5) == 0)         g_sortCol = SORT_TITLE;
    else if (strncmp(v, "date", 4) == 0)     g_sortCol = SORT_DATE;
    else if (strncmp(v, "duration", 8) == 0) g_sortCol = SORT_DUR;
    if (strstr(v, ":asc"))  g_sortDir = 1;
    if (strstr(v, ":desc")) g_sortDir = -1;
}

/* 配置文本格式（exe 尾部内嵌与旧 feeds.ini 完全同构，互为迁移）：
 *   [settings] 下 sort=title|date|duration:asc|desc, w1/w2=列宽千分比
 *   [feeds] 下一行一个 RSS 地址（裸 URL，直接粘贴）
 *   兼容旧版无 section 的 feeds.txt */

/* ---------- 配置内嵌 exe 尾部（PE overlay）：单文件便携 ----------
 * 配置跟着 exe 走：复制 exe 到 U 盘/别的电脑 = 连配置一起带走。
 * 运行中的 exe 本体不能改写，保存时走「整机换装」：
 *   新镜像（旧文件 + 新尾部）整体写 exe.new → 旧镜像改名挪进 %TEMP%
 *   （跨盘失败退回同目录隐藏 exe.tmp）→ exe.new 原子改名顶上。
 * 残留副本下次启动顺手删除；exe 所在目录正常情况下不留任何临时文件。 */

static const char CFG_TAIL_MAGIC[4] = { 'L','T','S','T' };

/* 读取自身 exe 尾部的配置文本. 成功返回 malloc 缓冲（NUL 结尾），无尾部返回 NULL */
static char* CfgTailRead(void)
{
    wchar_t exe[MAX_PATH];
    HANDLE h; LARGE_INTEGER sz, at; DWORD rd, len; unsigned char hdr[8]; char *buf = NULL;
    if (!GetModuleFileNameW(NULL, exe, MAX_PATH)) return NULL;
    h = CreateFileW(exe, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return NULL;
    if (GetFileSizeEx(h, &sz) && sz.QuadPart > 8) {
        at.QuadPart = sz.QuadPart - 8;
        if (SetFilePointerEx(h, at, NULL, FILE_BEGIN) && ReadFile(h, hdr, 8, &rd, NULL) && rd == 8
            && memcmp(hdr + 4, CFG_TAIL_MAGIC, 4) == 0) {
            memcpy(&len, hdr, 4);
            if (len > 0 && (LONGLONG)len <= sz.QuadPart - 8 && len <= (1u << 20)) {
                buf = (char*)malloc((size_t)len + 1);
                if (buf) {
                    at.QuadPart = sz.QuadPart - 8 - (LONGLONG)len;
                    if (!SetFilePointerEx(h, at, NULL, FILE_BEGIN)
                        || !ReadFile(h, buf, len, &rd, NULL) || rd != len) {
                        free(buf); buf = NULL;
                    } else buf[len] = 0;
                }
            }
        }
    }
    CloseHandle(h);
    return buf;
}

/* 整镜读入自身 exe；tailOff 返回旧内嵌尾部起点（无尾部为 -1）。
 * 成功返回 malloc 缓冲（调用方 free），失败 NULL */
static unsigned char* ExeReadImage(DWORD *lenOut, LONGLONG *tailOff)
{
    wchar_t exe[MAX_PATH];
    HANDLE h;
    LARGE_INTEGER sz;
    DWORD rd, exeLen;
    unsigned char *img;

    *lenOut = 0;
    *tailOff = -1;
    if (!GetModuleFileNameW(NULL, exe, MAX_PATH)) return NULL;
    h = CreateFileW(exe, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return NULL;
    if (!GetFileSizeEx(h, &sz) || sz.QuadPart <= 8 || sz.QuadPart > (1 << 26)) {
        CloseHandle(h);
        return NULL;
    }
    exeLen = (DWORD)sz.QuadPart;
    img = (unsigned char*)malloc(exeLen);
    if (!img || !ReadFile(h, img, exeLen, &rd, NULL) || rd != exeLen) {
        free(img);
        CloseHandle(h);
        return NULL;
    }
    CloseHandle(h);
    if (memcmp(img + exeLen - 4, CFG_TAIL_MAGIC, 4) == 0) {
        DWORD oldLen;
        memcpy(&oldLen, img + exeLen - 8, 4);
        if (oldLen > 0 && (LONGLONG)oldLen <= (LONGLONG)exeLen - 8)
            *tailOff = (LONGLONG)exeLen - 8 - oldLen;
    }
    *lenOut = exeLen;
    return img;
}

/* 把 img[0..len) 原子换装为当前 exe（见上方「整机换装」说明）。
 * 接管 img 所有权；成功返回 1 */
static int ExeSwapImage(unsigned char *img, DWORD len)
{
    wchar_t exe[MAX_PATH], tmp[MAX_PATH + 8], dump[MAX_PATH + 8], tdir[MAX_PATH];
    HANDLE h;
    DWORD wr;
    int ok = 0, dumped = 0;

    if (!img || !GetModuleFileNameW(NULL, exe, MAX_PATH)) { free(img); return 0; }
    lstrcpynW(tmp, exe, MAX_PATH); lstrcatW(tmp, L".new");
    lstrcpynW(dump, exe, MAX_PATH); lstrcatW(dump, L".tmp");

    DeleteFileW(tmp);
    h = CreateFileW(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (h != INVALID_HANDLE_VALUE) {
        ok = WriteFile(h, img, len, &wr, NULL) && wr == len;
        CloseHandle(h);
    }
    free(img);
    if (!ok) return 0;

    /* 旧镜像挪走：优先 %TEMP%（exe 目录不留痕迹），跨盘失败退回同目录 .tmp */
    if (GetTempPathW(MAX_PATH, tdir)) {
        lstrcatW(tdir, L"LiteTune_old.exe");
        if (MoveFileExW(exe, tdir, MOVEFILE_REPLACE_EXISTING)) {
            lstrcpynW(dump, tdir, MAX_PATH);
            dumped = 1;
        }
    }
    if (!dumped) {
        DeleteFileW(dump);
        if (!MoveFileExW(exe, dump, MOVEFILE_REPLACE_EXISTING)) return 0;
    }
    if (!MoveFileExW(tmp, exe, MOVEFILE_REPLACE_EXISTING)) {
        MoveFileExW(dump, exe, MOVEFILE_REPLACE_EXISTING);   /* 尽力回滚改名 */
        return 0;
    }
    if (!dumped) SetFileAttributesW(dump, FILE_ATTRIBUTE_HIDDEN);
    return 1;
}

/* 把配置文本写进自身 exe 尾部（有旧尾部则原地覆盖，没有则追加到文件末尾） */
static int CfgTailWrite(const char *data, DWORD len)
{
    unsigned char *img, *ni;
    DWORD exeLen, base, nl;
    LONGLONG tailOff;

    if (!data || len == 0) return 0;
    img = ExeReadImage(&exeLen, &tailOff);
    if (!img) return 0;
    /* 新镜像 = 截掉旧尾部 + 新配置 + 长度 + 魔数 */
    base = (tailOff >= 0) ? (DWORD)tailOff : exeLen;
    nl = base + len + 8;
    ni = (unsigned char*)malloc(nl);
    if (!ni) { free(img); return 0; }
    memcpy(ni, img, base);
    memcpy(ni + base, data, len);
    memcpy(ni + base + len, &len, 4);
    memcpy(ni + base + len + 4, CFG_TAIL_MAGIC, 4);
    free(img);
    return ExeSwapImage(ni, nl);
}

/* 写内嵌配置并读回校验；写失败或校验不符返回 0 */
static int CfgTailWriteVerified(const char *cfg, DWORD len)
{
    char *v;
    int ok = 0;
    if (!CfgTailWrite(cfg, len)) return 0;
    v = CfgTailRead();
    if (v) {
        ok = (len == strlen(v) && memcmp(v, cfg, len) == 0);
        free(v);
    }
    return ok;
}

/* 整段文本覆盖写入文件（失败返回 0）；配置/导出落盘统一走这里 */
static int WriteTextFile(const wchar_t *path, const char *data, DWORD len)
{
    FILE *f = _wfsopen(path, L"wb", _SH_DENYNO);
    int ok = 0;
    if (!f) return 0;
    if ((DWORD)fwrite(data, 1, len, f) == len) ok = 1;
    fclose(f);
    return ok;
}

/* 外挂配置文件的完整路径（exe 同目录 feeds.ini） */
static void CfgExternalPath(wchar_t *out, int outMax)
{
    ExeDir(out, outMax);
    wcscat_s(out, (size_t)outMax, L"feeds.ini");
}

/* 剥离自身 exe 尾部的内嵌配置（切换到外挂模式时调用），走与 CfgTailWrite 相同的
 * 整机换装流程。本就无尾部时视为成功；失败返回 0（不阻断，外挂 ini 仍生效） */
static int CfgStripTail(void)
{
    unsigned char *img;
    DWORD exeLen, base;
    LONGLONG tailOff;

    img = ExeReadImage(&exeLen, &tailOff);
    if (!img) return 0;
    base = (tailOff >= 0) ? (DWORD)tailOff : exeLen;
    if (base == exeLen) { free(img); return 1; }   /* 本就无尾部 */
    return ExeSwapImage(img, base);
}

/* ---------- 配置文本动态缓冲 ---------- */
typedef struct { char *p; size_t len, cap; } StrBuf;

/* 追加一段格式化文本（单行 < 1KB；内存不足时丢该行保主体） */
static void SbAddf(StrBuf *sb, const char *fmt, ...)
{
    char tmp[1024];
    va_list ap; int n;
    va_start(ap, fmt);
    n = _vsnprintf(tmp, sizeof(tmp) - 1, fmt, ap);
    va_end(ap);
    if (n < 0) { tmp[sizeof(tmp) - 1] = 0; n = (int)strlen(tmp); }   /* 截断保护 */
    if (n <= 0) return;
    if (sb->len + (size_t)n + 1 > sb->cap) {
        size_t nc = sb->cap ? sb->cap * 2 : 4096;
        char *np;
        while (nc < sb->len + (size_t)n + 1) nc *= 2;
        np = (char*)realloc(sb->p, nc);
        if (!np) return;
        sb->p = np; sb->cap = nc;
    }
    memcpy(sb->p + sb->len, tmp, (size_t)n);
    sb->len += (size_t)n;
    sb->p[sb->len] = 0;
}

/* 解析单行配置（就地修改缓冲）：key=value 或 [feeds] 段的裸 URL */
static void ParseConfigLine(char *line)
{
    char *s = line, *eq;
    size_t n;
    if ((unsigned char)s[0] == 0xEF && (unsigned char)s[1] == 0xBB && (unsigned char)s[2] == 0xBF)
        s += 3;
    while (*s == ' ' || *s == '\t') s++;
    n = strlen(s);
    while (n > 0 && (s[n-1] == '\n' || s[n-1] == '\r' || s[n-1] == ' ' || s[n-1] == '\t'))
        s[--n] = 0;
    if (*s == 0 || *s == '#' || *s == ';' || *s == '[') return;

    eq = strchr(s, '=');
    if (eq) {
        char *val = eq + 1;
        *eq = 0;
        while (eq > s && (eq[-1]==' '||eq[-1]=='\t')) { eq--; *eq = 0; }
        if (strcmp(s, "sort") == 0) ParseSortValue(val);
        else if (strcmp(s, "w1") == 0) g_w1perm = atoi(val);
        else if (strcmp(s, "w2") == 0) g_w2perm = atoi(val);
        else if (strcmp(s, "deschide") == 0) g_descHidden = (atoi(val) != 0);
        else if (strcmp(s, "vol") == 0) { g_volume = atoi(val); if (g_volume < 0) g_volume = 0; if (g_volume > 100) g_volume = 100; }
        else if (strcmp(s, "winw") == 0) g_winW = atoi(val);
        else if (strcmp(s, "winh") == 0) g_winH = atoi(val);
        else if (strcmp(s, "winx") == 0) g_winX = atoi(val);
        else if (strcmp(s, "winy") == 0) g_winY = atoi(val);
        else if (strcmp(s, "cw0") == 0) g_colW[0] = atoi(val);
        else if (strcmp(s, "cw1") == 0) g_colW[1] = atoi(val);
        else if (strcmp(s, "cw2") == 0) g_colW[2] = atoi(val);
        else if (strcmp(s, "font") == 0) g_fontLevel = atoi(val);
        else if (strcmp(s, "dark") == 0) g_darkMode = (atoi(val) != 0);
        else if (strcmp(s, "lang") == 0) g_lang = (strcmp(val, "en") == 0) ? 1 : 0;
        else if (strcmp(s, "speed") == 0) { g_speed = atoi(val); if (g_speed < 50) g_speed = 100; if (g_speed > 300) g_speed = 100; }
        else if (strcmp(s, "resume") == 0) { g_resumeSec = atoll(val); if (g_resumeSec < 0) g_resumeSec = 0; }
        else if (strcmp(s, "resumeurl") == 0) { free(g_resumeUrl); g_resumeUrl = U8ToW(val); }
        else if (strcmp(s, "resumeplay") == 0) { g_resumePaused = (atoi(val) == 0); }
        else if (strcmp(s, "lastep") == 0) {
            /* 格式 lastep=<hash hex>,<sec>,<utf8url 余下整行> */
            unsigned int h; long long sec;
            char *c1 = strchr(val, ',');
            if (c1) {
                char *c2;
                *c1 = 0; c2 = strchr(c1 + 1, ',');
                if (c2) {
                    wchar_t *w;
                    *c2 = 0;
                    h = (unsigned int)strtoul(val, NULL, 16);
                    sec = atoll(c1 + 1);
                    if (h != 0 && sec >= 0 && (w = U8ToW(c2 + 1)) != NULL) {
                        LastEpSet(h, w, sec);
                        free(w);
                    }
                }
            }
        }
        else if (strcmp(s, "cachedir") == 0) {
            wchar_t *w = U8ToW(val);
            if (w) { wcsncpy(g_cacheDir, w, MAX_PATH - 1); g_cacheDir[MAX_PATH - 1] = 0; free(w); }
        }
        else if (strcmp(s, "cachedays") == 0) {
            g_cacheDays = atoi(val);
            if (g_cacheDays < 0) g_cacheDays = 0;
        }
    } else if (strstr(s, "://")) {
        AddFeedUrl(s);   /* [feeds] 段或旧 txt 的裸 URL */
    }
}

/* 解析内存中的整份配置文本（exe 尾部载荷），按 \n 拆行复用 ParseConfigLine */
static void ParseConfigMem(char *buf)
{
    char *p = buf;
    while (p && *p) {
        char *nl = strchr(p, '\n');
        if (nl) *nl = 0;
        ParseConfigLine(p);
        if (!nl) break;
        p = nl + 1;
    }
}

/* 检查配置文本开头是否为 "; LiteTune config" 标记（本程序导出/保存的 ini）。
 * 仅凭标记识别，不认识的文件一律不碰 */
static int MemHasCfgMarker(const char *buf)
{
    const char *s = buf;
    if ((unsigned char)s[0] == 0xEF && (unsigned char)s[1] == 0xBB
        && (unsigned char)s[2] == 0xBF) s += 3;   /* UTF-8 BOM */
    return strncmp(s, "; LiteTune config", 17) == 0;
}

/* ---- 首启弹窗：选语言 + 选配置存储方式 ----
 * 主窗口尚未创建、g_lang 仍是默认值时弹出。顶部 中文/English 按钮点选即切换
 * 本弹窗文案（并把 g_lang 定为该语言，稍后建窗即用此语言）；底部两个按钮
 * 选定存储方式。关闭对话框按「内嵌」处理。 */
#define ID_FR_ZH   1
#define ID_FR_EN   2
#define ID_FR_BODY 3
#define ID_FR_EMB  4
#define ID_FR_EXT  5

static int g_frResult = 0;   /* 1=外挂 feeds.ini 0=内嵌 exe 尾部 -1=用户关窗（退出程序） */
static int g_skipStartup = 0;   /* 首启弹窗被关闭：放弃启动，wWinMain 直接退出 */

/* 按当前 g_lang 刷新本弹窗内的文案 */
static void FrRelabel(HWND h)
{
    SetWindowTextW(GetDlgItem(h, ID_FR_BODY), tr(TR_CFGMODE_BODY));
    SetWindowTextW(GetDlgItem(h, ID_FR_EMB), tr(TR_M_CFG_EMB));
    SetWindowTextW(GetDlgItem(h, ID_FR_EXT), tr(TR_M_CFG_EXT));
}

static LRESULT CALLBACK FirstRunProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    LRESULT r;
    if (DarkDlgMsg(h, msg, wp, lp, &r)) return r;

    switch (msg) {
    case WM_CREATE: {
        UINT bs = WS_CHILD | WS_VISIBLE | WS_TABSTOP;
        HFONT f = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        HWND c;
        if (g_darkMode) bs |= BS_OWNERDRAW;   /* 暗色按钮自绘 */
        c = CreateWindowExW(0, L"BUTTON", tr(TR_LANG_ZH), bs | BS_PUSHBUTTON,
            12, 12, 84, 28, h, (HMENU)(INT_PTR)ID_FR_ZH, NULL, NULL);
        SendMessageW(c, WM_SETFONT, (WPARAM)f, TRUE);
        c = CreateWindowExW(0, L"BUTTON", tr(TR_LANG_EN), bs | BS_PUSHBUTTON,
            102, 12, 96, 28, h, (HMENU)(INT_PTR)ID_FR_EN, NULL, NULL);
        SendMessageW(c, WM_SETFONT, (WPARAM)f, TRUE);
        c = CreateWindowExW(0, L"STATIC", tr(TR_CFGMODE_BODY),
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            12, 52, 436, 128, h, (HMENU)(INT_PTR)ID_FR_BODY, NULL, NULL);
        SendMessageW(c, WM_SETFONT, (WPARAM)f, TRUE);
        c = CreateWindowExW(0, L"BUTTON", tr(TR_M_CFG_EMB), bs | BS_DEFPUSHBUTTON,
            42, 190, 180, 30, h, (HMENU)(INT_PTR)ID_FR_EMB, NULL, NULL);
        SendMessageW(c, WM_SETFONT, (WPARAM)f, TRUE);
        c = CreateWindowExW(0, L"BUTTON", tr(TR_M_CFG_EXT), bs | BS_PUSHBUTTON,
            238, 190, 180, 30, h, (HMENU)(INT_PTR)ID_FR_EXT, NULL, NULL);
        SendMessageW(c, WM_SETFONT, (WPARAM)f, TRUE);
        return 0;
    }
    case WM_COMMAND:
        if (HIWORD(wp) != BN_CLICKED) return 0;
        if (LOWORD(wp) == ID_FR_ZH || LOWORD(wp) == ID_FR_EN) {
            int nl = (LOWORD(wp) == ID_FR_EN) ? 1 : 0;
            if (nl != g_lang) { g_lang = nl; FrRelabel(h); }
            return 0;
        }
        if (LOWORD(wp) == ID_FR_EMB || LOWORD(wp) == ID_FR_EXT) {
            g_frResult = (LOWORD(wp) == ID_FR_EXT) ? 1 : 0;
            DestroyWindow(h);
            return 0;
        }
        return 0;
    case WM_CLOSE:
        g_frResult = -1;   /* 关闭 = 什么都不做，退出程序 */
        DestroyWindow(h);
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

/* 模态弹出首启弹窗，返回 1=外挂 0=内嵌 -1=用户关闭弹窗（应退出程序） */
static int AskFirstRunMode(void)
{
    static const wchar_t *FR_CLASS = L"LpFirstRun";
    WNDCLASSW wc;
    HWND d;
    RECT scr, rc;
    MSG m;

    if (!GetClassInfoW(GetModuleHandleW(NULL), FR_CLASS, &wc)) {
        ZeroMemory(&wc, sizeof(wc));
        wc.lpfnWndProc = FirstRunProc;
        wc.hInstance = GetModuleHandleW(NULL);
        wc.lpszClassName = FR_CLASS;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        RegisterClassW(&wc);
    }

    g_frResult = 0;
    {
        RECT wr = { 0, 0, 460, 232 };
        AdjustWindowRectEx(&wr, WS_POPUP | WS_CAPTION | WS_SYSMENU,
                           FALSE, WS_EX_DLGMODALFRAME);
        d = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_TOPMOST, FR_CLASS,
            L"LiteTune", WS_POPUP | WS_CAPTION | WS_SYSMENU,
            0, 0, wr.right - wr.left, wr.bottom - wr.top,
            NULL, NULL, GetModuleHandleW(NULL), NULL);
    }
    if (!d) return 0;

    /* 此刻主窗口还没建：居中到屏幕工作区 */
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &scr, 0);
    GetWindowRect(d, &rc);
    SetWindowPos(d, NULL,
        scr.left + ((scr.right - scr.left) - (rc.right - rc.left)) / 2,
        scr.top  + ((scr.bottom - scr.top)  - (rc.bottom - rc.top)) / 2,
        0, 0, SWP_NOSIZE | SWP_NOZORDER);

    ShowWindow(d, SW_SHOW);
    UpdateWindow(d);
    while (IsWindow(d) && GetMessageW(&m, NULL, 0, 0) > 0) {
        if (!IsDialogMessageW(d, &m)) {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
    }
    return g_frResult;
}

/* 检查文件开头是否带 LiteTune 配置标记（只读前 64 字节） */
static int FileHasCfgMarker(const wchar_t *path)
{
    FILE *f = _wfsopen(path, L"rb", _SH_DENYNO);
    char buf[64];
    if (!f) return 0;
    {
        size_t n = fread(buf, 1, sizeof(buf) - 1, f);
        buf[n] = 0;
    }
    fclose(f);
    return MemHasCfgMarker(buf);
}

/* 只在程序启动时调用一次（读取订阅与全部界面设置）；
 * 运行期间的增删/排序/刷新都在内存里进行，退出时由 SaveConfig 统一写回。
 * 配置来源由磁盘现状决定：同目录 feeds.ini（外挂）> exe 尾部内嵌；两者皆无
 * 则首启询问用户选哪种，此后一律以文件现状为准（模式不写进配置本身） */
static void LoadConfig(void)
{
    wchar_t iniPath[MAX_PATH];
    FILE *f;
    char line[MAX_FEED_LINE];
    int i;
    char *tailCfg;

    for (i = 0; i < g_feedUrlCount; i++) free(g_feedUrls[i]);
    free(g_feedUrls);
    g_feedUrls = NULL; g_feedUrlCount = 0;
    g_sortCol = SORT_DATE; g_sortDir = -1;
    g_w1perm = 323; g_w2perm = 383;
    g_volume = 80; g_winW = 480; g_winH = 360;
    g_colW[0] = 110; g_colW[1] = 88; g_colW[2] = 44;
    g_fontLevel = 0;
    g_darkMode = 1;
    g_lang = 1;
    g_speed = 100;
    g_cfgExternal = 0;
    free(g_resumeUrl); g_resumeUrl = NULL;
    g_resumeSec = 0;
    g_resumePaused = 0;
    LastEpFreeAll();

    /* 上次保存残留的改名副本（此时已无占用），顺手删除 */
    {
        wchar_t exe[MAX_PATH], tmp[MAX_PATH + 8], tdir[MAX_PATH];
        if (GetModuleFileNameW(NULL, exe, MAX_PATH)) {
            lstrcpynW(tmp, exe, MAX_PATH); lstrcatW(tmp, L".tmp");
            DeleteFileW(tmp);
            lstrcpynW(tmp, exe, MAX_PATH); lstrcatW(tmp, L".new");
            DeleteFileW(tmp);
        }
        if (GetTempPathW(MAX_PATH, tdir)) {
            lstrcatW(tdir, L"LiteTune_old.exe");
            DeleteFileW(tdir);
        }
    }

    /* 1) 同目录 feeds.ini 存在 = 外挂模式（内嵌尾部一律作废） */
    CfgExternalPath(iniPath, MAX_PATH);
    if (FileHasCfgMarker(iniPath)) {
        g_cfgExternal = 1;
        f = _wfsopen(iniPath, L"rb", _SH_DENYNO);
        if (f) {
            while (fgets(line, sizeof(line), f)) ParseConfigLine(line);
            fclose(f);
        }
    } else if ((tailCfg = CfgTailRead()) != NULL) {
        /* 2) 无 ini，有内嵌尾部 = 内嵌模式 */
        g_cfgExternal = 0;
        ParseConfigMem(tailCfg);
        free(tailCfg);
    } else {
        /* 3) 首次运行（既无 ini 也无内嵌配置）：弹首启弹窗，选语言 + 选存储方式。
         *    用户关窗 = 放弃启动 */
        g_cfgExternal = AskFirstRunMode();
        if (g_cfgExternal < 0) { g_skipStartup = 1; return; }
    }

    if (g_w1perm < 100 || g_w1perm > 800) g_w1perm = 323;
    if (g_w2perm < 100 || g_w2perm > 800) g_w2perm = 383;
    if (g_winW < 480) g_winW = 480;
    if (g_winH < 360) g_winH = 360;
    if (g_winW > 4000) g_winW = 4000;
    if (g_winH > 2400) g_winH = 2400;
    {
        /* 多显示器安全：如果保存的窗口位置完全在主屏幕外，放弃用它 */
        int scrW = GetSystemMetrics(SM_CXSCREEN);
        int scrH = GetSystemMetrics(SM_CYSCREEN);
        if (g_winX != CW_USEDEFAULT && g_winY != CW_USEDEFAULT) {
            if (g_winX < -g_winW + 40 || g_winX > scrW - 40 ||
                g_winY < -g_winH + 40 || g_winY > scrH - 40) {
                g_winX = CW_USEDEFAULT; g_winY = CW_USEDEFAULT;
            }
        }
    }
    {
        static const int defCw[3] = { 110, 88, 44 };
        int ci;
        for (ci = 0; ci < 3; ci++)
            if (g_colW[ci] < 30 || g_colW[ci] > 2000) g_colW[ci] = defCw[ci];
    }
    if (g_fontLevel < 0 || g_fontLevel > 2) g_fontLevel = 0;
}

static const char *SortColName(void)
{
    return g_sortCol == SORT_TITLE ? "title" : g_sortCol == SORT_DATE ? "date" : "duration";
}

/* 组装当前配置文本（唯一生成处：exe 尾部写盘与「导出配置」共用）。
 * 返回 malloc 缓冲（调用方 free），失败返回 NULL，*lenOut 收字节数 */
static char *BuildConfigText(DWORD *lenOut)
{
    StrBuf sb;
    int i;

    sb.p = NULL; sb.len = 0; sb.cap = 0;
    SbAddf(&sb, "; LiteTune config\n");
    SbAddf(&sb, "; Add one RSS feed URL per line under [feeds]\n");
    SbAddf(&sb, "[settings]\n");
    SbAddf(&sb, "sort=%s:%s\n", SortColName(), g_sortDir > 0 ? "asc" : "desc");
    SbAddf(&sb, "w1=%d\n", g_w1perm);
    SbAddf(&sb, "w2=%d\n", g_w2perm);
    if (g_descHidden) SbAddf(&sb, "deschide=1\n");
    SbAddf(&sb, "cw0=%d\n", g_colW[0]);
    SbAddf(&sb, "cw1=%d\n", g_colW[1]);
    SbAddf(&sb, "cw2=%d\n", g_colW[2]);
    SbAddf(&sb, "vol=%d\n", g_volume);
    SbAddf(&sb, "font=%d\n", g_fontLevel);
    SbAddf(&sb, "dark=%d\n", g_darkMode);
    SbAddf(&sb, "lang=%s\n", g_lang ? "en" : "zh");
    SbAddf(&sb, "speed=%d\n", g_speed);
    SbAddf(&sb, "winw=%d\n", g_winW);
    SbAddf(&sb, "winh=%d\n", g_winH);
    SbAddf(&sb, "winx=%d\n", g_winX);
    SbAddf(&sb, "winy=%d\n", g_winY);
    /* 续播记忆：正在播放/暂停时记住剧集与位置；已手动停止则不写（下次不续播） */
    if (g_playUrl && g_playState != 0) {
        long long pos = PlayerGet100ns(0);
        char *u8;
        if (pos < 0) pos = 0;
        SbAddf(&sb, "resume=%d\n", (int)(pos / 10000000LL));
        SbAddf(&sb, "resumeplay=%d\n", g_playState == 1 ? 1 : 0);
        u8 = WToU8(g_playUrl);
        if (u8) { SbAddf(&sb, "resumeurl=%s\n", u8); free(u8); }
        /* 退出时刻把当前剧集精确写入每播客标记（订阅视图才记） */
        if (g_playPod >= 0 && g_playPod < g_podCount &&
            g_pods[g_playPod].feedUrl)
            LastEpSet(LastEpHash(g_pods[g_playPod].feedUrl), g_playUrl,
                      pos / 10000000LL);
    }
    /* 每播客“上次听到哪”：行数=订阅数，体积极小 */
    {
        int li;
        for (li = 0; li < g_lastEpCount; li++) {
            if (g_lastEps[li].epUrl && g_lastEps[li].hash != 0) {
                char *u8 = WToU8(g_lastEps[li].epUrl);
                if (u8) {
                    SbAddf(&sb, "lastep=%x,%d,%s\n",
                           g_lastEps[li].hash, (int)g_lastEps[li].sec, u8);
                    free(u8);
                }
            }
        }
    }
    if (g_cacheDir[0]) {
        char *u8 = WToU8(g_cacheDir);
        if (u8) { SbAddf(&sb, "cachedir=%s\n", u8); free(u8); }
    }
    if (g_cacheDays != 30)   /* 默认 30 天不写入，保持配置干净 */
        SbAddf(&sb, "cachedays=%d\n", g_cacheDays);
    SbAddf(&sb, "\n[feeds]\n");
    for (i = 0; i < g_feedUrlCount; i++)
        SbAddf(&sb, "%s\n", g_feedUrls[i]);

    *lenOut = (DWORD)sb.len;
    return sb.p;
}

/* 仅在退出时调用一次：按当前模式分流——外挂直接覆盖写同目录 feeds.ini；
 * 内嵌写进自身 exe 尾部（写后读回校验），失败才回退覆盖写 feeds.ini */
static void SaveConfig(void)
{
    wchar_t iniPath[MAX_PATH];
    DWORD clen = 0;
    char *cfg;

    if (g_skipSave) return;   /* 导入配置后退出：不能让旧内存状态覆盖刚导入的配置 */

    CfgExternalPath(iniPath, MAX_PATH);

    /* 退出前抓取当前窗口外框矩形（最大化/最小化时跳过，保留上次的尺寸位置） */
    if (g_hwnd && IsWindow(g_hwnd)) {
        RECT wr;
        int st = IsIconic(g_hwnd) ? SW_MINIMIZE : IsZoomed(g_hwnd) ? SW_MAXIMIZE : SW_RESTORE;
        if (st == SW_RESTORE && GetWindowRect(g_hwnd, &wr)) {
            g_winX = wr.left;  g_winY = wr.top;
            g_winW = wr.right - wr.left;  g_winH = wr.bottom - wr.top;
        }
    }

    /* 退出前回收剧集三列的实际宽度 */
    if (g_listEp) {
        int ci;
        for (ci = 0; ci < 3; ci++) {
            LVCOLUMNW col;
            ZeroMemory(&col, sizeof(col));
            col.mask = LVCF_WIDTH;
            if (SendMessageW(g_listEp, LVM_GETCOLUMNW, ci, (LPARAM)&col))
                g_colW[ci] = col.cx;
        }
    }

    cfg = BuildConfigText(&clen);
    if (!cfg) return;

    if (g_cfgExternal) {
        WriteTextFile(iniPath, cfg, clen);   /* 外挂模式：直接写 feeds.ini */
        free(cfg);
        return;
    }

    /* 内嵌模式：写 exe 尾部，写后读回校验 */
    if (CfgTailWriteVerified(cfg, clen)) {
        free(cfg);
        return;
    }
    /* 回退：尾部写失败或校验不符（只读介质/异常），覆盖写 feeds.ini */
    WriteTextFile(iniPath, cfg, clen);
    free(cfg);
}

/* ================= 排序 ================= */
static int EpCmp(const void *pa, const void *pb)
{
    const Episode *a = (const Episode*)pa, *b = (const Episode*)pb;
    int r = 0;
    switch (g_sortCol) {
    case SORT_TITLE:
        r = lstrcmpiW(a->title ? a->title : L"", b->title ? b->title : L"");
        break;
    case SORT_DATE:
        /* dateKey 是 long long，直接相减截断成 int 会溢出翻转 */
        r = (a->dateKey > b->dateKey) - (a->dateKey < b->dateKey);
        break;
    case SORT_PODCAST:
        r = lstrcmpiW(a->author ? a->author : L"", b->author ? b->author : L"");
        break;
    default:         r = a->durationSec - b->durationSec; break;
    }
    if (r == 0) r = a->origIdx - b->origIdx;
    return r * g_sortDir;
}

static void SortPodcast(Podcast *p)
{
    qsort(p->eps, p->epCount, sizeof(Episode), EpCmp);
}

/* 内存图像数据解码为 HBITMAP（保留原始分辨率，列表/提示窗按需缩放） */
static HBITMAP DecodeImageMem(const char *data, DWORD len)
{
    HGLOBAL hMem;
    IStream *stm;
    GpBitmap *bmp = NULL;
    HBITMAP hbmp = NULL;

    hMem = GlobalAlloc(GMEM_MOVEABLE, len);
    if (!hMem) return NULL;
    memcpy(GlobalLock(hMem), data, len);
    GlobalUnlock(hMem);

    if (CreateStreamOnHGlobal(hMem, TRUE, &stm) != S_OK) { GlobalFree(hMem); return NULL; }
    if (GdipCreateBitmapFromStream(stm, &bmp) == 0 && bmp) {
        /* 直接转出原始尺寸的 HBITMAP，调用方按需缩放 */
        GdipCreateHBITMAPFromBitmap(bmp, &hbmp, 0);
        GdipDisposeImage((GpImage*)bmp);
    }
    stm->lpVtbl->Release(stm);
    return hbmp;
}

static void FreePodcast(Podcast *p);   /* 定义在 UI 段，加载线程失败时也要用 */

/* 加载封面图并解码为 HBITMAP；按封面 URL 缓存到 cache\cover_*.img，
 * 二次启动零网络直接从文件解码 */
static HBITMAP LoadCoverImage(const char *url)
{
    DWORD len = 0;
    char *data;
    HBITMAP hbmp = NULL;

    if (!url) return NULL;
    data = ReadUrlCache(url, L"cover_", L"img", &len);
    if (!data) {
        wchar_t *wurl = U8ToW(url);
        if (!wurl) return NULL;
        data = HttpFetch(wurl, &len, 0, NULL, NULL);
        free(wurl);
        if (data && len > 0) WriteUrlCache(url, L"cover_", L"img", data, len);
    }
    if (data && len > 0) hbmp = DecodeImageMem(data, len);
    free(data);
    return hbmp;
}

/* ================= 后台线程：加载 feeds ================= */
/* 构造一个"加载失败"占位条目：保留 feedUrl 供删除/重试用，标题由 UI 按语言绘制 */
static Podcast* MakeFailedPod(const char *url)
{
    Podcast *p = (Podcast*)calloc(1, sizeof(Podcast));
    if (!p) return NULL;
    p->loadFailed = 1;
    p->feedUrl = _strdup(url);
    return p;
}

/* 加载单条订阅：成功返回 Podcast*（已排序、含封面、feedUrl 已设）；
 * 失败时返回 loadFailed=1 的占位条目（让 URL 在列表中可见、可删除、可重试），
 * 仅在内存分配彻底失败时返回 NULL。
 * netFirst=0 启动模式：缓存命中就不联网，秒开且断网可用；
 * netFirst=1 刷新模式：强制联网取最新，成功后更新缓存，
 *            联网失败时回退缓存（断网刷新列表不清空） */
static Podcast* LoadOneFeed(const char *url, int netFirst)
{
    wchar_t *wurl;
    DWORD xmlLen = 0;
    char *xml;
    int fromNet = 0;
    Podcast *pod;

    xml = netFirst ? NULL : ReadUrlCache(url, L"feed_", L"xml", &xmlLen);
    if (!xml) {
        wurl = U8ToW(url);
        if (wurl) {
            xml = HttpFetch(wurl, &xmlLen, 0, NULL, NULL);
            free(wurl);
        }
        if (xml) {
            fromNet = 1;
        } else if (netFirst) {
            xml = ReadUrlCache(url, L"feed_", L"xml", &xmlLen);
            if (!xml) return MakeFailedPod(url);
        } else {
            return MakeFailedPod(url);
        }
    }

    pod = (Podcast*)calloc(1, sizeof(Podcast));
    if (pod && ParseFeed(xml, pod)) {
        if (fromNet) WriteUrlCache(url, L"feed_", L"xml", xml, xmlLen);
        SortPodcast(pod);
        pod->feedUrl = _strdup(url);
        if (pod->imageUrl) pod->hImage = LoadCoverImage(pod->imageUrl);
        free(xml);
        return pod;
    }
    if (pod) { FreePodcast(pod); free(pod); }
    free(xml);
    return MakeFailedPod(url);
}

/* 加载线程参数：订阅 URL 快照。线程不再读 g_feedUrls，
 * 避免 UI 线程增删订阅（realloc/free）与加载线程竞争 */
typedef struct { char **urls; int count; int netFirst; } FeedLoadArg;

static void FreeFeedLoadArg(FeedLoadArg *a)
{
    int i;
    if (!a) return;
    if (a->urls) {
        for (i = 0; i < a->count; i++) free(a->urls[i]);
        free(a->urls);
    }
    free(a);
}

static DWORD WINAPI FeedLoaderThread(LPVOID param)
{
    FeedLoadArg *a = (FeedLoadArg*)param;
    int i, loaded = 0;

    for (i = 0; i < a->count; i++) {
        Podcast *pod = LoadOneFeed(a->urls[i], a->netFirst);
        if (pod) {
            PostMessageW(g_hwnd, WM_APP_FEED_ADDED, 0, (LPARAM)pod);
            if (!pod->loadFailed) loaded++;
        }
        /* 每条完成（成败都算）上报进度，状态栏显示 已加载/总数 */
        PostMessageW(g_hwnd, WM_APP_FEED_PROGRESS, i + 1, a->count);
    }
    FreeFeedLoadArg(a);
    PostMessageW(g_hwnd, WM_APP_FEEDS_DONE, loaded, 0);
    return 0;
}

/* 单条订阅加载线程参数。replace=1 表示刷新：按 feedUrl 原位替换 */
typedef struct { char *url; int replace; } SingleFeedArg;

static DWORD WINAPI SingleFeedLoaderThread(LPVOID param)
{
    SingleFeedArg *a = (SingleFeedArg*)param;
    Podcast *pod = LoadOneFeed(a->url, 1);   /* 强制联网取最新 */
    int replace = a->replace;
    free(a->url);
    free(a);
    if (pod) {
        /* wParam=1 增量添加；wParam=2 原位替换（刷新） */
        PostMessageW(g_hwnd, WM_APP_FEED_ADDED, replace ? 2 : 1, (LPARAM)pod);
        PostMessageW(g_hwnd, WM_APP_FEEDS_DONE, 1, 0);
    } else {
        PostMessageW(g_hwnd, WM_APP_FEEDS_DONE, 0, 0);
    }
    return 0;
}

/* 添加单条订阅（不刷新全部）；replace=1 时刷新已有条目（原位替换） */
static void AddSingleFeedEx(const char *url, int replace)
{
    SingleFeedArg *a = (SingleFeedArg*)malloc(sizeof(SingleFeedArg));
    if (!a) return;
    a->url = _strdup(url);
    a->replace = replace;
    if (!a->url) { free(a); return; }
    SetStatus(tr(TR_LOADING_FEEDS));
    g_feedsLoading = 1;
    g_fullReload = 0;
    {
        HANDLE th = CreateThread(NULL, 0, SingleFeedLoaderThread, a, 0, NULL);
        if (!th) {
            g_feedsLoading = 0;
            free(a->url); free(a);
            SetStatus(tr(TR_READY));
            return;
        }
        CloseHandle(th);   /* 线程句柄无需保留：不等待不查询 */
    }
}

static void AddSingleFeed(const char *url)
{
    AddSingleFeedEx(url, 0);
}

/* ============ 下载目标互斥锁（单集播放下载 × 批量下载） ============ */
/* 两个下载通道互相独立，但可能指向同一个缓存文件；同一文件同一时刻只许一个
 * 线程写，后来者等待并在拿到锁后双检文件（对方可能已写完）。 */
static CRITICAL_SECTION g_dlCs;
#define DL_PATH_SLOTS 4   /* 实际并发最多 2（1 个播放下载 + 1 个批量线程） */
static wchar_t *g_dlPaths[DL_PATH_SLOTS];

/* 1=获得该路径的写权（用完必须 DlPathRelease）；0=已有线程在写（应等待） */
static int DlPathTryAcquire(const wchar_t *path)
{
    int i, busy = 0, got = 0;
    EnterCriticalSection(&g_dlCs);
    for (i = 0; i < DL_PATH_SLOTS; i++) {
        if (g_dlPaths[i] && wcscmp(g_dlPaths[i], path) == 0) { busy = 1; break; }
    }
    if (!busy) {
        for (i = 0; i < DL_PATH_SLOTS; i++) {
            if (!g_dlPaths[i]) {
                g_dlPaths[i] = _wcsdup(path);
                if (g_dlPaths[i]) got = 1;
                break;
            }
        }
    }
    LeaveCriticalSection(&g_dlCs);
    return got;
}

static void DlPathRelease(const wchar_t *path)
{
    int i;
    EnterCriticalSection(&g_dlCs);
    for (i = 0; i < DL_PATH_SLOTS; i++) {
        if (g_dlPaths[i] && wcscmp(g_dlPaths[i], path) == 0) {
            free(g_dlPaths[i]);
            g_dlPaths[i] = NULL;
            break;
        }
    }
    LeaveCriticalSection(&g_dlCs);
}

/* ================= 后台线程：下载音频 ================= */
typedef struct {
    int pod;
    wchar_t *url;
    long gen;
    void * volatile reqSlot;   /* HttpFetch 在此登记请求句柄，供取消 */
} DlJob;

static volatile LONG g_dlGen = 0;   /* 下载代号，每次新请求 +1 */
static DlJob *g_curDlJob = NULL;    /* 当前下载任务，仅 UI 线程访问 */

/* 仅断开当前下载连接（切歌用，代号由新请求统一作废旧任务） */
static void AbortDownloadConnection(void)
{
    if (g_curDlJob) {
        HINTERNET h = (HINTERNET)InterlockedExchangePointer(
            (PVOID volatile*)&g_curDlJob->reqSlot, NULL);
        if (h) WinHttpCloseHandle(h);
        g_curDlJob = NULL;
    }
    g_downloading = 0;
}

/* 用户点停止：断开下载并作废代号，过期完成消息将被丢弃 */
static void CancelCurrentDownload(void)
{
    AbortDownloadConnection();
    InterlockedIncrement(&g_dlGen);
}

static DWORD WINAPI EpisodeDlThread(LPVOID param)
{
    DlJob *job = (DlJob*)param;
    EpReady *r;
    wchar_t cache[MAX_PATH];
    DWORD len = 0;
    char *data;
    FILE *f;

    r = (EpReady*)calloc(1, sizeof(EpReady));
    if (!r) {
        /* 失败也要复位下载状态，否则 UI 永久停在“准备音频” */
        InterlockedIncrement(&g_dlGen);
        g_downloading = 0;
        free(job->url); free(job);
        return 0;
    }
    r->pod = job->pod;
    r->url = job->url;   /* 转移所有权给 r */
    r->gen = job->gen;

    if (r->url) {
        CachePathFor(r->url, cache, MAX_PATH);
        EnsureCacheDir();
        /* 一律先拿路径锁再判文件存在：批量线程先把整集下载到内存再
         * fopen+fwrite 落盘，fwrite 窗口内文件已存在但只是半成品，
         * 锁前判存在会播到截断文件；锁内双检则必然看到完整文件。
         * 等待期间被切歌/停止（gen 作废）就直接放弃，不做无意义下载。
         * gotLock 与 gen 分开判定，保证拿到的锁一定释放，
         * 即使 gen 在拿到锁的瞬间作废 */
        int gotLock = 0;
        while (!gotLock) {
            if (InterlockedCompareExchange(&g_dlGen, 0, 0) != job->gen) break;
            if (DlPathTryAcquire(cache)) { gotLock = 1; break; }
            Sleep(300);
        }
        if (gotLock) {
            if (InterlockedCompareExchange(&g_dlGen, 0, 0) == job->gen) {
                if (GetFileAttributesW(cache) != INVALID_FILE_ATTRIBUTES) {
                    r->ok = 1;  /* 已缓存（含批量线程刚写完的） */
                } else {
                    data = HttpFetch(r->url, &len, WM_APP_DL_PROGRESS,
                                     &job->reqSlot, &job->gen);
                    if (data && len > 0) {
                        f = _wfopen(cache, L"wb");
                        if (f) {
                            r->ok = (fwrite(data, 1, len, f) == len);
                            fclose(f);
                        }
                    }
                    free(data);
                }
            }
            DlPathRelease(cache);
        }
    }
    free(job);
    PostMessageW(g_hwnd, WM_APP_EP_READY, 0, (LPARAM)r);
    return 0;
}

/* ================= 批量缓存（Ctrl+多选后点缓存按钮） ================= */
/* 一个常驻工作线程 + 条件变量队列：队列空了线程休眠，UI 追加 URL 即唤醒；
 * 点停止则 cancel + 关闭当前 HTTP 请求，线程退出，UI 在 FINISH 里回收。
 * 自然下完时结构保留（线程休眠），可继续追加新批次。 */
typedef struct {
    CRITICAL_SECTION    cs;
    CONDITION_VARIABLE  cv;
    HANDLE              thread;      /* NULL=线程未启动/已回收 */
    wchar_t           **urls;        /* 动态数组，UI 线程 realloc 追加 */
    int                 cap, count, head;   /* head=已取出数，只增不减 */
    int                 doneCount;          /* 已下完数（≠head：取出后还在下载） */
    int                 okCount;
    int                 truncated;   /* 选中数超过 256 上限时置 1 */
    volatile LONG       cancel;      /* 1=停止：中断当前下载并丢弃剩余队列 */
    void * volatile     reqSlot;     /* 当前 HttpFetch 请求句柄，关闭即中止 */
} BatchCache;

static BatchCache *g_batch = NULL;   /* 生命周期由 UI 线程管理 */

static DWORD WINAPI BatchDlThread(LPVOID param)
{
    BatchCache *bc = (BatchCache*)param;
    for (;;) {
        wchar_t *url;
        wchar_t cache[MAX_PATH];
        DWORD len = 0;
        char *data = NULL;
        FILE *f;
        int ok = 0;

        EnterCriticalSection(&bc->cs);
        while (bc->head >= bc->count && !bc->cancel)
            SleepConditionVariableCS(&bc->cv, &bc->cs, INFINITE);
        if (bc->cancel) {
            LeaveCriticalSection(&bc->cs);
            break;
        }
        url = bc->urls[bc->head++];
        LeaveCriticalSection(&bc->cs);

        if (url && url[0]) {
            CachePathFor(url, cache, MAX_PATH);
            EnsureCacheDir();
            /* lock-first 与单集线程对称：对方的 fopen→fwrite→fclose 窗口内
             * 文件已存在但只是半成品，锁前判存在会误判 ok=1；锁内双检则安全。
             * gotLock 与 cancel 分开判定：拿到的锁必须释放，
             * 即使取消信号在拿锁成功的瞬间到达 */
            {
                int gotLock = 0;
                while (!gotLock) {
                    if (bc->cancel) break;
                    if (DlPathTryAcquire(cache)) { gotLock = 1; break; }
                    Sleep(300);
                }
                if (gotLock) {
                    if (!bc->cancel) {
                        if (GetFileAttributesW(cache) != INVALID_FILE_ATTRIBUTES) {
                            ok = 1;   /* 单集/本批次其它线程已写完 */
                        } else {
                            data = HttpFetch(url, &len, WM_APP_BATCH_PROGRESS,
                                             &bc->reqSlot, NULL);
                            if (data && len > 0) {
                                f = _wfopen(cache, L"wb");
                                if (f) {
                                    ok = (fwrite(data, 1, len, f) == len);
                                    fclose(f);
                                }
                            }
                            free(data);
                        }
                    }
                    DlPathRelease(cache);
                }
            }
        }
        EnterCriticalSection(&bc->cs);
        if (ok) bc->okCount++;
        bc->doneCount++;
        LeaveCriticalSection(&bc->cs);
        PostMessageW(g_hwnd, WM_APP_BATCH_DONE, (WPARAM)ok, 0);
    }
    /* 仅取消时到达；结构体由 UI 线程在 FINISH 中等待本线程后释放 */
    PostMessageW(g_hwnd, WM_APP_BATCH_FINISH, 0, 0);
    return 0;
}

/* 停止整个批量队列：立即中断当前 HTTP，剩余 URL 全部丢弃 */
static void CancelBatchCache(void)
{
    BatchCache *bc = g_batch;
    HINTERNET h;
    if (!bc || InterlockedCompareExchange(&bc->cancel, 0, 0)) return;
    InterlockedExchange(&bc->cancel, 1);
    h = (HINTERNET)InterlockedExchangePointer((PVOID volatile*)&bc->reqSlot, NULL);
    if (h) WinHttpCloseHandle(h);
    WakeAllConditionVariable(&bc->cv);
}

/* UI 线程回收：cancel 后工作线程很快退出，等它结束再释放全部资源。
 * FINISH 消息处理和程序退出（消息泵已停、FINISH 不会到达）共用。 */
static void BatchReleaseAfterFinish(void)
{
    BatchCache *bc = g_batch;
    int bi;
    if (!bc) return;
    if (bc->thread) {
        WaitForSingleObject(bc->thread, 10000);
        CloseHandle(bc->thread);
    }
    for (bi = 0; bi < bc->count; bi++) free(bc->urls[bi]);
    free(bc->urls);
    DeleteCriticalSection(&bc->cs);
    free(bc);
    g_batch = NULL;
}

/* 启动或追加批量缓存：收集当前选中剧集的 URL，去重去已缓存后入队 */
static void StartBatchCache(void)
{
    int sel, total = 0, i, truncated = 0, addN = 0;
    int indices[256];
    Podcast *p;
    BatchCache *bc;
    wchar_t st[128];
    int needStart;

    if (g_viewCached || g_curPod < 0 || g_curPod >= g_podCount) return;
    p = &g_pods[g_curPod];

    /* 收集所有选中行（最多 256，超出部分不收集并在完成时提示） */
    sel = -1;
    while ((sel = (int)SendMessageW(g_listEp, LVM_GETNEXTITEM,
                                     (WPARAM)sel, LVNI_SELECTED)) >= 0) {
        if (sel < p->epCount && p->eps[sel].url) {
            if (total < 256) indices[total++] = sel;
            else truncated = 1;
        }
        if (sel >= p->epCount - 1) break;
    }

    if (total == 0) {
        SetStatusTemp(tr(TR_BATCH_NOSEL));
        return;
    }

    bc = g_batch;
    if (bc && InterlockedCompareExchange(&bc->cancel, 0, 0)) {
        /* 停止信号已发、FINISH 尚未处理的毫秒级窗口：稍候再试 */
        SetStatusTemp(tr(TR_BATCH_STOPPING));
        return;
    }
    if (!bc) {
        /* 首次：创建队列结构（线程在入队完成后再启动） */
        bc = (BatchCache*)calloc(1, sizeof(BatchCache));
        if (!bc) return;
        InitializeCriticalSection(&bc->cs);
        InitializeConditionVariable(&bc->cv);
        bc->cap = total + 8;
        bc->urls = (wchar_t**)calloc((size_t)bc->cap, sizeof(wchar_t*));
        if (!bc->urls) {
            DeleteCriticalSection(&bc->cs);
            free(bc);
            return;
        }
        g_batch = bc;
    }

    /* 入队：跳过队列已有（去重）和本地已存在的缓存文件 */
    EnterCriticalSection(&bc->cs);
    /* 上一批已自然下完、线程睡在 CV 上：清空旧 URL、计数归零，
     * 使本批进度只统计新选剧集（旧文件仍在盘上，下面双检会跳过）。
     * 重置后条件 head>=count 仍成立，线程不会因伪唤醒提前行动 */
    if (bc->thread && bc->head >= bc->count) {
        for (int j = 0; j < bc->count; j++) free(bc->urls[j]);
        bc->count = bc->head = bc->okCount = bc->doneCount = 0;
        bc->truncated = 0;
    }
    for (i = 0; i < total; i++) {
        Episode *e = &p->eps[indices[i]];
        wchar_t cache[MAX_PATH];
        int j, dup = 0;
        if (!e->url) continue;
        for (j = 0; j < bc->count; j++)
            if (bc->urls[j] && wcscmp(bc->urls[j], e->url) == 0) { dup = 1; break; }
        if (dup) continue;
        CachePathFor(e->url, cache, MAX_PATH);
        if (e->localFile || EpisodeIsCached(e) ||
            GetFileAttributesW(cache) != INVALID_FILE_ATTRIBUTES)
            continue;
        if (bc->count >= bc->cap) {
            int ncap = bc->cap * 2;
            wchar_t **nb = (wchar_t**)realloc(bc->urls,
                                               (size_t)ncap * sizeof(wchar_t*));
            if (!nb) break;
            bc->urls = nb;
            bc->cap = ncap;
        }
        bc->urls[bc->count] = _wcsdup(e->url);
        if (!bc->urls[bc->count]) bc->urls[bc->count] = _wcsdup(L"");
        bc->count++;
        addN++;
    }
    if (truncated) bc->truncated = 1;
    LeaveCriticalSection(&bc->cs);

    if (addN == 0) {
        /* 空队列是刚为本次创建的：回收；否则只是没有新东西可加 */
        if (!bc->thread && bc->count == 0) {
            g_batch = NULL;
            free(bc->urls);
            DeleteCriticalSection(&bc->cs);
            free(bc);
        } else {
            SetStatusTemp(tr(TR_BATCH_RUNNING));
        }
        return;
    }

    /* 清除多选，恢复单选外观（下载过程列表只读） */
    {
        LVITEMW lvi;
        ZeroMemory(&lvi, sizeof(lvi));
        lvi.stateMask = LVIS_SELECTED;
        lvi.state = 0;
        SendMessageW(g_listEp, LVM_SETITEMSTATE, (WPARAM)-1, (LPARAM)&lvi);
    }

    /* 状态栏：首次/上批已自然结束时重新保存 base；批次进行中保持现有 base */
    if (!g_batchActive) {
        wcsncpy(g_batchSavedBase, g_statusBase, 511);
        g_batchSavedBase[511] = 0;
        g_batchActive = 1;
    }

    needStart = (bc->thread == NULL);
    if (needStart) {
        bc->thread = CreateThread(NULL, 0, BatchDlThread, bc, 0, NULL);
        if (!bc->thread) {
            int bi;
            for (bi = 0; bi < bc->count; bi++) free(bc->urls[bi]);
            free(bc->urls);
            DeleteCriticalSection(&bc->cs);
            free(bc);
            g_batch = NULL;
            g_batchActive = 0;
            SetStatusTemp(tr(TR_DL_THREAD_FAIL));
            return;
        }
        /* 入队先于建线程：线程进入时 count>head，不会空睡，故无需唤醒 */
    } else {
        /* 线程上批下完后睡在 CV 上：唤醒它处理新追加的 URL */
        WakeAllConditionVariable(&bc->cv);
    }

    {
        int head, cnt;
        EnterCriticalSection(&bc->cs);
        head = bc->head; cnt = bc->count;
        LeaveCriticalSection(&bc->cs);
        swprintf(st, 128, tr(TR_BATCH_PROGRESS_FMT), head, cnt);
        SetBatchProgress(st);
    }
    InvalidateRect(g_listEp, NULL, TRUE);   /* 新增队列剧集立即标蓝 */
}

/* 该集是否在批量下载队列中（尚未完成的保持蓝色；已完成的 cached=1 由黄色优先）。
 * 按 URL 匹配：批量期间列表可能被刷新/重排，URL 不受影响。遍历在队列 CS 内，
 * 与 UI 线程的 realloc 追加互斥。 */
static int EpisodeBatchQueued(int ep)
{
    int j, found = 0;
    Podcast *p;
    BatchCache *bc = g_batch;
    if (!bc || g_viewCached || g_curPod < 0 || g_curPod >= g_podCount)
        return 0;
    p = &g_pods[g_curPod];
    if (ep < 0 || ep >= p->epCount || !p->eps[ep].url) return 0;
    EnterCriticalSection(&bc->cs);
    for (j = 0; j < bc->count; j++) {
        if (bc->urls[j] && bc->urls[j][0] &&
            wcscmp(p->eps[ep].url, bc->urls[j]) == 0) {
            found = 1;
            break;
        }
    }
    LeaveCriticalSection(&bc->cs);
    return found;
}

/* ================= 音频信息探测 ================= */
static void ProbeAudio(const wchar_t *path, wchar_t *out, int outMax, long long *outDur100ns)
{
    IMFSourceReader *reader = NULL;
    IMFMediaType *mt = NULL;
    UINT32 rate = 0, ch = 0, avgBps = 0;
    GUID subtype;
    const wchar_t *fmtName = tr(TR_AUDIO);
    HRESULT hr;

    *outDur100ns = 0;
    wcscpy_s(out, outMax, L"");
    hr = MFCreateSourceReaderFromURL(path, NULL, &reader);
    if (FAILED(hr) || !reader) return;

    /* 时长（100ns）：直接从文件读，比 MFPlay 的 GetDuration 可靠 */
    {
        PROPVARIANT pv;
        memset(&pv, 0, sizeof(pv));
        if (SUCCEEDED(reader->lpVtbl->GetPresentationAttribute(reader,
                MF_SOURCE_READER_MEDIASOURCE, &MF_PD_DURATION, &pv))) {
            if (pv.vt == VT_UI8)      *outDur100ns = (long long)pv.uhVal.QuadPart;
            else if (pv.vt == VT_I8)  *outDur100ns = pv.hVal.QuadPart;
            else if (pv.vt == VT_UI4) *outDur100ns = pv.ulVal;
        }
    }

    hr = reader->lpVtbl->GetNativeMediaType(reader, MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, &mt);
    if (SUCCEEDED(hr) && mt) {
        mt->lpVtbl->GetUINT32(mt, &MF_MT_AUDIO_SAMPLES_PER_SECOND, &rate);
        mt->lpVtbl->GetUINT32(mt, &MF_MT_AUDIO_NUM_CHANNELS, &ch);
        mt->lpVtbl->GetUINT32(mt, &MF_MT_AUDIO_AVG_BYTES_PER_SECOND, &avgBps);
        if (SUCCEEDED(mt->lpVtbl->GetGUID(mt, &MF_MT_SUBTYPE, &subtype))) {
            if (IsEqualGUID(&subtype, &MFAudioFormat_MP3))       fmtName = L"MP3";
            else if (IsEqualGUID(&subtype, &MFAudioFormat_AAC))  fmtName = L"AAC";
            else if (IsEqualGUID(&subtype, &MFAudioFormat_WMAudioV8) ||
                     IsEqualGUID(&subtype, &MFAudioFormat_WMAudioV9)) fmtName = L"WMA";
            else if (IsEqualGUID(&subtype, &MFAudioFormat_PCM))  fmtName = L"PCM";
        }
        mt->lpVtbl->Release(mt);
    }
    reader->lpVtbl->Release(reader);

    if (rate > 0) {
        int kbps = avgBps > 0 ? (int)(avgBps * 8 / 1000) : 0;
        const wchar_t *chName = ch == 1 ? tr(TR_MONO) : (ch == 2 ? tr(TR_STEREO) : tr(TR_MULTICH));
        if (kbps > 0)
            swprintf(out, outMax, L"%ls|%dkbps|%uHz|%ls", fmtName, kbps, rate, chName);
        else
            swprintf(out, outMax, L"%ls|%uHz|%ls", fmtName, rate, chName);
    } else {
        wcscpy_s(out, outMax, fmtName);
    }
}

/* 读一个字符串型属性，返回新分配的宽字符串（调用方 free），无则 NULL */
static wchar_t* PropGetString(IPropertyStore *ps, const PROPERTYKEY *key)
{
    PROPVARIANT pv;
    wchar_t *s = NULL;
    memset(&pv, 0, sizeof(pv));
    if (SUCCEEDED(ps->lpVtbl->GetValue(ps, key, &pv)) &&
        pv.vt == VT_LPWSTR && pv.pwszVal)
        s = _wcsdup(pv.pwszVal);
    PropVariantClear(&pv);
    return s;
}

/* 从 mp3 的 ID3 标签补全离线剧集：标题、播客名（优先艺术家，其次专辑）、时长。
 * 直接替换 e 内已有默认值并接管内存，无该字段时保持默认。 */
static void ProbeAudioMeta(const wchar_t *path, Episode *e)
{
    IPropertyStore *ps = NULL;
    PROPVARIANT pv;
    wchar_t *s;
    if (FAILED(SHGetPropertyStoreFromParsingName(path, NULL, GPS_DEFAULT,
            &IID_IPropertyStore, (void**)&ps)) || !ps)
        return;

    s = PropGetString(ps, &PKEY_Title_L);
    if (s) { free(e->title); e->title = s; }

    s = PropGetString(ps, &PKEY_Music_Artist_L);
    if (!s) s = PropGetString(ps, &PKEY_Music_AlbumTitle_L);
    if (s) { free(e->author); e->author = s; }

    memset(&pv, 0, sizeof(pv));
    if (SUCCEEDED(ps->lpVtbl->GetValue(ps, &PKEY_Media_Duration_L, &pv))) {
        if (pv.vt == VT_UI8 && pv.uhVal.QuadPart > 0)
            e->durationSec = (int)(pv.uhVal.QuadPart / 10000000);
        PropVariantClear(&pv);
    }

    ps->lpVtbl->Release(ps);
}

static void LoadCachePodcast(void);   /* 前置声明：/eps?p=缓存行 懒加载时用 */

/* ================= MFPlay 回调 ================= */
typedef struct {
    IMFPMediaPlayerCallbackVtbl *lpVtbl;
    LONG ref;
} PlayerCallback;

static HRESULT STDMETHODCALLTYPE PC_QueryInterface(IMFPMediaPlayerCallback *This, REFIID riid, void **ppv)
{
    (void)riid;
    *ppv = This;
    return S_OK;
}
static ULONG STDMETHODCALLTYPE PC_AddRef(IMFPMediaPlayerCallback *This)
{
    return (ULONG)InterlockedIncrement(&((PlayerCallback*)This)->ref);
}
static ULONG STDMETHODCALLTYPE PC_Release(IMFPMediaPlayerCallback *This)
{
    return (ULONG)InterlockedDecrement(&((PlayerCallback*)This)->ref);
}
static void STDMETHODCALLTYPE PC_OnMediaPlayerEvent(IMFPMediaPlayerCallback *This, MFP_EVENT_HEADER *hdr)
{
    (void)This;
    PostMessageW(g_hwnd, WM_APP_MF_EVENT, (WPARAM)hdr->eEventType, (LPARAM)hdr->hrEvent);
}

static IMFPMediaPlayerCallbackVtbl g_pcVtbl = {
    PC_QueryInterface, PC_AddRef, PC_Release, PC_OnMediaPlayerEvent
};
static PlayerCallback g_pc = { &g_pcVtbl, 1 };

/* ================= 播放控制 ================= */
static void PlayerPlayFile(const wchar_t *path, const wchar_t *title)
{
    HRESULT hr;
    if (g_player) {
        g_player->lpVtbl->Release(g_player);
        g_player = NULL;
    }
    /* 记录当前播放的本地文件，供手机 HTTP 服务流式输出 */
    EnterCriticalSection(&g_nowCs);
    wcsncpy(g_nowPath, path ? path : L"", MAX_PATH - 1);
    g_nowPath[MAX_PATH - 1] = 0;
    wcsncpy(g_nowTitle, title ? title : L"", 255);
    g_nowTitle[255] = 0;
    LeaveCriticalSection(&g_nowCs);
    /* 传 URL + fStartPlayback=TRUE：创建即自动播放，无需 SetMediaItem/Play */
    hr = MFPCreateMediaPlayer(path, TRUE, MFP_OPTION_NONE,
            (IMFPMediaPlayerCallback*)&g_pc, g_hwnd, &g_player);
    if (FAILED(hr) || !g_player) { SetStatus(tr(TR_CANT_OPEN)); return; }
    g_player->lpVtbl->SetVolume(g_player,
        (float)SendMessageW(g_sldVol, TBM_GETPOS, 0, 0) / 100.0f);
    g_player->lpVtbl->SetMute(g_player, g_muted ? TRUE : FALSE);
    ApplySpeed();   /* 恢复上次的倍速 */
    UpdateWindowTitle();
}

static void PlayerStop(void)
{
    if (g_player) g_player->lpVtbl->Stop(g_player);
    g_playState = 0;
    g_dur100ns = 0;
    g_posSec = 0; g_durSec = 0;
    g_playPod = -1;         /* 清空“正在播放”标记，避免和后续列表选中混淆 */
    g_playEp = -1;
    EnterCriticalSection(&g_nowCs);
    g_nowPath[0] = 0;
    g_nowTitle[0] = 0;
    LeaveCriticalSection(&g_nowCs);
    if (g_btnPlay) InvalidateRect(g_btnPlay, NULL, TRUE);   /* 图标回到“播放” */
    SetWindowTextW(g_stTime, L"00:00 / 00:00");
    SendMessageW(g_sldSeek, TBM_SETPOS, TRUE, 0);
    if (g_stFormat) SetWindowTextW(g_stFormat, L"");
    if (g_listEp) InvalidateRect(g_listEp, NULL, TRUE);      /* 剧集颜色复位 */
    UpdateThumbbarPlayPause();
    UpdateWindowTitle();
}

/* 取当前位置或总时长（wantDur=1），单位 100ns；无播放器/失败返回 -1 */
static long long PlayerGet100ns(int wantDur)
{
    PROPVARIANT pv;
    HRESULT hr;
    if (!g_player) return -1;
    memset(&pv, 0, sizeof(pv));
    hr = wantDur
        ? g_player->lpVtbl->GetDuration(g_player, &MFP_POSITIONTYPE_100NS, &pv)
        : g_player->lpVtbl->GetPosition(g_player, &MFP_POSITIONTYPE_100NS, &pv);
    if (FAILED(hr) || pv.vt != VT_I8) return -1;
    return pv.hVal.QuadPart;
}

static void PlayerSeekToFrac(int permille)
{
    PROPVARIANT pv;
    if (!g_player || g_dur100ns <= 0) return;
    memset(&pv, 0, sizeof(pv));
    pv.vt = VT_I8;
    pv.hVal.QuadPart = g_dur100ns * permille / 1000;
    g_player->lpVtbl->SetPosition(g_player, &MFP_POSITIONTYPE_100NS, &pv);
}

/* 应用当前倍速到播放器；g_speed 单位为百分之一（100=1.0x） */
static void ApplySpeed(void)
{
    if (!g_player) return;
    g_player->lpVtbl->SetRate(g_player, (float)g_speed / 100.0f);
}

/* 倍速按钮显示文本，如 "1.0x" */
static void SpeedText(wchar_t *out, int outMax)
{
    int whole = g_speed / 100;
    int frac  = g_speed % 100;
    if (frac == 0)
        _snwprintf(out, outMax, L"%d.0x", whole);
    else if (frac % 10 == 0)
        _snwprintf(out, outMax, L"%d.%dx", whole, frac / 10);
    else
        _snwprintf(out, outMax, L"%d.%02dx", whole, frac);
}

/* 循环切换倍速：1.0 → 1.25 → 1.5 → 2.0 → 0.75 → 1.0 */
static void CycleSpeed(void)
{
    static const int table[] = { 100, 125, 150, 200, 75 };
    int i, n = (int)(sizeof(table) / sizeof(table[0]));
    for (i = 0; i < n; i++)
        if (table[i] == g_speed) break;
    g_speed = table[(i + 1) % n];
    ApplySpeed();
    if (g_btnSpeed) InvalidateRect(g_btnSpeed, NULL, TRUE);
    SetStatusTemp(tr(TR_SPEED_SET));
}

/* ================= 手机播放/遥控 HTTP 服务 ================= */
/* PC 作为微型 HTTP 服务器：手机浏览器打开首页后，
 * 页面内 <audio> 直接拉 /audio 流（手机出声，支持 Range 拖动），
 * 页面按钮调 /cmd 遥控电脑（PostMessage 回主窗口按钮命令）。 */

/* 发送全部数据（send 可能部分发送） */
static int SendAll(SOCKET s, const char *buf, int len)
{
    int sent = 0;
    while (sent < len) {
        int n = send(s, buf + sent, len - sent, 0);
        if (n <= 0) return 0;
        sent += n;
    }
    return 1;
}

static void HttpRespond(SOCKET s, const char *status, const char *ctype,
                        const char *body, int blen)
{
    char hdr[512];
    int n = _snprintf(hdr, sizeof(hdr),
        "HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %d\r\n"
        "Cache-Control: no-store\r\nConnection: close\r\n\r\n",
        status, ctype, blen);
    if (n > 0) SendAll(s, hdr, n);
    if (blen > 0 && body) SendAll(s, body, blen);
}

/* 宽字符 → UTF-8 并做 JSON 转义（" \ 控制字符） */
static void JsonEscapeU8(const wchar_t *w, char *out, int outMax)
{
    int o = 0;
    while (*w && o < outMax - 8) {
        unsigned c = (unsigned)*w;   /* 注意不能截成 unsigned char，否则中文变低字节乱码 */
        if (*w == L'"' || *w == L'\\') { out[o++] = '\\'; out[o++] = (char)*w; w++; continue; }
        if (c < 0x20) { out[o++] = ' '; w++; continue; }
        if (c < 0x80) { out[o++] = (char)c; w++; continue; }
        /* 非 ASCII：逐字符 WideCharToMultiByte 太慢，先整串转换更省事——
         * 但这里逐字符调用也无妨（标题最多 255 字符） */
        {
            char tmp[8];
            int n = WideCharToMultiByte(CP_UTF8, 0, w, 1, tmp, sizeof(tmp), NULL, NULL);
            int k;
            if (n <= 0) { w++; continue; }
            for (k = 0; k < n && o < outMax - 2; k++) out[o++] = tmp[k];
            w++;
        }
    }
    out[o] = 0;
}

/* 小缓冲拼接：pos 累计写入位置，超界不溢出 */
static void catf(char *buf, int cap, int *pos, const char *fmt, ...)
{
    va_list ap;
    int left, n;
    if (*pos >= cap - 1) return;
    left = cap - 1 - *pos;
    va_start(ap, fmt);
    n = vsnprintf(buf + *pos, left, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if (n > left) n = left;
    *pos += n;
}

/* 判断虚拟网卡（WSL/VirtualBox/VMware 等手机连不通的适配器）：
 * 按适配器 Description 里的关键词匹配 */
static int IsVirtualAdapter(const wchar_t *desc)
{
    static const wchar_t *kw[] = {
        L"virtual", L"vmware", L"hyper-v", L"loopback",
        L"tailscale", L"wireguard", L"tap-", L"bluetooth"
    };
    wchar_t low[160];
    int i;
    size_t n, k;
    if (!desc) return 0;
    n = wcslen(desc);
    if (n >= sizeof(low) / sizeof(low[0])) n = sizeof(low) / sizeof(low[0]) - 1;
    for (i = 0; i < (int)n; i++) {
        wchar_t ch = desc[i];
        low[i] = (ch >= L'A' && ch <= L'Z') ? (wchar_t)(ch + 32) : ch;
    }
    low[n] = 0;
    for (k = 0; k < sizeof(kw) / sizeof(kw[0]); k++)
        if (wcsstr(low, kw[k])) return 1;
    return 0;
}

/* 枚举所有活动网卡的 IPv4，附带适配器友好名（UTF-8）。
 * 物理网卡（WiFi/以太网）排在前面，虚拟网卡排后——手机上只有物理地址才连得通。
 * 跳过环回和 169.254 链路本地 */
static int EnumLocalIPv4(char ips[][24], char names[][64], int maxN)
{
    ULONG sz = 15000;
    IP_ADAPTER_ADDRESSES *aa = NULL, *p;
    int n = 0, k, pass;
    DWORD r;
    for (k = 0; k < 4; k++) {
        aa = (IP_ADAPTER_ADDRESSES*)malloc(sz);
        if (!aa) return 0;
        r = GetAdaptersAddresses(AF_INET,
            GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
            GAA_FLAG_SKIP_DNS_SERVER,
            NULL, aa, &sz);
        if (r == NO_ERROR) break;
        free(aa); aa = NULL;
        if (r != ERROR_BUFFER_OVERFLOW) return 0;
    }
    if (!aa) return 0;
    /* 两遍扫描：pass0=物理网卡，pass1=虚拟网卡 */
    for (pass = 0; pass < 2; pass++) {
        for (p = aa; p && n < maxN; p = p->Next) {
            IP_ADAPTER_UNICAST_ADDRESS *u;
            int virt;
            if (p->OperStatus != IfOperStatusUp) continue;
            if (p->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
            virt = IsVirtualAdapter(p->Description);
            if ((pass == 1) != virt) continue;
            for (u = p->FirstUnicastAddress; u && n < maxN; u = u->Next) {
                struct sockaddr_in *sa = (struct sockaddr_in*)u->Address.lpSockaddr;
                unsigned char *b = (unsigned char*)&sa->sin_addr;
                char tmp[24];
                int j, dup = 0;
                if (sa->sin_family != AF_INET) continue;
                if (b[0] == 127) continue;
                if (b[0] == 169 && b[1] == 254) continue;
                _snprintf(tmp, sizeof(tmp), "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
                for (j = 0; j < n; j++) if (strcmp(ips[j], tmp) == 0) dup = 1;
                if (dup) continue;
                snprintf(ips[n], 24, "%s", tmp);
                names[n][0] = 0;
                if (p->FriendlyName)
                    WideCharToMultiByte(CP_UTF8, 0, p->FriendlyName, -1,
                                        names[n], 64, NULL, NULL);
                n++;
            }
        }
    }
    free(aa);
    return n;
}

/* 首页：手机端播放/遥控页面（audio + Media Session 锁屏控制） */
static void HttpServeHome(SOCKET s)
{
    char ips[8][24];
    char names[8][64];
    int nip, i;
    int cap = 26000, pos = 0;
    char *pg;
    /* 界面文案（跟随程序语言） */
    const wchar_t *wAddr, *wLocal, *wSpk, *wRem, *wSd, *wCancel;
    const wchar_t *wNoShut, *wNoTitle, *wHint, *wMin, *wSd1, *wHint2;
    const wchar_t *wProg, *wVol, *wEps;
    const wchar_t *wM[4];
    const wchar_t *wMinPH, *wSet;
    char *uAddr="", *uLocal="", *uSpk="", *uRem="", *uSd="", *uCancel="";
    char *uNoShut="", *uNoTitle="", *uHint="", *uMin="", *uSd1="", *uHint2="";
    char *uProg="", *uVol="", *uEps="", *uM[4]={0,0,0,0};
    char *uMinPH="", *uSet="";

    nip = EnumLocalIPv4(ips, names, 8);
    if (g_lang == 1) {
        wAddr = L"Open on your phone (same Wi-Fi as the PC):";
        wLocal = L"On this PC:";
        wSpk = L"Phone speaker";
        wRem = L"Remote control PC";
        wSd = L"Auto shutdown";
        wCancel = L"Cancel";
        wNoShut = L"Not set";
        wNoTitle = L"Nothing playing on the PC";
        wHint = L"Audio bar above: phone as speaker. Buttons below: remote the PC "
                L"(playback, skip, seek 15s, shutdown).";
        wHint2 = L"Can't connect? 1) Type the full address with http:// and the port. "
                 L"2) Allow the firewall prompt when starting the server. "
                 L"3) Some campus/public Wi-Fi blocks device-to-device traffic; "
                 L"in that case use a hotspot or Tailscale instead.";
        wMin = L" min";
        wSd1 = L"Shutting down in";
        wProg = L"Progress";
        wVol = L"Volume";
        wEps = L"Episodes";
        wM[0] = L"Order"; wM[1] = L"Shuffle"; wM[2] = L"Repeat"; wM[3] = L"Play once";
        wMinPH = L"Minutes"; wSet = L"Set";
    } else {
        wAddr = L"手机浏览器打开（手机需与电脑在同一 Wi-Fi），认准 WiFi/以太网 地址：";
        wLocal = L"本机浏览器：";
        wSpk = L"手机出声";
        wRem = L"遥控电脑";
        wSd = L"定时关机";
        wCancel = L"取消";
        wNoShut = L"未设定";
        wNoTitle = L"电脑上还没有播放内容";
        wHint = L"上面播放器：手机当喇叭。下面按钮：遥控电脑（播放、上下曲、快进15秒、关机）。";
        wHint2 = L"连不上时：① 手机要输入完整地址（含 http:// 和端口）；"
                 L"② 开启服务时若弹出防火墙/权限请求请点允许；"
                 L"③ 校园网/公共 WiFi 可能禁止设备互连，可改用电脑开热点，"
                 L"或手机和电脑都装 Tailscale 后用它提供的地址。";
        wMin = L" 分";
        wSd1 = L"距离关机";
        wProg = L"进度";
        wVol = L"音量";
        wEps = L"选集播放";
        wM[0] = L"顺序"; wM[1] = L"随机"; wM[2] = L"循环"; wM[3] = L"播完停";
        wMinPH = L"分钟"; wSet = L"设置";
    }
    uAddr = WToU8(wAddr); uLocal = WToU8(wLocal); uSpk = WToU8(wSpk);
    uRem = WToU8(wRem); uSd = WToU8(wSd); uCancel = WToU8(wCancel);
    uNoShut = WToU8(wNoShut); uNoTitle = WToU8(wNoTitle); uHint = WToU8(wHint);
    uMin = WToU8(wMin); uSd1 = WToU8(wSd1); uHint2 = WToU8(wHint2);
    uProg = WToU8(wProg); uVol = WToU8(wVol); uEps = WToU8(wEps);
    for (i = 0; i < 4; i++) uM[i] = WToU8(wM[i]);
    uMinPH = WToU8(wMinPH); uSet = WToU8(wSet);

    pg = (char*)malloc(cap);
    if (!pg) { HttpRespond(s, "500 Server Error", "text/plain", "err", 3); goto done; }

    catf(pg, cap, &pos,
        "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>LiteTune</title><style>"
        "body{font-family:sans-serif;max-width:560px;margin:14px auto;padding:0 12px;"
        "background:#202024;color:#e6e6e6}"
        "h3{margin:8px 2px;font-weight:600}"
        ".box{background:#28282e;border:1px solid #3a3a40;border-radius:12px;"
        "padding:12px;margin:10px 0}"
        ".lab{font-size:13px;color:#b0b0b8;margin-bottom:8px}"
        "#t{font-size:16px;margin:4px 2px 8px;min-height:22px;word-break:break-all}"
        "audio{width:100%%}"
        ".grid{display:grid;grid-template-columns:repeat(4,1fr);gap:8px;margin-top:8px}"
        "button{font-size:17px;padding:13px 0;border:1px solid #4a4a52;"
        "border-radius:10px;background:#34343c;color:#eee}"
        "button:active{background:#0078d7;border-color:#0078d7}"
        ".ip{display:block;font-size:18px;color:#62b5ff;text-decoration:none;"
        "padding:6px 2px;font-family:monospace}"
        ".muted{font-size:12px;color:#9a9aa2;margin-top:8px;line-height:1.6}"
        "#sd{font-size:14px;color:#ffb84d;margin-top:10px}"
        "input[type=range]{width:100%%;margin:6px 0;accent-color:#0078d7}"
        "#pt{font-size:13px;color:#b0b0b8;text-align:center;font-family:monospace}"
        "select{width:100%%;font-size:15px;padding:8px;border-radius:8px;"
        "background:#34343c;color:#eee;border:1px solid #4a4a52;margin-bottom:8px}"
        "#el{max-height:260px;overflow-y:auto}"
        ".ep{display:block;width:100%%;text-align:left;font-size:14px;"
        "padding:10px;margin:4px 0;border:1px solid #4a4a52;border-radius:8px;"
        "background:#2e2e36;color:#ddd;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}"
        ".ep.cur{border-color:#3fbf6f;color:#3fbf6f}"
        ".ep .d{color:#8a8a92;font-size:12px;margin-left:6px}"
        ".row2{display:flex;gap:6px;margin-top:8px}"
        ".row2 button{flex:1;padding:9px 2px;font-size:13px}"
        "#mb{flex:1.6}"
        ".sp.on{border-color:#3fbf6f;color:#3fbf6f}"
        "input[type=number]{flex:1.4;font-size:14px;padding:8px;border-radius:8px;"
        "background:#34343c;color:#eee;border:1px solid #4a4a52}"
        "</style></head><body><h3>LiteTune</h3>");

    /* 地址列表：WiFi/以太网在前并标注网卡名，虚拟网卡在后 */
    catf(pg, cap, &pos, "<div class=\"box\"><div class=\"lab\">%s</div>", uAddr ? uAddr : "");
    for (i = 0; i < nip; i++)
        catf(pg, cap, &pos,
            "<a class=\"ip\" href=\"http://%s:%u/\">http://%s:%u/</a>"
            "<span class=\"muted\" style=\"margin:0\">%s</span>",
            ips[i], g_httpPort, ips[i], g_httpPort, names[i]);
    catf(pg, cap, &pos,
        "<div class=\"muted\">%s <a style=\"color:#62b5ff\" href=\"http://localhost:%u/\">"
        "http://localhost:%u/</a></div></div>",
        uLocal ? uLocal : "", g_httpPort, g_httpPort);

    /* 手机出声：音频流播放器 */
    catf(pg, cap, &pos,
        "<div class=\"box\"><div class=\"lab\">%s</div>"
        "<div id=\"t\">-</div>"
        "<audio id=\"a\" src=\"/audio\" controls preload=\"auto\"></audio></div>",
        uSpk ? uSpk : "");

    /* 遥控电脑：进度条（可拖动定位）+ 上下曲/播放暂停/停止/±15 秒 + 音量 */
    catf(pg, cap, &pos,
        "<div class=\"box\"><div class=\"lab\">%s</div>"
        "<div class=\"lab\">%s</div>"
        "<div id=\"pt\">--:-- / --:--</div>"
        "<input type=\"range\" id=\"sk\" min=\"0\" max=\"1000\" value=\"0\">"
        "<div class=\"grid\">"
        "<button onclick=\"cmd('prev')\">&#9198;</button>"
        "<button id=\"pb\" onclick=\"cmd('play')\">&#9654;</button>"
        "<button onclick=\"cmd('next')\">&#9197;</button>"
        "<button onclick=\"cmd('stop')\">&#9632;</button>"
        "<button onclick=\"cmd('back')\">-15s</button>"
        "<button onclick=\"cmd('fwd')\">+15s</button>"
        "</div>"
        "<div class=\"row2\">"
        "<button id=\"mb\" onclick=\"cmd('mode')\">%s</button>"
        "<button class=\"sp\" data-v=\"75\" onclick=\"cmd('speed','v',75)\">0.75x</button>"
        "<button class=\"sp\" data-v=\"100\" onclick=\"cmd('speed','v',100)\">1x</button>"
        "<button class=\"sp\" data-v=\"125\" onclick=\"cmd('speed','v',125)\">1.25x</button>"
        "<button class=\"sp\" data-v=\"150\" onclick=\"cmd('speed','v',150)\">1.5x</button>"
        "<button class=\"sp\" data-v=\"200\" onclick=\"cmd('speed','v',200)\">2x</button>"
        "</div>"
        "<div class=\"lab\" style=\"margin-top:10px\">%s</div>"
        "<input type=\"range\" id=\"vl\" min=\"0\" max=\"100\" value=\"80\">"
        "</div>",
        uRem ? uRem : "", uProg ? uProg : "",
        uM[0] ? uM[0] : "", uVol ? uVol : "");

    /* 选集播放：播客下拉 + 剧集按钮列表（点击任意一集播放） */
    catf(pg, cap, &pos,
        "<div class=\"box\"><div class=\"lab\">%s</div>"
        "<select id=\"pl\" onchange=\"epPicked()\"></select>"
        "<div id=\"el\"></div></div>",
        uEps ? uEps : "");

    /* 定时关机（15/30/60 分 + 自定义分钟 + 取消，倒计时每秒刷新） */
    catf(pg, cap, &pos,
        "<div class=\"box\"><div class=\"lab\">%s</div><div class=\"grid\">"
        "<button onclick=\"cmd('shut','m',15)\">15%s</button>"
        "<button onclick=\"cmd('shut','m',30)\">30%s</button>"
        "<button onclick=\"cmd('shut','m',60)\">60%s</button>"
        "<button onclick=\"cmd('shutoff')\">%s</button>"
        "</div>"
        "<div class=\"row2\">"
        "<input type=\"number\" id=\"sm\" min=\"1\" max=\"720\" placeholder=\"%s\">"
        "<button style=\"flex:1\" onclick=\"customShut()\">%s</button>"
        "</div>"
        "<div id=\"sd\"></div></div>",
        uSd ? uSd : "", uMin ? uMin : "", uMin ? uMin : "", uMin ? uMin : "",
        uCancel ? uCancel : "", uMinPH ? uMinPH : "", uSet ? uSet : "");

    catf(pg, cap, &pos, "<div class=\"muted\">%s</div>", uHint ? uHint : "");
    catf(pg, cap, &pos, "<div class=\"muted\">%s</div>", uHint2 ? uHint2 : "");

    catf(pg, cap, &pos,
        "<script>"
        "var A=document.getElementById('a'),lastT='';"
        "var SK=document.getElementById('sk'),VL=document.getElementById('vl'),"
        "PL=document.getElementById('pl'),EL=document.getElementById('el');"
        "var NOTITLE='%s',NOSHUT='%s',SD1='%s';"
        "var MODES=['%s','%s','%s','%s'];"
        "var skDrag=false,vlDrag=false,userPicked=false,curEpPod=-1,lastPodList='';"
        "function cmd(x){var u='/cmd?a='+x;for(var i=1;i<arguments.length;i+=2)"
        "u+='&'+arguments[i]+'='+arguments[i+1];"
        "var f=new XMLHttpRequest();f.open('GET',u);f.send();setTimeout(poll,300);}"
        "function pad(n){return n<10?'0'+n:''+n;}"
        "function hms(s){var h=Math.floor(s/3600),m=Math.floor(s%%3600/60),r=s%%60;"
        "return (h?pad(h)+':':'')+pad(m)+':'+pad(r);}"
        "SK.oninput=function(){skDrag=true;};"
        "SK.onchange=function(){cmd('seek','p',SK.value);skDrag=false;};"
        "VL.oninput=function(){vlDrag=true;};"
        "VL.onchange=function(){cmd('vol','v',VL.value);vlDrag=false;};"
        "function epPicked(){userPicked=true;loadEps(PL.value);}"
        "function customShut(){var v=parseInt(document.getElementById('sm').value,10);"
        "if(v>0)cmd('shut','m',v);}"
        "function loadPods(cb){"
        "var f=new XMLHttpRequest();f.open('GET','/pods');"
        "f.onload=function(){try{var ps=JSON.parse(f.responseText);"
        "var key=ps.map(function(p){return p.i;}).join(',');"
        "if(key!==lastPodList){lastPodList=key;PL.innerHTML='';"
        "ps.forEach(function(p){var o=document.createElement('option');"
        "o.value=p.i;o.textContent=p.t;PL.appendChild(o);});}"
        "if(cb)cb();}catch(e){}};f.send();}"
        "function loadEps(p){"
        "var f=new XMLHttpRequest();f.open('GET','/eps?p='+p);"
        "f.onload=function(){try{var d=JSON.parse(f.responseText);"
        "curEpPod=d.pod;EL.innerHTML='';"
        "d.eps.forEach(function(e,idx){var b=document.createElement('button');"
        "b.className='ep'+(e.cur?' cur':'');"
        "b.textContent=e.t;"
        "if(e.d>0){var s=document.createElement('span');s.className='d';"
        "s.textContent=hms(e.d);b.appendChild(s);}"
        "b.onclick=function(){cmd('playep','p',d.pod,'e',idx);};"
        "EL.appendChild(b);});}catch(e){}};f.send();}"
        "function poll(){"
        "var f=new XMLHttpRequest();f.open('GET','/status');"
        "f.onload=function(){try{var s=JSON.parse(f.responseText);"
        "document.getElementById('t').textContent=s.title||NOTITLE;"
        "document.getElementById('pb').innerHTML=s.playing?'&#9208;':'&#9654;';"
        "document.getElementById('sd').textContent=s.shut>0?SD1+' '+hms(s.shut):NOSHUT;"
        "if(!skDrag&&s.dur>0)SK.value=Math.floor(s.pos*1000/s.dur);"
        "document.getElementById('pt').textContent=hms(s.pos)+' / '+(s.dur>0?hms(s.dur):'--:--');"
        "if(!vlDrag)VL.value=s.vol;"
        "document.getElementById('mb').textContent=MODES[s.mode]||MODES[0];"
        "document.querySelectorAll('.sp').forEach(function(b){"
        "b.className='sp'+(+b.dataset.v===s.speed?' on':'');});"
        "if(!userPicked&&s.pod>=0&&PL.options.length&&+PL.value!==s.pod){PL.value=s.pod;loadEps(s.pod);}"
        "if('mediaSession' in navigator){"
        "navigator.mediaSession.metadata=new MediaMetadata({title:s.title||'LiteTune',artist:'LiteTune'});"
        "navigator.mediaSession.playbackState=s.playing?'playing':'paused';}"
        "if(s.title&&s.title!==lastT){lastT=s.title;if(s.has)A.load();loadEps(curEpPod<0?s.pod:curEpPod);}"
        "}catch(e){}};f.send();}"
        "if('mediaSession' in navigator){try{"
        "navigator.mediaSession.setActionHandler('play',function(){A.play();});"
        "navigator.mediaSession.setActionHandler('pause',function(){A.pause();});"
        "navigator.mediaSession.setActionHandler('previoustrack',function(){cmd('prev');});"
        "navigator.mediaSession.setActionHandler('nexttrack',function(){cmd('next');});"
        "navigator.mediaSession.setActionHandler('seekbackward',function(){cmd('back');});"
        "navigator.mediaSession.setActionHandler('seekforward',function(){cmd('fwd');});"
        "}catch(e){}}"
        "loadPods(function(){poll();});setInterval(function(){if(!document.hidden)poll();},1000);"
        "</script></body></html>",
        uNoTitle ? uNoTitle : "", uNoShut ? uNoShut : "", uSd1 ? uSd1 : "",
        uM[0] ? uM[0] : "", uM[1] ? uM[1] : "",
        uM[2] ? uM[2] : "", uM[3] ? uM[3] : "");

    HttpRespond(s, "200 OK", "text/html; charset=utf-8", pg, pos);
    free(pg);
done:
    free(uAddr); free(uLocal); free(uSpk); free(uRem); free(uSd); free(uCancel);
    free(uNoShut); free(uNoTitle); free(uHint); free(uMin); free(uSd1);
    free(uHint2);
    free(uProg); free(uVol); free(uEps);
    for (i = 0; i < 4; i++) free(uM[i]);
    free(uMinPH); free(uSet);
}

/* /status：JSON 返回当前标题、播放状态、进度/时长/音量、正在播放下标、关机剩余秒数 */
static void HttpServeStatus(SOCKET s)
{
    wchar_t path[MAX_PATH], title[256];
    char t8[1024], body[1408];
    long long shutLeft = 0;
    int n;
    EnterCriticalSection(&g_nowCs);
    wcsncpy(path, g_nowPath, MAX_PATH - 1); path[MAX_PATH - 1] = 0;
    wcsncpy(title, g_nowTitle, 255);        title[255] = 0;
    LeaveCriticalSection(&g_nowCs);
    if (g_shutdownAt > 0) {
        shutLeft = g_shutdownAt - (long long)time(NULL);
        if (shutLeft < 0) shutLeft = 0;
    }
    JsonEscapeU8(title, t8, sizeof(t8));
    n = _snprintf(body, sizeof(body),
        "{\"title\":\"%s\",\"playing\":%s,\"has\":%s,\"shut\":%d,"
        "\"pos\":%d,\"dur\":%d,\"vol\":%d,\"pod\":%d,\"ep\":%d,"
        "\"mode\":%d,\"speed\":%d}",
        t8, g_playState == 1 ? "true" : "false",
        path[0] ? "true" : "false", (int)shutLeft,
        g_posSec, g_durSec, g_volume, g_playPod, g_playEp,
        g_playMode, g_speed);
    HttpRespond(s, "200 OK", "application/json; charset=utf-8", body, n);
}

/* /pods：JSON 播客列表，供手机选播客；i 为下标（缓存行 = g_podCount） */
static void HttpServePods(SOCKET s)
{
    int cap = 1024 + g_podCount * 768, pos = 0, i;
    char *body = (char*)malloc(cap);
    if (!body) { HttpRespond(s, "500 Server Error", "text/plain", "err", 3); return; }
    pos += _snprintf(body + pos, cap - pos, "[");
    for (i = 0; i < g_podCount; i++) {
        char t8[640];
        JsonEscapeU8(g_pods[i].title ? g_pods[i].title : L"", t8, sizeof(t8));
        pos += _snprintf(body + pos, cap - pos, "%s{\"i\":%d,\"t\":\"%s\"}",
            i ? "," : "", i, t8);
    }
    if (g_cacheFileCount > 0) {
        /* “已缓存”懒加载：未扫描时用标题占位，点选（/eps）时再真正建列表 */
        wchar_t row[64];
        char t8[640];
        swprintf(row, 64, tr(TR_CACHED_FMT), g_cacheFileCount);
        JsonEscapeU8(g_cachePod.epCount > 0 && g_cachePod.title
                     ? g_cachePod.title : row, t8, sizeof(t8));
        pos += _snprintf(body + pos, cap - pos, "%s{\"i\":%d,\"t\":\"%s\"}",
            g_podCount ? "," : "", g_podCount, t8);
    }
    _snprintf(body + pos, cap - pos, "]");
    HttpRespond(s, "200 OK", "application/json; charset=utf-8", body, (int)strlen(body));
    free(body);
}

/* /eps?p=N：JSON 某播客的剧集列表（含时长与正在播放标记），N==g_podCount 为缓存视图 */
static void HttpServeEps(SOCKET s, const char *pathq)
{
    const char *pp = strstr(pathq, "p=");
    Podcast *pod;
    int p, j, cap, pos;
    char *body;
    if (!pp) { HttpRespond(s, "400 Bad Request", "text/plain", "no p", 4); return; }
    p = atoi(pp + 2);
    if (p == g_podCount) {
        /* 懒加载：从未打开过“已缓存”视图时这里才真正扫描建列表 */
        if (g_cachePod.epCount == 0 && g_cacheFileCount > 0)
            LoadCachePodcast();
        pod = &g_cachePod;
    }
    else if (p >= 0 && p < g_podCount)          pod = &g_pods[p];
    else { HttpRespond(s, "404 Not Found", "text/plain", "no pod", 6); return; }

    cap = 4096 + pod->epCount * 768;
    body = (char*)malloc(cap);
    if (!body) { HttpRespond(s, "500 Server Error", "text/plain", "err", 3); return; }
    pos = _snprintf(body, cap, "{\"pod\":%d,\"eps\":[", p);
    for (j = 0; j < pod->epCount && pos < cap - 800; j++) {
        char t8[640];
        JsonEscapeU8(pod->eps[j].title ? pod->eps[j].title : L"", t8, sizeof(t8));
        pos += _snprintf(body + pos, cap - pos, "%s{\"t\":\"%s\",\"d\":%d,\"cur\":%s}",
            j ? "," : "", t8, pod->eps[j].durationSec,
            (g_playPod == p && g_playEp == j) ? "1" : "0");
    }
    _snprintf(body + pos, cap - pos, "]}");
    HttpRespond(s, "200 OK", "application/json; charset=utf-8", body, (int)strlen(body));
    free(body);
}

/* /audio：流式输出当前播放的本地 mp3，支持 Range 拖动 */
static void HttpServeAudio(SOCKET s, const char *req)
{
    wchar_t path[MAX_PATH];
    HANDLE hf;
    LARGE_INTEGER fsize, start, end;
    int partial = 0;
    const char *rp;

    EnterCriticalSection(&g_nowCs);
    wcsncpy(path, g_nowPath, MAX_PATH - 1); path[MAX_PATH - 1] = 0;
    LeaveCriticalSection(&g_nowCs);
    if (!path[0]) { HttpRespond(s, "404 Not Found", "text/plain", "no audio", 8); return; }

    hf = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (hf == INVALID_HANDLE_VALUE) {
        HttpRespond(s, "404 Not Found", "text/plain", "no file", 7);
        return;
    }
    fsize.QuadPart = 0;
    GetFileSizeEx(hf, &fsize);
    start.QuadPart = 0;
    end.QuadPart = fsize.QuadPart > 0 ? fsize.QuadPart - 1 : 0;

    rp = strstr(req, "\r\nRange:");
    if (!rp) rp = strstr(req, "\r\nrange:");
    if (rp) {
        const char *bp = strstr(rp, "bytes=");
        if (bp) {
            char *ep1;
            bp += 6;
            if (*bp == '-') {
                /* 后缀形式 bytes=-N：最后 N 字节（iOS Safari 探测时长会用） */
                long long suf = strtoll(bp + 1, NULL, 10);
                if (suf > 0 && fsize.QuadPart > 0) {
                    if (suf > fsize.QuadPart) suf = fsize.QuadPart;
                    start.QuadPart = fsize.QuadPart - suf;
                    partial = 1;
                }
            } else {
                long long a = strtoll(bp, &ep1, 10);
                if (ep1 > bp && a >= 0) {
                    long long b = -1;
                    if (*ep1 == '-' && ep1[1] >= '0' && ep1[1] <= '9')
                        b = strtoll(ep1 + 1, NULL, 10);
                    if (a >= fsize.QuadPart) {
                        /* 起点超出文件：416，浏览器会自行重新请求 */
                        char h[200];
                        int m = _snprintf(h, sizeof(h),
                            "HTTP/1.1 416 Range Not Satisfiable\r\n"
                            "Content-Range: bytes */%d\r\nContent-Length: 0\r\n"
                            "Connection: close\r\n\r\n", (int)fsize.QuadPart);
                        if (m > 0) SendAll(s, h, m);
                        CloseHandle(hf);
                        return;
                    }
                    start.QuadPart = a;
                    if (b >= 0 && b < fsize.QuadPart) end.QuadPart = b;
                    partial = 1;
                }
            }
        }
    }
    {
        char hdr[512];
        long long remain = end.QuadPart - start.QuadPart + 1;
        int n;
        if (partial)
            n = _snprintf(hdr, sizeof(hdr),
                "HTTP/1.1 206 Partial Content\r\nContent-Type: audio/mpeg\r\n"
                "Accept-Ranges: bytes\r\nContent-Range: bytes %d-%d/%d\r\n"
                "Content-Length: %d\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n",
                (int)start.QuadPart, (int)end.QuadPart, (int)fsize.QuadPart, (int)remain);
        else
            n = _snprintf(hdr, sizeof(hdr),
                "HTTP/1.1 200 OK\r\nContent-Type: audio/mpeg\r\n"
                "Accept-Ranges: bytes\r\nContent-Length: %d\r\n"
                "Cache-Control: no-store\r\nConnection: close\r\n\r\n",
                (int)remain);
        if (n <= 0 || !SendAll(s, hdr, n)) { CloseHandle(hf); return; }
        SetFilePointerEx(hf, start, NULL, FILE_BEGIN);
        {
            char buf[65536];
            while (remain > 0) {
                DWORD want = remain > (long long)sizeof(buf) ? sizeof(buf) : (DWORD)remain;
                DWORD got = 0;
                if (!ReadFile(hf, buf, want, &got, NULL) || got == 0) break;
                if (!SendAll(s, buf, (int)got)) break;
                remain -= got;
            }
        }
    }
    CloseHandle(hf);
}

/* /cmd?a=xxx[&m=分钟]：遥控电脑（等价于点主窗口按钮 / 快进15秒 / 定时关机） */
static void HttpServeCmd(SOCKET s, const char *pathq)
{
    const char *a = strstr(pathq, "a=");
    int id = 0, mins = 0;
    const char *mp;
    if (!a) { HttpRespond(s, "200 OK", "application/json", "{\"ok\":0}", 7); return; }
    a += 2;
    /* playep 必须在 play 之前判断（前缀匹配），shutoff 同理在 shut 之前 */
    if      (strncmp(a, "playep", 6) == 0) {
        const char *pp = strstr(pathq, "&p="), *ep = strstr(pathq, "&e=");
        if (pp && ep && g_hwnd)
            PostMessageW(g_hwnd, WM_APP_REMOTE_PLAYEP, (WPARAM)atoi(pp + 3), (LPARAM)atoi(ep + 3));
    }
    else if (strncmp(a, "play", 4) == 0)    id = IDC_BTN_PLAY;
    else if (strncmp(a, "prev", 4) == 0)    id = IDC_BTN_PREV;
    else if (strncmp(a, "next", 4) == 0)    id = IDC_BTN_NEXT;
    else if (strncmp(a, "stop", 4) == 0)    id = IDC_BTN_STOP;
    else if (strncmp(a, "seek", 4) == 0) {
        const char *pp = strstr(pathq, "&p=");
        if (pp && g_hwnd) {
            int pm = atoi(pp + 3);
            if (pm < 0) pm = 0;
            if (pm > 1000) pm = 1000;
            PostMessageW(g_hwnd, WM_APP_REMOTE_POS, (WPARAM)pm, 0);
        }
    }
    else if (strncmp(a, "vol", 3) == 0) {
        const char *vp = strstr(pathq, "&v=");
        if (vp && g_hwnd)
            PostMessageW(g_hwnd, WM_APP_REMOTE_VOL, (WPARAM)atoi(vp + 3), 0);
    }
    else if (strncmp(a, "mode", 4) == 0) {
        if (g_hwnd) PostMessageW(g_hwnd, WM_APP_REMOTE_MODE, 0, 0);
    }
    else if (strncmp(a, "speed", 5) == 0) {
        const char *vp = strstr(pathq, "&v=");
        if (vp && g_hwnd) {
            int v = atoi(vp + 3);
            if (v < 50) v = 50;
            if (v > 300) v = 300;
            PostMessageW(g_hwnd, WM_APP_REMOTE_SPEED, (WPARAM)v, 0);
        }
    }
    else if (strncmp(a, "back", 4) == 0)    { if (g_hwnd) PostMessageW(g_hwnd, WM_APP_REMOTE_SEEK, 0, 0); }
    else if (strncmp(a, "fwd", 3) == 0)     { if (g_hwnd) PostMessageW(g_hwnd, WM_APP_REMOTE_SEEK, 1, 0); }
    else if (strncmp(a, "shutoff", 7) == 0) { if (g_hwnd) PostMessageW(g_hwnd, WM_APP_REMOTE_SHUT, 0, 0); }
    else if (strncmp(a, "shut", 4) == 0) {
        mp = strstr(pathq, "&m=");
        if (mp) {
            mins = atoi(mp + 3);
            if (mins < 1) mins = 1;
            if (mins > 600) mins = 600;
            if (g_hwnd) PostMessageW(g_hwnd, WM_APP_REMOTE_SHUT, (WPARAM)mins, 0);
        }
    }
    if (id && g_hwnd)
        PostMessageW(g_hwnd, WM_COMMAND, MAKEWPARAM(id, BN_CLICKED), 0);
    HttpRespond(s, "200 OK", "application/json", "{\"ok\":1}", 7);
}

/* 单个客户端连接：读请求行与头部，路由到对应处理函数，关闭 */
static DWORD WINAPI PhoneClientThread(LPVOID p)
{
    SOCKET c = (SOCKET)(UINT_PTR)p;
    char req[4096];
    int total = 0, n;
    DWORD tv = 8000;
    setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
    tv = 15000;   /* 发送也设超时：手机卡住时线程不会永久挂住 */
    setsockopt(c, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof(tv));
    while (total < (int)sizeof(req) - 1) {
        n = recv(c, req + total, (int)sizeof(req) - 1 - total, 0);
        if (n <= 0) break;
        total += n;
        req[total] = 0;
        if (strstr(req, "\r\n\r\n")) break;
    }
    if (total > 0) {
        char *sp = strchr(req, ' ');
        if (sp) {
            char *path = sp + 1;
            char *end = strchr(path, ' ');
            if (end) *end = 0;
            if      (strncmp(path, "/audio", 6) == 0) HttpServeAudio(c, req);
            else if (strncmp(path, "/status", 7) == 0) HttpServeStatus(c);
            else if (strncmp(path, "/pods", 5) == 0)  HttpServePods(c);
            else if (strncmp(path, "/eps", 4) == 0)   HttpServeEps(c, path);
            else if (strncmp(path, "/cmd", 4) == 0)   HttpServeCmd(c, path);
            else HttpServeHome(c);
        }
    }
    closesocket(c);
    return 0;
}

static DWORD WINAPI PhoneServerThread(LPVOID p)
{
    (void)p;
    while (!g_httpStop) {
        SOCKET c = accept(g_httpSock, NULL, NULL);
        HANDLE th;
        if (c == INVALID_SOCKET) {
            /* 监听 socket 被关闭 → 退场退出；其它瞬时错误（如 WSAENOBUFS）
             * 只歇一下继续，不能让服务线程悄悄死掉（表现为手机一直超时） */
            if (g_httpStop || g_httpSock == INVALID_SOCKET) break;
            Sleep(100);
            continue;
        }
        th = CreateThread(NULL, 0, PhoneClientThread, (LPVOID)(UINT_PTR)c, 0, NULL);
        if (th) CloseHandle(th);
        else closesocket(c);
    }
    return 0;
}

/* 防火墙规则是否存在，且指向当前 exe。
 * 只按规则名判断不够：复制或移动过 exe 时，旧规则指向别的路径，本机浏览器
 * 能打开页面，局域网却会被防火墙直接拦掉（手机一直转圈/超时）。
 * netsh verbose 输出带 Program: 完整路径，按路径再校验一次。 */
static int FirewallRuleExists(void)
{
    char tmp[MAX_PATH], cmd[760], buf[4096], exeA[MAX_PATH * 2];
    wchar_t exeW[MAX_PATH];
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    HANDLE hf;
    DWORD got;
    int found = 0;
    if (GetTempPathA(sizeof(tmp), tmp) == 0) return 0;   /* 拿不到临时目录：视为无规则，最多多弹一次询问，无害 */
    strncat(tmp, "ltfw.txt", sizeof(tmp) - strlen(tmp) - 1);
    DeleteFileA(tmp);
    snprintf(cmd, sizeof(cmd),
        "cmd /c netsh advfirewall firewall show rule name=\"LiteTune Phone\" verbose "
        "> \"%s\" 2>&1", tmp);
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    ZeroMemory(&pi, sizeof(pi));
    if (CreateProcessA(NULL, cmd, NULL, NULL, FALSE,
                       CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, 5000);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
    hf = CreateFileA(tmp, GENERIC_READ, FILE_SHARE_READ, NULL,
                     OPEN_EXISTING, 0, NULL);
    if (hf != INVALID_HANDLE_VALUE) {
        if (ReadFile(hf, buf, sizeof(buf) - 1, &got, NULL) && got > 0) {
            buf[got] = 0;
            /* 重定向文件由 cmd 按控制台 OEM 编码写入，路径也按 OEM 编码比较 */
            if (GetModuleFileNameW(NULL, exeW, MAX_PATH) &&
                WideCharToMultiByte(CP_OEMCP, 0, exeW, -1, exeA, sizeof(exeA),
                                    NULL, NULL) > 0 &&
                StrStrIA(buf, "LiteTune Phone") && StrStrIA(buf, exeA))
                found = 1;
        }
        CloseHandle(hf);
    }
    return found;
}

/* 确保防火墙入站规则存在：不存在就提权添加（弹 UAC），
 * 等待提权进程结束后再校验一次。
 * 先删旧规则（防 exe 路径过期），再按当前路径添加；
 * profile=any 覆盖公用/专用/域网络。返回 1=规则确认存在 */
static int FirewallRuleEnsure(void)
{
    wchar_t exe[MAX_PATH], params[900];
    SHELLEXECUTEINFOW sei;
    if (FirewallRuleExists()) return 1;
    GetModuleFileNameW(NULL, exe, MAX_PATH);
    _snwprintf(params, 900,
        L"/c netsh advfirewall firewall delete rule name=\"LiteTune Phone\" >nul 2>&1 & "
        L"netsh advfirewall firewall add rule name=\"LiteTune Phone\" "
        L"dir=in action=allow program=\"%ls\" enable=yes profile=any", exe);
    ZeroMemory(&sei, sizeof(sei));
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = L"runas";
    sei.lpFile = L"cmd.exe";
    sei.lpParameters = params;
    sei.nShow = SW_HIDE;
    if (!ShellExecuteExW(&sei)) return 0;   /* 用户拒绝 UAC 等 */
    if (sei.hProcess) {
        WaitForSingleObject(sei.hProcess, 15000);
        CloseHandle(sei.hProcess);
    }
    return FirewallRuleExists();
}

static int PhoneServerStart(void)
{
    WSADATA wsa;
    struct sockaddr_in addr;
    int one = 1;
    int i, nPorts = (int)(sizeof(PHONE_PORTS) / sizeof(PHONE_PORTS[0]));
    if (g_httpThread) return 1;   /* 已在运行 */
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 0;
    g_httpSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (g_httpSock == INVALID_SOCKET) { WSACleanup(); return 0; }
    setsockopt(g_httpSock, SOL_SOCKET, SO_REUSEADDR, (const char*)&one, sizeof(one));
    ZeroMemory(&addr, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    g_httpPort = 0;
    for (i = 0; i < nPorts; i++) {
        addr.sin_port = htons(PHONE_PORTS[i]);
        if (bind(g_httpSock, (struct sockaddr*)&addr, sizeof(addr)) == 0 &&
            listen(g_httpSock, 32) == 0) {
            g_httpPort = PHONE_PORTS[i];
            break;
        }
    }
    if (!g_httpPort) {
        closesocket(g_httpSock);
        g_httpSock = INVALID_SOCKET;
        WSACleanup();
        return 0;
    }
    g_httpStop = 0;
    g_httpThread = CreateThread(NULL, 0, PhoneServerThread, NULL, 0, NULL);
    if (!g_httpThread) {
        closesocket(g_httpSock);
        g_httpSock = INVALID_SOCKET;
        g_httpPort = 0;
        WSACleanup();
        return 0;
    }
    /* 无论绑到哪个端口，手机都属于“入站连接”，Windows 防火墙默认拦：
     * 提权加一条入站规则（仅弹一次 UAC）。复制到别的电脑首次运行时
     * 也会在这里重新询问——这正是“换电脑就连不上”的原因。
     * 返回 2=服务已启动但规则没建成（用户拒绝 UAC 等） */
    return FirewallRuleEnsure() ? 1 : 2;
}

static void PhoneServerStop(void)
{
    if (!g_httpThread) return;
    g_httpStop = 1;
    if (g_httpSock != INVALID_SOCKET) {
        closesocket(g_httpSock);      /* 使 accept 立即失败退出 */
        g_httpSock = INVALID_SOCKET;
    }
    WaitForSingleObject(g_httpThread, 3000);
    CloseHandle(g_httpThread);
    g_httpThread = NULL;
    WSACleanup();
}

/* ================= UI 逻辑 ================= */
/* 切换剧集列表列头：普通=标题/日期/时长；缓存视图=播客/标题/时长 */
static void SetEpisodeColumns(int cached)
{
    const wchar_t *titlesNormal[3] = { tr(TR_COL_TITLE), tr(TR_COL_DATE), tr(TR_COL_DUR) };
    const wchar_t *titlesCache[3]  = { tr(TR_COL_PODCAST), tr(TR_COL_TITLE), tr(TR_COL_DUR) };
    const wchar_t **titles = cached ? titlesCache : titlesNormal;
    int widths[3], i;
    if (cached) {
        RECT rc;
        GetClientRect(g_listEp, &rc);
        widths[0] = 84;
        widths[2] = 46;
        widths[1] = (rc.right - rc.left) - widths[0] - widths[2] - 24;
        if (widths[1] < 60) widths[1] = 60;
    } else {
        widths[0] = g_colW[0]; widths[1] = g_colW[1]; widths[2] = g_colW[2];
    }
    while (SendMessageW(g_listEp, LVM_DELETECOLUMN, 0, 0)) {}
    for (i = 0; i < 3; i++) {
        LVCOLUMNW col;
        ZeroMemory(&col, sizeof(col));
        col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
        col.cx = widths[i];
        col.iSubItem = i;
        col.pszText = (wchar_t*)titles[i];
        SendMessageW(g_listEp, LVM_INSERTCOLUMNW, i, (LPARAM)&col);
    }
    /* 新建列会丢失 HDF_OWNERDRAW，暗色下要重新设置 */
    ApplyHeaderTheme();
}

/* 当前剧集列表对应的播客（可能是“已缓存”虚拟播客） */
static Podcast* ActivePodcast(void)
{
    if (g_viewCached) return &g_cachePod;
    if (g_curPod >= 0 && g_curPod < g_podCount) return &g_pods[g_curPod];
    return NULL;
}

/* 用当前播客的剧集填充 ListView（调用前数组应已排序）。
 * WM_SETREDRAW 括起来：几百条时逐条插入的中间重绘既闪又慢，一次画完秒出 */
static void FillEpisodeList(void)
{
    Podcast *p = ActivePodcast();
    int i;
    if (!p) return;
    SendMessageW(g_listEp, WM_SETREDRAW, FALSE, 0);
    SendMessageW(g_listEp, LVM_DELETEALLITEMS, 0, 0);
    for (i = 0; i < p->epCount; i++) {
        wchar_t dur[16];
        LVITEMW lvi;
        ZeroMemory(&lvi, sizeof(lvi));
        lvi.mask = LVIF_TEXT;
        lvi.iItem = i;
        /* 缓存视图列序是 播客/标题/时长，普通视图是 标题/日期/时长 */
        lvi.pszText = g_viewCached
            ? (p->eps[i].author ? p->eps[i].author : L"")
            : p->eps[i].title;
        SendMessageW(g_listEp, LVM_INSERTITEMW, 0, (LPARAM)&lvi);

        lvi.iSubItem = 1;
        lvi.pszText = g_viewCached
            ? p->eps[i].title
            : (p->eps[i].dateKey ? p->eps[i].dateText : L"");
        SendMessageW(g_listEp, LVM_SETITEMW, 0, (LPARAM)&lvi);

        FmtTime(p->eps[i].durationSec, dur, 16);
        lvi.iSubItem = 2;
        lvi.pszText = p->eps[i].durationSec > 0 ? dur : L"";
        SendMessageW(g_listEp, LVM_SETITEMW, 0, (LPARAM)&lvi);
    }
    SendMessageW(g_listEp, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_listEp, NULL, TRUE);
}

/* 构建剧集 tooltip 文本（标题+播客/日期+时长），写入 buf */
static void BuildEpisodeTipText(wchar_t *buf, int bufMax, int i)
{
    Podcast *p = ActivePodcast();
    Episode *e;
    wchar_t dur[16];
    int pos = 0;
    buf[0] = 0;
    if (!p || i < 0 || i >= p->epCount) return;
    e = &p->eps[i];
    if (e->title && *e->title)
        pos += swprintf(buf + pos, bufMax - pos, L"%ls", e->title);
    if (g_viewCached) {
        if (e->author && *e->author)
            pos += swprintf(buf + pos, bufMax - pos,
                            tr(TR_TIP_PODCAST_FMT), e->author);
    } else if (p->title && *p->title) {
        pos += swprintf(buf + pos, bufMax - pos,
                        tr(TR_TIP_PODCAST_FMT), p->title);
    }
    if (!g_viewCached && e->dateKey)
        pos += swprintf(buf + pos, bufMax - pos,
                        tr(TR_TIP_PUBDATE_FMT), e->dateText);
    if (e->durationSec > 0) {
        FmtTime(e->durationSec, dur, 16);
        pos += swprintf(buf + pos, bufMax - pos,
                        tr(TR_TIP_DUR_FMT), dur);
    }
}

/* 把字段拼成 "【标题】\r\n内容\r\n\r\n..." 塞进简介框 */
static void ShowDetailField(wchar_t *buf, size_t bufSize, size_t *pos,
                            const wchar_t *label, const wchar_t *value)
{
    if (!value || !*value) return;
    *pos += swprintf(buf + *pos, bufSize - *pos,
        L"【%ls】\r\n%ls\r\n\r\n", label, value);
}

static void ShowPodcastDetail(int idx)
{
    static wchar_t buf[32768];
    size_t pos = 0;
    Podcast *p;
    wchar_t *feedW = NULL;
    if (idx < 0 || idx >= g_podCount) return;
    p = &g_pods[idx];
    buf[0] = 0;
    ShowDetailField(buf, sizeof(buf)/sizeof(buf[0]), &pos, tr(TR_F_TITLE), p->title);
    ShowDetailField(buf, sizeof(buf)/sizeof(buf[0]), &pos, tr(TR_F_DESC), p->desc);
    ShowDetailField(buf, sizeof(buf)/sizeof(buf[0]), &pos, tr(TR_F_AUTHOR), p->author);
    ShowDetailField(buf, sizeof(buf)/sizeof(buf[0]), &pos, tr(TR_F_LANG), p->lang);
    ShowDetailField(buf, sizeof(buf)/sizeof(buf[0]), &pos, tr(TR_F_HOME), p->link);
    ShowDetailField(buf, sizeof(buf)/sizeof(buf[0]), &pos, tr(TR_F_COPYRIGHT), p->copyright);
    if (p->feedUrl) {
        feedW = U8ToW(p->feedUrl);
        if (feedW) {
            ShowDetailField(buf, sizeof(buf)/sizeof(buf[0]), &pos, tr(TR_F_FEEDURL), feedW);
            free(feedW);
        }
    }
    SetWindowTextW(g_editDesc, buf);
}

static void ShowEpisodeDetail(int podIdx, int epIdx)
{
    static wchar_t buf[32768];
    size_t pos = 0;
    Podcast *p;
    Episode *e;
    wchar_t dur[16];
    if (podIdx == g_podCount) p = &g_cachePod;
    else if (podIdx >= 0 && podIdx < g_podCount) p = &g_pods[podIdx];
    else return;
    if (epIdx < 0 || epIdx >= p->epCount) return;
    e = &p->eps[epIdx];
    buf[0] = 0;
    ShowDetailField(buf, sizeof(buf)/sizeof(buf[0]), &pos, tr(TR_F_TITLE), e->title);
    ShowDetailField(buf, sizeof(buf)/sizeof(buf[0]), &pos, tr(TR_F_DESC), e->desc);
    if (g_viewCached)
        ShowDetailField(buf, sizeof(buf)/sizeof(buf[0]), &pos, tr(TR_COL_PODCAST), e->author);
    else
        ShowDetailField(buf, sizeof(buf)/sizeof(buf[0]), &pos, tr(TR_F_AUTHOR), e->author);
    if (e->dateKey) ShowDetailField(buf, sizeof(buf)/sizeof(buf[0]), &pos, tr(TR_F_PUBDATE), e->dateText);
    if (e->durationSec > 0) {
        FmtTime(e->durationSec, dur, 16);
        ShowDetailField(buf, sizeof(buf)/sizeof(buf[0]), &pos, tr(TR_COL_DUR), dur);
    }
    ShowDetailField(buf, sizeof(buf)/sizeof(buf[0]), &pos,
                    e->localFile ? tr(TR_F_LOCALFILE) : tr(TR_F_LINK), e->url);
    SetWindowTextW(g_editDesc, buf);
}

/* “已缓存”虚拟行被选中：扫描缓存目录，换成 播客/标题/时长 列 */
static void SelectCachedView(void)
{
    static wchar_t buf[256];
    g_viewCached = 1;
    g_curPod = g_podCount;
    g_curEp = -1;               /* 进入缓存视图时重置剧集选中 */
    FreeCachePodcast();
    LoadCachePodcast();
    SetEpisodeColumns(1);
    FillEpisodeList();
    swprintf(buf, 256, tr(TR_CACHE_VIEW_DESC), g_cachePod.epCount);
    SetWindowTextW(g_editDesc, buf);
}

static void SelectPodcast(int idx)
{
    if (idx == g_podCount) { SelectCachedView(); return; }
    if (idx < 0 || idx >= g_podCount) return;
    if (g_viewCached) {
        /* 从缓存视图切回普通播客：恢复列头并释放扫描数据 */
        g_viewCached = 0;
        FreeCachePodcast();
        SetEpisodeColumns(0);
        g_curPod = -1;          /* 防止旧缓存下标越界 */
        g_curEp = -1;
    }
    g_curPod = idx;
    if (g_pods[idx].loadFailed) {
        /* 加载失败占位条目：清空剧集列表，详情区只显示订阅地址，避免访问空字段 */
        wchar_t *feedW;
        SendMessageW(g_listEp, LVM_DELETEALLITEMS, 0, 0);
        SetWindowTextW(g_editDesc, L"");
        feedW = U8ToW(g_pods[idx].feedUrl);
        if (feedW) {
            wchar_t buf[1024];
            size_t pos = 0;
            ShowDetailField(buf, sizeof(buf)/sizeof(buf[0]), &pos, tr(TR_F_FEEDURL), feedW);
            SetWindowTextW(g_editDesc, buf);
            free(feedW);
        }
        SyncScrollBar(g_vsEp);
        SyncScrollBar(g_hsEp);
        SyncDescScrollBar();
        return;
    }
    SortPodcast(&g_pods[idx]);
    FillEpisodeList();
    ShowPodcastDetail(idx);
    SyncScrollBar(g_vsEp);
    SyncScrollBar(g_hsEp);
    SyncDescScrollBar();
}

static void SelectEpisode(int idx)
{
    Podcast *p = ActivePodcast();
    if (!p || idx < 0 || idx >= p->epCount) return;
    g_curEp = idx;
    ShowEpisodeDetail(g_curPod, idx);
    SyncDescScrollBar();
}

/* 点击列头：同列切换方向，换列用默认方向（日期/时长降序，标题升序） */
static void ChangeSort(int col)
{
    int oldOrig = -1, i, sel;
    Podcast *p;
    int sortCol;

    if (g_viewCached) {
        /* 缓存视图列：0=播客 1=标题 2=时长 */
        if (col == 0) sortCol = SORT_PODCAST;
        else if (col == 1) sortCol = SORT_TITLE;
        else if (col == 2) sortCol = SORT_DUR;
        else return;
        col = sortCol;
    }
    if (g_sortCol == col) g_sortDir = -g_sortDir;
    else {
        g_sortCol = col;
        g_sortDir = (col == SORT_TITLE || col == SORT_PODCAST) ? 1 : -1;
    }
    /* 排序只更新内存，退出时统一保存 */

    if (g_curPod < 0) return;
    p = ActivePodcast();
    if (!p) return;
    sel = (int)SendMessageW(g_listEp, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_SELECTED);
    if (sel >= 0 && sel < p->epCount) oldOrig = p->eps[sel].origIdx;

    SortPodcast(p);
    FillEpisodeList();

    for (i = 0; i < p->epCount; i++) {
        if (p->eps[i].origIdx == oldOrig) {
            SendMessageW(g_listEp, LVM_ENSUREVISIBLE, i, FALSE);
            SendMessageW(g_listEp, LVM_SETITEMSTATE, i,
                (LPARAM)&(LVITEMW){ .state = LVIS_SELECTED|LVIS_FOCUSED,
                                    .stateMask = LVIS_SELECTED|LVIS_FOCUSED });
            break;
        }
    }
}

/* 剧集列表里把第 ep 行设为唯一选中并滚到可见处 */
static void HighlightEpisodeRow(int ep)
{
    LVITEMW lvi;
    ZeroMemory(&lvi, sizeof(lvi));
    lvi.stateMask = LVIS_SELECTED | LVIS_FOCUSED;
    lvi.state = 0;
    SendMessageW(g_listEp, LVM_SETITEMSTATE, (WPARAM)-1, (LPARAM)&lvi);
    lvi.state = LVIS_SELECTED | LVIS_FOCUSED;
    SendMessageW(g_listEp, LVM_SETITEMSTATE, (WPARAM)ep, (LPARAM)&lvi);
    SendMessageW(g_listEp, LVM_ENSUREVISIBLE, (WPARAM)ep, FALSE);
    InvalidateRect(g_listEp, NULL, TRUE);
}

/* 离线缓存视图：直接播放本地文件，不联网、不起下载线程 */
static void PlayLocalEpisode(int ep)
{
    Episode *e;
    wchar_t info[128], st[512];
    long long pd = 0;
    if (ep < 0 || ep >= g_cachePod.epCount) return;
    e = &g_cachePod.eps[ep];

    if (g_downloading) AbortDownloadConnection();
    PlayerStop();

    g_playPod = g_podCount;
    g_playEp  = ep;
    free(g_playUrl);
    g_playUrl = _wcsdup(e->url);

    /* 只有正在浏览“已缓存”时才同步列表选中/当前下标，避免后台自动连播抢走视图 */
    if (g_viewCached) {
        g_curPod = g_podCount;
        g_curEp = ep;
        HighlightEpisodeRow(ep);
    }

    ShowEpisodeDetail(g_podCount, ep);
    ProbeAudio(e->url, info, 128, &pd);
    g_dur100ns = pd;
    SetWindowTextW(g_stFormat, info);
    swprintf(st, 512, tr(TR_PLAYING_OFFLINE_FMT), e->title ? e->title : L"");
    SetStatus(st);
    PlayerPlayFile(e->url, e->title);
}

static void RequestPlayEpisode(int pod, int ep)
{
    DlJob *job;
    Episode *e;
    long myGen;
    if (pod == g_podCount) { PlayLocalEpisode(ep); return; }
    if (pod < 0 || pod >= g_podCount || ep < 0 || ep >= g_pods[pod].epCount) return;
    e = &g_pods[pod].eps[ep];

    /* 改主意了：立即中止旧下载、停止当前播放，转入新任务 */
    if (g_downloading) AbortDownloadConnection();
    PlayerStop();
    g_playPod = pod;
    g_playEp  = ep;
    free(g_playUrl);
    g_playUrl = _wcsdup(e->url);
    /* 播放的正是当前浏览的播客：同步选中行；后台连播到别的播客时不动视图 */
    if (!g_viewCached && pod == g_curPod) {
        g_curEp = ep;
        HighlightEpisodeRow(ep);   /* 播放目标确定，立即标绿 */
    }
    ShowEpisodeDetail(pod, ep);
    SetStatus(tr(TR_PREPARING));

    job = (DlJob*)calloc(1, sizeof(DlJob));
    if (!job) return;
    job->pod = pod;
    job->url = _wcsdup(e->url);
    if (!job->url) { free(job); return; }
    myGen = InterlockedIncrement(&g_dlGen);
    job->gen = myGen;
    g_curDlJob = job;
    g_downloading = 1;
    {
        HANDLE th = CreateThread(NULL, 0, EpisodeDlThread, job, 0, NULL);
        if (!th) {
            g_curDlJob = NULL;
            g_downloading = 0;
            free(job->url); free(job);
            /* 先纠正 base（前一条 SetStatus(TR_PREPARING) 已写入），
             * 否则 SetStatusTemp 到期会恢复到陈旧的“准备音频” */
            SetStatus(tr(TR_READY));
            SetStatusTemp(tr(TR_DL_THREAD_FAIL));
        } else {
            CloseHandle(th);   /* 线程句柄无需保留：不等待不查询 */
        }
    }
}

static void PauseResume(void)
{
    if (!g_player) {
        if (g_curPod >= 0 && g_curEp >= 0) RequestPlayEpisode(g_curPod, g_curEp);
        return;
    }
    if (g_playState == 1) {
        g_player->lpVtbl->Pause(g_player);
    } else if (g_playState == 2) {
        g_player->lpVtbl->Play(g_player);
    } else if (g_curPod >= 0 && g_curEp >= 0) {
        RequestPlayEpisode(g_curPod, g_curEp);
    }
    UpdateThumbbarPlayPause();
}

/* 上一曲/下一曲：按正在播放的剧集顺序，越界首尾循环；随机模式下取随机项。
 * 没有正在播放的内容时退化为当前浏览的选中行。 */
static void PlayAdjacent(int delta)
{
    int pod = (g_playPod >= 0) ? g_playPod : g_curPod;
    int ep  = (g_playPod >= 0) ? g_playEp  : g_curEp;
    int n, target;
    if (pod < 0) return;
    n = (pod == g_podCount) ? g_cachePod.epCount : g_pods[pod].epCount;
    if (n <= 0) return;
    if (g_playMode == 1 && n > 1) {
        do { target = rand() % n; } while (target == ep);
    } else {
        target = (ep < 0) ? 0 : ep + delta;
        if (target < 0) target = n - 1;
        if (target >= n) target = 0;
    }
    RequestPlayEpisode(pod, target);
}

/* 双击播客：优先续播“上次听到哪”的橙色剧集，无记录则从第 1 集开始。
 * 与剧集列表双击同一约定：先置 g_resumeSeek100ns，再由 PLAY 事件一次性定位。 */
static void PlayPodcastDefault(int pod)
{
    int target = 0, i;
    Podcast *p;
    LastEp *le;
    if (pod < 0 || pod > g_podCount) return;
    if (pod == g_podCount) {            /* 缓存聚合视图：无“上次听到”概念 */
        if (g_cachePod.epCount <= 0) return;
        g_resumeSeek100ns = 0;
        RequestPlayEpisode(pod, 0);
        return;
    }
    p = &g_pods[pod];
    if (p->epCount <= 0) return;        /* 加载失败/无剧集 */
    le = p->feedUrl ? LastEpFind(LastEpHash(p->feedUrl)) : NULL;
    g_resumeSeek100ns = 0;
    if (le && le->sec > 0 && le->epUrl) {
        for (i = 0; i < p->epCount; i++) {
            if (p->eps[i].url && wcscmp(p->eps[i].url, le->epUrl) == 0) {
                target = i;
                g_resumeSeek100ns = le->sec * 10000000LL;
                break;
            }
        }
        /* 记录里的那集已不在当前列表（订阅源下架等）→ 落回第 1 集 */
    }
    RequestPlayEpisode(pod, target);
}

/* 启动续播：feeds 加载完成后按 g_resumeUrl 定位剧集恢复播放。
 * 退出时是暂停（g_resumePaused）则在 PLAY 事件里定位后立即暂停回暂停态。
 * 匹配依据是剧集 enclosure URL（离线视图里是本地 mp3 路径），排序/增删不影响。
 * 找到后置 g_resumeSeek100ns，由 MFP_EVENT_TYPE_PLAY 一次性应用定位。 */
static void TryResumePlayback(void)
{
    int i, j, pod = -1, ep = -1;
    if (!g_resumeUrl || !g_resumeUrl[0]) return;
    /* 先查“已缓存”聚合视图，再查普通订阅；pod=g_podCount 表示缓存视图 */
    for (j = 0; j < g_cachePod.epCount && pod < 0; j++)
        if (g_cachePod.eps[j].url && wcscmp(g_cachePod.eps[j].url, g_resumeUrl) == 0) {
            pod = g_podCount;
            ep = j;
        }
    for (i = 0; i < g_podCount && pod < 0; i++)
        for (j = 0; j < g_pods[i].epCount; j++)
            if (g_pods[i].eps[j].url && wcscmp(g_pods[i].eps[j].url, g_resumeUrl) == 0) {
                pod = i;
                ep = j;
                break;
            }
    if (pod >= 0) {
        g_resumeSeek100ns = g_resumeSec * 10000000LL;
        SendMessageW(g_listPod, LB_SETCURSEL, pod, 0);
        SelectPodcast(pod);
        RequestPlayEpisode(pod, ep);
    }
    free(g_resumeUrl); g_resumeUrl = NULL;
    g_resumeSec = 0;
}

/* 喇叭按钮：静音切换（滑块位置不动） */
static void ToggleMute(void)
{
    g_muted = !g_muted;
    if (g_player) g_player->lpVtbl->SetMute(g_player, g_muted ? TRUE : FALSE);
    InvalidateRect(g_btnMute, NULL, TRUE);
}

/* 设定音量（0..100）：滑块、数字、播放器与静音状态一起同步，
 * 音量滑块拖动、手机遥控、启动恢复共用这一条路径
 * （手动调音量时自动解除静音） */
static void SetVolumeValue(int v)
{
    wchar_t buf[16];
    if (v < 0) v = 0;
    if (v > 100) v = 100;
    g_volume = v;
    if (g_sldVol) SendMessageW(g_sldVol, TBM_SETPOS, TRUE, v);
    swprintf(buf, 16, L"%d", v);
    if (g_stVol) SetWindowTextW(g_stVol, buf);
    if (g_player) g_player->lpVtbl->SetVolume(g_player, v / 100.0f);
    if (g_muted) {
        g_muted = 0;
        if (g_player) g_player->lpVtbl->SetMute(g_player, FALSE);
        InvalidateRect(g_btnMute, NULL, TRUE);
    }
}

/* 释放单个播客的全部堆内存与图片（字段允许为 NULL） */
static void FreePodcast(Podcast *p)
{
    int j;
    for (j = 0; j < p->epCount; j++) {
        free(p->eps[j].title);
        free(p->eps[j].desc);
        free(p->eps[j].url);
        free(p->eps[j].author);
    }
    free(p->eps);
    free(p->title);
    free(p->desc);
    free(p->author);
    free(p->lang);
    free(p->link);
    free(p->copyright);
    free(p->imageUrl);
    free(p->feedUrl);
    if (p->hImage) DeleteObject(p->hImage);
}

static void FreePodcasts(void)
{
    int i;
    for (i = 0; i < g_podCount; i++) FreePodcast(&g_pods[i]);
    g_podCount = 0;
    g_curPod = g_curEp = -1;
}

/* netFirst=0 启动加载（缓存优先、秒开）；=1 手动刷新（强制联网取最新） */
static void ReloadFeeds(int netFirst)
{
    if (g_feedsLoading) return;
    if (g_downloading) CancelCurrentDownload();   /* 刷新时中止正在进行的下载 */
    PlayerStop();
    /* 退出可能正处于的离线“已缓存”视图，列头恢复成 标题/日期/时长 */
    if (g_viewCached) {
        g_viewCached = 0;
        SetEpisodeColumns(0);
    }
    FreeCachePodcast();
    g_cacheRowHere = 0;
    g_cacheFileCount = 0;
    FreePodcasts();
    SendMessageW(g_listPod, LB_RESETCONTENT, 0, 0);
    SendMessageW(g_listEp, LVM_DELETEALLITEMS, 0, 0);
    SetWindowTextW(g_editDesc, L"");
    g_podMode = 0;
    SetStatus(tr(TR_LOADING_FEEDS));
    /* 刷新只用内存里的订阅链接；feeds.ini 仅启动读一次、退出写一次，
     * 因此运行中的删除/排序不会被刷新覆盖。
     * 给加载线程一份 URL 快照：加载期间增删订阅不会踩内存 */
    g_feedsLoading = 1;
    g_fullReload = 1;
    {
        FeedLoadArg *a = (FeedLoadArg*)calloc(1, sizeof(FeedLoadArg));
        HANDLE th = NULL;
        int i, ok = (a != NULL);
        if (ok) a->netFirst = netFirst;
        if (ok && g_feedUrlCount > 0) {
            a->urls = (char**)calloc(g_feedUrlCount, sizeof(char*));
            a->count = g_feedUrlCount;
            if (!a->urls) ok = 0;
            for (i = 0; ok && i < a->count; i++) {
                a->urls[i] = _strdup(g_feedUrls[i]);
                if (!a->urls[i]) ok = 0;
            }
        }
        if (ok)
            th = CreateThread(NULL, 0, FeedLoaderThread, a, 0, NULL);
        if (!th) {          /* 参数构造或线程创建失败：复位状态，不卡死刷新 */
            FreeFeedLoadArg(a);
            g_feedsLoading = 0;
            SetStatus(tr(TR_READY));
            return;
        }
        CloseHandle(th);    /* 线程句柄无需保留：不等待不查询 */
    }
}

/* ================= 播客订阅管理（增删/排序） ================= */

static char* WToU8(const wchar_t *w)
{
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
    char *p;
    if (n <= 0) return NULL;
    p = (char*)malloc(n);
    if (p) WideCharToMultiByte(CP_UTF8, 0, w, -1, p, n, NULL, NULL);
    return p;
}

/* 删除一个播客：同时从 g_pods、列表、g_feedUrls 三处移除 */
static void RemovePodcast(int i)
{
    int ui;
    int oldCount = g_podCount;   /* 哨兵 g_playPod/g_curPod 可能等于它（离线播放/缓存视图） */
    if (i < 0 || i >= g_podCount) return;

    if (g_playPod == i) {
        if (g_downloading) CancelCurrentDownload();
        PlayerStop();
        g_playPod = -1;
    } else {
        if (g_downloading && g_curDlJob && g_curDlJob->pod == i)
            CancelCurrentDownload();
        if (g_playPod > i && g_playPod < oldCount) g_playPod--;
        /* g_playPod == oldCount（离线播放中）：哨兵随 g_podCount 自然前移，不动 */
    }

    /* 失败的订阅没有对应 pod，按 feedUrl 精确匹配 URL */
    if (g_pods[i].feedUrl) {
        for (ui = 0; ui < g_feedUrlCount; ui++) {
            if (strcmp(g_feedUrls[ui], g_pods[i].feedUrl) == 0) {
                free(g_feedUrls[ui]);
                memmove(&g_feedUrls[ui], &g_feedUrls[ui + 1],
                        (g_feedUrlCount - ui - 1) * sizeof(char*));
                g_feedUrlCount--;
                break;
            }
        }
    }

    if (g_pods[i].feedUrl) {
        /* 顺带清掉该订阅的 RSS/封面缓存（已缓存音频 mp3 保留） */
        wchar_t cp[MAX_PATH];
        UrlCachePath(g_pods[i].feedUrl, L"feed_", L"xml", cp, MAX_PATH);
        DeleteFileW(cp);
        if (g_pods[i].imageUrl) {
            UrlCachePath(g_pods[i].imageUrl, L"cover_", L"img", cp, MAX_PATH);
            DeleteFileW(cp);
        }
        LastEpDrop(LastEpHash(g_pods[i].feedUrl));
    }
    FreePodcast(&g_pods[i]);
    memmove(&g_pods[i], &g_pods[i + 1], (g_podCount - i - 1) * sizeof(Podcast));
    g_podCount--;
    SendMessageW(g_listPod, LB_DELETESTRING, (WPARAM)i, 0);

    if (g_curPod == i) {
        g_curPod = -1; g_curEp = -1;
        SendMessageW(g_listEp, LVM_DELETEALLITEMS, 0, 0);
        SetWindowTextW(g_editDesc, L"");
    } else if (g_curPod > i) {
        g_curPod--;
    }

    if (g_viewCached) {
        /* 正在离线视图里删真实播客：缓存聚合不受影响，留在“已缓存”行 */
        g_curPod = g_podCount;
        SendMessageW(g_listPod, LB_SETCURSEL, g_podCount, 0);
    } else if (g_podCount > 0) {
        int sel = (i >= g_podCount) ? g_podCount - 1 : i;
        SendMessageW(g_listPod, LB_SETCURSEL, (WPARAM)sel, 0);
        SelectPodcast(sel);
    }
    InvalidateRect(g_listPod, NULL, TRUE);
}

/* ================= 单播客刷新队列 =================
 * 连续右键刷新多个播客时串行执行：入队 + FEEDS_DONE 接续，
 * 避免并发加载线程与主线程状态竞争。 */
static char *g_refreshQ[32];
static int   g_refreshQN = 0;

static void PumpRefreshQueue(void)
{
    char *url;
    wchar_t cp[MAX_PATH];
    if (g_refreshQN == 0 || g_feedsLoading) return;
    url = g_refreshQ[0];
    memmove(&g_refreshQ[0], &g_refreshQ[1], (g_refreshQN - 1) * sizeof(char*));
    g_refreshQN--;
    /* 清掉 RSS 缓存，强制联网取最新 */
    UrlCachePath(url, L"feed_", L"xml", cp, MAX_PATH);
    DeleteFileW(cp);
    AddSingleFeedEx(url, 1);   /* 原位替换，不改变订阅顺序 */
    free(url);
}

/* 右键"刷新播客"：入队，串行原位刷新 */
static void RefreshSinglePodcast(int i)
{
    char *url;
    int q;
    if (i < 0 || i >= g_podCount || !g_pods[i].feedUrl) return;
    if (g_refreshQN >= 32) return;
    for (q = 0; q < g_refreshQN; q++)   /* 已在队列：不重复排 */
        if (strcmp(g_refreshQ[q], g_pods[i].feedUrl) == 0) {
            SetStatusTemp(tr(TR_REFRESH_QUEUED));
            return;
        }
    url = _strdup(g_pods[i].feedUrl);
    if (!url) return;
    g_refreshQ[g_refreshQN++] = url;
    if (g_refreshQN == 1) PumpRefreshQueue();
    else SetStatusTemp(tr(TR_REFRESH_QUEUED));
}

/* 删除某播客全部已缓存的音频文件 */
static void DeletePodcastCache(int i)
{
    int j;
    if (i < 0 || i >= g_podCount) return;
    for (j = 0; j < g_pods[i].epCount; j++) {
        Episode *e = &g_pods[i].eps[j];
        if (e && e->url) {
            wchar_t path[MAX_PATH];
            CachePathFor(e->url, path, MAX_PATH);
            DeleteFileW(path);
        }
    }
    RefreshCachedEntry();
    InvalidateRect(g_listEp, NULL, TRUE);
}

/* 删除单集缓存文件。pod==g_podCount 表示离线视图：url 即本地文件路径 */
static void DeleteEpisodeCache(int pod, int ep)
{
    Episode *e;
    if (pod < 0 || ep < 0) return;
    if (pod == g_podCount) {
        if (ep >= g_cachePod.epCount) return;
        e = &g_cachePod.eps[ep];
    } else {
        if (pod >= g_podCount || ep >= g_pods[pod].epCount) return;
        e = &g_pods[pod].eps[ep];
    }
    if (e && e->url) {
        if (g_playPod == pod && g_playEp == ep) PlayerStop();   /* 正在播这集：先停 */
        if (pod == g_podCount) {
            DeleteFileW(e->url);
        } else {
            wchar_t path[MAX_PATH];
            CachePathFor(e->url, path, MAX_PATH);
            DeleteFileW(path);
        }
    }
    RefreshCachedEntry();
    if (g_viewCached) SelectPodcast(g_podCount);   /* 重建离线列表 */
    else InvalidateRect(g_listEp, NULL, TRUE);
}

/* 用资源管理器打开缓存目录 */
static void OpenCacheFolder(void)
{
    wchar_t dir[MAX_PATH];
    CacheDir(dir, MAX_PATH);
    EnsureCacheDir();
    ShellExecuteW(NULL, L"open", dir, NULL, NULL, SW_SHOWNORMAL);
}

/* 右键"已缓存"行 → 清空全部缓存 mp3 */
static void DeleteAllCacheFiles(void)
{
    wchar_t cdir[MAX_PATH], pat[MAX_PATH];
    WIN32_FIND_DATAW fd;
    HANDLE h;
    CacheDir(cdir, MAX_PATH);
    swprintf(pat, MAX_PATH, L"%ls*.mp3", cdir);
    h = FindFirstFileW(pat, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            wchar_t full[MAX_PATH];
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            swprintf(full, MAX_PATH, L"%ls%ls", cdir, fd.cFileName);
            DeleteFileW(full);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    if (g_playPod == g_podCount) PlayerStop();   /* 正在离线播放：停掉 */
    RefreshCachedEntry();                        /* 缓存清空会自动撤掉虚拟行 */
    if (g_viewCached) {
        if (g_cacheRowHere) {
            SelectPodcast(g_podCount);           /* 重建离线列表 */
        } else {
            g_viewCached = 0;
            SetEpisodeColumns(0);
            if (g_podCount > 0) {
                SendMessageW(g_listPod, LB_SETCURSEL, 0, 0);
                SelectPodcast(0);
            } else {
                g_curPod = -1; g_curEp = -1;
                SendMessageW(g_listEp, LVM_DELETEALLITEMS, 0, 0);
                SetWindowTextW(g_editDesc, L"");
            }
        }
    } else {
        InvalidateRect(g_listPod, NULL, TRUE);
    }
}

/* 下载单集到缓存：选中该行后走批量缓存队列（StartBatchCache 读列表选中） */
static void DownloadSingleEpisode(int pod, int ep)
{
    LVITEMW lvi;
    if (pod < 0 || pod != g_curPod || pod >= g_podCount) return;
    if (ep < 0 || ep >= g_pods[pod].epCount) return;
    if (!g_pods[pod].eps[ep].url) return;
    if (EpisodeIsCached(&g_pods[pod].eps[ep])) return;
    ZeroMemory(&lvi, sizeof(lvi));
    lvi.stateMask = LVIS_SELECTED;
    lvi.state = 0;
    SendMessageW(g_listEp, LVM_SETITEMSTATE, (WPARAM)-1, (LPARAM)&lvi);
    lvi.state = LVIS_SELECTED;
    SendMessageW(g_listEp, LVM_SETITEMSTATE, (WPARAM)ep, (LPARAM)&lvi);
    StartBatchCache();
}

/* 把播客从位置 i 搬到位置 j（任意距离）。
 * feedUrls 一并重排：按 pods 顺序重建，没挂到任何播客的 URL
 * （加载失败的订阅）保持相对顺序追加在末尾——比旧的相邻交换
 * 可靠，交换在有失败订阅时会让 pods 与 feedUrls 顺序错位 */
static void MovePodcastTo(int i, int j)
{
    wchar_t *title;
    Podcast tmp;
    int k;
    if (i < 0 || i >= g_podCount || j < 0 || j >= g_podCount || i == j) return;

    tmp = g_pods[i];
    if (j > i) memmove(&g_pods[i], &g_pods[i + 1], (j - i) * sizeof(Podcast));
    else       memmove(&g_pods[j + 1], &g_pods[j], (i - j) * sizeof(Podcast));
    g_pods[j] = tmp;

    /* g_feedUrls 按 pods 顺序重建（SaveConfig 写 [feeds] 用它） */
    if (g_feedUrlCount > 0) {
        char **nu = (char**)calloc(g_feedUrlCount, sizeof(char*));
        int n = 0, ui;
        if (nu) {
            for (k = 0; k < g_podCount; k++) {
                if (!g_pods[k].feedUrl) continue;
                for (ui = 0; ui < g_feedUrlCount; ui++) {
                    if (g_feedUrls[ui] &&
                        strcmp(g_feedUrls[ui], g_pods[k].feedUrl) == 0) {
                        nu[n++] = g_feedUrls[ui];
                        g_feedUrls[ui] = NULL;
                        break;
                    }
                }
            }
            for (ui = 0; ui < g_feedUrlCount; ui++)
                if (g_feedUrls[ui]) nu[n++] = g_feedUrls[ui];
            free(g_feedUrls);
            g_feedUrls = nu;
            g_feedUrlCount = n;
        }
    }

    title = g_pods[j].title;   /* 搬家后目标位置的标题（失败占位为 NULL，owner-draw 自绘） */
    SendMessageW(g_listPod, LB_DELETESTRING, (WPARAM)i, 0);
    SendMessageW(g_listPod, LB_INSERTSTRING, (WPARAM)j,
        (LPARAM)(title ? title : tr(TR_FEED_FAIL_TITLE)));
    SendMessageW(g_listPod, LB_SETCURSEL, (WPARAM)j, 0);

    /* 选中/正在播放的播客下标跟着搬 */
    if (g_curPod == i) g_curPod = j;
    else if (i < j && g_curPod > i && g_curPod <= j) g_curPod--;
    else if (j < i && g_curPod >= j && g_curPod < i) g_curPod++;
    if (g_playPod == i) g_playPod = j;
    else if (i < j && g_playPod > i && g_playPod <= j) g_playPod--;
    else if (j < i && g_playPod >= j && g_playPod < i) g_playPod++;
    InvalidateRect(g_listPod, NULL, TRUE);
}


/* ================= 添加订阅输入框（自建模态弹窗） ================= */
#define ID_IB_EDIT   9001
#define ID_IB_OK     9002
#define ID_IB_CANCEL 9003
#define ID_IB_OPML   9004
#define ID_IB_SEARCH 9005
#define ID_IB_LIST   9006
#define ID_IB_SB     9007   /* ListView 纵向自绘滚动条 */
#define ID_IB_SBH    9008   /* ListView 水平自绘滚动条 */

static HWND g_ibSb;
static HWND g_ibSbH;
static HWND g_ibList = NULL;   /* 搜索结果 ListView，供 PanelProc 识别（同 g_listEp 一套处理） */
static HWND g_ibTip  = NULL;   /* 搜索结果悬停提示 */
static int  g_ibBorderDeltaX;   /* 窗口外宽 - 客户区宽 */
static int  g_ibBorderDeltaY;   /* 窗口外高 - 客户区高 */
static WNDPROC g_ibOrigHdrProc = NULL;

static wchar_t g_ibBuf[2048];
static int     g_ibResult = 0;
static int     g_ibMode = 0;   /* 0=订阅 URL，1=定时关机分钟数 */

/* iTunes 搜索结果（线程结束后 Post 给 UI，UI 负责 free） */
#define MAX_SEARCH_RES 30
typedef struct {
    int count;
    char *title[MAX_SEARCH_RES];     /* UTF-8 */
    char *author[MAX_SEARCH_RES];    /* UTF-8 */
    char *feedUrl[MAX_SEARCH_RES];   /* UTF-8 */
} SearchResult;

/* 前置声明（实现在导入导出段） */
static BOOL FileDlg(int save, const wchar_t *title, const wchar_t *filter,
                    const wchar_t *defExt, wchar_t *path, int pathMax);
static char *ReadFileAll(const wchar_t *path, DWORD *lenOut);
static int ImportOpmlText(const char *buf);
static int FeedUrlDup(const char *url);

/* 添加订阅对话框内直接选 OPML 导入（与设置→导入同一套解析） */
static void DoOpmlPickImport(HWND h)
{
    static const wchar_t *FILTER =
        L"OPML files (*.opml)\0*.opml\0All Files (*.*)\0*.*\0";
    wchar_t path[MAX_PATH];
    DWORD len;
    char *buf;
    int added;

    if (!FileDlg(0, tr(TR_IMP_TITLE), FILTER, NULL, path, MAX_PATH)) return;
    buf = ReadFileAll(path, &len);
    if (!buf) {
        MessageBoxTh(h, tr(TR_IMP_FAIL), L"LiteTune", MB_OK | MB_ICONERROR);
        return;
    }
    added = ImportOpmlText(buf);
    free(buf);
    if (added > 0) {
        wchar_t msg[96];
        swprintf(msg, 96, tr(TR_IMP_OPML_OK_FMT), added);
        MessageBoxTh(h, msg, tr(TR_IMP_TITLE), MB_OK | MB_ICONINFORMATION);
        ReloadFeeds(0);   /* 新订阅无缓存联网拉取，老订阅走缓存秒开 */
        g_ibResult = 0;
        DestroyWindow(h); /* 导入完成，关闭添加对话框（URL 留空即不添加） */
    } else {
        MessageBoxTh(h, tr(TR_IMP_OPML_NONE), tr(TR_IMP_TITLE),
                     MB_OK | MB_ICONINFORMATION);
    }
}

/* ================= iTunes 播客搜索 ================= */

/* URL 编码：只保留 ASCII 字母数字和 -_.~，其他转 %XX；空格 → + */
static char *UrlEncode(const wchar_t *in)
{
    char *utf8 = WToU8(in);
    char *out, *p, *r;
    int n, sz;
    if (!utf8) return NULL;
    sz = 0;
    for (p = utf8; *p; p++) sz += ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') ||
                                    (*p >= '0' && *p <= '9') || *p == '-' || *p == '_' ||
                                    *p == '.' || *p == '~') ? 1 : 3;
    out = (char *)malloc(sz + 1);
    if (!out) { free(utf8); return NULL; }
    r = out;
    for (p = utf8; *p; p++) {
        if (*p == ' ') { *r++ = '+'; continue; }
        if ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') ||
            (*p >= '0' && *p <= '9') || *p == '-' || *p == '_' || *p == '.' || *p == '~') {
            *r++ = *p;
        } else {
            n = (unsigned char)*p;
            *r++ = '%';
            *r++ = "0123456789ABCDEF"[n >> 4];
            *r++ = "0123456789ABCDEF"[n & 15];
        }
    }
    *r = 0;
    free(utf8);
    return out;
}

/* 从 JSON 字符串中提取指定 key 的字符串值。
 * 只处理最朴素的 "key":"value" 形式，遇到转义反斜杠会正确处理；
 * 不支持嵌套对象、数组、数字。找不到返回 NULL。 */
/* 在 JSON 对象 buf 里提取 key 对应的字符串值。
 * 返回 malloc 出来的独立副本（调用方负责 free），失败返回 NULL。 */
static char *JsonStrVal(const char *buf, const char *key)
{
    char pat[64];
    const char *p, *end;
    int esc;
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    p = strstr(buf, pat);
    if (!p) return NULL;
    p += strlen(pat);
    while (*p == ' ' || *p == ':' || *p == '\t') p++;
    if (*p != '"') return NULL;
    p++; /* 跳过起始引号，p 指向 value 首字符 */
    end = p;
    esc = 0;
    while (*end) {
        if (esc) { esc = 0; end++; continue; }
        if (*end == '\\') { esc = 1; end++; continue; }
        if (*end == '"') break;
        end++;
    }
    if (*end != '"') return NULL;
    /* [p, end) 是纯 value，拷贝成独立字符串
     * （MinGW 未声明 strndup，就地实现） */
    {
        size_t n = (size_t)(end - p);
        char *out = (char *)malloc(n + 1);
        if (!out) return NULL;
        memcpy(out, p, n);
        out[n] = 0;
        return out;
    }
}

/* iTunes 搜索后台线程：抓 JSON → 解析 → Post 给调用者 */
typedef struct { HWND hwnd; wchar_t kw[2048]; } SearchParam;

static DWORD WINAPI ItunesSearchThread(LPVOID param)
{
    SearchParam *sp = (SearchParam *)param;
    HWND dst = sp->hwnd;
    wchar_t url[2048];
    wchar_t *enc;
    char *json;
    DWORD len;
    SearchResult *res;
    char *p;

    enc = U8ToW(UrlEncode(sp->kw));
    free(sp);
    if (!enc) { PostMessageW(dst, WM_APP_SEARCH_DONE, 0, 0); return 1; }
    swprintf(url, 2048, L"https://itunes.apple.com/search?term=%ls&media=podcast&limit=20", enc);
    free(enc);

    json = HttpFetch(url, &len, 0, NULL, NULL);
    if (!json) { PostMessageW(dst, WM_APP_SEARCH_DONE, 0, 0); return 1; }

    res = (SearchResult *)calloc(1, sizeof(SearchResult));
    if (!res) { free(json); PostMessageW(dst, WM_APP_SEARCH_DONE, 0, 0); return 1; }

    /* 遍历 results 数组：按 { ... } 对象边界切，每个对象独立提取三个字段。
     * count 只在成功提取后才自增，不在 for 里无条件自增 */
    p = strstr(json, "\"results\"");
    if (p) p = strchr(p, '[');
    if (p) for (p++; *p && res->count < MAX_SEARCH_RES; ) {
        char *ob, *cb, save;
        int depth;
        char *title, *author, *url2;
        while (*p && *p != '{') p++;
        if (*p != '{') break;
        ob = p++;
        depth = 1;
        while (*p && depth > 0) {
            if (*p == '{') depth++;
            else if (*p == '}') depth--;
            if (depth > 0) p++;
        }
        cb = p;
        save = *cb;
        *cb = 0;
        title = JsonStrVal(ob, "collectionName");
        author = JsonStrVal(ob, "artistName");
        url2 = JsonStrVal(ob, "feedUrl");
        *cb = save;
        p++;
        if (title && url2) {
            res->title[res->count] = title;
            res->author[res->count] = author ? author : strdup("");
            res->feedUrl[res->count] = url2;
            res->count++;
        } else {
            /* 解析失败，释放 JsonStrVal 已分配的 */
            free(title); free(author); free(url2);
        }
    }
    free(json);
    PostMessageW(dst, WM_APP_SEARCH_DONE, 0, (LPARAM)res);
    return 0;
}

static void FreeSearchResult(SearchResult *r)
{
    int i;
    if (!r) return;
    for (i = 0; i < r->count; i++) {
        free(r->title[i]); free(r->author[i]); free(r->feedUrl[i]);
    }
    free(r);
}

/* 搜索 ListView 表头子类化：暗色主题 gap 填黑 + 通过 NM_CUSTOMDRAW 改文字颜色 */
static LRESULT CALLBACK IbHeaderProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    LRESULT lr = CallWindowProcW(g_ibOrigHdrProc, hwnd, msg, wp, lp);
    if (g_darkMode && msg == WM_NOTIFY) {
        NMHDR *nm = (NMHDR *)lp;
        if (nm->code == NM_CUSTOMDRAW) {
            NMCUSTOMDRAW *cd = (NMCUSTOMDRAW *)lp;
            if (cd->dwDrawStage == CDDS_PREPAINT) {
                return CDRF_NOTIFYITEMDRAW;
            } else if (cd->dwDrawStage == CDDS_ITEMPREPAINT) {
                SetTextColor(cd->hdc, ThText());
                SetBkColor(cd->hdc, RGB(46, 46, 52));
                return CDRF_NEWFONT;
            }
        }
    }
    if (g_darkMode && msg == WM_PAINT) {
        HDC hdc = GetDC(hwnd);
        RECT rc, last;
        int cols;
        GetClientRect(hwnd, &rc);
        cols = (int)SendMessageW(hwnd, HDM_GETITEMCOUNT, 0, 0);
        if (hdc && cols > 0 && SendMessageW(hwnd, HDM_GETITEMRECT, cols - 1, (LPARAM)&last)
            && last.right < rc.right) {
            RECT gap = { last.right, rc.top, rc.right, rc.bottom };
            HBRUSH b = CreateSolidBrush(RGB(46, 46, 52));
            HPEN pn = CreatePen(PS_SOLID, 1, RGB(28, 28, 32));
            HGDIOBJ ob1 = SelectObject(hdc, b);
            HGDIOBJ ob2 = SelectObject(hdc, pn);
            FillRect(hdc, &gap, b);
            MoveToEx(hdc, rc.left, rc.bottom - 1, NULL);
            LineTo(hdc, rc.right, rc.bottom - 1);
            SelectObject(hdc, ob1);
            SelectObject(hdc, ob2);
            DeleteObject(b);
            DeleteObject(pn);
        }
        if (hdc) ReleaseDC(hwnd, hdc);
    }
    return lr;
}

/* 前向声明 —— 定义在主窗口 PanelProc 附近 */
static void SubclassForFontWheel(HWND w);

static LRESULT CALLBACK InputBoxProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    LRESULT r;
    if (DarkDlgMsg(h, msg, wp, lp, &r)) return r;

    switch (msg) {
    case WM_SIZE: {
        /* 搜索后窗口尺寸变化，重排 ListView + 纵向 + 水平滚动条 */
        HWND lv = GetDlgItem(h, ID_IB_LIST);
        if (lv && IsWindowVisible(lv)) {
            RECT rc;
            GetClientRect(h, &rc);
            int x0 = 12;
            int y0 = 104;
            int w  = rc.right - rc.left - 24;   /* 客户区总宽减去左右各 12px */
            int hgt = rc.bottom - rc.top - 12 - y0;  /* ListView 可用高 */
            int sbw = DSB_W, sbh = DSB_W;
            MoveWindow(lv, x0, y0, w - sbw - 2, hgt - sbh - 2, TRUE);
            if (g_ibSb) MoveWindow(g_ibSb, x0 + w - sbw - 2, y0 + 2, sbw, hgt - sbh - 6, TRUE);
            if (g_ibSbH) MoveWindow(g_ibSbH, x0 + 2, y0 + hgt - sbh - 2, w - sbw - 6, sbh, TRUE);
            if (g_ibSb) SyncScrollBar(g_ibSb);
            if (g_ibSbH) SyncScrollBar(g_ibSbH);
            /* 重排后把按钮提到顶层，防止被 ListView 盖住 */
            {
                HWND b1 = GetDlgItem(h, ID_IB_OPML);
                HWND b2 = GetDlgItem(h, ID_IB_OK);
                HWND b3 = GetDlgItem(h, ID_IB_CANCEL);
                if (b1) SetWindowPos(b1, HWND_TOP, 0,0,0,0, SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
                if (b2) SetWindowPos(b2, HWND_TOP, 0,0,0,0, SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
                if (b3) SetWindowPos(b3, HWND_TOP, 0,0,0,0, SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
            }
        }
        return 0;
    }
    case WM_CREATE: {
        HWND e, lab;
        UINT btnStyle = WS_CHILD | WS_VISIBLE | WS_TABSTOP;
        /* 只用 WS_BORDER，不加 WS_EX_CLIENTEDGE：Win11 会给带 client edge
         * 的编辑框在聚焦时补画一条主题色下划线（亮色下就是输入框底部的
         * 蓝色横条），且与暗色模式的朴素 1px 边框不一致 */
        e = CreateWindowExW(0, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER |
            (g_ibMode == 1 ? ES_NUMBER : ES_AUTOHSCROLL),
            12, 36, 408, 24, h, (HMENU)(INT_PTR)ID_IB_EDIT, NULL, NULL);
        lab = CreateWindowExW(0, L"STATIC",
            g_ibMode == 1 ? tr(TR_CUSTOM_LABEL) : tr(TR_SEARCH_HINT),
            WS_CHILD | WS_VISIBLE | SS_LEFT, 12, 12, 408, 18, h, NULL, NULL, NULL);
        if (g_darkMode) btnStyle |= BS_OWNERDRAW;
        if (g_ibMode == 0) {
            /* 客户区 432px，margin 12px 两边 → 可用 408px
             * OPML 160 靠左，确定/取消靠右成对：OK 88 + gap 8 + Cancel 88 */
            CreateWindowExW(0, L"BUTTON", tr(TR_IB_OPML),
                btnStyle | BS_PUSHBUTTON,
                12, 70, 160, 24, h, (HMENU)(INT_PTR)ID_IB_OPML, NULL, NULL);
            CreateWindowExW(0, L"BUTTON", tr(TR_OK),
                btnStyle | BS_DEFPUSHBUTTON,
                236, 70, 88, 24, h, (HMENU)(INT_PTR)ID_IB_OK, NULL, NULL);
            CreateWindowExW(0, L"BUTTON", tr(TR_CANCEL),
                btnStyle | BS_PUSHBUTTON,
                332, 70, 88, 24, h, (HMENU)(INT_PTR)ID_IB_CANCEL, NULL, NULL);
            /* ListView：两列标题+作者；初始隐藏，仅在真正搜索后显示；
             * 原生滚动条由 PanelProc 的 WM_NCCALCSIZE 并回客户区，
             * 自绘 DSB 覆盖（与主界面 g_listEp 同一套） */
            {
                HWND lv, hdr;
                lv = CreateWindowExW(0, WC_LISTVIEWW, NULL,
                    WS_CHILD | LVS_REPORT | LVS_SHOWSELALWAYS,
                    12, 104, 408 - DSB_W - 2, 204, h,
                    (HMENU)(INT_PTR)ID_IB_LIST, NULL, NULL);
                g_ibList = lv;
                {
                    LVCOLUMNW col;
                    ZeroMemory(&col, sizeof(col));
                    col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM | LVCF_FMT;
                    col.fmt = LVCFMT_LEFT | (g_darkMode ? HDF_OWNERDRAW : 0);
                    col.cx = 260; col.iSubItem = 0;
                    col.pszText = (wchar_t *)tr(TR_COL_TITLE);
                    SendMessageW(lv, LVM_INSERTCOLUMNW, 0, (LPARAM)&col);
                    col.cx = 140; col.iSubItem = 1;
                    col.pszText = (wchar_t *)tr(TR_F_AUTHOR);
                    SendMessageW(lv, LVM_INSERTCOLUMNW, 1, (LPARAM)&col);
                }
                ListView_SetExtendedListViewStyle(lv, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
                /* 搜索结果列表也用应用字体，和标签/按钮/编辑框保持一致 */
                SendMessageW(lv, WM_SETFONT, (WPARAM)g_font, TRUE);
                /* 条目颜色 */
                SendMessageW(lv, LVM_SETBKCOLOR, 0, (LPARAM)ThPanel());
                SendMessageW(lv, LVM_SETTEXTBKCOLOR, 0, (LPARAM)ThPanel());
                SendMessageW(lv, LVM_SETTEXTCOLOR, 0, (LPARAM)ThText());
                /* 给 ListView 挂 PanelProc：WM_NCCALCSIZE 抑制原生滚动条，
                 * WM_VSCROLL/WM_MOUSEWHEEL 同步 DSB。主界面 g_listEp 同一套。 */
                SubclassForFontWheel(lv);
                /* 表头：暗色下用 HDF_OWNERDRAW + WM_DRAWITEM（DrawHeaderItem）；
                 * IbHeaderProc 仅负责最后一列右侧空白区的暗色填充。
                 * 用 HDM_SETITEMW 显式打 HDF_OWNERDRAW（与主列表 ApplyHeaderTheme 同口径，
                 * 经 LVCFMT 透传在部分 comctl32 版本上不可靠） */
                hdr = (HWND)SendMessageW(lv, LVM_GETHEADER, 0, 0);
                if (hdr) {
                    int ci;
                    for (ci = 0; ci < 2; ci++) {
                        HDITEMW hi;
                        ZeroMemory(&hi, sizeof(hi));
                        hi.mask = HDI_FORMAT;
                        if (SendMessageW(hdr, HDM_GETITEMW, ci, (LPARAM)&hi)) {
                            if (g_darkMode) hi.fmt |= HDF_OWNERDRAW;
                            else            hi.fmt &= ~HDF_OWNERDRAW;
                            SendMessageW(hdr, HDM_SETITEMW, ci, (LPARAM)&hi);
                        }
                    }
                    if (!g_ibOrigHdrProc)
                        g_ibOrigHdrProc = (WNDPROC)SetWindowLongPtrW(hdr, GWLP_WNDPROC, (LONG_PTR)IbHeaderProc);
                    SendMessageW(hdr, WM_SETFONT, (WPARAM)g_font, TRUE);
                }
                /* 自绘纵向滚动条，贴 ListView 右边；初始隐藏，搜索后由 SyncScrollBar 决定显隐 */
                g_ibSb = CreateDarkScrollBar(h, lv, 1, 0, ID_IB_SB);
                if (g_ibSb) {
                    MoveWindow(g_ibSb, 12 + 408 - DSB_W - 2, 104 + 2, DSB_W, 204 - 4, TRUE);
                    ShowWindow(g_ibSb, SW_HIDE);
                }
                /* 自绘水平滚动条，初始隐藏，搜索后 ShowWindow */
                g_ibSbH = CreateDarkScrollBar(h, lv, 1, 1, ID_IB_SBH);
                if (g_ibSbH) {
                    MoveWindow(g_ibSbH, 12 + 2, 104 + 204 - 12 - DSB_W, 408 - DSB_W - 4, DSB_W, TRUE);
                    ShowWindow(g_ibSbH, SW_HIDE);
                }
                /* 悬停提示：显示标题/作者/订阅地址 */
                {
                    TOOLINFOW ti;
                    g_ibTip = CreateWindowExW(0, TOOLTIPS_CLASSW, NULL,
                        WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
                        0, 0, 0, 0, h, NULL, GetModuleHandleW(NULL), NULL);
                    if (g_ibTip) {
                        SendMessageW(g_ibTip, TTM_SETMAXTIPWIDTH, 0, 400);
                        ThemeTipSubclass(g_ibTip);
                        ZeroMemory(&ti, sizeof(ti));
                        ti.cbSize = sizeof(ti);
                        ti.uFlags = TTF_TRACK;
                        ti.hwnd = lv;
                        ti.uId = (UINT_PTR)lv;
                        ti.lpszText = L"";
                        SendMessageW(g_ibTip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
                    }
                }
            }
            /* 创建顺序：按钮先→ListView后。把按钮 Z-order 提到 ListView 前面，
             * 否则 ListView 显示后会盖住按钮行 */
            {
                HWND b1 = GetDlgItem(h, ID_IB_OPML);
                HWND b2 = GetDlgItem(h, ID_IB_OK);
                HWND b3 = GetDlgItem(h, ID_IB_CANCEL);
                if (b1) SetWindowPos(b1, HWND_TOP, 0,0,0,0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
                if (b2) SetWindowPos(b2, HWND_TOP, 0,0,0,0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
                if (b3) SetWindowPos(b3, HWND_TOP, 0,0,0,0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            }
        } else {
            CreateWindowExW(0, L"BUTTON", tr(TR_OK),
                btnStyle | BS_DEFPUSHBUTTON,
                276, 74, 72, 26, h, (HMENU)(INT_PTR)ID_IB_OK, NULL, NULL);
            CreateWindowExW(0, L"BUTTON", tr(TR_CANCEL),
                btnStyle | BS_PUSHBUTTON,
                348, 74, 72, 26, h, (HMENU)(INT_PTR)ID_IB_CANCEL, NULL, NULL);
        }
        /* 弹窗里的按钮必须显式设字体：不设时亮色用系统主题按钮字体，
         * 暗色自绘回落到 DEFAULT_GUI_FONT（中文系统是宋体 9pt），
         * 两种模式字号、字形都不同，看着像两套 UI */
        {
            static const int kBtnIds[] = { ID_IB_OPML, ID_IB_OK, ID_IB_CANCEL };
            int bi;
            for (bi = 0; bi < 3; bi++) {
                HWND b = GetDlgItem(h, kBtnIds[bi]);
                if (b) SendMessageW(b, WM_SETFONT, (WPARAM)g_font, TRUE);
            }
        }
        SendMessageW(e, WM_SETFONT, (WPARAM)g_font, TRUE);
        SendMessageW(lab, WM_SETFONT, (WPARAM)g_font, TRUE);
        ApplyDarkTitlebar(h, g_darkMode);
        SetFocus(e);
        return 0;
    }
    case WM_CHAR:
        /* 自注册窗口类不是 DialogBox，DefaultButton 不会自动处理 VK_RETURN，手动转 */
        if (wp == VK_RETURN) {
            SendMessageW(h, WM_COMMAND, MAKEWPARAM(ID_IB_OK, BN_CLICKED), 0);
            return 0;
        }
        return DefWindowProcW(h, msg, wp, lp);
    case WM_COMMAND:
        if (HIWORD(wp) == BN_CLICKED && LOWORD(wp) == ID_IB_CANCEL) {
            g_ibResult = 0;
            DestroyWindow(h);
        } else if (HIWORD(wp) == BN_CLICKED && LOWORD(wp) == ID_IB_OPML) {
            DoOpmlPickImport(h);
        } else if (HIWORD(wp) == BN_CLICKED && LOWORD(wp) == ID_IB_OK) {
            wchar_t raw[2048];
            wchar_t *s, *e;
            GetDlgItemTextW(h, ID_IB_EDIT, raw, 2048);
            s = raw;
            while (*s == L' ' || *s == L'\t') s++;
            e = s + wcslen(s);
            while (e > s && (e[-1] == L' ' || e[-1] == L'\t' ||
                             e[-1] == L'\r' || e[-1] == L'\n')) *--e = 0;

            if (g_ibMode == 1) {
                /* 分钟数：1-600（ES_NUMBER 已挡掉非数字输入） */
                int mins = _wtoi(s);
                if (mins < 1 || mins > 600) {
                    MessageBoxTh(h, tr(TR_CUSTOM_BAD), L"LiteTune", MB_ICONWARNING);
                    return 0;
                }
                wcsncpy(g_ibBuf, s, 2047);
                g_ibBuf[2047] = 0;
                g_ibResult = 1;
                DestroyWindow(h);
                return 0;
            }

            if (!wcsstr(s, L"://")) {
                /* 无协议头：关键词 → iTunes 搜索（而不是报 URL 错） */
                HWND lb = GetDlgItem(h, ID_IB_LIST);
                SendMessageW(lb, LVM_DELETEALLITEMS, 0, 0);
                ShowWindow(lb, SW_SHOW);
                { RECT rc; GetWindowRect(h, &rc);
                  MoveWindow(h, rc.left, rc.top,
                             432 + g_ibBorderDeltaX, 356 + g_ibBorderDeltaY, TRUE); }
                /* 结果回来前列表为空，先让 SyncScrollBar 隐藏空轨道；
                 * WM_APP_SEARCH_DONE 里会根据实际条目数再决定显隐 */
                if (g_ibSb) SyncScrollBar(g_ibSb);
                if (g_ibSbH) SyncScrollBar(g_ibSbH);
                {
                    SearchParam *sp = (SearchParam *)malloc(sizeof(SearchParam));
                    if (sp) {
                        sp->hwnd = h;
                        wcsncpy(sp->kw, s, 2047);
                        sp->kw[2047] = 0;
                        CreateThread(NULL, 0, ItunesSearchThread, sp, 0, NULL);
                    }
                }
                return 0;
            }
            /* 有协议头：直接添加 RSS */
            {
                char *u8 = WToU8(s);
                if (u8) {
                    if (FeedUrlDup(u8)) {
                        free(u8);
                        MessageBoxTh(h, tr(TR_DUP_FEED), L"LiteTune", MB_ICONINFORMATION);
                        return 0;
                    }
                    free(u8);
                }
            }
            if (g_feedUrlCount >= MAX_PODCASTS) {
                MessageBoxTh(h, tr(TR_MAX_FEEDS), L"LiteTune", MB_ICONWARNING);
                return 0;
            }
            wcsncpy(g_ibBuf, s, 2047);
            g_ibBuf[2047] = 0;
            g_ibResult = 1;
            DestroyWindow(h);
        }
        return 0;
    case WM_APP_SEARCH_DONE: {
        HWND lb = GetDlgItem(h, ID_IB_LIST);
        SearchResult *res = (SearchResult *)lp;
        if (!lb) { FreeSearchResult(res); return 0; }
        SendMessageW(lb, WM_SETREDRAW, FALSE, 0);
        SendMessageW(lb, LVM_DELETEALLITEMS, 0, 0);
        if (!res) {
            /* 搜索失败，不往 ListView 填东西 */
        } else if (res->count == 0) {
            /* 搜索无结果 */
        } else {
            int i;
            for (i = 0; i < res->count; i++) {
                wchar_t *tw = U8ToW(res->title[i]);
                wchar_t *aw = U8ToW(res->author[i]);
                wchar_t *fu = U8ToW(res->feedUrl[i]);
                LVITEMW li;
                int idx;
                ZeroMemory(&li, sizeof(li));
                li.mask = LVIF_TEXT | LVIF_PARAM;
                li.pszText = tw;
                li.iItem = i;
                li.lParam = (LPARAM)fu;   /* lParam 直接存 feedUrl 宽串指针 */
                idx = (int)SendMessageW(lb, LVM_INSERTITEMW, 0, (LPARAM)&li);
                if (idx >= 0 && aw && aw[0]) {
                    li.mask = LVIF_TEXT;
                    li.iItem = idx;
                    li.iSubItem = 1;
                    li.pszText = aw;
                    SendMessageW(lb, LVM_SETITEMW, 0, (LPARAM)&li);
                }
                free(tw); free(aw);
                /* fu 不 free，由 ListView 持有，WM_DESTROY 统一清理 */
            }
        }
        SendMessageW(lb, WM_SETREDRAW, TRUE, 0);
        InvalidateRect(lb, NULL, TRUE);
        if (g_ibSb) SyncScrollBar(g_ibSb);
        if (g_ibSbH) SyncScrollBar(g_ibSbH);
        FreeSearchResult(res);
        return 0;
    }
    case WM_DRAWITEM: {
        /* 兜底：若表头的 WM_DRAWITEM 被 ListView 转发到对话框，也走自绘深色表头 */
        DRAWITEMSTRUCT *di = (DRAWITEMSTRUCT *)lp;
        if (g_darkMode && di->CtlType == ODT_HEADER &&
            GetParent(di->hwndItem) == GetDlgItem(h, ID_IB_LIST))
            return DrawHeaderItem(di) ? TRUE : FALSE;
        break;
    }
    case WM_NOTIFY: {
        NMHDR *nm = (NMHDR *)lp;
        if (nm->idFrom == ID_IB_LIST) {
            HWND lv = GetDlgItem(h, ID_IB_LIST);
            if (nm->code == LVN_ITEMCHANGED) {
                /* 选中搜索结果：把 feedUrl 自动填进输入框 */
                int sel = (int)SendMessageW(lv, LVM_GETNEXTITEM, -1, LVNI_SELECTED);
                if (sel >= 0) {
                    LVITEMW li;
                    ZeroMemory(&li, sizeof(li));
                    li.mask = LVIF_PARAM;
                    li.iItem = sel;
                    if (SendMessageW(lv, LVM_GETITEMW, 0, (LPARAM)&li)) {
                        wchar_t *url = (wchar_t *)li.lParam;
                        if (url && *url) SetDlgItemTextW(h, ID_IB_EDIT, url);
                    }
                }
                if (g_ibSb) SyncScrollBar(g_ibSb);
            } else if (nm->code == LVN_ITEMACTIVATE) {
                /* 双击/回车：取 feedUrl 直接添加 */
                int sel = (int)SendMessageW(lv, LVM_GETNEXTITEM, -1, LVNI_SELECTED);
                if (sel >= 0) {
                    LVITEMW li;
                    ZeroMemory(&li, sizeof(li));
                    li.mask = LVIF_PARAM;
                    li.iItem = sel;
                    if (SendMessageW(lv, LVM_GETITEMW, 0, (LPARAM)&li)) {
                        wchar_t *url = (wchar_t *)li.lParam;
                        if (url && *url) SetDlgItemTextW(h, ID_IB_EDIT, url);
                    }
                }
                SendMessageW(h, WM_COMMAND, MAKEWPARAM(ID_IB_OK, BN_CLICKED), 0);
            }
        }
        return 0;
    }
    case WM_CLOSE:
        g_ibResult = 0;
        DestroyWindow(h);
        return 0;
    case WM_DESTROY: {
        /* 清理 ListView lParam 里的 feedUrl 宽串指针 */
        HWND lb = GetDlgItem(h, ID_IB_LIST);
        if (lb) {
            int n = (int)SendMessageW(lb, LVM_GETITEMCOUNT, 0, 0);
            int i;
            for (i = 0; i < n; i++) {
                LVITEMW li;
                ZeroMemory(&li, sizeof(li));
                li.mask = LVIF_PARAM;
                li.iItem = i;
                if (SendMessageW(lb, LVM_GETITEMW, 0, (LPARAM)&li))
                    free((wchar_t *)li.lParam);
            }
        }
        g_ibSb = NULL;
        g_ibSbH = NULL;
        g_ibList = NULL;
        g_ibTip = NULL;
        g_ibOrigHdrProc = NULL;   /* 表头随窗口销毁，原 proc 指针失效 */
        return 0;
    }
    }
    return DefWindowProcW(h, msg, wp, lp);
}

/* 模态输入框：title 为窗口标题，mode 0=订阅 URL 1=分钟数。
 * 确认返回 1（结果在 g_ibBuf），取消返回 0 */
static int AskInputBox(const wchar_t *title, int mode)
{
    static const wchar_t *IB_CLASS = L"LpInputBox";
    WNDCLASSW wc;
    HWND d;
    RECT rp, rc;
    MSG m;

    if (!GetClassInfoW(GetModuleHandleW(NULL), IB_CLASS, &wc)) {
        ZeroMemory(&wc, sizeof(wc));
        wc.lpfnWndProc = InputBoxProc;
        wc.hInstance = GetModuleHandleW(NULL);
        wc.lpszClassName = IB_CLASS;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        RegisterClassW(&wc);
    }

    g_ibMode = mode;
    g_ibResult = 0;
    g_ibBuf[0] = 0;
    /* 按目标客户区 432x112 反算含边框标题栏的外尺寸（四边留出间距） */
    {
        RECT wr = { 0, 0, 432, 112 };
        AdjustWindowRectEx(&wr, WS_POPUP | WS_CAPTION | WS_SYSMENU,
                           FALSE, WS_EX_DLGMODALFRAME);
        g_ibBorderDeltaX = (wr.right - wr.left) - 432;   /* 外宽 - 客户区宽 */
        g_ibBorderDeltaY = (wr.bottom - wr.top) - 112;   /* 外高 - 客户区高 */
        d = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_TOPMOST, IB_CLASS,
            title,
            WS_POPUP | WS_CAPTION | WS_SYSMENU,
            0, 0, wr.right - wr.left, wr.bottom - wr.top,
            g_hwnd, NULL, GetModuleHandleW(NULL), NULL);
    }
    if (!d) return 0;

    /* 居中到主窗口 */
    GetWindowRect(g_hwnd, &rp);
    GetWindowRect(d, &rc);
    SetWindowPos(d, 0,
        rp.left + ((rp.right - rp.left) - (rc.right - rc.left)) / 2,
        rp.top  + ((rp.bottom - rp.top)  - (rc.bottom - rc.top)) / 2,
        0, 0, SWP_NOSIZE | SWP_NOZORDER);

    EnableWindow(g_hwnd, FALSE);
    ShowWindow(d, SW_SHOW);
    UpdateWindow(d);
    while (IsWindow(d) && GetMessageW(&m, NULL, 0, 0) > 0) {
        HWND focus;
        /* 自己拦截 VK_RETURN：IsDialogMessageW 对 BS_OWNERDRAW 的
         * DefaultButton 识别不可靠，直接在消息循环层面处理。
         * 焦点在 ListView 上取选中项直接添加；其他情况一律转 OK。 */
        if (m.message == WM_KEYDOWN && m.wParam == VK_RETURN) {
            focus = GetFocus();
            if (focus == d || IsChild(d, focus)) {
                if (focus == GetDlgItem(d, ID_IB_LIST)) {
                    /* ListView 上按回车 = 等价 LVN_ITEMACTIVATE */
                    NMHDR nm;
                    nm.code = LVN_ITEMACTIVATE;
                    nm.idFrom = ID_IB_LIST;
                    nm.hwndFrom = focus;
                    SendMessageW(d, WM_NOTIFY, 0, (LPARAM)&nm);
                } else {
                    SendMessageW(d, WM_COMMAND,
                        MAKEWPARAM(ID_IB_OK, BN_CLICKED), 0);
                }
                continue;
            }
        }
        if (m.message == WM_KEYDOWN && m.wParam == VK_ESCAPE) {
            focus = GetFocus();
            if (focus == d || IsChild(d, focus)) {
                SendMessageW(d, WM_COMMAND,
                    MAKEWPARAM(ID_IB_CANCEL, BN_CLICKED), 0);
                continue;
            }
        }
        if (!IsDialogMessageW(d, &m)) {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
    }
    EnableWindow(g_hwnd, TRUE);
    SetForegroundWindow(g_hwnd);
    return g_ibResult && g_ibBuf[0];
}

/* 弹出输入框；确认后追加订阅并自动刷新 */
static void AskAddFeed(void)
{
    if (AskInputBox(tr(TR_INPUT_TITLE), 0)) {
        char *u8 = WToU8(g_ibBuf);
        if (u8) {
            AddFeedUrl(u8);        /* ini 只在退出时统一写，运行中全部走内存 */
            AddSingleFeed(u8);     /* 只加载新增的这一条，不刷新全部 */
            free(u8);
        }
    }
}

/* 自定义分钟数定时关机 */
static void SetShutdownTimer(int minutes);   /* 定义在设置菜单段 */
static void AskCustomShutdown(void)
{
    if (AskInputBox(tr(TR_CUSTOM_TITLE), 1))
        SetShutdownTimer(_wtoi(g_ibBuf));
}

/* 切换播客列表模式：0=普通 1=删除 2=排序（互斥） */
static void SetPodMode(int mode)
{
    if (g_podMode == mode) mode = 0;
    g_podMode = mode;
    InvalidateRect(g_listPod, NULL, TRUE);
    if (g_btnDel) InvalidateRect(g_btnDel, NULL, TRUE);
}

/* ================= 播客列表拖拽调序（普通模式按住整行拖动） =================
 * 同剧集拖拽多选：不用 SetCapture，定时器轮询 + GetAsyncKeyState 检测松开。
 * 移动超过阈值才进入拖拽（避免误点搬动条目）；拖动中实时搬家，
 * 复用 MovePodcast（g_feedUrls/g_curPod/g_playPod 同步）。 */
#define POD_DRAG_TIMER  0x4456
#define POD_DRAG_THRESH 8       /* 垂直位移阈值(px)，超过才算拖拽 */
static int  g_podDragOn = 0;      /* 按下且命中真实播客行（待判定） */
static int  g_podDragStarted = 0; /* 超过阈值，正式进入拖拽 */
static int  g_podDragIdx = -1;    /* 被拖行当前下标 */
static int  g_podDragCount = 0;   /* 按下时的播客数（中途重建列表则中止） */
static POINT g_podDragDown;       /* 按下时光标（屏幕坐标） */

/* 删除/排序模式下，播客列表的鼠标按下由这里处理（命中封面区域），返回 1=吞掉 */
static int PodListModeClick(POINT pt)
{
    DWORD v;
    int item;
    RECT rc;
    if (g_podMode == 0) return 0;
    v = (DWORD)SendMessageW(g_listPod, LB_ITEMFROMPOINT, 0, MAKELPARAM(pt.x, pt.y));
    if (HIWORD(v)) return 1;                 /* 点在列表空白：吞掉，不改选中 */
    item = (int)LOWORD(v);
    if (item < 0 || item >= g_podCount) return 1;
    if (SendMessageW(g_listPod, LB_GETITEMRECT, (WPARAM)item, (LPARAM)&rc) == LB_ERR)
        return 1;
    /* 封面热区：rc.left+2 .. rc.left+54 */
    if (pt.x < rc.left + 2 || pt.x > rc.left + 54) return 1;
    if (g_podMode == 1) {
        RemovePodcast(item);                 /* 封面整体是一个大 × */
    }
    return 1;
}

/* ================= 滑块滚轮修正 ================= */
/* trackbar 默认滚轮方向是反的（向上滚反而减小），子类化后自己处理 */

static LRESULT CALLBACK SliderProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    WNDPROC old = (hwnd == g_sldSeek) ? g_oldSeekProc : g_oldVolProc;
    if (msg == WM_MOUSEWHEEL) {
        /* Ctrl+滚轮：调全局字号（普通滚轮才是调滑块） */
        if (GET_KEYSTATE_WPARAM(wp) & MK_CONTROL) {
            ChangeFontLevel(g_fontLevel + (GET_WHEEL_DELTA_WPARAM(wp) > 0 ? 1 : -1));
            return 0;
        }
        int isVol = (hwnd == g_sldVol);
        int maxPos = isVol ? 100 : 1000;
        int step  = isVol ? 5 : 20;      /* 音量 5%/格，进度 2%/格 */
        int pos = (int)SendMessageW(hwnd, TBM_GETPOS, 0, 0);
        pos += (GET_WHEEL_DELTA_WPARAM(wp) > 0) ? step : -step;  /* 上滚 = 增大 */
        if (pos < 0) pos = 0;
        if (pos > maxPos) pos = maxPos;
        SendMessageW(hwnd, TBM_SETPOS, TRUE, pos);
        /* 复用主窗口 WM_HSCROLL 里的现有逻辑 */
        SendMessageW(g_hwnd, WM_HSCROLL,
            MAKEWPARAM(isVol ? TB_THUMBTRACK : TB_ENDTRACK, pos), (LPARAM)hwnd);
        return 0;
    }
    /* 自绘填充横跨 0..pos，而 trackbar 在位置变化时只重绘把手附近条带，
     * 会导致填充区残缺（拖快了出现断条）。凡位置可能变化的消息都全量失效：
     * TBM_SETPOS 覆盖定时器/远程/停止等程序性更新；
     * 拖动（capture 中）的 mousemove 覆盖用户拖拽。 */
    if (msg == TBM_SETPOS) {
        LRESULT lr = CallWindowProcW(old, hwnd, msg, wp, lp);
        InvalidateRect(hwnd, NULL, FALSE);
        return lr;
    }
    if (msg == WM_MOUSEMOVE && GetCapture() == hwnd) {
        LRESULT lr = CallWindowProcW(old, hwnd, msg, wp, lp);
        InvalidateRect(hwnd, NULL, FALSE);
        return lr;
    }
    return CallWindowProcW(old, hwnd, msg, wp, lp);
}

/* 内容面板与按钮共用：Ctrl+滚轮调字号。
 * 滚动条暗色由 33ms 定时器统一兜底，这里不拦截任何消息。 */
static LRESULT CALLBACK PanelProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    WNDPROC old;
    /* 暗色表头：ODT_HEADER 的 WM_DRAWITEM 发给表头的父窗口 ListView */
    if (g_darkMode && msg == WM_DRAWITEM &&
        (hwnd == g_listEp || hwnd == g_ibList)) {
        DRAWITEMSTRUCT *di = (DRAWITEMSTRUCT*)lp;
        if (di->CtlType == ODT_HEADER)
            return DrawHeaderItem(di) ? TRUE : FALSE;
    }
    /* 删除/排序模式下，播客列表的点击全部自管（不允许改选中项） */
    if (hwnd == g_listPod && msg == WM_LBUTTONDOWN && g_podMode != 0) {
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        PodListModeClick(pt);
        return 0;
    }
    /* 普通模式按下真实播客行：记录起点进入拖拽判定（不吞消息，
     * 选中仍由 ListBox 自己完成；不到阈值就是一次普通点击） */
    if (hwnd == g_listPod && msg == WM_LBUTTONDOWN && g_podMode == 0) {
        DWORD v = (DWORD)SendMessageW(hwnd, LB_ITEMFROMPOINT, 0, lp);
        if (!HIWORD(v) && g_podCount > 1 && (int)LOWORD(v) < g_podCount) {
            g_podDragOn = 1;
            g_podDragStarted = 0;
            g_podDragIdx = (int)LOWORD(v);
            g_podDragCount = g_podCount;
            GetCursorPos(&g_podDragDown);
            SetTimer(hwnd, POD_DRAG_TIMER, 50, NULL);
        }
    }
    /* 拖拽调序的轮询定时器（挂在 g_listPod 上） */
    if (hwnd == g_listPod && msg == WM_TIMER && wp == POD_DRAG_TIMER) {
        if (!g_podDragOn || g_podDragIdx < 0 || g_podCount != g_podDragCount) {
            g_podDragOn = g_podDragStarted = 0;   /* 列表被重建等异常，中止 */
            g_podDragIdx = -1;
            KillTimer(hwnd, POD_DRAG_TIMER);
            return 0;
        }
        if (!(GetAsyncKeyState(VK_LBUTTON) & 0x8000)) {   /* 松开 = 结束 */
            g_podDragOn = g_podDragStarted = 0;
            g_podDragIdx = -1;
            KillTimer(hwnd, POD_DRAG_TIMER);
            SyncScrollBar(g_vsPod);
            return 0;
        }
        if (!g_podDragStarted) {
            POINT pt;
            GetCursorPos(&pt);
            if (pt.y > g_podDragDown.y + POD_DRAG_THRESH ||
                g_podDragDown.y > pt.y + POD_DRAG_THRESH)
                g_podDragStarted = 1;   /* 超过阈值，开始拖 */
        }
        if (g_podDragStarted) {
            POINT pt;
            RECT rc;
            GetCursorPos(&pt);
            ScreenToClient(hwnd, &pt);
            GetClientRect(hwnd, &rc);
            if (!PtInRect(&rc, pt)) {   /* 出界自动滚一行 */
                int top = (int)SendMessageW(hwnd, LB_GETTOPINDEX, 0, 0);
                if (pt.y < rc.top) {
                    if (top > 0) {
                        SendMessageW(hwnd, LB_SETTOPINDEX, top - 1, 0);
                        SyncScrollBar(g_vsPod);
                    }
                } else {
                    SendMessageW(hwnd, LB_SETTOPINDEX, top + 1, 0);
                    SyncScrollBar(g_vsPod);
                }
            }
            /* 光标所在行 = 目标位置（夹到真实播客范围，最后是“已缓存”行） */
            {
                DWORD v = (DWORD)SendMessageW(hwnd, LB_ITEMFROMPOINT, 0,
                                               MAKELPARAM(pt.x, pt.y));
                int tgt = (int)LOWORD(v);
                if (tgt < 0) tgt = 0;
                if (tgt > g_podCount - 1) tgt = g_podCount - 1;
                MovePodcastTo(g_podDragIdx, tgt);
                g_podDragIdx = tgt;
            }
        }
        return 0;
    }
    /* 播客列表悬停 → 显示封面+标题+作者提示 */
    if (hwnd == g_listPod) {
        if (msg == WM_MOUSEMOVE) {
            TRACKMOUSEEVENT tme;
            if (g_podDragStarted) {          /* 拖拽中不弹提示 */
                ShowWindow(g_hwndPodTip, SW_HIDE);
                return 0;
            }
            tme.cbSize = sizeof(tme);
            tme.dwFlags = TME_HOVER | TME_LEAVE;
            tme.hwndTrack = hwnd;
            tme.dwHoverTime = HOVER_DEFAULT;
            TrackMouseEvent(&tme);
        } else if (msg == WM_MOUSEHOVER) {
            POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            if (!g_podDragStarted)
                UpdatePodTipFromPoint(pt);
        } else if (msg == WM_MOUSELEAVE) {
            ShowWindow(g_hwndPodTip, SW_HIDE);
        }
    }
    /* 剧集列表：tracking tooltip（不依赖标题截断，悬停即显示） */
    if (hwnd == g_listEp && g_hwndEpTip) {
        if (msg == WM_MOUSEMOVE) {
            TRACKMOUSEEVENT tme;
            tme.cbSize = sizeof(tme);
            tme.dwFlags = TME_HOVER | TME_LEAVE;
            tme.hwndTrack = hwnd;
            tme.dwHoverTime = HOVER_DEFAULT;
            TrackMouseEvent(&tme);
        } else if (msg == WM_MOUSEHOVER) {
            LVHITTESTINFO ht;
            POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            ht.pt = pt;
            int idx = (int)SendMessageW(hwnd, LVM_HITTEST, 0, (LPARAM)&ht);
            if (idx >= 0) {
                wchar_t tip[1024];
                TOOLINFOW ti;
                POINT scr = pt;
                BuildEpisodeTipText(tip, 1024, idx);
                ClientToScreen(hwnd, &scr);
                ZeroMemory(&ti, sizeof(ti));
                ti.cbSize = sizeof(ti);
                ti.hwnd = hwnd;
                ti.uId = (UINT_PTR)hwnd;
                ti.lpszText = tip;
                SendMessageW(g_hwndEpTip, TTM_UPDATETIPTEXTW, 0, (LPARAM)&ti);
                SendMessageW(g_hwndEpTip, TTM_TRACKPOSITION, 0,
                    MAKELPARAM(scr.x + 12, scr.y + 18));
                SendMessageW(g_hwndEpTip, TTM_TRACKACTIVATE, TRUE, (LPARAM)&ti);
            }
            return 0;
        } else if (msg == WM_MOUSELEAVE) {
            TOOLINFOW ti;
            ZeroMemory(&ti, sizeof(ti));
            ti.cbSize = sizeof(ti);
            ti.hwnd = hwnd;
            ti.uId = (UINT_PTR)hwnd;
            SendMessageW(g_hwndEpTip, TTM_TRACKACTIVATE, FALSE, (LPARAM)&ti);
        }
    }
    /* 搜索结果列表：悬停显示标题 / 作者 / 订阅地址 */
    if (hwnd == g_ibList && g_ibTip) {
        if (msg == WM_MOUSEMOVE) {
            TRACKMOUSEEVENT tme;
            tme.cbSize = sizeof(tme);
            tme.dwFlags = TME_HOVER | TME_LEAVE;
            tme.hwndTrack = hwnd;
            tme.dwHoverTime = HOVER_DEFAULT;
            TrackMouseEvent(&tme);
        } else if (msg == WM_MOUSEHOVER) {
            LVHITTESTINFO ht;
            POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            ht.pt = pt;
            int idx = (int)SendMessageW(hwnd, LVM_HITTEST, 0, (LPARAM)&ht);
            if (idx >= 0) {
                wchar_t title[512] = L"", author[512] = L"";
                wchar_t *url = NULL;
                wchar_t tip[2048];
                LVITEMW li;
                TOOLINFOW ti;
                POINT scr = pt;
                ZeroMemory(&li, sizeof(li));
                li.mask = LVIF_TEXT | LVIF_PARAM;
                li.iItem = idx;
                li.iSubItem = 0;
                li.pszText = title;
                li.cchTextMax = 512;
                SendMessageW(hwnd, LVM_GETITEMW, 0, (LPARAM)&li);
                url = (wchar_t *)li.lParam;
                li.mask = LVIF_TEXT;
                li.iSubItem = 1;
                li.pszText = author;
                li.cchTextMax = 512;
                SendMessageW(hwnd, LVM_GETITEMW, 0, (LPARAM)&li);
                swprintf(tip, 2048, L"%ls\r\n%ls\r\n%ls",
                         title, author, url ? url : L"");
                ClientToScreen(hwnd, &scr);
                ZeroMemory(&ti, sizeof(ti));
                ti.cbSize = sizeof(ti);
                ti.hwnd = hwnd;
                ti.uId = (UINT_PTR)hwnd;
                ti.lpszText = tip;
                SendMessageW(g_ibTip, TTM_UPDATETIPTEXTW, 0, (LPARAM)&ti);
                SendMessageW(g_ibTip, TTM_TRACKPOSITION, 0,
                    MAKELPARAM(scr.x + 12, scr.y + 18));
                SendMessageW(g_ibTip, TTM_TRACKACTIVATE, TRUE, (LPARAM)&ti);
            }
            return 0;
        } else if (msg == WM_MOUSELEAVE) {
            TOOLINFOW ti;
            ZeroMemory(&ti, sizeof(ti));
            ti.cbSize = sizeof(ti);
            ti.hwnd = hwnd;
            ti.uId = (UINT_PTR)hwnd;
            SendMessageW(g_ibTip, TTM_TRACKACTIVATE, FALSE, (LPARAM)&ti);
        }
    }
    if (msg == WM_MOUSEWHEEL && (GET_KEYSTATE_WPARAM(wp) & MK_CONTROL)) {
        ChangeFontLevel(g_fontLevel + (GET_WHEEL_DELTA_WPARAM(wp) > 0 ? 1 : -1));
        return 0;
    }
    /* 播客列表滚动时，隐藏悬停提示避免显示旧条目 */
    if (hwnd == g_listPod && msg == WM_MOUSEWHEEL)
        ShowWindow(g_hwndPodTip, SW_HIDE);
    /* 内容控件已无系统滚动条：滚轮由这里手动驱动（ListView 除外，
     * 它保留默认平滑滚动）。LB_SETTOPINDEX/EM_LINESCROLL 自动夹紧边界 */
    if (msg == WM_MOUSEWHEEL && hwnd == g_listPod) {
        int d = GET_WHEEL_DELTA_WPARAM(wp) > 0 ? -3 : 3;
        int top = (int)SendMessageW(hwnd, LB_GETTOPINDEX, 0, 0);
        if (top + d < 0) d = -top;
        SendMessageW(hwnd, LB_SETTOPINDEX, top + d, 0);
        SyncScrollBar(g_vsPod);
        return 0;
    }
    if (msg == WM_MOUSEWHEEL && hwnd == g_editDesc) {
        int d = GET_WHEEL_DELTA_WPARAM(wp) > 0 ? -3 : 3;
        SendMessageW(hwnd, EM_LINESCROLL, 0, (LPARAM)d);
        SyncScrollBar(g_vsDesc);
        return 0;
    }
    /* ListView 会自动显示自带滚动条：在 WM_NCCALCSIZE 里把条带并回
     * 客户区（永不显示白条，内容宽度也不抖动）。剧集列表与搜索结果列表通用 */
    if (msg == WM_NCCALCSIZE && wp &&
        (hwnd == g_listEp || hwnd == g_ibList)) {
        WNDPROC oldp = (WNDPROC)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
        LRESULT lr = CallWindowProcW(oldp, hwnd, msg, wp, lp);
        SCROLLBARINFO sb;
        RECT *rc = &((NCCALCSIZE_PARAMS*)lp)->rgrc[0];
        sb.cbSize = sizeof(sb);
        if (GetScrollBarInfo(hwnd, OBJID_VSCROLL, &sb) &&
            !(sb.rgstate[0] & STATE_SYSTEM_INVISIBLE))
            rc->right += GetSystemMetrics(SM_CXVSCROLL);
        sb.cbSize = sizeof(sb);
        if (GetScrollBarInfo(hwnd, OBJID_HSCROLL, &sb) &&
            !(sb.rgstate[0] & STATE_SYSTEM_INVISIBLE))
            rc->bottom += GetSystemMetrics(SM_CYHSCROLL);
        return lr;
    }
    /* 滚动/尺寸变化后立即同步自绘滚动条（不依赖定时器） */
    if (msg == WM_VSCROLL || msg == WM_HSCROLL ||
        msg == WM_SIZE || msg == WM_MOUSEWHEEL) {
        old = (WNDPROC)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
        LRESULT lr = CallWindowProcW(old, hwnd, msg, wp, lp);
        if (hwnd == g_listPod)       SyncScrollBar(g_vsPod);
        else if (hwnd == g_listEp) { SyncScrollBar(g_vsEp); SyncScrollBar(g_hsEp); }
        else if (hwnd == g_ibList) { if (g_ibSb) SyncScrollBar(g_ibSb);
                                     if (g_ibSbH) SyncScrollBar(g_ibSbH); }
        else if (hwnd == g_editDesc) SyncScrollBar(g_vsDesc);
        return lr;
    }
    old = (WNDPROC)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    return CallWindowProcW(old, hwnd, msg, wp, lp);
}

/* 给一个控件挂上 PanelProc，并把其原始过程保存在该窗口的 USERDATA */
static void SubclassForFontWheel(HWND w)
{
    SetWindowLongPtrW(w, GWLP_USERDATA, GetWindowLongPtrW(w, GWLP_WNDPROC));
    SetWindowLongPtrW(w, GWLP_WNDPROC, (LONG_PTR)PanelProc);
}

/* ================= 可拖动分隔条 ================= */
#define SPLIT_MINPX  90    /* 每列最小像素宽 */
#define DESC_TAB_W   14    /* 简介折叠按钮宽（跨在面板边界上，一半在缝里） */
#define DESC_TAB_H   40    /* 简介折叠按钮高 */
static int g_dragSplit = -1;   /* -1=无 0=第一根 1=第二根 */

static void LayoutControls(HWND hwnd);   /* 前置声明 */

/* 三面板像素宽度：简介隐藏时剧集吃掉简介那份宽度 */
static void PanelWidthsPx(int totalW, int *w1, int *w2, int *w3)
{
    *w1 = totalW * g_w1perm / 1000;
    if (g_descHidden) {
        /* 简介隐藏：剧集占据简介那份宽度，并连同原第二道缝隙一起占满，
         * 面板右缘与窗口右边距对齐（否则右侧会留下一条空白带） */
        *w2 = totalW - *w1 + GAP_W;
        *w3 = 0;
    } else {
        *w2 = totalW * g_w2perm / 1000;
        *w3 = totalW - *w1 - *w2;
    }
}

/* 返回 (x,y) 命中的分隔条；-1=未命中 */
static int SplitterHitTest(HWND hwnd, int x, int y)
{
    RECT rc;
    int W, H, totalW, w1, w2, w3, by, bh;
    int s1, s2;
    GetClientRect(hwnd, &rc);
    W = rc.right; H = rc.bottom;
    by = MARGIN + TOP_H + GAP_W;
    bh = H - by - MARGIN;
    by += 28;   /* 播客工具栏占去顶部 28px，分隔条只在列表区域生效 */
    if (y < by || y > by + bh - 28) return -1;
    totalW = W - 2*MARGIN - 2*GAP_W;
    PanelWidthsPx(totalW, &w1, &w2, &w3);
    s1 = MARGIN + w1 + GAP_W / 2;
    s2 = MARGIN + w1 + GAP_W + w2 + GAP_W / 2;
    if (abs(x - s1) <= 4) return 0;
    if (!g_descHidden && abs(x - s2) <= 4) return 1;   /* 简介隐藏时没有第二条分隔 */
    return -1;
}

/* 冻结重绘的整体布局，拖动/缩放共用 */
static void ApplyLayout(HWND hwnd, int live)
{
    if (live) {
        SendMessageW(hwnd, WM_SETREDRAW, FALSE, 0);
        LayoutControls(hwnd);
        SendMessageW(hwnd, WM_SETREDRAW, TRUE, 0);
        RedrawWindow(hwnd, NULL, NULL,
            RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_ERASE |
            RDW_FRAME | RDW_UPDATENOW);
    } else {
        LayoutControls(hwnd);
    }
}

/* ================= 布局 ================= */
static void LayoutControls(HWND hwnd)
{
    RECT rc;
    int W, H;
    int by, bh, totalW, w1, w2, w3, x, gx, gw, gy, bx;
    int volW = 24, sw = 84, fmtX;

    GetClientRect(hwnd, &rc);
    W = rc.right; H = rc.bottom;

    /* 顶部播放区：边框由 WM_PAINT 绘制，控件向内缩。
     * 第1行状态跑马灯由 WM_PAINT 自绘，这里只管第2、3行。 */
    gx = MARGIN + 8;
    gw = W - 2*MARGIN - 16;

    gy = MARGIN + 6 + 22;
    MoveWindow(g_sldSeek, gx, gy, gw - 108, 22, TRUE);
    MoveWindow(g_stTime,  gx + gw - 104, gy + 2, 104, 18, TRUE);
    gy += 28;

    /* 第3行：上一曲/播放/下一曲/停止/播放模式/倍速/静音（间距统一 26，给格式串留空间） */
    bx = gx;
    MoveWindow(g_btnPrev,     bx, gy, 24, 24, TRUE); bx += 26;
    MoveWindow(g_btnPlay,     bx, gy, 24, 24, TRUE); bx += 26;
    MoveWindow(g_btnNext,     bx, gy, 24, 24, TRUE); bx += 26;
    MoveWindow(g_btnStop,     bx, gy, 24, 24, TRUE); bx += 26;
    MoveWindow(g_btnPlayMode, bx, gy, 24, 24, TRUE); bx += 26;
    MoveWindow(g_btnSpeed,    bx, gy, 40, 24, TRUE); bx += 42;
    MoveWindow(g_btnMute,     bx, gy, 24, 24, TRUE); bx += 26;
    /* 音量滑块固定宽 + 数字；格式串右靠齐，占满剩余空间 */
    fmtX = bx + sw + 4 + volW + 8;
    MoveWindow(g_sldVol,   bx, gy, sw, 24, TRUE);
    MoveWindow(g_stVol,    bx + sw + 4, gy + 4, volW, 16, TRUE);
    MoveWindow(g_stFormat, fmtX, gy + 4, gx + gw - fmtX, 16, TRUE);

    by = MARGIN + TOP_H + GAP_W;
    bh = H - by - MARGIN;
    if (bh < 40) bh = 40;
    totalW = W - 2*MARGIN - 2*GAP_W;
    PanelWidthsPx(totalW, &w1, &w2, &w3);

    /* 播客列顶部工具栏（+/×/刷新/目录），列表向下让出 28px */
    x = MARGIN;
    MoveWindow(g_btnAdd,     x,      by, 24, 24, TRUE);
    MoveWindow(g_btnDel,     x + 27, by, 24, 24, TRUE);
    MoveWindow(g_btnRefresh, x + 54, by, 24, 24, TRUE);
    MoveWindow(g_btnDir,     x + 81, by, 24, 24, TRUE);
    /* 滚动条带预留：列表让出窄条宽度，自绘条放在旁边；
     * 内容不超范围时条会自动隐藏（SyncScrollBar），带子露出背景 */
    {
        int sbw = DSB_W, sbh = DSB_W;
        /* 面板边框由父窗口 WM_PAINT 统一绘制，控件一律在面板内缩 2px，
         * 滚条带收进边框内（亮/暗主题同一套边框，不再依赖控件自身样式） */
        g_rcPanel[0].left = x;           g_rcPanel[0].top = by + 28;
        g_rcPanel[0].right = x + w1;     g_rcPanel[0].bottom = by + bh;
        MoveWindow(g_listPod, x + 2, by + 30, w1 - sbw - 4, bh - 32, TRUE);
        if (g_vsPod) MoveWindow(g_vsPod, x + w1 - 2 - sbw, by + 30, sbw, bh - 32, TRUE);
        /* 缓存按钮：剧集面板内右上、竖向滚条带左侧，边框内侧 */
        if (g_btnCache)
            MoveWindow(g_btnCache, x + w1 + GAP_W + w2 - 28 - DSB_W, by + 2, 24, 24, TRUE);
        x += w1 + GAP_W;
        g_rcPanel[1].left = x;           g_rcPanel[1].top = by;
        g_rcPanel[1].right = x + w2;     g_rcPanel[1].bottom = by + bh;
        MoveWindow(g_listEp, x + 2, by + 2, w2 - sbw - 4, bh - sbh - 4, TRUE);
        if (g_vsEp) MoveWindow(g_vsEp, x + w2 - 2 - sbw, by + 2, sbw, bh - sbh - 4, TRUE);
        if (g_hsEp) MoveWindow(g_hsEp, x + 2, by + bh - 2 - sbh, w2 - sbw - 4, sbh, TRUE);
        x += w2 + GAP_W;
        if (g_descHidden) {
            SetRectEmpty(&g_rcPanel[2]);
            ShowWindow(g_editDesc, SW_HIDE);
            if (g_vsDesc) ShowWindow(g_vsDesc, SW_HIDE);
        } else {
            g_rcPanel[2].left = x;           g_rcPanel[2].top = by;
            g_rcPanel[2].right = x + w3;     g_rcPanel[2].bottom = by + bh;
            MoveWindow(g_editDesc, x + 2, by + 2, w3 - sbw - 4, bh - 4, TRUE);
            if (g_vsDesc) MoveWindow(g_vsDesc, x + w3 - 2 - sbw, by + 2, sbw, bh - 4, TRUE);
            ShowWindow(g_editDesc, SW_SHOWNA);
            if (g_vsDesc) ShowWindow(g_vsDesc, SW_SHOWNA);
        }
    }
    /* 简介折叠按钮：贴在简介左缘中段；简介关闭后贴到剧集列表右缘中段 */
    if (g_btnDescTab) {
        RECT pr = g_rcPanel[g_descHidden ? 1 : 2];
        int tx, ty;
        if (g_descHidden) {
            /* 贴在竖向滚条左侧：跨在滚条上会挡住拖动，还会显得像残留物 */
            tx = pr.right - 2 - DSB_W - DESC_TAB_W;
        } else {
            /* 跨在简介左缘（一半在缝里） */
            tx = pr.left - DESC_TAB_W / 2;
        }
        ty = (pr.top + pr.bottom - DESC_TAB_H) / 2;
        g_rcDescTab.left = tx;             g_rcDescTab.top = ty;
        g_rcDescTab.right = tx + DESC_TAB_W; g_rcDescTab.bottom = ty + DESC_TAB_H;
        MoveWindow(g_btnDescTab, tx, ty, DESC_TAB_W, DESC_TAB_H, TRUE);
        SetWindowPos(g_btnDescTab, HWND_TOP, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        if (!g_descTabShown) ShowWindow(g_btnDescTab, SW_HIDE);
    }
    /* 缓存按钮压到剧集列表之上（列表创建晚于按钮，z-order 更高；
     * 配合列表的 WS_CLIPSIBLINGS，列表重绘不会再盖住按钮） */
    if (g_btnCache)
        SetWindowPos(g_btnCache, HWND_TOP, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

/* ================= 简介折叠按钮 ================= */
/* 悬停才出现的窄条按钮：简介显示时贴在左缘画 ›，点击收起简介；
 * 收起后贴到剧集右缘画 ‹，点击恢复。只走鼠标，不进 Tab 焦点链。 */
static void DescTabShow(int show)
{
    if (!g_btnDescTab || show == g_descTabShown) return;
    g_descTabShown = show;
    ShowWindow(g_btnDescTab, show ? SW_SHOWNA : SW_HIDE);
    if (show) {
        SetWindowPos(g_btnDescTab, HWND_TOP, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        InvalidateRect(g_btnDescTab, NULL, TRUE);
    }
}

/* 每次定时器触发：光标靠近按钮（矩形外扩一圈）才显示 */
static void DescTabHoverTick(HWND hwnd)
{
    POINT p;
    RECT z;
    int nearIt = 0;
    if (!g_btnDescTab) return;
    if (IsIconic(hwnd) || !IsWindowVisible(hwnd)) nearIt = 0;
    else if (!IsRectEmpty(&g_rcDescTab) && GetCursorPos(&p)) {
        ScreenToClient(hwnd, &p);
        z = g_rcDescTab;
        InflateRect(&z, 16, 14);
        nearIt = PtInRect(&z, p);
    }
    DescTabShow(nearIt);
}

static LRESULT CALLBACK DescTabProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)wp;
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;

    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) {
            SetCursor(LoadCursorW(NULL, IDC_HAND));
            return TRUE;
        }
        break;

    case WM_LBUTTONDOWN:
        g_descHidden = !g_descHidden;
        /* 简介关闭时焦点若还在简介编辑框里，先交还主窗口，避免输入进了看不见的控件 */
        if (g_descHidden && g_editDesc && GetFocus() == g_editDesc) SetFocus(g_hwnd);
        /* 走 ApplyLayout：见/隐简介区会牵动父窗口背景与边框，必须整体重绘，
         * 否则旧滚动条、旧边框会以像素残留的形式留在界面上 */
        ApplyLayout(g_hwnd, 1);
        /* 折叠/恢复后把剧集列表连同表头（主窗口的孙窗口）整体强制重绘：
         * 悬浮控件盖过的区域平时画不进去，布局一变会露出旧像素，
         * 与标题栏换色同理——触发一次完整重绘即可 */
        if (g_listEp) {
            HWND hdr = (HWND)SendMessageW(g_listEp, LVM_GETHEADER, 0, 0);
            RedrawWindow(g_listEp, NULL, NULL,
                RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
            if (hdr) RedrawWindow(hdr, NULL, NULL,
                RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW);
        }
        DescTabShow(0);   /* 按钮已贴到另一侧，光标不在附近 → 收起 */
        return 0;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT rc;
        int cx, cy;
        HBRUSH br = CreateSolidBrush(ThPanel());
        HPEN pn = CreatePen(PS_SOLID, 1, ThDim());
        HBRUSH obr = (HBRUSH)SelectObject(dc, br);
        HPEN opn = (HPEN)SelectObject(dc, pn);
        GetClientRect(h, &rc);
        RoundRect(dc, rc.left + 1, rc.top + 1, rc.right - 1, rc.bottom - 1, 10, 10);
        SelectObject(dc, obr);
        SelectObject(dc, opn);
        DeleteObject(br);
        DeleteObject(pn);

        /* 1px 细 chevron：› 收起 / ‹ 展开。从顶点向外逐点镜像铺两条臂，
         * SetPixel 保证上下严格对称（LineTo 端点排他 + GDI 斜线方向归一化，
         * 一条线拐着画两臂像素集必然不对称） */
        cx = (rc.left + rc.right) / 2;
        cy = (rc.top + rc.bottom) / 2;
        {
            COLORREF col = ThText();
            int vx = cx + (g_descHidden ? -2 : 2);  /* 顶点：‹ 偏左 / › 偏右 */
            int sx = g_descHidden ? 1 : -1;         /* 臂自顶点向外延伸的方向 */
            int i;
            for (i = 0; i <= 4; i++) {
                SetPixel(dc, vx + sx * i, cy - i, col);
                SetPixel(dc, vx + sx * i, cy + i, col);
            }
        }
        EndPaint(h, &ps);
        return 0;
    }
    }
    return DefWindowProcW(h, msg, wp, lp);
}

/* ================= 方形图标按钮（owner-draw） ================= */
enum {
    ICO_PREV, ICO_PLAY, ICO_PAUSE, ICO_NEXT, ICO_STOP,
    ICO_MUTE, ICO_MUTED, ICO_PLUS, ICO_CROSS, ICO_SORT, ICO_REFRESH,
    ICO_ORDER, ICO_SHUFFLE, ICO_REPEATONE, ICO_PLAYONCE, ICO_GEAR,
    ICO_DOWNLOAD
};

/* 在按钮矩形内绘制单个矢量图标 */
static void DrawGlyphC(HDC hdc, const RECT *rc, int kind, COLORREF col)
{
    int w = rc->right - rc->left, h = rc->bottom - rc->top;
    int s = (w < h ? w : h) - 8;                 /* 24px 按钮 → 16px 图标 */
    int x0 = rc->left + (w - s) / 2;
    int y0 = rc->top + (h - s) / 2;
    int cy = y0 + s / 2;
    HPEN pen2 = CreatePen(PS_SOLID, 2, col);
    HPEN pen3 = CreatePen(PS_SOLID, 3, col);
    HPEN pen1 = CreatePen(PS_SOLID, 1, col);
    HBRUSH br = CreateSolidBrush(col);
    HGDIOBJ op = SelectObject(hdc, pen1);
    HGDIOBJ ob = SelectObject(hdc, br);

    switch (kind) {
    case ICO_PLAY: {
        POINT t[3] = { {x0+3,y0+1},{x0+3,y0+s-1},{x0+s-2,cy} };
        Polygon(hdc, t, 3);
        break;
    }
    case ICO_PAUSE:
        Rectangle(hdc, x0+3, y0+2, x0+7, y0+s-2);
        Rectangle(hdc, x0+9, y0+2, x0+13, y0+s-2);
        break;
    case ICO_STOP:
        Rectangle(hdc, x0+3, y0+3, x0+s-3, y0+s-3);
        break;
    case ICO_PREV: {
        POINT t[3] = { {x0+13,y0+2},{x0+5,cy},{x0+13,y0+s-2} };
        Rectangle(hdc, x0+2, y0+2, x0+5, y0+s-2);
        Polygon(hdc, t, 3);
        break;
    }
    case ICO_NEXT: {
        POINT t[3] = { {x0+3,y0+2},{x0+11,cy},{x0+3,y0+s-2} };
        Polygon(hdc, t, 3);
        Rectangle(hdc, x0+11, y0+2, x0+14, y0+s-2);
        break;
    }
    case ICO_MUTE:
    case ICO_MUTED: {
        /* 喇叭整体左移，音波弧收在 16px 图标盒内，避免被按钮边缘裁掉 */
        POINT cone[6] = {
            {x0+3,y0+5},{x0+7,y0+5},{x0+12,y0+1},
            {x0+12,y0+s-1},{x0+7,y0+s-5},{x0+3,y0+s-5} };
        Rectangle(hdc, x0+1, y0+5, x0+4, y0+s-5);
        Polygon(hdc, cone, 6);
        if (kind == ICO_MUTE) {
            /* 两道声波弧（开口朝右） */
            SelectObject(hdc, pen2);
            {
                POINT a1[5] = { {x0+12,y0+5},{x0+14,y0+7},{x0+14,cy},{x0+14,y0+s-7},{x0+12,y0+s-5} };
                POINT a2[5] = { {x0+14,y0+3},{x0+15,y0+6},{x0+15,cy},{x0+15,y0+s-6},{x0+14,y0+s-3} };
                Polyline(hdc, a1, 5);
                Polyline(hdc, a2, 5);
            }
        } else {
            /* 静音：喇叭右侧红叉 */
            HPEN rp = CreatePen(PS_SOLID, 2, RGB(200, 40, 40));
            SelectObject(hdc, rp);
            MoveToEx(hdc, x0+11, y0+3, NULL); LineTo(hdc, x0+16, y0+s-3);
            MoveToEx(hdc, x0+16, y0+3, NULL); LineTo(hdc, x0+11, y0+s-3);
            DeleteObject(rp);
        }
        break;
    }
    case ICO_PLUS:
        SelectObject(hdc, pen3);
        MoveToEx(hdc, x0+2, cy, NULL); LineTo(hdc, x0+s-2, cy);
        MoveToEx(hdc, x0+s/2, y0+2, NULL); LineTo(hdc, x0+s/2, y0+s-2);
        break;
    case ICO_CROSS:
        /* 删除按钮：× */
        SelectObject(hdc, pen3);
        MoveToEx(hdc, x0+3, y0+3, NULL); LineTo(hdc, x0+s-3, y0+s-3);
        MoveToEx(hdc, x0+s-3, y0+3, NULL); LineTo(hdc, x0+3, y0+s-3);
        break;
    case ICO_SORT: {
        /* 上下两个箭头（背靠背）：与“调整播客次序”用途一致 */
        int cx = x0 + s / 2;
        POINT up[3]   = { {cx-5,y0+9},{cx+5,y0+9},{cx,y0+2} };
        POINT down[3] = { {cx-5,y0+s-9},{cx+5,y0+s-9},{cx,y0+s-2} };
        Polygon(hdc, up, 3);
        Polygon(hdc, down, 3);
        break;
    }
    case ICO_REFRESH: {
        /* 标准刷新图标：两段圆弧各带一个箭头，首尾相追围成圆环 */
        double cxx = x0 + s / 2.0 + 0.5, cyy = y0 + s / 2.0 + 0.5, r = 5.6;
        int k;
        /* arcPts: 沿角度增大方向采样一段弧（屏幕上呈逆时针走笔） */
        #define LP_ARC(pts, a0, a1, n) do { \
            for (k = 0; k < (n); k++) { \
                double deg = (a0) + ((a1) - (a0)) * k / ((n) - 1); \
                double rad = deg * 3.14159265358979 / 180.0; \
                (pts)[k].x = (int)(cxx + r * cos(rad) + 0.5); \
                (pts)[k].y = (int)(cyy - r * sin(rad) + 0.5); \
            } } while (0)
        /* 在角度 a 处画一个沿“角度减小”（顺时针）方向的箭头 */
        #define LP_ARROW(a) do { \
            double rad = (a) * 3.14159265358979 / 180.0; \
            double dx = sin(rad), dy = cos(rad);          /* 顺时针切向 */ \
            double px = -cos(rad), py = sin(rad);         /* 法向 */ \
            int hx = (int)(cxx + r * cos(rad) + 0.5); \
            int hy = (int)(cyy - r * sin(rad) + 0.5); \
            POINT ah2[3]; \
            ah2[0].x = hx; ah2[0].y = hy; \
            ah2[1].x = (int)(hx - dx * 5 + px * 3 + 0.5); \
            ah2[1].y = (int)(hy - dy * 5 + py * 3 + 0.5); \
            ah2[2].x = (int)(hx - dx * 5 - px * 3 + 0.5); \
            ah2[2].y = (int)(hy - dy * 5 - py * 3 + 0.5); \
            Polygon(hdc, ah2, 3); } while (0)
        POINT arc[16];
        SelectObject(hdc, pen2);
        LP_ARC(arc, 60, 210, 16);     /* 上段弧：从右上绕左侧到左下 */
        Polyline(hdc, arc, 16);
        LP_ARC(arc, 240, 390, 16);    /* 下段弧：从左下绕右侧回右上 */
        Polyline(hdc, arc, 16);
        SelectObject(hdc, pen1);
        LP_ARROW(60);                 /* 上端箭头朝右 */
        LP_ARROW(240);                /* 下端箭头朝左 */
        #undef LP_ARC
        #undef LP_ARROW
        break;
    }
    case ICO_ORDER: {
        /* 顺序播放：三条横线 + 右下播放三角 */
        SelectObject(hdc, pen2);
        MoveToEx(hdc, x0+2, y0+4, NULL);  LineTo(hdc, x0+11, y0+4);
        MoveToEx(hdc, x0+2, y0+8, NULL);  LineTo(hdc, x0+11, y0+8);
        MoveToEx(hdc, x0+2, y0+12, NULL); LineTo(hdc, x0+8,  y0+12);
        {
            POINT t[3] = { {x0+10,y0+9},{x0+10,y0+s-1},{x0+s-2,cy+1} };
            Polygon(hdc, t, 3);
        }
        break;
    }
    case ICO_SHUFFLE: {
        /* 随机播放：两条交叉的折线箭头 */
        SelectObject(hdc, pen2);
        MoveToEx(hdc, x0+2, y0+3, NULL);
        LineTo(hdc, x0+7, y0+3); LineTo(hdc, x0+11, cy); LineTo(hdc, x0+7, y0+s-3); LineTo(hdc, x0+2, y0+s-3);
        MoveToEx(hdc, x0+9, y0+3, NULL);
        LineTo(hdc, x0+13, y0+3); LineTo(hdc, x0+9, cy); LineTo(hdc, x0+13, y0+s-3);
        /* 右箭头 */
        MoveToEx(hdc, x0+11, y0+s-3, NULL); LineTo(hdc, x0+s-2, y0+s-3);
        {
            POINT ah[3] = { {x0+s-5,y0+s-6},{x0+s-2,y0+s-3},{x0+s-5,y0+s} };
            SelectObject(hdc, pen1);
            Polygon(hdc, ah, 3);
        }
        break;
    }
    case ICO_REPEATONE: {
        /* 单曲循环：上下回环箭头 + 中间数字 1 */
        SelectObject(hdc, pen2);
        MoveToEx(hdc, x0+5, y0+4, NULL);
        LineTo(hdc, x0+s-4, y0+4); LineTo(hdc, x0+s-4, y0+8);
        LineTo(hdc, x0+s-7, y0+8);
        {
            POINT ah[3] = { {x0+s-10,y0+5},{x0+s-7,y0+8},{x0+s-10,y0+11} };
            Polygon(hdc, ah, 3);
        }
        MoveToEx(hdc, x0+s-5, y0+s-4, NULL);
        LineTo(hdc, x0+4, y0+s-4); LineTo(hdc, x0+4, y0+s-8);
        LineTo(hdc, x0+7, y0+s-8);
        {
            POINT ah2[3] = { {x0+10,y0+s-5},{x0+7,y0+s-8},{x0+10,y0+s-11} };
            Polygon(hdc, ah2, 3);
        }
        /* 中间 1 */
        SelectObject(hdc, pen3);
        MoveToEx(hdc, x0+s/2, y0+6, NULL); LineTo(hdc, x0+s/2, y0+s-6);
        break;
    }
    case ICO_PLAYONCE: {
        /* 一次性播放：播放三角 + 右侧数字 1（区别于“下一曲”的竖杠） */
        POINT t[3] = { {x0+1,y0+2},{x0+1,y0+s-2},{x0+9,cy} };
        Polygon(hdc, t, 3);
        SelectObject(hdc, pen2);
        /* 小旗 */
        MoveToEx(hdc, x0+10, y0+5, NULL); LineTo(hdc, x0+13, y0+3);
        /* 竖杆 */
        MoveToEx(hdc, x0+13, y0+3, NULL); LineTo(hdc, x0+13, y0+s-3);
        /* 底座 */
        MoveToEx(hdc, x0+10, y0+s-3, NULL); LineTo(hdc, x0+15, y0+s-3);
        break;
    }
    case ICO_GEAR: {
        /* 设置齿轮：8 根齿 + 外圈 + 中心孔 */
        double cx2 = x0 + s / 2.0 + 0.5, cy2 = y0 + s / 2.0 + 0.5;
        int k;
        SelectObject(hdc, pen2);
        SelectObject(hdc, GetStockObject(NULL_BRUSH));
        for (k = 0; k < 8; k++) {   /* 齿：沿半径方向的短辐条 */
            double rad = k * 3.14159265358979 / 4.0;
            double c = cos(rad), sn = sin(rad);
            MoveToEx(hdc, (int)(cx2 + c * 4.5 + 0.5), (int)(cy2 - sn * 4.5 + 0.5), NULL);
            LineTo(hdc,  (int)(cx2 + c * 7.5 + 0.5), (int)(cy2 - sn * 7.5 + 0.5));
        }
        Ellipse(hdc, (int)(cx2 - 4.5 + 0.5), (int)(cy2 - 4.5 + 0.5),
                     (int)(cx2 + 4.5 + 0.5), (int)(cy2 + 4.5 + 0.5));
        SelectObject(hdc, pen1);
        Ellipse(hdc, (int)(cx2 - 1.5 + 0.5), (int)(cy2 - 1.5 + 0.5),
                     (int)(cx2 + 1.5 + 0.5), (int)(cy2 + 1.5 + 0.5));
        break;
    }
    case ICO_DOWNLOAD: {
        /* 下载图标：向下的箭头 + 底线 */
        int mid = x0 + s / 2;
        int top = y0 + 3, bot = y0 + s - 5;
        SelectObject(hdc, pen2);
        MoveToEx(hdc, mid, top, NULL);
        LineTo(hdc, mid, bot - 2);
        MoveToEx(hdc, mid - 4, bot - 6, NULL);
        LineTo(hdc, mid, bot - 2);
        LineTo(hdc, mid + 4, bot - 6);
        SelectObject(hdc, pen1);
        MoveToEx(hdc, x0 + 3, bot, NULL);
        LineTo(hdc, x0 + s - 3, bot);
        break;
    }
    }

    SelectObject(hdc, op);
    SelectObject(hdc, ob);
    DeleteObject(pen1);
    DeleteObject(pen2);
    DeleteObject(pen3);
    DeleteObject(br);
}

/* 默认主题色版本（主窗口按钮用） */
static void DrawGlyph(HDC hdc, const RECT *rc, int kind)
{
    DrawGlyphC(hdc, rc, kind, ThDim());
}

/* on=1：模式按钮已按下（浅蓝底 + 凹陷边） */
static void DrawIconButton(DRAWITEMSTRUCT *di, int kind, int on)
{
    RECT rc = di->rcItem;
    HBRUSH bg;
    if (on) bg = CreateSolidBrush(g_darkMode ? RGB(56,74,100) : RGB(180, 215, 255));
    else bg = CreateSolidBrush(ThWinBg());
    FillRect(di->hDC, &rc, bg);
    DeleteObject(bg);
    if (on || (di->itemState & ODS_SELECTED))
        DrawEdge(di->hDC, &rc, EDGE_SUNKEN, BF_RECT);
    else
        DrawEdge(di->hDC, &rc, EDGE_RAISED, BF_RECT);
    DrawGlyph(di->hDC, &rc, kind);
}

/* 控件 ID → 图标种类；*on 返回是否按下态，不认识返回 -1 */
static int IconKindOf(int id, int *on)
{
    *on = 0;
    switch (id) {
    case IDC_BTN_PREV:  return ICO_PREV;
    case IDC_BTN_PLAY:  return g_playState == 1 ? ICO_PAUSE : ICO_PLAY;
    case IDC_BTN_NEXT:  return ICO_NEXT;
    case IDC_BTN_STOP:  return ICO_STOP;
    case IDC_BTN_MUTE:  *on = g_muted; return g_muted ? ICO_MUTED : ICO_MUTE;
    case IDC_BTN_ADD:   return ICO_PLUS;
    case IDC_BTN_DEL:   *on = (g_podMode == 1); return ICO_CROSS;
    case IDC_BTN_REFRESH: return ICO_REFRESH;
    case IDC_BTN_PLAYMODE:
        return (g_playMode == 1) ? ICO_SHUFFLE :
               (g_playMode == 2) ? ICO_REPEATONE :
               (g_playMode == 3) ? ICO_PLAYONCE : ICO_ORDER;
    case IDC_BTN_DIR:   return ICO_GEAR;
    case IDC_BTN_CACHE: return ICO_DOWNLOAD;
    }
    return -1;
}

/* ================= 缓存目录管理 ================= */
/* 统计某目录下 .mp3 文件数量 */
static int CountMp3InDir(const wchar_t *dir)
{
    wchar_t pat[MAX_PATH];
    WIN32_FIND_DATAW fd;
    HANDLE h;
    int n = 0;
    swprintf(pat, MAX_PATH, L"%ls*.mp3", dir);
    h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do { if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) n++; }
    while (FindNextFileW(h, &fd));
    FindClose(h);
    return n;
}

/* 把旧目录里所有缓存文件（.mp3）移动到新目录 */
static void MoveCacheFiles(const wchar_t *oldDir, const wchar_t *newDir,
                           const wchar_t *ext)
{
    wchar_t pat[MAX_PATH], src[MAX_PATH], dst[MAX_PATH];
    WIN32_FIND_DATAW fd;
    HANDLE h;
    swprintf(pat, MAX_PATH, L"%ls*.%ls", oldDir, ext);
    h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        swprintf(src, MAX_PATH, L"%ls%ls", oldDir, fd.cFileName);
        swprintf(dst, MAX_PATH, L"%ls%ls", newDir, fd.cFileName);
        MoveFileW(src, dst);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

/* 启动/取消定时关机：minutes>0 设定，0 取消；倒计时显示在窗口标题栏 */
static void SetShutdownTimer(int minutes)
{
    if (minutes > 0) {
        g_shutdownAt = (long long)time(NULL) + minutes * 60;
        SetTimer(g_hwnd, 3, 1000, NULL);
        SetStatusTemp(tr(TR_SHUT_SET));
    } else {
        g_shutdownAt = 0;
        KillTimer(g_hwnd, 3);
        SetStatusTemp(tr(TR_SHUT_CANCELED));
    }
    UpdateWindowTitle();
}

/* 到点执行关机：调用系统自带 shutdown.exe（/s 关机 /t 15 倒计时 /f 强制关闭应用）。
 * 不直接调用 AdjustTokenPrivileges/InitiateSystemShutdownExW/ExitWindowsEx 等
 * 提权关机 API——这些符号在导入表里与木马特征重合，容易被杀毒软件启发式误报。
 * shutdown.exe 是系统组件、自带倒计时 UI，15 秒内可运行 shutdown /a 取消。 */
static void DoSystemShutdown(void)
{
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    wchar_t cmd[] = L"shutdown.exe /s /t 15 /f /c \"LiteTune 定时关机\"";

    g_shutdownAt = 0;
    KillTimer(g_hwnd, 3);
    UpdateWindowTitle();

    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    if (CreateProcessW(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        SetStatusTemp(tr(TR_SHUTTING_DOWN));
    } else {
        SetStatusTemp(tr(TR_SHUT_DENIED));
    }
}

/* 设置按钮（原目录按钮）：缓存目录 3 项 + 夜间模式 + 语言 + 定时关机 + 字号 + 关于 */
static void SetEpisodeColumns(int cached);
static void FillEpisodeList(void);
static void RefreshCachedEntry(void);
static void ShowPodcastDetail(int idx);
static void ShowEpisodeDetail(int podIdx, int epIdx);
static const wchar_t *PlayModeTipText(void);

/* 更新某个按钮的系统 tooltip 文本（语言切换时用） */
static void UpdateTipText(HWND btn, const wchar_t *text)
{
    TOOLINFOW ti;
    if (!g_hwndTip || !btn) return;
    ZeroMemory(&ti, sizeof(ti));
    ti.cbSize = sizeof(ti);
    ti.uFlags = TTF_IDISHWND;
    ti.hwnd = g_hwnd;
    ti.uId = (UINT_PTR)btn;
    ti.lpszText = (LPWSTR)text;
    SendMessageW(g_hwndTip, TTM_UPDATETIPTEXTW, 0, (LPARAM)&ti);
}

/* 只换任务栏 4 个按钮的 tooltip，图标复用 */
static void UpdateThumbbarLanguage(void)
{
    THUMBBUTTON tb[4];
    const wchar_t *tips[4] = {
        tr(TR_TB_PREV),
        g_playState == 1 ? tr(TR_TB_PAUSE) : tr(TR_TB_PLAY),
        tr(TR_TB_NEXT), tr(TR_TB_SHUFFLE)
    };
    int i;
    if (!g_taskbar) return;
    for (i = 0; i < 4; i++) {
        ZeroMemory(&tb[i], sizeof(tb[i]));
        tb[i].dwMask = THB_TOOLTIP;
        tb[i].iId = i;
        wcscpy_s(tb[i].szTip, 260, tips[i]);
    }
    g_taskbar->lpVtbl->ThumbBarUpdateButtons(g_taskbar, g_hwnd, 4, tb);
}

/* 切换语言后刷新所有已创建的界面文本 */
static void ApplyLanguage(void)
{
    wchar_t buf[256];

    /* “已缓存”虚拟播客的固定标题/副标题 */
    if (g_cachePod.title)  { free(g_cachePod.title);  g_cachePod.title  = _wcsdup(tr(TR_CACHED)); }
    if (g_cachePod.author) { free(g_cachePod.author); g_cachePod.author = _wcsdup(tr(TR_CACHE_SUBTITLE)); }

    if (g_listEp) { SetEpisodeColumns(g_viewCached); FillEpisodeList(); }
    RefreshCachedEntry();
    if (g_listPod) InvalidateRect(g_listPod, NULL, TRUE);

    /* 详情框：按当前选中重显 */
    if (g_viewCached) {
        if (g_curEp >= 0) ShowEpisodeDetail(g_podCount, g_curEp);
        else { swprintf(buf, 256, tr(TR_CACHE_VIEW_DESC), g_cachePod.epCount);
               SetWindowTextW(g_editDesc, buf); }
    } else if (g_curPod >= 0 && g_curPod < g_podCount) {
        if (g_curEp >= 0) ShowEpisodeDetail(g_curPod, g_curEp);
        else ShowPodcastDetail(g_curPod);
    }

    UpdateTipText(g_btnPrev,     tr(TR_TIP_PREV));
    UpdateTipText(g_btnPlay,     tr(TR_TIP_PLAYPAUSE));
    UpdateTipText(g_btnNext,     tr(TR_TIP_NEXT));
    UpdateTipText(g_btnStop,     tr(TR_TIP_STOP));
    UpdateTipText(g_btnMute,     tr(TR_TIP_MUTE));
    UpdateTipText(g_btnAdd,      tr(TR_TIP_ADD));
    UpdateTipText(g_btnDel,      tr(TR_TIP_DEL));
    UpdateTipText(g_btnRefresh,  tr(TR_TIP_REFRESH));
    UpdateTipText(g_btnPlayMode, PlayModeTipText());
    UpdateTipText(g_btnDir,      tr(TR_TIP_SETTINGS));
    UpdateTipText(g_btnCache,    tr(TR_TIP_CACHE));
    UpdateThumbbarLanguage();
    UpdateWindowTitle();

    /* 状态行：下载中不打断；播放中重拼“正在播放”，其余回到就绪 */
    if (!g_downloading) {
        if (g_playPod >= 0 && g_playEp >= 0 &&
            (g_playState == 1 || g_playState == 2)) {
            Podcast *pp = (g_playPod == g_podCount) ? &g_cachePod : &g_pods[g_playPod];
            if (g_playEp < pp->epCount) {
                wchar_t st[512];
                swprintf(st, 512,
                    g_playPod == g_podCount ? tr(TR_PLAYING_OFFLINE_FMT) : tr(TR_PLAYING_FMT),
                    pp->eps[g_playEp].title ? pp->eps[g_playEp].title : L"");
                SetStatus(st);
            }
        } else {
            SetStatus(tr(TR_READY));
        }
    }
}

/* ================= 设置菜单：导入 / 导出 ================= */

/* 通用打开/保存文件对话框（comdlg32），返回是否选中路径 */
static BOOL FileDlg(int save, const wchar_t *title, const wchar_t *filter,
                    const wchar_t *defExt, wchar_t *path, int pathMax)
{
    OPENFILENAMEW ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    path[0] = 0;
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = g_hwnd;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile   = path;
    ofn.nMaxFile    = pathMax;
    ofn.lpstrTitle  = title;
    ofn.lpstrDefExt = defExt;
    ofn.Flags = OFN_HIDEREADONLY | (save ? OFN_OVERWRITEPROMPT
                                         : OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST);
    return save ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn);
}

/* 读整个文件到内存（上限 4MB），返回 malloc 缓冲（NUL 结尾），失败 NULL */
static char *ReadFileAll(const wchar_t *path, DWORD *lenOut)
{
    FILE *f = _wfsopen(path, L"rb", _SH_DENYNO);
    char *buf;
    long sz;
    *lenOut = 0;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > 4 * 1024 * 1024) { fclose(f); return NULL; }
    buf = (char*)malloc((size_t)sz + 1);
    if (buf) {
        if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
            free(buf); buf = NULL;
        } else {
            buf[sz] = 0;
            *lenOut = (DWORD)sz;
        }
    }
    fclose(f);
    return buf;
}

/* URL 是否已在订阅列表（与添加对话框同口径：宽字符不区分大小写比较） */
static int FeedUrlDup(const char *url)
{
    int k;
    wchar_t *w = U8ToW(url);
    if (!w) return 0;
    for (k = 0; k < g_feedUrlCount; k++) {
        wchar_t *e = U8ToW(g_feedUrls[k]);
        if (e) {
            int same = (_wcsicmp(e, w) == 0);
            free(e);
            if (same) { free(w); return 1; }
        }
    }
    free(w);
    return 0;
}

/* 就地还原 OPML 里的 &amp; 实体（URL 查询串最常见），其余实体不处理 */
static void DecodeAmp(char *s)
{
    char *w = s;
    while (*s) {
        if (strncmp(s, "&amp;", 5) == 0) { *w++ = '&'; s += 5; }
        else *w++ = *s++;
    }
    *w = 0;
}

/* 扫描 OPML 文本里的 xmlUrl="..." / xmlUrl='...'，去重后加入订阅列表。
 * 返回新导入数量（受 MAX_PODCASTS 上限约束） */
static int ImportOpmlText(const char *buf)
{
    const char *p = buf;
    int added = 0;
    while (g_feedUrlCount < MAX_PODCASTS && (p = strstr(p, "xmlUrl")) != NULL) {
        p += 6;
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
        if (*p == '=') {
            p++;
            while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
            if (*p == '"' || *p == '\'') {
                char q = *p++;
                const char *e = strchr(p, q);
                if (!e) break;
                if (e - p > 4 && e - p < MAX_FEED_LINE && strstr(p, "://")) {
                    char *url = (char*)malloc((size_t)(e - p) + 1);
                    if (url) {
                        int before = g_feedUrlCount;
                        memcpy(url, p, (size_t)(e - p));
                        url[e - p] = 0;
                        DecodeAmp(url);
                        if (!FeedUrlDup(url)) AddFeedUrl(url);
                        if (g_feedUrlCount > before) added++;
                        free(url);
                    }
                }
                p = e + 1;
            }
        }
    }
    return added;
}

/* XML 属性转义（& < > " '）；UTF-8 多字节序列高位为 1，不会被误改 */
static char *XmlEscapeUtf8(const char *s)
{
    size_t n = strlen(s), w = 0;
    char *o = (char*)malloc(n * 6 + 1);
    if (!o) return NULL;
    for (; *s; s++) {
        const char *rep;
        switch (*s) {
        case '&':  rep = "&amp;";  break;
        case '<':  rep = "&lt;";   break;
        case '>':  rep = "&gt;";   break;
        case '"':  rep = "&quot;"; break;
        case '\'': rep = "&apos;"; break;
        default:   o[w++] = *s;    continue;
        }
        memcpy(o + w, rep, strlen(rep));
        w += strlen(rep);
    }
    o[w] = 0;
    return o;
}

/* 按 feedUrl 在播客列表里找显示名（失败占位条目 title 为 NULL，调用方自回退） */
static const wchar_t *PodTitleByUrl(const char *url)
{
    int i;
    for (i = 0; i < g_podCount; i++)
        if (g_pods[i].feedUrl && strcmp(g_pods[i].feedUrl, url) == 0)
            return g_pods[i].title;
    return NULL;
}

/* 导出订阅列表为标准 OPML 2.0（text 用播客名，加载失败/未就绪则用 URL） */
static void DoExportOpml(void)
{
    static const wchar_t *FILTER =
        L"OPML (*.opml)\0*.opml\0All Files (*.*)\0*.*\0";
    wchar_t path[MAX_PATH];
    FILE *f;
    int i;

    if (!FileDlg(1, tr(TR_EXP_OPML_CAP), FILTER, L"opml", path, MAX_PATH)) return;
    f = _wfsopen(path, L"wb", _SH_DENYNO);
    if (!f) {
        MessageBoxTh(g_hwnd, tr(TR_EXP_FAIL), L"LiteTune", MB_OK | MB_ICONERROR);
        return;
    }
    fputs("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
          "<opml version=\"2.0\">\n"
          "  <head><title>LiteTune subscriptions</title></head>\n"
          "  <body>\n", f);
    for (i = 0; i < g_feedUrlCount; i++) {
        const wchar_t *t = PodTitleByUrl(g_feedUrls[i]);
        char *t8  = t ? WToU8(t) : NULL;
        char *txt = XmlEscapeUtf8(t8 ? t8 : g_feedUrls[i]);
        char *url = XmlEscapeUtf8(g_feedUrls[i]);
        free(t8);
        if (txt && url)
            fprintf(f, "    <outline type=\"rss\" text=\"%s\" xmlUrl=\"%s\"/>\n",
                    txt, url);
        free(txt);
        free(url);
    }
    fputs("  </body>\n</opml>\n", f);
    fclose(f);
    SetStatusTemp(tr(TR_EXP_DONE));
}

/* 导出完整配置为 ini（文本与 exe 尾部内嵌配置完全一致，可导入回来） */
static void DoExportIni(void)
{
    static const wchar_t *FILTER =
        L"Configuration (*.ini)\0*.ini\0All Files (*.*)\0*.*\0";
    wchar_t path[MAX_PATH];
    DWORD len = 0;
    char *cfg;

    if (!FileDlg(1, tr(TR_EXP_INI_CAP), FILTER, L"ini", path, MAX_PATH)) return;
    cfg = BuildConfigText(&len);
    if (!cfg) return;
    if (WriteTextFile(path, cfg, len))
        SetStatusTemp(tr(TR_EXP_DONE));
    else
        MessageBoxTh(g_hwnd, tr(TR_EXP_FAIL), L"LiteTune", MB_OK | MB_ICONERROR);
    free(cfg);
}

/* 导入：带标记头的配置 ini（全量恢复，写回尾部后退出生效）或 OPML（合并订阅） */
static void DoImport(void)
{
    static const wchar_t *FILTER =
        L"Supported files (*.opml;*.ini)\0*.opml;*.ini\0"
        L"OPML (*.opml)\0*.opml\0Configuration (*.ini)\0*.ini\0"
        L"All Files (*.*)\0*.*\0";
    wchar_t path[MAX_PATH];
    DWORD len = 0;
    char *buf;

    if (!FileDlg(0, tr(TR_IMP_TITLE), FILTER, NULL, path, MAX_PATH)) return;
    buf = ReadFileAll(path, &len);
    if (!buf) {
        MessageBoxTh(g_hwnd, tr(TR_IMP_FAIL), L"LiteTune", MB_OK | MB_ICONERROR);
        return;
    }

    if (MemHasCfgMarker(buf)) {
        /* 配置 ini：确认后按当前模式落地（外挂写 feeds.ini / 内嵌写 exe 尾部）；
         * g_skipSave 保证退出时不被旧内存状态覆盖 */
        if (MessageBoxTh(g_hwnd, tr(TR_IMP_INI_Q), tr(TR_IMP_TITLE),
                        MB_YESNO | MB_ICONQUESTION) == IDYES) {
            int ok = 0;
            if (g_cfgExternal) {
                wchar_t iniPath[MAX_PATH];
                CfgExternalPath(iniPath, MAX_PATH);
                ok = WriteTextFile(iniPath, buf, len);
            } else if (CfgTailWriteVerified(buf, len)) {
                ok = 1;
            }
            if (ok) {
                free(buf);
                g_skipSave = 1;
                MessageBoxTh(g_hwnd, tr(TR_IMP_INI_DONE), tr(TR_IMP_TITLE),
                            MB_OK | MB_ICONINFORMATION);
                SendMessageW(g_hwnd, WM_CLOSE, 0, 0);   /* 正常走清理并退出 */
                return;
            }
            MessageBoxTh(g_hwnd, tr(TR_IMP_WRITE_FAIL), L"LiteTune",
                        MB_OK | MB_ICONERROR);
        }
    } else if (strstr(buf, "xmlUrl")) {
        int added = ImportOpmlText(buf);
        free(buf);
        if (added > 0) {
            wchar_t msg[96];
            swprintf(msg, 96, tr(TR_IMP_OPML_OK_FMT), added);
            MessageBoxTh(g_hwnd, msg, tr(TR_IMP_TITLE), MB_OK | MB_ICONINFORMATION);
            ReloadFeeds(0);   /* 新订阅无缓存联网拉取，老订阅走缓存秒开 */
        } else {
            MessageBoxTh(g_hwnd, tr(TR_IMP_OPML_NONE), tr(TR_IMP_TITLE),
                        MB_OK | MB_ICONINFORMATION);
        }
        return;
    } else {
        MessageBoxTh(g_hwnd, tr(TR_IMP_UNKNOWN), L"LiteTune", MB_OK | MB_ICONWARNING);
    }
    free(buf);
}

/* 切换配置存储模式：0=内嵌 exe 尾部，1=外挂同目录 feeds.ini。
 * 仅由用户在此手动切换；启动时的模式一律由磁盘现状决定（见 LoadConfig） */
static void CfgSwitchMode(int external)
{
    wchar_t iniPath[MAX_PATH];
    DWORD len = 0;
    char *cfg;

    if (external == g_cfgExternal) return;
    CfgExternalPath(iniPath, MAX_PATH);

    if (external) {
        /* 切外挂：先把当前配置写入 feeds.ini，再剥离 exe 尾部内嵌配置（防尾随分发） */
        cfg = BuildConfigText(&len);
        if (!cfg) return;
        if (!WriteTextFile(iniPath, cfg, len)) {
            free(cfg);
            MessageBoxTh(g_hwnd, tr(TR_CFGMODE_FAIL), L"LiteTune", MB_OK | MB_ICONERROR);
            return;
        }
        free(cfg);
        CfgStripTail();   /* 尽力剥离，失败不影响外挂生效 */
        g_cfgExternal = 1;
        SetStatusTemp(tr(TR_CFGMODE_EXT_DONE));
    } else {
        /* 切内嵌：存在 feeds.ini 时先征得同意（切换即删除），再写尾部 */
        if (GetFileAttributesW(iniPath) != INVALID_FILE_ATTRIBUTES
            && MessageBoxTh(g_hwnd, tr(TR_CFGMODE_DELINI_Q), tr(TR_CFGMODE_CAP),
                            MB_YESNO | MB_ICONQUESTION) != IDYES)
            return;
        cfg = BuildConfigText(&len);
        if (!cfg) return;
        if (!CfgTailWriteVerified(cfg, len)) {
            free(cfg);
            MessageBoxTh(g_hwnd, tr(TR_CFGMODE_FAIL), L"LiteTune", MB_OK | MB_ICONERROR);
            return;
        }
        free(cfg);
        DeleteFileW(iniPath);
        g_cfgExternal = 0;
        SetStatusTemp(tr(TR_CFGMODE_EMB_DONE));
    }
}

/* ================= 下拉菜单（亮色原生 / 暗色全自绘） ================= */
/* 暗色模式下所有菜单项（含子菜单）都用 MF_OWNERDRAW，由 MenuMeasureItem /
 * MenuDrawItem 统一绘制。这样文字颜色、勾选符号、子菜单箭头完全可控，
 * 不会出现系统在深色底上叠黑字、彩色 ✓ emoji、或无法染色的 ▶ 三角。 */
typedef struct MenuItemDataS {
    wchar_t *text;      /* 纯文本（自绘项不使用系统存储的文本） */
    int isPopup;        /* MF_POPUP：右侧画 chevron */
    int isChecked;      /* 创建时的 MF_CHECKED：左侧画 √ */
} MenuItemData;

/* 列宽对齐系统浅色菜单：左侧勾选栏 + 文字与箭头之间的大留白 + 右内边距。
 * 数值按原生菜单实测（同字体下总宽多 22px），改小会比亮色菜单明显偏窄 */
#define MENU_CHECK_COL_W  32
#define MENU_ARROW_COL_W  40
#define MENU_RIGHT_PAD     6

/* 系统标准菜单字体（懒创建，全程序复用） */
static HFONT MenuFont(void)
{
    if (!g_menuFont) {
        NONCLIENTMETRICSW ncm;
        ncm.cbSize = sizeof(ncm);
        if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0))
            g_menuFont = CreateFontIndirectW(&ncm.lfMenuFont);
        if (!g_menuFont) g_menuFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    }
    return g_menuFont;
}

/* 追加普通项/子菜单项：暗色转成自绘项，亮色保持系统原生 */
static void MenuAdd(HMENU m, UINT flags, UINT_PTR id, const wchar_t *text)
{
    MenuItemData *md;
    if (!g_darkMode) {
        AppendMenuW(m, flags | MF_STRING, id, text);
        return;
    }
    md = calloc(1, sizeof(*md));
    if (!md) {
        AppendMenuW(m, flags | MF_STRING, id, text);   /* 极端情况退回原生项 */
        return;
    }
    md->text = _wcsdup(text ? text : L"");
    md->isPopup = (flags & MF_POPUP) != 0;
    md->isChecked = (flags & MF_CHECKED) != 0;
    AppendMenuW(m, flags | MF_OWNERDRAW, id, (const wchar_t*)md);
}

static void MenuAddPopup(HMENU m, HMENU sub, const wchar_t *text, UINT flags)
{
    MenuAdd(m, MF_POPUP | (flags & (MF_CHECKED | MF_GRAYED)), (UINT_PTR)sub, text);
}

static void MenuAddSep(HMENU m)
{
    AppendMenuW(m, MF_SEPARATOR | (g_darkMode ? MF_OWNERDRAW : 0), 0, NULL);
}

/* 递归释放自绘项数据 */
static void ThemeMenuCleanup(HMENU m)
{
    int i, n;
    n = GetMenuItemCount(m);
    for (i = 0; i < n; i++) {
        MENUITEMINFOW mi;
        ZeroMemory(&mi, sizeof(mi));
        mi.cbSize = sizeof(mi);
        mi.fMask = MIIM_DATA | MIIM_SUBMENU;
        if (!GetMenuItemInfoW(m, i, TRUE, &mi)) continue;
        if (mi.dwItemData) {
            MenuItemData *md = (MenuItemData*)(ULONG_PTR)mi.dwItemData;
            free(md->text);
            free(md);
        }
        if (mi.hSubMenu) ThemeMenuCleanup(mi.hSubMenu);
    }
}

/* 暗色菜单背景：填满菜单窗口和项之间的间隙，MIM_APPLYTOSUBMENUS 同时作用于子菜单 */
static HBRUSH ApplyMenuTheme(HMENU m)
{
    HBRUSH bg;
    MENUINFO mi;
    if (!g_darkMode) return NULL;
    bg = CreateSolidBrush(ThPanel());
    ZeroMemory(&mi, sizeof(mi));
    mi.cbSize = sizeof(mi);
    mi.fMask = MIM_BACKGROUND | MIM_APPLYTOSUBMENUS;
    mi.hbrBack = bg;
    SetMenuInfo(m, &mi);
    return bg;
}

/* 自绘菜单项尺寸：行高与浅色系统菜单一致（更紧凑），列宽留足勾选/箭头 */
static void MenuMeasureItem(HWND owner, MEASUREITEMSTRUCT *mi)
{
    HDC hdc;
    HFONT old;
    MenuItemData *md;
    TEXTMETRICW tm;
    SIZE sz;

    if (!mi->itemData) {          /* 分隔线 */
        mi->itemHeight = 8;
        mi->itemWidth  = 0;
        return;
    }
    md = (MenuItemData*)(ULONG_PTR)mi->itemData;
    hdc = GetDC(owner);
    old = (HFONT)SelectObject(hdc, MenuFont());
    GetTextMetricsW(hdc, &tm);
    GetTextExtentPoint32W(hdc, md->text, (int)wcslen(md->text), &sz);
    mi->itemHeight = tm.tmHeight + 6;
    mi->itemWidth  = sz.cx + MENU_CHECK_COL_W + MENU_ARROW_COL_W + MENU_RIGHT_PAD;
    SelectObject(hdc, old);
    ReleaseDC(owner, hdc);
}

/* 勾选符号：1px 细线 √，尺寸与系统浅色菜单的 Marlett 勾一致（不粗不大） */
static void MenuDrawCheck(HDC hdc, int cx, int cy, COLORREF clr)
{
    HPEN p = CreatePen(PS_SOLID, 1, clr);
    HGDIOBJ old = SelectObject(hdc, p);
    MoveToEx(hdc, cx - 4, cy, NULL);
    LineTo(hdc, cx - 1, cy + 3);
    LineTo(hdc, cx + 4, cy - 4);
    SelectObject(hdc, old);
    DeleteObject(p);
}

/* 子菜单箭头：单像素 chevron，两臂逐点镜像（同简介折叠按钮）。
 * LineTo 的斜线像素归一化会让上下臂不等长，所以改用 SetPixel */
static void MenuDrawChevron(HDC hdc, int x, int cy, COLORREF clr)
{
    int vx = x + 4, i;
    for (i = 0; i <= 4; i++) {
        SetPixel(hdc, vx - i, cy - i, clr);
        SetPixel(hdc, vx - i, cy + i, clr);
    }
}

static void MenuDrawItem(const DRAWITEMSTRUCT *di)
{
    RECT rc = di->rcItem;
    MenuItemData *md = di->itemData ? (MenuItemData*)(ULONG_PTR)di->itemData : NULL;
    int isSel  = (di->itemState & (ODS_SELECTED | ODS_HOTLIGHT)) != 0;
    int isGray = (di->itemState & ODS_GRAYED) != 0;
    int isChk  = (md && md->isChecked) || (di->itemState & ODS_CHECKED) != 0;
    int cy = (rc.top + rc.bottom) / 2;
    COLORREF fg = isGray ? ThDim() : ThText();

    FillRectColor(di->hDC, &rc, isSel ? ThSel() : ThPanel());
    if (!md) {   /* 分隔线 */
        HPEN p = CreatePen(PS_SOLID, 1, ThDim());
        HGDIOBJ old = SelectObject(di->hDC, p);
        MoveToEx(di->hDC, rc.left + MENU_RIGHT_PAD, cy, NULL);
        LineTo(di->hDC, rc.right - MENU_RIGHT_PAD, cy);
        SelectObject(di->hDC, old);
        DeleteObject(p);
        return;
    }
    if (isChk) MenuDrawCheck(di->hDC, rc.left + MENU_CHECK_COL_W / 2, cy, fg);
    if (md->isPopup) MenuDrawChevron(di->hDC, rc.right - MENU_RIGHT_PAD - 9, cy, fg);
    SetBkMode(di->hDC, TRANSPARENT);
    SetTextColor(di->hDC, fg);
    {
        RECT trc = { rc.left + MENU_CHECK_COL_W, rc.top,
                     rc.right - MENU_ARROW_COL_W, rc.bottom };
        HFONT old = (HFONT)SelectObject(di->hDC, MenuFont());
        DrawTextW(di->hDC, md->text, -1, &trc,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(di->hDC, old);
    }
    if (md->isPopup) {
        /* 系统会在 WM_DRAWITEM 返回后再补画一个原生 ▶ 箭头（颜色不可控）：
         * 画完后把右缘区域从裁剪区排除，系统箭头就被裁掉，只留我们的 chevron */
        ExcludeClipRect(di->hDC, rc.right - MENU_ARROW_COL_W, rc.top,
                        rc.right + 32, rc.bottom);
    }
}

static void OnSettingsButton(void)
{
    HMENU menu = CreatePopupMenu();
    HMENU sub  = CreatePopupMenu();
    HMENU lang = CreatePopupMenu();
    HMENU exp  = CreatePopupMenu();
    HMENU cm   = CreatePopupMenu();
    POINT pt;
    int cmd;
    wchar_t curDir[MAX_PATH];
    wchar_t fontItem[32];

    static const int mins[] = { 15, 30, 45, 60, 90, 120 };
    static const int nMins = sizeof(mins) / sizeof(mins[0]);
    int i;

    MenuAdd(menu, 0, 1, tr(TR_M_OPEN_CACHE));
    MenuAdd(menu, 0, 2, tr(TR_M_CHANGE_CACHE));
    MenuAdd(menu, 0, 3, tr(TR_M_DEFAULT_CACHE));
    {
        /* 自动清理：关 / 7 / 30 / 90 天，单选勾选 */
        static const int days[] = { 7, 30, 90 };
        HMENU cl = CreatePopupMenu();
        int di;
        MenuAdd(cl, g_cacheDays == 0 ? MF_CHECKED : 0, 30, tr(TR_CC_OFF));
        for (di = 0; di < 3; di++) {
            wchar_t t[32];
            swprintf(t, 32, tr(TR_CC_DAYS_FMT), days[di]);
            MenuAdd(cl, g_cacheDays == days[di] ? MF_CHECKED : 0, 31 + di, t);
        }
        MenuAddPopup(menu, cl, tr(TR_M_CACHE_CLEAN), 0);
    }
    MenuAddSep(menu);
    MenuAdd(menu, 0, 22, tr(TR_M_IMPORT));
    MenuAdd(exp, 0, 23, tr(TR_M_EXP_OPML));
    MenuAdd(exp, 0, 24, tr(TR_M_EXP_INI));
    MenuAddPopup(menu, exp, tr(TR_M_EXPORT), 0);
    MenuAddSep(menu);
    MenuAdd(menu, g_darkMode ? MF_CHECKED : 0, 4, tr(TR_M_NIGHT));
    MenuAdd(lang, g_lang == 0 ? MF_CHECKED : 0, 20, tr(TR_LANG_ZH));
    MenuAdd(lang, g_lang == 1 ? MF_CHECKED : 0, 21, tr(TR_LANG_EN));
    MenuAddPopup(menu, lang, tr(TR_M_LANGUAGE), 0);
    MenuAdd(cm, g_cfgExternal ? 0 : MF_CHECKED, 25, tr(TR_M_CFG_EMB));
    MenuAdd(cm, g_cfgExternal ? MF_CHECKED : 0, 26, tr(TR_M_CFG_EXT));
    MenuAddPopup(menu, cm, tr(TR_M_CFGMODE), 0);
    for (i = 0; i < nMins; i++) {
        wchar_t t[32];
        swprintf(t, 32, tr(TR_SHUT_IN_MINS_FMT), mins[i]);
        MenuAdd(sub, 0, 100 + mins[i], t);
    }
    MenuAddSep(sub);
    MenuAdd(sub, 0, 8, tr(TR_M_CUSTOM));
    MenuAddSep(sub);
    MenuAdd(sub, g_shutdownAt ? MF_CHECKED : (MF_GRAYED | MF_CHECKED), 5, tr(TR_M_SHUT_CANCEL));
    /* 定时关机生效时，父项左侧也显示 √（与其它多选项一致） */
    MenuAddPopup(menu, sub, tr(TR_M_SHUTDOWN), g_shutdownAt ? MF_CHECKED : 0);
    swprintf(fontItem, 32, tr(TR_M_FONT_FMT),
        g_fontLevel == 0 ? tr(TR_FONT_S) : g_fontLevel == 1 ? tr(TR_FONT_M) : tr(TR_FONT_L));
    MenuAdd(menu, 0, 6, fontItem);
    MenuAddSep(menu);
    MenuAdd(menu, g_httpThread ? MF_CHECKED : 0, 9, tr(TR_M_PHONE));
    MenuAdd(menu, 0, 7, tr(TR_M_ABOUT));

    GetCursorPos(&pt);
    {
        HBRUSH bg = ApplyMenuTheme(menu);
        cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY,
            pt.x, pt.y, 0, g_hwnd, NULL);
        ThemeMenuCleanup(menu);
        if (bg) DeleteObject(bg);
    }
    DestroyMenu(menu);   /* 级联销毁子菜单 */
    if (cmd == 0) return;

    /* 外观/语言/定时类命令先处理，不涉及缓存目录 */
    if (cmd == 22) { DoImport(); return; }
    if (cmd == 23) { DoExportOpml(); return; }
    if (cmd == 24) { DoExportIni(); return; }
    if (cmd == 25) { CfgSwitchMode(0); return; }
    if (cmd == 26) { CfgSwitchMode(1); return; }
    if (cmd >= 30 && cmd <= 33) {   /* 自动清理缓存：关/7/30/90 天，退出时随配置保存 */
        wchar_t t[64], d[32];
        static const int vals[] = { 0, 7, 30, 90 };
        g_cacheDays = vals[cmd - 30];
        if (g_cacheDays > 0) swprintf(d, 32, tr(TR_CC_DAYS_FMT), g_cacheDays);
        else wcscpy_s(d, 32, tr(TR_CC_OFF));
        swprintf(t, 64, tr(TR_CC_SET_FMT), d);
        SetStatusTemp(t);
        return;
    }
    if (cmd == 4) {
        g_darkMode = !g_darkMode;
        ApplyTheme();
        SetStatusTemp(g_darkMode ? tr(TR_NIGHT_ON) : tr(TR_NIGHT_OFF));
        return;
    }
    if (cmd == 20 || cmd == 21) {
        int nl = (cmd == 21) ? 1 : 0;
        if (nl != g_lang) { g_lang = nl; ApplyLanguage(); }
        return;
    }
    if (cmd == 9) {
        if (g_httpThread) {
            PhoneServerStop();
            SetStatusTemp(tr(TR_PHONE_OFF));
        } else {
            int r = PhoneServerStart();
            if (r) {
                wchar_t url[64];
                /* 自动在本机浏览器打开，地址列表（WiFi/以太网）在网页里显示 */
                swprintf(url, 64, L"http://localhost:%u/", g_httpPort);
                ShellExecuteW(g_hwnd, L"open", url, NULL, NULL, SW_SHOWNORMAL);
                SetStatusTemp(tr(TR_PHONE_ON));
                /* 服务开了但防火墙没放行：本机页面正常，手机会一直转圈 */
                if (r == 2)
                    MessageBoxTh(g_hwnd, tr(TR_PHONE_FW), tr(TR_PHONE_TITLE),
                                MB_OK | MB_ICONWARNING);
            } else {
                MessageBoxTh(g_hwnd, tr(TR_PHONE_FAIL), tr(TR_PHONE_TITLE), MB_OK | MB_ICONERROR);
            }
        }
        return;
    }
    if (cmd == 7) {
        MessageBoxTh(g_hwnd, tr(TR_ABOUT_TEXT), tr(TR_M_ABOUT),
                    MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (cmd == 5) { SetShutdownTimer(0); return; }
    if (cmd == 8) { AskCustomShutdown(); return; }
    if (cmd >= 100) { SetShutdownTimer(cmd - 100); return; }
    if (cmd == 6) {
        ChangeFontLevel((g_fontLevel + 1) % 3);
        SetStatusTemp(tr(TR_FONT_SWITCHED));
        return;
    }

    CacheDir(curDir, MAX_PATH);   /* 保留末尾反斜杠：CountMp3InDir/移动都需要它 */

    if (cmd == 1) {
        wchar_t openDir[MAX_PATH];
        wcsncpy(openDir, curDir, MAX_PATH);
        openDir[MAX_PATH - 1] = 0;
        openDir[wcslen(openDir) - 1] = 0;   /* 浏览时去掉末尾反斜杠 */
        ShellExecuteW(g_hwnd, L"open", openDir, NULL, NULL, SW_SHOWNORMAL);
    } else if (cmd == 2) {
        BROWSEINFOW bi;
        LPITEMIDLIST pidl;
        wchar_t newPath[MAX_PATH] = L"";
        ZeroMemory(&bi, sizeof(bi));
        bi.hwndOwner = g_hwnd;
        bi.lpszTitle = tr(TR_BROWSE_CACHE);
        bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
        pidl = SHBrowseForFolderW(&bi);
        if (pidl && SHGetPathFromIDListW(pidl, newPath)) {
            int oldHasFiles = CountMp3InDir(curDir);   /* curDir 带反斜杠 */
            wchar_t newDir[MAX_PATH];
            swprintf(newDir, MAX_PATH, L"%ls\\", newPath);
            /* 新旧相同就不必移动 */
            if (oldHasFiles > 0 &&
                _wcsicmp(curDir, newDir) != 0) {
                wchar_t msg[256];
                swprintf(msg, 256, tr(TR_MOVE_Q_NEW_FMT), oldHasFiles);
                if (MessageBoxTh(g_hwnd, msg, tr(TR_MOVE_CACHE), MB_YESNO | MB_ICONQUESTION) == IDYES) {
                    CreateDirectoryW(newPath, NULL);
                    MoveCacheFiles(curDir, newDir, L"mp3");
                }
            }
            wcsncpy(g_cacheDir, newPath, MAX_PATH - 1);
            g_cacheDir[MAX_PATH - 1] = 0;
            EnsureCacheDir();
            SetStatusTemp(tr(TR_CACHE_CHANGED));
        }
        if (pidl) CoTaskMemFree(pidl);
    } else if (cmd == 3) {
        /* 恢复默认目录：先清空自定义设置，获取真正的默认目录（%TEMP%\LiteTune\）。
         * 旧目录（恢复前的 curDir）里有文件时询问是否移到新默认目录。 */
        wchar_t defDir[MAX_PATH];
        g_cacheDir[0] = 0;
        CacheDir(defDir, MAX_PATH);          /* 现在返回真正的默认目录 */
        if (CountMp3InDir(curDir) > 0 && _wcsicmp(curDir, defDir) != 0) {
            wchar_t msg[256];
            swprintf(msg, 256, tr(TR_MOVE_Q_DEF_FMT), CountMp3InDir(curDir));
            if (MessageBoxTh(g_hwnd, msg, tr(TR_MOVE_CACHE), MB_YESNO | MB_ICONQUESTION) == IDYES) {
                EnsureCacheDir();            /* 确保默认目录已创建 */
                MoveCacheFiles(curDir, defDir, L"mp3");
            }
        }
        EnsureCacheDir();
        SetStatusTemp(tr(TR_CACHE_RESET));
    }

    /* 目录变了：刷新“已缓存”虚拟行；正在看离线列表则重建内容并保持选中 */
    if (cmd == 2 || cmd == 3) {
        RefreshCachedEntry();
        if (g_viewCached) {
            int sel = (int)SendMessageW(g_listPod, LB_GETCURSEL, 0, 0);
            FreeCachePodcast();
            LoadCachePodcast();
            FillEpisodeList();
            if (sel < 0) sel = g_podCount;
            SendMessageW(g_listPod, LB_SETCURSEL, sel, 0);
        }
    }
}

/* ---- 托盘左键迷你控制浮窗 ---- */
/* 4 个按钮命令与任务栏缩略图工具栏完全一致（tid 0..3），
 * 点击时以 THBN_CLICKED 转发回主窗口同一分支处理 */
static LRESULT CALLBACK FlyoutProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_DRAWITEM: {
        DRAWITEMSTRUCT *di = (DRAWITEMSTRUCT *)lp;
        int kind;
        if (di->CtlType != ODT_BUTTON) break;
        FillRectColor(di->hDC, &di->rcItem,
            (di->itemState & ODS_SELECTED) ? RGB(96, 96, 112)
            : ((int)di->CtlID == g_flyHover ? RGB(64, 64, 74) : RGB(45, 45, 52)));
        kind = (di->CtlID == 0) ? ICO_PREV
             : (di->CtlID == 1) ? ((g_playState == 1) ? ICO_PAUSE : ICO_PLAY)
             : (di->CtlID == 2) ? ICO_NEXT : ICO_SHUFFLE;
        DrawGlyphC(di->hDC, &di->rcItem, kind, RGB(255, 255, 255));
        return TRUE;
    }
    case WM_COMMAND:
        PostMessageW(g_hwnd, WM_COMMAND,
            MAKEWPARAM(LOWORD(wp), THBN_CLICKED), 0);
        return 0;
    case WM_MOUSEMOVE: {
        /* 悬停高亮：按固定网格算出按钮下标 */
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        int id = -1;
        if (pt.x >= FLY_PAD && pt.x < FLY_PAD + FLY_BTN * 4 + FLY_GAP * 3 &&
            pt.y >= FLY_PAD && pt.y < FLY_PAD + FLY_BTN) {
            id = (pt.x - FLY_PAD) / (FLY_BTN + FLY_GAP);
            if (id > 3) id = 3;
        }
        if (id != g_flyHover) {
            g_flyHover = id;
            InvalidateRect(hwnd, NULL, TRUE);
        }
        {
            TRACKMOUSEEVENT tme;
            tme.cbSize = sizeof(tme);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hwnd;
            tme.dwHoverTime = 0;
            TrackMouseEvent(&tme);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        if (g_flyHover != -1) {
            g_flyHover = -1;
            InvalidateRect(hwnd, NULL, TRUE);
        }
        return 0;
    case WM_ACTIVATE:
        /* 点击浮窗以外的地方→失去激活→自动收起。
         * 不能用 WM_KILLFOCUS：点击窗内按钮时焦点会转到子控件，父窗随之收到
         * KILLFOCUS 而立即隐藏，导致 4 个按钮「按下去没反应」。 */
        if (LOWORD(wp) == WA_INACTIVE)
            ShowWindow(hwnd, SW_HIDE);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void CreateFlyout(HINSTANCE hInst)
{
    int i;
    g_fly = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
        FLY_CLASS, NULL, WS_POPUP | WS_BORDER,
        0, 0, FLY_W, FLY_H, NULL, NULL, hInst, NULL);
    if (!g_fly) return;
    for (i = 0; i < 4; i++)
        CreateWindowExW(0, L"BUTTON", NULL,
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            FLY_PAD + i * (FLY_BTN + FLY_GAP), FLY_PAD,
            FLY_BTN, FLY_BTN, g_fly, (HMENU)(INT_PTR)i, hInst, NULL);
}

/* 在托盘图标正上方弹出浮窗（夹到屏幕工作区内） */
static void ShowFlyout(void)
{
    POINT pt;
    RECT wr;
    HMONITOR mon;
    MONITORINFO mi;
    if (!g_fly) return;
    g_flyHover = -1;
    GetCursorPos(&pt);
    wr.left = pt.x - FLY_W / 2;
    wr.top = pt.y - FLY_H - 8;      /* 光标上方 8px */
    wr.right = wr.left + FLY_W;
    wr.bottom = wr.top + FLY_H;
    mi.cbSize = sizeof(mi);
    mon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    if (GetMonitorInfoW(mon, &mi)) {
        if (wr.left < mi.rcWork.left) {
            wr.right += mi.rcWork.left - wr.left; wr.left = mi.rcWork.left;
        }
        if (wr.right > mi.rcWork.right) {
            wr.left -= wr.right - mi.rcWork.right; wr.right = mi.rcWork.right;
        }
        if (wr.top < mi.rcWork.top) {
            wr.bottom += mi.rcWork.top - wr.top; wr.top = mi.rcWork.top;
        }
        if (wr.bottom > mi.rcWork.bottom) {
            wr.top -= wr.bottom - mi.rcWork.bottom; wr.bottom = mi.rcWork.bottom;
        }
    }
    SetWindowPos(g_fly, HWND_TOPMOST, wr.left, wr.top, 0, 0,
        SWP_NOSIZE | SWP_SHOWWINDOW);
    /* 接管前台：点击浮窗外时收到 WM_ACTIVATE(WA_INACTIVE) 自动隐藏 */
    SetForegroundWindow(g_fly);
}

/* ---- 任务栏缩略图工具栏（ITaskbarList3） ---- */
/* 任务栏/开始菜单是否为浅色（SystemUsesLightTheme=1）。缩略图工具栏按钮由
 * 任务栏绘制底色，浅色下必须用深色图标，否则白图标几乎看不见。 */
static int TaskbarIsLight(void)
{
    DWORD v = 0, sz = sizeof(v);
    if (RegGetValueW(HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
            L"SystemUsesLightTheme", RRF_RT_REG_DWORD, NULL, &v, &sz) == ERROR_SUCCESS)
        return v != 0;
    return 0;   /* 读不到（如 Win7）按深色处理 */
}

/* 在 16x16 画布上画满幅的简洁线性图标（图形区约 14x14），颜色随任务栏主题 */
static void DrawTbGlyph(HDC hdc, int kind)
{
    COLORREF col = TaskbarIsLight() ? RGB(28, 28, 28) : RGB(255, 255, 255);
    HBRUSH wh = CreateSolidBrush(col);
    HPEN p2 = CreatePen(PS_SOLID, 2, col);
    HGDIOBJ ob = SelectObject(hdc, wh);
    HGDIOBJ op = SelectObject(hdc, p2);

    switch (kind) {
    case ICO_PLAY: {
        POINT t[3] = { {3,2},{3,14},{13,8} };
        Polygon(hdc, t, 3);
        break;
    }
    case ICO_PAUSE:
        Rectangle(hdc, 3, 2, 7, 14);
        Rectangle(hdc, 9, 2, 13, 14);
        break;
    case ICO_PREV: {
        POINT t[3] = { {12,2},{4,8},{12,14} };
        Rectangle(hdc, 1, 3, 4, 13);
        Polygon(hdc, t, 3);
        break;
    }
    case ICO_NEXT: {
        POINT t[3] = { {4,2},{12,8},{4,14} };
        Polygon(hdc, t, 3);
        Rectangle(hdc, 12, 3, 15, 13);
        break;
    }
    case ICO_SHUFFLE: {
        /* 两条交叉折线 + 右向箭头 */
        POINT ah[3] = { {11,9},{11,15},{15,12} };
        MoveToEx(hdc, 2, 4, NULL);
        LineTo(hdc, 7, 4); LineTo(hdc, 12, 12);
        MoveToEx(hdc, 2, 12, NULL);
        LineTo(hdc, 7, 12); LineTo(hdc, 12, 4);
        MoveToEx(hdc, 9, 12, NULL); LineTo(hdc, 14, 12);
        SelectObject(hdc, wh);
        Polygon(hdc, ah, 3);
        break;
    }
    }
    SelectObject(hdc, ob);
    SelectObject(hdc, op);
    DeleteObject(wh);
    DeleteObject(p2);
}

/* 创建 16x16 真彩带 alpha 的图标：背景全透明，图形不透明白色。
 * GDI 不写 alpha 通道，画完后扫描像素，把有色像素的 A 补成 255。 */
static HICON CreateTbIcon(int kind)
{
    HDC sc = GetDC(NULL), cdc, mdc;
    HBITMAP cb, mb, ob1, ob2;
    HICON ico;
    BITMAPINFO bi;
    unsigned char *bits;
    RECT rc = { 0, 0, 16, 16 };
    ICONINFO ii;
    int i;

    ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = 16;
    bi.bmiHeader.biHeight = -16;          /* 自上而下 */
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    cb = CreateDIBSection(sc, &bi, DIB_RGB_COLORS, (void**)&bits, NULL, 0);
    cdc = CreateCompatibleDC(sc);
    ob1 = (HBITMAP)SelectObject(cdc, cb);
    DrawTbGlyph(cdc, kind);
    /* 补 alpha：BGRA 中任一通道非 0 即图形像素 */
    for (i = 0; i < 16 * 16; i++) {
        if (bits[i*4] | bits[i*4+1] | bits[i*4+2])
            bits[i*4+3] = 255;
    }
    SelectObject(cdc, ob1);

    /* AND 遮罩全 0：整块都用彩色位图（含 alpha 混合） */
    mb = CreateBitmap(16, 16, 1, 1, NULL);
    mdc = CreateCompatibleDC(sc);
    ob2 = (HBITMAP)SelectObject(mdc, mb);
    FillRect(mdc, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH));
    SelectObject(mdc, ob2);

    ii.fIcon = TRUE; ii.xHotspot = 0; ii.yHotspot = 0;
    ii.hbmMask = mb; ii.hbmColor = cb;
    ico = CreateIconIndirect(&ii);
    DeleteObject(mb); DeleteObject(cb);
    DeleteDC(cdc); DeleteDC(mdc);
    ReleaseDC(NULL, sc);
    return ico;
}

/* 响应 WM_TASKBARBUTTONCREATED：创建并挂接缩略图按钮。
 * Explorer 重启会重播此消息，先释放旧图标/COM 对象防泄漏 */
static void InitThumbbar(void)
{
    THUMBBUTTON tb[4];
    HRESULT hr;
    int i;
    if (!g_wmTaskbarBtnCreated) return;

    if (g_taskbar) { g_taskbar->lpVtbl->Release(g_taskbar); g_taskbar = NULL; }
    for (i = 0; i < 5; i++)
        if (g_tbIco[i]) { DestroyIcon(g_tbIco[i]); g_tbIco[i] = NULL; }

    hr = CoCreateInstance(&CLSID_TaskbarList, NULL, CLSCTX_INPROC_SERVER,
                          &IID_ITaskbarList3, (void**)&g_taskbar);
    if (FAILED(hr) || !g_taskbar) return;
    hr = g_taskbar->lpVtbl->HrInit(g_taskbar);
    if (FAILED(hr)) { g_taskbar->lpVtbl->Release(g_taskbar); g_taskbar = NULL; return; }

    g_tbIco[0] = CreateTbIcon(ICO_PREV);
    g_tbIco[1] = CreateTbIcon(ICO_PLAY);
    g_tbIco[2] = CreateTbIcon(ICO_PAUSE);
    g_tbIco[3] = CreateTbIcon(ICO_NEXT);
    g_tbIco[4] = CreateTbIcon(ICO_SHUFFLE);

    ZeroMemory(tb, sizeof(tb));
    tb[0].dwMask = THB_ICON | THB_TOOLTIP | THB_FLAGS;
    tb[0].iId = 0; tb[0].hIcon = g_tbIco[0];
    wcscpy_s(tb[0].szTip, 260, tr(TR_TB_PREV));
    tb[0].dwFlags = THBF_ENABLED;

    tb[1].dwMask = THB_ICON | THB_TOOLTIP | THB_FLAGS;
    tb[1].iId = 1; tb[1].hIcon = g_tbIco[1];
    wcscpy_s(tb[1].szTip, 260, tr(TR_TB_PAUSE));
    tb[1].dwFlags = THBF_ENABLED;

    tb[2].dwMask = THB_ICON | THB_TOOLTIP | THB_FLAGS;
    tb[2].iId = 2; tb[2].hIcon = g_tbIco[3];
    wcscpy_s(tb[2].szTip, 260, tr(TR_TB_NEXT));
    tb[2].dwFlags = THBF_ENABLED;

    tb[3].dwMask = THB_ICON | THB_TOOLTIP | THB_FLAGS;
    tb[3].iId = 3; tb[3].hIcon = g_tbIco[4];
    wcscpy_s(tb[3].szTip, 260, tr(TR_TB_SHUFFLE));
    tb[3].dwFlags = THBF_ENABLED;

    g_taskbar->lpVtbl->ThumbBarAddButtons(g_taskbar, g_hwnd, 4, tb);
}

/* 播放/暂停状态切换时更新缩略图按钮图标 */
static void UpdateThumbbarPlayPause(void)
{
    THUMBBUTTON tb;
    if (!g_taskbar) return;
    ZeroMemory(&tb, sizeof(tb));
    tb.dwMask = THB_ICON | THB_TOOLTIP;
    tb.iId = 1;
    tb.hIcon = g_tbIco[g_playState == 1 ? 2 : 1];   /* 播放中显示暂停图标 */
    wcscpy_s(tb.szTip, 260, g_playState == 1 ? tr(TR_TB_PAUSE) : tr(TR_TB_PLAY));
    g_taskbar->lpVtbl->ThumbBarUpdateButtons(g_taskbar, g_hwnd, 1, &tb);
    if (g_fly) InvalidateRect(g_fly, NULL, TRUE);   /* 浮窗播放/暂停图标同步 */
}

/* ================= 工具提示 ================= */
/* 给按钮加一条固定文本的系统 tooltip。
 * TTF_SUBCLASS 让 tooltip 自己子类化控件窗口拦截鼠标消息；
 * 本程序的 PanelProc 是更早安装的外层子类，二者通过
 * SetWindowSubclass/CallWindowProc 链式共存，互不影响。 */
static void AddButtonTip(HWND btn, const wchar_t *text)
{
    TOOLINFOW ti;
    if (!g_hwndTip || !btn) return;
    ZeroMemory(&ti, sizeof(ti));
    ti.cbSize = sizeof(ti);
    ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
    ti.hwnd = GetParent(btn);
    ti.uId = (UINT_PTR)btn;
    ti.lpszText = (LPWSTR)text;
    SendMessageW(g_hwndTip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
}

static const wchar_t* PlayModeTipText(void)
{
    return g_playMode == 1 ? tr(TR_PM_SHUFFLE) :
           g_playMode == 2 ? tr(TR_PM_REPEAT) :
           g_playMode == 3 ? tr(TR_PM_ONCE) : tr(TR_PM_ORDER);
}

/* 切换播放模式并刷新按钮图标 + 按钮 tooltip（不改动状态行） */
static void CyclePlayMode(void)
{
    g_playMode = (g_playMode + 1) % 4;
    InvalidateRect(g_btnPlayMode, NULL, TRUE);
    UpdateTipText(g_btnPlayMode, PlayModeTipText());
}

/* 播客自定义提示窗过程：左侧封面，右侧标题+作者 */
static LRESULT CALLBACK PodTipProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(h, &ps);
        RECT rc;
        int coverW = 128;
        GetClientRect(h, &rc);
        /* 深色主题：自绘背景/边框/文字，不再用系统 COLOR_INFOBK */
        {
            HBRUSH bg = CreateSolidBrush(ThPanel());
            HPEN bp = CreatePen(PS_SOLID, 1, ThDim());
            FillRect(hdc, &rc, bg);
            SelectObject(hdc, bp);
            SelectObject(hdc, GetStockObject(NULL_BRUSH));
            Rectangle(hdc, rc.left, rc.top, rc.right, rc.bottom);
            DeleteObject(bg); DeleteObject(bp);
        }
        if (g_tipPodIdx >= 0 && g_tipPodIdx < g_podCount) {
            Podcast *p = &g_pods[g_tipPodIdx];
            int pad = 10;
            int coverH = coverW;
            int textX  = pad + coverW + 14;
            int maxR   = rc.right - pad;
            RECT rci = { pad, pad, pad + coverW, pad + coverH };
            RECT rct = { textX, pad + 4, maxR, pad + 32 };
            RECT rca = { textX, pad + 34, maxR, pad + 60 };
            RECT rcd = { textX, pad + 62, maxR, rc.bottom - pad };
            /* 封面：保持比例绘制 */
            if (p->hImage) {
                BITMAP bm;
                HDC mem = CreateCompatibleDC(hdc);
                HBITMAP old = (HBITMAP)SelectObject(mem, p->hImage);
                GetObject(p->hImage, sizeof(bm), &bm);
                if (bm.bmWidth > 0 && bm.bmHeight > 0) {
                    double r1 = (double)coverW / bm.bmWidth;
                    double r2 = (double)coverW / bm.bmHeight;
                    double r = r1 < r2 ? r1 : r2;
                    int dw = (int)(bm.bmWidth * r + 0.5);
                    int dh = (int)(bm.bmHeight * r + 0.5);
                    int dx = rci.left + (coverW - dw) / 2;
                    int dy = rci.top + (coverW - dh) / 2;
                    FillRectColor(hdc, &rci, g_darkMode ? RGB(24,24,28) : RGB(245,245,245));
                    SetStretchBltMode(hdc, HALFTONE);
                    StretchBlt(hdc, dx, dy, dw, dh, mem, 0, 0, bm.bmWidth, bm.bmHeight, SRCCOPY);
                }
                SelectObject(mem, old);
                DeleteDC(mem);
            } else {
                FillRectColor(hdc, &rci, g_darkMode ? RGB(24,24,28) : RGB(245,245,245));
            }
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, ThText());
            SelectObject(hdc, g_fontBold);
            DrawTextW(hdc, p->title ? p->title : tr(TR_UNTITLED), -1, &rct,
                DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
            SetTextColor(hdc, ThDim());
            SelectObject(hdc, g_fontSmall);
            DrawTextW(hdc, p->author ? p->author : L"", -1, &rca,
                DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
            SetTextColor(hdc, ThDim());
            if (p->desc)
                DrawTextW(hdc, p->desc, -1, &rcd,
                    DT_LEFT | DT_WORDBREAK | DT_NOPREFIX | DT_END_ELLIPSIS);
        }
        EndPaint(h, &ps);
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

/* 根据鼠标在播客列表中的位置，更新并显示自定义播客提示 */
static void UpdatePodTipFromPoint(POINT pt)
{
    int idx;
    RECT irc;
    POINT screen;
    if (g_podMode != 0) { ShowWindow(g_hwndPodTip, SW_HIDE); return; }
    idx = (int)SendMessageW(g_listPod, LB_ITEMFROMPOINT, 0, MAKELPARAM(pt.x, pt.y));
    if (idx < 0 || idx >= g_podCount) { ShowWindow(g_hwndPodTip, SW_HIDE); return; }
    if (idx != g_tipPodIdx) {
        g_tipPodIdx = idx;
        InvalidateRect(g_hwndPodTip, NULL, TRUE);
    }
    /* 定位到条目右下方，避免遮挡文字 */
    SendMessageW(g_listPod, LB_GETITEMRECT, idx, (LPARAM)&irc);
    screen.x = irc.right; screen.y = irc.top;
    ClientToScreen(g_listPod, &screen);
    SetWindowPos(g_hwndPodTip, HWND_TOPMOST, screen.x + 6, screen.y, 0, 0,
        SWP_NOSIZE | SWP_NOACTIVATE);
    ShowWindow(g_hwndPodTip, SW_SHOWNA);
}

/* ================= 窗口过程 ================= */
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    /* 任务栏缩略图工具栏：Explorer 广播此消息后初始化 */
    if (msg == g_wmTaskbarBtnCreated) {
        InitThumbbar();
        return 0;
    }
    switch (msg) {
    case WM_SETTINGCHANGE:
        /* 系统在浅色/深色之间切换后重画缩略图工具栏图标，避免颜色对不上 */
        if (lParam && !wcscmp((const wchar_t *)lParam, L"ImmersiveColorSet"))
            InitThumbbar();
        break;
    case WM_THEMECHANGED:
        InitThumbbar();
        break;
    case WM_CREATE: {
        HINSTANCE hInst = ((LPCREATESTRUCTW)lParam)->hInstance;
        /* 不带 WS_VSCROLL：系统滚动条彻底不用，滚动条全部自绘 */
        DWORD lbStyle = WS_CHILD | WS_VISIBLE | WS_TABSTOP
                      | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | LBS_HASSTRINGS;

        g_font = CreateUiFont(8 + g_fontLevel, FW_NORMAL);
        g_fontBold = CreateUiFont(8 + g_fontLevel, FW_BOLD);
        g_fontSmall = CreateUiFont(7 + g_fontLevel, FW_NORMAL);
        g_whiteBrush = CreateSolidBrush(ThPanel());

        /* 第一行状态文本由 WM_PAINT 自绘跑马灯，这里不建 STATIC */
        g_sldSeek = CreateWindowExW(0, TRACKBAR_CLASSW, NULL,
            WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS,
            0,0,0,0, hwnd, (HMENU)IDC_SLD_SEEK, NULL, NULL);
        SendMessageW(g_sldSeek, TBM_SETRANGE, TRUE, MAKELONG(0, 1000));
        g_stTime = CreateWindowExW(0, L"STATIC", L"00:00 / 00:00",
            WS_CHILD | WS_VISIBLE | SS_RIGHT,
            0,0,0,0, hwnd, (HMENU)IDC_ST_TIME, NULL, NULL);

        /* 全部方形图标按钮 */
        {
            const int ids[12] = {
                IDC_BTN_PREV, IDC_BTN_PLAY, IDC_BTN_NEXT, IDC_BTN_STOP, IDC_BTN_MUTE,
                IDC_BTN_ADD, IDC_BTN_DEL, IDC_BTN_REFRESH, IDC_BTN_PLAYMODE,
                IDC_BTN_DIR, IDC_BTN_SPEED, IDC_BTN_CACHE };
            HWND *p[12] = {
                &g_btnPrev, &g_btnPlay, &g_btnNext, &g_btnStop, &g_btnMute,
                &g_btnAdd, &g_btnDel, &g_btnRefresh, &g_btnPlayMode,
                &g_btnDir, &g_btnSpeed, &g_btnCache };
            int bi;
            for (bi = 0; bi < 12; bi++)
                *p[bi] = CreateWindowExW(0, L"BUTTON", NULL,
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                    0,0,0,0, hwnd, (HMENU)(INT_PTR)ids[bi], NULL, NULL);
        }

        g_sldVol = CreateWindowExW(0, TRACKBAR_CLASSW, NULL,
            WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS,
            0,0,0,0, hwnd, (HMENU)IDC_SLD_VOL, NULL, NULL);
        SendMessageW(g_sldVol, TBM_SETRANGE, TRUE, MAKELONG(0, 100));
        SendMessageW(g_sldVol, TBM_SETPOS, TRUE, g_volume);
        {
            wchar_t vt[8];
            swprintf(vt, 8, L"%d", g_volume);
            g_stVol = CreateWindowExW(0, L"STATIC", vt,
                WS_CHILD | WS_VISIBLE | SS_LEFT,
                0,0,0,0, hwnd, (HMENU)IDC_ST_VOL, NULL, NULL);
        }
        g_stFormat = CreateWindowExW(0, L"STATIC", L"",
            WS_CHILD | WS_VISIBLE | SS_RIGHT,
            0,0,0,0, hwnd, (HMENU)IDC_ST_FORMAT, NULL, NULL);

        g_listPod = CreateWindowExW(0, L"LISTBOX", NULL,
            lbStyle | LBS_OWNERDRAWFIXED,
            0,0,0,0, hwnd, (HMENU)IDC_LIST_POD, NULL, NULL);
        /* 行高 54px 由 WM_MEASUREITEM 统一设置 */

        /* 剧集：ListView 报表视图（标题/日期/时长，横向可滚动，列头可排序）
         * WS_CLIPSIBLINGS：重绘时裁掉缓存按钮区域，否则列表会画到按钮上 */
        g_listEp = CreateWindowExW(0, WC_LISTVIEWW, NULL,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPSIBLINGS |
            LVS_REPORT | LVS_SHOWSELALWAYS,
            0,0,0,0, hwnd, (HMENU)IDC_LIST_EP, NULL, NULL);
        {
            const wchar_t *colTitles[3] = { tr(TR_COL_TITLE), tr(TR_COL_DATE), tr(TR_COL_DUR) };
            int i;
            for (i = 0; i < 3; i++) {
                LVCOLUMNW col;
                ZeroMemory(&col, sizeof(col));
                col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
                col.cx = g_colW[i];
                col.iSubItem = i;
                col.pszText = (wchar_t*)colTitles[i];
                SendMessageW(g_listEp, LVM_INSERTCOLUMNW, i, (LPARAM)&col);
            }
            ListView_SetExtendedListViewStyle(g_listEp,
                LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
            SendMessageW(g_listEp, LVM_SETBKCOLOR, 0, (LPARAM)ThPanel());
            SendMessageW(g_listEp, LVM_SETTEXTBKCOLOR, 0, (LPARAM)ThPanel());
        }

        g_editDesc = CreateWindowExW(0, L"EDIT", NULL,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPSIBLINGS |
            ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
            0,0,0,0, hwnd, (HMENU)IDC_EDIT_DESC, NULL, NULL);

        /* 自绘滚动条（唯一滚动条；剧集列表另有水平条） */
        g_vsPod  = CreateDarkScrollBar(hwnd, g_listPod,  0, 0, IDC_SB_POD);
        g_vsEp   = CreateDarkScrollBar(hwnd, g_listEp,   1, 0, IDC_SB_EP);
        g_hsEp   = CreateDarkScrollBar(hwnd, g_listEp,   1, 1, IDC_SB_EP_H);
        g_vsDesc = CreateDarkScrollBar(hwnd, g_editDesc, 2, 0, IDC_SB_DESC);

        /* 简介折叠按钮：悬停才显示，位置由 LayoutControls 每次布局写入 */
        g_btnDescTab = CreateWindowExW(0, DESC_TAB_CLASS, L"",
            WS_CHILD | WS_CLIPSIBLINGS,
            0, 0, 0, 0, hwnd, (HMENU)(INT_PTR)IDC_TAB_DESC, NULL, NULL);

        ApplyFontsToControls();

        /* 滑块子类化：修复滚轮方向 */
        g_oldSeekProc = (WNDPROC)SetWindowLongPtrW(g_sldSeek, GWLP_WNDPROC, (LONG_PTR)SliderProc);
        g_oldVolProc  = (WNDPROC)SetWindowLongPtrW(g_sldVol,  GWLP_WNDPROC, (LONG_PTR)SliderProc);
        /* 内容面板与按钮子类化：Ctrl+滚轮调字号 */
        SubclassForFontWheel(g_listPod);
        SubclassForFontWheel(g_listEp);
        /* 在 PanelProc 之上再叠一层拖拽多选（USERDATA 已被 PanelProc 占用，
         * 所以用独立的 g_oldListEpProc 变量保存） */
        g_oldListEpProc = (WNDPROC)SetWindowLongPtrW(g_listEp, GWLP_WNDPROC,
            (LONG_PTR)ListEpDragProc);
        SubclassForFontWheel(g_editDesc);
        SubclassForFontWheel(g_btnPrev);
        SubclassForFontWheel(g_btnPlay);
        SubclassForFontWheel(g_btnNext);
        SubclassForFontWheel(g_btnStop);
        SubclassForFontWheel(g_btnMute);
        SubclassForFontWheel(g_btnAdd);
        SubclassForFontWheel(g_btnDel);
        SubclassForFontWheel(g_btnRefresh);
        SubclassForFontWheel(g_btnPlayMode);
        SubclassForFontWheel(g_btnDir);
        SubclassForFontWheel(g_btnCache);

        SetTimer(hwnd, 1, 500, NULL);
        SetTimer(hwnd, 5, 150, NULL);   /* 简介折叠按钮的悬停探测 */

        /* 媒体键（全局热键）：播放/暂停、停止、上一曲、下一曲。
         * 失败不影响主功能（可能被其他程序占用） */
        RegisterHotKey(hwnd, 1, 0, VK_MEDIA_PLAY_PAUSE);
        RegisterHotKey(hwnd, 2, 0, VK_MEDIA_STOP);
        RegisterHotKey(hwnd, 3, 0, VK_MEDIA_NEXT_TRACK);
        RegisterHotKey(hwnd, 4, 0, VK_MEDIA_PREV_TRACK);

        /* 托盘图标：点最小化时收进来，左键弹控制浮窗，右键弹菜单 */
        {
            NOTIFYICONDATAW nid;
            ZeroMemory(&nid, sizeof(nid));
            nid.cbSize = sizeof(nid);
            nid.hWnd = hwnd;
            nid.uID = 1;
            nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
            nid.uCallbackMessage = WM_TRAYICON;
            nid.hIcon = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(1), IMAGE_ICON,
                GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON),
                LR_DEFAULTCOLOR);
            g_trayIco = nid.hIcon;
            wcscpy_s(nid.szTip, 128, L"LiteTune");
            g_trayAdded = Shell_NotifyIconW(NIM_ADD, &nid);
        }

        /* 托盘迷你控制浮窗（左键单击托盘图标弹出） */
        CreateFlyout(hInst);

        /* 系统工具提示：按钮 */
        g_hwndTip = CreateWindowExW(0, TOOLTIPS_CLASSW, NULL,
            WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
            CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
            hwnd, NULL, GetModuleHandleW(NULL), NULL);
        if (g_hwndTip) {
            SetWindowPos(g_hwndTip, HWND_TOPMOST, 0,0,0,0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            ThemeTipSubclass(g_hwndTip);
            AddButtonTip(g_btnPrev,   tr(TR_TIP_PREV));
            AddButtonTip(g_btnPlay,   tr(TR_TIP_PLAYPAUSE));
            AddButtonTip(g_btnNext,   tr(TR_TIP_NEXT));
            AddButtonTip(g_btnStop,   tr(TR_TIP_STOP));
            AddButtonTip(g_btnMute,   tr(TR_TIP_MUTE));
            AddButtonTip(g_btnAdd,    tr(TR_TIP_ADD));
            AddButtonTip(g_btnDel,    tr(TR_TIP_DEL));
            AddButtonTip(g_btnRefresh,tr(TR_TIP_REFRESH));
            AddButtonTip(g_btnPlayMode, PlayModeTipText());
            AddButtonTip(g_btnDir,    tr(TR_TIP_SETTINGS));
            AddButtonTip(g_btnSpeed,  tr(TR_TIP_SPEED));
            AddButtonTip(g_btnCache,   tr(TR_TIP_CACHE));
        }
        /* 剧集列表 tracking tooltip：不依赖标题截断，悬停即显示 */
        {
            TOOLINFOW ti;
            g_hwndEpTip = CreateWindowExW(0, TOOLTIPS_CLASSW, NULL,
                WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
                0, 0, 0, 0, hwnd, NULL, GetModuleHandleW(NULL), NULL);
            if (g_hwndEpTip) {
                SendMessageW(g_hwndEpTip, TTM_SETMAXTIPWIDTH, 0, 400);
                ZeroMemory(&ti, sizeof(ti));
                ti.cbSize = sizeof(ti);
                ti.uFlags = TTF_TRACK;
                ti.hwnd = g_listEp;
                ti.uId = (UINT_PTR)g_listEp;
                ti.lpszText = L"";
                SendMessageW(g_hwndEpTip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
                ThemeTipSubclass(g_hwndEpTip);
            }
        }

        /* 自定义播客提示窗（封面 + 标题 + 作者） */
        {
            WNDCLASSEXW wc; ZeroMemory(&wc, sizeof(wc));
            wc.cbSize = sizeof(wc);
            wc.lpfnWndProc = PodTipProc;
            wc.hInstance = GetModuleHandleW(NULL);
            wc.lpszClassName = L"LpPodTip";
            wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
            wc.hbrBackground = (HBRUSH)(COLOR_INFOBK + 1);
            RegisterClassExW(&wc);
            g_hwndPodTip = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST,
                L"LpPodTip", NULL, WS_POPUP, 0,0, 440, 160, hwnd, NULL, GetModuleHandleW(NULL), NULL);
        }
        return 0;
    }

    case WM_MEASUREITEM: {
        MEASUREITEMSTRUCT *mi = (MEASUREITEMSTRUCT*)lParam;
        if (mi->CtlType == ODT_MENU) {
            MenuMeasureItem(hwnd, mi);
            return TRUE;
        }
        if (mi->CtlID == IDC_LIST_POD) {
            mi->itemHeight = 54;
            return TRUE;
        }
        break;
    }

    case WM_DRAWITEM: {
        DRAWITEMSTRUCT *di = (DRAWITEMSTRUCT*)lParam;
        if (di->CtlType == ODT_MENU) {
            MenuDrawItem(di);
            return TRUE;
        }
        if (di->CtlType == ODT_BUTTON) {
            /* 倍速按钮：文字按钮，显示 "1.0x" 等 */
            if (di->CtlID == IDC_BTN_SPEED) {
                RECT rc = di->rcItem;
                HFONT oldf;
                wchar_t txt[16];
                FillRectColor(di->hDC, &rc, ThWinBg());
                if (di->itemState & ODS_SELECTED)
                    DrawEdge(di->hDC, &rc, EDGE_SUNKEN, BF_RECT);
                else
                    DrawEdge(di->hDC, &rc, EDGE_RAISED, BF_RECT);
                SpeedText(txt, 16);
                SetBkMode(di->hDC, TRANSPARENT);
                SetTextColor(di->hDC, ThAccent());
                oldf = (HFONT)SelectObject(di->hDC, g_fontSmall);
                DrawTextW(di->hDC, txt, -1, &rc,
                    DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                SelectObject(di->hDC, oldf);
                return TRUE;
            }
            int on, kind = IconKindOf(di->CtlID, &on);
            if (kind >= 0) { DrawIconButton(di, kind, on); return TRUE; }
            break;
        }
        if (di->CtlID != IDC_LIST_POD || di->itemID == (UINT)-1) break;
        if ((int)di->itemID > g_podCount) break;
        {
            int isCache = ((int)di->itemID == g_podCount);
            Podcast *p = isCache ? &g_cachePod : &g_pods[di->itemID];
            int isSel = (di->itemState & ODS_SELECTED) != 0;
            int isPlaying = (g_playPod == (int)di->itemID && g_playState == 1);
            RECT rc = di->rcItem;
            int textLeft;

            FillRectColor(di->hDC, &rc, isSel ? ThSel() : ThPanel());

            /* 封面区：48x48，左留 2px、上留 3px。
             * 删除模式 → 大红叉按钮
             * "已缓存"虚拟行画一个下载缓存图标 */
            if (isCache) {
                /* "已缓存"虚拟行：柔和底色 + 下载箭头图标 */
                RECT ib = { rc.left + 2, rc.top + 3, rc.left + 50, rc.top + 51 };
                COLORREF bg = g_darkMode ? RGB(48,54,64) : RGB(236,241,248);
                COLORREF fg = g_darkMode ? RGB(120,170,230) : RGB(70,120,200);
                HPEN gp = CreatePen(PS_SOLID, 1, fg);
                HBRUSH ab = CreateSolidBrush(fg);
                HGDIOBJ oldPen, oldBrush;
                FillRectColor(di->hDC, &ib, bg);
                oldPen = SelectObject(di->hDC, gp);
                oldBrush = SelectObject(di->hDC, GetStockObject(NULL_BRUSH));
                RoundRect(di->hDC, ib.left+2, ib.top+2, ib.right-2, ib.bottom-2, 10, 10);
                /* 向下箭头（表示缓存到本地） */
                SelectObject(di->hDC, ab);
                SelectObject(di->hDC, GetStockObject(NULL_PEN));
                {
                    POINT tri[3] = {
                        {rc.left+18, rc.top+24}, {rc.left+34, rc.top+24},
                        {rc.left+26, rc.top+36} };
                    Polygon(di->hDC, tri, 3);
                }
                /* 箭头上方的竖线（下载中） */
                {
                    RECT tb = { rc.left+24, rc.top+14, rc.left+28, rc.top+25 };
                    Rectangle(di->hDC, tb.left, tb.top, tb.right, tb.bottom);
                }
                /* 底部托盘横线 */
                {
                    RECT bt = { rc.left+16, rc.top+38, rc.left+36, rc.top+41 };
                    Rectangle(di->hDC, bt.left, bt.top, bt.right, bt.bottom);
                }
                SelectObject(di->hDC, oldPen);
                SelectObject(di->hDC, oldBrush);
                DeleteObject(ab);
                DeleteObject(gp);
                textLeft = rc.left + 56;
            } else if (!isCache && p->loadFailed) {
                /* 加载失败占位：琥珀色警告三角（无灰色底框，直接画在面板上） */
                COLORREF warn = g_darkMode ? RGB(255,200,60) : RGB(200,140,0);
                HPEN wp = CreatePen(PS_SOLID, 2, warn);
                HBRUSH wb = CreateSolidBrush(warn);
                HGDIOBJ oldPen, oldBrush;
                oldPen = SelectObject(di->hDC, wp);
                oldBrush = SelectObject(di->hDC, GetStockObject(NULL_BRUSH));
                {
                    POINT tri[3] = {
                        {rc.left+26, rc.top+8},
                        {rc.left+44, rc.top+44},
                        {rc.left+8,  rc.top+44} };
                    Polygon(di->hDC, tri, 3);
                }
                /* 感叹号：竖条 + 圆点 */
                SelectObject(di->hDC, wb);
                SelectObject(di->hDC, GetStockObject(NULL_PEN));
                {
                    RECT tb = { rc.left+24, rc.top+20, rc.left+28, rc.top+33 };
                    Rectangle(di->hDC, tb.left, tb.top, tb.right, tb.bottom);
                }
                {
                    RECT td = { rc.left+23, rc.top+36, rc.left+29, rc.top+42 };
                    Ellipse(di->hDC, td.left, td.top, td.right, td.bottom);
                }
                SelectObject(di->hDC, oldPen);
                SelectObject(di->hDC, oldBrush);
                DeleteObject(wb);
                DeleteObject(wp);
                textLeft = rc.left + 56;
            } else if (g_podMode == 1) {
                RECT xb = { rc.left + 2, rc.top + 3, rc.left + 50, rc.top + 51 };
                HPEN rp = CreatePen(PS_SOLID, 5, RGB(205, 40, 40));
                HGDIOBJ oldp = SelectObject(di->hDC, rp);
                FillRect(di->hDC, &xb, (HBRUSH)GetStockObject(WHITE_BRUSH));
                DrawEdge(di->hDC, &xb, EDGE_RAISED, BF_RECT);
                MoveToEx(di->hDC, xb.left + 8, xb.top + 8, NULL);
                LineTo(di->hDC, xb.right - 8, xb.bottom - 8);
                MoveToEx(di->hDC, xb.right - 8, xb.top + 8, NULL);
                LineTo(di->hDC, xb.left + 8, xb.bottom - 8);
                SelectObject(di->hDC, oldp);
                DeleteObject(rp);
                textLeft = rc.left + 56;
            } else if (p->hImage) {
                BITMAP bm;
                HDC mem = CreateCompatibleDC(di->hDC);
                HBITMAP old = (HBITMAP)SelectObject(mem, p->hImage);
                GetObject(p->hImage, sizeof(bm), &bm);
                SetStretchBltMode(di->hDC, HALFTONE);
                StretchBlt(di->hDC, rc.left + 2, rc.top + 3, 48, 48,
                           mem, 0, 0, bm.bmWidth, bm.bmHeight, SRCCOPY);
                SelectObject(mem, old);
                DeleteDC(mem);
                textLeft = rc.left + 56;
            } else {
                textLeft = rc.left + 6;
            }

            SetBkMode(di->hDC, TRANSPARENT);

            /* 标题：加粗，播放中绿色；加载失败占位用固定文案+置灰 */
            {
                wchar_t title[64];
                const wchar_t *tp;
                RECT rct = rc;
                rct.left = textLeft;
                rct.right = rc.right - 28;
                rct.top += 3;
                rct.bottom = rct.top + 22;
                if (isCache) {
                    swprintf(title, 64, tr(TR_CACHED_FMT), g_cacheFileCount);
                    tp = title;
                } else if (p->loadFailed) {
                    tp = tr(TR_FEED_FAIL_TITLE);
                } else tp = p->title;
                SetTextColor(di->hDC,
                    p->loadFailed ? RGB(230,175,50) : (isPlaying ? ThGreen() : ThText()));
                SelectObject(di->hDC, g_fontBold);
                DrawTextW(di->hDC, tp, -1, &rct,
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            }

            /* 副标题：copyright（空则 author），小字号灰色，标题下方；
             * 虚拟行固定提示离线播放；失败占位显示原因说明 */
            {
                const wchar_t *sub = isCache
                    ? tr(TR_OFFLINE_LOCAL)
                    : (p->loadFailed ? tr(TR_FEED_FAIL_SUB)
                       : (p->copyright ? p->copyright : p->author));
                if (sub && *sub) {
                    RECT rcs = rc;
                    rcs.left = textLeft;
                    rcs.right = rc.right - 28;
                    rcs.top += 26;
                    rcs.bottom = rcs.top + 24;
                    SetTextColor(di->hDC, ThDim());
                    SelectObject(di->hDC, g_fontSmall);
                    DrawTextW(di->hDC, sub, -1, &rcs,
                        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
                }
            }

            /* 右侧：剧集数（小一号字体）；虚拟行显示缓存文件数 */
            {
                wchar_t cnt[16];
                RECT rc2 = di->rcItem;
                int n = isCache ? g_cacheFileCount : p->epCount;
                swprintf(cnt, 16, L"%d", n);
                SetTextColor(di->hDC, isSel ? ThText() : ThDim());
                SelectObject(di->hDC, g_fontSmall);
                rc2.left = rc2.right - 26;
                rc2.right -= 4;
                DrawTextW(di->hDC, cnt, -1, &rc2,
                    DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
            }

            /* 焦点虚线框 */
            if (di->itemState & ODS_FOCUS) DrawFocusRect(di->hDC, &di->rcItem);
        }
        return TRUE;
    }

    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED) {
            /* 最小化 = 收进托盘（点 × 才是退出） */
            ShowWindow(hwnd, SW_HIDE);
            return 0;
        }
        /* 这里不再写 g_winW/g_winH——SaveConfig 统一用 GetWindowRect 抓外框矩形 */
        ApplyLayout(hwnd, IsWindowVisible(hwnd));
        UpdateMarquee();   /* 可用宽度变化，重算跑马灯 */
        return 0;

    case WM_MOUSEWHEEL:
        /* 在播放区/按钮等空白处 Ctrl+滚轮也能调字号 */
        if (GET_KEYSTATE_WPARAM(wParam) & MK_CONTROL) {
            ChangeFontLevel(g_fontLevel + (GET_WHEEL_DELTA_WPARAM(wParam) > 0 ? 1 : -1));
            return 0;
        }
        break;

    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT) {
            DWORD pos = GetMessagePos();
            POINT pt = { GET_X_LPARAM(pos), GET_Y_LPARAM(pos) };
            ScreenToClient(hwnd, &pt);
            if (g_dragSplit >= 0 || SplitterHitTest(hwnd, pt.x, pt.y) >= 0) {
                SetCursor(LoadCursorW(NULL, IDC_SIZEWE));
                return TRUE;
            }
        }
        break;

    case WM_LBUTTONDOWN: {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        int s = SplitterHitTest(hwnd, pt.x, pt.y);
        if (s >= 0) {
            g_dragSplit = s;
            SetCapture(hwnd);
            return 0;
        }
        SetFocus(hwnd);   /* 点击空白处焦点回主窗口：方向键快捷键生效 */
        break;
    }

    case WM_MOUSEMOVE:
        if (g_dragSplit >= 0) {
            RECT rc;
            int totalW, w1px, w2px, w3px;
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            GetClientRect(hwnd, &rc);
            totalW = rc.right - 2*MARGIN - 2*GAP_W;
            PanelWidthsPx(totalW, &w1px, &w2px, &w3px);
            (void)w3px;
            if (g_dragSplit == 0) {
                w1px = pt.x - MARGIN - GAP_W/2;
                if (w1px < SPLIT_MINPX) w1px = SPLIT_MINPX;
                /* 简介隐藏时剧集面板吃满剩余宽度，上限按总宽算；
                 * 正常模式下要给简介面板留出最小宽度 */
                if (w1px > (g_descHidden ? totalW - SPLIT_MINPX
                                         : totalW - w2px - SPLIT_MINPX))
                    w1px = g_descHidden ? totalW - SPLIT_MINPX
                                        : totalW - w2px - SPLIT_MINPX;
                g_w1perm = w1px * 1000 / totalW;
            } else {
                w2px = pt.x - MARGIN - w1px - 3*GAP_W/2;
                if (w2px < SPLIT_MINPX) w2px = SPLIT_MINPX;
                if (w2px > totalW - w1px - SPLIT_MINPX)
                    w2px = totalW - w1px - SPLIT_MINPX;
                g_w2perm = w2px * 1000 / totalW;
            }
            ApplyLayout(hwnd, 1);
            return 0;
        }
        break;

    case WM_LBUTTONUP:
        if (g_dragSplit >= 0) {
            g_dragSplit = -1;
            ReleaseCapture();
            /* 列宽退出时统一保存 */
            return 0;
        }
        break;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT r;
        GetClientRect(hwnd, &r);
        r.left = MARGIN; r.top = MARGIN;
        r.right -= MARGIN; r.bottom = MARGIN + TOP_H;
        /* 顶部播放区：与下方三个区域同款凹陷边框 */
        DrawEdge(hdc, &r, EDGE_SUNKEN, BF_RECT);

        /* 下方三面板边框：与顶部同款（布局几何由 LayoutControls 写入 g_rcPanel） */
        {
            int pi;
            for (pi = 0; pi < 3; pi++) {
                RECT b = g_rcPanel[pi];
                if (b.right <= b.left || b.bottom <= b.top) continue;
                DrawEdge(hdc, &b, EDGE_SUNKEN, BF_RECT);
            }
        }

        /* 第一行状态文本：超长时跑马灯循环，用双缓冲避免闪烁 */
        {
            RECT sr;
            int sw, sh;
            HDC mem;
            HBITMAP bmp, oldBmp;
            HFONT oldF;
            StatusRowRect(&sr);
            sw = sr.right - sr.left;
            sh = sr.bottom - sr.top;
            mem = CreateCompatibleDC(hdc);
            bmp = CreateCompatibleBitmap(hdc, sw, sh);
            oldBmp = (HBITMAP)SelectObject(mem, bmp);
            FillRect(mem, &(RECT){0, 0, sw, sh}, g_bgBrush ? g_bgBrush : GetSysColorBrush(COLOR_BTNFACE));
            SetBkMode(mem, TRANSPARENT);
            SetTextColor(mem, ThText());
            oldF = (HFONT)SelectObject(mem, g_font);
            if (!g_mqActive) {
                RECT tr = { 0, 0, sw, sh };
                DrawTextW(mem, g_statusText, -1, &tr,
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
            } else {
                /* 首尾相接的两份文本，间隔 28px */
                int x0 = -g_mqOffset, i;
                for (i = 0; i < 2; i++) {
                    RECT tr = { x0 + i * (g_mqTextW + 28), 0,
                                x0 + i * (g_mqTextW + 28) + g_mqTextW + 2, sh };
                    DrawTextW(mem, g_statusText, -1, &tr,
                        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                }
            }
            SelectObject(mem, oldF);
            BitBlt(hdc, sr.left, sr.top, sw, sh, mem, 0, 0, SRCCOPY);
            SelectObject(mem, oldBmp);
            DeleteObject(bmp);
            DeleteDC(mem);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }

    /* 播客列表底色随主题 */
    case WM_CTLCOLORLISTBOX:
        if ((HWND)lParam == g_listPod) {
            HDC hdc = (HDC)wParam;
            SetBkColor(hdc, ThPanel());
            SetTextColor(hdc, ThText());
            return (LRESULT)g_whiteBrush;
        }
        break;

    case WM_CTLCOLORSTATIC:
        /* 只读简介框：面板底色 */
        if ((HWND)lParam == g_editDesc) {
            HDC hdc = (HDC)wParam;
            SetBkColor(hdc, ThPanel());
            SetTextColor(hdc, ThText());
            return (LRESULT)g_whiteBrush;
        }
        /* 其它静态文字（时间/音量/格式串）：透明背景 + 主题文字色 */
        {
            HDC hdc = (HDC)wParam;
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, ThText());
            return (LRESULT)(g_bgBrush
                ? g_bgBrush : GetSysColorBrush(COLOR_BTNFACE));
        }

    case WM_GETMINMAXINFO: {
        /* 限制的是客户区最小 480x360，换算成含标题栏/可调边框的外框尺寸
         * （GetWindowRect 含每侧约 7-8px 不可见调宽边，直接写 480 会导致客户区被缩小） */
        MINMAXINFO *mm = (MINMAXINFO*)lParam;
        RECT wr = { 0, 0, 480, 360 };
        AdjustWindowRectEx(&wr, WS_OVERLAPPEDWINDOW, FALSE, 0);
        mm->ptMinTrackSize.x = wr.right - wr.left;
        mm->ptMinTrackSize.y = wr.bottom - wr.top;
        return 0;
    }

    case WM_NCCALCSIZE: {
        /* 窄边框：让 DefWindowProc 算出默认非客户区后，把客户区向外扩几像素，
         * 压薄左右下三边的可见边框（标题栏保留）。尺寸拖拽仍由原命中区负责。 */
        if (wParam == TRUE) {
            NCCALCSIZE_PARAMS *ncp = (NCCALCSIZE_PARAMS*)lParam;
            DefWindowProcW(hwnd, WM_NCCALCSIZE, wParam, lParam);
            ncp->rgrc[0].left   -= 4;
            ncp->rgrc[0].right  += 4;
            ncp->rgrc[0].bottom += 4;
            return 0;
        }
        break;
    }

    case WM_CONTEXTMENU: {
        HWND src = (HWND)wParam;
        POINT pt;
        pt.x = GET_X_LPARAM(lParam);
        pt.y = GET_Y_LPARAM(lParam);

        if (src == g_listPod) {
            /* 播客列表右键：先选中该行，让用户看清正在操作谁；空白处弹添加订阅菜单 */
            POINT cl = pt;
            int item, cmd;
            DWORD v;
            HMENU m;
            ScreenToClient(g_listPod, &cl);
            v = (DWORD)SendMessageW(g_listPod, LB_ITEMFROMPOINT, 0,
                                    MAKELPARAM(cl.x, cl.y));
            if (HIWORD(v)) {
                /* 点在空白处：弹极简菜单 */
                m = CreatePopupMenu();
                MenuAdd(m, 0, 100, tr(TR_CTX_ADD_POD));
                SetForegroundWindow(hwnd);
                {
                    HBRUSH bg = ApplyMenuTheme(m);
                    cmd = TrackPopupMenu(m,
                        TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY,
                        pt.x, pt.y, 0, hwnd, NULL);
                    ThemeMenuCleanup(m);
                    if (bg) DeleteObject(bg);
                }
                DestroyMenu(m);
                if (cmd == 100) AskAddFeed();
                break;
            }
            item = (int)LOWORD(v);
            if (item < 0 || item > g_podCount) break;
            SendMessageW(g_listPod, LB_SETCURSEL, item, 0);
            SelectPodcast(item);
            m = CreatePopupMenu();
            if (item < g_podCount) {
                MenuAdd(m, 0, 1, tr(TR_CTX_REFRESH));
                MenuAdd(m, 0, 2, tr(TR_CTX_DELETE_POD));
                MenuAdd(m, 0, 3, tr(TR_CTX_DEL_POD_CACHE));
                MenuAddSep(m);
                MenuAdd(m, 0, 6, tr(TR_CTX_COPY_FEED_URL));
            } else {
                int cacheN = g_cachePod.epCount;
                MenuAdd(m, cacheN ? 0 : MF_GRAYED, 4, tr(TR_CTX_DEL_ALL_CACHE));
                MenuAdd(m, 0, 5, tr(TR_CTX_OPEN_FOLDER));
            }
            SetForegroundWindow(hwnd);
            {
                HBRUSH bg = ApplyMenuTheme(m);
                cmd = TrackPopupMenu(m,
                    TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY,
                    pt.x, pt.y, 0, hwnd, NULL);
                ThemeMenuCleanup(m);
                if (bg) DeleteObject(bg);
            }
            DestroyMenu(m);
            if (item < g_podCount) {
                if (cmd == 1) RefreshSinglePodcast(item);
                else if (cmd == 2) RemovePodcast(item);
                else if (cmd == 3) DeletePodcastCache(item);
                else if (cmd == 6) {
                    wchar_t *fu = U8ToW(g_pods[item].feedUrl);
                    if (fu) {
                        if (CopyToClipboardW(fu)) SetStatusTemp(tr(TR_CLIP_OK));
                        else SetStatusTemp(tr(TR_CLIP_FAIL));
                        free(fu);
                    }
                }
            } else {
                if (cmd == 4) DeleteAllCacheFiles();
                else if (cmd == 5) OpenCacheFolder();
            }
        } else if (src == g_listEp) {
            /* 剧集列表右键：已缓存→删除缓存；未缓存→下载；恒有复制剧集链接 */
            LVHITTESTINFO hti;
            HMENU m;
            int cmd, ep, cached;
            Episode *e = NULL;
            ZeroMemory(&hti, sizeof(hti));
            hti.pt = pt;
            ScreenToClient(g_listEp, &hti.pt);
            ep = (int)SendMessageW(g_listEp, LVM_HITTEST, 0, (LPARAM)&hti);
            if (ep < 0 || g_curPod < 0 || g_curPod > g_podCount) break;
            if (g_curPod == g_podCount) {
                if (ep >= g_cachePod.epCount) break;
                e = &g_cachePod.eps[ep];
            } else {
                if (ep >= g_pods[g_curPod].epCount) break;
                e = &g_pods[g_curPod].eps[ep];
            }
            if (!e || !e->url) break;
            cached = e->localFile || EpisodeIsCached(e);
            m = CreatePopupMenu();
            if (cached) MenuAdd(m, 0, 2, tr(TR_CTX_DEL_EP_CACHE));
            else        MenuAdd(m, 0, 1, tr(TR_CTX_DOWNLOAD));
            MenuAddSep(m);
            MenuAdd(m, 0, 3, tr(TR_CTX_COPY_EP_URL));
            SetForegroundWindow(hwnd);
            {
                HBRUSH bg = ApplyMenuTheme(m);
                cmd = TrackPopupMenu(m,
                    TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY,
                    pt.x, pt.y, 0, hwnd, NULL);
                ThemeMenuCleanup(m);
                if (bg) DeleteObject(bg);
            }
            DestroyMenu(m);
            if (cmd == 1) DownloadSingleEpisode(g_curPod, ep);
            else if (cmd == 2) DeleteEpisodeCache(g_curPod, ep);
            else if (cmd == 3) {
                if (CopyToClipboardW(e->url)) SetStatusTemp(tr(TR_CLIP_OK));
                else SetStatusTemp(tr(TR_CLIP_FAIL));
            }
        }
        return 0;
    }

    case WM_COMMAND:
        /* 任务栏缩略图工具栏按钮（THBN_CLICKED） */
        if (HIWORD(wParam) == THBN_CLICKED) {
            int tid = LOWORD(wParam);
            if (tid == 0) PlayAdjacent(-1);                    /* 上一曲 */
            else if (tid == 1) PauseResume();                  /* 播放/暂停 */
            else if (tid == 2) PlayAdjacent(+1);               /* 下一曲 */
            else if (tid == 3) {                               /* 随机下一曲 */
                int pod = (g_playPod >= 0) ? g_playPod : g_curPod;
                int n;
                if (pod < 0) return 0;
                n = (pod == g_podCount) ? g_cachePod.epCount : g_pods[pod].epCount;
                if (n > 0) {
                    int target = rand() % n;
                    RequestPlayEpisode(pod, target);
                }
            }
            return 0;
        }
        if (HIWORD(wParam) == LBN_SELCHANGE) {
            if (LOWORD(wParam) == IDC_LIST_POD)
                SelectPodcast((int)SendMessageW(g_listPod, LB_GETCURSEL, 0, 0));
        } else if (HIWORD(wParam) == LBN_DBLCLK) {
            /* 双击播客：播橙色（上次听到）剧集，无记录则从第 1 集开始 */
            if (LOWORD(wParam) == IDC_LIST_POD)
                PlayPodcastDefault((int)SendMessageW(g_listPod, LB_GETCURSEL, 0, 0));
        } else if (HIWORD(wParam) == BN_CLICKED) {
            int id = LOWORD(wParam);
            if (id == IDC_BTN_PLAY) PauseResume();
            else if (id == IDC_BTN_PREV) PlayAdjacent(-1);
            else if (id == IDC_BTN_NEXT) PlayAdjacent(+1);
            else if (id == IDC_BTN_STOP) {
                if (g_downloading) CancelCurrentDownload();   /* 播放下载：中止 */
                CancelBatchCache();                            /* 批量队列：全部取消 */
                PlayerStop();
                SetStatus(tr(TR_STOPPED));
            }
            else if (id == IDC_BTN_MUTE) ToggleMute();
            else if (id == IDC_BTN_ADD) AskAddFeed();
            else if (id == IDC_BTN_DEL) SetPodMode(1);
            else if (id == IDC_BTN_REFRESH) ReloadFeeds(1);
            else if (id == IDC_BTN_PLAYMODE) CyclePlayMode();
            else if (id == IDC_BTN_DIR) OnSettingsButton();
            else if (id == IDC_BTN_SPEED) CycleSpeed();
            else if (id == IDC_BTN_CACHE) StartBatchCache();
        }
        return 0;

    case WM_NOTIFY: {
        NMHDR *nm = (NMHDR*)lParam;

        /* 滑块始终自绘：亮/暗主题统一圆角造型 */
        if ((nm->idFrom == IDC_SLD_SEEK || nm->idFrom == IDC_SLD_VOL) &&
            nm->code == NM_CUSTOMDRAW) {
            LPNMCUSTOMDRAW cd = (LPNMCUSTOMDRAW)lParam;
            if (cd->dwDrawStage == CDDS_PREPAINT &&
                DrawSliderTrack(cd))
                return CDRF_SKIPDEFAULT;
            return CDRF_DODEFAULT;
        }

        if (nm->code == HDN_ITEMCLICKW) {   /* Unicode 窗口只会收到 W 版通知 */
            LPNMHEADERW hdr = (LPNMHEADERW)lParam;
            HWND hv = (HWND)SendMessageW(g_listEp, LVM_GETHEADER, 0, 0);
            if (nm->hwndFrom == hv && hdr->iItem >= 0 && hdr->iItem <= 2)
                ChangeSort(hdr->iItem);
        } else if (nm->idFrom == IDC_LIST_EP) {
            if (nm->code == LVN_ITEMCHANGED) {
                LPNMLISTVIEW lv = (LPNMLISTVIEW)lParam;
                if ((lv->uNewState & LVIS_SELECTED) && lv->iItem >= 0)
                    SelectEpisode(lv->iItem);
            } else if (nm->code == NM_DBLCLK) {
                int sel = (int)SendMessageW(g_listEp, LVM_GETNEXTITEM,
                                            (WPARAM)-1, LVNI_SELECTED);
                /* 命中该播客“上次听到”的那集 → 双击即从记录位置续播；
                 * 点其它集则从头播（清掉待定位，避免继承残留） */
                if (sel >= 0 && g_curPod >= 0 && g_curPod < g_podCount) {
                    Podcast *p = &g_pods[g_curPod];
                    LastEp *le = p->feedUrl ? LastEpFind(LastEpHash(p->feedUrl))
                                            : NULL;
                    if (le && le->sec > 0 && sel < p->epCount &&
                        p->eps[sel].url && le->epUrl &&
                        wcscmp(p->eps[sel].url, le->epUrl) == 0)
                        g_resumeSeek100ns = le->sec * 10000000LL;
                    else
                        g_resumeSeek100ns = 0;
                } else if (sel >= 0) {
                    g_resumeSeek100ns = 0;   /* 缓存聚合视图无单一 feed，从头播 */
                }
                if (sel >= 0) RequestPlayEpisode(g_curPod, sel);
            } else if (nm->code == NM_CUSTOMDRAW) {
                /* 正在播放 → 绿；已缓存（非播放）→ 黄 */
                LPNMLVCUSTOMDRAW cd = (LPNMLVCUSTOMDRAW)lParam;
                if (cd->nmcd.dwDrawStage == CDDS_PREPAINT) {
                    return CDRF_NOTIFYITEMDRAW;   /* 必须，否则收不到 ITEMPREPAINT */
                } else if (cd->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
                    int i = (int)cd->nmcd.dwItemSpec;
                    Podcast *p = ActivePodcast();
                    int playing = 0;
                    /* 正在播放目标（下载准备中/播放中/暂停中均算）标绿。
                     * 用独立的 g_playEp，用户点选其他行不会让绿色乱跑；
                     * g_playPod 在 PlayerStop 或播放失败时清空。 */
                    if (g_playPod >= 0 && i >= 0 && i == g_playEp) {
                        if (g_viewCached)
                            playing = (g_playPod == g_podCount);
                        else if (g_curPod >= 0 && g_curPod < g_podCount)
                            playing = (g_playPod == g_curPod);
                    }
                    int cached = 0;
                    int queued = 0;
                    int lastmark = 0;
                    int sel = (ListView_GetItemState(g_listEp, i, LVIS_SELECTED)
                               & LVIS_SELECTED) != 0;
                    if (p && i >= 0 && i < p->epCount) {
                        cached = p->eps[i].localFile || EpisodeIsCached(&p->eps[i]);
                        /* 批量下载队列中尚未完成的剧集标蓝，下完即转黄 */
                        if (!cached && !playing) queued = EpisodeBatchQueued(i);
                    }
                    /* “上次听到哪”标记：该集 == 此播客记录的最近播放集（橙）。
                     * 播放中绿色优先；缓存黄次之（标记是更强的“听过”信号） */
                    if (p && i >= 0 && i < p->epCount && !playing &&
                        g_curPod >= 0 && g_curPod < g_podCount &&
                        g_pods[g_curPod].feedUrl) {
                        LastEp *le = LastEpFind(
                            LastEpHash(g_pods[g_curPod].feedUrl));
                        if (le && le->sec > 0 && le->epUrl && p->eps[i].url &&
                            wcscmp(p->eps[i].url, le->epUrl) == 0)
                            lastmark = 1;
                    }
                    if (playing) {
                        /* 播放行同时是选中行（默认蓝底白字会盖住绿字），
                         * 自己铺一层浅绿底再用深绿字 */
                        FillRectColor(cd->nmcd.hdc, &cd->nmcd.rc, ThGreenBg());
                        cd->clrText = ThGreen();
                    } else {
                        /* 选中行自己铺主题高亮色（暗色下系统高亮仍是浅色） */
                        if (sel)
                            FillRectColor(cd->nmcd.hdc, &cd->nmcd.rc, ThSel());
                        /* 优先级：批量队列蓝 > 上次听到橙 > 已缓存黄 */
                        cd->clrText = queued
                            ? ThBlue()
                            : lastmark
                            ? (g_darkMode ? RGB(235, 165, 80) : RGB(184, 106, 0))
                            : cached ? ThYellow()
                            : (sel ? (g_darkMode ? RGB(236, 242, 250)
                                                 : RGB(0, 30, 80))
                                   : ThText());
                    }
                    return CDRF_NEWFONT;
                }
            }
        }
        return 0;
    }

    case WM_HSCROLL:
        if ((HWND)lParam == g_sldVol) {
            SetVolumeValue((int)SendMessageW(g_sldVol, TBM_GETPOS, 0, 0));
            /* 音量退出时统一保存 */
        } else if ((HWND)lParam == g_sldSeek) {
            int code = LOWORD(wParam);
            if (code == TB_THUMBTRACK || code == TB_THUMBPOSITION) {
                g_seeking = 1;
                if (g_dur100ns > 0) {
                    int pm = (int)SendMessageW(g_sldSeek, TBM_GETPOS, 0, 0);
                    ShowTimeLabel((int)(g_dur100ns / 10000000 * pm / 1000),
                                  (int)(g_dur100ns / 10000000));
                }
            } else if (code == TB_ENDTRACK) {
                int pm = (int)SendMessageW(g_sldSeek, TBM_GETPOS, 0, 0);
                PlayerSeekToFrac(pm);
                g_seeking = 0;
            }
        }
        return 0;

    case WM_TIMER:
        if (wParam == 2) {
            /* 跑马灯：每次左移 2px，整条滚完后从头再来 */
            if (g_mqActive) {
                RECT sr;
                g_mqOffset += 2;
                if (g_mqOffset > g_mqTextW + 28) g_mqOffset = 0;
                StatusRowRect(&sr);
                InvalidateRect(hwnd, &sr, FALSE);
            }
            return 0;
        }
        if (wParam == 3) {
            /* 定时关机倒计时：每秒刷新标题栏，到点执行关机 */
            if (g_shutdownAt > 0) {
                if ((long long)time(NULL) >= g_shutdownAt) {
                    DoSystemShutdown();
                } else {
                    UpdateWindowTitle();
                }
            }
            return 0;
        }
        if (wParam == 4) {
            /* 临时状态提示到期：恢复“正在播放：…”等持久内容 */
            RestoreBaseStatus();
            return 0;
        }
        if (wParam == 5) {
            DescTabHoverTick(hwnd);
            return 0;
        }
        /* 滚动条跟随内容/滚动实时刷新（自绘条是唯一滚动条） */
        SyncScrollBar(g_vsPod);
        SyncScrollBar(g_vsEp);
        SyncScrollBar(g_hsEp);
        /* 简介区关闭时不要再同步它的滚条：SyncScrollBar 会按内容重新
         * ShowWindow，把已隐藏的条弹回旧位置，看起来像一条残留 */
        SyncDescScrollBar();
        if (g_playState == 1 && !g_seeking && g_player) {
            long long pos = PlayerGet100ns(0);
            long long dur = PlayerGet100ns(1);
            if (dur > 0) g_dur100ns = dur;
            if (pos >= 0) {
                g_posSec = (int)(pos / 10000000);              /* 供手机 /status 读 */
                g_durSec = g_dur100ns > 0 ? (int)(g_dur100ns / 10000000) : 0;
                /* 每播客“上次听到哪”：随播放实时刷新内存，退出/崩溃都不丢太多 */
                if (g_playPod >= 0 && g_playPod < g_podCount &&
                    g_pods[g_playPod].feedUrl && g_playUrl)
                    LastEpSet(LastEpHash(g_pods[g_playPod].feedUrl), g_playUrl,
                              pos / 10000000);
                ShowTimeLabel(g_posSec, g_durSec);
                if (g_dur100ns > 0) {
                    SendMessageW(g_sldSeek, TBM_SETPOS, TRUE,
                        (LPARAM)(pos * 1000 / g_dur100ns));
                }
            }
        }
        return 0;

    /* 手机遥控：±15 秒快退/快进 */
    case WM_APP_REMOTE_SEEK: {
        long long cur, t;
        PROPVARIANT pv;
        if (!g_player || g_dur100ns <= 0) return 0;
        cur = PlayerGet100ns(0);
        if (cur < 0) return 0;
        t = cur + (wParam ? 15LL : -15LL) * 10000000LL;
        if (t < 0) t = 0;
        if (t > g_dur100ns) t = g_dur100ns;
        memset(&pv, 0, sizeof(pv));
        pv.vt = VT_I8;
        pv.hVal.QuadPart = t;
        g_player->lpVtbl->SetPosition(g_player, &MFP_POSITIONTYPE_100NS, &pv);
        return 0;
    }
    /* 手机遥控：定时关机（wParam=分钟数，0=取消） */
    case WM_APP_REMOTE_SHUT:
        SetShutdownTimer((int)wParam);
        return 0;

    /* 手机遥控：进度条拖动定位（wParam=千分比 0..1000） */
    case WM_APP_REMOTE_POS:
        PlayerSeekToFrac((int)wParam);
        return 0;

    /* 手机遥控：音量（wParam=0..100），与音量滑块同一套行为 */
    case WM_APP_REMOTE_VOL:
        SetVolumeValue((int)wParam);
        return 0;

    /* 手机遥控：点选剧集播放（wParam=pod，lParam=ep；pod==g_podCount 为缓存视图） */
    case WM_APP_REMOTE_PLAYEP:
        if ((int)wParam == g_podCount || ((int)wParam >= 0 && (int)wParam < g_podCount))
            RequestPlayEpisode((int)wParam, (int)lParam);
        return 0;

    /* 手机遥控：播放模式循环切换（复用按钮同款逻辑，含 tooltip 同步） */
    case WM_APP_REMOTE_MODE:
        CyclePlayMode();
        return 0;

    /* 手机遥控：倍速（wParam=百分之一，50..300） */
    case WM_APP_REMOTE_SPEED: {
        int v = (int)wParam;
        if (v < 50) v = 50;
        if (v > 300) v = 300;
        g_speed = v;
        ApplySpeed();
        if (g_btnSpeed) InvalidateRect(g_btnSpeed, NULL, TRUE);
        SetStatusTemp(tr(TR_SPEED_SET));
        return 0;
    }

    /* 批量缓存：当前这一集的下载百分比（批量线程 HttpFetch 直接 Post 上来）。
     * head 在取件时已 +1，即当前正在下载的序号；取消/完成后到达的滞留消息直接丢弃 */
    case WM_APP_BATCH_PROGRESS: {
        BatchCache *bc = g_batch;
        if (bc && g_batchActive) {
            wchar_t st[128];
            int head, cnt, len;
            EnterCriticalSection(&bc->cs);
            head = bc->head; cnt = bc->count;
            LeaveCriticalSection(&bc->cs);
            len = swprintf(st, 128, tr(TR_BATCH_PROGRESS_FMT), head, cnt);
            swprintf(st + len, 128 - len, L" %d%%", (int)wParam);
            SetBatchProgress(st);
        }
        return 0;
    }

    /* 批量缓存：每集下完通知 UI。队列自然耗尽时线程转休眠（结构保留供追加），
     * 此时像旧版 FINISH 一样恢复 base 并显示完成统计 */
    case WM_APP_BATCH_DONE: {
        BatchCache *bc = g_batch;
        if (bc) {
            wchar_t st[128];
            int head, cnt, okc, cap, doneN;
            EnterCriticalSection(&bc->cs);
            head = bc->head; cnt = bc->count;
            doneN = bc->doneCount; okc = bc->okCount; cap = bc->truncated;
            LeaveCriticalSection(&bc->cs);
            InvalidateRect(g_listEp, NULL, TRUE);   /* 完成项变黄/队列项保持蓝 */
            /* doneCount 而不是 head：head 在取件时就 +1，最后一集还在下载时
             * head 已等于 count，会让进度提前显示“全部完成” */
            if (g_batchActive && doneN >= cnt) {
                g_batchActive = 0;
                SetStatus(g_batchSavedBase);
                if (cap) swprintf(st, 128, tr(TR_BATCH_DONE_CAP), okc, cnt);
                else     swprintf(st, 128, tr(TR_BATCH_DONE_FMT), okc, cnt);
                SetStatusTemp(st);
            } else if (g_batchActive) {
                swprintf(st, 128, tr(TR_BATCH_PROGRESS_FMT), head, cnt);
                SetBatchProgress(st);
            }
        }
        return 0;
    }

    /* 批量缓存被取消：工作线程退出后发来此消息，同步回收并提示。
     * 自然下完不会到这里（线程转休眠，完成统计在 BATCH_DONE 里处理） */
    case WM_APP_BATCH_FINISH: {
        (void)wParam; (void)lParam;
        BatchReleaseAfterFinish();
        g_batchActive = 0;
        SetStatus(g_batchSavedBase);
        SetStatusTemp(tr(TR_BATCH_CANCELLED));
        InvalidateRect(g_listEp, NULL, TRUE);
        return 0;
    }

    case WM_APP_FEED_PROGRESS: {
        /* 订阅加载进度：状态栏显示 已加载/总数，让用户能区分网速慢和卡死 */
        wchar_t st[128];
        swprintf(st, 128, tr(TR_LOADING_PROG), (int)wParam, (int)lParam);
        SetStatus(st);
        return 0;
    }

    case WM_APP_FEED_ADDED: {
        Podcast *pod = (Podcast*)lParam;
        int incremental = (int)wParam;   /* 0=全量刷新 1=单条添加 2=原位替换 */
        if (pod && incremental == 2) {
            /* 右键"刷新播客"：按 feedUrl 原位替换，列表顺序不变 */
            int idx = -1, x;
            for (x = 0; x < g_podCount; x++)
                if (g_pods[x].feedUrl && pod->feedUrl &&
                    strcmp(g_pods[x].feedUrl, pod->feedUrl) == 0) { idx = x; break; }
            if (idx >= 0) {
                Podcast old = g_pods[idx];
                g_pods[idx] = *pod;      /* 接管新数据（含封面句柄） */
                FreePodcast(&old);       /* 释放旧数据 */
                free(pod);
                pod = NULL;
                if (g_playPod == idx && g_playEp >= g_pods[idx].epCount)
                    g_playEp = -1;
                if (g_curPod == idx) SelectPodcast(idx);
                else InvalidateRect(g_listPod, NULL, TRUE);
                if (g_playPod == idx && g_playEp >= 0)
                    HighlightEpisodeRow(g_playEp);
            } else {
                incremental = 1;   /* 旧条目已被删除：按普通新增处理 */
            }
        }
        if (pod) {
            if (g_podCount < MAX_PODCASTS) {
                int newIdx = g_podCount;
                g_pods[g_podCount] = *pod;
                /* 插入到"已缓存"虚拟行之前（无虚拟行时等同末尾追加）。
                 * owner-draw 列表真正显示在 WM_DRAWITEM 里画，这里 lParam 仅作占位 */
                SendMessageW(g_listPod, LB_INSERTSTRING, newIdx,
                    (LPARAM)(pod->loadFailed ? tr(TR_FEED_FAIL_TITLE) : (pod->title ? pod->title : L"")));
                g_podCount++;
                if (incremental) {
                    if (g_viewCached) {
                        /* 正在看离线列表：保持虚拟行选中，只让新播客排在前面 */
                        SendMessageW(g_listPod, LB_SETCURSEL, g_podCount, 0);
                    } else {
                        /* 单条添加：自动选中新条目，让用户立即看到剧集 */
                        SendMessageW(g_listPod, LB_SETCURSEL, newIdx, 0);
                        SelectPodcast(newIdx);
                    }
                } else if (g_viewCached) {
                    /* 启动时预置了“已缓存”选中：插入真实播客后选中跟随虚拟行 */
                    SendMessageW(g_listPod, LB_SETCURSEL, g_podCount, 0);
                }
            } else {
                FreePodcast(pod);   /* 超过上限：丢弃并释放 */
            }
            free(pod);
        }
        return 0;
    }

    case WM_APP_FEEDS_DONE:
        g_feedsLoading = 0;
        RefreshCachedEntry();   /* 末尾补“已缓存”行（有本地音频时） */
        if (g_podCount > 0) {
            int cursel = (int)SendMessageW(g_listPod, LB_GETCURSEL, 0, 0);
            SetStatus(tr(TR_READY));
            /* 单条添加时新条目已在 FEED_ADDED 中选中；全量刷新默认第一条。
             * 启动时为离线预置了“已缓存”选中，订阅加载成功后切到第一条 */
            if (cursel == LB_ERR || (g_fullReload && g_viewCached)) {
                SendMessageW(g_listPod, LB_SETCURSEL, 0, 0);
                SelectPodcast(0);
            }
        } else if (g_cacheRowHere) {
            SetStatus(tr(TR_OFFLINE_HINT));
            /* 直接选中“已缓存”行，离线音频一目了然 */
            SendMessageW(g_listPod, LB_SETCURSEL, g_podCount, 0);
            SelectPodcast(g_podCount);
        } else {
            SetStatus(tr(TR_NO_FEEDS_HINT));
        }
        TryResumePlayback();   /* 启动续播：有记忆位置时自动恢复上次播放 */
        LastEpPurgeOrphans();  /* 清掉对不上现有订阅的“上次听到”记录 */
        g_fullReload = 0;
        PumpRefreshQueue();    /* 单播客刷新队列：接续下一条 */
        return 0;

    case WM_APP_EP_READY: {
        EpReady *r = (EpReady*)lParam;
        if (r) {
            int k, epIdx = -1;
            /* 已被切歌/停止作废的任务：直接丢弃，不动当前状态 */
            if (r->gen != g_dlGen) {
                free(r->url);
                free(r);
                return 0;
            }
            g_curDlJob = NULL;
            g_downloading = 0;
            if (r->ok && r->url && g_playUrl && wcscmp(r->url, g_playUrl) == 0 &&
                r->pod >= 0 && r->pod < g_podCount) {
                /* 按 URL 在（可能已重排的）数组里找回当前位置 */
                for (k = 0; k < g_pods[r->pod].epCount; k++)
                    if (wcscmp(g_pods[r->pod].eps[k].url, r->url) == 0) { epIdx = k; break; }
            }
            if (epIdx >= 0) {
                wchar_t cache[MAX_PATH], info[128], st[512];
                long long pd = 0;
                Episode *e = &g_pods[r->pod].eps[epIdx];
                CachePathFor(e->url, cache, MAX_PATH);
                ProbeAudio(cache, info, 128, &pd);
                g_dur100ns = pd;
                SetWindowTextW(g_stFormat, info);
                /* 探测到时长：补进剧集列表 */
                if (pd > 0 && e->durationSec <= 0) {
                    e->durationSec = (int)(pd / 10000000);
                    {
                        wchar_t dur[16];
                        LVITEMW li;
                        FmtTime(e->durationSec, dur, 16);
                        ZeroMemory(&li, sizeof(li));
                        li.iItem = epIdx;
                        li.iSubItem = 2;
                        li.pszText = dur;
                        SendMessageW(g_listEp, LVM_SETITEMW, 0, (LPARAM)&li);
                    }
                }
                swprintf(st, 512, tr(TR_PLAYING_FMT), e->title);
                SetStatus(st);
                PlayerPlayFile(cache, e->title);
            } else {
                /* 下载失败或条目已消失：撤销绿色播放标记 */
                g_playPod = -1;
                g_playEp = -1;
                SetStatus(r->ok ? tr(TR_AUDIO_GONE) : tr(TR_DL_FAILED));
            }
            free(r->url);
            free(r);
        }
        RefreshCachedEntry();   /* 首次下载完成时让“已缓存”行出现/更新计数 */
        InvalidateRect(g_listEp, NULL, TRUE);   /* 下载完成 → 缓存标记变黄 */
        return 0;
    }

    case WM_APP_DL_PROGRESS: {
        wchar_t buf[64];
        if ((long)lParam != g_dlGen) return 0;   /* 旧任务进度，丢弃 */
        swprintf(buf, 64, tr(TR_DL_PROGRESS_FMT), (int)wParam);
        SetStatus(buf);
        return 0;
    }

    case WM_APP_MF_EVENT:
        switch ((MFP_EVENT_TYPE)wParam) {
        case MFP_EVENT_TYPE_PLAY:
            if (SUCCEEDED((HRESULT)lParam)) {
                long long d;
                g_playState = 1;
                InvalidateRect(g_btnPlay, NULL, TRUE);   /* 图标变成“暂停” */
                UpdateThumbbarPlayPause();
                d = PlayerGet100ns(1);
                if (d > 0) g_dur100ns = d;
                /* 续播记忆：开播后跳到上次退出的位置（一次性） */
                if (g_resumeSeek100ns > 0) {
                    long long t = g_resumeSeek100ns;
                    g_resumeSeek100ns = 0;
                    if (g_dur100ns > 0 && t >= g_dur100ns) t = 0;   /* 已到片尾就从头 */
                    if (t > 0) {
                        PROPVARIANT pv;
                        memset(&pv, 0, sizeof(pv));
                        pv.vt = VT_I8;
                        pv.hVal.QuadPart = t;
                        g_player->lpVtbl->SetPosition(g_player, &MFP_POSITIONTYPE_100NS, &pv);
                    }
                }
                /* 退出时是暂停：定位完成即暂停，恢复到暂停状态（不自动出声） */
                if (g_resumePaused) {
                    g_resumePaused = 0;
                    PauseResume();
                }
                if (g_listPod) InvalidateRect(g_listPod, NULL, TRUE);   /* 标题变绿 */
                if (g_listEp) InvalidateRect(g_listEp, NULL, TRUE);    /* 剧集变绿 */
                UpdateWindowTitle();
            } else {
                /* 如无可用音频输出设备（0xC00D11BA）：明确提示，绿色标记撤销 */
                g_playState = 0;
                g_playPod = -1;
                g_playEp = -1;
                SetStatus(tr(TR_NO_AUDIO_DEV));
                InvalidateRect(g_btnPlay, NULL, TRUE);
                UpdateWindowTitle();
                if (g_listEp) InvalidateRect(g_listEp, NULL, TRUE);
            }
            break;
        case MFP_EVENT_TYPE_PAUSE:
            if (SUCCEEDED((HRESULT)lParam)) {
                g_playState = 2;
                InvalidateRect(g_btnPlay, NULL, TRUE);   /* 图标变回“播放” */
                UpdateThumbbarPlayPause();
                InvalidateRect(g_listPod, NULL, TRUE);
                InvalidateRect(g_listEp, NULL, TRUE);
            }
            break;
        case MFP_EVENT_TYPE_PLAYBACK_ENDED:
            if (g_playMode == 2) {
                /* 单曲循环：重新播放当前集 */
                if (g_playPod >= 0 && g_playEp >= 0)
                    RequestPlayEpisode(g_playPod, g_playEp);
            } else if (g_playMode == 3) {
                /* 一次性播放：播放完毕即停止 */
                PlayerStop();
                SetStatus(tr(TR_FINISHED));
            } else if (g_playPod >= 0 && g_playEp >= 0) {
                int epN = (g_playPod == g_podCount)
                    ? g_cachePod.epCount : g_pods[g_playPod].epCount;
                if (epN > 1) PlayAdjacent(+1);   /* 顺序/随机：自动下一曲 */
                else { PlayerStop(); SetStatus(tr(TR_FINISHED)); }
            } else {
                PlayerStop();
                SetStatus(tr(TR_FINISHED));
            }
            InvalidateRect(g_listPod, NULL, TRUE);
            InvalidateRect(g_listEp, NULL, TRUE);
            break;
        case MFP_EVENT_TYPE_ERROR:
            SetStatus(tr(TR_PLAY_ERROR));
            break;
        default:
            break;
        }
        return 0;

    /* 媒体键（全局热键） */
    case WM_HOTKEY:
        if (wParam == 1) PauseResume();
        else if (wParam == 2) { PlayerStop(); SetStatus(tr(TR_STOPPED)); }
        else if (wParam == 3) PlayAdjacent(+1);
        else if (wParam == 4) PlayAdjacent(-1);
        return 0;

    /* 媒体键主通道：键盘/耳机线控大多发 APPCOMMAND 广播（RegisterHotKey
     * 只能收到直接发 VK_MEDIA_* 的键盘，所以两条通道并存互补） */
    case WM_APPCOMMAND: {
        unsigned mcmd = (unsigned)GET_APPCOMMAND_LPARAM(lParam);
        if (mcmd == APPCOMMAND_MEDIA_PLAY_PAUSE) { PauseResume(); return TRUE; }
        if (mcmd == APPCOMMAND_MEDIA_STOP) { PlayerStop(); SetStatus(tr(TR_STOPPED)); return TRUE; }
        if (mcmd == APPCOMMAND_MEDIA_NEXTTRACK) { PlayAdjacent(+1); return TRUE; }
        if (mcmd == APPCOMMAND_MEDIA_PREVIOUSTRACK) { PlayAdjacent(-1); return TRUE; }
        break;
    }

    /* 托盘图标消息：wParam=图标ID，lParam=鼠标事件。
     * 左键单击：弹出迷你控制浮窗；右键：显示主窗口/退出菜单 */
    case WM_TRAYICON:
        /* 左键单击：弹出/收起控制浮窗（双击、右键行为不变） */
        if (LOWORD(lParam) == WM_LBUTTONUP) {
            if (g_fly && IsWindowVisible(g_fly)) ShowWindow(g_fly, SW_HIDE);
            else ShowFlyout();
        } else if (LOWORD(lParam) == WM_LBUTTONDBLCLK) {
            /* 双击：直接恢复主窗口（第二击前单击已弹出的浮窗一并收起） */
            if (g_fly) ShowWindow(g_fly, SW_HIDE);
            ShowWindow(hwnd, SW_RESTORE);
            SetForegroundWindow(hwnd);
        } else if (LOWORD(lParam) == WM_RBUTTONUP) {
            POINT pt;
            HMENU m = CreatePopupMenu();
            MenuAdd(m, 0, 1, tr(TR_TRAY_SHOW));
            MenuAdd(m, 0, 2, tr(TR_TRAY_EXIT));
            GetCursorPos(&pt);
            SetForegroundWindow(hwnd);   /* 让菜单能正常消失 */
            {
                HBRUSH bg = ApplyMenuTheme(m);
                {
                    int cmd = TrackPopupMenu(m,
                        TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY,
                        pt.x, pt.y, 0, hwnd, NULL);
                    if (cmd == 1) { ShowWindow(hwnd, SW_RESTORE); SetForegroundWindow(hwnd); }
                    else if (cmd == 2) DestroyWindow(hwnd);
                }
                ThemeMenuCleanup(m);
                if (bg) DeleteObject(bg);
            }
            DestroyMenu(m);
        }
        return 0;

    /* 关闭按钮：直接退出（收进托盘走最小化按钮；托盘右键菜单也可退出） */
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;

    /* 系统关机/注销：允许结束，并在结束前保存配置 */
    case WM_QUERYENDSESSION:
        return TRUE;
    case WM_ENDSESSION:
        if (wParam) SaveConfig();
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd, 1);
        KillTimer(hwnd, 2);
        KillTimer(hwnd, 3);
        KillTimer(hwnd, 4);
        UnregisterHotKey(hwnd, 1);
        UnregisterHotKey(hwnd, 2);
        UnregisterHotKey(hwnd, 3);
        UnregisterHotKey(hwnd, 4);
        /* 托盘图标 */
        if (g_trayAdded) {
            NOTIFYICONDATAW nid;
            ZeroMemory(&nid, sizeof(nid));
            nid.cbSize = sizeof(nid);
            nid.hWnd = hwnd;
            nid.uID = 1;
            Shell_NotifyIconW(NIM_DELETE, &nid);
            g_trayAdded = 0;
        }
        if (g_trayIco) { DestroyIcon(g_trayIco); g_trayIco = NULL; }
        /* 迷你控制浮窗 */
        if (g_fly) { DestroyWindow(g_fly); g_fly = NULL; }
        PhoneServerStop();   /* 手机服务随程序退出关闭 */
        /* 批量工作线程会读 urls 并 PostMessage：先取消并同步等它退出回收；
         * 播放下载线程无法等待（句柄已关闭，随进程终止），仅关闭其 HTTP */
        CancelBatchCache();
        BatchReleaseAfterFinish();
        CancelCurrentDownload();
        DeleteCriticalSection(&g_nowCs);
        /* g_dlCs 不显式删除：播放下载线程可能仍在其等待循环中，
         * 进程终止即回收，删除反而有进入已释放 CS 的崩溃风险 */
        SaveConfig();   /* 退出时统一写盘：排序/列宽/音量/字号/窗口尺寸/订阅顺序 */
        if (g_player) { g_player->lpVtbl->Release(g_player); g_player = NULL; }
        if (g_taskbar) { g_taskbar->lpVtbl->Release(g_taskbar); g_taskbar = NULL; }
        {
            int i;
            for (i = 0; i < 5; i++)
                if (g_tbIco[i]) { DestroyIcon(g_tbIco[i]); g_tbIco[i] = NULL; }
        }

        FreePodcasts();
        FreeCachePodcast();
        free(g_playUrl);
        if (g_whiteBrush) DeleteObject(g_whiteBrush);
        if (g_bgBrush) DeleteObject(g_bgBrush);
        if (g_font) DeleteObject(g_font);
        if (g_fontBold) DeleteObject(g_fontBold);
        if (g_fontSmall) DeleteObject(g_fontSmall);
        MFShutdown();
        CoUninitialize();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

/* 方向键快捷键是否生效：主窗口自身，或焦点在按钮/静态框等
 * 无方向键用途的控件上；列表/编辑框/滑块/滚动条保留自己的方向键 */
static BOOL FocusTakesKeys(HWND f, HWND mainWnd)
{
    wchar_t cn[40];
    if (!f) return FALSE;
    if (f == mainWnd) return TRUE;
    if (!IsChild(mainWnd, f)) return FALSE;
    cn[0] = 0;
    GetClassNameW(f, cn, 40);
    if (wcscmp(cn, L"ListBox") == 0 || wcscmp(cn, L"Edit") == 0 ||
        wcscmp(cn, L"SysListView32") == 0 ||
        wcscmp(cn, TRACKBAR_CLASSW) == 0 || wcscmp(cn, DSB_CLASS) == 0)
        return FALSE;
    return TRUE;
}

/* ================= 入口 ================= */
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE hPrev, PWSTR lpCmdLine, int nCmdShow)
{
    const wchar_t *CLASS_NAME = L"LiteTuneWnd";
    WNDCLASSEXW wc;
    HWND hwnd;
    MSG msg;
    INITCOMMONCONTROLSEX icc;

    (void)hPrev; (void)lpCmdLine;

    /* DPI 不感知：交给系统做整窗位图缩放（125%/150% 下窗口整体等比放大）。
     * 本程序布局全部是 96 DPI 下的硬编码像素，而系统字体会随 DPI 光栅化放大，
     * 若声明感知（系统级/PMv2），高缩放机器上字变大、布局不变 → 文字溢出截断。
     * 位图缩放让不同缩放比例的机器显示内容完全一致（代价：高缩放屏略模糊）。
     * Win10+ 动态调用，Win7 上静默失败；与 manifest 的 dpiAware=false 保持一致，
     * 同时覆盖用户在文件属性里可能残留的高 DPI 兼容性设置。 */
    {
        typedef BOOL (WINAPI *SetDpiAwareCtxFn)(int);
        HMODULE us = GetModuleHandleW(L"user32.dll");
        if (us) {
            SetDpiAwareCtxFn p = NULL;
            *(void**)&p = (void*)GetProcAddress(us, "SetProcessDpiAwarenessContext");
            if (p) p(0xFFFFFFFF);   /* DPI_AWARENESS_CONTEXT_UNAWARE */
        }
    }

    /* 单实例：重复启动时把已运行的窗口带到前台后退出，避免双开互相干扰（配置内嵌 exe 尾部） */
    {
        HANDLE mu = CreateMutexW(NULL, FALSE, L"Local\\LiteTune_SingleInstance");
        if (mu && GetLastError() == ERROR_ALREADY_EXISTS) {
            HWND prev = FindWindowW(CLASS_NAME, NULL);
            if (prev) {
                ShowWindow(prev, SW_RESTORE);
                SetForegroundWindow(prev);
            }
            CloseHandle(mu);
            return 0;
        }
        /* 首个实例持有句柄直到进程退出，以维持单实例标志 */
    }

    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    MFStartup(MF_VERSION, MFSTARTUP_FULL);
    InitializeCriticalSection(&g_nowCs);
    InitializeCriticalSection(&g_dlCs);
    {
        GdiplusStartupInput gsi;
        gsi.GdiplusVersion = 1;
        gsi.DebugEventCallback = NULL;
        gsi.SuppressBackgroundThread = FALSE;
        gsi.SuppressExternalCodecs = FALSE;
        GdiplusStartup(&g_gdipToken, &gsi, NULL);
    }

    icc.dwSize = sizeof(icc);
    icc.dwICC  = ICC_BAR_CLASSES | ICC_STANDARD_CLASSES | ICC_LISTVIEW_CLASSES;
    InitCommonControlsEx(&icc);
    srand((unsigned)GetTickCount());

    /* 建窗前读取配置：排序/列宽/音量/窗口尺寸 */
    LoadConfig();
    if (g_skipStartup) return 0;   /* 首启弹窗被关闭：不建窗直接退出 */
    EnsureCacheDir();   /* 配置里可能有自定义缓存目录，先确保存在 */
    CacheAutoClean();   /* 按 cachedays 设置清理旧缓存 */

    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    /* 程序图标：资源 1（app.ico 编译进 exe），任务栏/Alt-Tab/标题栏共用 */
    wc.hIcon         = LoadIconW(hInst, MAKEINTRESOURCEW(1));
    wc.hIconSm       = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(1), IMAGE_ICON,
                            GetSystemMetrics(SM_CXSMICON),
                            GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR);
    if (g_darkMode) {
        g_bgBrush = CreateSolidBrush(ThWinBg());
        wc.hbrBackground = g_bgBrush;
    } else {
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    }
    if (!RegisterClassExW(&wc)) return 1;

    /* 注册自绘滚动条窗口类 */
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = DarkScrollBarProc;
    wc.hInstance = hInst;
    wc.lpszClassName = DSB_CLASS;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;   /* WM_PAINT 里自绘背景 */
    RegisterClassExW(&wc);

    /* 注册托盘迷你控制浮窗类 */
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = FlyoutProc;
    wc.hInstance = hInst;
    wc.lpszClassName = FLY_CLASS;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(RGB(45, 45, 52));   /* 暗底与按钮一致 */
    RegisterClassExW(&wc);

    /* 注册简介折叠按钮窗口类 */
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = DescTabProc;
    wc.hInstance = hInst;
    wc.lpszClassName = DESC_TAB_CLASS;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;   /* WM_PAINT 里自绘 */
    RegisterClassExW(&wc);

    /* 注册任务栏缩略图工具栏消息（Windows 7+） */
    g_wmTaskbarBtnCreated = RegisterWindowMessageW(L"TaskbarButtonCreated");

    /* 直接用保存的外框矩形（含标题栏边框）创建窗口，不走 AdjustWindowRectEx
     * 避免不同会话边框像素差异导致每次启动窗口变大。首次无保存时 CW_USEDEFAULT */
    hwnd = CreateWindowExW(0, CLASS_NAME, L"LiteTune",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        g_winX, g_winY,
        g_winW, g_winH,
        NULL, NULL, hInst, NULL);
    if (!hwnd) return 1;
    g_hwnd = hwnd;

    ApplyTheme();    /* 显示前应用主题（含暗色标题栏）：首帧即终态，不闪 */
    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);
    SetStatus(tr(TR_READY));   /* 初始状态文本跟随语言（静态初值固定为中文） */
    ReloadFeeds(0);   /* 启动：订阅缓存优先，已下载过就无需重新联网 */
    /* 离线启动时订阅可能加载很慢/失败：先立即挂上“已缓存”行，不等订阅 */
    RefreshCachedEntry();
    if (g_podCount == 0 && g_cacheRowHere) {
        SendMessageW(g_listPod, LB_SETCURSEL, g_podCount, 0);
        SelectPodcast(g_podCount);
    }

    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        /* ESC 退出（叉叉也是同一条退出路径，都会触发 WM_DESTROY 保存）。
         * 仅在焦点属于主窗口时生效：添加订阅弹窗打开时 ESC 只关弹窗 */
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE &&
            (msg.hwnd == hwnd || IsChild(hwnd, msg.hwnd))) {
            DestroyWindow(hwnd);
            continue;
        }
        /* 空格键暂停/恢复（仅当主窗口或其子控件有焦点时） */
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_SPACE) {
            HWND focus = GetFocus();
            if (focus == hwnd || IsChild(hwnd, focus)) {
                SendMessageW(hwnd, WM_COMMAND,
                    MAKEWPARAM(IDC_BTN_PLAY, BN_CLICKED), (LPARAM)g_btnPlay);
                continue;
            }
        }
        /* 方向键快捷键（焦点在主窗口或无方向键用途的控件上时生效；
         * 列表/编辑框/滑块/滚动条保留自己的方向键）：
         * ←/→ 后退/前进 15 秒，↑/↓ 上一曲/下一曲 */
        if (msg.message == WM_KEYDOWN) {
            if (FocusTakesKeys(GetFocus(), hwnd)) {
                if (msg.wParam == VK_LEFT) {
                    PostMessageW(hwnd, WM_APP_REMOTE_SEEK, 0, 0); continue;
                } else if (msg.wParam == VK_RIGHT) {
                    PostMessageW(hwnd, WM_APP_REMOTE_SEEK, 1, 0); continue;
                } else if (msg.wParam == VK_UP) {
                    PlayAdjacent(-1); continue;
                } else if (msg.wParam == VK_DOWN) {
                    PlayAdjacent(+1); continue;
                }
            }
        }
        /* tooltip 采用 TTF_SUBCLASS 自行拦截鼠标消息，无需在此转发 */
        if (!IsDialogMessageW(hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    if (g_gdipToken) GdiplusShutdown(g_gdipToken);
    return (int)msg.wParam;
}

