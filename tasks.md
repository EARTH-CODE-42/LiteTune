# Tasks

## Task 1: Fix feed parsing for feedburner & BBC feeds
- Locate `HttpFetch()` and change `status != 200` to accept all 2xx codes.
- Verify `XmlAttrRange` handles attribute order correctly.
- Test build.

## Task 2: Add right-click context menu for podcast list
- Add `WM_CONTEXTMENU` handler in main WndProc.
- Implement hit-testing for ListBox items.
- Real podcast row menu: 刷新/删除/删除缓存.
- Cached virtual row menu: 删除/打开文件夹.
- Implement actions using existing functions.

## Task 3: Add right-click context menu for episode list
- Implement ListView hit-testing.
- Menu: 下载/删除该缓存.
- Wire to batch cache queue and cache deletion.

## Task 4: Revert error icon to yellow triangle style
- Replace current error cover block with commit 7d489b8 triangle outline style.

## Task 5: Beautify cached icon
- Improve the isCache icon rendering.

## Task 6: Answer linker parameter question
- Explain that `-Os` is the main size lever; some libs may be removable but risky.
