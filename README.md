# Quicky

Quicky is a native, ultra-lightweight Windows system tray utility built for creative professionals (video editors, 3D artists, graphic designers, and motion designers) who are tired of constantly juggling between File Explorer windows and editing suites.

Download an asset in your browser, click Quicky in your taskbar, and drag it directly into Premiere Pro, DaVinci Resolve, After Effects, Photoshop, Blender, or Figma.

---

## Why Quicky?

Creative workflows demand hundreds of assets a day: sound effects, b-roll footage, stock photos, reference images, textures, 3D models, and project archives. 

Switching windows to open File Explorer, scrolling down crowded Downloads folders, and dragging files back and forth breaks focus.

Quicky solves this by living quietly in your system tray:
- **Instant OLE Drag and Drop**: Drag files directly from Quicky straight onto editing timelines, composition canvases, and project bins.
- **Three Visual View Modes**:
  - **Small (List)**: Compact list with small icons for scanning large numbers of files.
  - **Medium (2-Column Grid)**: Balanced 2-column tile layout with 48x48 thumbnails.
  - **Big (Large Previews)**: 96x96 high-resolution shell thumbnails for visual previews of images, textures, videos, and PDFs.
- **Windows XP-Style Folder Tree**: Expandable side-pane for organizing and navigating subfolders without opening File Explorer windows.
- **Real-Time Search (`Ctrl+F`)**: Instant case-insensitive filtering as you type. Press Escape to clear and Enter to open.
- **Zero Bloat & Peak Performance**: Standalone native Win32/C++ executable (~160 KB). Zero Electron, zero WebView2, zero background CPU usage.
- **Clipboard & File Operations**: Copy (`Ctrl+C`), paste (`Ctrl+V`), in-place rename (`F2`), and safe delete to Recycle Bin (`Del`).
- **Live Downloads Monitoring**: Automatically syncs the moment a download finishes via background directory watching.

---

## Previews

### 2-Column Grid Mode with Folder Tree
![2-Column Tile Grid with Folder Tree](screenshoot/full%20view_tile%20mode.png)

### Compact Sticky View
![Compact Sticky Floating View](screenshoot/small%20view.png)

### List Mode with Folder Tree
![List Mode with Folder Tree](screenshoot/full%20view_list%20mode.png)

### Instant Search Filter
![Instant Search Filter](screenshoot/search%20functionality.png)

---

## Keyboard Shortcuts

| Shortcut | Action |
| :--- | :--- |
| `Ctrl + F` | Jump focus to the Search box |
| `Ctrl + T` | Cycle view mode: Small -> Medium -> Big |
| `Ctrl + C` | Copy selected file(s) to Windows clipboard |
| `Ctrl + V` | Paste copied file(s) into active folder |
| `F2` | In-place file rename |
| `F5` | Refresh current folder |
| `Delete` | Move selected file to Recycle Bin |
| `Enter` | Open selected file in default system application |
| `Escape` | Clear search query / Dismiss focus |

---

## Installation & Downloads

### Option 1: Windows Installer (Recommended)
1. Download `Quicky-v1.0.0-Setup.exe` from the [Releases](https://github.com/) page.
2. Run the installer (runs at user level, zero administrator/UAC prompt required).
3. The installer automatically provides options to:
   - Create a **Start Menu** shortcut.
   - Launch Quicky automatically when **Windows starts**.
   - Create a **Desktop** shortcut.
4. Finish and Quicky launches directly into your system tray.

### Option 2: Portable Standalone Archive
1. Download `Quicky-v1.0.0-windows-x64.zip` from [Releases](https://github.com/).
2. Extract `Quicky.exe` to any folder of your choice.
3. Double-click `Quicky.exe` to run. No installation or dependencies needed.

---

## Building from Source

### Prerequisites
- Windows 10 or Windows 11 (x64)
- CMake 3.15 or newer
- Microsoft Visual Studio 2019/2022 (with Desktop Development with C++ workload)

### Build Steps
```cmd
git clone https://github.com/your-username/quicky.git
cd quicky
mkdir build
cd build
cmake ..
cmake --build . --config Release
```

The compiled binary will be generated at:
```text
build\Release\Quicky.exe
```

---

## Architecture & Technology Stack

- **Language**: C++17
- **UI Framework**: Pure Win32 API and Common Controls v6 (`SysListView32`, `SysTreeView32`, `WC_EDITW`)
- **Graphics**: Direct GDI vector drawing with Per-Monitor DPI awareness
- **Thumbnails**: Windows Shell Thumbnail API (`IShellItemImageFactory`)
- **Inter-Process Data Transfer**: Native OLE Data Object implementation (`IDataObject`, `IDropSource`, `IDropTarget`)
- **Directory Monitoring**: Asynchronous `ReadDirectoryChangesW` worker thread with debounced notifications

---

## License

This project is licensed under the MIT License. See the [LICENSE](LICENSE) file for details.
