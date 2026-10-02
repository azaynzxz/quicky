#include <windows.h>
#include <shellapi.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <shlwapi.h>
#include <string>
#include <vector>
#include <filesystem>
#include <algorithm>

#include "resource.h"
#include "Logger.h"
#include "DragDropHelper.h"
#include "FolderWatcher.h"

#pragma comment(linker,"\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

// Application constants
const wchar_t CLASS_NAME[] = L"QuickyFloatingClass";
const UINT WM_TRAYICON = WM_USER + 1;
const UINT WM_RELOAD_FILES = WM_USER + 100;

const int HEADER_HEIGHT = 40;
const int SEARCH_HEIGHT = 32;
const int TOP_BAR_TOTAL_HEIGHT = HEADER_HEIGHT + SEARCH_HEIGHT + 4; // ~76px
const int TREE_VIEW_WIDTH = 190;
const int COMPACT_WINDOW_WIDTH = 360;
const int EXPANDED_WINDOW_WIDTH = 570;
const int DEFAULT_WINDOW_HEIGHT = 490;

// View Mode enumeration (Small, Medium, Big)
enum class ViewMode {
    Small,   // 1-column details/list view with 16x16 icon
    Medium,  // 2-column optimized tile grid with 48x48 icon
    Big      // 2-column large preview cards with 96x96 thumbnails
};

// Command Identifiers
const UINT IDC_BTN_TOGGLE_VIEW = 2001;
const UINT IDC_BTN_TOGGLE_TREE = 2002;
const UINT IDC_BTN_OPEN_FOLDER = 2003;
const UINT IDC_BTN_REFRESH     = 2004;
const UINT IDC_BTN_MINIMIZE    = 2005;
const UINT IDC_LISTVIEW        = 2006;
const UINT IDC_TREEVIEW        = 2007;
const UINT IDC_SEARCH_EDIT     = 2008;

const UINT IDM_OPEN_FILE       = 3001;
const UINT IDM_COPY_FILE       = 3002;
const UINT IDM_PASTE_FILE      = 3003;
const UINT IDM_RENAME_FILE     = 3004;
const UINT IDM_DELETE_FILE     = 3005;
const UINT IDM_CYCLE_VIEW      = 3006;
const UINT IDM_TOGGLE_TREE     = 3007;
const UINT IDM_OPEN_FOLDER     = 3008;
const UINT IDM_REFRESH         = 3009;
const UINT IDM_EXIT            = 3010;
const UINT IDM_VIEW_SMALL      = 3011;
const UINT IDM_VIEW_MEDIUM     = 3012;
const UINT IDM_VIEW_BIG        = 3013;

// Global UI Handles
HWND g_hWnd = NULL;
HWND g_hListView = NULL;
HWND g_hTreeView = NULL;
HWND g_hSearchEdit = NULL;
HWND g_hTooltip = NULL;

HWND g_hBtnToggleView = NULL;
HWND g_hBtnToggleTree = NULL;
HWND g_hBtnOpenFolder = NULL;
HWND g_hBtnRefresh = NULL;
HWND g_hBtnMinimize = NULL;
HWND g_hoveredBtn = NULL;

// Image Lists for the three view modes
HIMAGELIST g_hImageListSmall = NULL;   // 16x16
HIMAGELIST g_hImageListMedium = NULL;  // 48x48
HIMAGELIST g_hImageListBig = NULL;     // 96x96
HIMAGELIST g_hTreeImageList = NULL;    // 16x16

// Directories and State
std::wstring g_downloadsPath;
std::wstring g_activeFolder;
std::wstring g_searchQuery;

CDropTarget* g_pDropTarget = nullptr;
FolderWatcher g_folderWatcher;
NOTIFYICONDATAW g_nid = {};

HFONT g_hUiFont = NULL;
HFONT g_hBoldFont = NULL;
HBRUSH g_hHeaderBrush = NULL;
HBRUSH g_hSearchBgBrush = NULL;
HPEN g_hBorderPen = NULL;

ViewMode g_viewMode = ViewMode::Medium; // Default to 2-column medium tile view
bool g_showFolderTree = false;
bool g_isDragging = false;
bool g_isEditingLabel = false;

struct FileItem {
    std::wstring name;
    std::wstring fullPath;
    std::filesystem::file_time_type lastWriteTime;
};
std::vector<FileItem> g_currentFiles;
std::vector<FileItem> g_displayedFiles;
std::vector<std::wstring*> g_treeAllocatedPaths;

// Forward Declarations
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK ListViewSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData);
LRESULT CALLBACK SearchEditSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData);
LRESULT CALLBACK ButtonSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData);

void LoadFolderFiles(const std::wstring& folderPath);
void ApplySearchFilter();
void PopulateFolderTree();
void PopulateSubfolders(HTREEITEM hParent, const std::wstring& parentPath);
void ClearTreeAllocations();

void SetViewMode(ViewMode mode);
void CycleViewMode();
void UpdateListViewLayout();
void UpdateViewButtonTooltip();

void ToggleFolderTree();
void OpenFileAtIndex(int index);
void RenameSelectedFile();
void DeleteSelectedFile();
void CopySelectedFiles();
void PasteFiles();
void ShowListViewContextMenu(POINT pt);
void ShowTrayContextMenu(POINT pt);
void AddButtonTooltip(HWND hBtn, const wchar_t* tipText);
std::vector<int> GetSelectedListViewIndices();

// Vector Icon Drawing Helpers
void DrawGridIcon(HDC hdc, int cx, int cy, COLORREF color);
void DrawListIcon(HDC hdc, int cx, int cy, COLORREF color);
void DrawBigCardIcon(HDC hdc, int cx, int cy, COLORREF color);
void DrawFolderTreeIcon(HDC hdc, int cx, int cy, COLORREF color, bool isActive);
void DrawFolderIcon(HDC hdc, int cx, int cy, COLORREF color);
void DrawSyncIcon(HDC hdc, int cx, int cy, COLORREF color);
void DrawMinimizeIcon(HDC hdc, int cx, int cy, COLORREF color);

/**
 * @brief Retrieves directory of running executable for log placement.
 */
std::wstring GetExecutableDir() {
    wchar_t buffer[MAX_PATH];
    GetModuleFileNameW(NULL, buffer, MAX_PATH);
    std::filesystem::path p(buffer);
    return p.parent_path().wstring();
}

/**
 * @brief Converts string to lowercase for case-insensitive filtering.
 */
std::wstring ToLower(const std::wstring& str) {
    std::wstring out = str;
    std::transform(out.begin(), out.end(), out.begin(), ::towlower);
    return out;
}

