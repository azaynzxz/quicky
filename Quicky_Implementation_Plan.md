# Quicky: Implementation Plan

## 1. Overview and Core Objectives
Quicky is a lightweight, high-performance, native Windows application that resides in the system tray. Its primary function is to provide instant access to the most recent files in the user's Downloads folder. 

**Key Features:**
* Native Windows C++ executable (no Electron, no web views).
* System tray integration for quick access.
* Live monitoring of the Downloads folder.
* View modes: Simple list or 2-column thumbnail view.
* Full Drag-and-Drop (OLE D&D) support (inbound and outbound).
* File renaming capabilities.
* Minimalist, instantaneous UI.

## 2. Technology Stack
* **Language:** C++17 or C++20.
* **UI Framework:** Win32 API with WTL (Windows Template Library) or raw Win32 for maximum performance, minimal binary size, and zero bloat.
* **Build System:** CMake + MSVC (Visual Studio).
* **Rendering:** GDI or Direct2D (for high-DPI scaling and smooth thumbnail rendering).

## 3. Architecture & Core Components

### A. System Tray Management (`Shell_NotifyIcon`)
* Register a hidden message-only window to listen for tray icon clicks (left-click to toggle main window, right-click for a minimal context menu).
* Keep the app running in the background with minimal memory footprint.

### B. File System Monitoring (`ReadDirectoryChangesW`)
* Use a background worker thread calling `ReadDirectoryChangesW` on the `CSIDL_PERSONAL` / `FOLDERID_Downloads` path.
* Maintain a thread-safe data structure sorted by creation/modification date (newest first).

### C. UI Elements & Layout
* **Main Window:** A small, floating widget-style window (resembling a sticky note for files). It will use the `WS_EX_TOPMOST` extended style to remain always on top of other applications.
* **Click-Through Behavior:** Empty areas of the widget will allow the user to click through to the applications below it. This is achievable using `WS_EX_LAYERED` and `WS_EX_TRANSPARENT` combined with color keying, or by custom handling of `WM_NCHITTEST`.
* **Minimize Button:** A visible button on the floating widget that minimizes it back to the system tray.
* **ListView Control:** Use the native Win32 `SysListView32` control. It supports both "Details/List" view and "Icon" (Thumbnail) view natively.
* **Thumbnails:** Use the `IShellItemImageFactory` interface to request cached thumbnails directly from the Windows Shell. This avoids manually parsing images and supports all file types Windows knows how to render.

### D. Drag and Drop (OLE)
* **Drag Out (Source):** Implement `IDataObject` and `IDropSource`. When the user drags an item from the ListView, initiate `DoDragDrop` passing the `CF_HDROP` format (list of file paths).
* **Drag In (Target):** Implement `IDropTarget` on the main window. Accept `CF_HDROP` data. When dropped, copy/move the files to the monitored Downloads folder using `SHFileOperation` or `IFileOperation`.

### E. File Renaming
* Enable the `LVS_EDITLABELS` style on the ListView control.
* Handle the `LVN_ENDLABELEDIT` notification to physically rename the file on disk using `MoveFileW`.

## 4. Development Phases

### Phase 1: Project Setup and System Tray
* Initialize CMake project.
* Create a Win32 application entry point (`WinMain`).
* Add the system tray icon using `Shell_NotifyIcon`.
* Implement a basic hidden window to capture tray clicks and a stub main window that shows/hides on click.

### Phase 2: File Scanning and ListView
* Retrieve the path to the current user's Downloads folder using `SHGetKnownFolderPath(FOLDERID_Downloads)`.
* Scan the folder and populate a `SysListView32` control.
* Implement a toggle mechanism (button or hotkey) to switch the ListView between List mode and Icon mode (2-column layout).

### Phase 3: Thumbnails and Live Updates
* Implement an asynchronous thumbnail loader using `IShellItemImageFactory`. Attach these images to the ListView's ImageList.
* Set up the background thread with `ReadDirectoryChangesW` to automatically refresh the ListView when new files are downloaded.

### Phase 4: Drag and Drop Implementation
* Implement OLE initialization (`OleInitialize`).
* Build the `IDataObject` to allow dragging files out of the Quicky window to the Desktop or other apps.
* Build the `IDropTarget` to allow dropping files from Explorer into the Quicky window (which copies them to the Downloads folder).

### Phase 5: Renaming and Polish
* Add file renaming functionality via ListView in-place editing.
* Refine the UI (colors, margins, dark mode support if desired via `DwmSetWindowAttribute`).
* Optimize startup time and minimize memory usage.

## 5. Known Challenges & Solutions
* **DPI Awareness:** Must declare the application as Per-Monitor DPI Aware v2 in the manifest so the UI doesn't look blurry on modern displays.
* **Thumbnail Loading Blocking the UI:** Loading thumbnails synchronously will freeze the app. Must use a background thread or async COM calls to load thumbnails into the ImageList.
* **Focus Management:** When clicking the tray icon, the window needs to gain foreground focus properly (`SetForegroundWindow`) so it can be dismissed if the user clicks away (handling `WM_KILLFOCUS`).
