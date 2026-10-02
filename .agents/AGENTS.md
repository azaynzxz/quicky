# Quicky Documentation Rules

## Code Documentation
- All C++ files must include standard docstrings for classes and complex functions.
- Keep comments concise and focused on *why* something is done, not *what*.
- Win32 API calls should be accompanied by brief explanations of their purpose, as the API is notoriously cryptic.

## Formatting and Linting
- Do not use emojis anywhere in code, UI components, strings, comments, or logs. 
- Use standard C++ naming conventions: PascalCase for classes/structs, camelCase for variables/functions, and UPPER_SNAKE_CASE for macros.

## Project Structure
- `src/` contains all C++ source files and headers.
- `CMakeLists.txt` is the main build configuration.
- Update `Quicky_Implementation_Plan.md` whenever core architecture decisions change.

## Established Win32 & Quicky Quirks (Important Context)
- **OLE Drag and Drop Target**: Always register `IDropTarget` on the child `SysListView32` and `SysTreeView32` controls (`RegisterDragDrop(g_hListView, target)`), because child controls intercept mouse messages and prevent the parent window from receiving drop notifications.
- **OLE IDataObject Creation**: Use a self-contained `CFileDataObject` with a properly formatted `DROPFILES` (`CF_HDROP`) memory layout instead of binding shell PIDLs. Shell PIDL binding can trigger memory access violations if types or parent folders are mismatched.
- **Common Controls Notifications**: In `SysListView32`, `LVN_BEGINDRAG` is a unified notification code (`(LVN_FIRST - 9)`), whereas label edit uses Unicode variants `LVN_BEGINLABELEDITW` and `LVN_ENDLABELEDITW`.
- **Window Activation and Auto-Hide**: Never hide the main popup window on `WM_KILLFOCUS`. Focus moves to the child ListView, edit box, or buttons on click. Instead, handle `WM_ACTIVATE`, check if the activated window is external (`!IsChild(hwnd, hNew)`), and ensure `!g_isDragging && !g_isEditingLabel`.
- **System Tray Toggle Minimize/Restore**: When a visible popup window is open, clicking the system tray icon causes Windows to send `WM_ACTIVATE` (`WA_INACTIVE`) *before* `WM_TRAYICON` (`WM_LBUTTONUP`). If the window hides on `WM_ACTIVATE`, the subsequent tray click sees the window as hidden and mistakenly re-shows it. Always track the timestamp of deactivation (`GetTickCount64()`) and ignore re-showing if a tray click arrives within 400ms of deactivation.
- **System Tray Window Expansion (Left-Anchored)**: Because the tray icon resides near the bottom-right of the screen, expanding a side pane (such as the folder tree) must expand to the **left** by computing `newLeft = rc.right - newW` while keeping `rc.right` fixed. Expanding to the right pushes the window off-screen or over the notification area.
- **ListView 2-Column Grid Calculation**: In `LVS_ICON` view, `ListView_SetIconSpacing` must account for the vertical scrollbar width (`GetSystemMetrics(SM_CXVSCROLL)`) and borders: `usableW = clientW - scrollbarW - 6`, then `colW = (usableW / 2) - 2`. If `colW * 2` exceeds `usableW` by even 1 pixel, Windows collapses to a single centered column.
- **ListView In-Place Rename**: On `LVN_BEGINLABELEDITW`, return `FALSE` to allow editing. On `LVN_ENDLABELEDITW`, rename via `MoveFileW`, update item data, and return `TRUE` to accept text.
- **Search Filtering**: Maintain a master list (`g_currentFiles`) and a displayed list (`g_displayedFiles`). Subclass the search edit box to handle Escape (clear search) and Enter (open first match).
- **Windows XP Folder Tree**: Use `SysTreeView32` with `TVS_HASLINES | TVS_LINESATROOT | TVS_HASBUTTONS | TVS_SHOWSELALWAYS`. Expanding nodes populates subfolders on demand via `TVN_ITEMEXPANDINGW`.
- **Logging**: Use `Logger` (`src/Logger.h`) to output to both `quicky.log` and `OutputDebugStringW`. Keep logs strictly emoji-free.

## Agent Self-Maintenance (Crucial)
- **Auto-Update this File**: Agents MUST proactively update this `AGENTS.md` file whenever there is a new user request, a recurring problem, an established solution, or a new "do/don't" rule.
- **Context Sharing**: Do not wait for the user to ask you to update the rules. If you discover a specific fix, a UI design preference, or a quirk related to this C++ Win32 project, document it here immediately so future AI agents have the context.