/**
 * @brief Application entry point.
 */
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow) {
    HRESULT hrOle = OleInitialize(NULL);

    std::wstring logPath = GetExecutableDir() + L"\\quicky.log";
    Logger::init(logPath);
    LOG_INFO(L"Quicky starting up. OleInitialize result: " + std::to_wstring(hrOle));

    INITCOMMONCONTROLSEX icex = {0};
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_LISTVIEW_CLASSES | ICC_TREEVIEW_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icex);

    PWSTR knownPath = NULL;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Downloads, 0, NULL, &knownPath))) {
        g_downloadsPath = knownPath;
        CoTaskMemFree(knownPath);
        LOG_INFO(L"Monitored Downloads path: " + g_downloadsPath);
    } else {
        LOG_ERROR(L"Failed to resolve FOLDERID_Downloads");
        g_downloadsPath = L"C:\\Users\\Public\\Downloads";
    }
    g_activeFolder = g_downloadsPath;

    // Create modern typography and drawing brushes
    g_hUiFont = CreateFontW(
        -12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI"
    );

    g_hBoldFont = CreateFontW(
        -13, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI"
    );

    g_hHeaderBrush = CreateSolidBrush(RGB(248, 249, 250));
    g_hSearchBgBrush = CreateSolidBrush(RGB(255, 255, 255));
    g_hBorderPen = CreatePen(PS_SOLID, 1, RGB(225, 228, 232));

    WNDCLASSW wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_APPICON));

    if (!RegisterClassW(&wc)) {
        LOG_ERROR(L"RegisterClassW failed with code: " + std::to_wstring(GetLastError()));
        OleUninitialize();
        return 0;
    }

    // Create the main floating widget window
    g_hWnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        CLASS_NAME,
        L"Quicky",
        WS_POPUP | WS_BORDER | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, COMPACT_WINDOW_WIDTH, DEFAULT_WINDOW_HEIGHT,
        NULL, NULL, hInstance, NULL
    );

    if (!g_hWnd) {
        LOG_ERROR(L"CreateWindowExW failed with code: " + std::to_wstring(GetLastError()));
        OleUninitialize();
        return 0;
    }

    // Create tooltip control
    g_hTooltip = CreateWindowExW(
        WS_EX_TOPMOST, TOOLTIPS_CLASS, NULL,
        WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        g_hWnd, NULL, hInstance, NULL
    );

    // Create modern toolbar action buttons (owner-drawn flat buttons)
    auto createBtn = [&](UINT id, const wchar_t* tip) -> HWND {
        HWND hBtn = CreateWindowExW(
            0, L"BUTTON", L"",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            0, 0, 32, 28,
            g_hWnd, (HMENU)(UINT_PTR)id, hInstance, NULL
        );
        SetWindowSubclass(hBtn, ButtonSubclassProc, id, 0);
        AddButtonTooltip(hBtn, tip);
        return hBtn;
    };

    g_hBtnToggleView = createBtn(IDC_BTN_TOGGLE_VIEW, L"View: Medium (2-Column Grid). Click to switch mode");
    g_hBtnToggleTree = createBtn(IDC_BTN_TOGGLE_TREE, L"Toggle Folder Tree Pane (XP Style)");
    g_hBtnOpenFolder = createBtn(IDC_BTN_OPEN_FOLDER, L"Open Folder in File Explorer");
    g_hBtnRefresh    = createBtn(IDC_BTN_REFRESH, L"Refresh Files (F5)");
    g_hBtnMinimize   = createBtn(IDC_BTN_MINIMIZE, L"Minimize to System Tray");

    // Create modern search edit control
    g_hSearchEdit = CreateWindowExW(
        0, WC_EDITW, L"",
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
        34, 45, COMPACT_WINDOW_WIDTH - 48, 22,
        g_hWnd, (HMENU)(UINT_PTR)IDC_SEARCH_EDIT, hInstance, NULL
    );
    SendMessageW(g_hSearchEdit, WM_SETFONT, (WPARAM)g_hUiFont, TRUE);
    SendMessageW(g_hSearchEdit, EM_SETCUEBANNER, TRUE, (LPARAM)L"Search files (Ctrl+F)...");
    SetWindowSubclass(g_hSearchEdit, SearchEditSubclassProc, IDC_SEARCH_EDIT, 0);

    // Create Windows XP-style TreeView control
    g_hTreeView = CreateWindowExW(
        0, WC_TREEVIEWW, L"",
        WS_CHILD | TVS_HASLINES | TVS_LINESATROOT | TVS_HASBUTTONS | TVS_SHOWSELALWAYS | WS_VSCROLL | WS_HSCROLL,
        0, TOP_BAR_TOTAL_HEIGHT, TREE_VIEW_WIDTH, DEFAULT_WINDOW_HEIGHT - TOP_BAR_TOTAL_HEIGHT,
        g_hWnd, (HMENU)(UINT_PTR)IDC_TREEVIEW, hInstance, NULL
    );
    SendMessageW(g_hTreeView, WM_SETFONT, (WPARAM)g_hUiFont, TRUE);

    // Create TreeView ImageList for folder icons
    g_hTreeImageList = ImageList_Create(16, 16, ILC_COLOR32 | ILC_MASK, 5, 20);
    SHFILEINFOW sfiFolder = {0};
    SHGetFileInfoW(L"folder", FILE_ATTRIBUTE_DIRECTORY, &sfiFolder, sizeof(sfiFolder), SHGFI_USEFILEATTRIBUTES | SHGFI_ICON | SHGFI_SMALLICON);
    if (sfiFolder.hIcon) {
        ImageList_AddIcon(g_hTreeImageList, sfiFolder.hIcon);
        DestroyIcon(sfiFolder.hIcon);
    }
    SendMessageW(g_hTreeView, TVM_SETIMAGELIST, TVSIL_NORMAL, (LPARAM)g_hTreeImageList);

    // Create ListView control with 2-column icon view by default
    g_hListView = CreateWindowExW(
        0, WC_LISTVIEWW, L"",
        WS_CHILD | WS_VISIBLE | LVS_ICON | LVS_AUTOARRANGE | LVS_EDITLABELS | WS_VSCROLL,
        0, TOP_BAR_TOTAL_HEIGHT, COMPACT_WINDOW_WIDTH, DEFAULT_WINDOW_HEIGHT - TOP_BAR_TOTAL_HEIGHT,
        g_hWnd, (HMENU)(UINT_PTR)IDC_LISTVIEW, hInstance, NULL
    );
    SendMessageW(g_hListView, WM_SETFONT, (WPARAM)g_hUiFont, TRUE);
    ListView_SetExtendedListViewStyle(g_hListView, LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT);

    LVCOLUMNW lvc = {0};
    lvc.mask = LVCF_WIDTH | LVCF_TEXT;
    lvc.cx = COMPACT_WINDOW_WIDTH - 20;
    lvc.pszText = (LPWSTR)L"Files";
    SendMessageW(g_hListView, LVM_INSERTCOLUMNW, 0, (LPARAM)&lvc);

    // Create image lists for Small (16x16), Medium (48x48), and Big (96x96) modes
    g_hImageListSmall  = ImageList_Create(16, 16, ILC_COLOR32 | ILC_MASK, 20, 100);
    g_hImageListMedium = ImageList_Create(48, 48, ILC_COLOR32 | ILC_MASK, 20, 100);
    g_hImageListBig    = ImageList_Create(96, 96, ILC_COLOR32 | ILC_MASK, 20, 100);

    SendMessageW(g_hListView, LVM_SETIMAGELIST, LVSIL_SMALL, (LPARAM)g_hImageListSmall);
    SendMessageW(g_hListView, LVM_SETIMAGELIST, LVSIL_NORMAL, (LPARAM)g_hImageListMedium);

    SetWindowSubclass(g_hListView, ListViewSubclassProc, 1, 0);

    // Register OLE Drag-and-Drop target
    g_pDropTarget = new CDropTarget(g_hWnd, g_downloadsPath);
    RegisterDragDrop(g_hListView, g_pDropTarget);
    RegisterDragDrop(g_hTreeView, g_pDropTarget);
    RegisterDragDrop(g_hWnd, g_pDropTarget);

    // Setup System Tray icon
    g_nid.cbSize = sizeof(NOTIFYICONDATAW);
    g_nid.hWnd = g_hWnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_APPICON));
    if (!g_nid.hIcon) {
        g_nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    }
    wcscpy_s(g_nid.szTip, L"Quicky - Quick Downloads");
    Shell_NotifyIconW(NIM_ADD, &g_nid);

    // Start background directory monitor
    g_folderWatcher.start(g_activeFolder, g_hWnd);

    // Initial folder tree and file loading
    PopulateFolderTree();
    LoadFolderFiles(g_activeFolder);
    SetViewMode(ViewMode::Medium);

    LOG_INFO(L"Quicky initialization complete. Entering message loop.");

    MSG msg = {};
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    LOG_INFO(L"Terminating Quicky application.");
    g_folderWatcher.stop();

    RevokeDragDrop(g_hListView);
    RevokeDragDrop(g_hTreeView);
    RevokeDragDrop(g_hWnd);
    if (g_pDropTarget) {
        g_pDropTarget->Release();
        g_pDropTarget = nullptr;
    }

    Shell_NotifyIconW(NIM_DELETE, &g_nid);
    ClearTreeAllocations();

    if (g_hUiFont) DeleteObject(g_hUiFont);
    if (g_hBoldFont) DeleteObject(g_hBoldFont);
    if (g_hHeaderBrush) DeleteObject(g_hHeaderBrush);
    if (g_hSearchBgBrush) DeleteObject(g_hSearchBgBrush);
    if (g_hBorderPen) DeleteObject(g_hBorderPen);

    if (g_hImageListSmall) ImageList_Destroy(g_hImageListSmall);
    if (g_hImageListMedium) ImageList_Destroy(g_hImageListMedium);
    if (g_hImageListBig) ImageList_Destroy(g_hImageListBig);
    if (g_hTreeImageList) ImageList_Destroy(g_hTreeImageList);

    OleUninitialize();
    Logger::shutdown();
    return 0;
}

/**
 * @brief Subclass for toolbar buttons to track mouse hover events smoothly.
 */
LRESULT CALLBACK ButtonSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    switch (uMsg) {
        case WM_MOUSEMOVE: {
            if (g_hoveredBtn != hWnd) {
                g_hoveredBtn = hWnd;
                InvalidateRect(hWnd, NULL, TRUE);

                TRACKMOUSEEVENT tme = {0};
                tme.cbSize = sizeof(TRACKMOUSEEVENT);
                tme.dwFlags = TME_LEAVE;
                tme.hwndTrack = hWnd;
                TrackMouseEvent(&tme);
            }
            break;
        }
        case WM_MOUSELEAVE: {
            if (g_hoveredBtn == hWnd) {
                g_hoveredBtn = NULL;
                InvalidateRect(hWnd, NULL, TRUE);
            }
            break;
        }
        case WM_NCDESTROY:
            RemoveWindowSubclass(hWnd, ButtonSubclassProc, uIdSubclass);
            break;
    }
    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

