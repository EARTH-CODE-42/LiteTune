# LiteTune Podcast Player — Q3 Enhancement Spec

## Context
Single-file pure C / Win32 podcast player (`main.c`). All work targets that one file. Build with MinGW per README flags (`-Os -s -mwindows -municode ...`). User values small green portable exe, native Win API, concise solutions.

## Requirements

### 1. Fix feed parsing failures (feedburner & BBC)
**Problem:** `https://feeds.feedburner.com/h2g2-powetcast` and `https://podcasts.files.bbci.co.uk/p02pc9tn.rss` open fine in browser but fail to parse inside LiteTune.

**Investigation findings:**
- Both return HTTP 200 with valid RSS 2.0 XML containing `<item>` and `<enclosure url="...">` tags.
- BBC feed is served from AmazonS3, `Content-Type: application/rss+xml`.
- `ParseFeed()` logic is correct: iterates `<item>`, extracts enclosure URL.
- **Likely root cause:** `HttpFetch()` (line ~1880) has strict `if (status != 200) goto fail;`. Some servers return 206 (Partial Content) or other 2xx codes. Also the `WINHTTP_OPTION_TLS_VERSION` (=105) forcing could be problematic.

**Changes:**
- In `HttpFetch()`, change `status != 200` to `status < 200 || status >= 300` (accept all 2xx).
- Keep TLS forcing but add a fallback: if `WinHttpSetOption(WINHTTP_OPTION_TLS_VERSION)` fails, log/ignore and proceed with default protocols.
- Verify `XmlAttrRange()` handles attribute order variations (feedburner has `length` before `url`).

### 2. Right-click context menus

**Podcast list (`g_listPod`, owner-drawn ListBox):**
- Real podcast row → menu:
  - 刷新该播客 (Refresh this podcast)
  - 删除播客条目 (Delete this podcast entry)
  - 删除该播客已缓存的文件 (Delete cached files for this podcast)
- "已缓存" (cached) virtual row → menu:
  - 删除 (Delete)
  - 打开文件夹 (Open folder)

**Episode list (`g_listEp`, ListView):**
- Episode row → menu:
  - 下载 (Download/cache)
  - 删除该缓存 (Delete this cache)

**Implementation approach:**
- Add `WM_CONTEXTMENU` handler in the main window proc.
- Determine source control from `lParam`/`HWND`.
- For ListBox: use `LB_ITEMFROMPOINT` to find item; if `item == g_podCount` → cached virtual row; else real podcast.
- For ListView: use `LVM_HITTEST` to find item.
- Use `CreatePopupMenu` / `AppendMenuW` / `TrackPopupMenu` (pattern already exists in tray menu at line 9453).
- Map commands to existing functions: `RemovePodcast(i)`, `ReloadFeeds(1)` or single-feed refresh, cache deletion via `CachePathFor()`, download via batch cache queue.

**Single-feed refresh:** Reuse `SingleFeedLoaderThread` — remove the podcast from list (keeping URL in `g_feedUrls`), start loader thread with that URL. The podcast gets re-added at end. Acceptable for refresh.

**Delete cached files for a podcast:** Iterate `g_pods[i].eps[]`, call `CachePathFor(url)` and `DeleteFileW` for each.

**Open folder:** Use `ShellExecuteW(NULL, L"open", cacheDir, NULL, NULL, SW_SHOWNORMAL)`.

**Download single episode:** Add episode URL to batch cache queue (reuse `StartBatchCache` logic, or create a simplified single-URL enqueue).

**Delete single episode cache:** `CachePathFor(url)` → `DeleteFileW`.

### 3. Icon improvements

**Error podcast icon:** Revert to commit `7d489b8` style — yellow/amber triangle outline with exclamation mark, no gray background box. Code reference from old_main3.c lines 7884-7908:
```c
COLORREF warn = g_darkMode ? RGB(255,200,60) : RGB(200,140,0);
// triangle outline + exclamation bar + dot
```

**Cached icon (`isCache` branch):** Beautify — make the download arrow nicer, consistent rounded style. Improve the current gray-box-with-blue-arrow to look cleaner (e.g., softer colors, better proportions).

### 4. Linker parameter question (informational)
The "complete" parameter string from README is the proven working set. `-Os` (not `-O2`) was the main size fix (~264KB). Some libs (`-lshlwapi`, `-lpropsys`, `-ladvapi32`, `-lcomdlg32`) might be removable if not directly used, but removing risks link errors — only remove after testing. The real size lever is `-Os` and `-s` (strip).

## Files affected
- `c:\Users\15923\Desktop\lightpodcast\main.c` — all changes

## Verification
1. Build with README flags, confirm exe ~264KB, no warnings.
2. Add both test feeds, confirm they parse and show episodes.
3. Right-click podcast list items — all menu items work.
4. Right-click episode list items — download and delete cache work.
5. Error feed shows yellow triangle icon (not gray box).
6. Cached icon looks improved.