/**
 * @brief Subclass for search edit box to handle Escape, Enter, and arrow key navigation.
 */
LRESULT CALLBACK SearchEditSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    switch (uMsg) {
        case WM_KEYDOWN: {
            if (wParam == VK_ESCAPE) {
                SetWindowTextW(hWnd, L"");
                SetFocus(g_hListView);
                return 0;
            } else if (wParam == VK_DOWN) {
                SetFocus(g_hListView);
                if (ListView_GetItemCount(g_hListView) > 0) {
                    ListView_SetItemState(g_hListView, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
                }
                return 0;
            } else if (wParam == VK_RETURN) {
                if (!g_displayedFiles.empty()) {
                    OpenFileAtIndex(0);
                }
                return 0;
            }
            break;
        }
        case WM_NCDESTROY:
            RemoveWindowSubclass(hWnd, SearchEditSubclassProc, uIdSubclass);
            break;
    }
    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

/**
 * @brief Subclass for ListView control handling shortcuts and context menus.
 */
LRESULT CALLBACK ListViewSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    switch (uMsg) {
        case WM_KEYDOWN: {
            bool isCtrl = (GetKeyState(VK_CONTROL) < 0);
            if (isCtrl && (wParam == 'F' || wParam == 'f')) {
                SetFocus(g_hSearchEdit);
                SendMessageW(g_hSearchEdit, EM_SETSEL, 0, -1);
                return 0;
            } else if (isCtrl && (wParam == 'C' || wParam == 'c')) {
                CopySelectedFiles();
                return 0;
            } else if (isCtrl && (wParam == 'V' || wParam == 'v')) {
                PasteFiles();
                return 0;
            } else if (isCtrl && (wParam == 'T' || wParam == 't')) {
                CycleViewMode();
                return 0;
            } else if (wParam == VK_F5) {
                LoadFolderFiles(g_activeFolder);
                return 0;
            } else if (wParam == VK_RETURN) {
                int sel = ListView_GetNextItem(hWnd, -1, LVNI_SELECTED);
                if (sel >= 0) {
                    OpenFileAtIndex(sel);
                }
                return 0;
            } else if (wParam == VK_F2) {
                RenameSelectedFile();
                return 0;
            } else if (wParam == VK_DELETE) {
                DeleteSelectedFile();
                return 0;
            }
            break;
        }

        case WM_CONTEXTMENU: {
            POINT pt = { LOWORD(lParam), HIWORD(lParam) };
            if (pt.x == -1 && pt.y == -1) {
                int sel = ListView_GetNextItem(hWnd, -1, LVNI_SELECTED);
                RECT rc;
                if (sel >= 0 && ListView_GetItemRect(hWnd, sel, &rc, LVIR_BOUNDS)) {
                    pt.x = rc.left + 20;
                    pt.y = rc.top + 10;
                    ClientToScreen(hWnd, &pt);
                } else {
                    GetCursorPos(&pt);
                }
            }
            ShowListViewContextMenu(pt);
            return 0;
        }

        case WM_NCDESTROY:
            RemoveWindowSubclass(hWnd, ListViewSubclassProc, uIdSubclass);
            break;
    }
    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

/**
 * @brief Window procedure for the main floating widget window.
 */
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_RELOAD_FILES:
            LoadFolderFiles(g_activeFolder);
            return 0;

        case WM_DRAWITEM: {
            LPDRAWITEMSTRUCT pdis = reinterpret_cast<LPDRAWITEMSTRUCT>(lParam);
            if (!pdis) break;

            HDC hdc = pdis->hDC;
            RECT rc = pdis->rcItem;
            bool isPressed = (pdis->itemState & ODS_SELECTED);
            bool isHovered = (pdis->hwndItem == g_hoveredBtn);

            COLORREF bgCol = RGB(248, 249, 250);
            COLORREF borderCol = RGB(248, 249, 250);
            COLORREF iconCol = RGB(80, 85, 95);

            if (pdis->CtlID == IDC_BTN_TOGGLE_TREE && g_showFolderTree) {
                bgCol = isPressed ? RGB(200, 225, 250) : (isHovered ? RGB(215, 235, 255) : RGB(225, 240, 255));
                borderCol = RGB(140, 190, 240);
                iconCol = RGB(10, 100, 210);
            } else if (isPressed) {
                bgCol = RGB(215, 218, 225);
                borderCol = RGB(190, 195, 205);
                iconCol = RGB(30, 35, 45);
            } else if (isHovered) {
                bgCol = RGB(232, 235, 240);
                borderCol = RGB(210, 215, 225);
                iconCol = RGB(30, 35, 45);
            }

            HBRUSH hBr = CreateSolidBrush(bgCol);
            HPEN hPen = CreatePen(PS_SOLID, 1, borderCol);
            HBRUSH hOldBr = (HBRUSH)SelectObject(hdc, hBr);
            HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);

            RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 6, 6);

            SelectObject(hdc, hOldBr);
            SelectObject(hdc, hOldPen);
            DeleteObject(hBr);
            DeleteObject(hPen);

            int cx = (rc.left + rc.right) / 2;
            int cy = (rc.top + rc.bottom) / 2;

            switch (pdis->CtlID) {
                case IDC_BTN_TOGGLE_VIEW:
                    if (g_viewMode == ViewMode::Small) {
                        DrawListIcon(hdc, cx, cy, iconCol);
                    } else if (g_viewMode == ViewMode::Medium) {
                        DrawGridIcon(hdc, cx, cy, iconCol);
                    } else {
                        DrawBigCardIcon(hdc, cx, cy, iconCol);
                    }
                    break;
                case IDC_BTN_TOGGLE_TREE:
                    DrawFolderTreeIcon(hdc, cx, cy, iconCol, g_showFolderTree);
                    break;
                case IDC_BTN_OPEN_FOLDER:
                    DrawFolderIcon(hdc, cx, cy, iconCol);
                    break;
                case IDC_BTN_REFRESH:
                    DrawSyncIcon(hdc, cx, cy, iconCol);
                    break;
                case IDC_BTN_MINIMIZE:
                    DrawMinimizeIcon(hdc, cx, cy, iconCol);
                    break;
            }
            return TRUE;
        }

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);

            RECT rcClient;
            GetClientRect(hwnd, &rcClient);

            // Paint modern header background
            RECT rcHeader = { 0, 0, rcClient.right, TOP_BAR_TOTAL_HEIGHT };
            FillRect(hdc, &rcHeader, g_hHeaderBrush);

            // Draw App Title and current folder indicator
            HFONT hOldFont = (HFONT)SelectObject(hdc, g_hBoldFont);
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(35, 38, 45));

            std::wstring titleText = L"Quicky";
            if (g_activeFolder != g_downloadsPath) {
                std::filesystem::path ap(g_activeFolder);
                titleText += L" - " + ap.filename().wstring();
            }

            RECT rcTitle = { 12, 10, rcClient.right - 180, HEADER_HEIGHT };
            DrawTextW(hdc, titleText.c_str(), -1, &rcTitle, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
            SelectObject(hdc, hOldFont);

            // Draw modern rounded search bar container
            RECT rcSearchBox = { 10, 42, rcClient.right - 10, 42 + SEARCH_HEIGHT - 4 };
            HBRUSH hSearchBr = CreateSolidBrush(RGB(255, 255, 255));
            HPEN hSearchPen = CreatePen(PS_SOLID, 1, RGB(218, 222, 229));
            HBRUSH hOldB = (HBRUSH)SelectObject(hdc, hSearchBr);
            HPEN hOldP = (HPEN)SelectObject(hdc, hSearchPen);

            RoundRect(hdc, rcSearchBox.left, rcSearchBox.top, rcSearchBox.right, rcSearchBox.bottom, 6, 6);

            SelectObject(hdc, hOldB);
            SelectObject(hdc, hOldP);
            DeleteObject(hSearchBr);
            DeleteObject(hSearchPen);

            // Draw magnifying glass icon inside search box
            int mx = rcSearchBox.left + 14;
            int my = (rcSearchBox.top + rcSearchBox.bottom) / 2 - 1;
            HPEN hIconPen = CreatePen(PS_SOLID, 2, RGB(150, 155, 165));
            HPEN hPrevPen = (HPEN)SelectObject(hdc, hIconPen);
            HBRUSH hNullBr = (HBRUSH)GetStockObject(NULL_BRUSH);
            HBRUSH hPrevBr = (HBRUSH)SelectObject(hdc, hNullBr);

            Ellipse(hdc, mx - 4, my - 4, mx + 5, my + 5);
            MoveToEx(hdc, mx + 3, my + 3, NULL);
            LineTo(hdc, mx + 7, my + 7);

            SelectObject(hdc, hPrevPen);
            SelectObject(hdc, hPrevBr);
            DeleteObject(hIconPen);

            // Draw bottom divider line below search bar
            HPEN hDivPen = CreatePen(PS_SOLID, 1, RGB(230, 233, 238));
            HPEN hOldDiv = (HPEN)SelectObject(hdc, hDivPen);
            MoveToEx(hdc, 0, TOP_BAR_TOTAL_HEIGHT - 1, NULL);
            LineTo(hdc, rcClient.right, TOP_BAR_TOTAL_HEIGHT - 1);

            // Draw vertical splitter line if Folder Tree is visible
            if (g_showFolderTree) {
                MoveToEx(hdc, TREE_VIEW_WIDTH, TOP_BAR_TOTAL_HEIGHT, NULL);
                LineTo(hdc, TREE_VIEW_WIDTH, rcClient.bottom);
            }

            SelectObject(hdc, hOldDiv);
            DeleteObject(hDivPen);

            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_NCHITTEST: {
            LRESULT hit = DefWindowProcW(hwnd, uMsg, wParam, lParam);
            if (hit == HTCLIENT) {
                POINT pt = { LOWORD(lParam), HIWORD(lParam) };
                ScreenToClient(hwnd, &pt);
                RECT rcClient;
                GetClientRect(hwnd, &rcClient);
                if (pt.y < HEADER_HEIGHT && pt.x < (rcClient.right - 180)) {
                    return HTCAPTION;
                }
            }
            return hit;
        }

        case WM_SIZE: {
            int cx = LOWORD(lParam);
            int cy = HIWORD(lParam);

            // Reposition header toolbar buttons (right-aligned)
            int right = cx - 8;
            auto posBtn = [&](HWND hBtn) {
                if (hBtn) {
                    SetWindowPos(hBtn, NULL, right - 32, 6, 32, 28, SWP_NOZORDER);
                    right -= 36;
                }
            };

            posBtn(g_hBtnMinimize);
            posBtn(g_hBtnRefresh);
            posBtn(g_hBtnOpenFolder);
            posBtn(g_hBtnToggleTree);
            posBtn(g_hBtnToggleView);

            // Reposition search edit box
            if (g_hSearchEdit) {
                SetWindowPos(g_hSearchEdit, NULL, 36, 45, cx - 52, 21, SWP_NOZORDER);
            }

            // Layout TreeView and ListView
            int contentY = TOP_BAR_TOTAL_HEIGHT;
            int contentH = cy - contentY;

            if (g_showFolderTree && g_hTreeView) {
                ShowWindow(g_hTreeView, SW_SHOW);
                SetWindowPos(g_hTreeView, NULL, 0, contentY, TREE_VIEW_WIDTH, contentH, SWP_NOZORDER);

                int listX = TREE_VIEW_WIDTH + 1;
                int listW = cx - listX;
                if (g_hListView) {
                    SetWindowPos(g_hListView, NULL, listX, contentY, listW, contentH, SWP_NOZORDER);
                }
            } else {
                if (g_hTreeView) ShowWindow(g_hTreeView, SW_HIDE);
                if (g_hListView) {
                    SetWindowPos(g_hListView, NULL, 0, contentY, cx, contentH, SWP_NOZORDER);
                }
            }

            UpdateListViewLayout();
            return 0;
        }

        case WM_COMMAND: {
            WORD id = LOWORD(wParam);
            WORD code = HIWORD(wParam);

            if (id == IDC_SEARCH_EDIT && code == EN_CHANGE) {
                wchar_t buf[256];
                GetWindowTextW(g_hSearchEdit, buf, 256);
                g_searchQuery = buf;
                ApplySearchFilter();
                return 0;
            }

            switch (id) {
                case IDC_BTN_TOGGLE_VIEW:
                case IDM_CYCLE_VIEW:
                    CycleViewMode();
                    break;

                case IDM_VIEW_SMALL:
                    SetViewMode(ViewMode::Small);
                    break;

                case IDM_VIEW_MEDIUM:
                    SetViewMode(ViewMode::Medium);
                    break;

                case IDM_VIEW_BIG:
                    SetViewMode(ViewMode::Big);
                    break;

                case IDC_BTN_TOGGLE_TREE:
                case IDM_TOGGLE_TREE:
                    ToggleFolderTree();
                    break;

                case IDC_BTN_OPEN_FOLDER:
                case IDM_OPEN_FOLDER:
                    ShellExecuteW(NULL, L"open", g_activeFolder.c_str(), NULL, NULL, SW_SHOWNORMAL);
                    break;

                case IDC_BTN_REFRESH:
                case IDM_REFRESH:
                    LoadFolderFiles(g_activeFolder);
                    break;

                case IDC_BTN_MINIMIZE:
                    ShowWindow(hwnd, SW_HIDE);
                    break;

                case IDM_OPEN_FILE: {
                    int sel = ListView_GetNextItem(g_hListView, -1, LVNI_SELECTED);
                    if (sel >= 0) OpenFileAtIndex(sel);
                    break;
                }

                case IDM_COPY_FILE:
                    CopySelectedFiles();
                    break;

                case IDM_PASTE_FILE:
                    PasteFiles();
                    break;

                case IDM_RENAME_FILE:
                    RenameSelectedFile();
                    break;

                case IDM_DELETE_FILE:
                    DeleteSelectedFile();
                    break;

                case IDM_EXIT:
                    PostQuitMessage(0);
                    break;
            }
            return 0;
        }

        case WM_NOTIFY: {
            LPNMHDR lpnmh = reinterpret_cast<LPNMHDR>(lParam);
            if (lpnmh->idFrom == IDC_LISTVIEW) {
                if (lpnmh->code == NM_DBLCLK) {
                    LPNMITEMACTIVATE pia = reinterpret_cast<LPNMITEMACTIVATE>(lParam);
                    if (pia && pia->iItem >= 0) {
                        OpenFileAtIndex(pia->iItem);
                    }
                    return 0;
                } else if (lpnmh->code == LVN_BEGINDRAG) {
                    NMLISTVIEW* pnmv = reinterpret_cast<NMLISTVIEW*>(lParam);
                    if (!pnmv || pnmv->iItem < 0 || pnmv->iItem >= static_cast<int>(g_displayedFiles.size())) {
                        return 0;
                    }

                    std::vector<int> selected = GetSelectedListViewIndices();
                    if (selected.empty()) {
                        selected.push_back(pnmv->iItem);
                    }

                    std::vector<std::wstring> paths;
                    for (int idx : selected) {
                        if (idx >= 0 && idx < static_cast<int>(g_displayedFiles.size())) {
                            paths.push_back(g_displayedFiles[idx].fullPath);
                        }
                    }

                    if (!paths.empty()) {
                        LOG_INFO(L"Initiating drag-and-drop out with " + std::to_wstring(paths.size()) + L" file(s)");
                        IDataObject* pDataObject = CreateFileDataObject(paths);
                        if (pDataObject) {
                            IDropSource* pDropSource = new CDropSource();
                            DWORD dwEffect = DROPEFFECT_NONE;
                            g_isDragging = true;
                            HRESULT hrDrag = DoDragDrop(pDataObject, pDropSource, DROPEFFECT_COPY | DROPEFFECT_MOVE, &dwEffect);
                            g_isDragging = false;
                            LOG_INFO(L"DoDragDrop finished with HRESULT: " + std::to_wstring(hrDrag));
                            pDataObject->Release();
                            pDropSource->Release();
                        }
                    }
                    return 0;
                } else if (lpnmh->code == LVN_BEGINLABELEDITW) {
                    g_isEditingLabel = true;
                    return FALSE;
                } else if (lpnmh->code == LVN_ENDLABELEDITW) {
                    g_isEditingLabel = false;
                    NMLVDISPINFOW* pdi = reinterpret_cast<NMLVDISPINFOW*>(lParam);
                    if (pdi && pdi->item.pszText != NULL && pdi->item.iItem >= 0 && pdi->item.iItem < static_cast<int>(g_displayedFiles.size())) {
                        std::wstring newName = pdi->item.pszText;
                        if (!newName.empty()) {
                            std::wstring oldPath = g_displayedFiles[pdi->item.iItem].fullPath;
                            std::wstring newPath = (std::filesystem::path(g_activeFolder) / newName).wstring();

                            if (oldPath != newPath) {
                                if (MoveFileW(oldPath.c_str(), newPath.c_str())) {
                                    LOG_INFO(L"Renamed file: " + oldPath + L" -> " + newPath);
                                    LoadFolderFiles(g_activeFolder);
                                    return TRUE;
                                } else {
                                    DWORD err = GetLastError();
                                    LOG_ERROR(L"MoveFileW error: " + std::to_wstring(err));
                                    MessageBoxW(hwnd, L"Failed to rename file.", L"Quicky", MB_ICONERROR | MB_OK);
                                }
                            }
                        }
                    }
                    return FALSE;
                }
            } else if (lpnmh->idFrom == IDC_TREEVIEW) {
                if (lpnmh->code == TVN_ITEMEXPANDINGW) {
                    LPNMTREEVIEWW pnmtv = reinterpret_cast<LPNMTREEVIEWW>(lParam);
                    if (pnmtv->action == TVE_EXPAND) {
                        std::wstring* pPath = reinterpret_cast<std::wstring*>(pnmtv->itemNew.lParam);
                        if (pPath) {
                            PopulateSubfolders(pnmtv->itemNew.hItem, *pPath);
                        }
                    }
                } else if (lpnmh->code == TVN_SELCHANGEDW) {
                    LPNMTREEVIEWW pnmtv = reinterpret_cast<LPNMTREEVIEWW>(lParam);
                    std::wstring* pPath = reinterpret_cast<std::wstring*>(pnmtv->itemNew.lParam);
                    if (pPath && *pPath != g_activeFolder) {
                        g_activeFolder = *pPath;
                        LOG_INFO(L"Folder tree selection changed to: " + g_activeFolder);
                        LoadFolderFiles(g_activeFolder);
                        g_folderWatcher.start(g_activeFolder, g_hWnd);
                        InvalidateRect(g_hWnd, NULL, TRUE);
                    }
                }
            }
            break;
        }

        case WM_ACTIVATE: {
            static ULONGLONG s_lastDeactivateTick = 0;
            if (LOWORD(wParam) == WA_INACTIVE) {
                HWND hNew = reinterpret_cast<HWND>(lParam);
                if (!g_isDragging && !g_isEditingLabel) {
                    if (hNew != hwnd && !IsChild(hwnd, hNew)) {
                        LOG_DEBUG(L"Quicky lost focus to outside window. Auto-hiding.");
                        s_lastDeactivateTick = GetTickCount64();
                        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)s_lastDeactivateTick);
                        ShowWindow(hwnd, SW_HIDE);
                    }
                }
            }
            return 0;
        }

        case WM_TRAYICON: {
            if (LOWORD(lParam) == WM_LBUTTONUP) {
                ULONGLONG lastDeact = (ULONGLONG)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
                ULONGLONG now = GetTickCount64();

                // If window was deactivated in the last 400ms by this click, user intended to toggle close
                if (now - lastDeact < 400) {
                    LOG_INFO(L"Tray icon clicked while open. Minimizing popup.");
                    SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
                    return 0;
                }

                if (IsWindowVisible(hwnd)) {
                    LOG_INFO(L"Tray icon clicked while visible. Minimizing popup.");
                    ShowWindow(hwnd, SW_HIDE);
                } else {
                    LOG_INFO(L"Tray icon clicked while hidden. Showing popup.");
                    POINT pt;
                    GetCursorPos(&pt);

                    RECT rcClient;
                    GetClientRect(hwnd, &rcClient);
                    int w = rcClient.right;
                    int h = rcClient.bottom;
                    if (w < 100) w = g_showFolderTree ? EXPANDED_WINDOW_WIDTH : COMPACT_WINDOW_WIDTH;
                    if (h < 100) h = DEFAULT_WINDOW_HEIGHT;

                    int x = pt.x - (w / 2);
                    int y = pt.y - h;
                    if (y < 20) y = pt.y + 10;

                    RECT rcWork;
                    SystemParametersInfoW(SPI_GETWORKAREA, 0, &rcWork, 0);
                    if (x < rcWork.left) x = rcWork.left + 10;
                    if (x + w > rcWork.right) x = rcWork.right - w - 10;
                    if (y + h > rcWork.bottom) y = rcWork.bottom - h - 10;

                    SetWindowPos(hwnd, HWND_TOPMOST, x, y, w, h, SWP_SHOWWINDOW);
                    SetForegroundWindow(hwnd);
                    SetFocus(g_hSearchEdit);
                }
            } else if (LOWORD(lParam) == WM_RBUTTONUP) {
                POINT pt;
                GetCursorPos(&pt);
                ShowTrayContextMenu(pt);
            }
            return 0;
        }

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

/**
 * @brief Adds a native tooltip string to a specified control.
 */
void AddButtonTooltip(HWND hBtn, const wchar_t* tipText) {
    if (!g_hTooltip || !hBtn) return;
    TOOLINFOW ti = {0};
    ti.cbSize = sizeof(TOOLINFOW);
    ti.uFlags = TTF_SUBCLASS | TTF_IDISHWND;
    ti.hwnd = g_hWnd;
    ti.uId = reinterpret_cast<UINT_PTR>(hBtn);
    ti.lpszText = const_cast<LPWSTR>(tipText);
    SendMessageW(g_hTooltip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&ti));
}

/**
 * @brief Updates the tooltip on the View toggle button based on active mode.
 */
void UpdateViewButtonTooltip() {
    if (!g_hTooltip || !g_hBtnToggleView) return;
    const wchar_t* tip = L"View: Medium (2-Column Grid). Click for Big Previews (Ctrl+T)";
    if (g_viewMode == ViewMode::Small) {
        tip = L"View: Small (List). Click for Medium (2-Column Grid) (Ctrl+T)";
    } else if (g_viewMode == ViewMode::Big) {
        tip = L"View: Big (Large Previews). Click for Small List (Ctrl+T)";
    }

    TOOLINFOW ti = {0};
    ti.cbSize = sizeof(TOOLINFOW);
    ti.hwnd = g_hWnd;
    ti.uId = reinterpret_cast<UINT_PTR>(g_hBtnToggleView);
    ti.lpszText = const_cast<LPWSTR>(tip);
    SendMessageW(g_hTooltip, TTM_UPDATETIPTEXTW, 0, reinterpret_cast<LPARAM>(&ti));
}

/**
 * @brief Calculates and enforces exact 2-column or list spacing on the ListView control.
 */
void UpdateListViewLayout() {
    if (!g_hListView) return;

    RECT rc;
    GetClientRect(g_hListView, &rc);
    int clientW = rc.right - rc.left;
    int scrollbarW = GetSystemMetrics(SM_CXVSCROLL);
    if (scrollbarW < 16) scrollbarW = 17;

    // Available width after subtracting vertical scrollbar and safety margin
    int usableW = clientW - scrollbarW - 6;
    if (usableW < 120) usableW = 120;

    if (g_viewMode == ViewMode::Small) {
        ListView_SetColumnWidth(g_hListView, 0, clientW - 20);
    } else if (g_viewMode == ViewMode::Medium) {
        // Enforce strictly 2 equal columns: (usableW / 2) - 2 ensures no overflow
        int colW = (usableW / 2) - 2;
        if (colW < 120) colW = 120;
        int colH = 92;
        ListView_SetIconSpacing(g_hListView, colW, colH);
    } else if (g_viewMode == ViewMode::Big) {
        // Big thumbnails: 2 columns in standard width, 1 column if extremely narrow
        int cols = (usableW >= 280) ? 2 : 1;
        int colW = (usableW / cols) - 4;
        if (colW < 140) colW = 140;
        int colH = 148;
        ListView_SetIconSpacing(g_hListView, colW, colH);
    }

    ListView_Arrange(g_hListView, LVA_DEFAULT);
}

/**
 * @brief Sets the active view mode (Small, Medium, or Big) and updates styles and image lists.
 */
void SetViewMode(ViewMode mode) {
    g_viewMode = mode;
    DWORD dwStyle = GetWindowLongW(g_hListView, GWL_STYLE);
    dwStyle &= ~LVS_TYPEMASK;

    if (g_viewMode == ViewMode::Small) {
        dwStyle |= LVS_REPORT;
        SetWindowLongW(g_hListView, GWL_STYLE, dwStyle);
        SendMessageW(g_hListView, LVM_SETIMAGELIST, LVSIL_SMALL, (LPARAM)g_hImageListSmall);
        LOG_INFO(L"View mode set to: Small (List)");
    } else if (g_viewMode == ViewMode::Medium) {
        dwStyle |= LVS_ICON | LVS_AUTOARRANGE;
        SetWindowLongW(g_hListView, GWL_STYLE, dwStyle);
        SendMessageW(g_hListView, LVM_SETIMAGELIST, LVSIL_NORMAL, (LPARAM)g_hImageListMedium);
        LOG_INFO(L"View mode set to: Medium (2-Column Grid)");
    } else if (g_viewMode == ViewMode::Big) {
        dwStyle |= LVS_ICON | LVS_AUTOARRANGE;
        SetWindowLongW(g_hListView, GWL_STYLE, dwStyle);
        SendMessageW(g_hListView, LVM_SETIMAGELIST, LVSIL_NORMAL, (LPARAM)g_hImageListBig);
        LOG_INFO(L"View mode set to: Big (Large Previews)");
    }

    UpdateListViewLayout();
    SetWindowPos(g_hListView, NULL, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);

    UpdateViewButtonTooltip();
    InvalidateRect(g_hBtnToggleView, NULL, TRUE);
    InvalidateRect(g_hListView, NULL, TRUE);
}

/**
 * @brief Cycles through view modes: Small -> Medium -> Big -> Small.
 */
void CycleViewMode() {
    if (g_viewMode == ViewMode::Small) {
        SetViewMode(ViewMode::Medium);
    } else if (g_viewMode == ViewMode::Medium) {
        SetViewMode(ViewMode::Big);
    } else {
        SetViewMode(ViewMode::Small);
    }
}

/**
 * @brief Retrieves selected item indices from ListView.
 */
std::vector<int> GetSelectedListViewIndices() {
    std::vector<int> indices;
    int idx = -1;
    while ((idx = ListView_GetNextItem(g_hListView, idx, LVNI_SELECTED)) != -1) {
        indices.push_back(idx);
    }
    return indices;
}

/**
 * @brief Opens the file at the given index in default viewer.
 */
void OpenFileAtIndex(int index) {
    if (index >= 0 && index < static_cast<int>(g_displayedFiles.size())) {
        const std::wstring& path = g_displayedFiles[index].fullPath;
        LOG_INFO(L"Opening file: " + path);
        HINSTANCE hInst = ShellExecuteW(NULL, L"open", path.c_str(), NULL, NULL, SW_SHOWNORMAL);
        if (reinterpret_cast<INT_PTR>(hInst) <= 32) {
            LOG_ERROR(L"ShellExecuteW failed to open: " + path);
        }
    }
}

/**
 * @brief Begins label editing on selected file.
 */
void RenameSelectedFile() {
    int sel = ListView_GetNextItem(g_hListView, -1, LVNI_SELECTED);
    if (sel >= 0) {
        SetFocus(g_hListView);
        ListView_EditLabel(g_hListView, sel);
    }
}

/**
 * @brief Deletes selected file safely to Recycle Bin.
 */
void DeleteSelectedFile() {
    int sel = ListView_GetNextItem(g_hListView, -1, LVNI_SELECTED);
    if (sel < 0 || sel >= static_cast<int>(g_displayedFiles.size())) return;

    std::wstring path = g_displayedFiles[sel].fullPath;
    std::wstring msg = L"Move '" + g_displayedFiles[sel].name + L"' to Recycle Bin?";
    if (MessageBoxW(g_hWnd, msg.c_str(), L"Confirm Delete", MB_YESNO | MB_ICONQUESTION) != IDYES) {
        return;
    }

    std::vector<wchar_t> doubleNullPath(path.length() + 2, 0);
    wcscpy_s(doubleNullPath.data(), path.length() + 1, path.c_str());

    SHFILEOPSTRUCTW fos = {0};
    fos.hwnd = g_hWnd;
    fos.wFunc = FO_DELETE;
    fos.pFrom = doubleNullPath.data();
    fos.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT;

    int res = SHFileOperationW(&fos);
    if (res == 0 && !fos.fAnyOperationsAborted) {
        LOG_INFO(L"Deleted file to Recycle Bin: " + path);
        LoadFolderFiles(g_activeFolder);
    } else {
        LOG_ERROR(L"SHFileOperationW delete failed with code: " + std::to_wstring(res));
    }
}

/**
 * @brief Copies selected files to clipboard.
 */
void CopySelectedFiles() {
    std::vector<int> selected = GetSelectedListViewIndices();
    if (selected.empty()) {
        LOG_INFO(L"No files selected to copy.");
        return;
    }

    std::vector<std::wstring> paths;
    for (int idx : selected) {
        if (idx >= 0 && idx < static_cast<int>(g_displayedFiles.size())) {
            paths.push_back(g_displayedFiles[idx].fullPath);
        }
    }

    CopyFilesToClipboard(paths);
}

/**
 * @brief Pastes files from clipboard into the active folder.
 */
void PasteFiles() {
    PasteFilesFromClipboard(g_activeFolder, g_hWnd);
}

/**
 * @brief Toggles the Windows XP-style folder tree pane with left expansion.
 */
void ToggleFolderTree() {
    g_showFolderTree = !g_showFolderTree;

    RECT rc;
    GetWindowRect(g_hWnd, &rc);
    int h = rc.bottom - rc.top;

    int newW = g_showFolderTree ? EXPANDED_WINDOW_WIDTH : COMPACT_WINDOW_WIDTH;

    // Anchor right edge so the window expands to the left
    int newLeft = rc.right - newW;
    int newTop = rc.top;

    RECT rcWork;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &rcWork, 0);
    if (newLeft < rcWork.left) {
        newLeft = rcWork.left + 10;
    }

    SetWindowPos(g_hWnd, NULL, newLeft, newTop, newW, h, SWP_NOZORDER);

    UpdateListViewLayout();
    InvalidateRect(g_hBtnToggleTree, NULL, TRUE);
    InvalidateRect(g_hWnd, NULL, TRUE);
    LOG_INFO(L"Toggled folder tree pane to the left: " + std::to_wstring(g_showFolderTree));
}

/**
 * @brief Applies case-insensitive substring search filter and populates image lists.
 */
void ApplySearchFilter() {
    SendMessageW(g_hListView, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(g_hListView);
    g_displayedFiles.clear();

    ImageList_RemoveAll(g_hImageListSmall);
    ImageList_RemoveAll(g_hImageListMedium);
    ImageList_RemoveAll(g_hImageListBig);

    std::wstring queryLower = ToLower(g_searchQuery);

    for (const auto& fi : g_currentFiles) {
        if (queryLower.empty() || ToLower(fi.name).find(queryLower) != std::wstring::npos) {
            g_displayedFiles.push_back(fi);
        }
    }

    for (size_t i = 0; i < g_displayedFiles.size(); ++i) {
        const auto& fi = g_displayedFiles[i];

        // 1. Small icon (16x16)
        SHFILEINFOW sfiSmall = {0};
        SHGetFileInfoW(fi.fullPath.c_str(), 0, &sfiSmall, sizeof(sfiSmall), SHGFI_ICON | SHGFI_SMALLICON);
        if (sfiSmall.hIcon) {
            ImageList_AddIcon(g_hImageListSmall, sfiSmall.hIcon);
            DestroyIcon(sfiSmall.hIcon);
        }

        // 2. Medium thumbnail (48x48) & Big thumbnail (96x96)
        HBITMAP hThumbMed = nullptr;
        HBITMAP hThumbBig = nullptr;
        IShellItem* pShellItem = nullptr;

        if (SUCCEEDED(SHCreateItemFromParsingName(fi.fullPath.c_str(), nullptr, IID_PPV_ARGS(&pShellItem)))) {
            IShellItemImageFactory* pFactory = nullptr;
            if (SUCCEEDED(pShellItem->QueryInterface(IID_PPV_ARGS(&pFactory)))) {
                SIZE szMed = { 48, 48 };
                pFactory->GetImage(szMed, SIIGBF_BIGGERSIZEOK, &hThumbMed);

                SIZE szBig = { 96, 96 };
                pFactory->GetImage(szBig, SIIGBF_BIGGERSIZEOK, &hThumbBig);

                pFactory->Release();
            }
            pShellItem->Release();
        }

        if (hThumbMed) {
            ImageList_Add(g_hImageListMedium, hThumbMed, NULL);
            DeleteObject(hThumbMed);
        } else {
            SHFILEINFOW sfiLarge = {0};
            SHGetFileInfoW(fi.fullPath.c_str(), 0, &sfiLarge, sizeof(sfiLarge), SHGFI_ICON | SHGFI_LARGEICON);
            if (sfiLarge.hIcon) {
                ImageList_AddIcon(g_hImageListMedium, sfiLarge.hIcon);
                DestroyIcon(sfiLarge.hIcon);
            }
        }

        if (hThumbBig) {
            ImageList_Add(g_hImageListBig, hThumbBig, NULL);
            DeleteObject(hThumbBig);
        } else {
            SHFILEINFOW sfiLarge = {0};
            SHGetFileInfoW(fi.fullPath.c_str(), 0, &sfiLarge, sizeof(sfiLarge), SHGFI_ICON | SHGFI_LARGEICON);
            if (sfiLarge.hIcon) {
                ImageList_AddIcon(g_hImageListBig, sfiLarge.hIcon);
                DestroyIcon(sfiLarge.hIcon);
            }
        }

        LVITEMW lvi = {0};
        lvi.mask = LVIF_TEXT | LVIF_IMAGE;
        lvi.pszText = const_cast<LPWSTR>(fi.name.c_str());
        lvi.iItem = static_cast<int>(i);
        lvi.iImage = static_cast<int>(i);
        SendMessageW(g_hListView, LVM_INSERTITEMW, 0, reinterpret_cast<LPARAM>(&lvi));
    }

    SendMessageW(g_hListView, WM_SETREDRAW, TRUE, 0);
    UpdateListViewLayout();
    InvalidateRect(g_hListView, NULL, TRUE);
}

/**
 * @brief Loads and sorts all files contained within the specified folder.
 */
void LoadFolderFiles(const std::wstring& folderPath) {
    LOG_INFO(L"Scanning directory: " + folderPath);
    g_currentFiles.clear();

    namespace fs = std::filesystem;
    std::vector<FileItem> items;

    try {
        for (const auto& entry : fs::directory_iterator(folderPath)) {
            if (entry.is_regular_file()) {
                FileItem fi;
                fi.name = entry.path().filename().wstring();
                fi.fullPath = entry.path().wstring();
                fi.lastWriteTime = entry.last_write_time();
                items.push_back(fi);
            }
        }
    } catch (const std::exception& ex) {
        LOG_ERROR(L"Directory scan error: " + folderPath);
    }

    std::sort(items.begin(), items.end(), [](const FileItem& a, const FileItem& b) {
        return a.lastWriteTime > b.lastWriteTime;
    });

    g_currentFiles = items;
    ApplySearchFilter();
    LOG_INFO(L"Loaded " + std::to_wstring(g_currentFiles.size()) + L" file(s)");
}

/**
 * @brief Clears dynamically allocated directory path strings used by TreeView.
 */
void ClearTreeAllocations() {
    for (auto* p : g_treeAllocatedPaths) {
        delete p;
    }
    g_treeAllocatedPaths.clear();
}

/**
 * @brief Populates the Windows XP-style folder tree with the Downloads root.
 */
void PopulateFolderTree() {
    TreeView_DeleteAllItems(g_hTreeView);
    ClearTreeAllocations();

    TVINSERTSTRUCTW tvis = {0};
    tvis.hParent = TVI_ROOT;
    tvis.hInsertAfter = TVI_LAST;
    tvis.item.mask = TVIF_TEXT | TVIF_IMAGE | TVIF_SELECTEDIMAGE | TVIF_CHILDREN | TVIF_PARAM;
    tvis.item.pszText = (LPWSTR)L"Downloads";
    tvis.item.iImage = 0;
    tvis.item.iSelectedImage = 0;
    tvis.item.cChildren = 1;

    std::wstring* pRootPath = new std::wstring(g_downloadsPath);
    g_treeAllocatedPaths.push_back(pRootPath);
    tvis.item.lParam = reinterpret_cast<LPARAM>(pRootPath);

    HTREEITEM hRoot = TreeView_InsertItem(g_hTreeView, &tvis);
    PopulateSubfolders(hRoot, g_downloadsPath);
    TreeView_Expand(g_hTreeView, hRoot, TVE_EXPAND);
    TreeView_SelectItem(g_hTreeView, hRoot);
}

/**
 * @brief Populates child subfolders under a specific tree node.
 */
void PopulateSubfolders(HTREEITEM hParent, const std::wstring& parentPath) {
    namespace fs = std::filesystem;
    try {
        for (const auto& entry : fs::directory_iterator(parentPath)) {
            if (entry.is_directory()) {
                std::wstring folderName = entry.path().filename().wstring();

                bool hasChildren = false;
                try {
                    for (const auto& sub : fs::directory_iterator(entry.path())) {
                        if (sub.is_directory()) { hasChildren = true; break; }
                    }
                } catch (...) {}

                TVINSERTSTRUCTW tvis = {0};
                tvis.hParent = hParent;
                tvis.hInsertAfter = TVI_LAST;
                tvis.item.mask = TVIF_TEXT | TVIF_IMAGE | TVIF_SELECTEDIMAGE | TVIF_CHILDREN | TVIF_PARAM;
                tvis.item.pszText = const_cast<LPWSTR>(folderName.c_str());
                tvis.item.iImage = 0;
                tvis.item.iSelectedImage = 0;
                tvis.item.cChildren = hasChildren ? 1 : 0;

                std::wstring* pSubPath = new std::wstring(entry.path().wstring());
                g_treeAllocatedPaths.push_back(pSubPath);
                tvis.item.lParam = reinterpret_cast<LPARAM>(pSubPath);

                TreeView_InsertItem(g_hTreeView, &tvis);
            }
        }
    } catch (...) {}
}

/**
 * @brief Displays context menu for ListView control.
 */
void ShowListViewContextMenu(POINT pt) {
    LVHITTESTINFO hti = {0};
    hti.pt = pt;
    ScreenToClient(g_hListView, &hti.pt);
    int hitItem = ListView_HitTest(g_hListView, &hti);

    if (hitItem >= 0) {
        if (!(ListView_GetItemState(g_hListView, hitItem, LVIS_SELECTED) & LVIS_SELECTED)) {
            ListView_SetItemState(g_hListView, -1, 0, LVIS_SELECTED);
            ListView_SetItemState(g_hListView, hitItem, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        }
    }

    std::vector<int> selected = GetSelectedListViewIndices();
    bool hasSelection = !selected.empty();

    HMENU hMenu = CreatePopupMenu();
    if (hasSelection) {
        InsertMenuW(hMenu, 0, MF_BYPOSITION | MF_STRING, IDM_OPEN_FILE, L"Open / View");
        SetMenuDefaultItem(hMenu, IDM_OPEN_FILE, FALSE);
        InsertMenuW(hMenu, 1, MF_BYPOSITION | MF_STRING, IDM_COPY_FILE, L"Copy\tCtrl+C");
        InsertMenuW(hMenu, 2, MF_BYPOSITION | MF_STRING, IDM_RENAME_FILE, L"Rename\tF2");
        InsertMenuW(hMenu, 3, MF_BYPOSITION | MF_STRING, IDM_DELETE_FILE, L"Delete\tDel");
        InsertMenuW(hMenu, 4, MF_BYPOSITION | MF_SEPARATOR, 0, NULL);
    }

    InsertMenuW(hMenu, -1, MF_BYPOSITION | MF_STRING, IDM_PASTE_FILE, L"Paste\tCtrl+V");

    // Submenu for 3 view modes
    HMENU hViewSub = CreatePopupMenu();
    InsertMenuW(hViewSub, 0, MF_BYPOSITION | MF_STRING | (g_viewMode == ViewMode::Small ? MF_CHECKED : 0), IDM_VIEW_SMALL, L"Small (List)");
    InsertMenuW(hViewSub, 1, MF_BYPOSITION | MF_STRING | (g_viewMode == ViewMode::Medium ? MF_CHECKED : 0), IDM_VIEW_MEDIUM, L"Medium (2-Column Grid)");
    InsertMenuW(hViewSub, 2, MF_BYPOSITION | MF_STRING | (g_viewMode == ViewMode::Big ? MF_CHECKED : 0), IDM_VIEW_BIG, L"Big (Large Thumbnails)");
    InsertMenuW(hMenu, -1, MF_BYPOSITION | MF_POPUP, (UINT_PTR)hViewSub, L"View Mode");

    InsertMenuW(hMenu, -1, MF_BYPOSITION | MF_STRING, IDM_TOGGLE_TREE, g_showFolderTree ? L"Hide Folder Tree" : L"Show Folder Tree");
    InsertMenuW(hMenu, -1, MF_BYPOSITION | MF_STRING, IDM_OPEN_FOLDER, L"Open Folder in Explorer");
    InsertMenuW(hMenu, -1, MF_BYPOSITION | MF_STRING, IDM_REFRESH, L"Refresh\tF5");
    InsertMenuW(hMenu, -1, MF_BYPOSITION | MF_SEPARATOR, 0, NULL);
    InsertMenuW(hMenu, -1, MF_BYPOSITION | MF_STRING, IDM_EXIT, L"Exit Quicky");

    SetForegroundWindow(g_hWnd);
    TrackPopupMenu(hMenu, TPM_RIGHTBUTTON | TPM_TOPALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, g_hWnd, NULL);
    DestroyMenu(hMenu);
}

/**
 * @brief Displays system tray context menu.
 */
void ShowTrayContextMenu(POINT pt) {
    HMENU hMenu = CreatePopupMenu();

    HMENU hViewSub = CreatePopupMenu();
    InsertMenuW(hViewSub, 0, MF_BYPOSITION | MF_STRING | (g_viewMode == ViewMode::Small ? MF_CHECKED : 0), IDM_VIEW_SMALL, L"Small (List)");
    InsertMenuW(hViewSub, 1, MF_BYPOSITION | MF_STRING | (g_viewMode == ViewMode::Medium ? MF_CHECKED : 0), IDM_VIEW_MEDIUM, L"Medium (2-Column Grid)");
    InsertMenuW(hViewSub, 2, MF_BYPOSITION | MF_STRING | (g_viewMode == ViewMode::Big ? MF_CHECKED : 0), IDM_VIEW_BIG, L"Big (Large Thumbnails)");
    InsertMenuW(hMenu, 0, MF_BYPOSITION | MF_POPUP, (UINT_PTR)hViewSub, L"View Mode");

    InsertMenuW(hMenu, 1, MF_BYPOSITION | MF_STRING, IDM_TOGGLE_TREE, g_showFolderTree ? L"Hide Folder Tree" : L"Show Folder Tree");
    InsertMenuW(hMenu, 2, MF_BYPOSITION | MF_STRING, IDM_OPEN_FOLDER, L"Open Downloads Folder");
    InsertMenuW(hMenu, 3, MF_BYPOSITION | MF_STRING, IDM_REFRESH, L"Refresh Downloads");
    InsertMenuW(hMenu, 4, MF_BYPOSITION | MF_SEPARATOR, 0, NULL);
    InsertMenuW(hMenu, 5, MF_BYPOSITION | MF_STRING, IDM_EXIT, L"Exit Quicky");

    SetForegroundWindow(g_hWnd);
    TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, g_hWnd, NULL);
    DestroyMenu(hMenu);
}

// ==========================================
// Modern Vector Icon Drawing Implementations
// ==========================================

void DrawGridIcon(HDC hdc, int cx, int cy, COLORREF color) {
    HBRUSH hBr = CreateSolidBrush(color);
    HPEN hNullPen = (HPEN)GetStockObject(NULL_PEN);
    HBRUSH hOldBr = (HBRUSH)SelectObject(hdc, hBr);
    HPEN hOldPen = (HPEN)SelectObject(hdc, hNullPen);

    // 2x2 grid tiles
    RoundRect(hdc, cx - 6, cy - 6, cx - 1, cy - 1, 2, 2);
    RoundRect(hdc, cx + 2, cy - 6, cx + 7, cy - 1, 2, 2);
    RoundRect(hdc, cx - 6, cy + 2, cx - 1, cy + 7, 2, 2);
    RoundRect(hdc, cx + 2, cy + 2, cx + 7, cy + 7, 2, 2);

    SelectObject(hdc, hOldBr);
    SelectObject(hdc, hOldPen);
    DeleteObject(hBr);
}

void DrawListIcon(HDC hdc, int cx, int cy, COLORREF color) {
    HBRUSH hBr = CreateSolidBrush(color);
    HPEN hPen = CreatePen(PS_SOLID, 2, color);
    HBRUSH hOldBr = (HBRUSH)SelectObject(hdc, hBr);
    HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);

    // 3 list items: bullet + horizontal line
    for (int i = -1; i <= 1; ++i) {
        int y = cy + (i * 5);
        Ellipse(hdc, cx - 7, y - 2, cx - 3, y + 2);
        MoveToEx(hdc, cx - 1, y, NULL);
        LineTo(hdc, cx + 7, y);
    }

    SelectObject(hdc, hOldBr);
    SelectObject(hdc, hOldPen);
    DeleteObject(hBr);
    DeleteObject(hPen);
}

void DrawBigCardIcon(HDC hdc, int cx, int cy, COLORREF color) {
    HPEN hPen = CreatePen(PS_SOLID, 2, color);
    HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);
    HBRUSH hNullBr = (HBRUSH)GetStockObject(NULL_BRUSH);
    HBRUSH hOldBr = (HBRUSH)SelectObject(hdc, hNullBr);

    // Single large preview card outline
    RoundRect(hdc, cx - 7, cy - 7, cx + 7, cy + 7, 3, 3);

    // Inner picture/mountain silhouette
    MoveToEx(hdc, cx - 4, cy + 3, NULL);
    LineTo(hdc, cx - 1, cy - 2);
    LineTo(hdc, cx + 2, cy + 1);
    LineTo(hdc, cx + 4, cy - 1);
    LineTo(hdc, cx + 5, cy + 3);

    SelectObject(hdc, hOldBr);
    SelectObject(hdc, hOldPen);
    DeleteObject(hPen);
}

void DrawFolderTreeIcon(HDC hdc, int cx, int cy, COLORREF color, bool isActive) {
    HPEN hPen = CreatePen(PS_SOLID, 1, color);
    HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);
    HBRUSH hNullBr = (HBRUSH)GetStockObject(NULL_BRUSH);
    HBRUSH hOldBr = (HBRUSH)SelectObject(hdc, hNullBr);

    // Outer split-pane frame
    RoundRect(hdc, cx - 7, cy - 7, cx + 8, cy + 8, 3, 3);
    // Vertical pane divider
    MoveToEx(hdc, cx - 2, cy - 7, NULL);
    LineTo(hdc, cx - 2, cy + 8);

    // Left pane folder tree branches
    MoveToEx(hdc, cx - 5, cy - 4, NULL);
    LineTo(hdc, cx - 5, cy + 4);
    MoveToEx(hdc, cx - 5, cy - 1, NULL);
    LineTo(hdc, cx - 3, cy - 1);
    MoveToEx(hdc, cx - 5, cy + 3, NULL);
    LineTo(hdc, cx - 3, cy + 3);

    SelectObject(hdc, hOldBr);
    SelectObject(hdc, hOldPen);
    DeleteObject(hPen);
}

void DrawFolderIcon(HDC hdc, int cx, int cy, COLORREF color) {
    HPEN hPen = CreatePen(PS_SOLID, 1, color);
    HBRUSH hBr = CreateSolidBrush(color);
    HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);
    HBRUSH hOldBr = (HBRUSH)SelectObject(hdc, hBr);

    RECT rcTab = { cx - 7, cy - 6, cx - 2, cy - 3 };
    FillRect(hdc, &rcTab, hBr);

    RECT rcBody = { cx - 7, cy - 4, cx + 7, cy + 6 };
    RoundRect(hdc, rcBody.left, rcBody.top, rcBody.right, rcBody.bottom, 2, 2);

    SelectObject(hdc, hOldBr);
    SelectObject(hdc, hOldPen);
    DeleteObject(hBr);
    DeleteObject(hPen);
}

void DrawSyncIcon(HDC hdc, int cx, int cy, COLORREF color) {
    HPEN hPen = CreatePen(PS_SOLID, 2, color);
    HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);
    HBRUSH hNullBr = (HBRUSH)GetStockObject(NULL_BRUSH);
    HBRUSH hOldBr = (HBRUSH)SelectObject(hdc, hNullBr);

    Arc(hdc, cx - 6, cy - 6, cx + 6, cy + 6, cx - 3, cy - 5, cx + 5, cy - 3);

    HBRUSH hFillBr = CreateSolidBrush(color);
    HBRUSH hPrev = (HBRUSH)SelectObject(hdc, hFillBr);
    POINT pts[3] = { { cx + 2, cy - 6 }, { cx + 7, cy - 6 }, { cx + 5, cy - 1 } };
    Polygon(hdc, pts, 3);

    SelectObject(hdc, hPrev);
    SelectObject(hdc, hOldBr);
    SelectObject(hdc, hOldPen);
    DeleteObject(hFillBr);
    DeleteObject(hPen);
}

void DrawMinimizeIcon(HDC hdc, int cx, int cy, COLORREF color) {
    HPEN hPen = CreatePen(PS_SOLID, 2, color);
    HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);

    MoveToEx(hdc, cx - 5, cy, NULL);
    LineTo(hdc, cx + 6, cy);

    SelectObject(hdc, hOldPen);
    DeleteObject(hPen);
}
